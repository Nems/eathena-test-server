// $Id: pc.c 101 2004-09-25 17:57:22Z Valaris $
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "socket.h"             // [Valaris]
#include "timer.h"
#include "db.h"

#include "malloc.h"
#include "nullpo.h"

#include "atcommand.h"
#include "battle.h"
#include "chat.h"
#include "chrif.h"
#include "clif.h"
#include "guild.h"
#include "intif.h"
#include "itemdb.h"
#include "map.h"
#include "mob.h"
#include "npc.h"
#include "party.h"
#include "pc.h"
#include "script.h"
#include "skill.h"
#include "storage.h"
#include "trade.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

#define PVP_CALCRANK_INTERVAL 1000  // PVP���ʌv�Z�̊Ԋu

#define STATE_BLIND 0x10

#ifdef USE_ASTRAL_SOUL_SKILL
#define MAGIC_SKILL_THRESHOLD 200   // [fate] At this threshold, the Astral Soul skill kicks in
#endif

#define MAP_LOG_STATS(sd, suffix)	\
        MAP_LOG_PC(sd, "STAT %d %d %d %d %d %d " suffix,            \
                   sd->status.str, sd->status.agi, sd->status.vit, sd->status.int_, sd->status.dex, sd->status.luk)

#define MAP_LOG_XP(sd, suffix)	\
        MAP_LOG_PC(sd, "XP %d %d JOB %d %d %d ZENY %d + %d " suffix,		\
                   sd->status.base_level, sd->status.base_exp, sd->status.job_level, sd->status.job_exp, sd->status.skill_point,  sd->status.zeny, pc_readaccountreg(sd, "BankAccount"))

#define MAP_LOG_MAGIC(sd, suffix)	\
        MAP_LOG_PC(sd, "MAGIC %d %d %d %d %d %d EXP %d %d " suffix,	\
                   sd->status.skill[TMW_MAGIC].lv,                      \
                   sd->status.skill[TMW_MAGIC_LIFE].lv,			\
                   sd->status.skill[TMW_MAGIC_WAR].lv,			\
                   sd->status.skill[TMW_MAGIC_TRANSMUTE].lv,		\
                   sd->status.skill[TMW_MAGIC_NATURE].lv,               \
                   sd->status.skill[TMW_MAGIC_ETHER].lv,                \
                   pc_readglobalreg(sd, "MAGIC_EXPERIENCE") & 0xffff,	\
                   (pc_readglobalreg(sd, "MAGIC_EXPERIENCE") >> 24) & 0xff)

static int max_weight_base[MAX_PC_CLASS];
static int hp_coefficient[MAX_PC_CLASS];
static int hp_coefficient2[MAX_PC_CLASS];
static int hp_sigma_val[MAX_PC_CLASS][MAX_LEVEL];
static int sp_coefficient[MAX_PC_CLASS];
static int aspd_base[MAX_PC_CLASS][20];
static char job_bonus[3][MAX_PC_CLASS][MAX_LEVEL];
static int exp_table[14][MAX_LEVEL];
static char statp[255][7];
static struct
{
    int  id;
    int  max;
    struct
    {
        short id, lv;
    } need[6];
} skill_tree[3][MAX_PC_CLASS][100];

static int atkmods[3][20];      // ����ATK�T�C�Y�C��(size_fix.txt)
static int refinebonus[5][3];   // ���B�{�[�i�X�e�[�u��(refine_db.txt)
static int percentrefinery[5][10];  // ���B������(refine_db.txt)

static int dirx[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };
static int diry[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };

static unsigned int equip_pos[11] =
    { 0x0080, 0x0008, 0x0040, 0x0004, 0x0001, 0x0200, 0x0100, 0x0010, 0x0020,
    0x0002, 0x8000
};

//static struct dbt *gm_account_db;
static struct gm_account *gm_account = NULL;
static int GM_num = 0;

int pc_isGM (struct map_session_data *sd)
{
//  struct gm_account *p;
    int  i;

    nullpo_retr (0, sd);

/*	p = numdb_search(gm_account_db, sd->status.account_id);
	if (p == NULL)
		return 0;
	return p->level;*/

    for (i = 0; i < GM_num; i++)
        if (gm_account[i].account_id == sd->status.account_id)
            return gm_account[i].level;
    return 0;

}

int pc_iskiller (struct map_session_data *src,
                 struct map_session_data *target)
{
    nullpo_retr (0, src);

    if (src->bl.type != BL_PC)
        return 0;
    if (src->special_state.killer)
        return 1;

    if (target->bl.type != BL_PC)
        return 0;
    if (target->special_state.killable)
        return 1;

    return 0;
}

int pc_set_gm_level (int account_id, int level)
{
    int  i;
    for (i = 0; i < GM_num; i++)
    {
        if (account_id == gm_account[i].account_id)
        {
            gm_account[i].level = level;
            return 0;
        }
    }

    GM_num++;
    gm_account = realloc (gm_account, sizeof (struct gm_account) * GM_num);
    gm_account[GM_num - 1].account_id = account_id;
    gm_account[GM_num - 1].level = level;
    return 0;
}

int pc_getrefinebonus (int lv, int type)
{
    if (lv >= 0 && lv < 5 && type >= 0 && type < 3)
        return refinebonus[lv][type];
    return 0;
}

static int distance (int x0, int y0, int x1, int y1)
{
    int  dx, dy;

    dx = abs (x0 - x1);
    dy = abs (y0 - y1);
    return dx > dy ? dx : dy;
}

static int pc_invincible_timer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd;

    if ((sd = (struct map_session_data *) map_id2sd (id)) == NULL
        || sd->bl.type != BL_PC)
        return 1;

    if (sd->invincible_timer != tid)
    {
        if (battle_config.error_log)
            printf ("invincible_timer %d != %d\n", sd->invincible_timer, tid);
        return 0;
    }
    sd->invincible_timer = -1;

    return 0;
}

int pc_setinvincibletimer (struct map_session_data *sd, int val)
{
    nullpo_retr (0, sd);

    if (sd->invincible_timer != -1)
        delete_timer (sd->invincible_timer, pc_invincible_timer);
    sd->invincible_timer =
        add_timer (gettick () + val, pc_invincible_timer, sd->bl.id, 0);
    return 0;
}

int pc_delinvincibletimer (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->invincible_timer != -1)
    {
        delete_timer (sd->invincible_timer, pc_invincible_timer);
        sd->invincible_timer = -1;
    }
    return 0;
}

static int pc_spiritball_timer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd;
    int  i;

    if ((sd = (struct map_session_data *) map_id2sd (id)) == NULL
        || sd->bl.type != BL_PC)
        return 1;

    if (sd->spirit_timer[0] != tid)
    {
        if (battle_config.error_log)
            printf ("spirit_timer %d != %d\n", sd->spirit_timer[0], tid);
        return 0;
    }
    sd->spirit_timer[0] = -1;
    for (i = 1; i < sd->spiritball; i++)
    {
        sd->spirit_timer[i - 1] = sd->spirit_timer[i];
        sd->spirit_timer[i] = -1;
    }
    sd->spiritball--;
    if (sd->spiritball < 0)
        sd->spiritball = 0;
    clif_spiritball (sd);

    return 0;
}

int pc_addspiritball (struct map_session_data *sd, int interval, int max)
{
    int  i;

    nullpo_retr (0, sd);

    if (max > MAX_SKILL_LEVEL)
        max = MAX_SKILL_LEVEL;
    if (sd->spiritball < 0)
        sd->spiritball = 0;

    if (sd->spiritball >= max)
    {
        if (sd->spirit_timer[0] != -1)
        {
            delete_timer (sd->spirit_timer[0], pc_spiritball_timer);
            sd->spirit_timer[0] = -1;
        }
        for (i = 1; i < max; i++)
        {
            sd->spirit_timer[i - 1] = sd->spirit_timer[i];
            sd->spirit_timer[i] = -1;
        }
    }
    else
        sd->spiritball++;

    sd->spirit_timer[sd->spiritball - 1] =
        add_timer (gettick () + interval, pc_spiritball_timer, sd->bl.id, 0);
    clif_spiritball (sd);

    return 0;
}

int pc_delspiritball (struct map_session_data *sd, int count, int type)
{
    int  i;

    nullpo_retr (0, sd);

    if (sd->spiritball <= 0)
    {
        sd->spiritball = 0;
        return 0;
    }

    if (count > sd->spiritball)
        count = sd->spiritball;
    sd->spiritball -= count;
    if (count > MAX_SKILL_LEVEL)
        count = MAX_SKILL_LEVEL;

    for (i = 0; i < count; i++)
    {
        if (sd->spirit_timer[i] != -1)
        {
            delete_timer (sd->spirit_timer[i], pc_spiritball_timer);
            sd->spirit_timer[i] = -1;
        }
    }
    for (i = count; i < MAX_SKILL_LEVEL; i++)
    {
        sd->spirit_timer[i - count] = sd->spirit_timer[i];
        sd->spirit_timer[i] = -1;
    }

    if (!type)
        clif_spiritball (sd);

    return 0;
}

int pc_setrestartvalue (struct map_session_data *sd, int type)
{
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    s_class = pc_calc_base_job (sd->status.class);

    //-----------------------
    // ���S����
    if (sd->special_state.restart_full_recover)
    {                           // �I�V���X�J�[�h
        sd->status.hp = sd->status.max_hp;
        sd->status.sp = sd->status.max_sp;
    }
    else
    {
        if (s_class.job == 0 && battle_config.restart_hp_rate < 50)
        {                       //�m�r�͔�������
            sd->status.hp = (sd->status.max_hp) / 2;
        }
        else
        {
            if (battle_config.restart_hp_rate <= 0)
                sd->status.hp = 1;
            else
            {
                sd->status.hp =
                    sd->status.max_hp * battle_config.restart_hp_rate / 100;
                if (sd->status.hp <= 0)
                    sd->status.hp = 1;
            }
        }
        if (battle_config.restart_sp_rate > 0)
        {
            int  sp = sd->status.max_sp * battle_config.restart_sp_rate / 100;
            if (sd->status.sp < sp)
                sd->status.sp = sp;
        }
    }
    if (type & 1)
        clif_updatestatus (sd, SP_HP);
    if (type & 1)
        clif_updatestatus (sd, SP_SP);

    /* removed exp penalty on spawn [Valaris] */

    if (type & 2 && sd->status.class != 0 && battle_config.zeny_penalty > 0
        && !map[sd->bl.m].flag.nozenypenalty)
    {
        int  zeny =
            (int) ((double) sd->status.zeny *
                   (double) battle_config.zeny_penalty / 10000.);
        if (zeny < 1)
            zeny = 1;
        sd->status.zeny -= zeny;
        if (sd->status.zeny < 0)
            sd->status.zeny = 0;
        clif_updatestatus (sd, SP_ZENY);
    }
    sd->heal_xp = 0;            // [Fate] Set gainable xp for healing this player to 0

    return 0;
}

/*==========================================
 * ������b�N���Ă���MOB�̐��𐔂���(foreachclient)
 *------------------------------------------
 */
static int pc_counttargeted_sub (struct block_list *bl, va_list ap)
{
    int  id, *c, target_lv;
    struct block_list *src;

    nullpo_retr (0, bl);
    nullpo_retr (0, ap);

    id = va_arg (ap, int);

    nullpo_retr (0, c = va_arg (ap, int *));

    src = va_arg (ap, struct block_list *);
    target_lv = va_arg (ap, int);
    if (id == bl->id || (src && id == src->id))
        return 0;
    if (bl->type == BL_PC)
    {
        struct map_session_data *sd = (struct map_session_data *) bl;
        if (sd && sd->attacktarget == id && sd->attacktimer != -1
            && sd->attacktarget_lv >= target_lv)
            (*c)++;
    }
    else if (bl->type == BL_MOB)
    {
        struct mob_data *md = (struct mob_data *) bl;
        if (md && md->target_id == id && md->timer != -1
            && md->state.state == MS_ATTACK && md->target_lv >= target_lv)

            (*c)++;
        //printf("md->target_lv:%d, target_lv:%d\n",((struct mob_data *)bl)->target_lv,target_lv);
    }
    return 0;
}

int pc_counttargeted (struct map_session_data *sd, struct block_list *src,
                      int target_lv)
{
    int  c = 0;
    map_foreachinarea (pc_counttargeted_sub, sd->bl.m,
                       sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
                       sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE, 0,
                       sd->bl.id, &c, src, target_lv);
    return c;
}

/*==========================================
 * ���[�J���v���g�^�C�v�錾 (�K�v�ȕ��̂�)
 *------------------------------------------
 */
static int pc_walktoxy_sub (struct map_session_data *);

/*==========================================
 * save�ɕK�v�ȃX�e�[�^�X�C�����s�Ȃ�
 *------------------------------------------
 */
int pc_makesavestatus (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    // ���̐F�͐F�X���Q�������̂ŕۑ��Ώۂɂ͂��Ȃ�
    if (!battle_config.save_clothcolor)
        sd->status.clothes_color = 0;

    // ���S���Ԃ�����̂�hp��1�A�ʒu���Z�[�u�ꏊ�ɕύX
    if (pc_isdead (sd))
    {
        pc_setrestartvalue (sd, 0);
        memcpy (&sd->status.last_point, &sd->status.save_point,
                sizeof (sd->status.last_point));
    }
    else
    {
        memcpy (sd->status.last_point.map, sd->mapname, 24);
        sd->status.last_point.x = sd->bl.x;
        sd->status.last_point.y = sd->bl.y;
    }

    // �Z�[�u�֎~�}�b�v������̂Ŏw���ʒu�Ɉړ�
    if (map[sd->bl.m].flag.nosave)
    {
        struct map_data *m = &map[sd->bl.m];
        if (strcmp (m->save.map, "SavePoint") == 0)
            memcpy (&sd->status.last_point, &sd->status.save_point,
                    sizeof (sd->status.last_point));
        else
            memcpy (&sd->status.last_point, &m->save,
                    sizeof (sd->status.last_point));
    }

    //�}�i�[�|�C���g���v���X������ꍇ0��
    if (battle_config.muting_players && sd->status.manner > 0)
        sd->status.manner = 0;
    return 0;
}

/*==========================================
 * �ڑ����̏�����
 *------------------------------------------
 */
int pc_setnewpc (struct map_session_data *sd, int account_id, int char_id,
                 int login_id1, int client_tick, int sex, int fd)
{
    nullpo_retr (0, sd);

    sd->bl.id = account_id;
    sd->char_id = char_id;
    sd->login_id1 = login_id1;
    sd->login_id2 = 0;          // at this point, we can not know the value :(
    sd->client_tick = client_tick;
    sd->sex = sex;
    sd->state.auth = 0;
    sd->bl.type = BL_PC;
    sd->canact_tick = sd->canmove_tick = gettick ();
    sd->canlog_tick = gettick ();
    sd->state.waitingdisconnect = 0;

    return 0;
}

int pc_equippoint (struct map_session_data *sd, int n)
{
    int  ep = 0;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    if (!sd->inventory_data[n])
        return 0;

    s_class = pc_calc_base_job (sd->status.class);

    ep = sd->inventory_data[n]->equip;
    if ((sd->inventory_data[n]->look == 1 || sd->inventory_data[n]->look == 2
         || sd->inventory_data[n]->look == 6) && (ep == 2
                                                  &&
                                                  (pc_checkskill (sd, AS_LEFT)
                                                   > 0 || s_class.job == 12)))
    {
        return 34;
    }

    return ep;
}

int pc_setinventorydata (struct map_session_data *sd)
{
    int  i, id;

    nullpo_retr (0, sd);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        id = sd->status.inventory[i].nameid;
        sd->inventory_data[i] = itemdb_search (id);
    }
    return 0;
}

int pc_calcweapontype (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->weapontype1 != 0 && sd->weapontype2 == 0)
        sd->status.weapon = sd->weapontype1;
    if (sd->weapontype1 == 0 && sd->weapontype2 != 0)   // ���蕐�� Only
        sd->status.weapon = sd->weapontype2;
    else if (sd->weapontype1 == 1 && sd->weapontype2 == 1)  // �o�Z��
        sd->status.weapon = 0x11;
    else if (sd->weapontype1 == 2 && sd->weapontype2 == 2)  // �o�P�茕
        sd->status.weapon = 0x12;
    else if (sd->weapontype1 == 6 && sd->weapontype2 == 6)  // �o�P�蕀
        sd->status.weapon = 0x13;
    else if ((sd->weapontype1 == 1 && sd->weapontype2 == 2) || (sd->weapontype1 == 2 && sd->weapontype2 == 1))  // �Z�� - �P�茕
        sd->status.weapon = 0x14;
    else if ((sd->weapontype1 == 1 && sd->weapontype2 == 6) || (sd->weapontype1 == 6 && sd->weapontype2 == 1))  // �Z�� - ��
        sd->status.weapon = 0x15;
    else if ((sd->weapontype1 == 2 && sd->weapontype2 == 6) || (sd->weapontype1 == 6 && sd->weapontype2 == 2))  // �P�茕 - ��
        sd->status.weapon = 0x16;
    else
        sd->status.weapon = sd->weapontype1;

    return 0;
}

int pc_setequipindex (struct map_session_data *sd)
{
    int  i, j;

    nullpo_retr (0, sd);

    for (i = 0; i < 11; i++)
        sd->equip_index[i] = -1;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid <= 0)
            continue;
        if (sd->status.inventory[i].equip)
        {
            for (j = 0; j < 11; j++)
                if (sd->status.inventory[i].equip & equip_pos[j])
                    sd->equip_index[j] = i;
            if (sd->status.inventory[i].equip & 0x0002)
            {
                if (sd->inventory_data[i])
                    sd->weapontype1 = sd->inventory_data[i]->look;
                else
                    sd->weapontype1 = 0;
            }
            if (sd->status.inventory[i].equip & 0x0020)
            {
                if (sd->inventory_data[i])
                {
                    if (sd->inventory_data[i]->type == 4)
                    {
                        if (sd->status.inventory[i].equip == 0x0020)
                            sd->weapontype2 = sd->inventory_data[i]->look;
                        else
                            sd->weapontype2 = 0;
                    }
                    else
                        sd->weapontype2 = 0;
                }
                else
                    sd->weapontype2 = 0;
            }
        }
    }
    pc_calcweapontype (sd);

    return 0;
}

int pc_isequip (struct map_session_data *sd, int n)
{
    struct item_data *item;
    struct status_change *sc_data;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����

    nullpo_retr (0, sd);

    item = sd->inventory_data[n];
    sc_data = battle_get_sc_data (&sd->bl);
    //s_class = pc_calc_base_job(sd->status.class);

    if (battle_config.gm_allequip > 0
        && pc_isGM (sd) >= battle_config.gm_allequip)
        return 1;

    if (item == NULL)
        return 0;
    if (item->sex != 2 && sd->status.sex != item->sex)
        return 0;
    if (item->elv > 0 && sd->status.base_level < item->elv)
        return 0;

    if (map[sd->bl.m].flag.pvp
        && (item->flag.no_equip == 1 || item->flag.no_equip == 3))
        return 0;
    if (map[sd->bl.m].flag.gvg
        && (item->flag.no_equip == 2 || item->flag.no_equip == 3))
        return 0;
    if (item->equip & 0x0002 && sc_data
        && sc_data[SC_STRIPWEAPON].timer != -1)
        return 0;
    if (item->equip & 0x0020 && sc_data
        && sc_data[SC_STRIPSHIELD].timer != -1)
        return 0;
    if (item->equip & 0x0010 && sc_data && sc_data[SC_STRIPARMOR].timer != -1)
        return 0;
    if (item->equip & 0x0100 && sc_data && sc_data[SC_STRIPHELM].timer != -1)
        return 0;
    return 1;
}

/*==========================================
 * Weapon Breaking [Valaris]
 *------------------------------------------
 */
int pc_breakweapon (struct map_session_data *sd)
{
    struct item_data *item;
    char output[255];
    int  i;

    if (sd == NULL)
        return -1;
    if (sd->unbreakable >= MRAND (100))
        return 0;
    if (sd->sc_data && sd->sc_data[SC_CP_WEAPON].timer != -1)
        return 0;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].equip
            && sd->status.inventory[i].equip & 0x0002
            && !sd->status.inventory[i].broken)
        {
            item = sd->inventory_data[i];
            sd->status.inventory[i].broken = 1;
            //pc_unequipitem(sd,i,0);
            if (sd->status.inventory[i].equip
                && sd->status.inventory[i].equip & 0x0002
                && sd->status.inventory[i].broken == 1)
            {
                sprintf (output, "%s has broken.", item->jname);
                clif_emotion (&sd->bl, 23);
                clif_displaymessage (sd->fd, output);
                clif_equiplist (sd);
                skill_status_change_start (&sd->bl, SC_BROKNWEAPON, 0, 0, 0,
                                           0, 0, 0);
            }
        }
        if (sd->status.inventory[i].broken == 1)
            return 0;
    }

    return 0;
}

/*==========================================
 * Armor Breaking [Valaris]
 *------------------------------------------
 */
int pc_breakarmor (struct map_session_data *sd)
{
    struct item_data *item;
    char output[255];
    int  i;

    if (sd == NULL)
        return -1;
    if (sd->unbreakable >= MRAND (100))
        return 0;
    if (sd->sc_data && sd->sc_data[SC_CP_ARMOR].timer != -1)
        return 0;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].equip
            && sd->status.inventory[i].equip & 0x0010
            && !sd->status.inventory[i].broken)
        {
            item = sd->inventory_data[i];
            sd->status.inventory[i].broken = 1;
            //pc_unequipitem(sd,i,0);
            if (sd->status.inventory[i].equip
                && sd->status.inventory[i].equip & 0x0010
                && sd->status.inventory[i].broken == 1)
            {
                sprintf (output, "%s has broken.", item->jname);
                clif_emotion (&sd->bl, 23);
                clif_displaymessage (sd->fd, output);
                clif_equiplist (sd);
                skill_status_change_start (&sd->bl, SC_BROKNARMOR, 0, 0, 0, 0,
                                           0, 0);
            }
        }
        if (sd->status.inventory[i].broken == 1)
            return 0;
    }
    return 0;
}

/*==========================================
 * session id�ɖ��薳��
 * char�I���瑗�����Ă����X�e�[�^�X���ݒ�
 *------------------------------------------
 */
int pc_authok (int id, int login_id2, time_t connect_until_time,
               short tmw_version, struct mmo_charstatus *st)
{
    struct map_session_data *sd = NULL;

    struct party *p;
    struct guild *g;
    int  i;
    unsigned long tick = gettick ();

    sd = map_id2sd (id);
    if (sd == NULL)
        return 1;

    sd->login_id2 = login_id2;
    sd->tmw_version = tmw_version;

    memcpy (&sd->status, st, sizeof (*st));

    if (sd->status.sex != sd->sex)
    {
        clif_authfail_fd (sd->fd, 0);
        return 1;
    }

    MAP_LOG_STATS (sd, "LOGIN");
    MAP_LOG_XP (sd, "LOGIN");
    MAP_LOG_MAGIC (sd, "LOGIN");

    memset (&sd->state, 0, sizeof (sd->state));
    // ���{�I�ȏ�����
    sd->state.connect_new = 1;
    sd->bl.prev = sd->bl.next = NULL;

    sd->weapontype1 = sd->weapontype2 = 0;
    sd->view_class = sd->status.class;
    sd->speed = DEFAULT_WALK_SPEED;
    sd->state.dead_sit = 0;
    sd->dir = 0;
    sd->head_dir = 0;
    sd->state.auth = 1;
    sd->walktimer = -1;
    sd->attacktimer = -1;
    sd->followtimer = -1;       // [MouseJstr]
    sd->skilltimer = -1;
    sd->skillitem = -1;
    sd->skillitemlv = -1;
    sd->invincible_timer = -1;
    sd->sg_count = 0;

    sd->deal_locked = 0;
    sd->trade_partner = 0;

    sd->inchealhptick = 0;
    sd->inchealsptick = 0;
    sd->hp_sub = 0;
    sd->sp_sub = 0;
    sd->quick_regeneration_hp.amount = 0;
    sd->quick_regeneration_sp.amount = 0;
    sd->heal_xp = 0;
    sd->inchealspirithptick = 0;
    sd->inchealspiritsptick = 0;
    sd->canact_tick = tick;
    sd->canmove_tick = tick;
    sd->attackabletime = tick;
    sd->cast_tick = tick;

    sd->doridori_counter = 0;

    sd->spiritball = 0;
    for (i = 0; i < MAX_SKILL_LEVEL; i++)
        sd->spirit_timer[i] = -1;
    for (i = 0; i < MAX_SKILLTIMERSKILL; i++)
        sd->skilltimerskill[i].timer = -1;

    memset (&sd->dev, 0, sizeof (struct square));
    for (i = 0; i < 5; i++)
    {
        sd->dev.val1[i] = 0;
        sd->dev.val2[i] = 0;
    }

    // �A�J�E���g�ϐ��̑��M�v��
    intif_request_accountreg (sd);

    // �A�C�e���`�F�b�N
    pc_setinventorydata (sd);
    pc_checkitem (sd);

    // �X�e�[�^�X�ُ��̏�����
    for (i = 0; i < MAX_STATUSCHANGE; i++)
    {
        sd->sc_data[i].timer = -1;
        sd->sc_data[i].val1 = sd->sc_data[i].val2 = sd->sc_data[i].val3 =
            sd->sc_data[i].val4 = 0;
    }
    sd->sc_count = 0;
    if ((battle_config.atc_gmonly == 0 || pc_isGM (sd)) &&
        (pc_isGM (sd) >= get_atcommand_level (AtCommand_Hide)))
        sd->status.option &= (OPTION_MASK | OPTION_HIDE);
    else
        sd->status.option &= OPTION_MASK;

    // �X�L�����j�b�g�֌W�̏�����
    memset (sd->skillunit, 0, sizeof (sd->skillunit));
    memset (sd->skillunittick, 0, sizeof (sd->skillunittick));

    // init ignore list
    memset (sd->ignore, 0, sizeof (sd->ignore));

    // �p�[�e�B�[�֌W�̏�����
    sd->party_sended = 0;
    sd->party_invite = 0;
    sd->party_x = -1;
    sd->party_y = -1;
    sd->party_hp = -1;

    // �M���h�֌W�̏�����
    sd->guild_sended = 0;
    sd->guild_invite = 0;
    sd->guild_alliance = 0;

    // �C�x���g�֌W�̏�����
    memset (sd->eventqueue, 0, sizeof (sd->eventqueue));
    for (i = 0; i < MAX_EVENTTIMER; i++)
        sd->eventtimer[i] = -1;

    // �ʒu�̐ݒ�
    pc_setpos (sd, sd->status.last_point.map, sd->status.last_point.x,
               sd->status.last_point.y, 0);

    // �p�[�e�B�A�M���h�f�[�^�̗v��
    if (sd->status.party_id > 0
        && (p = party_search (sd->status.party_id)) == NULL)
        party_request_info (sd->status.party_id);
    if (sd->status.guild_id > 0
        && (g = guild_search (sd->status.guild_id)) == NULL)
        guild_request_info (sd->status.guild_id);

    // pvp�̐ݒ�
    sd->pvp_rank = 0;
    sd->pvp_point = 0;
    sd->pvp_timer = -1;

    // �ʒm

    clif_authok (sd);
    map_addnickdb (sd);
    if (map_charid2nick (sd->status.char_id) == NULL)
        map_addchariddb (sd->status.char_id, sd->status.name);

    //�X�p�m�r�p���ɃJ�E���^�[�̃X�N���v�g�ϐ������̓ǂݏo����sd�ւ̃Z�b�g
    sd->die_counter = pc_readglobalreg (sd, "PC_DIE_COUNTER");

    if (night_flag == 1)
    {
        char tmpstr[1024];
        strcpy (tmpstr, msg_txt (500)); // Actually, it's the night...
        clif_wis_message (sd->fd, wisp_server_name, tmpstr,
                          strlen (tmpstr) + 1);
        sd->opt2 |= STATE_BLIND;
    }

    // �X�e�[�^�X�����v�Z�Ȃ�
    pc_calcstatus (sd, 1);

    if (pc_isGM (sd))
    {
        printf
            ("Connection accepted: character '%s' (account: %d; GM level %d).\n",
             sd->status.name, sd->status.account_id, pc_isGM (sd));
        clif_updatestatus (sd, SP_GM);
    }
    else
        printf ("Connection accepted: Character '%s' (account: %d).\n",
                sd->status.name, sd->status.account_id);

    // Message of the Day�̑��M
    {
        char buf[256];
        FILE *fp;
        if ((fp = fopen_ (motd_txt, "r")) != NULL)
        {
            while (fgets (buf, sizeof (buf) - 1, fp) != NULL)
            {
                int  i;
                for (i = 0; buf[i]; i++)
                {
                    if (buf[i] == '\r' || buf[i] == '\n')
                    {
                        buf[i] = 0;
                        break;
                    }
                }
                clif_displaymessage (sd->fd, buf);
            }
            fclose_ (fp);
        }
    }

    sd->auto_ban_info.in_progress = 0;

    // Initialize antispam vars
    sd->chat_reset_due = sd->chat_lines_in = sd->chat_total_repeats =
        sd->chat_repeat_reset_due = 0;
    sd->chat_lastmsg[0] = '\0';

    memset(sd->flood_rates, 0, sizeof(sd->flood_rates));
    sd->packet_flood_reset_due = sd->packet_flood_in = 0;

    // message of the limited time of the account
    if (connect_until_time != 0)
    {                           // don't display if it's unlimited or unknow value
        char tmpstr[1024];
        strftime (tmpstr, sizeof (tmpstr) - 1, msg_txt (501), gmtime (&connect_until_time));    // "Your account time limit is: %d-%m-%Y %H:%M:%S."
        clif_wis_message (sd->fd, wisp_server_name, tmpstr,
                          strlen (tmpstr) + 1);
    }
    pc_calcstatus (sd, 1);

    return 0;
}

/*==========================================
 * session id�ɖ��肠���Ȃ̂Ō��n��
 *------------------------------------------
 */
int pc_authfail (int id)
{
    struct map_session_data *sd;

    sd = map_id2sd (id);
    if (sd == NULL)
        return 1;

    clif_authfail_fd (sd->fd, 0);

    return 0;
}

static int pc_calc_skillpoint (struct map_session_data *sd)
{
    int  i, skill_points = 0;

    nullpo_retr (0, sd);

    for (i = 0; i < skill_pool_skills_size; i++) {
        int lv = sd->status.skill[skill_pool_skills[i]].lv;
        if (lv)
            skill_points += ((lv * (lv - 1)) >> 1) - 1;
    }

    return skill_points;
}

/*==========================================
 * �o���������X�L���̌v�Z
 *------------------------------------------
 */
int pc_calc_skilltree (struct map_session_data *sd)
{
    int  i, id = 0, flag;
    int  c = 0, s = 0;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    s_class = pc_calc_base_job (sd->status.class);
    c = s_class.job;
    s = (s_class.upper == 1) ? 1 : 0;   //�]���ȊO�͒ʏ��̃X�L���H

    if ((battle_config.skillup_limit)
        && ((c >= 0 && c < 23) || (c >= 4001 && c < 4023)
            || (c >= 4023 && c < 4045)))
    {
        int  skill_point = pc_calc_skillpoint (sd);
        if (skill_point < 9)
            c = 0;
        else if ((sd->status.skill_point >= sd->status.job_level
                  && skill_point < 58) && ((c > 6 && c < 23) || (c > 4007
                                                                 && c < 4023)
                                           || (c > 4029 && c < 4045)))
        {
            switch (c)
            {
                case 7:
                case 14:
                    c = 1;
                    break;
                case 8:
                case 15:
                    c = 4;
                    break;
                case 9:
                case 16:
                    c = 2;
                    break;
                case 10:
                case 18:
                    c = 5;
                    break;
                case 11:
                case 19:
                case 20:
                    c = 3;
                    break;
                case 12:
                case 17:
                    c = 6;
                    break;
                case 4008:
                case 4015:
                    c = 4002;
                    break;
                case 4009:
                case 4016:
                    c = 4005;
                    break;
                case 4010:
                case 4017:
                    c = 4003;
                    break;
                case 4011:
                case 4019:
                    c = 4006;
                    break;
                case 4012:
                case 4020:
                case 4021:
                    c = 4004;
                    break;
                case 4013:
                case 4018:
                    c = 4007;
                    break;
                case 4030:
                case 4037:
                    c = 4024;
                    break;
                case 4031:
                case 4038:
                    c = 4027;
                    break;
                case 4032:
                case 4039:
                    c = 4025;
                    break;
                case 4033:
                case 4040:
                    c = 4028;
                    break;
                case 4034:
                case 4041:
                case 4042:
                    c = 4026;
                    break;
                case 4035:
                case 4043:
                    c = 4029;
                    break;

            }
        }
    }

    /*Comment this out for now, as we manage skills differently
     * for(i=0;i<MAX_SKILL;i++)
     * if (i < TMW_MAGIC || i > TMW_MAGIC_END){ // [Fate] This hack gets TMW magic working and persisted without bothering about the skill tree.
     * if (sd->status.skill[i].flag != 13) sd->status.skill[i].id=0;
     * if (sd->status.skill[i].flag && sd->status.skill[i].flag != 13){ // card�X�L���Ȃ��A
     * sd->status.skill[i].lv=(sd->status.skill[i].flag==1)?0:sd->status.skill[i].flag-2;   // �{����lv��
     * sd->status.skill[i].flag=0;  // flag��0�ɂ��Ă���
     * }
     * }
     */

    if (battle_config.gm_allskill > 0
        && pc_isGM (sd) >= battle_config.gm_allskill)
    {
        // �S�ẴX�L��
        for (i = 1; i < 158; i++)
            sd->status.skill[i].id = i;
        for (i = 210; i < 291; i++)
            sd->status.skill[i].id = i;
        for (i = 304; i < 337; i++)
            sd->status.skill[i].id = i;
        if (battle_config.enable_upper_class)
        {                       //conf�Ŗ����łȂ����Γǂݍ���
            for (i = 355; i < MAX_SKILL; i++)
                sd->status.skill[i].id = i;
        }

    }
    else
    {
        // �ʏ��̌v�Z
        do
        {
            flag = 0;
            for (i = 0; (id = skill_tree[s][c][i].id) > 0; i++)
            {
                int  j, f = 1;
                if (!battle_config.skillfree)
                {
                    for (j = 0; j < 5; j++)
                    {
                        if (skill_tree[s][c][i].need[j].id &&
                            pc_checkskill (sd,
                                           skill_tree[s][c][i].need[j].id) <
                            skill_tree[s][c][i].need[j].lv)
                            f = 0;
                    }
                }
                if (f && sd->status.skill[id].id == 0)
                {
                    sd->status.skill[id].id = id;
                    flag = 1;
                }
            }
        }
        while (flag);
    }
//  if(battle_config.etc_log)
//      printf("calc skill_tree\n");
    return 0;
}

/*==========================================
 * �d�ʃA�C�R���̊m�F
 *------------------------------------------
 */
int pc_checkweighticon (struct map_session_data *sd)
{
    int  flag = 0;

    nullpo_retr (0, sd);

    if (sd->weight * 2 >= sd->max_weight
        && sd->sc_data[SC_FLYING_BACKPACK].timer == -1)
        flag = 1;
    if (sd->weight * 10 >= sd->max_weight * 9)
        flag = 2;

    if (flag == 1)
    {
        if (sd->sc_data[SC_WEIGHT50].timer == -1)
            skill_status_change_start (&sd->bl, SC_WEIGHT50, 0, 0, 0, 0, 0,
                                       0);
    }
    else
    {
        skill_status_change_end (&sd->bl, SC_WEIGHT50, -1);
    }
    if (flag == 2)
    {
        if (sd->sc_data[SC_WEIGHT90].timer == -1)
            skill_status_change_start (&sd->bl, SC_WEIGHT90, 0, 0, 0, 0, 0,
                                       0);
    }
    else
    {
        skill_status_change_end (&sd->bl, SC_WEIGHT90, -1);
    }
    return 0;
}

void pc_set_weapon_look (struct map_session_data *sd)
{
    if (sd->attack_spell_override)
        clif_changelook (&sd->bl, LOOK_WEAPON,
                         sd->attack_spell_look_override);
    else
        clif_changelook (&sd->bl, LOOK_WEAPON, sd->status.weapon);
}

/*==========================================
 * �p�����[�^�v�Z
 * first==0�̎��A�v�Z�Ώۂ̃p�����[�^���Ăяo���O����
 * �� �������ꍇ������send���邪�A
 * �\���I�ɕω��������p�����[�^�͎��O��send�����悤��
 *------------------------------------------
 */
int pc_calcstatus (struct map_session_data *sd, int first)
{
    int  b_speed, b_max_hp, b_max_sp, b_hp, b_sp, b_weight, b_max_weight,
        b_paramb[6], b_parame[6], b_hit, b_flee;
    int  b_aspd, b_watk, b_def, b_watk2, b_def2, b_flee2, b_critical,
        b_attackrange, b_matk1, b_matk2, b_mdef, b_mdef2, b_class;
    int  b_base_atk;
    struct skill b_skill[MAX_SKILL];
    int  i, bl, index;
    int  skill, aspd_rate, wele, wele_, def_ele, refinedef = 0;
    int  str, dstr, dex;
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    s_class = pc_calc_base_job (sd->status.class);

    b_speed = sd->speed;
    b_max_hp = sd->status.max_hp;
    b_max_sp = sd->status.max_sp;
    b_hp = sd->status.hp;
    b_sp = sd->status.sp;
    b_weight = sd->weight;
    b_max_weight = sd->max_weight;
    memcpy (b_paramb, &sd->paramb, sizeof (b_paramb));
    memcpy (b_parame, &sd->paramc, sizeof (b_parame));
    memcpy (b_skill, &sd->status.skill, sizeof (b_skill));
    b_hit = sd->hit;
    b_flee = sd->flee;
    b_aspd = sd->aspd;
    b_watk = sd->watk;
    b_def = sd->def;
    b_watk2 = sd->watk2;
    b_def2 = sd->def2;
    b_flee2 = sd->flee2;
    b_critical = sd->critical;
    b_attackrange = sd->attackrange;
    b_matk1 = sd->matk1;
    b_matk2 = sd->matk2;
    b_mdef = sd->mdef;
    b_mdef2 = sd->mdef2;
    b_class = sd->view_class;
    sd->view_class = sd->status.class;
    b_base_atk = sd->base_atk;

    pc_calc_skilltree (sd);     // �X�L���c���[�̌v�Z

    sd->max_weight = max_weight_base[s_class.job] + sd->status.str * 300;

    if (first & 1)
    {
        sd->weight = 0;
        for (i = 0; i < MAX_INVENTORY; i++)
        {
            if (sd->status.inventory[i].nameid == 0
                || sd->inventory_data[i] == NULL)
                continue;
            sd->weight +=
                sd->inventory_data[i]->weight *
                sd->status.inventory[i].amount;
        }
        sd->cart_max_weight = battle_config.max_cart_weight;
        sd->cart_weight = 0;
        sd->cart_max_num = MAX_CART;
        sd->cart_num = 0;
        for (i = 0; i < MAX_CART; i++)
        {
            if (sd->status.cart[i].nameid == 0)
                continue;
            sd->cart_weight +=
                itemdb_weight (sd->status.cart[i].nameid) *
                sd->status.cart[i].amount;
            sd->cart_num++;
        }
    }

    memset (sd->paramb, 0, sizeof (sd->paramb));
    memset (sd->parame, 0, sizeof (sd->parame));
    sd->hit = 0;
    sd->flee = 0;
    sd->flee2 = 0;
    sd->critical = 0;
    sd->aspd = 0;
    sd->watk = 0;
    sd->def = 0;
    sd->mdef = 0;
    sd->watk2 = 0;
    sd->def2 = 0;
    sd->mdef2 = 0;
    sd->status.max_hp = 0;
    sd->status.max_sp = 0;
    sd->attackrange = 0;
    sd->attackrange_ = 0;
    sd->atk_ele = 0;
    sd->def_ele = 0;
    sd->star = 0;
    sd->overrefine = 0;
    sd->matk1 = 0;
    sd->matk2 = 0;
    sd->speed = DEFAULT_WALK_SPEED;
    sd->hprate = 100;
    sd->sprate = 100;
    sd->castrate = 100;
    sd->dsprate = 100;
    sd->base_atk = 0;
    sd->arrow_atk = 0;
    sd->arrow_ele = 0;
    sd->arrow_hit = 0;
    sd->arrow_range = 0;
    sd->nhealhp = sd->nhealsp = sd->nshealhp = sd->nshealsp = sd->nsshealhp =
        sd->nsshealsp = 0;
    memset (sd->addele, 0, sizeof (sd->addele));
    memset (sd->addrace, 0, sizeof (sd->addrace));
    memset (sd->addsize, 0, sizeof (sd->addsize));
    memset (sd->addele_, 0, sizeof (sd->addele_));
    memset (sd->addrace_, 0, sizeof (sd->addrace_));
    memset (sd->addsize_, 0, sizeof (sd->addsize_));
    memset (sd->subele, 0, sizeof (sd->subele));
    memset (sd->subrace, 0, sizeof (sd->subrace));
    memset (sd->addeff, 0, sizeof (sd->addeff));
    memset (sd->addeff2, 0, sizeof (sd->addeff2));
    memset (sd->reseff, 0, sizeof (sd->reseff));
    memset (&sd->special_state, 0, sizeof (sd->special_state));
    memset (sd->weapon_coma_ele, 0, sizeof (sd->weapon_coma_ele));
    memset (sd->weapon_coma_race, 0, sizeof (sd->weapon_coma_race));

    sd->watk_ = 0;              //�񓁗��p(��)
    sd->watk_2 = 0;
    sd->atk_ele_ = 0;
    sd->star_ = 0;
    sd->overrefine_ = 0;

    sd->aspd_rate = 100;
    sd->speed_rate = 100;
    sd->hprecov_rate = 100;
    sd->sprecov_rate = 100;
    sd->critical_def = 0;
    sd->double_rate = 0;
    sd->near_attack_def_rate = sd->long_attack_def_rate = 0;
    sd->atk_rate = sd->matk_rate = 100;
    sd->ignore_def_ele = sd->ignore_def_race = 0;
    sd->ignore_def_ele_ = sd->ignore_def_race_ = 0;
    sd->ignore_mdef_ele = sd->ignore_mdef_race = 0;
    sd->arrow_cri = 0;
    sd->magic_def_rate = sd->misc_def_rate = 0;
    memset (sd->arrow_addele, 0, sizeof (sd->arrow_addele));
    memset (sd->arrow_addrace, 0, sizeof (sd->arrow_addrace));
    memset (sd->arrow_addsize, 0, sizeof (sd->arrow_addsize));
    memset (sd->arrow_addeff, 0, sizeof (sd->arrow_addeff));
    memset (sd->arrow_addeff2, 0, sizeof (sd->arrow_addeff2));
    memset (sd->magic_addele, 0, sizeof (sd->magic_addele));
    memset (sd->magic_addrace, 0, sizeof (sd->magic_addrace));
    memset (sd->magic_subrace, 0, sizeof (sd->magic_subrace));
    sd->perfect_hit = 0;
    sd->critical_rate = sd->hit_rate = sd->flee_rate = sd->flee2_rate = 100;
    sd->def_rate = sd->def2_rate = sd->mdef_rate = sd->mdef2_rate = 100;
    sd->def_ratio_atk_ele = sd->def_ratio_atk_ele_ = 0;
    sd->def_ratio_atk_race = sd->def_ratio_atk_race_ = 0;
    sd->get_zeny_num = 0;
    sd->add_damage_class_count = sd->add_damage_class_count_ =
        sd->add_magic_damage_class_count = 0;
    sd->add_def_class_count = sd->add_mdef_class_count = 0;
    sd->monster_drop_item_count = 0;
    memset (sd->add_damage_classrate, 0, sizeof (sd->add_damage_classrate));
    memset (sd->add_damage_classrate_, 0, sizeof (sd->add_damage_classrate_));
    memset (sd->add_magic_damage_classrate, 0,
            sizeof (sd->add_magic_damage_classrate));
    memset (sd->add_def_classrate, 0, sizeof (sd->add_def_classrate));
    memset (sd->add_mdef_classrate, 0, sizeof (sd->add_mdef_classrate));
    memset (sd->monster_drop_race, 0, sizeof (sd->monster_drop_race));
    memset (sd->monster_drop_itemrate, 0, sizeof (sd->monster_drop_itemrate));
    sd->speed_add_rate = sd->aspd_add_rate = 100;
    sd->double_add_rate = sd->perfect_hit_add = sd->get_zeny_add_num = 0;
    sd->splash_range = sd->splash_add_range = 0;
    sd->autospell_id = sd->autospell_lv = sd->autospell_rate = 0;
    sd->hp_drain_rate = sd->hp_drain_per = sd->sp_drain_rate =
        sd->sp_drain_per = 0;
    sd->hp_drain_rate_ = sd->hp_drain_per_ = sd->sp_drain_rate_ =
        sd->sp_drain_per_ = 0;
    sd->short_weapon_damage_return = sd->long_weapon_damage_return = 0;
    sd->magic_damage_return = 0;    //AppleGirl Was Here
    sd->random_attack_increase_add = sd->random_attack_increase_per = 0;

    if (!sd->disguiseflag && sd->disguise)
    {
        sd->disguise = 0;
        pc_set_weapon_look (sd);
        clif_changelook (&sd->bl, LOOK_SHIELD, sd->status.shield);
        clif_changelook (&sd->bl, LOOK_HEAD_BOTTOM, sd->status.head_bottom);
        clif_changelook (&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
        clif_changelook (&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);
        clif_clearchar (&sd->bl, 9);
        pc_setpos (sd, sd->mapname, sd->bl.x, sd->bl.y, 3);
    }

    sd->spellpower_bonus_target = 0;

    for (i = 0; i < 10; i++)
    {
        index = sd->equip_index[i];
        if (index < 0)
            continue;
        if (i == 9 && sd->equip_index[8] == index)
            continue;
        if (i == 5 && sd->equip_index[4] == index)
            continue;
        if (i == 6
            && (sd->equip_index[5] == index || sd->equip_index[4] == index))
            continue;

        if (sd->inventory_data[index])
        {
            sd->spellpower_bonus_target +=
                sd->inventory_data[index]->magic_bonus;

            if (sd->inventory_data[index]->type == 4)
            {
                if (sd->status.inventory[index].card[0] != 0x00ff
                    && sd->status.inventory[index].card[0] != 0x00fe
                    && sd->status.inventory[index].card[0] != (short) 0xff00)
                {
                    int  j;
                    for (j = 0; j < sd->inventory_data[index]->slot; j++)
                    {           // �J�[�h
                        int  c = sd->status.inventory[index].card[j];
                        if (c > 0)
                        {
                            argrec_t arg[2];
                            arg[0].name = "@slotId";
                            arg[0].v.i = i;
                            arg[1].name = "@itemId";
                            arg[1].v.i = sd->inventory_data[index]->nameid;
                            if (i == 8
                                && sd->status.inventory[index].equip == 0x20)
                                sd->state.lr_flag = 1;
                            run_script_l (itemdb_equipscript (c), 0, sd->bl.id,
                                        0, 2, arg);
                            sd->state.lr_flag = 0;
                        }
                    }
                }
            }
            else if (sd->inventory_data[index]->type == 5)
            {                   // �h��
                if (sd->status.inventory[index].card[0] != 0x00ff
                    && sd->status.inventory[index].card[0] != 0x00fe
                    && sd->status.inventory[index].card[0] != (short) 0xff00)
                {
                    int  j;
                    for (j = 0; j < sd->inventory_data[index]->slot; j++)
                    {           // �J�[�h
                        int  c = sd->status.inventory[index].card[j];
                        if (c > 0) {
                            argrec_t arg[2];
                            arg[0].name = "@slotId";
                            arg[0].v.i = i;
                            arg[1].name = "@itemId";
                            arg[1].v.i = sd->inventory_data[index]->nameid;
                            run_script_l (itemdb_equipscript (c), 0, sd->bl.id,
                                        0, 2, arg);
                        }
                    }
                }
            }
        }
    }

#ifdef USE_ASTRAL_SOUL_SKILL
    if (sd->spellpower_bonus_target < 0)
        sd->spellpower_bonus_target =
            (sd->spellpower_bonus_target * 256) /
            (MIN (128 + skill_power (sd, TMW_ASTRAL_SOUL), 256));
#endif

    if (sd->spellpower_bonus_target < sd->spellpower_bonus_current)
        sd->spellpower_bonus_current = sd->spellpower_bonus_target;

    wele = sd->atk_ele;
    wele_ = sd->atk_ele_;
    def_ele = sd->def_ele;
    memcpy (sd->paramcard, sd->parame, sizeof (sd->paramcard));

    // �����i�ɂ����X�e�[�^�X�ω��͂����Ŏ�s
    for (i = 0; i < 10; i++)
    {
        index = sd->equip_index[i];
        if (index < 0)
            continue;
        if (i == 9 && sd->equip_index[8] == index)
            continue;
        if (i == 5 && sd->equip_index[4] == index)
            continue;
        if (i == 6
            && (sd->equip_index[5] == index || sd->equip_index[4] == index))
            continue;
        if (sd->inventory_data[index])
        {
            sd->def += sd->inventory_data[index]->def;
            if (sd->inventory_data[index]->type == 4)
            {
                int  r, wlv = sd->inventory_data[index]->wlv;
                if (i == 8 && sd->status.inventory[index].equip == 0x20)
                {
                    //�񓁗��p�f�[�^����
                    sd->watk_ += sd->inventory_data[index]->atk;
                    sd->watk_2 = (r = sd->status.inventory[index].refine) * // ���B�U����
                        refinebonus[wlv][0];
                    if ((r -= refinebonus[wlv][2]) > 0) // �ߏ萸�B�{�[�i�X
                        sd->overrefine_ = r * refinebonus[wlv][1];

                    if (sd->status.inventory[index].card[0] == 0x00ff)
                    {           // ��������
                        sd->star_ = (sd->status.inventory[index].card[1] >> 8); // ���̂�����
                        wele_ = (sd->status.inventory[index].card[1] & 0x0f);   // �� ��
                    }
                    sd->attackrange_ += sd->inventory_data[index]->range;
                    sd->state.lr_flag = 1;
                    {
                        argrec_t arg[2];
                        arg[0].name = "@slotId";
                        arg[0].v.i = i;
                        arg[1].name = "@itemId";
                        arg[1].v.i = sd->inventory_data[index]->nameid;
                        run_script_l (sd->inventory_data[index]->equip_script, 0,
                                      sd->bl.id, 0, 2, arg);
                    }
                    sd->state.lr_flag = 0;
                }
                else
                {               //�񓁗������ȊO
                    argrec_t arg[2];
                    arg[0].name = "@slotId";
                    arg[0].v.i = i;
                    arg[1].name = "@itemId";
                    arg[1].v.i = sd->inventory_data[index]->nameid;
                    sd->watk += sd->inventory_data[index]->atk;
                    sd->watk2 += (r = sd->status.inventory[index].refine) * // ���B�U����
                        refinebonus[wlv][0];
                    if ((r -= refinebonus[wlv][2]) > 0) // �ߏ萸�B�{�[�i�X
                        sd->overrefine += r * refinebonus[wlv][1];

                    if (sd->status.inventory[index].card[0] == 0x00ff)
                    {           // ��������
                        sd->star += (sd->status.inventory[index].card[1] >> 8); // ���̂�����
                        wele = (sd->status.inventory[index].card[1] & 0x0f);    // �� ��
                    }
                    sd->attackrange += sd->inventory_data[index]->range;
                    run_script_l (sd->inventory_data[index]->equip_script, 0,
                                  sd->bl.id, 0, 2, arg);
                }
            }
            else if (sd->inventory_data[index]->type == 5)
            {
                argrec_t arg[2];
                arg[0].name = "@slotId";
                arg[0].v.i = i;
                arg[1].name = "@itemId";
                arg[1].v.i = sd->inventory_data[index]->nameid;
                sd->watk += sd->inventory_data[index]->atk;
                refinedef +=
                    sd->status.inventory[index].refine * refinebonus[0][0];
                run_script_l (sd->inventory_data[index]->equip_script, 0,
                              sd->bl.id, 0, 2, arg);
            }
        }
    }

    if (battle_is_unarmed (&sd->bl))
    {
        sd->watk += skill_power (sd, TMW_BRAWLING) / 3; // +66 for 200
        sd->watk2 += skill_power (sd, TMW_BRAWLING) >> 3;   // +25 for 200
        sd->watk_ += skill_power (sd, TMW_BRAWLING) / 3;    // +66 for 200
        sd->watk_2 += skill_power (sd, TMW_BRAWLING) >> 3;  // +25 for 200
    }

    if (sd->equip_index[10] >= 0)
    {                           // ��
        index = sd->equip_index[10];
        if (sd->inventory_data[index])
        {                       //�܂�����������Ă��Ȃ�
            argrec_t arg[2];
            arg[0].name = "@slotId";
            arg[0].v.i = i;
            arg[1].name = "@itemId";
            arg[1].v.i = sd->inventory_data[index]->nameid;
            sd->state.lr_flag = 2;
            run_script_l (sd->inventory_data[index]->equip_script, 0, sd->bl.id,
                        0, 2, arg);
            sd->state.lr_flag = 0;
            sd->arrow_atk += sd->inventory_data[index]->atk;
        }
    }
    sd->def += (refinedef + 50) / 100;

    if (sd->attackrange < 1)
        sd->attackrange = 1;
    if (sd->attackrange_ < 1)
        sd->attackrange_ = 1;
    if (sd->attackrange < sd->attackrange_)
        sd->attackrange = sd->attackrange_;
    if (sd->status.weapon == 11)
        sd->attackrange += sd->arrow_range;
    if (wele > 0)
        sd->atk_ele = wele;
    if (wele_ > 0)
        sd->atk_ele_ = wele_;
    if (def_ele > 0)
        sd->def_ele = def_ele;
    sd->double_rate += sd->double_add_rate;
    sd->perfect_hit += sd->perfect_hit_add;
    sd->get_zeny_num += sd->get_zeny_add_num;
    sd->splash_range += sd->splash_add_range;
    if (sd->speed_add_rate != 100)
        sd->speed_rate += sd->speed_add_rate - 100;
    if (sd->aspd_add_rate != 100)
        sd->aspd_rate += sd->aspd_add_rate - 100;

    // ����ATK�T�C�Y�␳ (�E��)
    sd->atkmods[0] = atkmods[0][sd->weapontype1];
    sd->atkmods[1] = atkmods[1][sd->weapontype1];
    sd->atkmods[2] = atkmods[2][sd->weapontype1];
    //����ATK�T�C�Y�␳ (����)
    sd->atkmods_[0] = atkmods[0][sd->weapontype2];
    sd->atkmods_[1] = atkmods[1][sd->weapontype2];
    sd->atkmods_[2] = atkmods[2][sd->weapontype2];

/*
	// job�{�[�i�X��
	for(i=0;i<sd->status.job_level && i<MAX_LEVEL;i++){
		if(job_bonus[s_class.upper][s_class.job][i])
			sd->paramb[job_bonus[s_class.upper][s_class.job][i]-1]++;
	}
*/

    if ((skill = pc_checkskill (sd, MC_INCCARRY)) > 0)  // skill can be used with an item now, thanks to orn [Valaris]
        sd->max_weight += skill * 1000;

    // �X�e�[�^�X�ω��ɂ������{�p�����[�^�␳
    if (sd->sc_count)
    {
        if (sd->sc_data[SC_CONCENTRATE].timer != -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1)
        {                       // �W���͌���
            sd->paramb[1] +=
                (sd->status.agi + sd->paramb[1] + sd->parame[1] -
                 sd->paramcard[1]) * (2 +
                                      sd->sc_data[SC_CONCENTRATE].val1) / 100;
            sd->paramb[4] +=
                (sd->status.dex + sd->paramb[4] + sd->parame[4] -
                 sd->paramcard[4]) * (2 +
                                      sd->sc_data[SC_CONCENTRATE].val1) / 100;
        }
        if (sd->sc_data[SC_INCREASEAGI].timer != -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
        {                       // ���x����
            sd->paramb[1] += 2 + sd->sc_data[SC_INCREASEAGI].val1;
            sd->speed -= sd->speed * 25 / 100;
        }
        if (sd->sc_data[SC_DECREASEAGI].timer != -1)    // ���x����(agi��battle.c��)
            sd->speed = sd->speed * 125 / 100;
        if (sd->sc_data[SC_CLOAKING].timer != -1)
            sd->speed =
                (sd->speed * (76 + (sd->sc_data[SC_INCREASEAGI].val1 * 3))) /
                100;
        if (sd->sc_data[SC_BLESSING].timer != -1)
        {                       // �u���b�V���O
            sd->paramb[0] += sd->sc_data[SC_BLESSING].val1;
            sd->paramb[3] += sd->sc_data[SC_BLESSING].val1;
            sd->paramb[4] += sd->sc_data[SC_BLESSING].val1;
        }
        if (sd->sc_data[SC_GLORIA].timer != -1) // �O�����A
            sd->paramb[5] += 30;
        if (sd->sc_data[SC_LOUD].timer != -1 && sd->sc_data[SC_QUAGMIRE].timer == -1)   // ���E�h�{�C�X
            sd->paramb[0] += 4;
        if (sd->sc_data[SC_QUAGMIRE].timer != -1)
        {                       // �N�@�O�}�C�A
            sd->speed = sd->speed * 3 / 2;
            sd->paramb[1] -=
                (sd->status.agi + sd->paramb[1] + sd->parame[1]) / 2;
            sd->paramb[4] -=
                (sd->status.dex + sd->paramb[4] + sd->parame[4]) / 2;
        }
        if (sd->sc_data[SC_TRUESIGHT].timer != -1)
        {                       // �g�D���[�T�C�g
            sd->paramb[0] += 5;
            sd->paramb[1] += 5;
            sd->paramb[2] += 5;
            sd->paramb[3] += 5;
            sd->paramb[4] += 5;
            sd->paramb[5] += 5;
        }
    }
    sd->speed -= skill_power (sd, TMW_SPEED) >> 3;
    sd->aspd_rate -= skill_power (sd, TMW_SPEED) / 10;
    if (sd->aspd_rate < 20)
        sd->aspd_rate = 20;

/*
	//1�x�����łȂ�Job70�X�p�m�r��+10
	if(s_class.job == 23 && sd->die_counter == 0 && sd->status.job_level >= 70){
		sd->paramb[0]+= 15;
		sd->paramb[1]+= 15;
		sd->paramb[2]+= 15;
		sd->paramb[3]+= 15;
		sd->paramb[4]+= 15;
		sd->paramb[5]+= 15;
	}
*/
    sd->paramc[0] = sd->status.str + sd->paramb[0] + sd->parame[0];
    sd->paramc[1] = sd->status.agi + sd->paramb[1] + sd->parame[1];
    sd->paramc[2] = sd->status.vit + sd->paramb[2] + sd->parame[2];
    sd->paramc[3] = sd->status.int_ + sd->paramb[3] + sd->parame[3];
    sd->paramc[4] = sd->status.dex + sd->paramb[4] + sd->parame[4];
    sd->paramc[5] = sd->status.luk + sd->paramb[5] + sd->parame[5];
    for (i = 0; i < 6; i++)
        if (sd->paramc[i] < 0)
            sd->paramc[i] = 0;

    if (sd->status.weapon == 11 || sd->status.weapon == 13
        || sd->status.weapon == 14)
    {
        str = sd->paramc[4];
        dex = sd->paramc[0];
    }
    else
    {
        str = sd->paramc[0];
        dex = sd->paramc[4];
        sd->critical += ((dex * 3) >> 1);
    }
    dstr = str / 10;
    sd->base_atk += str + dstr * dstr + dex / 5 + sd->paramc[5] / 5;
//fprintf(stderr, "baseatk = %d = x + %d + %d + %d + %d\n", sd->base_atk, str, dstr*dstr, dex/5, sd->paramc[5]/5);
    sd->matk1 += sd->paramc[3] + (sd->paramc[3] / 5) * (sd->paramc[3] / 5);
    sd->matk2 += sd->paramc[3] + (sd->paramc[3] / 7) * (sd->paramc[3] / 7);
    if (sd->matk1 < sd->matk2)
    {
        int  temp = sd->matk2;
        sd->matk2 = sd->matk1;
        sd->matk1 = temp;
    }
    // [Fate] New tmw magic system
    sd->matk1 += sd->status.base_level + sd->spellpower_bonus_current;
#ifdef USE_ASTRAL_SOUL_SKILL
    if (sd->matk1 > MAGIC_SKILL_THRESHOLD)
    {
        int  bonus = sd->matk1 - MAGIC_SKILL_THRESHOLD;
        int  bound = 2 * skill_power (sd, TMW_ASTRAL_SOUL);
        if (bonus > bound)
            bonus = (bonus * 100) / (100 + bonus - bound);

        sd->matk1 = MAGIC_SKILL_THRESHOLD + bonus;
    }
#endif
    sd->matk2 = 0;
    if (sd->matk1 < 0)
        sd->matk1 = 0;

    sd->hit += sd->paramc[4] + sd->status.base_level;
    sd->flee += sd->paramc[1] + sd->status.base_level;
    sd->def2 += sd->paramc[2];
    sd->mdef2 += sd->paramc[3];
    sd->flee2 += sd->paramc[5] + 10;
    sd->critical += (sd->paramc[5] * 3) + 10;

    if (sd->base_atk < 1)
        sd->base_atk = 1;
    if (sd->critical_rate != 100)
        sd->critical = (sd->critical * sd->critical_rate) / 100;
    if (sd->critical < 10)
        sd->critical = 10;
    if (sd->hit_rate != 100)
        sd->hit = (sd->hit * sd->hit_rate) / 100;
    if (sd->hit < 1)
        sd->hit = 1;
    if (sd->flee_rate != 100)
        sd->flee = (sd->flee * sd->flee_rate) / 100;
    if (sd->flee < 1)
        sd->flee = 1;
    if (sd->flee2_rate != 100)
        sd->flee2 = (sd->flee2 * sd->flee2_rate) / 100;
    if (sd->flee2 < 10)
        sd->flee2 = 10;
    if (sd->def_rate != 100)
        sd->def = (sd->def * sd->def_rate) / 100;
    if (sd->def < 0)
        sd->def = 0;
    if (sd->def2_rate != 100)
        sd->def2 = (sd->def2 * sd->def2_rate) / 100;
    if (sd->def2 < 1)
        sd->def2 = 1;
    if (sd->mdef_rate != 100)
        sd->mdef = (sd->mdef * sd->mdef_rate) / 100;
    if (sd->mdef < 0)
        sd->mdef = 0;
    if (sd->mdef2_rate != 100)
        sd->mdef2 = (sd->mdef2 * sd->mdef2_rate) / 100;
    if (sd->mdef2 < 1)
        sd->mdef2 = 1;

    // �񓁗� ASPD �C��
    if (sd->status.weapon <= 16)
        sd->aspd +=
            aspd_base[s_class.job][sd->status.weapon] - (sd->paramc[1] * 4 +
                                                         sd->paramc[4]) *
            aspd_base[s_class.job][sd->status.weapon] / 1000;
    else
        sd->aspd += ((aspd_base[s_class.job][sd->weapontype1] -
                      (sd->paramc[1] * 4 +
                       sd->paramc[4]) *
                      aspd_base[s_class.job][sd->weapontype1] / 1000) +
                     (aspd_base[s_class.job][sd->weapontype2] -
                      (sd->paramc[1] * 4 +
                       sd->paramc[4]) *
                      aspd_base[s_class.job][sd->weapontype2] / 1000)) * 140 /
            200;

    aspd_rate = sd->aspd_rate;

    //�U�����x����

    if ((skill = pc_checkskill (sd, AC_VULTURE)) > 0)
    {                           // ���V�̖�
        sd->hit += skill;
        if (sd->status.weapon == 11)
            sd->attackrange += skill;
    }

    if (sd->attackrange > 2)
    {                           // [fate] ranged weapon?
        sd->attackrange += MIN (skill_power (sd, AC_OWL) / 60, 3);
        sd->hit += skill_power (sd, AC_OWL) / 10;   // 20 for 200
    }

    if ((skill = pc_checkskill (sd, BS_WEAPONRESEARCH)) > 0)    // ���팤���̖���������
        sd->hit += skill * 2;
    if (sd->status.option & 2 && (skill = pc_checkskill (sd, RG_TUNNELDRIVE)) > 0)  // �g���l���h���C�u   // �g���l���h���C�u
        sd->speed += (1.2 * DEFAULT_WALK_SPEED - skill * 9);
    if (pc_iscarton (sd) && (skill = pc_checkskill (sd, MC_PUSHCART)) > 0)  // �J�[�g�ɂ��鑬�x�ቺ
        sd->speed += (10 - skill) * (DEFAULT_WALK_SPEED * 0.1);
    else if (pc_isriding (sd))  // �y�R�y�R�����ɂ��鑬�x����
        sd->speed -= (0.25 * DEFAULT_WALK_SPEED);
    sd->max_weight += 1000;
    if (sd->sc_count)
    {
        if (sd->sc_data[SC_WINDWALK].timer != -1)   //�E�B���h�E�H�[�N����Lv*2%���Z
            sd->speed -=
                sd->speed * (sd->sc_data[SC_WINDWALK].val1 * 2) / 100;
        if (sd->sc_data[SC_CARTBOOST].timer != -1)  // �J�[�g�u�[�X�g
            sd->speed -= (DEFAULT_WALK_SPEED * 20) / 100;
        if (sd->sc_data[SC_BERSERK].timer != -1)    //�o�[�T�[�N����IA�Ɠ������炢�����H
            sd->speed -= sd->speed * 25 / 100;
        if (sd->sc_data[SC_WEDDING].timer != -1)    //�������͕��̂��x��
            sd->speed = 2 * DEFAULT_WALK_SPEED;
    }

    if ((skill = pc_checkskill (sd, CR_TRUST)) > 0)
    {                           // �t�F�C�X
        sd->status.max_hp += skill * 200;
        sd->subele[6] += skill * 5;
    }
    if ((skill = pc_checkskill (sd, BS_SKINTEMPER)) > 0)
        sd->subele[3] += skill * 4;

    bl = sd->status.base_level;

    sd->status.max_hp +=
        (3500 + bl * hp_coefficient2[s_class.job] +
         hp_sigma_val[s_class.job][(bl > 0) ? bl - 1 : 0]) / 100 * (100 +
                                                                    sd->paramc
                                                                    [2]) /
        100 + (sd->parame[2] - sd->paramcard[2]);
    if (s_class.upper == 1)     // [MouseJstr]
        sd->status.max_hp = sd->status.max_hp * 130 / 100;
    if (sd->hprate != 100)
        sd->status.max_hp = sd->status.max_hp * sd->hprate / 100;

    if (sd->sc_data && sd->sc_data[SC_BERSERK].timer != -1)
    {                           // �o�[�T�[�N
        sd->status.max_hp = sd->status.max_hp * 3;
        sd->status.hp = sd->status.hp * 3;
        if (sd->status.max_hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
            sd->status.max_hp = battle_config.max_hp;
        if (sd->status.hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
            sd->status.hp = battle_config.max_hp;
    }
    if (s_class.job == 23 && sd->status.base_level >= 99)
    {
        sd->status.max_hp = sd->status.max_hp + 2000;
    }

    if (sd->status.max_hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
        sd->status.max_hp = battle_config.max_hp;
    if (sd->status.max_hp <= 0)
        sd->status.max_hp = 1;  // end

    // �ő�SP�v�Z
    sd->status.max_sp +=
        ((sp_coefficient[s_class.job] * bl) + 1000) / 100 * (100 +
                                                             sd->paramc[3]) /
        100 + (sd->parame[3] - sd->paramcard[3]);
    if (s_class.upper == 1)     // [MouseJstr]
        sd->status.max_sp = sd->status.max_sp * 130 / 100;
    if (sd->sprate != 100)
        sd->status.max_sp = sd->status.max_sp * sd->sprate / 100;

    if ((skill = pc_checkskill (sd, HP_MEDITATIO)) > 0) // ���f�B�e�C�e�B�I
        sd->status.max_sp += sd->status.max_sp * skill / 100;
    if ((skill = pc_checkskill (sd, HW_SOULDRAIN)) > 0) /* �\�E���h���C�� */
        sd->status.max_sp += sd->status.max_sp * 2 * skill / 100;

    if (sd->status.max_sp < 0 || sd->status.max_sp > battle_config.max_sp)
        sd->status.max_sp = battle_config.max_sp;

    //���R����HP
    sd->nhealhp = 1 + (sd->paramc[2] / 5) + (sd->status.max_hp / 200);
    if ((skill = pc_checkskill (sd, SM_RECOVERY)) > 0)
    {                           /* HP�񕜗͌��� */
        sd->nshealhp = skill * 5 + (sd->status.max_hp * skill / 500);
        if (sd->nshealhp > 0x7fff)
            sd->nshealhp = 0x7fff;
    }
    //���R����SP
    sd->nhealsp = 1 + (sd->paramc[3] / 6) + (sd->status.max_sp / 100);
    if (sd->paramc[3] >= 120)
        sd->nhealsp += ((sd->paramc[3] - 120) >> 1) + 4;
    if ((skill = pc_checkskill (sd, MG_SRECOVERY)) > 0)
    {                           /* SP�񕜗͌��� */
        sd->nshealsp = skill * 3 + (sd->status.max_sp * skill / 500);
        if (sd->nshealsp > 0x7fff)
            sd->nshealsp = 0x7fff;
    }

    if ((skill = pc_checkskill (sd, MO_SPIRITSRECOVERY)) > 0)
    {
        sd->nsshealhp = skill * 4 + (sd->status.max_hp * skill / 500);
        sd->nsshealsp = skill * 2 + (sd->status.max_sp * skill / 500);
        if (sd->nsshealhp > 0x7fff)
            sd->nsshealhp = 0x7fff;
        if (sd->nsshealsp > 0x7fff)
            sd->nsshealsp = 0x7fff;
    }
    if (sd->hprecov_rate != 100)
    {
        sd->nhealhp = sd->nhealhp * sd->hprecov_rate / 100;
        if (sd->nhealhp < 1)
            sd->nhealhp = 1;
    }
    if (sd->sprecov_rate != 100)
    {
        sd->nhealsp = sd->nhealsp * sd->sprecov_rate / 100;
        if (sd->nhealsp < 1)
            sd->nhealsp = 1;
    }
    if ((skill = pc_checkskill (sd, HP_MEDITATIO)) > 0)
    {                           // ���f�B�e�C�e�B�I��SPR�ł͂Ȃ����R�񕜂ɂ�����
        sd->nhealsp += 3 * skill * (sd->status.max_sp) / 100;
        if (sd->nhealsp > 0x7fff)
            sd->nhealsp = 0x7fff;
    }

    // �푰�ϐ��i�����ł����́H �f�B�o�C���v���e�N�V�����Ɠ������������邩���j
    if ((skill = pc_checkskill (sd, SA_DRAGONOLOGY)) > 0)
    {                           // �h���S�m���W�[
        skill = skill * 4;
        sd->addrace[9] += skill;
        sd->addrace_[9] += skill;
        sd->subrace[9] += skill;
        sd->magic_addrace[9] += skill;
        sd->magic_subrace[9] -= skill;
    }

    //Flee�㏸
    if ((skill = pc_checkskill (sd, TF_MISS)) > 0)
    {                           // ���𗦑���
        if (sd->status.class == 6 || sd->status.class == 4007
            || sd->status.class == 23)
        {
            sd->flee += skill * 3;
        }
        if (sd->status.class == 12 || sd->status.class == 17
            || sd->status.class == 4013 || sd->status.class == 4018)
            sd->flee += skill * 4;
        if (sd->status.class == 12 || sd->status.class == 4013)
            sd->speed -= sd->speed * (skill * .5) / 100;
    }
    if ((skill = pc_checkskill (sd, MO_DODGE)) > 0) // ���؂�
        sd->flee += (skill * 3) >> 1;

    // �X�L�����X�e�[�^�X�ُ��ɂ����c���̃p�����[�^�␳
    if (sd->sc_count)
    {
        // ATK/DEF�ω��`
        if (sd->sc_data[SC_ANGELUS].timer != -1)    // �G���W�F���X
            sd->def2 =
                sd->def2 * (110 + 5 * sd->sc_data[SC_ANGELUS].val1) / 100;
        if (sd->sc_data[SC_IMPOSITIO].timer != -1)
        {                       // �C���|�V�e�B�I�}�k�X
            sd->watk += sd->sc_data[SC_IMPOSITIO].val1 * 5;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == 4)
                sd->watk_ += sd->sc_data[SC_IMPOSITIO].val1 * 5;
        }
        if (sd->sc_data[SC_PROVOKE].timer != -1)
        {                       // �v���{�b�N
            sd->def2 =
                sd->def2 * (100 - 6 * sd->sc_data[SC_PROVOKE].val1) / 100;
            sd->base_atk =
                sd->base_atk * (100 + 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
            sd->watk =
                sd->watk * (100 + 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == 4)
                sd->watk_ =
                    sd->watk_ * (100 +
                                 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
        }
        if (sd->sc_data[SC_ENDURE].timer != -1)
            sd->mdef2 += sd->sc_data[SC_ENDURE].val1;
        if (sd->sc_data[SC_MINDBREAKER].timer != -1)
        {                       // �v���{�b�N
            sd->mdef2 =
                sd->mdef2 * (100 -
                             6 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
            sd->matk1 =
                sd->matk1 * (100 +
                             2 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
            sd->matk2 =
                sd->matk2 * (100 +
                             2 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
        }
        if (sd->sc_data[SC_POISON].timer != -1) // �ŏ���
            sd->def2 = sd->def2 * 75 / 100;
        if (sd->sc_data[SC_DRUMBATTLE].timer != -1)
        {                       // �푾�ۂ̋���
            sd->watk += sd->sc_data[SC_DRUMBATTLE].val2;
            sd->def += sd->sc_data[SC_DRUMBATTLE].val3;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == 4)
                sd->watk_ += sd->sc_data[SC_DRUMBATTLE].val2;
        }
        if (sd->sc_data[SC_NIBELUNGEN].timer != -1)
        {                       // �j�[�x�����O�̎w��
            index = sd->equip_index[9];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 3)
                sd->watk += sd->sc_data[SC_NIBELUNGEN].val3;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 3)
                sd->watk_ += sd->sc_data[SC_NIBELUNGEN].val3;
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 4)
                sd->watk += sd->sc_data[SC_NIBELUNGEN].val2;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 4)
                sd->watk_ += sd->sc_data[SC_NIBELUNGEN].val2;
        }

        if (sd->sc_data[SC_VOLCANO].timer != -1 && sd->def_ele == 3)
        {                       // �{���P�[�m
            sd->watk += sd->sc_data[SC_VIOLENTGALE].val3;
        }

        if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1)
            sd->def =
                sd->def * (100 - sd->sc_data[SC_SIGNUMCRUCIS].val2) / 100;
        if (sd->sc_data[SC_ETERNALCHAOS].timer != -1)   // �G�^�[�i���J�I�X
            sd->def = 0;

        if (sd->sc_data[SC_CONCENTRATION].timer != -1)
        {                       //�R���Z���g���[�V����
            sd->watk =
                sd->watk * (100 +
                            5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
            index = sd->equip_index[8];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == 4)
                sd->watk_ =
                    sd->watk * (100 +
                                5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
            sd->def =
                sd->def * (100 -
                           5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
        }

        if (sd->sc_data[SC_MAGICPOWER].timer != -1)
        {                       //���@�͑���
            sd->matk1 =
                sd->matk1 * (100 + 2 * sd->sc_data[SC_MAGICPOWER].val1) / 100;
            sd->matk2 =
                sd->matk2 * (100 + 2 * sd->sc_data[SC_MAGICPOWER].val1) / 100;
        }
        if (sd->sc_data[SC_ATKPOT].timer != -1)
            sd->watk += sd->sc_data[SC_ATKPOT].val1;
        if (sd->sc_data[SC_MATKPOT].timer != -1)
        {
            sd->matk1 += sd->sc_data[SC_MATKPOT].val1;
            sd->matk2 += sd->sc_data[SC_MATKPOT].val1;
        }

        // ASPD/�ړ����x�ω��n
        if (sd->sc_data[SC_TWOHANDQUICKEN].timer != -1 && sd->sc_data[SC_QUAGMIRE].timer == -1 && sd->sc_data[SC_DONTFORGETME].timer == -1) // 2HQ
            aspd_rate -= 30;
        if (sd->sc_data[SC_ADRENALINE].timer != -1
            && sd->sc_data[SC_TWOHANDQUICKEN].timer == -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
        {                       // �A�h���i�������b�V��
            if (sd->sc_data[SC_ADRENALINE].val2
                || !battle_config.party_skill_penaly)
                aspd_rate -= 30;
            else
                aspd_rate -= 25;
        }
        if (sd->sc_data[SC_SPEARSQUICKEN].timer != -1 && sd->sc_data[SC_ADRENALINE].timer == -1 && sd->sc_data[SC_TWOHANDQUICKEN].timer == -1 && sd->sc_data[SC_QUAGMIRE].timer == -1 && sd->sc_data[SC_DONTFORGETME].timer == -1)  // �X�s�A�N�B�b�P��
            aspd_rate -= sd->sc_data[SC_SPEARSQUICKEN].val2;
        if (sd->sc_data[SC_ASSNCROS].timer != -1 && // �[�z�̃A�T�V���N���X
            sd->sc_data[SC_TWOHANDQUICKEN].timer == -1
            && sd->sc_data[SC_ADRENALINE].timer == -1
            && sd->sc_data[SC_SPEARSQUICKEN].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
            aspd_rate -=
                5 + sd->sc_data[SC_ASSNCROS].val1 +
                sd->sc_data[SC_ASSNCROS].val2 + sd->sc_data[SC_ASSNCROS].val3;
        if (sd->sc_data[SC_DONTFORGETME].timer != -1)
        {                       // �����Y���Ȃ���
            aspd_rate +=
                sd->sc_data[SC_DONTFORGETME].val1 * 3 +
                sd->sc_data[SC_DONTFORGETME].val2 +
                (sd->sc_data[SC_DONTFORGETME].val3 >> 16);
            sd->speed =
                sd->speed * (100 + sd->sc_data[SC_DONTFORGETME].val1 * 2 +
                             sd->sc_data[SC_DONTFORGETME].val2 +
                             (sd->sc_data[SC_DONTFORGETME].val3 & 0xffff)) /
                100;
        }
        if (sd->sc_data[i = SC_SPEEDPOTION2].timer != -1 || sd->sc_data[i = SC_SPEEDPOTION1].timer != -1 || sd->sc_data[i = SC_SPEEDPOTION0].timer != -1)   // �� ���|�[�V����
            aspd_rate -= sd->sc_data[i].val1;

        if (sd->sc_data[SC_HASTE].timer != -1)
            aspd_rate -= sd->sc_data[SC_HASTE].val1;

        /* Slow down if protected */

        if (sd->sc_data[SC_PHYS_SHIELD].timer != -1)
            aspd_rate += sd->sc_data[SC_PHYS_SHIELD].val1;

        // HIT/FLEE�ω��n
        if (sd->sc_data[SC_WHISTLE].timer != -1)
        {                       // ���J
            sd->flee += sd->flee * (sd->sc_data[SC_WHISTLE].val1
                                    + sd->sc_data[SC_WHISTLE].val2 +
                                    (sd->sc_data[SC_WHISTLE].val3 >> 16)) /
                100;
            sd->flee2 +=
                (sd->sc_data[SC_WHISTLE].val1 + sd->sc_data[SC_WHISTLE].val2 +
                 (sd->sc_data[SC_WHISTLE].val3 & 0xffff)) * 10;
        }
        if (sd->sc_data[SC_HUMMING].timer != -1)    // �n�~���O
            sd->hit +=
                (sd->sc_data[SC_HUMMING].val1 * 2 +
                 sd->sc_data[SC_HUMMING].val2 +
                 sd->sc_data[SC_HUMMING].val3) * sd->hit / 100;
        if (sd->sc_data[SC_VIOLENTGALE].timer != -1 && sd->def_ele == 4)
        {                       // �o�C�I�����g�Q�C��
            sd->flee += sd->flee * sd->sc_data[SC_VIOLENTGALE].val3 / 100;
        }
        if (sd->sc_data[SC_BLIND].timer != -1)
        {                       // �Í�
            sd->hit -= sd->hit * 25 / 100;
            sd->flee -= sd->flee * 25 / 100;
        }
        if (sd->sc_data[SC_WINDWALK].timer != -1)   // �E�B���h�E�H�[�N
            sd->flee += sd->flee * (sd->sc_data[SC_WINDWALK].val2) / 100;
        if (sd->sc_data[SC_SPIDERWEB].timer != -1)  //�X�p�C�_�[�E�F�u
            sd->flee -= sd->flee * 50 / 100;
        if (sd->sc_data[SC_TRUESIGHT].timer != -1)  //�g�D���[�T�C�g
            sd->hit += 3 * (sd->sc_data[SC_TRUESIGHT].val1);
        if (sd->sc_data[SC_CONCENTRATION].timer != -1)  //�R���Z���g���[�V����
            sd->hit += (10 * (sd->sc_data[SC_CONCENTRATION].val1));

        // �ϐ�
        if (sd->sc_data[SC_SIEGFRIED].timer != -1)
        {                       // �s���g�̃W�[�N�t���[�h
            sd->subele[1] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[2] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[3] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[4] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[5] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[6] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[7] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[8] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
            sd->subele[9] += sd->sc_data[SC_SIEGFRIED].val2;    // ��
        }
        if (sd->sc_data[SC_PROVIDENCE].timer != -1)
        {                       // �v�����B�f���X
            sd->subele[6] += sd->sc_data[SC_PROVIDENCE].val2;   // �� ������
            sd->subrace[6] += sd->sc_data[SC_PROVIDENCE].val2;  // �� ����
        }

        // ���̑�
        if (sd->sc_data[SC_APPLEIDUN].timer != -1)
        {                       // �C�h�D���̗ь�
            sd->status.max_hp +=
                ((5 + sd->sc_data[SC_APPLEIDUN].val1 * 2 +
                  ((sd->sc_data[SC_APPLEIDUN].val2 + 1) >> 1) +
                  sd->sc_data[SC_APPLEIDUN].val3 / 10) * sd->status.max_hp) /
                100;
            if (sd->status.max_hp < 0
                || sd->status.max_hp > battle_config.max_hp)
                sd->status.max_hp = battle_config.max_hp;
        }
        if (sd->sc_data[SC_DELUGE].timer != -1 && sd->def_ele == 1)
        {                       // �f�����[�W
            sd->status.max_hp +=
                sd->status.max_hp * sd->sc_data[SC_DELUGE].val3 / 100;
            if (sd->status.max_hp < 0
                || sd->status.max_hp > battle_config.max_hp)
                sd->status.max_hp = battle_config.max_hp;
        }
        if (sd->sc_data[SC_SERVICE4U].timer != -1)
        {                       // �T�[�r�X�t�H�[���[
            sd->status.max_sp +=
                sd->status.max_sp * (10 + sd->sc_data[SC_SERVICE4U].val1 +
                                     sd->sc_data[SC_SERVICE4U].val2 +
                                     sd->sc_data[SC_SERVICE4U].val3) / 100;
            if (sd->status.max_sp < 0
                || sd->status.max_sp > battle_config.max_sp)
                sd->status.max_sp = battle_config.max_sp;
            sd->dsprate -=
                (10 + sd->sc_data[SC_SERVICE4U].val1 * 3 +
                 sd->sc_data[SC_SERVICE4U].val2 +
                 sd->sc_data[SC_SERVICE4U].val3);
            if (sd->dsprate < 0)
                sd->dsprate = 0;
        }

        if (sd->sc_data[SC_FORTUNE].timer != -1)    // �K�^�̃L�X
            sd->critical +=
                (10 + sd->sc_data[SC_FORTUNE].val1 +
                 sd->sc_data[SC_FORTUNE].val2 +
                 sd->sc_data[SC_FORTUNE].val3) * 10;

        if (sd->sc_data[SC_EXPLOSIONSPIRITS].timer != -1)
        {                       // �����g��
            if (s_class.job == 23)
                sd->critical += sd->sc_data[SC_EXPLOSIONSPIRITS].val1 * 100;
            else
                sd->critical += sd->sc_data[SC_EXPLOSIONSPIRITS].val2;
        }

        if (sd->sc_data[SC_STEELBODY].timer != -1)
        {                       // ��
            sd->def = 90;
            sd->mdef = 90;
            aspd_rate += 25;
            sd->speed = (sd->speed * 125) / 100;
        }
        if (sd->sc_data[SC_DEFENDER].timer != -1)
        {
            sd->aspd += (550 - sd->sc_data[SC_DEFENDER].val1 * 50);
            sd->speed =
                (sd->speed * (155 - sd->sc_data[SC_DEFENDER].val1 * 5)) / 100;
        }
        if (sd->sc_data[SC_ENCPOISON].timer != -1)
            sd->addeff[4] += sd->sc_data[SC_ENCPOISON].val2;

        if (sd->sc_data[SC_DANCING].timer != -1)
        {                       // ���t/�_���X�g�p��
            sd->speed *= 4;
            sd->nhealsp = 0;
            sd->nshealsp = 0;
            sd->nsshealsp = 0;
        }
        if (sd->sc_data[SC_CURSE].timer != -1)
            sd->speed += 450;

        if (sd->sc_data[SC_TRUESIGHT].timer != -1)  //�g�D���[�T�C�g
            sd->critical +=
                sd->critical * (sd->sc_data[SC_TRUESIGHT].val1) / 100;

/*		if(sd->sc_data[SC_VOLCANO].timer!=-1)	// �G���`�����g�|�C�Y��(������battle.c��)
			sd->addeff[2]+=sd->sc_data[SC_VOLCANO].val2;//% of granting
		if(sd->sc_data[SC_DELUGE].timer!=-1)	// �G���`�����g�|�C�Y��(������battle.c��)
			sd->addeff[0]+=sd->sc_data[SC_DELUGE].val2;//% of granting
		*/
    }

    if (sd->speed_rate != 100)
        sd->speed = sd->speed * sd->speed_rate / 100;
    if (sd->speed < 1)
        sd->speed = 1;
    if (aspd_rate != 100)
        sd->aspd = sd->aspd * aspd_rate / 100;
    if (pc_isriding (sd))       // �R���C��
        sd->aspd =
            sd->aspd * (100 +
                        10 * (5 -
                              pc_checkskill (sd, KN_CAVALIERMASTERY))) / 100;

    if (sd->attack_spell_override)
        sd->aspd = sd->attack_spell_delay;

    if (sd->aspd < battle_config.max_aspd)
        sd->aspd = battle_config.max_aspd;
    sd->amotion = sd->aspd;
    sd->dmotion = 800 - sd->paramc[1] * 4;
    if (sd->dmotion < 400)
        sd->dmotion = 400;
    if (sd->skilltimer != -1 && (skill = pc_checkskill (sd, SA_FREECAST)) > 0)
    {
        sd->prev_speed = sd->speed;
        sd->speed = sd->speed * (175 - skill * 5) / 100;
    }

    if (sd->status.hp > sd->status.max_hp)
        sd->status.hp = sd->status.max_hp;
    if (sd->status.sp > sd->status.max_sp)
        sd->status.sp = sd->status.max_sp;

    if (first & 4)
        return 0;
    if (first & 3)
    {
        clif_updatestatus (sd, SP_SPEED);
        clif_updatestatus (sd, SP_MAXHP);
        clif_updatestatus (sd, SP_MAXSP);
        if (first & 1)
        {
            clif_updatestatus (sd, SP_HP);
            clif_updatestatus (sd, SP_SP);
        }
        return 0;
    }

    if (b_class != sd->view_class)
    {
        clif_changelook (&sd->bl, LOOK_BASE, sd->view_class);
        clif_changelook (&sd->bl, LOOK_WEAPON, 0);
    }

    if (memcmp (b_skill, sd->status.skill, sizeof (sd->status.skill))
        || b_attackrange != sd->attackrange)
        clif_skillinfoblock (sd);   // �X�L�����M

    if (b_speed != sd->speed)
        clif_updatestatus (sd, SP_SPEED);
    if (b_weight != sd->weight)
        clif_updatestatus (sd, SP_WEIGHT);
    if (b_max_weight != sd->max_weight)
    {
        clif_updatestatus (sd, SP_MAXWEIGHT);
        pc_checkweighticon (sd);
    }
    for (i = 0; i < 6; i++)
        if (b_paramb[i] + b_parame[i] != sd->paramb[i] + sd->parame[i])
            clif_updatestatus (sd, SP_STR + i);
    if (b_hit != sd->hit)
        clif_updatestatus (sd, SP_HIT);
    if (b_flee != sd->flee)
        clif_updatestatus (sd, SP_FLEE1);
    if (b_aspd != sd->aspd)
        clif_updatestatus (sd, SP_ASPD);
    if (b_watk != sd->watk || b_base_atk != sd->base_atk)
        clif_updatestatus (sd, SP_ATK1);
    if (b_def != sd->def)
        clif_updatestatus (sd, SP_DEF1);
    if (b_watk2 != sd->watk2)
        clif_updatestatus (sd, SP_ATK2);
    if (b_def2 != sd->def2)
        clif_updatestatus (sd, SP_DEF2);
    if (b_flee2 != sd->flee2)
        clif_updatestatus (sd, SP_FLEE2);
    if (b_critical != sd->critical)
        clif_updatestatus (sd, SP_CRITICAL);
    if (b_matk1 != sd->matk1)
        clif_updatestatus (sd, SP_MATK1);
    if (b_matk2 != sd->matk2)
        clif_updatestatus (sd, SP_MATK2);
    if (b_mdef != sd->mdef)
        clif_updatestatus (sd, SP_MDEF1);
    if (b_mdef2 != sd->mdef2)
        clif_updatestatus (sd, SP_MDEF2);
    if (b_attackrange != sd->attackrange)
        clif_updatestatus (sd, SP_ATTACKRANGE);
    if (b_max_hp != sd->status.max_hp)
        clif_updatestatus (sd, SP_MAXHP);
    if (b_max_sp != sd->status.max_sp)
        clif_updatestatus (sd, SP_MAXSP);
    if (b_hp != sd->status.hp)
        clif_updatestatus (sd, SP_HP);
    if (b_sp != sd->status.sp)
        clif_updatestatus (sd, SP_SP);

/*	if(before.cart_num != before.cart_num || before.cart_max_num != before.cart_max_num ||
		before.cart_weight != before.cart_weight || before.cart_max_weight != before.cart_max_weight )
		clif_updatestatus(sd,SP_CARTINFO);*/

    if (sd->status.hp < sd->status.max_hp >> 2
        && pc_checkskill (sd, SM_AUTOBERSERK) > 0
        && (sd->sc_data[SC_PROVOKE].timer == -1
            || sd->sc_data[SC_PROVOKE].val2 == 0) && !pc_isdead (sd))
        // �I�[�g�o�[�T�[�N����
        skill_status_change_start (&sd->bl, SC_PROVOKE, 10, 1, 0, 0, 0, 0);

    return 0;
}

/*==========================================
 * �� ���i�ɂ����\�͓��̃{�[�i�X�ݒ�
 *------------------------------------------
 */
int pc_bonus (struct map_session_data *sd, int type, int val)
{
    nullpo_retr (0, sd);

    switch (type)
    {
        case SP_STR:
        case SP_AGI:
        case SP_VIT:
        case SP_INT:
        case SP_DEX:
        case SP_LUK:
            if (sd->state.lr_flag != 2)
                sd->parame[type - SP_STR] += val;
            break;
        case SP_ATK1:
            if (!sd->state.lr_flag)
                sd->watk += val;
            else if (sd->state.lr_flag == 1)
                sd->watk_ += val;
            break;
        case SP_ATK2:
            if (!sd->state.lr_flag)
                sd->watk2 += val;
            else if (sd->state.lr_flag == 1)
                sd->watk_2 += val;
            break;
        case SP_BASE_ATK:
            if (sd->state.lr_flag != 2)
                sd->base_atk += val;
            break;
        case SP_MATK1:
            if (sd->state.lr_flag != 2)
                sd->matk1 += val;
            break;
        case SP_MATK2:
            if (sd->state.lr_flag != 2)
                sd->matk2 += val;
            break;
        case SP_MATK:
            if (sd->state.lr_flag != 2)
            {
                sd->matk1 += val;
                sd->matk2 += val;
            }
            break;
        case SP_DEF1:
            if (sd->state.lr_flag != 2)
                sd->def += val;
            break;
        case SP_MDEF1:
            if (sd->state.lr_flag != 2)
                sd->mdef += val;
            break;
        case SP_MDEF2:
            if (sd->state.lr_flag != 2)
                sd->mdef += val;
            break;
        case SP_HIT:
            if (sd->state.lr_flag != 2)
                sd->hit += val;
            else
                sd->arrow_hit += val;
            break;
        case SP_FLEE1:
            if (sd->state.lr_flag != 2)
                sd->flee += val;
            break;
        case SP_FLEE2:
            if (sd->state.lr_flag != 2)
                sd->flee2 += val * 10;
            break;
        case SP_CRITICAL:
            if (sd->state.lr_flag != 2)
                sd->critical += val * 10;
            else
                sd->arrow_cri += val * 10;
            break;
        case SP_ATKELE:
            if (!sd->state.lr_flag)
                sd->atk_ele = val;
            else if (sd->state.lr_flag == 1)
                sd->atk_ele_ = val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_ele = val;
            break;
        case SP_DEFELE:
            if (sd->state.lr_flag != 2)
                sd->def_ele = val;
            break;
        case SP_MAXHP:
            if (sd->state.lr_flag != 2)
                sd->status.max_hp += val;
            break;
        case SP_MAXSP:
            if (sd->state.lr_flag != 2)
                sd->status.max_sp += val;
            break;
        case SP_CASTRATE:
            if (sd->state.lr_flag != 2)
                sd->castrate += val;
            break;
        case SP_MAXHPRATE:
            if (sd->state.lr_flag != 2)
                sd->hprate += val;
            break;
        case SP_MAXSPRATE:
            if (sd->state.lr_flag != 2)
                sd->sprate += val;
            break;
        case SP_SPRATE:
            if (sd->state.lr_flag != 2)
                sd->dsprate += val;
            break;
        case SP_ATTACKRANGE:
            if (!sd->state.lr_flag)
                sd->attackrange += val;
            else if (sd->state.lr_flag == 1)
                sd->attackrange_ += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_range += val;
            break;
        case SP_ADD_SPEED:
            if (sd->state.lr_flag != 2)
                sd->speed -= val;
            break;
        case SP_SPEED_RATE:
            if (sd->state.lr_flag != 2)
            {
                if (sd->speed_rate > 100 - val)
                    sd->speed_rate = 100 - val;
            }
            break;
        case SP_SPEED_ADDRATE:
            if (sd->state.lr_flag != 2)
                sd->speed_add_rate = sd->speed_add_rate * (100 - val) / 100;
            break;
        case SP_ASPD:
            if (sd->state.lr_flag != 2)
                sd->aspd -= val * 10;
            break;
        case SP_ASPD_RATE:
            if (sd->state.lr_flag != 2)
            {
                if (sd->aspd_rate > 100 - val)
                    sd->aspd_rate = 100 - val;
            }
            break;
        case SP_ASPD_ADDRATE:
            if (sd->state.lr_flag != 2)
                sd->aspd_add_rate = sd->aspd_add_rate * (100 - val) / 100;
            break;
        case SP_HP_RECOV_RATE:
            if (sd->state.lr_flag != 2)
                sd->hprecov_rate += val;
            break;
        case SP_SP_RECOV_RATE:
            if (sd->state.lr_flag != 2)
                sd->sprecov_rate += val;
            break;
        case SP_CRITICAL_DEF:
            if (sd->state.lr_flag != 2)
                sd->critical_def += val;
            break;
        case SP_NEAR_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->near_attack_def_rate += val;
            break;
        case SP_LONG_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->long_attack_def_rate += val;
            break;
        case SP_DOUBLE_RATE:
            if (sd->state.lr_flag == 0 && sd->double_rate < val)
                sd->double_rate = val;
            break;
        case SP_DOUBLE_ADD_RATE:
            if (sd->state.lr_flag == 0)
                sd->double_add_rate += val;
            break;
        case SP_MATK_RATE:
            if (sd->state.lr_flag != 2)
                sd->matk_rate += val;
            break;
        case SP_IGNORE_DEF_ELE:
            if (!sd->state.lr_flag)
                sd->ignore_def_ele |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->ignore_def_ele_ |= 1 << val;
            break;
        case SP_IGNORE_DEF_RACE:
            if (!sd->state.lr_flag)
                sd->ignore_def_race |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->ignore_def_race_ |= 1 << val;
            break;
        case SP_ATK_RATE:
            if (sd->state.lr_flag != 2)
                sd->atk_rate += val;
            break;
        case SP_MAGIC_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->magic_def_rate += val;
            break;
        case SP_MISC_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->misc_def_rate += val;
            break;
        case SP_IGNORE_MDEF_ELE:
            if (sd->state.lr_flag != 2)
                sd->ignore_mdef_ele |= 1 << val;
            break;
        case SP_IGNORE_MDEF_RACE:
            if (sd->state.lr_flag != 2)
                sd->ignore_mdef_race |= 1 << val;
            break;
        case SP_PERFECT_HIT_RATE:
            if (sd->state.lr_flag != 2 && sd->perfect_hit < val)
                sd->perfect_hit = val;
            break;
        case SP_PERFECT_HIT_ADD_RATE:
            if (sd->state.lr_flag != 2)
                sd->perfect_hit_add += val;
            break;
        case SP_CRITICAL_RATE:
            if (sd->state.lr_flag != 2)
                sd->critical_rate += val;
            break;
        case SP_GET_ZENY_NUM:
            if (sd->state.lr_flag != 2 && sd->get_zeny_num < val)
                sd->get_zeny_num = val;
            break;
        case SP_ADD_GET_ZENY_NUM:
            if (sd->state.lr_flag != 2)
                sd->get_zeny_add_num += val;
            break;
        case SP_DEF_RATIO_ATK_ELE:
            if (!sd->state.lr_flag)
                sd->def_ratio_atk_ele |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->def_ratio_atk_ele_ |= 1 << val;
            break;
        case SP_DEF_RATIO_ATK_RACE:
            if (!sd->state.lr_flag)
                sd->def_ratio_atk_race |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->def_ratio_atk_race_ |= 1 << val;
            break;
        case SP_HIT_RATE:
            if (sd->state.lr_flag != 2)
                sd->hit_rate += val;
            break;
        case SP_FLEE_RATE:
            if (sd->state.lr_flag != 2)
                sd->flee_rate += val;
            break;
        case SP_FLEE2_RATE:
            if (sd->state.lr_flag != 2)
                sd->flee2_rate += val;
            break;
        case SP_DEF_RATE:
            if (sd->state.lr_flag != 2)
                sd->def_rate += val;
            break;
        case SP_DEF2_RATE:
            if (sd->state.lr_flag != 2)
                sd->def2_rate += val;
            break;
        case SP_MDEF_RATE:
            if (sd->state.lr_flag != 2)
                sd->mdef_rate += val;
            break;
        case SP_MDEF2_RATE:
            if (sd->state.lr_flag != 2)
                sd->mdef2_rate += val;
            break;
        case SP_RESTART_FULL_RECORVER:
            if (sd->state.lr_flag != 2)
                sd->special_state.restart_full_recover = 1;
            break;
        case SP_NO_CASTCANCEL:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_castcancel = 1;
            break;
        case SP_NO_CASTCANCEL2:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_castcancel2 = 1;
            break;
        case SP_NO_SIZEFIX:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_sizefix = 1;
            break;
        case SP_NO_MAGIC_DAMAGE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_magic_damage = 1;
            break;
        case SP_NO_WEAPON_DAMAGE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_weapon_damage = 1;
            break;
        case SP_NO_GEMSTONE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_gemstone = 1;
            break;
        case SP_INFINITE_ENDURE:
            if (sd->state.lr_flag != 2)
                sd->special_state.infinite_endure = 1;
            break;
        case SP_SPLASH_RANGE:
            if (sd->state.lr_flag != 2 && sd->splash_range < val)
                sd->splash_range = val;
            break;
        case SP_SPLASH_ADD_RANGE:
            if (sd->state.lr_flag != 2)
                sd->splash_add_range += val;
            break;
        case SP_SHORT_WEAPON_DAMAGE_RETURN:
            if (sd->state.lr_flag != 2)
                sd->short_weapon_damage_return += val;
            break;
        case SP_LONG_WEAPON_DAMAGE_RETURN:
            if (sd->state.lr_flag != 2)
                sd->long_weapon_damage_return += val;
            break;
        case SP_MAGIC_DAMAGE_RETURN:   //AppleGirl Was Here
            if (sd->state.lr_flag != 2)
                sd->magic_damage_return += val;
            break;
        case SP_ALL_STATS:     // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->parame[SP_STR - SP_STR] += val;
                sd->parame[SP_AGI - SP_STR] += val;
                sd->parame[SP_VIT - SP_STR] += val;
                sd->parame[SP_INT - SP_STR] += val;
                sd->parame[SP_DEX - SP_STR] += val;
                sd->parame[SP_LUK - SP_STR] += val;
                clif_updatestatus (sd, 13);
                clif_updatestatus (sd, 14);
                clif_updatestatus (sd, 15);
                clif_updatestatus (sd, 16);
                clif_updatestatus (sd, 17);
                clif_updatestatus (sd, 18);
            }
            break;
        case SP_AGI_VIT:       // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->parame[SP_AGI - SP_STR] += val;
                sd->parame[SP_VIT - SP_STR] += val;
                clif_updatestatus (sd, 14);
                clif_updatestatus (sd, 15);
            }
            break;
        case SP_AGI_DEX_STR:   // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->parame[SP_AGI - SP_STR] += val;
                sd->parame[SP_DEX - SP_STR] += val;
                sd->parame[SP_STR - SP_STR] += val;
                clif_updatestatus (sd, 14);
                clif_updatestatus (sd, 17);
                clif_updatestatus (sd, 13);
            }
            break;
        case SP_PERFECT_HIDE:  // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->perfect_hiding = 1;
            }
            break;
        case SP_DISGUISE:      // Disguise script for items [Valaris]
            if (sd->state.lr_flag != 2 && sd->disguiseflag == 0)
            {
                if (pc_isriding (sd))
                {               // temporary prevention of crash caused by peco + disguise, will look into a better solution [Valaris]
                    clif_displaymessage (sd->fd,
                                         "Cannot wear disguise when riding a Peco.");
                    break;
                }
                sd->disguise = val;
                clif_clearchar (&sd->bl, 9);
                pc_setpos (sd, sd->mapname, sd->bl.x, sd->bl.y, 3);
            }
            break;
        case SP_UNBREAKABLE:
            if (sd->state.lr_flag != 2)
            {
                sd->unbreakable += val;
            }
            break;
        case SP_DEAF:
            sd->special_state.deaf = 1;
            break;
        default:
            if (battle_config.error_log)
                printf ("pc_bonus: unknown type %d %d !\n", type, val);
            break;
    }
    return 0;
}

/*==========================================
 * �� ���i�ɂ����\�͓��̃{�[�i�X�ݒ�
 *------------------------------------------
 */
int pc_bonus2 (struct map_session_data *sd, int type, int type2, int val)
{
    int  i;

    nullpo_retr (0, sd);

    switch (type)
    {
        case SP_ADDELE:
            if (!sd->state.lr_flag)
                sd->addele[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addele_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addele[type2] += val;
            break;
        case SP_ADDRACE:
            if (!sd->state.lr_flag)
                sd->addrace[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addrace_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addrace[type2] += val;
            break;
        case SP_ADDSIZE:
            if (!sd->state.lr_flag)
                sd->addsize[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addsize_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addsize[type2] += val;
            break;
        case SP_SUBELE:
            if (sd->state.lr_flag != 2)
                sd->subele[type2] += val;
            break;
        case SP_SUBRACE:
            if (sd->state.lr_flag != 2)
                sd->subrace[type2] += val;
            break;
        case SP_ADDEFF:
            if (sd->state.lr_flag != 2)
                sd->addeff[type2] += val;
            else
                sd->arrow_addeff[type2] += val;
            break;
        case SP_ADDEFF2:
            if (sd->state.lr_flag != 2)
                sd->addeff2[type2] += val;
            else
                sd->arrow_addeff2[type2] += val;
            break;
        case SP_RESEFF:
            if (sd->state.lr_flag != 2)
                sd->reseff[type2] += val;
            break;
        case SP_MAGIC_ADDELE:
            if (sd->state.lr_flag != 2)
                sd->magic_addele[type2] += val;
            break;
        case SP_MAGIC_ADDRACE:
            if (sd->state.lr_flag != 2)
                sd->magic_addrace[type2] += val;
            break;
        case SP_MAGIC_SUBRACE:
            if (sd->state.lr_flag != 2)
                sd->magic_subrace[type2] += val;
            break;
        case SP_ADD_DAMAGE_CLASS:
            if (!sd->state.lr_flag)
            {
                for (i = 0; i < sd->add_damage_class_count; i++)
                {
                    if (sd->add_damage_classid[i] == type2)
                    {
                        sd->add_damage_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_damage_class_count
                    && sd->add_damage_class_count < 10)
                {
                    sd->add_damage_classid[sd->add_damage_class_count] =
                        type2;
                    sd->add_damage_classrate[sd->add_damage_class_count] +=
                        val;
                    sd->add_damage_class_count++;
                }
            }
            else if (sd->state.lr_flag == 1)
            {
                for (i = 0; i < sd->add_damage_class_count_; i++)
                {
                    if (sd->add_damage_classid_[i] == type2)
                    {
                        sd->add_damage_classrate_[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_damage_class_count_
                    && sd->add_damage_class_count_ < 10)
                {
                    sd->add_damage_classid_[sd->add_damage_class_count_] =
                        type2;
                    sd->add_damage_classrate_[sd->add_damage_class_count_] +=
                        val;
                    sd->add_damage_class_count_++;
                }
            }
            break;
        case SP_ADD_MAGIC_DAMAGE_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_magic_damage_class_count; i++)
                {
                    if (sd->add_magic_damage_classid[i] == type2)
                    {
                        sd->add_magic_damage_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_magic_damage_class_count
                    && sd->add_magic_damage_class_count < 10)
                {
                    sd->add_magic_damage_classid
                        [sd->add_magic_damage_class_count] = type2;
                    sd->add_magic_damage_classrate
                        [sd->add_magic_damage_class_count] += val;
                    sd->add_magic_damage_class_count++;
                }
            }
            break;
        case SP_ADD_DEF_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_def_class_count; i++)
                {
                    if (sd->add_def_classid[i] == type2)
                    {
                        sd->add_def_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_def_class_count
                    && sd->add_def_class_count < 10)
                {
                    sd->add_def_classid[sd->add_def_class_count] = type2;
                    sd->add_def_classrate[sd->add_def_class_count] += val;
                    sd->add_def_class_count++;
                }
            }
            break;
        case SP_ADD_MDEF_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_mdef_class_count; i++)
                {
                    if (sd->add_mdef_classid[i] == type2)
                    {
                        sd->add_mdef_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_mdef_class_count
                    && sd->add_mdef_class_count < 10)
                {
                    sd->add_mdef_classid[sd->add_mdef_class_count] = type2;
                    sd->add_mdef_classrate[sd->add_mdef_class_count] += val;
                    sd->add_mdef_class_count++;
                }
            }
            break;
        case SP_HP_DRAIN_RATE:
            if (!sd->state.lr_flag)
            {
                sd->hp_drain_rate += type2;
                sd->hp_drain_per += val;
            }
            else if (sd->state.lr_flag == 1)
            {
                sd->hp_drain_rate_ += type2;
                sd->hp_drain_per_ += val;
            }
            break;
        case SP_SP_DRAIN_RATE:
            if (!sd->state.lr_flag)
            {
                sd->sp_drain_rate += type2;
                sd->sp_drain_per += val;
            }
            else if (sd->state.lr_flag == 1)
            {
                sd->sp_drain_rate_ += type2;
                sd->sp_drain_per_ += val;
            }
            break;
        case SP_WEAPON_COMA_ELE:
            if (sd->state.lr_flag != 2)
                sd->weapon_coma_ele[type2] += val;
            break;
        case SP_WEAPON_COMA_RACE:
            if (sd->state.lr_flag != 2)
                sd->weapon_coma_race[type2] += val;
            break;
        case SP_RANDOM_ATTACK_INCREASE:    // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->random_attack_increase_add = type2;
                sd->random_attack_increase_per += val;
                break;
            }                   // end addition
        default:
            if (battle_config.error_log)
                printf ("pc_bonus2: unknown type %d %d %d!\n", type, type2,
                        val);
            break;
    }
    return 0;
}

int pc_bonus3 (struct map_session_data *sd, int type, int type2, int type3,
               int val)
{
    int  i;
    switch (type)
    {
        case SP_ADD_MONSTER_DROP_ITEM:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->monster_drop_item_count; i++)
                {
                    if (sd->monster_drop_itemid[i] == type2)
                    {
                        sd->monster_drop_race[i] |= 1 << type3;
                        if (sd->monster_drop_itemrate[i] < val)
                            sd->monster_drop_itemrate[i] = val;
                        break;
                    }
                }
                if (i >= sd->monster_drop_item_count
                    && sd->monster_drop_item_count < 10)
                {
                    sd->monster_drop_itemid[sd->monster_drop_item_count] =
                        type2;
                    sd->monster_drop_race[sd->monster_drop_item_count] |=
                        1 << type3;
                    sd->monster_drop_itemrate[sd->monster_drop_item_count] =
                        val;
                    sd->monster_drop_item_count++;
                }
            }
            break;
        case SP_AUTOSPELL:
            if (sd->state.lr_flag != 2)
            {
                sd->autospell_id = type2;
                sd->autospell_lv = type3;
                sd->autospell_rate = val;
            }
            break;
        default:
            if (battle_config.error_log)
                printf ("pc_bonus3: unknown type %d %d %d %d!\n", type, type2,
                        type3, val);
            break;
    }

    return 0;
}

/*==========================================
 * �X�N���v�g�ɂ����X�L������
 *------------------------------------------
 */
int pc_skill (struct map_session_data *sd, int id, int level, int flag)
{
    nullpo_retr (0, sd);

    if (level > MAX_SKILL_LEVEL)
    {
        if (battle_config.error_log)
            printf ("support card skill only!\n");
        return 0;
    }
    if (!flag && (sd->status.skill[id].id == id || level == 0))
    {                           // �N�G�X�g�����Ȃ炱���ŏ���m�F���đ��M����
        sd->status.skill[id].lv = level;
        pc_calcstatus (sd, 0);
        clif_skillinfoblock (sd);
    }
    else if (sd->status.skill[id].lv < level)
    {                           // �o�������邪lv���������Ȃ�
        sd->status.skill[id].id = id;
        sd->status.skill[id].lv = level;
    }

    return 0;
}

/*==========================================
 * �J�[�h�}��
 *------------------------------------------
 */
int pc_insert_card (struct map_session_data *sd, int idx_card, int idx_equip)
{
    nullpo_retr (0, sd);

    if (idx_card >= 0 && idx_card < MAX_INVENTORY && idx_equip >= 0
        && idx_equip < MAX_INVENTORY && sd->inventory_data[idx_card])
    {
        int  i;
        int  nameid = sd->status.inventory[idx_equip].nameid;
        int  cardid = sd->status.inventory[idx_card].nameid;
        int  ep = sd->inventory_data[idx_card]->equip;

        if (nameid <= 0 || sd->inventory_data[idx_equip] == NULL || (sd->inventory_data[idx_equip]->type != 4 && sd->inventory_data[idx_equip]->type != 5) ||   // �� ����Ȃ�
            (sd->status.inventory[idx_equip].identify == 0) ||  // ���Ӓ�
            (sd->status.inventory[idx_equip].card[0] == 0x00ff) ||  // ��������
            (sd->status.inventory[idx_equip].card[0] == 0x00fe) || ((sd->inventory_data[idx_equip]->equip & ep) == 0) ||    // �� �����Ⴂ
            (sd->inventory_data[idx_equip]->type == 4 && ep == 32) ||   // �� �蕐���Ə��J�[�h
            (sd->status.inventory[idx_equip].card[0] == (short) 0xff00)
            || sd->status.inventory[idx_equip].equip)
        {

            clif_insert_card (sd, idx_equip, idx_card, 1);
            return 0;
        }
        for (i = 0; i < sd->inventory_data[idx_equip]->slot; i++)
        {
            if (sd->status.inventory[idx_equip].card[i] == 0)
            {
                // �󂫃X���b�g��������̂ō�������
                sd->status.inventory[idx_equip].card[i] = cardid;

                // �J�[�h�͌��炷
                clif_insert_card (sd, idx_equip, idx_card, 0);
                pc_delitem (sd, idx_card, 1, 1);
                return 0;
            }
        }
    }
    else
        clif_insert_card (sd, idx_equip, idx_card, 1);

    return 0;
}

//
// �A�C�e����
//

/*==========================================
 * �X�L���ɂ��锃���l�C��
 *------------------------------------------
 */
int pc_modifybuyvalue (struct map_session_data *sd, int orig_value)
{
    int  skill, val = orig_value, rate1 = 0, rate2 = 0;
    if ((skill = pc_checkskill (sd, MC_DISCOUNT)) > 0)  // �f�B�X�J�E���g
        rate1 = 5 + skill * 2 - ((skill == 10) ? 1 : 0);
    if ((skill = pc_checkskill (sd, RG_COMPULSION)) > 0)    // �R���p���V�����f�B�X�J�E���g
        rate2 = 5 + skill * 4;
    if (rate1 < rate2)
        rate1 = rate2;
    if (rate1)
        val = (int) ((double) orig_value * (double) (100 - rate1) / 100.);
    if (val < 0)
        val = 0;
    if (orig_value > 0 && val < 1)
        val = 1;

    return val;
}

/*==========================================
 * �X�L���ɂ��锄���l�C��
 *------------------------------------------
 */
int pc_modifysellvalue (struct map_session_data *sd, int orig_value)
{
    int  skill, val = orig_value, rate = 0;
    if ((skill = pc_checkskill (sd, MC_OVERCHARGE)) > 0)    // �I�[�o�[�`���[�W
        rate = 5 + skill * 2 - ((skill == 10) ? 1 : 0);
    if (rate)
        val = (int) ((double) orig_value * (double) (100 + rate) / 100.);
    if (val < 0)
        val = 0;
    if (orig_value > 0 && val < 1)
        val = 1;

    return val;
}

/*==========================================
 * �A�C�e���𔃂�����ɁA�V�����A�C�e�������g�����A
 * 3������ɂ����邩�m�F
 *------------------------------------------
 */
int pc_checkadditem (struct map_session_data *sd, int nameid, int amount)
{
    int  i;

    nullpo_retr (0, sd);

    if (itemdb_isequip (nameid))
        return ADDITEM_NEW;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == nameid)
        {
            if (sd->status.inventory[i].amount + amount > MAX_AMOUNT)
                return ADDITEM_OVERAMOUNT;
            return ADDITEM_EXIST;
        }
    }

    if (amount > MAX_AMOUNT)
        return ADDITEM_OVERAMOUNT;
    return ADDITEM_NEW;
}

/*==========================================
 * �󂫃A�C�e�����̌�
 *------------------------------------------
 */
int pc_inventoryblank (struct map_session_data *sd)
{
    int  i, b;

    nullpo_retr (0, sd);

    for (i = 0, b = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == 0)
            b++;
    }

    return b;
}

/*==========================================
 * �����𕥂�
 *------------------------------------------
 */
int pc_payzeny (struct map_session_data *sd, int zeny)
{
    double z;

    nullpo_retr (0, sd);

    z = (double) sd->status.zeny;
    if (sd->status.zeny < zeny || z - (double) zeny > MAX_ZENY)
        return 1;
    sd->status.zeny -= zeny;
    clif_updatestatus (sd, SP_ZENY);

    return 0;
}

/*==========================================
 * �����𓾂�
 *------------------------------------------
 */
int pc_getzeny (struct map_session_data *sd, int zeny)
{
    double z;

    nullpo_retr (0, sd);

    z = (double) sd->status.zeny;
    if (z + (double) zeny > MAX_ZENY)
    {
        zeny = 0;
        sd->status.zeny = MAX_ZENY;
    }
    sd->status.zeny += zeny;
    clif_updatestatus (sd, SP_ZENY);

    return 0;
}

/*==========================================
 * �A�C�e�����T���āA�C���f�b�N�X���Ԃ�
 *------------------------------------------
 */
int pc_search_inventory (struct map_session_data *sd, int item_id)
{
    int  i;

    nullpo_retr (-1, sd);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == item_id &&
            (sd->status.inventory[i].amount > 0 || item_id == 0))
            return i;
    }

    return -1;
}

int pc_count_all_items (struct map_session_data *player, int item_id)
{
    int  i;
    int  count = 0;

    nullpo_retr (0, player);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (player->status.inventory[i].nameid == item_id)
            count += player->status.inventory[i].amount;
    }

    return count;
}

int pc_remove_items (struct map_session_data *player, int item_id, int count)
{
    int  i;

    nullpo_retr (0, player);

    for (i = 0; i < MAX_INVENTORY && count; i++)
    {
        if (player->status.inventory[i].nameid == item_id)
        {
            int  to_delete = count;
            /* only delete as much as we have */
            if (to_delete > player->status.inventory[i].amount)
                to_delete = player->status.inventory[i].amount;

            count -= to_delete;

            pc_delitem (player, i, to_delete,
                        0 /* means `really delete and update status' */ );

            if (!count)
                return 0;
        }
    }
    return 0;
}

/*==========================================
 * �A�C�e���ǉ�B���̂�item�\���̂̐����𖳎�
 *------------------------------------------
 */
int pc_additem (struct map_session_data *sd, struct item *item_data,
                int amount)
{
    struct item_data *data;
    int  i, w;

    MAP_LOG_PC (sd, "PICKUP %d %d", item_data->nameid, amount);

    nullpo_retr (1, sd);
    nullpo_retr (1, item_data);

    if (item_data->nameid <= 0 || amount <= 0)
        return 1;
    data = itemdb_search (item_data->nameid);
    if ((w = data->weight * amount) + sd->weight > sd->max_weight)
        return 2;

    i = MAX_INVENTORY;

    if (!itemdb_isequip2 (data))
    {
        // �� ���i�ł͂Ȃ��̂ŁA�����L�i�Ȃ����̂ݕω�������
        for (i = 0; i < MAX_INVENTORY; i++)
            if (sd->status.inventory[i].nameid == item_data->nameid &&
                sd->status.inventory[i].card[0] == item_data->card[0]
                && sd->status.inventory[i].card[1] == item_data->card[1]
                && sd->status.inventory[i].card[2] == item_data->card[2]
                && sd->status.inventory[i].card[3] == item_data->card[3])
            {
                if (sd->status.inventory[i].amount + amount > MAX_AMOUNT)
                    return 5;
                sd->status.inventory[i].amount += amount;
                clif_additem (sd, i, amount, 0);
                break;
            }
    }
    if (i >= MAX_INVENTORY)
    {
        // �� ���i�������L�i������̂ŋ󂫗��֒ǉ�
        i = pc_search_inventory (sd, 0);
        if (i >= 0)
        {
            memcpy (&sd->status.inventory[i], item_data,
                    sizeof (sd->status.inventory[0]));

            if (item_data->equip)
                sd->status.inventory[i].equip = 0;

            sd->status.inventory[i].amount = amount;
            sd->inventory_data[i] = data;
            clif_additem (sd, i, amount, 0);
        }
        else
            return 4;
    }
    sd->weight += w;
    clif_updatestatus (sd, SP_WEIGHT);

    return 0;
}

/*==========================================
 * �A�C�e����炷
 *------------------------------------------
 */
int pc_delitem (struct map_session_data *sd, int n, int amount, int type)
{
    nullpo_retr (1, sd);

    if (sd->trade_partner != 0)
        trade_tradecancel (sd);

    if (sd->status.inventory[n].nameid == 0 || amount <= 0
        || sd->status.inventory[n].amount < amount
        || sd->inventory_data[n] == NULL)
        return 1;

    sd->status.inventory[n].amount -= amount;
    sd->weight -= sd->inventory_data[n]->weight * amount;
    if (sd->status.inventory[n].amount <= 0)
    {
        if (sd->status.inventory[n].equip)
            pc_unequipitem (sd, n, 0);
        memset (&sd->status.inventory[n], 0,
                sizeof (sd->status.inventory[0]));
        sd->inventory_data[n] = NULL;
    }
    if (!(type & 1))
        clif_delitem (sd, n, amount);
    if (!(type & 2))
        clif_updatestatus (sd, SP_WEIGHT);

    return 0;
}

/*==========================================
 * �A�C�e���𗎂�
 *------------------------------------------
 */
int pc_dropitem (struct map_session_data *sd, int n, int amount)
{
    nullpo_retr (1, sd);

    if (sd->trade_partner != 0 || sd->npc_id != 0 || sd->state.storage_flag)
        return 0;               // no dropping while trading/npc/storage

    if (n < 0 || n >= MAX_INVENTORY)
        return 0;

    if (amount <= 0)
        return 0;

    pc_unequipinvyitem (sd, n, 0);

    if (sd->status.inventory[n].nameid <= 0 ||
        sd->status.inventory[n].amount < amount ||
        sd->trade_partner != 0 || sd->status.inventory[n].amount <= 0)
        return 1;
    map_addflooritem (&sd->status.inventory[n], amount, sd->bl.m, sd->bl.x,
                      sd->bl.y, NULL, NULL, NULL, 0);
    pc_delitem (sd, n, amount, 0);

    return 0;
}

/*==========================================
 * �A�C�e�����E��
 *------------------------------------------
 */

static int can_pick_item_up_from (struct map_session_data *self, int other_id)
{
    struct party *p = party_search (self->status.party_id);

    /* From ourselves or from no-one? */
    if (!self || self->bl.id == other_id || !other_id)
        return 1;

    struct map_session_data *other = map_id2sd (other_id);

    /* Other no longer exists? */
    if (!other)
        return 1;

    /* From our partner? */
    if (self->status.partner_id == other->status.char_id)
        return 1;

    /* From a party member? */
    if (self->status.party_id
        && self->status.party_id == other->status.party_id
        && p && p->item != 0)
        return 1;

    /* From someone who is far away? */
    /* On another map? */
    if (other->bl.m != self->bl.m)
        return 1;
    else
    {
        int  distance_x = abs (other->bl.x - self->bl.x);
        int  distance_y = abs (other->bl.y - self->bl.y);
        int  distance = (distance_x > distance_y) ? distance_x : distance_y;

        return distance > battle_config.drop_pickup_safety_zone;
    }
}

int pc_takeitem (struct map_session_data *sd, struct flooritem_data *fitem)
{
    int  flag;
    unsigned int tick = gettick ();
    int  can_take;

    nullpo_retr (0, sd);
    nullpo_retr (0, fitem);

    /* Sometimes the owners reported to us are buggy: */

    if (fitem->first_get_id == fitem->third_get_id
        || fitem->second_get_id == fitem->third_get_id)
        fitem->third_get_id = 0;

    if (fitem->first_get_id == fitem->second_get_id)
    {
        fitem->second_get_id = fitem->third_get_id;
        fitem->third_get_id = 0;
    }

    can_take = can_pick_item_up_from (sd, fitem->first_get_id);
    if (!can_take)
        can_take = fitem->first_get_tick <= tick
            && can_pick_item_up_from (sd, fitem->second_get_id);

    if (!can_take)
        can_take = fitem->second_get_tick <= tick
            && can_pick_item_up_from (sd, fitem->third_get_id);

    if (!can_take)
        can_take = fitem->third_get_tick <= tick;

    if (can_take)
    {
        /* Can pick up */

        if ((flag =
             pc_additem (sd, &fitem->item_data, fitem->item_data.amount)))
            // �d��over�Ŏ擾���s
            clif_additem (sd, 0, 0, flag);
        else
        {
            /* �擾���� */
            if (sd->attacktimer != -1)
                pc_stopattack (sd);
            clif_takeitem (&sd->bl, &fitem->bl);
            map_clearflooritem (fitem->bl.id);
        }
        return 0;
    }

    /* Otherwise, we can't pick up */
    clif_additem (sd, 0, 0, 6);
    return 0;
}

int pc_isUseitem (struct map_session_data *sd, int n)
{
    struct item_data *item;
    int  nameid;

    nullpo_retr (0, sd);

    item = sd->inventory_data[n];
    nameid = sd->status.inventory[n].nameid;

    if (item == NULL)
        return 0;
    if (itemdb_type (nameid) != 0)
        return 0;
    if ((nameid == 605) && map[sd->bl.m].flag.gvg)
        return 0;
    if (nameid == 601
        && (map[sd->bl.m].flag.noteleport || map[sd->bl.m].flag.gvg))
    {
        clif_skill_teleportmessage (sd, 0);
        return 0;
    }

    if (nameid == 602 && map[sd->bl.m].flag.noreturn)
        return 0;
    if (nameid == 604
        && (map[sd->bl.m].flag.nobranch || map[sd->bl.m].flag.gvg))
        return 0;
    if (item->sex != 2 && sd->status.sex != item->sex)
        return 0;
    if (item->elv > 0 && sd->status.base_level < item->elv)
        return 0;

    return 1;
}

/*==========================================
 * �A�C�e�����g��
 *------------------------------------------
 */
int pc_useitem (struct map_session_data *sd, int n)
{
    int  nameid, amount;

    nullpo_retr (1, sd);

    if (n >= 0 && n < MAX_INVENTORY && sd->inventory_data[n])
    {
        nameid = sd->status.inventory[n].nameid;
        amount = sd->status.inventory[n].amount;
        if (sd->status.inventory[n].nameid <= 0
            || sd->status.inventory[n].amount <= 0
            || sd->sc_data[SC_BERSERK].timer != -1 || !pc_isUseitem (sd, n))
        {
            clif_useitemack (sd, n, 0, 0);
            return 1;
        }

        run_script (sd->inventory_data[n]->use_script, 0, sd->bl.id, 0);

        clif_useitemack (sd, n, amount - 1, 1);
        pc_delitem (sd, n, 1, 1);
    }

    return 0;
}

/*==========================================
 * �J�[�g�A�C�e���ǉ�B���̂�item�\���̂̐����𖳎�
 *------------------------------------------
 */
int pc_cart_additem (struct map_session_data *sd, struct item *item_data,
                     int amount)
{
    struct item_data *data;
    int  i, w;

    nullpo_retr (1, sd);
    nullpo_retr (1, item_data);

    if (item_data->nameid <= 0 || amount <= 0)
        return 1;
    data = itemdb_search (item_data->nameid);

    if ((w = data->weight * amount) + sd->cart_weight > sd->cart_max_weight)
        return 1;

    i = MAX_CART;
    if (!itemdb_isequip2 (data))
    {
        // �� ���i�ł͂Ȃ��̂ŁA�����L�i�Ȃ����̂ݕω�������
        for (i = 0; i < MAX_CART; i++)
        {
            if (sd->status.cart[i].nameid == item_data->nameid &&
                sd->status.cart[i].card[0] == item_data->card[0]
                && sd->status.cart[i].card[1] == item_data->card[1]
                && sd->status.cart[i].card[2] == item_data->card[2]
                && sd->status.cart[i].card[3] == item_data->card[3])
            {
                if (sd->status.cart[i].amount + amount > MAX_AMOUNT)
                    return 1;
                sd->status.cart[i].amount += amount;
                clif_cart_additem (sd, i, amount, 0);
                break;
            }
        }
    }
    if (i >= MAX_CART)
    {
        // �� ���i�������L�i������̂ŋ󂫗��֒ǉ�
        for (i = 0; i < MAX_CART; i++)
        {
            if (sd->status.cart[i].nameid == 0)
            {
                memcpy (&sd->status.cart[i], item_data,
                        sizeof (sd->status.cart[0]));
                sd->status.cart[i].amount = amount;
                sd->cart_num++;
                clif_cart_additem (sd, i, amount, 0);
                break;
            }
        }
        if (i >= MAX_CART)
            return 1;
    }
    sd->cart_weight += w;
    clif_updatestatus (sd, SP_CARTINFO);

    return 0;
}

/*==========================================
 * �J�[�g�A�C�e����炷
 *------------------------------------------
 */
int pc_cart_delitem (struct map_session_data *sd, int n, int amount, int type)
{
    nullpo_retr (1, sd);

    if (sd->status.cart[n].nameid == 0 || sd->status.cart[n].amount < amount)
        return 1;

    sd->status.cart[n].amount -= amount;
    sd->cart_weight -= itemdb_weight (sd->status.cart[n].nameid) * amount;
    if (sd->status.cart[n].amount <= 0)
    {
        memset (&sd->status.cart[n], 0, sizeof (sd->status.cart[0]));
        sd->cart_num--;
    }
    if (!type)
    {
        clif_cart_delitem (sd, n, amount);
        clif_updatestatus (sd, SP_CARTINFO);
    }

    return 0;
}

/*==========================================
 * �J�[�g�փA�C�e���ړ�
 *------------------------------------------
 */
int pc_putitemtocart (struct map_session_data *sd, int idx, int amount)
{
    struct item *item_data;

    nullpo_retr (0, sd);
    nullpo_retr (0, item_data = &sd->status.inventory[idx]);

    if (item_data->nameid == 0 || item_data->amount < amount)
        return 1;
    if (pc_cart_additem (sd, item_data, amount) == 0)
        return pc_delitem (sd, idx, amount, 0);

    return 1;
}

/*==========================================
 * �J�[�g���̃A�C�e�����m�F(���̍������Ԃ�)
 *------------------------------------------
 */
int pc_cartitem_amount (struct map_session_data *sd, int idx, int amount)
{
    struct item *item_data;

    nullpo_retr (-1, sd);
    nullpo_retr (-1, item_data = &sd->status.cart[idx]);

    if (item_data->nameid == 0 || !item_data->amount)
        return -1;
    return item_data->amount - amount;
}

/*==========================================
 * �J�[�g�����A�C�e���ړ�
 *------------------------------------------
 */

int pc_getitemfromcart (struct map_session_data *sd, int idx, int amount)
{
    struct item *item_data;
    int  flag;

    nullpo_retr (0, sd);
    nullpo_retr (0, item_data = &sd->status.cart[idx]);

    if (item_data->nameid == 0 || item_data->amount < amount)
        return 1;
    if ((flag = pc_additem (sd, item_data, amount)) == 0)
        return pc_cart_delitem (sd, idx, amount, 0);

    clif_additem (sd, 0, 0, flag);
    return 1;
}

/*==========================================
 * �A�C�e���Ӓ�
 *------------------------------------------
 */
int pc_item_identify (struct map_session_data *sd, int idx)
{
    int  flag = 1;

    nullpo_retr (0, sd);

    if (idx >= 0 && idx < MAX_INVENTORY)
    {
        if (sd->status.inventory[idx].nameid > 0
            && sd->status.inventory[idx].identify == 0)
        {
            flag = 0;
            sd->status.inventory[idx].identify = 1;
        }
        clif_item_identified (sd, idx, flag);
    }
    else
        clif_item_identified (sd, idx, flag);

    return !flag;
}

/*==========================================
 * �X�e�B���i���J
 *------------------------------------------
 */
int pc_show_steal (struct block_list *bl, va_list ap)
{
    struct map_session_data *sd;
    int  itemid;
    int  type;

    struct item_data *item = NULL;
    char output[100];

    nullpo_retr (0, bl);
    nullpo_retr (0, ap);
    nullpo_retr (0, sd = va_arg (ap, struct map_session_data *));

    itemid = va_arg (ap, int);
    type = va_arg (ap, int);

    if (!type)
    {
        if ((item = itemdb_exists (itemid)) == NULL)
            sprintf (output, "%s stole an Unknown_Item.", sd->status.name);
        else
            sprintf (output, "%s stole %s.", sd->status.name, item->jname);
        clif_displaymessage (((struct map_session_data *) bl)->fd, output);
    }
    else
    {
        sprintf (output,
                 "%s has not stolen the item because of being  overweight.",
                 sd->status.name);
        clif_displaymessage (((struct map_session_data *) bl)->fd, output);
    }

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
//** pc.c: Small Steal Item fix by fritz
int pc_steal_item (struct map_session_data *sd, struct block_list *bl)
{
    if (sd != NULL && bl != NULL && bl->type == BL_MOB)
    {
        int  i, skill, rate, itemid, flag, count;
        struct mob_data *md;
        md = (struct mob_data *) bl;
        if (!md->state.steal_flag && mob_db[md->class].mexp <= 0 && !(mob_db[md->class].mode & 0x20) && md->sc_data[SC_STONE].timer == -1 && md->sc_data[SC_FREEZE].timer == -1 && (!(md->class > 1324 && md->class < 1364)))   // prevent stealing from treasure boxes [Valaris]
        {
            skill =
                sd->paramc[4] - mob_db[md->class].dex + pc_checkskill (sd,
                                                                       TF_STEAL)
                + 10;

            if (0 < skill)
            {
                for (count = 8; count <= 8 && count != 0; count--)
                {
                    i = rand () % 8;
                    itemid = mob_db[md->class].dropitem[i].nameid;

                    if (itemid > 0 && itemdb_type (itemid) != 6)
                    {
                        rate =
                            (mob_db[md->class].dropitem[i].p /
                             battle_config.item_rate_common * 100 * skill) /
                            100;

                        if (rand () % 10000 < rate)
                        {
                            struct item tmp_item;
                            memset (&tmp_item, 0, sizeof (tmp_item));
                            tmp_item.nameid = itemid;
                            tmp_item.amount = 1;
                            tmp_item.identify = 1;
                            flag = pc_additem (sd, &tmp_item, 1);
                            if (battle_config.show_steal_in_same_party)
                            {
                                party_foreachsamemap (pc_show_steal, sd, 1,
                                                      sd, tmp_item.nameid, 0);
                            }

                            if (flag)
                            {
                                if (battle_config.show_steal_in_same_party)
                                {
                                    party_foreachsamemap (pc_show_steal, sd,
                                                          1, sd,
                                                          tmp_item.nameid, 1);
                                }

                                clif_additem (sd, 0, 0, flag);
                            }
                            md->state.steal_flag = 1;
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int pc_steal_coin (struct map_session_data *sd, struct block_list *bl)
{
    if (sd != NULL && bl != NULL && bl->type == BL_MOB)
    {
        int  rate, skill;
        struct mob_data *md = (struct mob_data *) bl;
        if (md && !md->state.steal_coin_flag && md->sc_data
            && md->sc_data[SC_STONE].timer == -1
            && md->sc_data[SC_FREEZE].timer == -1)
        {
            skill = pc_checkskill (sd, RG_STEALCOIN) * 10;
            rate =
                skill + (sd->status.base_level - mob_db[md->class].lv) * 3 +
                sd->paramc[4] * 2 + sd->paramc[5] * 2;
            if (MRAND (1000) < rate)
            {
                pc_getzeny (sd, mob_db[md->class].lv * 10 + MRAND (100));
                md->state.steal_coin_flag = 1;
                return 1;
            }
        }
    }

    return 0;
}

//
//
//
/*==========================================
 * PC�̈ʒu�ݒ�
 *------------------------------------------
 */
int pc_setpos (struct map_session_data *sd, char *mapname_org, int x, int y,
               int clrtype)
{
    char mapname[24];
    int  m = 0, c = 0, disguise = 0;

    nullpo_retr (0, sd);

    if (sd->chatID)             // �`���b�g�����o��
        chat_leavechat (sd);
    if (sd->trade_partner)      // �����𒆒f����
        trade_tradecancel (sd);
    if (sd->state.storage_flag == 1)
        storage_storage_quit (sd);  // �q�ɂ��J���Ă��Ȃ��ۑ�����
    else if (sd->state.storage_flag == 2)
        storage_guild_storage_quit (sd, 0);

    if (sd->party_invite > 0)   // �p�[�e�B���U��ۂ���
        party_reply_invite (sd, sd->party_invite_account, 0);
    if (sd->guild_invite > 0)   // �M���h���U��ۂ���
        guild_reply_invite (sd, sd->guild_invite, 0);
    if (sd->guild_alliance > 0) // �M���h�������U��ۂ���
        guild_reply_reqalliance (sd, sd->guild_alliance_account, 0);

    skill_castcancel (&sd->bl, 0);  // �r�����f
    pc_stop_walking (sd, 0);    // ���s���f
    pc_stopattack (sd);         // �U�����f

    if (pc_issit (sd))
    {
//        pc_setstand (sd); // [fate] Nothing wrong with warping while sitting
        skill_gangsterparadise (sd, 0);
    }

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)
        skill_status_change_end (&sd->bl, SC_TRICKDEAD, -1);
    if (sd->status.option & 2)
        skill_status_change_end (&sd->bl, SC_HIDING, -1);
    if (sd->status.option & 4)
        skill_status_change_end (&sd->bl, SC_CLOAKING, -1);
    if (sd->status.option & 16386)
        skill_status_change_end (&sd->bl, SC_CHASEWALK, -1);
    if (sd->sc_data[SC_BLADESTOP].timer != -1)
        skill_status_change_end (&sd->bl, SC_BLADESTOP, -1);
    if (sd->sc_data[SC_DANCING].timer != -1)    // clear dance effect when warping [Valaris]
        skill_stop_dancing (&sd->bl, 0);

    if (sd->disguise)
    {                           // clear disguises when warping [Valaris]
        clif_clearchar (&sd->bl, 9);
        disguise = sd->disguise;
        sd->disguise = 0;
    }

    memcpy (mapname, mapname_org, 24);
    mapname[16] = 0;
    if (strstr (mapname, ".gat") == NULL && strlen (mapname) < 16)
    {
        strcat (mapname, ".gat");
    }

    m = map_mapname2mapid (mapname);
    if (m < 0)
    {
        if (sd->mapname[0])
        {
            int  ip, port;
            if (map_mapname2ipport (mapname, &ip, &port) == 0)
            {
                skill_stop_dancing (&sd->bl, 1);
                skill_unit_out_all (&sd->bl, gettick (), 1);
                clif_clearchar_area (&sd->bl, clrtype & 0xffff);
                skill_gangsterparadise (sd, 0);
                map_delblock (&sd->bl);
                memcpy (sd->mapname, mapname, 24);
                sd->bl.x = x;
                sd->bl.y = y;
                sd->state.waitingdisconnect = 1;
                pc_makesavestatus (sd);
                //The storage close routines save the char data. [Skotlex]
                if (!sd->state.storage_flag)
                    chrif_save (sd);
                else if (sd->state.storage_flag == 1)
                    storage_storage_quit (sd);
                else if (sd->state.storage_flag == 2)
                    storage_guild_storage_quit (sd, 1);

                chrif_changemapserver (sd, mapname, x, y, ip, port);
                return 0;
            }
        }
#if 0
        clif_authfail_fd (sd->fd, 0);   // cancel
        clif_setwaitclose (sd->fd);
#endif
        return 1;
    }

    if (x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys)
        x = y = 0;
    if ((x == 0 && y == 0) || (c = read_gat (m, x, y)) == 1 || c == 5)
    {
        if (x || y)
        {
            if (battle_config.error_log)
                printf ("stacked (%d,%d)\n", x, y);
        }
        do
        {
            x = MRAND (map[m].xs - 2) + 1;
            y = MRAND (map[m].ys - 2) + 1;
        }
        while ((c = read_gat (m, x, y)) == 1 || c == 5);
    }

    if (sd->mapname[0] && sd->bl.prev != NULL)
    {
        skill_unit_out_all (&sd->bl, gettick (), 1);
        clif_clearchar_area (&sd->bl, clrtype & 0xffff);
        skill_gangsterparadise (sd, 0);
        map_delblock (&sd->bl);
        clif_changemap (sd, map[m].name, x, y); // [MouseJstr]
    }

    if (disguise)               // disguise teleport fix [Valaris]
        sd->disguise = disguise;

    memcpy (sd->mapname, mapname, 24);
    sd->bl.m = m;
    sd->to_x = x;
    sd->to_y = y;

    // moved and changed dance effect stopping  

    sd->bl.x = x;
    sd->bl.y = y;

//  map_addblock(&sd->bl);  /// �u���b�N�o�^��spawn��
//  clif_spawnpc(sd);

    return 0;
}

/*==========================================
 * PC�̃����_�����[�v
 *------------------------------------------
 */
int pc_randomwarp (struct map_session_data *sd, int type)
{
    int  x, y, c, i = 0;
    int  m;

    nullpo_retr (0, sd);

    m = sd->bl.m;

    if (map[sd->bl.m].flag.noteleport)  // �e���|�[�g�֎~
        return 0;

    do
    {
        x = MRAND (map[m].xs - 2) + 1;
        y = MRAND (map[m].ys - 2) + 1;
    }
    while (((c = read_gat (m, x, y)) == 1 || c == 5) && (i++) < 1000);

    if (i < 1000)
        pc_setpos (sd, map[m].name, x, y, type);

    return 0;
}

/*==========================================
 * ���݈ʒu�̃���
 *------------------------------------------
 */
int pc_memo (struct map_session_data *sd, int i)
{
    int  skill;
    int  j;

    nullpo_retr (0, sd);

    skill = pc_checkskill (sd, AL_WARP);

    if (i >= MIN_PORTAL_MEMO)
        i -= MIN_PORTAL_MEMO;
    else if (map[sd->bl.m].flag.nomemo
             || (map[sd->bl.m].flag.nowarpto
                 && battle_config.any_warp_GM_min_level > pc_isGM (sd)))
    {
        clif_skill_teleportmessage (sd, 1);
        return 0;
    }

    if (skill < 1)
    {
        clif_skill_memo (sd, 2);
    }

    if (skill < 2 || i < -1 || i > 2)
    {
        clif_skill_memo (sd, 1);
        return 0;
    }

    for (j = 0; j < 3; j++)
    {
        if (strcmp (sd->status.memo_point[j].map, map[sd->bl.m].name) == 0)
        {
            i = j;
            break;
        }
    }

    if (i == -1)
    {
        for (i = skill - 3; i >= 0; i--)
        {
            memcpy (&sd->status.memo_point[i + 1], &sd->status.memo_point[i],
                    sizeof (struct point));
        }
        i = 0;
    }
    memcpy (sd->status.memo_point[i].map, map[sd->bl.m].name, 24);
    sd->status.memo_point[i].x = sd->bl.x;
    sd->status.memo_point[i].y = sd->bl.y;

    clif_skill_memo (sd, 0);

    return 1;
}

/*==========================================
 *
 *------------------------------------------
 */
int pc_can_reach (struct map_session_data *sd, int x, int y)
{
    struct walkpath_data wpd;

    nullpo_retr (0, sd);

    if (sd->bl.x == x && sd->bl.y == y) // �����}�X
        return 1;

    // ���Q������
    wpd.path_len = 0;
    wpd.path_pos = 0;
    wpd.path_half = 0;
    return (path_search (&wpd, sd->bl.m, sd->bl.x, sd->bl.y, x, y, 0) !=
            -1) ? 1 : 0;
}

//
// �� �s��
//
/*==========================================
 * ����1���ɂ����鎞�Ԃ��v�Z
 *------------------------------------------
 */
static int calc_next_walk_step (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->walkpath.path_pos >= sd->walkpath.path_len)
        return -1;
    if (sd->walkpath.path[sd->walkpath.path_pos] & 1)
        return sd->speed * 14 / 10;

    return sd->speed;
}

/*==========================================
 * �����i��(timer�֐�)
 *------------------------------------------
 */
static int pc_walk (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd;
    int  i, ctype;
    int  moveblock;
    int  x, y, dx, dy;

    sd = map_id2sd (id);
    if (sd == NULL)
        return 0;

    if (sd->walktimer != tid)
    {
        if (battle_config.error_log)
            printf ("pc_walk %d != %d\n", sd->walktimer, tid);
        return 0;
    }
    sd->walktimer = -1;
    if (sd->walkpath.path_pos >= sd->walkpath.path_len
        || sd->walkpath.path_pos != data)
        return 0;

    //�����̂ő����̃^�C�}�[�����
    sd->inchealspirithptick = 0;
    sd->inchealspiritsptick = 0;

    sd->walkpath.path_half ^= 1;
    if (sd->walkpath.path_half == 0)
    {                           // �}�X�ڒ��S�֓���
        sd->walkpath.path_pos++;
        if (sd->state.change_walk_target)
        {
            pc_walktoxy_sub (sd);
            return 0;
        }
    }
    else
    {                           // �}�X�ڋ��E�֓���
        if (sd->walkpath.path[sd->walkpath.path_pos] >= 8)
            return 1;

        x = sd->bl.x;
        y = sd->bl.y;
        ctype = map_getcell (sd->bl.m, x, y);
        if (ctype == 1 || ctype == 5)
        {
            pc_stop_walking (sd, 1);
            return 0;
        }
        sd->dir = sd->head_dir = sd->walkpath.path[sd->walkpath.path_pos];
        dx = dirx[(int) sd->dir];
        dy = diry[(int) sd->dir];
        ctype = map_getcell (sd->bl.m, x + dx, y + dy);
        if (ctype == 1 || ctype == 5)
        {
            pc_walktoxy_sub (sd);
            return 0;
        }

        moveblock = (x / BLOCK_SIZE != (x + dx) / BLOCK_SIZE
                     || y / BLOCK_SIZE != (y + dy) / BLOCK_SIZE);

        sd->walktimer = 1;
        map_foreachinmovearea (clif_pcoutsight, sd->bl.m, x - AREA_SIZE,
                               y - AREA_SIZE, x + AREA_SIZE, y + AREA_SIZE,
                               dx, dy, 0, sd);

        x += dx;
        y += dy;

        if (moveblock)
            map_delblock (&sd->bl);
        sd->bl.x = x;
        sd->bl.y = y;
        if (moveblock)
            map_addblock (&sd->bl);

        if (sd->sc_data[SC_DANCING].timer != -1)
            skill_unit_move_unit_group ((struct skill_unit_group *)
                                        sd->sc_data[SC_DANCING].val2,
                                        sd->bl.m, dx, dy);

        map_foreachinmovearea (clif_pcinsight, sd->bl.m, x - AREA_SIZE,
                               y - AREA_SIZE, x + AREA_SIZE, y + AREA_SIZE,
                               -dx, -dy, 0, sd);
        sd->walktimer = -1;

        if (sd->status.party_id > 0)
        {                       // �p�[�e�B�̂g�o�����ʒm����
            struct party *p = party_search (sd->status.party_id);
            if (p != NULL)
            {
                int  p_flag = 0;
                map_foreachinmovearea (party_send_hp_check, sd->bl.m,
                                       x - AREA_SIZE, y - AREA_SIZE,
                                       x + AREA_SIZE, y + AREA_SIZE, -dx, -dy,
                                       BL_PC, sd->status.party_id, &p_flag);
                if (p_flag)
                    sd->party_hp = -1;
            }
        }
        if (sd->status.option & 4)  // �N���[�L���O�̏�Ō���
            skill_check_cloaking (&sd->bl);
        /* �f�B�{�[�V�������� */
        for (i = 0; i < 5; i++)
            if (sd->dev.val1[i])
            {
                skill_devotion3 (&sd->bl, sd->dev.val1[i]);
                break;
            }
        /* ���f�B�{�[�V�������� */
        if (sd->sc_data && sd->sc_data[SC_DEVOTION].val1)
        {
            skill_devotion2 (&sd->bl, sd->sc_data[SC_DEVOTION].val1);
        }

        skill_unit_move (&sd->bl, tick, 1); // �X�L�����j�b�g�̌���

        if (map_getcell (sd->bl.m, x, y) & 0x80)
            npc_touch_areanpc (sd, sd->bl.m, x, y);
        else
            sd->areanpc_id = 0;
    }
    if ((i = calc_next_walk_step (sd)) > 0)
    {
        i = i >> 1;
        if (i < 1 && sd->walkpath.path_half == 0)
            i = 1;
        sd->walktimer =
            add_timer (tick + i, pc_walk, id, sd->walkpath.path_pos);
    }

    return 0;
}

/*==========================================
 * �ړ��\���m�F���āA�\�Ȃ����s�J�n
 *------------------------------------------
 */
static int pc_walktoxy_sub (struct map_session_data *sd)
{
    struct walkpath_data wpd;
    int  i;

    nullpo_retr (1, sd);

    if (path_search
        (&wpd, sd->bl.m, sd->bl.x, sd->bl.y, sd->to_x, sd->to_y, 0))
        return 1;
    memcpy (&sd->walkpath, &wpd, sizeof (wpd));

    clif_walkok (sd);
    sd->state.change_walk_target = 0;

    if ((i = calc_next_walk_step (sd)) > 0)
    {
        i = i >> 2;
        sd->walktimer = add_timer (gettick () + i, pc_walk, sd->bl.id, 0);
    }
    clif_movechar (sd);

    return 0;
}

/*==========================================
 * pc�� �s�v��
 *------------------------------------------
 */
int pc_walktoxy (struct map_session_data *sd, int x, int y)
{

    nullpo_retr (0, sd);

    sd->to_x = x;
    sd->to_y = y;

    if (pc_issit (sd))
        pc_setstand (sd);

    if (sd->walktimer != -1 && sd->state.change_walk_target == 0)
    {
        // ���ݕ��Ă����Œ��̖ړI�n�ύX�Ȃ̂Ń}�X�ڂ̒��S�ɗ�������
        // timer�֐�����pc_walktoxy_sub���ĂԂ悤�ɂ���
        sd->state.change_walk_target = 1;
    }
    else
    {
        pc_walktoxy_sub (sd);
    }

    return 0;
}

/*==========================================
 * �� �s���~
 *------------------------------------------
 */
int pc_stop_walking (struct map_session_data *sd, int type)
{
    nullpo_retr (0, sd);

    if (sd->walktimer != -1)
    {
        delete_timer (sd->walktimer, pc_walk);
        sd->walktimer = -1;
    }
    sd->walkpath.path_len = 0;
    sd->to_x = sd->bl.x;
    sd->to_y = sd->bl.y;
    if (type & 0x01)
        clif_fixpos (&sd->bl);
    if (type & 0x02 && battle_config.pc_damage_delay)
    {
        unsigned int tick = gettick ();
        int  delay = battle_get_dmotion (&sd->bl);
        if (sd->canmove_tick < tick)
            sd->canmove_tick = tick + delay;
    }

    return 0;
}

void pc_touch_all_relevant_npcs (struct map_session_data *sd)
{
    if (map_getcell (sd->bl.m, sd->bl.x, sd->bl.y) & 0x80)
        npc_touch_areanpc (sd, sd->bl.m, sd->bl.x, sd->bl.y);
    else
        sd->areanpc_id = 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int pc_movepos (struct map_session_data *sd, int dst_x, int dst_y)
{
    int  moveblock;
    int  dx, dy, dist;

    struct walkpath_data wpd;

    nullpo_retr (0, sd);

    if (path_search (&wpd, sd->bl.m, sd->bl.x, sd->bl.y, dst_x, dst_y, 0))
        return 1;

    sd->dir = sd->head_dir = map_calc_dir (&sd->bl, dst_x, dst_y);

    dx = dst_x - sd->bl.x;
    dy = dst_y - sd->bl.y;
    dist = distance (sd->bl.x, sd->bl.y, dst_x, dst_y);

    moveblock = (sd->bl.x / BLOCK_SIZE != dst_x / BLOCK_SIZE
                 || sd->bl.y / BLOCK_SIZE != dst_y / BLOCK_SIZE);

    map_foreachinmovearea (clif_pcoutsight, sd->bl.m, sd->bl.x - AREA_SIZE,
                           sd->bl.y - AREA_SIZE, sd->bl.x + AREA_SIZE,
                           sd->bl.y + AREA_SIZE, dx, dy, 0, sd);

    if (moveblock)
        map_delblock (&sd->bl);
    sd->bl.x = dst_x;
    sd->bl.y = dst_y;
    if (moveblock)
        map_addblock (&sd->bl);

    map_foreachinmovearea (clif_pcinsight, sd->bl.m, sd->bl.x - AREA_SIZE,
                           sd->bl.y - AREA_SIZE, sd->bl.x + AREA_SIZE,
                           sd->bl.y + AREA_SIZE, -dx, -dy, 0, sd);

    if (sd->status.party_id > 0)
    {                           // �p�[�e�B�̂g�o�����ʒm����
        struct party *p = party_search (sd->status.party_id);
        if (p != NULL)
        {
            int  flag = 0;
            map_foreachinmovearea (party_send_hp_check, sd->bl.m,
                                   sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
                                   sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE,
                                   -dx, -dy, BL_PC, sd->status.party_id,
                                   &flag);
            if (flag)
                sd->party_hp = -1;
        }
    }

    if (sd->status.option & 4)  // �N���[�L���O�̏�Ō���
        skill_check_cloaking (&sd->bl);

    skill_unit_move (&sd->bl, gettick (), dist + 7);    // �X�L�����j�b�g�̌���

    pc_touch_all_relevant_npcs (sd);
    return 0;
}

//
// �����퓬
//
/*==========================================
 * �X�L���̌��� ���L���Ă����ꍇLv���Ԃ�
 *------------------------------------------
 */
int pc_checkskill (struct map_session_data *sd, int skill_id)
{
    if (sd == NULL)
        return 0;
    if (skill_id >= 10000)
    {
        struct guild *g;
        if (sd->status.guild_id > 0
            && (g = guild_search (sd->status.guild_id)) != NULL)
            return guild_checkskill (g, skill_id);
        return 0;
    }

    if (sd->status.skill[skill_id].id == skill_id)
        return (sd->status.skill[skill_id].lv);

    return 0;
}

/*==========================================
 * �����ύX�ɂ����X�L���̌p���`�F�b�N
 * �����F
 *   struct map_session_data *sd	�Z�b�V�����f�[�^
 *   int nameid						�����iID
 * �Ԃ��l�F
 *   0		�ύX�Ȃ�
 *   -1		�X�L��������
 *------------------------------------------
 */
int pc_checkallowskill (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->sc_data == NULL)
        return 0;

    if (!(skill_get_weapontype (KN_TWOHANDQUICKEN) & (1 << sd->status.weapon))
        && sd->sc_data[SC_TWOHANDQUICKEN].timer != -1)
    {                           // 2HQ
        skill_status_change_end (&sd->bl, SC_TWOHANDQUICKEN, -1);   // 2HQ������
        return -1;
    }
    if (!(skill_get_weapontype (LK_AURABLADE) & (1 << sd->status.weapon))
        && sd->sc_data[SC_AURABLADE].timer != -1)
    {                           /* �I�[���u���[�h */
        skill_status_change_end (&sd->bl, SC_AURABLADE, -1);    /* �I�[���u���[�h������ */
        return -1;
    }
    if (!(skill_get_weapontype (LK_PARRYING) & (1 << sd->status.weapon))
        && sd->sc_data[SC_PARRYING].timer != -1)
    {                           /* �p���C���O */
        skill_status_change_end (&sd->bl, SC_PARRYING, -1); /* �p���C���O������ */
        return -1;
    }
    if (!(skill_get_weapontype (LK_CONCENTRATION) & (1 << sd->status.weapon))
        && sd->sc_data[SC_CONCENTRATION].timer != -1)
    {                           /* �R���Z���g���[�V���� */
        skill_status_change_end (&sd->bl, SC_CONCENTRATION, -1);    /* �R���Z���g���[�V���������� */
        return -1;
    }
    if (!(skill_get_weapontype (CR_SPEARQUICKEN) & (1 << sd->status.weapon))
        && sd->sc_data[SC_SPEARSQUICKEN].timer != -1)
    {                           // �X�s�A�N�B�b�P��
        skill_status_change_end (&sd->bl, SC_SPEARSQUICKEN, -1);    // �X�s�A�N�C�b�P��������
        return -1;
    }
    if (!(skill_get_weapontype (BS_ADRENALINE) & (1 << sd->status.weapon))
        && sd->sc_data[SC_ADRENALINE].timer != -1)
    {                           // �A�h���i�������b�V��
        skill_status_change_end (&sd->bl, SC_ADRENALINE, -1);   // �A�h���i�������b�V��������
        return -1;
    }

    if (sd->status.shield <= 0)
    {
        if (sd->sc_data[SC_AUTOGUARD].timer != -1)
        {                       // �I�[�g�K�[�h
            skill_status_change_end (&sd->bl, SC_AUTOGUARD, -1);
            return -1;
        }
        if (sd->sc_data[SC_DEFENDER].timer != -1)
        {                       // �f�B�t�F���_�[
            skill_status_change_end (&sd->bl, SC_DEFENDER, -1);
            return -1;
        }
        if (sd->sc_data[SC_REFLECTSHIELD].timer != -1)
        {                       //���t���N�g�V�[���h
            skill_status_change_end (&sd->bl, SC_REFLECTSHIELD, -1);
            return -1;
        }
    }

    return 0;
}

/*==========================================
 * �� ���i�̃`�F�b�N
 *------------------------------------------
 */
int pc_checkequip (struct map_session_data *sd, int pos)
{
    int  i;

    nullpo_retr (-1, sd);

    for (i = 0; i < 11; i++)
    {
        if (pos & equip_pos[i])
            return sd->equip_index[i];
    }

    return -1;
}

/*==========================================
 * �]���E���{�q�E�̌��̐E�Ƃ��Ԃ�
 *------------------------------------------
 */
struct pc_base_job pc_calc_base_job (int b_class)
{
    struct pc_base_job bj;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    if (b_class < MAX_PC_CLASS)
    {                           //�ʏ�
        bj.job = b_class;
        bj.upper = 0;
    }
    else if (b_class >= 4001 && b_class < 4023)
    {                           //�]���E
        bj.job = b_class - 4001;
        bj.upper = 1;
    }
    else if (b_class == 23 + 4023 - 1)
    {                           //�{�q�X�p�m�r
        bj.job = b_class - (4023 - 1);
        bj.upper = 2;
    }
    else
    {                           //�{�q�X�p�m�r�ȊO�̗{�q
        bj.job = b_class - 4023;
        bj.upper = 2;
    }

    if (battle_config.enable_upper_class == 0)
    {                           //conf�Ŗ����ɂȂ�Ă�����upper=0
        bj.upper = 0;
    }

    if (bj.job == 0)
    {
        bj.type = 0;
    }
    else if (bj.job < 7)
    {
        bj.type = 1;
    }
    else
    {
        bj.type = 2;
    }

    return bj;
}

/*==========================================
 * PC�̍U�� (timer�֐�)
 *------------------------------------------
 */
int pc_attack_timer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd;
    struct block_list *bl;
    struct status_change *sc_data;
    short *opt;
    int  dist, skill, range;
    int  attack_spell_delay;

    sd = map_id2sd (id);
    if (sd == NULL)
        return 0;
    if (sd->attacktimer != tid)
    {
        if (battle_config.error_log)
            printf ("pc_attack_timer %d != %d\n", sd->attacktimer, tid);
        return 0;
    }
    sd->attacktimer = -1;

    if (sd->bl.prev == NULL)
        return 0;

    bl = map_id2bl (sd->attacktarget);
    if (bl == NULL || bl->prev == NULL)
        return 0;

    if (bl->type == BL_PC && pc_isdead ((struct map_session_data *) bl))
        return 0;

    // ����map�łȂ��Ȃ��U�����Ȃ�
    // PC�������łĂ��U�����Ȃ�
    if (sd->bl.m != bl->m || pc_isdead (sd))
        return 0;

    if (sd->opt1 > 0 || sd->status.option & 2 || sd->status.option & 16388) // �ُ��ȂǂōU���ł��Ȃ�
        return 0;

    if (sd->sc_data[SC_AUTOCOUNTER].timer != -1)
        return 0;
    if (sd->sc_data[SC_BLADESTOP].timer != -1)
        return 0;

    if ((opt = battle_get_option (bl)) != NULL && *opt & 0x46)
        return 0;
    if (((sc_data = battle_get_sc_data (bl)) != NULL
         && sc_data[SC_TRICKDEAD].timer != -1)
        || ((sc_data = battle_get_sc_data (bl)) != NULL
            && sc_data[SC_BASILICA].timer != -1))
        return 0;

    if (sd->skilltimer != -1 && pc_checkskill (sd, SA_FREECAST) <= 0)
        return 0;

    if (!battle_config.sdelay_attack_enable
        && pc_checkskill (sd, SA_FREECAST) <= 0)
    {
        if (DIFF_TICK (tick, sd->canact_tick) < 0)
        {
            clif_skill_fail (sd, 1, 4, 0);
            return 0;
        }
    }

    if (sd->attackabletime > tick)
        return 0;               // cannot attack yet

    attack_spell_delay = sd->attack_spell_delay;
    if (sd->attack_spell_override   // [Fate] If we have an active attack spell, use that
        && spell_attack (id, sd->attacktarget))
    {
        // Return if the spell succeeded.  If the spell had disspiated, spell_attack() may fail.
        sd->attackabletime = tick + attack_spell_delay;

    }
    else
    {
        dist = distance (sd->bl.x, sd->bl.y, bl->x, bl->y);
        range = sd->attackrange;
        if (sd->status.weapon != 11)
            range++;
        if (dist > range)
        {                       // �� ���Ȃ��̂ňړ�
            //if(pc_can_reach(sd,bl->x,bl->y))
            //clif_movetoattack(sd,bl);
            return 0;
        }

        if (dist <= range && !battle_check_range (&sd->bl, bl, range))
        {
            if (pc_can_reach (sd, bl->x, bl->y) && sd->canmove_tick < tick
                && (sd->sc_data[SC_ANKLE].timer == -1
                    || sd->sc_data[SC_SPIDERWEB].timer == -1))
                // TMW client doesn't support this
                //pc_walktoxy(sd,bl->x,bl->y);
                clif_movetoattack (sd, bl);
            sd->attackabletime = tick + (sd->aspd << 1);
        }
        else
        {
            if (battle_config.pc_attack_direction_change)
                sd->dir = sd->head_dir = map_calc_dir (&sd->bl, bl->x, bl->y);  // �����ݒ�

            if (sd->walktimer != -1)
                pc_stop_walking (sd, 1);

            if (sd->sc_data[SC_COMBO].timer == -1)
            {
                map_freeblock_lock ();
                pc_stop_walking (sd, 0);
                sd->attacktarget_lv =
                    battle_weapon_attack (&sd->bl, bl, tick, 0);
                if (!(battle_config.pc_cloak_check_type & 2)
                    && sd->sc_data[SC_CLOAKING].timer != -1)
                    skill_status_change_end (&sd->bl, SC_CLOAKING, -1);
                map_freeblock_unlock ();
                if (sd->skilltimer != -1 && (skill = pc_checkskill (sd, SA_FREECAST)) > 0)  // �t���[�L���X�g
                    sd->attackabletime =
                        tick + ((sd->aspd << 1) * (150 - skill * 5) / 100);
                else
                    sd->attackabletime = tick + (sd->aspd << 1);
            }
            else if (sd->attackabletime <= tick)
            {
                if (sd->skilltimer != -1 && (skill = pc_checkskill (sd, SA_FREECAST)) > 0)  // �t���[�L���X�g
                    sd->attackabletime =
                        tick + ((sd->aspd << 1) * (150 - skill * 5) / 100);
                else
                    sd->attackabletime = tick + (sd->aspd << 1);
            }
            if (sd->attackabletime <= tick)
                sd->attackabletime = tick + (battle_config.max_aspd << 1);
        }
    }

    if (sd->state.attack_continue)
    {
        sd->attacktimer =
            add_timer (sd->attackabletime, pc_attack_timer, sd->bl.id, 0);
    }

    return 0;
}

/*==========================================
 * �U���v��
 * type��1�Ȃ��p���U��
 *------------------------------------------
 */
int pc_attack (struct map_session_data *sd, int target_id, int type)
{
    struct block_list *bl;
    int  d;

    nullpo_retr (0, sd);

    bl = map_id2bl (target_id);
    if (bl == NULL)
        return 1;

    if (bl->type == BL_NPC)
    {                           // monster npcs [Valaris]
        npc_click (sd, RFIFOL (sd->fd, 2));
        return 0;
    }

    if (!battle_check_target (&sd->bl, bl, BCT_ENEMY))
        return 1;
    if (sd->attacktimer != -1)
        pc_stopattack (sd);
    sd->attacktarget = target_id;
    sd->state.attack_continue = type;

    d = DIFF_TICK (sd->attackabletime, gettick ());
    if (d > 0 && d < 2000)
    {                           // �U��delay��
        sd->attacktimer =
            add_timer (sd->attackabletime, pc_attack_timer, sd->bl.id, 0);
    }
    else
    {
        // �{��timer�֐��Ȃ̂ň�����킹��
        pc_attack_timer (-1, gettick (), sd->bl.id, 0);
    }

    return 0;
}

/*==========================================
 * �p���U�����~
 *------------------------------------------
 */
int pc_stopattack (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->attacktimer != -1)
    {
        delete_timer (sd->attacktimer, pc_attack_timer);
        sd->attacktimer = -1;
    }
    sd->attacktarget = 0;
    sd->state.attack_continue = 0;

    return 0;
}

int pc_follow_timer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd, *bl;

    sd = map_id2sd (id);
    if (sd == NULL || sd->followtimer != tid)
        return 0;

    sd->followtimer = -1;

    do
    {
        if (sd->bl.prev == NULL)
            break;

        bl = (struct map_session_data *) map_id2bl (sd->followtarget);

        if (bl == NULL)
            return 0;

        if (bl->bl.prev == NULL)
            break;

        if (bl->bl.type == BL_PC
            && pc_isdead ((struct map_session_data *) bl))
            return 0;

        if (sd->skilltimer == -1 && sd->attacktimer == -1
            && sd->walktimer == -1)
        {
            if ((sd->bl.m == bl->bl.m)
                && pc_can_reach (sd, bl->bl.x, bl->bl.y))
            {
                if (distance (sd->bl.x, sd->bl.y, bl->bl.x, bl->bl.y) > 5)
                    pc_walktoxy (sd, bl->bl.x, bl->bl.y);
            }
            else
                pc_setpos ((struct map_session_data *) sd, bl->mapname,
                           bl->bl.x, bl->bl.y, 3);
        }
    }
    while (0);

    sd->followtimer =
        add_timer (tick + sd->aspd, pc_follow_timer, sd->bl.id, 0);

    return 0;
}

int pc_follow (struct map_session_data *sd, int target_id)
{
    struct block_list *bl;

    bl = map_id2bl (target_id);
    if (bl == NULL)
        return 1;
    sd->followtarget = target_id;
    if (sd->followtimer != -1)
    {
        delete_timer (sd->followtimer, pc_follow_timer);
        sd->followtimer = -1;
    }

    pc_follow_timer (-1, gettick (), sd->bl.id, 0);

    return 0;
}

int pc_checkbaselevelup (struct map_session_data *sd)
{
    int  next = pc_nextbaseexp (sd);

    nullpo_retr (0, sd);

    if (sd->status.base_exp >= next && next > 0)
    {
        struct pc_base_job s_class = pc_calc_base_job (sd->status.class);

        // base�����x���A�b�v����
        sd->status.base_exp -= next;

        sd->status.base_level++;
        sd->status.status_point += (sd->status.base_level + 14) / 4;
        clif_updatestatus (sd, SP_STATUSPOINT);
        clif_updatestatus (sd, SP_BASELEVEL);
        clif_updatestatus (sd, SP_NEXTBASEEXP);
        pc_calcstatus (sd, 0);
        pc_heal (sd, sd->status.max_hp, sd->status.max_sp);

        //�X�p�m�r�̓L���G�A�C���|�A�}�j�s�A�O���A�T�t��Lv1��������
        if (s_class.job == 23)
        {
            skill_status_change_start (&sd->bl,
                                       SkillStatusChangeTable[PR_KYRIE], 1, 0,
                                       0, 0, skill_get_time (PR_KYRIE, 1), 0);
            skill_status_change_start (&sd->bl,
                                       SkillStatusChangeTable[PR_IMPOSITIO],
                                       1, 0, 0, 0,
                                       skill_get_time (PR_IMPOSITIO, 1), 0);
            skill_status_change_start (&sd->bl,
                                       SkillStatusChangeTable[PR_MAGNIFICAT],
                                       1, 0, 0, 0,
                                       skill_get_time (PR_MAGNIFICAT, 1), 0);
            skill_status_change_start (&sd->bl,
                                       SkillStatusChangeTable[PR_GLORIA], 1,
                                       0, 0, 0, skill_get_time (PR_GLORIA, 1),
                                       0);
            skill_status_change_start (&sd->bl,
                                       SkillStatusChangeTable[PR_SUFFRAGIUM],
                                       1, 0, 0, 0,
                                       skill_get_time (PR_SUFFRAGIUM, 1), 0);
        }

        clif_misceffect (&sd->bl, 0);
        //���x���A�b�v�����̂Ńp�[�e�B�[�������X�V����
        //(��͈̓`�F�b�N)
        party_send_movemap (sd);
        MAP_LOG_XP (sd, "LEVELUP") return 1;
    }

    return 0;
}

/*========================================
 * Compute the maximum for sd->skill_point, i.e., the max. number of skill points that can still be filled in
 *----------------------------------------
 */
int pc_skillpt_potential (struct map_session_data *sd)
{
    int  skill_id;
    int  potential = 0;

#define RAISE_COST(x) (((x)*((x)-1))>>1)

    for (skill_id = 0; skill_id < MAX_SKILL; skill_id++)
        if (sd->status.skill[skill_id].id != 0
            && sd->status.skill[skill_id].lv < skill_db[skill_id].max_raise)
            potential += RAISE_COST (skill_db[skill_id].max_raise)
                - RAISE_COST (sd->status.skill[skill_id].lv);
#undef RAISE_COST

    return potential;
}

int pc_checkjoblevelup (struct map_session_data *sd)
{
    int  next = pc_nextjobexp (sd);

    nullpo_retr (0, sd);

    if (sd->status.job_exp >= next && next > 0)
    {
        if (pc_skillpt_potential (sd) < sd->status.skill_point)
        {                       // [Fate] Bah, this is is painful.
            // But the alternative is quite error-prone, and eAthena has far worse performance issues...
            sd->status.job_exp = next - 1;
            pc_calcstatus(sd,0);
            return 0;
        }

        // job�����x���A�b�v����
        sd->status.job_exp -= next;
        clif_updatestatus (sd, SP_NEXTJOBEXP);
        sd->status.skill_point++;
        clif_updatestatus (sd, SP_SKILLPOINT);
        pc_calcstatus (sd, 0);

        MAP_LOG_PC (sd, "SKILLPOINTS-UP %d", sd->status.skill_point);

        if (sd->status.job_level < 250
            && sd->status.job_level < sd->status.base_level * 2)
            sd->status.job_level++; // Make levelling up a little harder

        clif_misceffect (&sd->bl, 1);
        return 1;
    }

    return 0;
}

/*==========================================
 * �o���l�擾
 *------------------------------------------
 */
int pc_gainexp (struct map_session_data *sd, int base_exp, int job_exp)
{
    return pc_gainexp_reason (sd, base_exp, job_exp,
                              PC_GAINEXP_REASON_KILLING);
}

int pc_gainexp_reason (struct map_session_data *sd, int base_exp, int job_exp,
                       int reason)
{
    char output[256];
    nullpo_retr (0, sd);

    if (sd->bl.prev == NULL || pc_isdead (sd))
        return 0;

    if ((battle_config.pvp_exp == 0) && map[sd->bl.m].flag.pvp) // [MouseJstr]
        return 0;               // no exp on pvp maps

    MAP_LOG_PC (sd, "GAINXP %d %d %s", base_exp, job_exp,
                ((reason ==
                  2) ? "SCRIPTXP" : ((reason == 1) ? "HEALXP" : "KILLXP")));

    if (sd->sc_data[SC_RICHMANKIM].timer != -1)
    {                           // added bounds checking [Vaalris]
        base_exp +=
            base_exp * (25 + sd->sc_data[SC_RICHMANKIM].val1 * 25) / 100;
        job_exp +=
            job_exp * (25 + sd->sc_data[SC_RICHMANKIM].val1 * 25) / 100;
    }

    if (sd->status.guild_id > 0)
    {                           // �M���h�ɏ��[
        base_exp -= guild_payexp (sd, base_exp);
        if (base_exp < 0)
            base_exp = 0;
    }

    if (!battle_config.multi_level_up && pc_nextbaseafter (sd))
    {
        while (sd->status.base_exp + base_exp >= pc_nextbaseafter (sd)
               && sd->status.base_exp <= pc_nextbaseexp (sd)
               && pc_nextbaseafter (sd) > 0)
        {
            base_exp *= .90;
        }
    }

    sd->status.base_exp += base_exp;

    // [Fate] Adjust experience points that healers can extract from this character
    if (reason != PC_GAINEXP_REASON_HEALING)
    {
        const int max_heal_xp =
            20 + (sd->status.base_level * sd->status.base_level);

        sd->heal_xp += base_exp;
        if (sd->heal_xp > max_heal_xp)
            sd->heal_xp = max_heal_xp;
    }

    if (sd->status.base_exp < 0)
        sd->status.base_exp = 0;

    while (pc_checkbaselevelup (sd));

    clif_updatestatus (sd, SP_BASEEXP);
    if (!battle_config.multi_level_up && pc_nextjobafter (sd))
    {
        while (sd->status.job_exp + job_exp >= pc_nextjobafter (sd)
               && sd->status.job_exp <= pc_nextjobexp (sd)
               && pc_nextjobafter (sd) > 0)
        {
            job_exp *= .90;
        }
    }

    sd->status.job_exp += job_exp;
    if (sd->status.job_exp < 0)
        sd->status.job_exp = 0;

    while (pc_checkjoblevelup (sd));

    clif_updatestatus (sd, SP_JOBEXP);

    if (battle_config.disp_experience)
    {
        sprintf (output,
                 "Experienced Gained Base:%d Job:%d", base_exp, job_exp);
        clif_disp_onlyself (sd, output, strlen (output));
    }

    return 0;
}

int pc_extract_healer_exp (struct map_session_data *sd, int max)
{
    int  amount;
    nullpo_retr (0, sd);

    amount = sd->heal_xp;
    if (max < amount)
        amount = max;

    sd->heal_xp -= amount;
    return amount;
}

/*==========================================
 * base level���K�v�o���l�v�Z
 *------------------------------------------
 */
int pc_nextbaseexp (struct map_session_data *sd)
{
    int  i;

    nullpo_retr (0, sd);

    if (sd->status.base_level >= MAX_LEVEL || sd->status.base_level <= 0)
        return 0;

    if (sd->status.class == 0)
        i = 0;
    else if (sd->status.class <= 6)
        i = 1;
    else if (sd->status.class <= 22)
        i = 2;
    else if (sd->status.class == 23)
        i = 3;
    else if (sd->status.class == 4001)
        i = 4;
    else if (sd->status.class <= 4007)
        i = 5;
    else
        i = 6;

    return exp_table[i][sd->status.base_level - 1];
}

/*==========================================
 * job level���K�v�o���l�v�Z
 *------------------------------------------
 */
int pc_nextjobexp (struct map_session_data *sd)
{
    // [fate]  For normal levels, this ranges from 20k to 50k, depending on job level.
    // Job level is at most twice the player's experience level (base_level).  Levelling
    // from 2 to 9 is 44 points, i.e., 880k to 2.2M job experience points (this is per
    // skill, obviously.)

    return 20000 + sd->status.job_level * 150;
}

/*==========================================
 * base level after next [Valaris]
 *------------------------------------------
 */
int pc_nextbaseafter (struct map_session_data *sd)
{
    int  i;

    nullpo_retr (0, sd);

    if (sd->status.base_level >= MAX_LEVEL || sd->status.base_level <= 0)
        return 0;

    if (sd->status.class == 0)
        i = 0;
    else if (sd->status.class <= 6)
        i = 1;
    else if (sd->status.class <= 22)
        i = 2;
    else if (sd->status.class == 23)
        i = 3;
    else if (sd->status.class == 4001)
        i = 4;
    else if (sd->status.class <= 4007)
        i = 5;
    else
        i = 6;

    return exp_table[i][sd->status.base_level];
}

/*==========================================
 * job level after next [Valaris]
 *------------------------------------------
 */
int pc_nextjobafter (struct map_session_data *sd)
{
    int  i;

    nullpo_retr (0, sd);

    if (sd->status.job_level >= MAX_LEVEL || sd->status.job_level <= 0)
        return 0;

    if (sd->status.class == 0)
        i = 7;
    else if (sd->status.class <= 6)
        i = 8;
    else if (sd->status.class <= 22)
        i = 9;
    else if (sd->status.class == 23)
        i = 10;
    else if (sd->status.class == 4001)
        i = 11;
    else if (sd->status.class <= 4007)
        i = 12;
    else
        i = 13;

    return exp_table[i][sd->status.job_level];
}

/*==========================================

 * �K�v�X�e�[�^�X�|�C���g�v�Z
 *------------------------------------------
 */
int pc_need_status_point (struct map_session_data *sd, int type)
{
    int  val;

    nullpo_retr (-1, sd);

    if (type < SP_STR || type > SP_LUK)
        return -1;
    val =
        type == SP_STR ? sd->status.str :
        type == SP_AGI ? sd->status.agi :
        type == SP_VIT ? sd->status.vit :
        type == SP_INT ? sd->status.int_ :
        type == SP_DEX ? sd->status.dex : sd->status.luk;

    return (val + 9) / 10 + 1;
}

/*==========================================
 * �\�͒l����
 *------------------------------------------
 */
int pc_statusup (struct map_session_data *sd, int type)
{
    int  need, val = 0;

    nullpo_retr (0, sd);

    switch (type)
    {
        case SP_STR:
            val = sd->status.str;
            break;
        case SP_AGI:
            val = sd->status.agi;
            break;
        case SP_VIT:
            val = sd->status.vit;
            break;
        case SP_INT:
            val = sd->status.int_;
            break;
        case SP_DEX:
            val = sd->status.dex;
            break;
        case SP_LUK:
            val = sd->status.luk;
            break;
    }

    need = pc_need_status_point (sd, type);
    if (type < SP_STR || type > SP_LUK || need < 0
        || need > sd->status.status_point
        || val >= battle_config.max_parameter)
    {
        clif_statusupack (sd, type, 0, val);
        return 1;
    }
    switch (type)
    {
        case SP_STR:
            val = ++sd->status.str;
            break;
        case SP_AGI:
            val = ++sd->status.agi;
            break;
        case SP_VIT:
            val = ++sd->status.vit;
            break;
        case SP_INT:
            val = ++sd->status.int_;
            break;
        case SP_DEX:
            val = ++sd->status.dex;
            break;
        case SP_LUK:
            val = ++sd->status.luk;
            break;
    }
    sd->status.status_point -= need;
    if (need != pc_need_status_point (sd, type))
    {
        clif_updatestatus (sd, type - SP_STR + SP_USTR);
    }
    clif_updatestatus (sd, SP_STATUSPOINT);
    clif_updatestatus (sd, type);
    pc_calcstatus (sd, 0);
    clif_statusupack (sd, type, 1, val);

    MAP_LOG_STATS (sd, "STATUP");

    return 0;
}

/*==========================================
 * �\�͒l����
 *------------------------------------------
 */
int pc_statusup2 (struct map_session_data *sd, int type, int val)
{
    nullpo_retr (0, sd);

    if (type < SP_STR || type > SP_LUK)
    {
        clif_statusupack (sd, type, 0, 0);
        return 1;
    }
    switch (type)
    {
        case SP_STR:
            if (sd->status.str + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.str + val < 1)
                val = 1;
            else
                val += sd->status.str;
            sd->status.str = val;
            break;
        case SP_AGI:
            if (sd->status.agi + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.agi + val < 1)
                val = 1;
            else
                val += sd->status.agi;
            sd->status.agi = val;
            break;
        case SP_VIT:
            if (sd->status.vit + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.vit + val < 1)
                val = 1;
            else
                val += sd->status.vit;
            sd->status.vit = val;
            break;
        case SP_INT:
            if (sd->status.int_ + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.int_ + val < 1)
                val = 1;
            else
                val += sd->status.int_;
            sd->status.int_ = val;
            break;
        case SP_DEX:
            if (sd->status.dex + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.dex + val < 1)
                val = 1;
            else
                val += sd->status.dex;
            sd->status.dex = val;
            break;
        case SP_LUK:
            if (sd->status.luk + val >= battle_config.max_parameter)
                val = battle_config.max_parameter;
            else if (sd->status.luk + val < 1)
                val = 1;
            else
                val = sd->status.luk + val;
            sd->status.luk = val;
            break;
    }
    clif_updatestatus (sd, type - SP_STR + SP_USTR);
    clif_updatestatus (sd, type);
    pc_calcstatus (sd, 0);
    clif_statusupack (sd, type, 1, val);
    MAP_LOG_STATS (sd, "STATUP2");

    return 0;
}

/*==========================================
 * �X�L���|�C���g�����U��
 *------------------------------------------
 */
int pc_skillup (struct map_session_data *sd, int skill_num)
{
    nullpo_retr (0, sd);

    if (sd->status.skill[skill_num].id != 0
        && sd->status.skill_point >= sd->status.skill[skill_num].lv
        && sd->status.skill[skill_num].lv < skill_db[skill_num].max_raise)
    {
        sd->status.skill_point -= sd->status.skill[skill_num].lv;
        sd->status.skill[skill_num].lv++;

        pc_calcstatus (sd, 0);
        clif_skillup (sd, skill_num);
        clif_updatestatus (sd, SP_SKILLPOINT);
        clif_skillinfoblock (sd);
        MAP_LOG_PC(sd, "SKILLUP %d %d %d",
                   skill_num, sd->status.skill[skill_num].lv, skill_power(sd, skill_num));
    }

    return 0;
}

/*==========================================
 * /allskill
 *------------------------------------------
 */
int pc_allskillup (struct map_session_data *sd)
{
    int  i, id;
    int  c = 0, s = 0;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    s_class = pc_calc_base_job (sd->status.class);
    c = s_class.job;
    s = (s_class.upper == 1) ? 1 : 0;   //�]���ȊO�͒ʏ��̃X�L���H

    for (i = 0; i < MAX_SKILL; i++)
        sd->status.skill[i].id = 0;

    if (battle_config.gm_allskill > 0
        && pc_isGM (sd) >= battle_config.gm_allskill)
    {
        // �S�ẴX�L��
        for (i = 1; i < 158; i++)
            sd->status.skill[i].lv = skill_get_max (i);
        for (i = 210; i < 291; i++)
            sd->status.skill[i].lv = skill_get_max (i);
        for (i = 304; i < MAX_SKILL; i++)
            sd->status.skill[i].lv = skill_get_max (i);
    }
    else
    {
        for (i = 0; (id = skill_tree[s][c][i].id) > 0; i++)
        {
            if (sd->status.skill[id].id == 0 && skill_get_inf2 (id) & 0x01)
                sd->status.skill[id].lv = skill_get_max (id);
        }
    }
    pc_calcstatus (sd, 0);

    return 0;
}

/*==========================================
 * /resetlvl
 *------------------------------------------
 */
int pc_resetlvl (struct map_session_data *sd, int type)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 1; i < MAX_SKILL; i++)
    {
        sd->status.skill[i].lv = 0;
    }

    if (type == 1)
    {
        sd->status.skill_point = 0;
        sd->status.base_level = 1;
        sd->status.job_level = 1;
        sd->status.base_exp = sd->status.base_exp = 0;
        sd->status.job_exp = sd->status.job_exp = 0;
        if (sd->status.option != 0)
            sd->status.option = 0;

        sd->status.str = 1;
        sd->status.agi = 1;
        sd->status.vit = 1;
        sd->status.int_ = 1;
        sd->status.dex = 1;
        sd->status.luk = 1;
        if (sd->status.class == 4001)
            sd->status.status_point = 100;
    }

    if (type == 2)
    {
        sd->status.skill_point = 0;
        sd->status.base_level = 1;
        sd->status.job_level = 1;
        sd->status.base_exp = 0;
        sd->status.job_exp = 0;
    }
    if (type == 3)
    {
        sd->status.base_level = 1;
        sd->status.base_exp = 0;
    }
    if (type == 4)
    {
        sd->status.job_level = 1;
        sd->status.job_exp = 0;
    }

    clif_updatestatus (sd, SP_STATUSPOINT);
    clif_updatestatus (sd, SP_STR);
    clif_updatestatus (sd, SP_AGI);
    clif_updatestatus (sd, SP_VIT);
    clif_updatestatus (sd, SP_INT);
    clif_updatestatus (sd, SP_DEX);
    clif_updatestatus (sd, SP_LUK);
    clif_updatestatus (sd, SP_BASELEVEL);
    clif_updatestatus (sd, SP_JOBLEVEL);
    clif_updatestatus (sd, SP_STATUSPOINT);
    clif_updatestatus (sd, SP_NEXTBASEEXP);
    clif_updatestatus (sd, SP_NEXTJOBEXP);
    clif_updatestatus (sd, SP_SKILLPOINT);

    clif_updatestatus (sd, SP_USTR);    // Updates needed stat points - Valaris
    clif_updatestatus (sd, SP_UAGI);
    clif_updatestatus (sd, SP_UVIT);
    clif_updatestatus (sd, SP_UINT);
    clif_updatestatus (sd, SP_UDEX);
    clif_updatestatus (sd, SP_ULUK);    // End Addition

    for (i = 0; i < 11; i++)
    {                           // unequip items that can't be equipped by base 1 [Valaris]
        if (sd->equip_index[i] >= 0)
            if (!pc_isequip (sd, sd->equip_index[i]))
            {
                pc_unequipitem (sd, sd->equip_index[i], 1);
                sd->equip_index[i] = -1;
            }
    }

    clif_skillinfoblock (sd);
    pc_calcstatus (sd, 0);

    MAP_LOG_STATS (sd, "STATRESET");

    return 0;
}

/*==========================================
 * /resetstate
 *------------------------------------------
 */
int pc_resetstate (struct map_session_data *sd)
{
#define sumsp(a) ((a)*((a-2)/10+2) - 5*((a-2)/10)*((a-2)/10) - 6*((a-2)/10) -2)
//  int add=0; // Removed by Dexity

    nullpo_retr (0, sd);

//  New statpoint table used here - Dexity
    sd->status.status_point = atoi (statp[sd->status.base_level - 1]);
//  End addition 

//  Removed by Dexity - old count
//  add += sumsp(sd->status.str);
//  add += sumsp(sd->status.agi);
//  add += sumsp(sd->status.vit);
//  add += sumsp(sd->status.int_);
//  add += sumsp(sd->status.dex);
//  add += sumsp(sd->status.luk);
//  sd->status.status_point+=add;

    clif_updatestatus (sd, SP_STATUSPOINT);

    sd->status.str = 1;
    sd->status.agi = 1;
    sd->status.vit = 1;
    sd->status.int_ = 1;
    sd->status.dex = 1;
    sd->status.luk = 1;

    clif_updatestatus (sd, SP_STR);
    clif_updatestatus (sd, SP_AGI);
    clif_updatestatus (sd, SP_VIT);
    clif_updatestatus (sd, SP_INT);
    clif_updatestatus (sd, SP_DEX);
    clif_updatestatus (sd, SP_LUK);

    clif_updatestatus (sd, SP_USTR);    // Updates needed stat points - Valaris
    clif_updatestatus (sd, SP_UAGI);
    clif_updatestatus (sd, SP_UVIT);
    clif_updatestatus (sd, SP_UINT);
    clif_updatestatus (sd, SP_UDEX);
    clif_updatestatus (sd, SP_ULUK);    // End Addition

    pc_calcstatus (sd, 0);

    return 0;
}

/*==========================================
 * /resetskill
 *------------------------------------------
 */
int pc_resetskill (struct map_session_data *sd)
{
    int  i, skill;

    nullpo_retr (0, sd);

    sd->status.skill_point += pc_calc_skillpoint (sd);

    for (i = 1; i < MAX_SKILL; i++)
        if ((skill = pc_checkskill (sd, i)) > 0)
        {
            sd->status.skill[i].lv = 0;
            sd->status.skill[i].flags = 0;
        }

    clif_updatestatus (sd, SP_SKILLPOINT);
    clif_skillinfoblock (sd);
    pc_calcstatus (sd, 0);

    return 0;
}

/*==========================================
 * pc�Ƀ_���[�W���^����
 *------------------------------------------
 */
int pc_damage (struct block_list *src, struct map_session_data *sd,
               int damage)
{
    int  i = 0, j = 0;
    struct pc_base_job s_class;
	struct mob_data *md;
	struct block_list *bl;

    nullpo_retr (0, sd);

    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    s_class = pc_calc_base_job (sd->status.class);
    // ���Ɏ����ł����疳��
    if (pc_isdead (sd))
        return 0;
    // ���Ă��痧���オ��
    if (pc_issit (sd))
    {
        pc_setstand (sd);
        skill_gangsterparadise (sd, 0);
    }

    if (src)
    {
        if (src->type == BL_PC)
        {
            MAP_LOG_PC (sd, "INJURED-BY PC%d FOR %d",
                        ((struct map_session_data *) src)->status.char_id,
                        damage);
        }
        else
        {
            MAP_LOG_PC (sd, "INJURED-BY MOB%d FOR %d", src->id, damage);
        }
    }
    else
        MAP_LOG_PC (sd, "INJURED-BY null FOR %d", damage);

    // �� ���Ă����瑫���~�߂�
    if (sd->sc_data[SC_ENDURE].timer == -1
        && !sd->special_state.infinite_endure)
        pc_stop_walking (sd, 3);
    // ���t/�_���X�̒��f
    if (damage > sd->status.max_hp >> 2)
        skill_stop_dancing (&sd->bl, 0);

    sd->status.hp -= damage;

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)
        skill_status_change_end (&sd->bl, SC_TRICKDEAD, -1);
    if (sd->status.option & 2)
        skill_status_change_end (&sd->bl, SC_HIDING, -1);
    if (sd->status.option & 4)
        skill_status_change_end (&sd->bl, SC_CLOAKING, -1);
    if (sd->status.option & 16386)
        skill_status_change_end (&sd->bl, SC_CHASEWALK, -1);

    if (sd->status.hp > 0)
    {
        // �܂������Ă����Ȃ�HP�X�V
        clif_updatestatus (sd, SP_HP);

        if (sd->status.hp < sd->status.max_hp >> 2
            && pc_checkskill (sd, SM_AUTOBERSERK) > 0
            && (sd->sc_data[SC_PROVOKE].timer == -1
                || sd->sc_data[SC_PROVOKE].val2 == 0))
            // �I�[�g�o�[�T�[�N����
            skill_status_change_start (&sd->bl, SC_PROVOKE, 10, 1, 0, 0, 0,
                                       0);

        sd->canlog_tick = gettick ();

        if (sd->status.party_id > 0)
        {                       // on-the-fly party hp updates [Valaris]
            struct party *p = party_search (sd->status.party_id);
            if (p != NULL)
                clif_party_hp (p, sd);
        }                       // end addition [Valaris]

        return 0;
    }

    MAP_LOG_PC (sd, "DEAD%s", "");

    // Character is dead!
	
	//mob brags the kill
	if ((bl = map_id2bl (src->id)) != NULL && (bl && bl->type == BL_MOB))
    {
		nullpo_retr (1, md = (struct mob_data *) bl);
		mobskill_talk(md, MSS_KILL);
	}

    sd->status.hp = 0;
	//UFB Temporary fix to drunkness effect (to fix)
	/*if(sd->sc_data[SC_DRUNK].val4 > -1)
		skill_status_change_end (&sd->bl, SC_DRUNK, -1);*/
	
    // [Fate] Stop quickregen
    sd->quick_regeneration_hp.amount = 0;
    sd->quick_regeneration_sp.amount = 0;
    skill_update_heal_animation (sd);

    pc_setdead (sd);

    pc_stop_walking (sd, 0);
    skill_castcancel (&sd->bl, 0);  // �r���̒��~
    clif_clearchar_area (&sd->bl, 1);
    skill_unit_out_all (&sd->bl, gettick (), 1);
    if (sd->sc_data[SC_BLADESTOP].timer != -1)  //���n�͎��O�ɉ���
        skill_status_change_end (&sd->bl, SC_BLADESTOP, -1);
    pc_setglobalreg (sd, "PC_DIE_COUNTER", ++sd->die_counter);  //���ɃJ�E���^�[��������
    skill_status_change_clear (&sd->bl, 0); // �X�e�[�^�X�ُ��������
    clif_updatestatus (sd, SP_HP);
    pc_calcstatus (sd, 0);
    // [Fate] Reset magic
    sd->cast_tick = gettick ();
    magic_stop_completely (sd);

    for (i = 0; i < 5; i++)
        if (sd->dev.val1[i])
        {
            skill_status_change_end (&map_id2sd (sd->dev.val1[i])->bl,
                                     SC_DEVOTION, -1);
            sd->dev.val1[i] = sd->dev.val2[i] = 0;
        }

    if (battle_config.death_penalty_type > 0 && sd->status.base_level >= 20)
    {                           // changed penalty options, added death by player if pk_mode [Valaris]
        if (!map[sd->bl.m].flag.nopenalty && !map[sd->bl.m].flag.gvg)
        {
            if (battle_config.death_penalty_type == 1
                && battle_config.death_penalty_base > 0)
                sd->status.base_exp -=
                    (double) pc_nextbaseexp (sd) *
                    (double) battle_config.death_penalty_base / 10000;
            if (battle_config.pk_mode && src && src->type == BL_PC)
                sd->status.base_exp -=
                    (double) pc_nextbaseexp (sd) *
                    (double) battle_config.death_penalty_base / 10000;
            else if (battle_config.death_penalty_type == 2
                     && battle_config.death_penalty_base > 0)
            {
                if (pc_nextbaseexp (sd) > 0)
                    sd->status.base_exp -=
                        (double) sd->status.base_exp *
                        (double) battle_config.death_penalty_base / 10000;
                if (battle_config.pk_mode && src && src->type == BL_PC)
                    sd->status.base_exp -=
                        (double) sd->status.base_exp *
                        (double) battle_config.death_penalty_base / 10000;
            }
            if (sd->status.base_exp < 0)
                sd->status.base_exp = 0;
            clif_updatestatus (sd, SP_BASEEXP);

            if (battle_config.death_penalty_type == 1
                && battle_config.death_penalty_job > 0)
                sd->status.job_exp -=
                    (double) pc_nextjobexp (sd) *
                    (double) battle_config.death_penalty_job / 10000;
            if (battle_config.pk_mode && src && src->type == BL_PC)
                sd->status.job_exp -=
                    (double) pc_nextjobexp (sd) *
                    (double) battle_config.death_penalty_job / 10000;
            else if (battle_config.death_penalty_type == 2
                     && battle_config.death_penalty_job > 0)
            {
                if (pc_nextjobexp (sd) > 0)
                    sd->status.job_exp -=
                        (double) sd->status.job_exp *
                        (double) battle_config.death_penalty_job / 10000;
                if (battle_config.pk_mode && src && src->type == BL_PC)
                    sd->status.job_exp -=
                        (double) sd->status.job_exp *
                        (double) battle_config.death_penalty_job / 10000;
            }
            if (sd->status.job_exp < 0)
                sd->status.job_exp = 0;
            clif_updatestatus (sd, SP_JOBEXP);
        }
    }
    //�i�C�g���A���[�h�A�C�e���h���b�v
    if (map[sd->bl.m].flag.pvp_nightmaredrop)
    {                           // Moved this outside so it works when PVP isnt enabled and during pk mode [Ancyker]
        for (j = 0; j < MAX_DROP_PER_MAP; j++)
        {
            int  id = map[sd->bl.m].drop_list[j].drop_id;
            int  type = map[sd->bl.m].drop_list[j].drop_type;
            int  per = map[sd->bl.m].drop_list[j].drop_per;
            if (id == 0)
                continue;
            if (id == -1)
            {                   //�����_���h���b�v
                int  eq_num = 0, eq_n[MAX_INVENTORY];
                memset (eq_n, 0, sizeof (eq_n));
                //�悸����Ă����A�C�e�������J�E���g
                for (i = 0; i < MAX_INVENTORY; i++)
                {
                    int  k;
                    if ((type == 1 && !sd->status.inventory[i].equip)
                        || (type == 2 && sd->status.inventory[i].equip)
                        || type == 3)
                    {
                        //InventoryIndex���i�[
                        for (k = 0; k < MAX_INVENTORY; k++)
                        {
                            if (eq_n[k] <= 0)
                            {
                                eq_n[k] = i;
                                break;
                            }
                        }
                        eq_num++;
                    }
                }
                if (eq_num > 0)
                {
                    int  n = eq_n[MRAND (eq_num)];  //�Y���A�C�e���̒����烉���_��
                    if (MRAND (10000) < per)
                    {
                        if (sd->status.inventory[n].equip)
                            pc_unequipitem (sd, n, 0);
                        pc_dropitem (sd, n, 1);
                    }
                }
            }
            else if (id > 0)
            {
                for (i = 0; i < MAX_INVENTORY; i++)
                {
                    if (sd->status.inventory[i].nameid == id    //ItemID�����v���Ă���
                        && MRAND (10000) < per  //�h���b�v��������OK��
                        && ((type == 1 && !sd->status.inventory[i].equip)   //�^�C�v������OK�Ȃ��h���b�v
                            || (type == 2 && sd->status.inventory[i].equip)
                            || type == 3))
                    {
                        if (sd->status.inventory[i].equip)
                            pc_unequipitem (sd, i, 0);
                        pc_dropitem (sd, i, 1);
                        break;
                    }
                }
            }
        }
    }
    // pvp
    if (map[sd->bl.m].flag.pvp && !battle_config.pk_mode)
    {                           // disable certain pvp functions on pk_mode [Valaris]
        //�����L���O�v�Z
        if (!map[sd->bl.m].flag.pvp_nocalcrank)
        {
            sd->pvp_point -= 5;
            if (src && src->type == BL_PC)
                ((struct map_session_data *) src)->pvp_point++;
            //} //fixed wrong '{' placement by Lupus
            pc_setdead (sd);
        }
        // ��������
        if (sd->pvp_point < 0)
        {
            sd->pvp_point = 0;
            pc_setstand (sd);
            pc_setrestartvalue (sd, 3);
            pc_setpos (sd, sd->status.save_point.map, sd->status.save_point.x,
                       sd->status.save_point.y, 0);
        }
    }
    //GvG
    if (map[sd->bl.m].flag.gvg)
    {
        pc_setstand (sd);
        pc_setrestartvalue (sd, 3);
        pc_setpos (sd, sd->status.save_point.map, sd->status.save_point.x,
                   sd->status.save_point.y, 0);
    }

    if (src && src->type == BL_PC)
    {
        // [Fate] PK death, trigger scripts
        argrec_t arg[3];
        arg[0].name = "@killerrid";
        arg[0].v.i = src->id;
        arg[1].name = "@victimrid";
        arg[1].v.i = sd->bl.id;
        arg[2].name = "@victimlvl";
        arg[2].v.i = sd->status.base_level;
        npc_event_doall_l ("OnPCKilledEvent", sd->bl.id, 3, arg);
        npc_event_doall_l ("OnPCKillEvent", src->id, 3, arg);
    }
    npc_event_doall_l ("OnPCDieEvent", sd->bl.id, 0, NULL);

    return 0;
}

//
// script�� �A
//
/*==========================================
 * script�pPC�X�e�[�^�X�ǂݏo��
 *------------------------------------------
 */
int pc_readparam (struct map_session_data *sd, int type)
{
    int  val = 0;
    struct pc_base_job s_class;

    s_class = pc_calc_base_job (sd->status.class);

    nullpo_retr (0, sd);

    switch (type)
    {
        case SP_SKILLPOINT:
            val = sd->status.skill_point;
            break;
        case SP_STATUSPOINT:
            val = sd->status.status_point;
            break;
        case SP_ZENY:
            val = sd->status.zeny;
            break;
        case SP_BASELEVEL:
            val = sd->status.base_level;
            break;
        case SP_JOBLEVEL:
            val = sd->status.job_level;
            break;
        case SP_CLASS:
            if (val >= 24 && val < 45)
                val += 3978;
            else
                val = sd->status.class;
            break;
        case SP_UPPER:
            val = s_class.upper;
            break;
        case SP_SEX:
            val = sd->sex;
            break;
        case SP_WEIGHT:
            val = sd->weight;
            break;
        case SP_MAXWEIGHT:
            val = sd->max_weight;
            break;
        case SP_BASEEXP:
            val = sd->status.base_exp;
            break;
        case SP_JOBEXP:
            val = sd->status.job_exp;
            break;
        case SP_NEXTBASEEXP:
            val = pc_nextbaseexp (sd);
            break;
        case SP_NEXTJOBEXP:
            val = pc_nextjobexp (sd);
            break;
        case SP_HP:
            val = sd->status.hp;
            break;
        case SP_MAXHP:
            val = sd->status.max_hp;
            break;
        case SP_SP:
            val = sd->status.sp;
            break;
        case SP_MAXSP:
            val = sd->status.max_sp;
            break;
        case SP_STR:
            val = sd->status.str;
            break;
        case SP_AGI:
            val = sd->status.agi;
            break;
        case SP_VIT:
            val = sd->status.vit;
            break;
        case SP_INT:
            val = sd->status.int_;
            break;
        case SP_DEX:
            val = sd->status.dex;
            break;
        case SP_LUK:
            val = sd->status.luk;
            break;
        case SP_FAME:
            val = sd->fame;
            break;
    }

    return val;
}

/*==========================================
 * script�pPC�X�e�[�^�X�ݒ�
 *------------------------------------------
 */
int pc_setparam (struct map_session_data *sd, int type, int val)
{
    int  i = 0, up_level = 50;
    struct pc_base_job s_class;

    nullpo_retr (0, sd);

    s_class = pc_calc_base_job (sd->status.class);

    switch (type)
    {
        case SP_BASELEVEL:
            if (val > sd->status.base_level)
            {
                for (i = 1; i <= (val - sd->status.base_level); i++)
                    sd->status.status_point +=
                        (sd->status.base_level + i + 14) / 4;
            }
            sd->status.base_level = val;
            sd->status.base_exp = 0;
            clif_updatestatus (sd, SP_BASELEVEL);
            clif_updatestatus (sd, SP_NEXTBASEEXP);
            clif_updatestatus (sd, SP_STATUSPOINT);
            clif_updatestatus (sd, SP_BASEEXP);
            pc_calcstatus (sd, 0);
            pc_heal (sd, sd->status.max_hp, sd->status.max_sp);
            break;
        case SP_JOBLEVEL:
            if (sd->status.class == 0)
                up_level -= 40;
            if ((sd->status.class == 23)
                || (sd->status.class >= 4001 && sd->status.class <= 4022))
                up_level += 20;
            if (val >= sd->status.job_level)
            {
                if (val > up_level)
                    val = up_level;
                sd->status.skill_point += (val - sd->status.job_level);
                sd->status.job_level = val;
                sd->status.job_exp = 0;
                clif_updatestatus (sd, SP_JOBLEVEL);
                clif_updatestatus (sd, SP_NEXTJOBEXP);
                clif_updatestatus (sd, SP_JOBEXP);
                clif_updatestatus (sd, SP_SKILLPOINT);
                pc_calcstatus (sd, 0);
                clif_misceffect (&sd->bl, 1);
            }
            else
            {
                sd->status.job_level = val;
                sd->status.job_exp = 0;
                clif_updatestatus (sd, SP_JOBLEVEL);
                clif_updatestatus (sd, SP_NEXTJOBEXP);
                clif_updatestatus (sd, SP_JOBEXP);
                pc_calcstatus (sd, 0);
            }
            clif_updatestatus (sd, type);
            break;
        case SP_SKILLPOINT:
            sd->status.skill_point = val;
            break;
        case SP_STATUSPOINT:
            sd->status.status_point = val;
            break;
        case SP_ZENY:
            sd->status.zeny = val;
            break;
        case SP_BASEEXP:
            if (pc_nextbaseexp (sd) > 0)
            {
                sd->status.base_exp = val;
                if (sd->status.base_exp < 0)
                    sd->status.base_exp = 0;
                pc_checkbaselevelup (sd);
            }
            break;
        case SP_JOBEXP:
            if (pc_nextjobexp (sd) > 0)
            {
                sd->status.job_exp = val;
                if (sd->status.job_exp < 0)
                    sd->status.job_exp = 0;
                pc_checkjoblevelup (sd);
            }
            break;
        case SP_SEX:
            sd->sex = val;
            break;
        case SP_WEIGHT:
            sd->weight = val;
            break;
        case SP_MAXWEIGHT:
            sd->max_weight = val;
            break;
        case SP_HP:
            sd->status.hp = val;
            break;
        case SP_MAXHP:
            sd->status.max_hp = val;
            break;
        case SP_SP:
            sd->status.sp = val;
            break;
        case SP_MAXSP:
            sd->status.max_sp = val;
            break;
        case SP_STR:
            sd->status.str = val;
            break;
        case SP_AGI:
            sd->status.agi = val;
            break;
        case SP_VIT:
            sd->status.vit = val;
            break;
        case SP_INT:
            sd->status.int_ = val;
            break;
        case SP_DEX:
            sd->status.dex = val;
            break;
        case SP_LUK:
            sd->status.luk = val;
            break;
        case SP_FAME:
            sd->fame = val;
            break;
    }
    clif_updatestatus (sd, type);

    return 0;
}

/*==========================================
 * HP/SP����
 *------------------------------------------
 */
int pc_heal (struct map_session_data *sd, int hp, int sp)
{
//  if(battle_config.battle_log)
//      printf("heal %d %d\n",hp,sp);

    nullpo_retr (0, sd);

    if (pc_checkoverhp (sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp (sd))
    {
        if (sp > 0)
            sp = 0;
    }

    if (sd->sc_data && sd->sc_data[SC_BERSERK].timer != -1) //�o�[�T�[�N���͉񕜂����Ȃ��炵��
        return 0;

    if (hp + sd->status.hp > sd->status.max_hp)
        hp = sd->status.max_hp - sd->status.hp;
    if (sp + sd->status.sp > sd->status.max_sp)
        sp = sd->status.max_sp - sd->status.sp;
    sd->status.hp += hp;
    if (sd->status.hp <= 0)
    {
        sd->status.hp = 0;
        pc_damage (NULL, sd, 1);
        hp = 0;
    }
    sd->status.sp += sp;
    if (sd->status.sp <= 0)
        sd->status.sp = 0;
    if (hp)
        clif_updatestatus (sd, SP_HP);
    if (sp)
        clif_updatestatus (sd, SP_SP);

    if (sd->status.party_id > 0)
    {                           // on-the-fly party hp updates [Valaris]
        struct party *p = party_search (sd->status.party_id);
        if (p != NULL)
            clif_party_hp (p, sd);
    }                           // end addition [Valaris]

    return hp + sp;
}

/*==========================================
 * HP/SP����
 *------------------------------------------
 */
static int pc_itemheal_effect (struct map_session_data *sd, int hp, int sp);

static int                      // Compute how quickly we regenerate (less is faster) for that amount
pc_heal_quick_speed (int amount)
{
    if (amount >= 100)
    {
        if (amount >= 500)
            return 0;
        if (amount >= 250)
            return 1;
        return 2;
    }
    else
    {                           // < 100
        if (amount >= 50)
            return 3;
        if (amount >= 20)
            return 4;
        return 5;
    }
}

static void
pc_heal_quick_accumulate (int new_amount,
                          struct quick_regeneration *quick_regen, int max)
{
    int  current_amount = quick_regen->amount;
    int  current_speed = quick_regen->speed;
    int  new_speed = pc_heal_quick_speed (new_amount);

    int  average_speed = ((new_speed * new_amount) + (current_speed * current_amount)) / (current_amount + new_amount); // new_amount > 0, current_amount >= 0

    quick_regen->speed = average_speed;
    quick_regen->amount = MIN (current_amount + new_amount, max);

    quick_regen->tickdelay = MIN (quick_regen->speed, quick_regen->tickdelay);
}

int pc_itemheal (struct map_session_data *sd, int hp, int sp)
{
    /* defer healing */
    if (hp > 0)
    {
        pc_heal_quick_accumulate (hp,
                                  &sd->quick_regeneration_hp,
                                  sd->status.max_hp - sd->status.hp);
        hp = 0;
    }
    if (sp > 0)
    {
        pc_heal_quick_accumulate (sp,
                                  &sd->quick_regeneration_sp,
                                  sd->status.max_sp - sd->status.sp);

        sp = 0;
    }

    /* Hurt right away, if necessary */
    if (hp < 0 || sp < 0)
        pc_itemheal_effect (sd, hp, sp);

    return 0;
}

/* pc_itemheal_effect is invoked once every 0.5s whenever the pc
 * has health recovery queued up (cf. pc_natural_heal_sub).
 */
static int pc_itemheal_effect (struct map_session_data *sd, int hp, int sp)
{
    int  bonus;
//  if(battle_config.battle_log)
//      printf("heal %d %d\n",hp,sp);

    nullpo_retr (0, sd);

    if (sd->sc_data && sd->sc_data[SC_GOSPEL].timer != -1)  //�o�[�T�[�N���͉񕜂����Ȃ��炵��
        return 0;

    if (sd->state.potionpitcher_flag)
    {
        sd->potion_hp = hp;
        sd->potion_sp = sp;
        return 0;
    }

    if (pc_checkoverhp (sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp (sd))
    {
        if (sp > 0)
            sp = 0;
    }
    if (hp > 0)
    {
        bonus =
            (sd->paramc[2] << 1) + 100 + pc_checkskill (sd, SM_RECOVERY) * 10;
        if (bonus != 100)
            hp = hp * bonus / 100;
        bonus = 100 + pc_checkskill (sd, AM_LEARNINGPOTION) * 5;
        if (bonus != 100)
            hp = hp * bonus / 100;
    }
    if (sp > 0)
    {
        bonus =
            (sd->paramc[3] << 1) + 100 + pc_checkskill (sd,
                                                        MG_SRECOVERY) * 10;
        if (bonus != 100)
            sp = sp * bonus / 100;
        bonus = 100 + pc_checkskill (sd, AM_LEARNINGPOTION) * 5;
        if (bonus != 100)
            sp = sp * bonus / 100;
    }
    if (hp + sd->status.hp > sd->status.max_hp)
        hp = sd->status.max_hp - sd->status.hp;
    if (sp + sd->status.sp > sd->status.max_sp)
        sp = sd->status.max_sp - sd->status.sp;
    sd->status.hp += hp;
    if (sd->status.hp <= 0)
    {
        sd->status.hp = 0;
        pc_damage (NULL, sd, 1);
        hp = 0;
    }
    sd->status.sp += sp;
    if (sd->status.sp <= 0)
        sd->status.sp = 0;
    if (hp)
        clif_updatestatus (sd, SP_HP);
    if (sp)
        clif_updatestatus (sd, SP_SP);

    return 0;
}

/*==========================================
 * HP/SP����
 *------------------------------------------
 */
int pc_percentheal (struct map_session_data *sd, int hp, int sp)
{
    nullpo_retr (0, sd);

    if (sd->state.potionpitcher_flag)
    {
        sd->potion_per_hp = hp;
        sd->potion_per_sp = sp;
        return 0;
    }

    if (pc_checkoverhp (sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp (sd))
    {
        if (sp > 0)
            sp = 0;
    }
    if (hp)
    {
        if (hp >= 100)
        {
            sd->status.hp = sd->status.max_hp;
        }
        else if (hp <= -100)
        {
            sd->status.hp = 0;
            pc_damage (NULL, sd, 1);
        }
        else
        {
            sd->status.hp += sd->status.max_hp * hp / 100;
            if (sd->status.hp > sd->status.max_hp)
                sd->status.hp = sd->status.max_hp;
            if (sd->status.hp <= 0)
            {
                sd->status.hp = 0;
                pc_damage (NULL, sd, 1);
                hp = 0;
            }
        }
    }
    if (sp)
    {
        if (sp >= 100)
        {
            sd->status.sp = sd->status.max_sp;
        }
        else if (sp <= -100)
        {
            sd->status.sp = 0;
        }
        else
        {
            sd->status.sp += sd->status.max_sp * sp / 100;
            if (sd->status.sp > sd->status.max_sp)
                sd->status.sp = sd->status.max_sp;
            if (sd->status.sp < 0)
                sd->status.sp = 0;
        }
    }
    if (hp)
        clif_updatestatus (sd, SP_HP);
    if (sp)
        clif_updatestatus (sd, SP_SP);

    return 0;
}

/*==========================================
 * �E�ύX
 * ����	job �E�� 0�`23
 *		upper �ʏ� 0, �]�� 1, �{�q 2, ���̂܂� -1
 *------------------------------------------
 */
int pc_jobchange (struct map_session_data *sd, int job, int upper)
{
    int  i;
    int  b_class = 0;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����
    struct pc_base_job s_class = pc_calc_base_job (sd->status.class);

    nullpo_retr (0, sd);

    if ((job > 23) && (job < 68))
        job += 3977;

    if ((job > 69) && (job < 4000))
        return 1;

    if (upper < 0)              //���ݓ]�����ǂ����𔻒f����
        upper = s_class.upper;

    if (upper == 0)
    {                           //�ʏ��E�Ȃ�job���̂܂���
        b_class = job;
    }
    else if (upper == 1)
    {
        if (job == 23)
        {                       //�]���ɃX�p�m�r�͑��݂��Ȃ��̂ł��f��
            return 1;
        }
        else
        {
            b_class = job + 4001;
        }
    }
    else if (upper == 2)
    {                           //�{�q�Ɍ����͂Ȃ����ǂǂ������ŏR�����邩�炢����
        b_class = (job == 23) ? job + 4022 : job + 4023;
    }
    else
    {
        return 1;
    }

    if ((sd->status.sex == 0 && job == 19) || (sd->status.sex == 1 && job == 20) || (sd->status.sex == 0 && job == 4020) || (sd->status.sex == 1 && job == 4021) || job == 22 || sd->status.class == b_class)   //���̓o�[�h�ɂȂ��Ȃ��A���̓_���T�[�ɂȂ��Ȃ��A�����ߏւ��f��
        return 1;

    sd->status.class = sd->view_class = b_class;

    sd->status.job_level = 1;
    sd->status.job_exp = 0;
    clif_updatestatus (sd, SP_JOBLEVEL);
    clif_updatestatus (sd, SP_JOBEXP);
    clif_updatestatus (sd, SP_NEXTJOBEXP);

    for (i = 0; i < 11; i++)
    {
        if (sd->equip_index[i] >= 0)
            if (!pc_isequip (sd, sd->equip_index[i]))
                pc_unequipitem (sd, sd->equip_index[i], 1); // �����O��
    }

    clif_changelook (&sd->bl, LOOK_BASE, sd->view_class);   // move sprite update to prevent client crashes with incompatible equipment [Valaris]
    if (sd->status.clothes_color > 0)
        clif_changelook (&sd->bl, LOOK_CLOTHES_COLOR,
                         sd->status.clothes_color);
    if (battle_config.muting_players && sd->status.manner < 0)
        clif_changestatus (&sd->bl, SP_MANNER, sd->status.manner);

    pc_calcstatus (sd, 0);
    pc_checkallowskill (sd);
    pc_equiplookall (sd);
    clif_equiplist (sd);

    if (pc_isriding (sd))
    {                           // remove peco status if changing into invalid class [Valaris]
        if (!(pc_checkskill (sd, KN_RIDING)))
            pc_setoption (sd, sd->status.option | -0x0000);
        if (pc_checkskill (sd, KN_RIDING) > 0)
            pc_setriding (sd);
    }

    return 0;
}

/*==========================================
 * �����ڕύX
 *------------------------------------------
 */
int pc_equiplookall (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    clif_changelook (&sd->bl, LOOK_WEAPON, 0);
//  clif_changelook(&sd->bl,LOOK_SHOES,0);
    clif_changelook (&sd->bl, LOOK_HEAD_BOTTOM, sd->status.head_bottom);
    clif_changelook (&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
    clif_changelook (&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);

    clif_changelook_accessories (&sd->bl, NULL);

    return 0;
}

/*==========================================
 * �����ڕύX
 *------------------------------------------
 */
int pc_changelook (struct map_session_data *sd, int type, int val)
{
    nullpo_retr (0, sd);

    switch (type)
    {
        case LOOK_HAIR:
            sd->status.hair = val;
            break;
        case LOOK_WEAPON:
            sd->status.weapon = val;
            break;
        case LOOK_HEAD_BOTTOM:
            sd->status.head_bottom = val;
            break;
        case LOOK_HEAD_TOP:
            sd->status.head_top = val;
            break;
        case LOOK_HEAD_MID:
            sd->status.head_mid = val;
            break;
        case LOOK_HAIR_COLOR:
            sd->status.hair_color = val;
            break;
        case LOOK_CLOTHES_COLOR:
            sd->status.clothes_color = val;
            break;
        case LOOK_SHIELD:
            sd->status.shield = val;
            break;
        case LOOK_SHOES:
            break;
    }
    clif_changelook (&sd->bl, type, val);

    return 0;
}

/*==========================================
 * �t���i(��,�y�R,�J�[�g)�ݒ�
 *------------------------------------------
 */
int pc_setoption (struct map_session_data *sd, int type)
{
    nullpo_retr (0, sd);

    sd->status.option = type;
    clif_changeoption (&sd->bl);
    pc_calcstatus (sd, 0);

    return 0;
}

/*==========================================
 * �J�[�g�ݒ�
 *------------------------------------------
 */
int pc_setcart (struct map_session_data *sd, int type)
{
    int  cart[6] = { 0x0000, 0x0008, 0x0080, 0x0100, 0x0200, 0x0400 };

    nullpo_retr (0, sd);

    if (pc_checkskill (sd, MC_PUSHCART) > 0)
    {                           // �v�b�V���J�[�g�X�L������
        if (!pc_iscarton (sd))
        {                       // �J�[�g���t���Ă��Ȃ�
            pc_setoption (sd, cart[type]);
            clif_cart_itemlist (sd);
            clif_cart_equiplist (sd);
            clif_updatestatus (sd, SP_CARTINFO);
            clif_status_change (&sd->bl, 0x0c, 0);
        }
        else
        {
            pc_setoption (sd, cart[type]);
        }
    }

    return 0;
}

/*==========================================
 * ���ݒ�
 *------------------------------------------
 */
int pc_setfalcon (struct map_session_data *sd)
{
    if (pc_checkskill (sd, HT_FALCON) > 0)
    {                           // �t�@���R���}�X�^���[�X�L������
        pc_setoption (sd, sd->status.option | 0x0010);
    }

    return 0;
}

/*==========================================
 * �y�R�y�R�ݒ�
 *------------------------------------------
 */
int pc_setriding (struct map_session_data *sd)
{
    if (sd->disguise > 0)
    {                           // temporary prevention of crash caused by peco + disguise, will look into a better solution [Valaris]
        clif_displaymessage (sd->fd,
                             "Cannot mount a Peco while in disguise.");
        return 0;
    }

    if ((pc_checkskill (sd, KN_RIDING) > 0))
    {                           // ���C�f�B���O�X�L������
        pc_setoption (sd, sd->status.option | 0x0020);

        if (sd->status.class == 7)
            sd->status.class = sd->view_class = 13;

        if (sd->status.class == 14)
            sd->status.class = sd->view_class = 21;

        if (sd->status.class == 4008)
            sd->status.class = sd->view_class = 4014;

        if (sd->status.class == 4015)
            sd->status.class = sd->view_class = 4022;
    }

    return 0;
}

/*==========================================
 * script�p�ϐ��̒l���ǂ�
 *------------------------------------------
 */
int pc_readreg (struct map_session_data *sd, int reg)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->reg_num; i++)
        if (sd->reg[i].index == reg)
            return sd->reg[i].data;

    return 0;
}

/*==========================================
 * script�p�ϐ��̒l���ݒ�
 *------------------------------------------
 */
int pc_setreg (struct map_session_data *sd, int reg, int val)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->reg_num; i++)
    {
        if (sd->reg[i].index == reg)
        {
            sd->reg[i].data = val;
            return 0;
        }
    }
    sd->reg_num++;
    sd->reg = realloc (sd->reg, sizeof (*(sd->reg)) * sd->reg_num);
    if (sd->reg == NULL)
    {
        printf ("out of memory : pc_setreg\n");
        exit (1);
    }
/*	memset(sd->reg + (sd->reg_num - 1) * sizeof(*(sd->reg)), 0,
		sizeof(*(sd->reg)));
*/
    sd->reg[i].index = reg;
    sd->reg[i].data = val;

    return 0;
}

/*==========================================
 * script�p�������ϐ��̒l���ǂ�
 *------------------------------------------
 */
char *pc_readregstr (struct map_session_data *sd, int reg)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->regstr_num; i++)
        if (sd->regstr[i].index == reg)
            return sd->regstr[i].data;

    return NULL;
}

/*==========================================
 * script�p�������ϐ��̒l���ݒ�
 *------------------------------------------
 */
int pc_setregstr (struct map_session_data *sd, int reg, char *str)
{
    int  i;

    nullpo_retr (0, sd);

    if (strlen (str) + 1 >= sizeof (sd->regstr[0].data))
    {
        printf ("pc_setregstr: string too long !\n");
        return 0;
    }

    for (i = 0; i < sd->regstr_num; i++)
        if (sd->regstr[i].index == reg)
        {
            strcpy (sd->regstr[i].data, str);
            return 0;
        }
    sd->regstr_num++;
    sd->regstr =
        realloc (sd->regstr, sizeof (sd->regstr[0]) * sd->regstr_num);
    if (sd->regstr == NULL)
    {
        printf ("out of memory : pc_setreg\n");
        exit (1);
    }
/*	memset(sd->reg + (sd->reg_num - 1) * sizeof(*(sd->reg)), 0,
		sizeof(*(sd->reg)));
*/
    sd->regstr[i].index = reg;
    strcpy (sd->regstr[i].data, str);

    return 0;
}

/*==========================================
 * script�p�O���[�o���ϐ��̒l���ǂ�
 *------------------------------------------
 */
int pc_readglobalreg (struct map_session_data *sd, char *reg)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->status.global_reg_num; i++)
    {
        if (strcmp (sd->status.global_reg[i].str, reg) == 0)
            return sd->status.global_reg[i].value;
    }

    return 0;
}

/*==========================================
 * script�p�O���[�o���ϐ��̒l���ݒ�
 *------------------------------------------
 */
int pc_setglobalreg (struct map_session_data *sd, char *reg, int val)
{
    int  i;

    nullpo_retr (0, sd);

    //PC_DIE_COUNTER���X�N���v�g�ȂǂŕύX���ꂽ���̏���
    if (strcmp (reg, "PC_DIE_COUNTER") == 0 && sd->die_counter != val)
    {
        sd->die_counter = val;
        pc_calcstatus (sd, 0);
    }
    if (val == 0)
    {
        for (i = 0; i < sd->status.global_reg_num; i++)
        {
            if (strcmp (sd->status.global_reg[i].str, reg) == 0)
            {
                sd->status.global_reg[i] =
                    sd->status.global_reg[sd->status.global_reg_num - 1];
                sd->status.global_reg_num--;
                break;
            }
        }
        return 0;
    }
    for (i = 0; i < sd->status.global_reg_num; i++)
    {
        if (strcmp (sd->status.global_reg[i].str, reg) == 0)
        {
            sd->status.global_reg[i].value = val;
            return 0;
        }
    }
    if (sd->status.global_reg_num < GLOBAL_REG_NUM)
    {
        strcpy (sd->status.global_reg[i].str, reg);
        sd->status.global_reg[i].value = val;
        sd->status.global_reg_num++;
        return 0;
    }
    if (battle_config.error_log)
        printf ("pc_setglobalreg : couldn't set %s (GLOBAL_REG_NUM = %d)\n",
                reg, GLOBAL_REG_NUM);

    return 1;
}

/*==========================================
 * script�p�A�J�E���g�ϐ��̒l���ǂ�
 *------------------------------------------
 */
int pc_readaccountreg (struct map_session_data *sd, char *reg)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->status.account_reg_num; i++)
    {
        if (strcmp (sd->status.account_reg[i].str, reg) == 0)
            return sd->status.account_reg[i].value;
    }

    return 0;
}

/*==========================================
 * script�p�A�J�E���g�ϐ��̒l���ݒ�
 *------------------------------------------
 */
int pc_setaccountreg (struct map_session_data *sd, char *reg, int val)
{
    int  i;

    nullpo_retr (0, sd);

    if (val == 0)
    {
        for (i = 0; i < sd->status.account_reg_num; i++)
        {
            if (strcmp (sd->status.account_reg[i].str, reg) == 0)
            {
                sd->status.account_reg[i] =
                    sd->status.account_reg[sd->status.account_reg_num - 1];
                sd->status.account_reg_num--;
                break;
            }
        }
        intif_saveaccountreg (sd);
        return 0;
    }
    for (i = 0; i < sd->status.account_reg_num; i++)
    {
        if (strcmp (sd->status.account_reg[i].str, reg) == 0)
        {
            sd->status.account_reg[i].value = val;
            intif_saveaccountreg (sd);
            return 0;
        }
    }
    if (sd->status.account_reg_num < ACCOUNT_REG_NUM)
    {
        strcpy (sd->status.account_reg[i].str, reg);
        sd->status.account_reg[i].value = val;
        sd->status.account_reg_num++;
        intif_saveaccountreg (sd);
        return 0;
    }
    if (battle_config.error_log)
        printf ("pc_setaccountreg : couldn't set %s (ACCOUNT_REG_NUM = %d)\n",
                reg, ACCOUNT_REG_NUM);

    return 1;
}

/*==========================================
 * script�p�A�J�E���g�ϐ�2�̒l���ǂ�
 *------------------------------------------
 */
int pc_readaccountreg2 (struct map_session_data *sd, char *reg)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < sd->status.account_reg2_num; i++)
    {
        if (strcmp (sd->status.account_reg2[i].str, reg) == 0)
            return sd->status.account_reg2[i].value;
    }

    return 0;
}

/*==========================================
 * script�p�A�J�E���g�ϐ�2�̒l���ݒ�
 *------------------------------------------
 */
int pc_setaccountreg2 (struct map_session_data *sd, char *reg, int val)
{
    int  i;

    nullpo_retr (1, sd);

    if (val == 0)
    {
        for (i = 0; i < sd->status.account_reg2_num; i++)
        {
            if (strcmp (sd->status.account_reg2[i].str, reg) == 0)
            {
                sd->status.account_reg2[i] =
                    sd->status.account_reg2[sd->status.account_reg2_num - 1];
                sd->status.account_reg2_num--;
                break;
            }
        }
        chrif_saveaccountreg2 (sd);
        return 0;
    }
    for (i = 0; i < sd->status.account_reg2_num; i++)
    {
        if (strcmp (sd->status.account_reg2[i].str, reg) == 0)
        {
            sd->status.account_reg2[i].value = val;
            chrif_saveaccountreg2 (sd);
            return 0;
        }
    }
    if (sd->status.account_reg2_num < ACCOUNT_REG2_NUM)
    {
        strcpy (sd->status.account_reg2[i].str, reg);
        sd->status.account_reg2[i].value = val;
        sd->status.account_reg2_num++;
        chrif_saveaccountreg2 (sd);
        return 0;
    }
    if (battle_config.error_log)
        printf
            ("pc_setaccountreg2 : couldn't set %s (ACCOUNT_REG2_NUM = %d)\n",
             reg, ACCOUNT_REG2_NUM);

    return 1;
}

/*==========================================
 * ���B������
 *------------------------------------------
 */
int pc_percentrefinery (struct map_session_data *sd, struct item *item)
{
    int  percent;

    nullpo_retr (0, item);
    percent = percentrefinery[itemdb_wlv (item->nameid)][(int) item->refine];

    percent += pc_checkskill (sd, BS_WEAPONRESEARCH);   // ���팤���X�L������

    // �m���̗L���͈̓`�F�b�N
    if (percent > 100)
    {
        percent = 100;
    }
    if (percent < 0)
    {
        percent = 0;
    }

    return percent;
}

/*==========================================
 * �C�x���g�^�C�}�[����
 *------------------------------------------
 */
int pc_eventtimer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd = map_id2sd (id);
    int  i;
    if (sd == NULL)
        return 0;

    for (i = 0; i < MAX_EVENTTIMER; i++)
    {
        if (sd->eventtimer[i] == tid)
        {
            sd->eventtimer[i] = -1;
            npc_event (sd, (const char *) data, 0);
            break;
        }
    }
    free ((void *) data);
    if (i == MAX_EVENTTIMER)
    {
        if (battle_config.error_log)
            printf ("pc_eventtimer: no such event timer\n");
    }

    return 0;
}

/*==========================================
 * �C�x���g�^�C�}�[�ǉ�
 *------------------------------------------
 */
int pc_addeventtimer (struct map_session_data *sd, int tick, const char *name)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] == -1)
            break;

    if (i < MAX_EVENTTIMER)
    {
        char *evname = (char *) aCalloc (24, sizeof (char));
        strncpy (evname, name, 24);
        evname[23] = '\0';
        sd->eventtimer[i] = add_timer (gettick () + tick,
                                       pc_eventtimer, sd->bl.id,
                                       (int) evname);
        return 1;
    }

    return 0;
}

/*==========================================
 * �C�x���g�^�C�}�[�폜
 *------------------------------------------
 */
int pc_deleventtimer (struct map_session_data *sd, const char *name)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1 && strcmp ((char
                                                *) (get_timer (sd->eventtimer
                                                               [i])->data),
                                               name) == 0)
        {
            delete_timer (sd->eventtimer[i], pc_eventtimer);
            sd->eventtimer[i] = -1;
            break;
        }

    return 0;
}

/*==========================================
 * �C�x���g�^�C�}�[�J�E���g�l�ǉ�
 *------------------------------------------
 */
int pc_addeventtimercount (struct map_session_data *sd, const char *name,
                           int tick)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1 && strcmp ((char
                                                *) (get_timer (sd->eventtimer
                                                               [i])->data),
                                               name) == 0)
        {
            addtick_timer (sd->eventtimer[i], tick);
            break;
        }

    return 0;
}

/*==========================================
 * �C�x���g�^�C�}�[�S�폜
 *------------------------------------------
 */
int pc_cleareventtimer (struct map_session_data *sd)
{
    int  i;

    nullpo_retr (0, sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1)
        {
            delete_timer (sd->eventtimer[i], pc_eventtimer);
            sd->eventtimer[i] = -1;
        }

    return 0;
}

//
// �� ����
//
/*==========================================
 * �A�C�e���𑕔��
 *------------------------------------------
 */
static int
pc_signal_advanced_equipment_change (struct map_session_data *sd, int n)
{
    if (sd->status.inventory[n].equip & 0x0040)
        clif_changelook (&sd->bl, LOOK_SHOES, 0);
    if (sd->status.inventory[n].equip & 0x0004)
        clif_changelook (&sd->bl, LOOK_GLOVES, 0);
    if (sd->status.inventory[n].equip & 0x0008)
        clif_changelook (&sd->bl, LOOK_CAPE, 0);
    if (sd->status.inventory[n].equip & 0x0010)
        clif_changelook (&sd->bl, LOOK_MISC1, 0);
    if (sd->status.inventory[n].equip & 0x0080)
        clif_changelook (&sd->bl, LOOK_MISC2, 0);
    return 0;
}

int pc_equipitem (struct map_session_data *sd, int n, int pos)
{
    int  i, nameid, arrow, view;
    struct item_data *id;
    //�]�����{�q�̏ꍇ�̌��̐E�Ƃ��Z�o����

    nullpo_retr (0, sd);

    if (n < 0 || n >= MAX_INVENTORY)
    {
        clif_equipitemack (sd, 0, 0, 0);
        return 0;
    }

    nameid = sd->status.inventory[n].nameid;
    id = sd->inventory_data[n];
    pos = pc_equippoint (sd, n);

    if (battle_config.battle_log)
        printf ("equip %d(%d) %x:%x\n", nameid, n, id->equip, pos);
    if (!pc_isequip (sd, n) || !pos || sd->status.inventory[n].broken == 1)
    {                           // [Valaris]
        clif_equipitemack (sd, n, 0, 0);    // fail
        return 0;
    }

// -- moonsoul (if player is berserk then cannot equip)
//
    if (sd->sc_data[SC_BERSERK].timer != -1)
    {
        clif_equipitemack (sd, n, 0, 0);    // fail
        return 0;
    }

    if (pos == 0x88)
    {                           // �A�N�Z�T���p���O����
        int  epor = 0;
        if (sd->equip_index[0] >= 0)
            epor |= sd->status.inventory[sd->equip_index[0]].equip;
        if (sd->equip_index[1] >= 0)
            epor |= sd->status.inventory[sd->equip_index[1]].equip;
        epor &= 0x88;
        pos = epor == 0x08 ? 0x80 : 0x08;
    }

    // �񓁗�����
    if ((pos == 0x22)           // �ꉞ�A�����v���ӏ����񓁗����킩�`�F�b�N����
        && (id->equip == 2)     // �P �蕐��
        && (pc_checkskill (sd, AS_LEFT) > 0 || sd->status.class == 12)) // �����C�B�L
    {
        int  tpos = 0;
        if (sd->equip_index[8] >= 0)
            tpos |= sd->status.inventory[sd->equip_index[8]].equip;
        if (sd->equip_index[9] >= 0)
            tpos |= sd->status.inventory[sd->equip_index[9]].equip;
        tpos &= 0x02;
        pos = tpos == 0x02 ? 0x20 : 0x02;
    }

    arrow = pc_search_inventory (sd, pc_checkequip (sd, 9));    // Added by RoVeRT
    for (i = 0; i < 11; i++)
    {
        if (pos & equip_pos[i])
        {
            if (sd->equip_index[i] >= 0)    //Slot taken, remove item from there.
                pc_unequipitem (sd, sd->equip_index[i], 1);
            sd->equip_index[i] = n;
        }
    }
    // �|���
    if (pos == 0x8000)
    {
        clif_arrowequip (sd, n);
        clif_arrow_fail (sd, 3);    // 3=������ł��܂���
    }
    else
    {
        /* Don't update re-equipping if we're using a spell */
        if (!(pos == 4 && sd->attack_spell_override))
            clif_equipitemack (sd, n, pos, 1);
    }

    for (i = 0; i < 11; i++)
    {
        if (pos & equip_pos[i])
            sd->equip_index[i] = n;
    }
    sd->status.inventory[n].equip = pos;

    if (sd->inventory_data[n])
    {
        view = sd->inventory_data[n]->look;
        if (view == 0)
            view = sd->inventory_data[n]->nameid;
    }
    else
    {
        view = 0;
    }

    if (sd->status.inventory[n].equip & 0x0002)
    {
        sd->weapontype1 = view;
        pc_calcweapontype (sd);
        pc_set_weapon_look (sd);
    }
    if (sd->status.inventory[n].equip & 0x0020)
    {
        if (sd->inventory_data[n])
        {
            if (sd->inventory_data[n]->type == 4)
            {
                sd->status.shield = 0;
                if (sd->status.inventory[n].equip == 0x0020)
                    sd->weapontype2 = view;
            }
            else if (sd->inventory_data[n]->type == 5)
            {
                sd->status.shield = view;
                sd->weapontype2 = 0;
            }
        }
        else
            sd->status.shield = sd->weapontype2 = 0;
        pc_calcweapontype (sd);
        clif_changelook (&sd->bl, LOOK_SHIELD, sd->status.shield);
    }
    if (sd->status.inventory[n].equip & 0x0001)
    {
        sd->status.head_bottom = view;
        clif_changelook (&sd->bl, LOOK_HEAD_BOTTOM, sd->status.head_bottom);
    }
    if (sd->status.inventory[n].equip & 0x0100)
    {
        sd->status.head_top = view;
        clif_changelook (&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
    }
    if (sd->status.inventory[n].equip & 0x0200)
    {
        sd->status.head_mid = view;
        clif_changelook (&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);
    }
    pc_signal_advanced_equipment_change (sd, n);

    pc_checkallowskill (sd);    // �����i�ŃX�L����������邩�`�F�b�N
    if (itemdb_look (sd->status.inventory[n].nameid) == 11 && arrow)
    {                           // Added by RoVeRT
        clif_arrowequip (sd, arrow);
        sd->status.inventory[arrow].equip = 32768;
    }
    pc_calcstatus (sd, 0);

    if (sd->special_state.infinite_endure)
    {
        if (sd->sc_data[SC_ENDURE].timer == -1)
            skill_status_change_start (&sd->bl, SC_ENDURE, 10, 1, 0, 0, 0, 0);
    }
    else
    {
        if (sd->sc_data[SC_ENDURE].timer != -1 && sd->sc_data[SC_ENDURE].val2)
            skill_status_change_end (&sd->bl, SC_ENDURE, -1);
    }

    if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1
        && !battle_check_undead (7, sd->def_ele))
        skill_status_change_end (&sd->bl, SC_SIGNUMCRUCIS, -1);
    if (sd->sc_data[SC_DANCING].timer != -1
        && (sd->status.weapon != 13 && sd->status.weapon != 14))
        skill_stop_dancing (&sd->bl, 0);

    return 0;
}

/*==========================================
 * �� ��������O��
 *------------------------------------------
 */
int pc_unequipitem (struct map_session_data *sd, int n, int type)
{
	int  i = 0;
    nullpo_retr (0, sd);

// -- moonsoul  (if player is berserk then cannot unequip)
//
    if (sd->sc_data[SC_BERSERK].timer != -1)
    {
        clif_unequipitemack (sd, n, 0, 0);
        return 0;
    }

    if (battle_config.battle_log)
        printf ("unequip %d %x:%x\n", n, pc_equippoint (sd, n),
                sd->status.inventory[n].equip);
    if (sd->status.inventory[n].equip)
    {
        int  i;
        for (i = 0; i < 11; i++)
        {
            if (sd->status.inventory[n].equip & equip_pos[i])
                sd->equip_index[i] = -1;
        }
        if (sd->status.inventory[n].equip & 0x0002)
        {
            sd->weapontype1 = 0;
            sd->status.weapon = sd->weapontype2;
            pc_calcweapontype (sd);
            pc_set_weapon_look (sd);
        }
        if (sd->status.inventory[n].equip & 0x0020)
        {
            sd->status.shield = sd->weapontype2 = 0;
            pc_calcweapontype (sd);
            clif_changelook (&sd->bl, LOOK_SHIELD, sd->status.shield);
        }
        if (sd->status.inventory[n].equip & 0x0001)
        {
            sd->status.head_bottom = 0;
            clif_changelook (&sd->bl, LOOK_HEAD_BOTTOM,
                             sd->status.head_bottom);
        }
        if (sd->status.inventory[n].equip & 0x0100)
        {
            sd->status.head_top = 0;
            clif_changelook (&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
        }
        if (sd->status.inventory[n].equip & 0x0200)
        {
            sd->status.head_mid = 0;
            clif_changelook (&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);
        }
        pc_signal_advanced_equipment_change (sd, n);

        if (sd->sc_data[SC_BROKNWEAPON].timer != -1
            && sd->status.inventory[n].equip & 0x0002
            && sd->status.inventory[i].broken == 1)
            skill_status_change_end (&sd->bl, SC_BROKNWEAPON, -1);

        clif_unequipitemack (sd, n, sd->status.inventory[n].equip, 1);
        sd->status.inventory[n].equip = 0;
        if (!type)
            pc_checkallowskill (sd);
        if (sd->weapontype1 == 0 && sd->weapontype2 == 0)
            skill_encchant_eremental_end (&sd->bl, -1); //���펝�������͖���ő����t�^����
    }
    else
    {
        clif_unequipitemack (sd, n, 0, 0);
    }

	if(sd->inventory_data[n]->unequip_script){
        argrec_t arg[2];
        arg[0].name = "@slotId";
        arg[0].v.i = i;
        arg[1].name = "@itemId";
        arg[1].v.i = sd->inventory_data[n]->nameid;
        run_script_l (sd->inventory_data[n]->unequip_script, 0, sd->bl.id,
                    0, 2, arg);
	}

    if (!type)
    {
        pc_calcstatus (sd, 0);
        if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1
            && !battle_check_undead (7, sd->def_ele))
            skill_status_change_end (&sd->bl, SC_SIGNUMCRUCIS, -1);
    }

    return 0;
}

int pc_unequipinvyitem (struct map_session_data *sd, int n, int type)
{
    int  i;

    nullpo_retr (1, sd);

    for (i = 0; i < 11; i++)
    {
        if (equip_pos[i] > 0 && sd->equip_index[i] == n)
        {                       //Slot taken, remove item from there.
            pc_unequipitem (sd, sd->equip_index[i], type);
            sd->equip_index[i] = -1;
        }
    }

    return 0;
}

/*==========================================
 * �A�C�e����index�ԍ����l�߂���
 * �� ���i�̑����\�`�F�b�N���s�Ȃ�
 *------------------------------------------
 */
int pc_checkitem (struct map_session_data *sd)
{
    int  i, j, k, id, calc_flag = 0;
    struct item_data *it = NULL;

    nullpo_retr (0, sd);

    // �����i�󂫋l��
    for (i = j = 0; i < MAX_INVENTORY; i++)
    {
        if ((id = sd->status.inventory[i].nameid) == 0)
            continue;
        if (battle_config.item_check && !itemdb_available (id))
        {
            if (battle_config.error_log)
                printf ("illeagal item id %d in %d[%s] inventory.\n", id,
                        sd->bl.id, sd->status.name);
            pc_delitem (sd, i, sd->status.inventory[i].amount, 3);
            continue;
        }
        if (i > j)
        {
            memcpy (&sd->status.inventory[j], &sd->status.inventory[i],
                    sizeof (struct item));
            sd->inventory_data[j] = sd->inventory_data[i];
        }
        j++;
    }
    if (j < MAX_INVENTORY)
        memset (&sd->status.inventory[j], 0,
                sizeof (struct item) * (MAX_INVENTORY - j));
    for (k = j; k < MAX_INVENTORY; k++)
        sd->inventory_data[k] = NULL;

    // �J�[�g���󂫋l��
    for (i = j = 0; i < MAX_CART; i++)
    {
        if ((id = sd->status.cart[i].nameid) == 0)
            continue;
        if (battle_config.item_check && !itemdb_available (id))
        {
            if (battle_config.error_log)
                printf ("illeagal item id %d in %d[%s] cart.\n", id,
                        sd->bl.id, sd->status.name);
            pc_cart_delitem (sd, i, sd->status.cart[i].amount, 1);
            continue;
        }
        if (i > j)
        {
            memcpy (&sd->status.cart[j], &sd->status.cart[i],
                    sizeof (struct item));
        }
        j++;
    }
    if (j < MAX_CART)
        memset (&sd->status.cart[j], 0,
                sizeof (struct item) * (MAX_CART - j));

    // �� ���ʒu�`�F�b�N

    for (i = 0; i < MAX_INVENTORY; i++)
    {

        it = sd->inventory_data[i];

        if (sd->status.inventory[i].nameid == 0)
            continue;
        if (sd->status.inventory[i].equip & ~pc_equippoint (sd, i))
        {
            sd->status.inventory[i].equip = 0;
            calc_flag = 1;
        }
        //�����`�F�b�N
        if (sd->status.inventory[i].equip && map[sd->bl.m].flag.pvp
            && (it->flag.no_equip == 1 || it->flag.no_equip == 3))
        {                       //PvP����
            sd->status.inventory[i].equip = 0;
            calc_flag = 1;
        }
        else if (sd->status.inventory[i].equip && map[sd->bl.m].flag.gvg
                 && (it->flag.no_equip == 2 || it->flag.no_equip == 3))
        {                       //GvG����
            sd->status.inventory[i].equip = 0;
            calc_flag = 1;
        }
    }

    pc_setequipindex (sd);
    if (calc_flag)
        pc_calcstatus (sd, 2);

    return 0;
}

int pc_checkoverhp (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->status.hp == sd->status.max_hp)
        return 1;
    if (sd->status.hp > sd->status.max_hp)
    {
        sd->status.hp = sd->status.max_hp;
        clif_updatestatus (sd, SP_HP);
        return 2;
    }

    return 0;
}

int pc_checkoversp (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    if (sd->status.sp == sd->status.max_sp)
        return 1;
    if (sd->status.sp > sd->status.max_sp)
    {
        sd->status.sp = sd->status.max_sp;
        clif_updatestatus (sd, SP_SP);
        return 2;
    }

    return 0;
}

/*==========================================
 * PVP���ʌv�Z�p(foreachinarea)
 *------------------------------------------
 */
int pc_calc_pvprank_sub (struct block_list *bl, va_list ap)
{
    struct map_session_data *sd1, *sd2 = NULL;

    nullpo_retr (0, bl);
    nullpo_retr (0, ap);
    nullpo_retr (0, sd1 = (struct map_session_data *) bl);
    nullpo_retr (0, sd2 = va_arg (ap, struct map_session_data *));

    if (sd1->pvp_point > sd2->pvp_point)
        sd2->pvp_rank++;
    return 0;
}

/*==========================================
 * PVP���ʌv�Z
 *------------------------------------------
 */
int pc_calc_pvprank (struct map_session_data *sd)
{
    int  old;
    struct map_data *m;

    nullpo_retr (0, sd);
    nullpo_retr (0, m = &map[sd->bl.m]);

    old = sd->pvp_rank;

    if (!(m->flag.pvp))
        return 0;
    sd->pvp_rank = 1;
    map_foreachinarea (pc_calc_pvprank_sub, sd->bl.m, 0, 0, m->xs, m->ys,
                       BL_PC, sd);
    if (old != sd->pvp_rank || sd->pvp_lastusers != m->users)
        clif_pvpset (sd, sd->pvp_rank, sd->pvp_lastusers = m->users, 0);
    return sd->pvp_rank;
}

/*==========================================
 * PVP���ʌv�Z(timer)
 *------------------------------------------
 */
int pc_calc_pvprank_timer (int tid, unsigned int tick, int id, int data)
{
    struct map_session_data *sd = NULL;
    if (battle_config.pk_mode)  // disable pvp ranking if pk_mode on [Valaris]
        return 0;

    sd = map_id2sd (id);
    if (sd == NULL)
        return 0;
    sd->pvp_timer = -1;
    if (pc_calc_pvprank (sd) > 0)
        sd->pvp_timer = add_timer (gettick () + PVP_CALCRANK_INTERVAL,
                                   pc_calc_pvprank_timer, id, data);
    return 0;
}

/*==========================================
 * sd�͌������Ă��邩(�����̏ꍇ�͑�����char_id���Ԃ�)
 *------------------------------------------
 */
int pc_ismarried (struct map_session_data *sd)
{
    if (sd == NULL)
        return -1;
    if (sd->status.partner_id > 0)
        return sd->status.partner_id;
    else
        return 0;
}

/*==========================================
 * sd��dstsd�ƌ���(dstsd��sd�̌������������ɍs��)
 *------------------------------------------
 */
int pc_marriage (struct map_session_data *sd, struct map_session_data *dstsd)
{
    if (sd == NULL || dstsd == NULL || sd->status.partner_id > 0
        || dstsd->status.partner_id > 0)
        return -1;
    sd->status.partner_id = dstsd->status.char_id;
    dstsd->status.partner_id = sd->status.char_id;
    return 0;
}

/*==========================================
 * sd������(������sd->status.partner_id�Ɉ˂�)(���������ɗ����E�����w�֎������D)
 *------------------------------------------
 */
int pc_divorce (struct map_session_data *sd)
{
    struct map_session_data *p_sd = NULL;
    if (sd == NULL || !pc_ismarried (sd))
        return -1;

    // If both are on map server we don't need to bother the char server
    if ((p_sd =
         map_nick2sd (map_charid2nick (sd->status.partner_id))) != NULL)
    {
        if (p_sd->status.partner_id != sd->status.char_id
            || sd->status.partner_id != p_sd->status.char_id)
        {
            printf ("pc_divorce: Illegal partner_id sd=%d p_sd=%d\n",
                    sd->status.partner_id, p_sd->status.partner_id);
            return -1;
        }
        p_sd->status.partner_id = 0;
        sd->status.partner_id = 0;

        if (sd->npc_flags.divorce)
        {
            sd->npc_flags.divorce = 0;
            map_scriptcont (sd, sd->npc_id);
        }
    }
    else
        chrif_send_divorce (sd->status.char_id);

    return 0;
}

/*==========================================
 * sd�̑�����map_session_data���Ԃ�
 *------------------------------------------
 */
struct map_session_data *pc_get_partner (struct map_session_data *sd)
{
    struct map_session_data *p_sd = NULL;
    char *nick;
    if (sd == NULL || !pc_ismarried (sd))
        return NULL;

    nick = map_charid2nick (sd->status.partner_id);

    if (nick == NULL)
        return NULL;

    if ((p_sd = map_nick2sd (nick)) == NULL)
        return NULL;

    return p_sd;
}

//
// ���R�񕜕�
//
/*==========================================
 * SP�񕜗ʌv�Z
 *------------------------------------------
 */
static int natural_heal_tick, natural_heal_prev_tick, natural_heal_diff_tick;
static int pc_spheal (struct map_session_data *sd)
{
    int  a;
    struct guild_castle *gc = NULL;

    nullpo_retr (0, sd);

    a = natural_heal_diff_tick;
    if (pc_issit (sd))
        a += a;
    if (sd->sc_data[SC_MAGNIFICAT].timer != -1) // �}�O�j�t�B�J�[�g
        a += a;

    gc = guild_mapname2gc (sd->mapname);    // Increased guild castle regen [Valaris]
    if (gc)
    {
        struct guild *g;
        g = guild_search (sd->status.guild_id);
        if (g && g->guild_id == gc->guild_id)
            a += a;
    }                           // end addition [Valaris]

    return a;
}

/*==========================================
 * HP�񕜗ʌv�Z
 *------------------------------------------
 */
static int pc_hpheal (struct map_session_data *sd)
{
    int  a;
    struct guild_castle *gc;

    nullpo_retr (0, sd);

    a = natural_heal_diff_tick;
    if (pc_issit (sd))
        a += a;
    if (sd->sc_data[SC_MAGNIFICAT].timer != -1) // Modified by RoVeRT
        a += a;

    gc = guild_mapname2gc (sd->mapname);    // Increased guild castle regen [Valaris]
    if (gc)
    {
        struct guild *g;
        g = guild_search (sd->status.guild_id);
        if (g && g->guild_id == gc->guild_id)
            a += a;
    }                           // end addition [Valaris]

    return a;
}

static int pc_natural_heal_hp (struct map_session_data *sd)
{
    int  bhp;
    int  inc_num, bonus, skill, hp_flag;

    nullpo_retr (0, sd);

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)  // Modified by RoVeRT
        return 0;

    if (pc_checkoverhp (sd))
    {
        sd->hp_sub = sd->inchealhptick = 0;
        return 0;
    }

    bhp = sd->status.hp;
    hp_flag = (pc_checkskill (sd, SM_MOVINGRECOVERY) > 0
               && sd->walktimer != -1);

    if (sd->walktimer == -1)
    {
        inc_num = pc_hpheal (sd);
        if (sd->sc_data[SC_TENSIONRELAX].timer != -1)
        {                       // �e���V���������b�N�X
            sd->hp_sub += 2 * inc_num;
            sd->inchealhptick += 3 * natural_heal_diff_tick;
        }
        else
        {
            sd->hp_sub += inc_num;
            sd->inchealhptick += natural_heal_diff_tick;
        }
    }
    else if (hp_flag)
    {
        inc_num = pc_hpheal (sd);
        sd->hp_sub += inc_num;
        sd->inchealhptick = 0;
    }
    else
    {
        sd->hp_sub = sd->inchealhptick = 0;
        return 0;
    }

    if (sd->hp_sub >= battle_config.natural_healhp_interval)
    {
        bonus = sd->nhealhp;
        if (hp_flag)
        {
            bonus >>= 2;
            if (bonus <= 0)
                bonus = 1;
        }
        while (sd->hp_sub >= battle_config.natural_healhp_interval)
        {
            sd->hp_sub -= battle_config.natural_healhp_interval;
            if (sd->status.hp + bonus <= sd->status.max_hp)
                sd->status.hp += bonus;
            else
            {
                sd->status.hp = sd->status.max_hp;
                sd->hp_sub = sd->inchealhptick = 0;
            }
        }
    }
    if (bhp != sd->status.hp)
        clif_updatestatus (sd, SP_HP);

    if (sd->nshealhp > 0)
    {
        if (sd->inchealhptick >= battle_config.natural_heal_skill_interval
            && sd->status.hp < sd->status.max_hp)
        {
            bonus = sd->nshealhp;
            while (sd->inchealhptick >=
                   battle_config.natural_heal_skill_interval)
            {
                sd->inchealhptick -=
                    battle_config.natural_heal_skill_interval;
                if (sd->status.hp + bonus <= sd->status.max_hp)
                    sd->status.hp += bonus;
                else
                {
                    bonus = sd->status.max_hp - sd->status.hp;
                    sd->status.hp = sd->status.max_hp;
                    sd->hp_sub = sd->inchealhptick = 0;
                }
                clif_heal (sd->fd, SP_HP, bonus);
            }
        }
    }
    else
        sd->inchealhptick = 0;

    return 0;

    if (sd->sc_data[SC_APPLEIDUN].timer != -1)
    {                           // Apple of Idun
        if (sd->inchealhptick >= 6000 && sd->status.hp < sd->status.max_hp)
        {
            bonus = skill * 20;
            while (sd->inchealhptick >= 6000)
            {
                sd->inchealhptick -= 6000;
                if (sd->status.hp + bonus <= sd->status.max_hp)
                    sd->status.hp += bonus;
                else
                {
                    bonus = sd->status.max_hp - sd->status.hp;
                    sd->status.hp = sd->status.max_hp;
                    sd->hp_sub = sd->inchealhptick = 0;
                }
                clif_heal (sd->fd, SP_HP, bonus);
            }
        }
    }
    else
        sd->inchealhptick = 0;

    return 0;
}

static int pc_natural_heal_sp (struct map_session_data *sd)
{
    int  bsp;
    int  inc_num, bonus;

    nullpo_retr (0, sd);

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)  // Modified by RoVeRT
        return 0;

    if (pc_checkoversp (sd))
    {
        sd->sp_sub = sd->inchealsptick = 0;
        return 0;
    }

    bsp = sd->status.sp;

    inc_num = pc_spheal (sd);
    if (sd->sc_data[SC_EXPLOSIONSPIRITS].timer == -1)
        sd->sp_sub += inc_num;
    if (sd->walktimer == -1)
        sd->inchealsptick += natural_heal_diff_tick;
    else
        sd->inchealsptick = 0;

    if (sd->sp_sub >= battle_config.natural_healsp_interval)
    {
        bonus = sd->nhealsp;;
        while (sd->sp_sub >= battle_config.natural_healsp_interval)
        {
            sd->sp_sub -= battle_config.natural_healsp_interval;
            if (sd->status.sp + bonus <= sd->status.max_sp)
                sd->status.sp += bonus;
            else
            {
                sd->status.sp = sd->status.max_sp;
                sd->sp_sub = sd->inchealsptick = 0;
            }
        }
    }

    if (bsp != sd->status.sp)
        clif_updatestatus (sd, SP_SP);

    if (sd->nshealsp > 0)
    {
        if (sd->inchealsptick >= battle_config.natural_heal_skill_interval
            && sd->status.sp < sd->status.max_sp)
        {
            struct pc_base_job s_class = pc_calc_base_job (sd->status.class);
            if (sd->doridori_counter && s_class.job == 23)
                bonus = sd->nshealsp * 2;
            else
                bonus = sd->nshealsp;
            sd->doridori_counter = 0;
            while (sd->inchealsptick >=
                   battle_config.natural_heal_skill_interval)
            {
                sd->inchealsptick -=
                    battle_config.natural_heal_skill_interval;
                if (sd->status.sp + bonus <= sd->status.max_sp)
                    sd->status.sp += bonus;
                else
                {
                    bonus = sd->status.max_sp - sd->status.sp;
                    sd->status.sp = sd->status.max_sp;
                    sd->sp_sub = sd->inchealsptick = 0;
                }
                clif_heal (sd->fd, SP_SP, bonus);
            }
        }
    }
    else
        sd->inchealsptick = 0;

    return 0;
}

static int pc_spirit_heal_hp (struct map_session_data *sd, int level)
{
    int  bonus_hp, interval = battle_config.natural_heal_skill_interval;
    struct status_change *sc_data = battle_get_sc_data (&sd->bl);

    nullpo_retr (0, sd);

    if (pc_checkoverhp (sd))
    {
        sd->inchealspirithptick = 0;
        return 0;
    }

    sd->inchealspirithptick += natural_heal_diff_tick;

    if (sd->weight * 100 / sd->max_weight >=
        battle_config.natural_heal_weight_rate
        && sc_data[SC_FLYING_BACKPACK].timer == -1)
        interval += interval;

    if (sd->inchealspirithptick >= interval)
    {
        bonus_hp = sd->nsshealhp;
        while (sd->inchealspirithptick >= interval)
        {
            if (pc_issit (sd))
            {
                sd->inchealspirithptick -= interval;
                if (sd->status.hp < sd->status.max_hp)
                {
                    if (sd->status.hp + bonus_hp <= sd->status.max_hp)
                        sd->status.hp += bonus_hp;
                    else
                    {
                        bonus_hp = sd->status.max_hp - sd->status.hp;
                        sd->status.hp = sd->status.max_hp;
                    }
                    clif_heal (sd->fd, SP_HP, bonus_hp);
                    sd->inchealspirithptick = 0;
                }
            }
            else
            {
                sd->inchealspirithptick -= natural_heal_diff_tick;
                break;
            }
        }
    }

    return 0;
}

static int pc_spirit_heal_sp (struct map_session_data *sd, int level)
{
    int  bonus_sp, interval = battle_config.natural_heal_skill_interval;

    nullpo_retr (0, sd);

    if (pc_checkoversp (sd))
    {
        sd->inchealspiritsptick = 0;
        return 0;
    }

    sd->inchealspiritsptick += natural_heal_diff_tick;

    if (sd->weight * 100 / sd->max_weight >=
        battle_config.natural_heal_weight_rate)
        interval += interval;

    if (sd->inchealspiritsptick >= interval)
    {
        bonus_sp = sd->nsshealsp;
        while (sd->inchealspiritsptick >= interval)
        {
            if (pc_issit (sd))
            {
                sd->inchealspiritsptick -= interval;
                if (sd->status.sp < sd->status.max_sp)
                {
                    if (sd->status.sp + bonus_sp <= sd->status.max_sp)
                        sd->status.sp += bonus_sp;
                    else
                    {
                        bonus_sp = sd->status.max_sp - sd->status.sp;
                        sd->status.sp = sd->status.max_sp;
                    }
                    clif_heal (sd->fd, SP_SP, bonus_sp);
                    sd->inchealspiritsptick = 0;
                }
            }
            else
            {
                sd->inchealspiritsptick -= natural_heal_diff_tick;
                break;
            }
        }
    }

    return 0;
}

/*==========================================
 * HP/SP ���R���� �e�N���C�A���g
 *------------------------------------------
 */
static int pc_itemheal_effect (struct map_session_data *sd, int hp, int sp);

static int
pc_quickregenerate_effect (struct quick_regeneration *quick_regen,
                           int heal_speed)
{
    if (!(quick_regen->tickdelay--))
    {
        int  bonus =
            MIN (heal_speed * battle_config.itemheal_regeneration_factor,
                 quick_regen->amount);

        quick_regen->amount -= bonus;

        quick_regen->tickdelay = quick_regen->speed;

        return bonus;
    }

    return 0;
}

static int pc_natural_heal_sub (struct map_session_data *sd, va_list ap)
{
    int  skill;

    nullpo_retr (0, sd);

    if (sd->heal_xp > 0)
    {
        if (sd->heal_xp < 64)
            --sd->heal_xp;      // [Fate] Slowly reduce XP that healers can get for healing this char
        else
            sd->heal_xp -= (sd->heal_xp >> 6);
    }

    // Hijack this callback:  Adjust spellpower bonus
    if (sd->spellpower_bonus_target < sd->spellpower_bonus_current)
    {
        sd->spellpower_bonus_current = sd->spellpower_bonus_target;
        pc_calcstatus (sd, 0);
    }
    else if (sd->spellpower_bonus_target > sd->spellpower_bonus_current)
    {
        sd->spellpower_bonus_current +=
            1 +
            ((sd->spellpower_bonus_target -
              sd->spellpower_bonus_current) >> 5);
        pc_calcstatus (sd, 0);
    }

    if (sd->sc_data[SC_HALT_REGENERATE].timer != -1)
        return 0;

    if (sd->quick_regeneration_hp.amount || sd->quick_regeneration_sp.amount)
    {
        int  hp_bonus = pc_quickregenerate_effect (&sd->quick_regeneration_hp,
                                                   (sd->sc_data[SC_POISON].timer == -1 || sd->sc_data[SC_SLOWPOISON].timer != -1) ? sd->nhealhp : 1);   // [fate] slow down when poisoned
        int  sp_bonus = pc_quickregenerate_effect (&sd->quick_regeneration_sp,
                                                   sd->nhealsp);

        pc_itemheal_effect (sd, hp_bonus, sp_bonus);
    }
    skill_update_heal_animation (sd);   // if needed.

// -- moonsoul (if conditions below altered to disallow natural healing if under berserk status)
    if ((sd->sc_data[SC_FLYING_BACKPACK].timer != -1
         || battle_config.natural_heal_weight_rate > 100
         || sd->weight * 100 / sd->max_weight <
         battle_config.natural_heal_weight_rate) && !pc_isdead (sd)
        && !pc_ishiding (sd) && sd->sc_data[SC_POISON].timer == -1)
    {
        pc_natural_heal_hp (sd);
        if (sd->sc_data && sd->sc_data[SC_EXTREMITYFIST].timer == -1 && //���C�����Ԃł�SP���񕜂��Ȃ�
            sd->sc_data[SC_DANCING].timer == -1 &&  //�_���X���Ԃł�SP���񕜂��Ȃ�
            sd->sc_data[SC_BERSERK].timer == -1 //�o�[�T�[�N���Ԃł�SP���񕜂��Ȃ�
            )
            pc_natural_heal_sp (sd);
    }
    else
    {
        sd->hp_sub = sd->inchealhptick = 0;
        sd->sp_sub = sd->inchealsptick = 0;
    }
    if ((skill = pc_checkskill (sd, MO_SPIRITSRECOVERY)) > 0
        && !pc_ishiding (sd) && sd->sc_data[SC_POISON].timer == -1
        && sd->sc_data[SC_BERSERK].timer == -1)
    {
        pc_spirit_heal_hp (sd, skill);
        pc_spirit_heal_sp (sd, skill);
    }
    else
    {
        sd->inchealspirithptick = 0;
        sd->inchealspiritsptick = 0;
    }
    return 0;
}

/*==========================================
 * HP/SP���R���� (interval timer�֐�)
 *------------------------------------------
 */
int pc_natural_heal (int tid, unsigned int tick, int id, int data)
{
    natural_heal_tick = tick;
    natural_heal_diff_tick =
        DIFF_TICK (natural_heal_tick, natural_heal_prev_tick);
    clif_foreachclient (pc_natural_heal_sub);

    natural_heal_prev_tick = tick;
    return 0;
}

/*==========================================
 * �Z�[�u�|�C���g�̕ۑ�
 *------------------------------------------
 */
int pc_setsavepoint (struct map_session_data *sd, char *mapname, int x, int y)
{
    nullpo_retr (0, sd);

    strncpy (sd->status.save_point.map, mapname, 23);
    sd->status.save_point.map[23] = '\0';
    sd->status.save_point.x = x;
    sd->status.save_point.y = y;

    return 0;
}

/*==========================================
 * �����Z�[�u �e�N���C�A���g
 *------------------------------------------
 */
static int last_save_fd, save_flag;
static int pc_autosave_sub (struct map_session_data *sd, va_list ap)
{
    nullpo_retr (0, sd);

    if (save_flag == 0 && sd->fd > last_save_fd)
    {
        struct guild_castle *gc = NULL;
        int  i;

        pc_makesavestatus (sd);
        chrif_save (sd);

        for (i = 0; i < MAX_GUILDCASTLE; i++)
        {
            gc = guild_castle_search (i);
            if (!gc)
                continue;
            if (gc->visibleG0 == 1)
                guild_castledatasave (gc->castle_id, 18, gc->Ghp0);
            if (gc->visibleG1 == 1)
                guild_castledatasave (gc->castle_id, 19, gc->Ghp1);
            if (gc->visibleG2 == 1)
                guild_castledatasave (gc->castle_id, 20, gc->Ghp2);
            if (gc->visibleG3 == 1)
                guild_castledatasave (gc->castle_id, 21, gc->Ghp3);
            if (gc->visibleG4 == 1)
                guild_castledatasave (gc->castle_id, 22, gc->Ghp4);
            if (gc->visibleG5 == 1)
                guild_castledatasave (gc->castle_id, 23, gc->Ghp5);
            if (gc->visibleG6 == 1)
                guild_castledatasave (gc->castle_id, 24, gc->Ghp6);
            if (gc->visibleG7 == 1)
                guild_castledatasave (gc->castle_id, 25, gc->Ghp7);
        }

        save_flag = 1;
        last_save_fd = sd->fd;
    }

    return 0;
}

/*==========================================
 * �����Z�[�u (timer�֐�)
 *------------------------------------------
 */
int pc_autosave (int tid, unsigned int tick, int id, int data)
{
    int  interval;

    save_flag = 0;
    clif_foreachclient (pc_autosave_sub);
    if (save_flag == 0)
        last_save_fd = 0;

    interval = autosave_interval / (clif_countusers () + 1);
    if (interval <= 0)
        interval = 1;
    add_timer (gettick () + interval, pc_autosave, 0, 0);

    return 0;
}

int pc_read_gm_account (int fd)
{
    int  i = 0;
    if (gm_account != NULL)
        free (gm_account);
    GM_num = 0;

    gm_account =
        calloc (sizeof (struct gm_account) * ((RFIFOW (fd, 2) - 4) / 5), 1);
    for (i = 4; i < RFIFOW (fd, 2); i = i + 5)
    {
        gm_account[GM_num].account_id = RFIFOL (fd, i);
        gm_account[GM_num].level = (int) RFIFOB (fd, i + 4);
        //printf("GM account: %d -> level %d\n", gm_account[GM_num].account_id, gm_account[GM_num].level);
        GM_num++;
    }
    return GM_num;
}

/*==========================================
 * timer to do the day
 *------------------------------------------
 */
int map_day_timer (int tid, unsigned int tick, int id, int data)
{                               // by [yor]
    struct map_session_data *pl_sd = NULL;
    int  i;
    char tmpstr[1024];

    if (battle_config.day_duration > 0)
    {                           // if we want a day
        if (night_flag != 0)
        {
            strcpy (tmpstr, msg_txt (502)); // The day has arrived!
            night_flag = 0;     // 0=day, 1=night [Yor]
            for (i = 0; i < fd_max; i++)
            {
                if (session[i] && (pl_sd = session[i]->session_data)
                    && pl_sd->state.auth)
                {
                    pl_sd->opt2 &= ~STATE_BLIND;
                    clif_changeoption (&pl_sd->bl);
                    clif_wis_message (pl_sd->fd, wisp_server_name, tmpstr,
                                      strlen (tmpstr) + 1);
                }
            }
        }
    }

    return 0;
}

/*==========================================
 * timer to do the night
 *------------------------------------------
 */
int map_night_timer (int tid, unsigned int tick, int id, int data)
{                               // by [yor]
    struct map_session_data *pl_sd = NULL;
    int  i;
    char tmpstr[1024];

    if (battle_config.night_duration > 0)
    {                           // if we want a night
        if (night_flag == 0)
        {
            strcpy (tmpstr, msg_txt (503)); // The night has fallen...
            night_flag = 1;     // 0=day, 1=night [Yor]
            for (i = 0; i < fd_max; i++)
            {
                if (session[i] && (pl_sd = session[i]->session_data)
                    && pl_sd->state.auth)
                {
                    pl_sd->opt2 |= STATE_BLIND;
                    clif_changeoption (&pl_sd->bl);
                    clif_wis_message (pl_sd->fd, wisp_server_name, tmpstr,
                                      strlen (tmpstr) + 1);
                }
            }
        }
    }

    return 0;
}

void pc_setstand (struct map_session_data *sd)
{
    nullpo_retv (sd);

    if (sd->sc_data && sd->sc_data[SC_TENSIONRELAX].timer != -1)
        skill_status_change_end (&sd->bl, SC_TENSIONRELAX, -1);

    sd->state.dead_sit = 0;
}

//
// ��������
//
/*==========================================
 * �ݒ��t�@�C���ǂݍ���
 * exp.txt �K�v�o���l
 * job_db1.txt �d��,hp,sp,�U�����x
 * job_db2.txt job�\�͒l�{�[�i�X
 * skill_tree.txt �e�E���̃X�L���c���[
 * attr_fix.txt �����C���e�[�u��
 * size_fix.txt �T�C�Y�␳�e�[�u��
 * refine_db.txt ���B�f�[�^�e�[�u��
 *------------------------------------------
 */
int pc_readdb (void)
{
    int  i, j, k;
    FILE *fp;
    char line[1024], *p;

    // �K�v�o���l�ǂݍ���

    fp = fopen_ ("db/exp.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/exp.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        int  bn, b1, b2, b3, b4, b5, b6, jn, j1, j2, j3, j4, j5, j6;
        if (line[0] == '/' && line[1] == '/')
            continue;
        if (sscanf
            (line, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &bn, &b1, &b2,
             &b3, &b4, &b5, &b6, &jn, &j1, &j2, &j3, &j4, &j5, &j6) != 14)
            continue;
        exp_table[0][i] = bn;
        exp_table[1][i] = b1;
        exp_table[2][i] = b2;
        exp_table[3][i] = b3;
        exp_table[4][i] = b4;
        exp_table[5][i] = b5;
        exp_table[6][i] = b6;
        exp_table[7][i] = jn;
        exp_table[8][i] = j1;
        exp_table[9][i] = j2;
        exp_table[10][i] = j3;
        exp_table[11][i] = j4;
        exp_table[12][i] = j5;
        exp_table[13][i] = j6;
        i++;
        if (i >= battle_config.maximum_level)
            break;
    }
    fclose_ (fp);
    printf ("read db/exp.txt done\n");

    // JOB�␳���l�P
    fp = fopen_ ("db/job_db1.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/job_db1.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        char *split[50];
        if (line[0] == '/' && line[1] == '/')
            continue;
        for (j = 0, p = line; j < 21 && p; j++)
        {
            split[j] = p;
            p = strchr (p, ',');
            if (p)
                *p++ = 0;
        }
        if (j < 21)
            continue;
        max_weight_base[i] = atoi (split[0]);
        hp_coefficient[i] = atoi (split[1]);
        hp_coefficient2[i] = atoi (split[2]);
        sp_coefficient[i] = atoi (split[3]);
        for (j = 0; j < 17; j++)
            aspd_base[i][j] = atoi (split[j + 4]);
        i++;
// -- moonsoul (below two lines added to accommodate high numbered new class ids)
        if (i == 24)
            i = 4001;
        if (i == MAX_PC_CLASS)
            break;
    }
    fclose_ (fp);
    printf ("read db/job_db1.txt done\n");

    // JOB�{�[�i�X
    fp = fopen_ ("db/job_db2.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/job_db2.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        if (line[0] == '/' && line[1] == '/')
            continue;
        for (j = 0, p = line; j < MAX_LEVEL && p; j++)
        {
            if (sscanf (p, "%d", &k) == 0)
                break;
            job_bonus[0][i][j] = k;
            job_bonus[2][i][j] = k; //�{�q�E�̃{�[�i�X�͕������Ȃ��̂ŉ�
            p = strchr (p, ',');
            if (p)
                p++;
        }
        i++;
// -- moonsoul (below two lines added to accommodate high numbered new class ids)
        if (i == 24)
            i = 4001;
        if (i == MAX_PC_CLASS)
            break;
    }
    fclose_ (fp);
    printf ("read db/job_db2.txt done\n");

    // JOB�{�[�i�X2 �]���E�p
    fp = fopen_ ("db/job_db2-2.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/job_db2-2.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        if (line[0] == '/' && line[1] == '/')
            continue;
        for (j = 0, p = line; j < MAX_LEVEL && p; j++)
        {
            if (sscanf (p, "%d", &k) == 0)
                break;
            job_bonus[1][i][j] = k;
            p = strchr (p, ',');
            if (p)
                p++;
        }
        i++;
        if (i == MAX_PC_CLASS)
            break;
    }
    fclose_ (fp);
    printf ("read db/job_db2-2.txt done\n");

    // �X�L���c���[
    memset (skill_tree, 0, sizeof (skill_tree));
    fp = fopen_ ("db/skill_tree.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/skill_tree.txt\n");
        return 1;
    }
    while (fgets (line, sizeof (line) - 1, fp))
    {
        char *split[50];
        if (line[0] == '/' && line[1] == '/')
            continue;
        for (j = 0, p = line; j < 13 && p; j++)
        {
            split[j] = p;
            p = strchr (p, ',');
            if (p)
                *p++ = 0;
        }
        if (j < 13)
            continue;
        i = atoi (split[0]);
        for (j = 0; skill_tree[0][i][j].id; j++);
        skill_tree[0][i][j].id = atoi (split[1]);
        skill_tree[0][i][j].max = atoi (split[2]);
        skill_tree[2][i][j].id = atoi (split[1]);   //�{�q�E�͗ǂ��������Ȃ��̂Ŏb��
        skill_tree[2][i][j].max = atoi (split[2]);  //�{�q�E�͗ǂ��������Ȃ��̂Ŏb��
        for (k = 0; k < 5; k++)
        {
            skill_tree[0][i][j].need[k].id = atoi (split[k * 2 + 3]);
            skill_tree[0][i][j].need[k].lv = atoi (split[k * 2 + 4]);
            skill_tree[2][i][j].need[k].id = atoi (split[k * 2 + 3]);   //�{�q�E�͗ǂ��������Ȃ��̂Ŏb��
            skill_tree[2][i][j].need[k].lv = atoi (split[k * 2 + 4]);   //�{�q�E�͗ǂ��������Ȃ��̂Ŏb��
        }
    }
    fclose_ (fp);
    printf ("read db/skill_tree.txt done\n");

    // �����C���e�[�u��
    for (i = 0; i < 4; i++)
        for (j = 0; j < 10; j++)
            for (k = 0; k < 10; k++)
                attr_fix_table[i][j][k] = 100;
    fp = fopen_ ("db/attr_fix.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/attr_fix.txt\n");
        return 1;
    }
    while (fgets (line, sizeof (line) - 1, fp))
    {
        char *split[10];
        int  lv, n;
        if (line[0] == '/' && line[1] == '/')
            continue;
        for (j = 0, p = line; j < 3 && p; j++)
        {
            split[j] = p;
            p = strchr (p, ',');
            if (p)
                *p++ = 0;
        }
        lv = atoi (split[0]);
        n = atoi (split[1]);
//      printf("%d %d\n",lv,n);

        for (i = 0; i < n;)
        {
            if (!fgets (line, sizeof (line) - 1, fp))
                break;
            if (line[0] == '/' && line[1] == '/')
                continue;

            for (j = 0, p = line; j < n && p; j++)
            {
                while (*p == 32 && *p > 0)
                    p++;
                attr_fix_table[lv - 1][i][j] = atoi (p);
                if (battle_config.attr_recover == 0
                    && attr_fix_table[lv - 1][i][j] < 0)
                    attr_fix_table[lv - 1][i][j] = 0;
                p = strchr (p, ',');
                if (p)
                    *p++ = 0;
            }

            i++;
        }
    }
    fclose_ (fp);
    printf ("read db/attr_fix.txt done\n");

    // �T�C�Y�␳�e�[�u��
    for (i = 0; i < 3; i++)
        for (j = 0; j < 20; j++)
            atkmods[i][j] = 100;
    fp = fopen_ ("db/size_fix.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/size_fix.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        char *split[20];
        if (line[0] == '/' && line[1] == '/')
            continue;
        if (atoi (line) <= 0)
            continue;
        memset (split, 0, sizeof (split));
        for (j = 0, p = line; j < 20 && p; j++)
        {
            split[j] = p;
            p = strchr (p, ',');
            if (p)
                *p++ = 0;
        }
        for (j = 0; j < 20 && split[j]; j++)
            atkmods[i][j] = atoi (split[j]);
        i++;
    }
    fclose_ (fp);
    printf ("read db/size_fix.txt done\n");

    // ���B�f�[�^�e�[�u��
    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 10; j++)
            percentrefinery[i][j] = 100;
        refinebonus[i][0] = 0;
        refinebonus[i][1] = 0;
        refinebonus[i][2] = 10;
    }
    fp = fopen_ ("db/refine_db.txt", "r");
    if (fp == NULL)
    {
        printf ("can't read db/refine_db.txt\n");
        return 1;
    }
    i = 0;
    while (fgets (line, sizeof (line) - 1, fp))
    {
        char *split[16];
        if (line[0] == '/' && line[1] == '/')
            continue;
        if (atoi (line) <= 0)
            continue;
        memset (split, 0, sizeof (split));
        for (j = 0, p = line; j < 16 && p; j++)
        {
            split[j] = p;
            p = strchr (p, ',');
            if (p)
                *p++ = 0;
        }
        refinebonus[i][0] = atoi (split[0]);    // ���B�{�[�i�X
        refinebonus[i][1] = atoi (split[1]);    // �ߏ萸�B�{�[�i�X
        refinebonus[i][2] = atoi (split[2]);    // ��S���B��E
        for (j = 0; j < 10 && split[j]; j++)
            percentrefinery[i][j] = atoi (split[j + 3]);
        i++;
    }
    fclose_ (fp);               //Lupus. close this file!!!
    printf ("read db/refine_db.txt done\n");

    return 0;
}

static int pc_calc_sigma (void)
{
    int  i, j, k;

    for (i = 0; i < MAX_PC_CLASS; i++)
    {
        memset (hp_sigma_val[i], 0, sizeof (hp_sigma_val[i]));
        for (k = 0, j = 2; j <= MAX_LEVEL; j++)
        {
            k += hp_coefficient[i] * j + 50;
            k -= k % 100;
            hp_sigma_val[i][j - 1] = k;
        }
    }
    return 0;
}

static void pc_statpointdb (void)
{
    char *buf_stat;
    int  i = 0, j = 0, k = 0, l = 0, end = 0;

    FILE *stp;

    stp = fopen_ ("db/statpoint.txt", "r");

    if (stp == NULL)
    {
        printf ("can't read db/statpoint.txt\n");
        return;
    }

    fseek (stp, 0, SEEK_END);
    end = ftell (stp);
    rewind (stp);

    buf_stat = (char *) malloc (end + 1);
    l = fread (buf_stat, 1, end, stp);
    fclose_ (stp);
    printf ("read db/statpoint.txt done (size=%d)\n", l);

    for (i = 0; i < 255; i++)
    {
        j = 0;
        while (*(buf_stat + k) != '\n')
        {
            statp[i][j] = *(buf_stat + k);
            j++;
            k++;
        }
        statp[i][j + 1] = '\0';
        k++;
    }

    free (buf_stat);
}

/*==========================================
 * pc�� �W������
 *------------------------------------------
 */
int do_init_pc (void)
{
    pc_readdb ();
    pc_statpointdb ();
    pc_calc_sigma ();

//  gm_account_db = numdb_init();

    add_timer_func_list (pc_walk, "pc_walk");
    add_timer_func_list (pc_attack_timer, "pc_attack_timer");
    add_timer_func_list (pc_natural_heal, "pc_natural_heal");
    add_timer_func_list (pc_invincible_timer, "pc_invincible_timer");
    add_timer_func_list (pc_eventtimer, "pc_eventtimer");
    add_timer_func_list (pc_calc_pvprank_timer, "pc_calc_pvprank_timer");
    add_timer_func_list (pc_autosave, "pc_autosave");
    add_timer_func_list (pc_spiritball_timer, "pc_spiritball_timer");
    add_timer_interval ((natural_heal_prev_tick =
                         gettick () + NATURAL_HEAL_INTERVAL), pc_natural_heal,
                        0, 0, NATURAL_HEAL_INTERVAL);
    add_timer (gettick () + autosave_interval, pc_autosave, 0, 0);

    // add night/day timer (by [yor])
    add_timer_func_list (map_day_timer, "map_day_timer");   // by [yor]
    add_timer_func_list (map_night_timer, "map_night_timer");   // by [yor]
    {
        int  day_duration = battle_config.day_duration;
        int  night_duration = battle_config.night_duration;
        if (day_duration < 60000)
            day_duration = 60000;
        if (night_duration < 60000)
            night_duration = 60000;
        if (battle_config.night_at_start == 0)
        {
            night_flag = 0;     // 0=day, 1=night [Yor]
            day_timer_tid =
                add_timer_interval (gettick () + day_duration +
                                    night_duration, map_day_timer, 0, 0,
                                    day_duration + night_duration);
            night_timer_tid =
                add_timer_interval (gettick () + day_duration,
                                    map_night_timer, 0, 0,
                                    day_duration + night_duration);
        }
        else
        {
            night_flag = 1;     // 0=day, 1=night [Yor]
            day_timer_tid =
                add_timer_interval (gettick () + night_duration,
                                    map_day_timer, 0, 0,
                                    day_duration + night_duration);
            night_timer_tid =
                add_timer_interval (gettick () + day_duration +
                                    night_duration, map_night_timer, 0, 0,
                                    day_duration + night_duration);
        }
    }

    return 0;
}

void pc_cleanup (struct map_session_data *sd)
{
    magic_stop_completely (sd);
}

void pc_invisibility (struct map_session_data *sd, int enabled)
{
    if (enabled && !(sd->status.option & OPTION_INVISIBILITY))
    {
        clif_clearchar_area (&sd->bl, 3);
        sd->status.option |= OPTION_INVISIBILITY;
        clif_status_change (&sd->bl, CLIF_OPTION_SC_INVISIBILITY, 1);
    }
    else if (!enabled)
    {
        sd->status.option &= ~OPTION_INVISIBILITY;
        clif_status_change (&sd->bl, CLIF_OPTION_SC_INVISIBILITY, 0);
        pc_setpos (sd, map[sd->bl.m].name, sd->bl.x, sd->bl.y, 3);
    }
}

int pc_logout (struct map_session_data *sd) // [fate] Player logs out
{
    if (!sd)
        return 0;

    if (sd->sc_data[SC_POISON].timer != -1)
        sd->status.hp = 1;      // Logging out while poisoned -> bad

    MAP_LOG_STATS (sd, "LOGOUT") return 0;
}
