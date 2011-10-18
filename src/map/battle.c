// $Id: battle.c,v 1.10 2004/09/29 21:08:17 Akitasha Exp $
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "battle.h"

#include "timer.h"
#include "nullpo.h"
#include "malloc.h"

#include "clif.h"
#include "guild.h"
#include "itemdb.h"
#include "map.h"
#include "mob.h"
#include "pc.h"
#include "skill.h"
#include "../common/socket.h"
#include "mt_rand.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

int  attr_fix_table[4][10][10];

struct Battle_Config battle_config;

/*==========================================
 * ��_�Ԃ̋�����Ԃ�
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
static int distance (int x0, int y0, int x1, int y1)
{
    int  dx, dy;

    dx = abs (x0 - x1);
    dy = abs (y0 - y1);
    return dx > dy ? dx : dy;
}

/*==========================================
 * ���������b�N���Ă���Ώۂ̐���Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_counttargeted (struct block_list *bl, struct block_list *src,
                          int target_lv)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC)
        return pc_counttargeted ((struct map_session_data *) bl, src,
                                 target_lv);
    else if (bl->type == BL_MOB)
        return mob_counttargeted ((struct mob_data *) bl, src, target_lv);
    return 0;
}

/*==========================================
 * �Ώۂ�Class��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_class (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->class;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->status.class;
    else
        return 0;
}

/*==========================================
 * �Ώۂ̕�����Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_dir (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->dir;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->dir;
    else
        return 0;
}

/*==========================================
 * �Ώۂ̃��x����Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_lv (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->stats[MOB_LV];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->status.base_level;
    else
        return 0;
}

/*==========================================
 * �Ώۂ̎˒���Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_range (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return mob_db[((struct mob_data *) bl)->class].range;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->attackrange;
    else
        return 0;
}

/*==========================================
 * �Ώۂ�HP��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_hp (struct block_list *bl)
{
    nullpo_retr (1, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->hp;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->status.hp;
    else
        return 1;
}

/*==========================================
 * �Ώۂ�MHP��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_max_hp (struct block_list *bl)
{
    nullpo_retr (1, bl);
    if (bl->type == BL_PC && ((struct map_session_data *) bl))
        return ((struct map_session_data *) bl)->status.max_hp;
    else
    {
        struct status_change *sc_data = battle_get_sc_data (bl);
        int  max_hp = 1;
        if (bl->type == BL_MOB && ((struct mob_data *) bl))
        {
            max_hp = ((struct mob_data *) bl)->stats[MOB_MAX_HP];
            if (mob_db[((struct mob_data *) bl)->class].mexp > 0)
            {
                if (battle_config.mvp_hp_rate != 100)
                    max_hp = (max_hp * battle_config.mvp_hp_rate) / 100;
            }
            else
            {
                if (battle_config.monster_hp_rate != 100)
                    max_hp = (max_hp * battle_config.monster_hp_rate) / 100;
            }
        }
        if (sc_data)
        {
            if (sc_data[SC_APPLEIDUN].timer != -1)
                max_hp +=
                    ((5 + sc_data[SC_APPLEIDUN].val1 * 2 +
                      ((sc_data[SC_APPLEIDUN].val2 + 1) >> 1) +
                      sc_data[SC_APPLEIDUN].val3 / 10) * max_hp) / 100;
        }
        if (max_hp < 1)
            max_hp = 1;
        return max_hp;
    }
    return 1;
}

/*==========================================
 * �Ώۂ�Str��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_str (struct block_list *bl)
{
    int  str = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && ((struct mob_data *) bl))
        str = ((struct mob_data *) bl)->stats[MOB_STR];
    else if (bl->type == BL_PC && ((struct map_session_data *) bl))
        return ((struct map_session_data *) bl)->paramc[0];

    if (sc_data)
    {
        if (sc_data[SC_LOUD].timer != -1 && sc_data[SC_QUAGMIRE].timer == -1
            && bl->type != BL_PC)
            str += 4;
        if (sc_data[SC_BLESSING].timer != -1 && bl->type != BL_PC)
        {                       // �u���b�V���O
            int  race = battle_get_race (bl);
            if (battle_check_undead (race, battle_get_elem_type (bl))
                || race == 6)
                str >>= 1;      // �� ��/�s��
            else
                str += sc_data[SC_BLESSING].val1;   // ���̑�
        }
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            str += 5;
    }
    if (str < 0)
        str = 0;
    return str;
}

/*==========================================
 * �Ώۂ�Agi��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */

int battle_get_agi (struct block_list *bl)
{
    int  agi = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        agi = ((struct mob_data *) bl)->stats[MOB_AGI];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        agi = ((struct map_session_data *) bl)->paramc[1];

    if (sc_data)
    {
        if (sc_data[SC_INCREASEAGI].timer != -1 && sc_data[SC_QUAGMIRE].timer == -1 && sc_data[SC_DONTFORGETME].timer == -1 && bl->type != BL_PC)   // ���x����(PC��pc.c��)
            agi += 2 + sc_data[SC_INCREASEAGI].val1;

        if (sc_data[SC_CONCENTRATE].timer != -1
            && sc_data[SC_QUAGMIRE].timer == -1 && bl->type != BL_PC)
            agi += agi * (2 + sc_data[SC_CONCENTRATE].val1) / 100;

        if (sc_data[SC_DECREASEAGI].timer != -1)    // ���x����
            agi -= 2 + sc_data[SC_DECREASEAGI].val1;

        if (sc_data[SC_QUAGMIRE].timer != -1)   // �N�@�O�}�C�A
            agi >>= 1;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            agi += 5;
    }
    if (agi < 0)
        agi = 0;
    return agi;
}

/*==========================================
 * �Ώۂ�Vit��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_vit (struct block_list *bl)
{
    int  vit = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        vit = ((struct mob_data *) bl)->stats[MOB_VIT];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        vit = ((struct map_session_data *) bl)->paramc[2];
    if (sc_data)
    {
        if (sc_data[SC_STRIPARMOR].timer != -1 && bl->type != BL_PC)
            vit = vit * 60 / 100;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            vit += 5;
    }

    if (vit < 0)
        vit = 0;
    return vit;
}

/*==========================================
 * �Ώۂ�Int��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_int (struct block_list *bl)
{
    int  int_ = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        int_ = ((struct mob_data *) bl)->stats[MOB_INT];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        int_ = ((struct map_session_data *) bl)->paramc[3];

    if (sc_data)
    {
        if (sc_data[SC_BLESSING].timer != -1 && bl->type != BL_PC)
        {                       // �u���b�V���O
            int  race = battle_get_race (bl);
            if (battle_check_undead (race, battle_get_elem_type (bl))
                || race == 6)
                int_ >>= 1;     // �� ��/�s��
            else
                int_ += sc_data[SC_BLESSING].val1;  // ���̑�
        }
        if (sc_data[SC_STRIPHELM].timer != -1 && bl->type != BL_PC)
            int_ = int_ * 60 / 100;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            int_ += 5;
    }
    if (int_ < 0)
        int_ = 0;
    return int_;
}

/*==========================================
 * �Ώۂ�Dex��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_dex (struct block_list *bl)
{
    int  dex = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        dex = ((struct mob_data *) bl)->stats[MOB_DEX];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        dex = ((struct map_session_data *) bl)->paramc[4];

    if (sc_data)
    {
        if (sc_data[SC_CONCENTRATE].timer != -1
            && sc_data[SC_QUAGMIRE].timer == -1 && bl->type != BL_PC)
            dex += dex * (2 + sc_data[SC_CONCENTRATE].val1) / 100;

        if (sc_data[SC_BLESSING].timer != -1 && bl->type != BL_PC)
        {                       // �u���b�V���O
            int  race = battle_get_race (bl);
            if (battle_check_undead (race, battle_get_elem_type (bl))
                || race == 6)
                dex >>= 1;      // �� ��/�s��
            else
                dex += sc_data[SC_BLESSING].val1;   // ���̑�
        }

        if (sc_data[SC_QUAGMIRE].timer != -1)   // �N�@�O�}�C�A
            dex >>= 1;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            dex += 5;
    }
    if (dex < 0)
        dex = 0;
    return dex;
}

/*==========================================
 * �Ώۂ�Luk��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_luk (struct block_list *bl)
{
    int  luk = 0;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        luk = ((struct mob_data *) bl)->stats[MOB_LUK];
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        luk = ((struct map_session_data *) bl)->paramc[5];

    if (sc_data)
    {
        if (sc_data[SC_GLORIA].timer != -1 && bl->type != BL_PC)    // �O�����A(PC��pc.c��)
            luk += 30;
        if (sc_data[SC_CURSE].timer != -1)  // ��
            luk = 0;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            luk += 5;
    }
    if (luk < 0)
        luk = 0;
    return luk;
}

/*==========================================
 * �Ώۂ�Flee��Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_flee (struct block_list *bl)
{
    int  flee = 1;
    struct status_change *sc_data;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        flee = ((struct map_session_data *) bl)->flee;
    else
        flee = battle_get_agi (bl) + battle_get_lv (bl);

    if (sc_data)
    {
        if (sc_data[SC_WHISTLE].timer != -1 && bl->type != BL_PC)
            flee +=
                flee * (sc_data[SC_WHISTLE].val1 + sc_data[SC_WHISTLE].val2 +
                        (sc_data[SC_WHISTLE].val3 >> 16)) / 100;
        if (sc_data[SC_BLIND].timer != -1 && bl->type != BL_PC)
            flee -= flee * 25 / 100;
        if (sc_data[SC_WINDWALK].timer != -1 && bl->type != BL_PC)  // �E�B���h�E�H�[�N
            flee += flee * (sc_data[SC_WINDWALK].val2) / 100;
        if (sc_data[SC_SPIDERWEB].timer != -1 && bl->type != BL_PC) //�X�p�C�_�[�E�F�u
            flee -= flee * 50 / 100;

        if (battle_is_unarmed (bl))
            flee += (skill_power_bl (bl, TMW_BRAWLING) >> 3);   // +25 for 200
        flee += skill_power_bl (bl, TMW_SPEED) >> 3;
    }
    if (flee < 1)
        flee = 1;
    return flee;
}

/*==========================================
 * �Ώۂ�Hit��Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_hit (struct block_list *bl)
{
    int  hit = 1;
    struct status_change *sc_data;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        hit = ((struct map_session_data *) bl)->hit;
    else
        hit = battle_get_dex (bl) + battle_get_lv (bl);

    if (sc_data)
    {
        if (sc_data[SC_HUMMING].timer != -1 && bl->type != BL_PC)   //
            hit +=
                hit * (sc_data[SC_HUMMING].val1 * 2 +
                       sc_data[SC_HUMMING].val2 +
                       sc_data[SC_HUMMING].val3) / 100;
        if (sc_data[SC_BLIND].timer != -1 && bl->type != BL_PC) // ��
            hit -= hit * 25 / 100;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) // �g�D���[�T�C�g
            hit += 3 * (sc_data[SC_TRUESIGHT].val1);
        if (sc_data[SC_CONCENTRATION].timer != -1 && bl->type != BL_PC) //�R���Z���g���[�V����
            hit += (hit * (10 * (sc_data[SC_CONCENTRATION].val1))) / 100;

        if (battle_is_unarmed (bl))
            hit += (skill_power_bl (bl, TMW_BRAWLING) >> 4);    // +12 for 200
    }
    if (hit < 1)
        hit = 1;
    return hit;
}

/*==========================================
 * �Ώۂ̊��S�����Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_flee2 (struct block_list *bl)
{
    int  flee2 = 1;
    struct status_change *sc_data;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        flee2 = battle_get_luk (bl) + 10;
        flee2 +=
            ((struct map_session_data *) bl)->flee2 -
            (((struct map_session_data *) bl)->paramc[5] + 10);
    }
    else
        flee2 = battle_get_luk (bl) + 1;

    if (sc_data)
    {
        if (sc_data[SC_WHISTLE].timer != -1 && bl->type != BL_PC)
            flee2 += (sc_data[SC_WHISTLE].val1 + sc_data[SC_WHISTLE].val2
                      + (sc_data[SC_WHISTLE].val3 & 0xffff)) * 10;

        if (battle_is_unarmed (bl))
            flee2 += (skill_power_bl (bl, TMW_BRAWLING) >> 3);  // +25 for 200
        flee2 += skill_power_bl (bl, TMW_SPEED) >> 3;
    }
    if (flee2 < 1)
        flee2 = 1;
    return flee2;
}

/*==========================================
 * �Ώۂ̃N���e�B�J����Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_critical (struct block_list *bl)
{
    int  critical = 1;
    struct status_change *sc_data;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        critical = battle_get_luk (bl) * 2 + 10;
        critical +=
            ((struct map_session_data *) bl)->critical -
            ((((struct map_session_data *) bl)->paramc[5] * 3) + 10);
    }
    else
        critical = battle_get_luk (bl) * 3 + 1;

    if (sc_data)
    {
        if (sc_data[SC_FORTUNE].timer != -1 && bl->type != BL_PC)
            critical +=
                (10 + sc_data[SC_FORTUNE].val1 + sc_data[SC_FORTUNE].val2 +
                 sc_data[SC_FORTUNE].val3) * 10;
        if (sc_data[SC_EXPLOSIONSPIRITS].timer != -1 && bl->type != BL_PC)
            critical += sc_data[SC_EXPLOSIONSPIRITS].val2;
        if (sc_data[SC_TRUESIGHT].timer != -1 && bl->type != BL_PC) //�g�D���[�T�C�g
            critical += critical * sc_data[SC_TRUESIGHT].val1 / 100;
    }
    if (critical < 1)
        critical = 1;
    return critical;
}

/*==========================================
 * base_atk�̎擾
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_baseatk (struct block_list *bl)
{
    struct status_change *sc_data;
    int  batk = 1;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        batk = ((struct map_session_data *) bl)->base_atk;  //�ݒ肳��Ă���base_atk
    else
    {                           //����ȊO�Ȃ�
        int  str, dstr;
        str = battle_get_str (bl);  //STR
        dstr = str / 10;
        batk = dstr * dstr + str;   //base_atk���v�Z����
    }
    if (sc_data)
    {                           //��Ԉُ킠��
        if (sc_data[SC_PROVOKE].timer != -1 && bl->type != BL_PC)   //PC�Ńv���{�b�N(SM_PROVOKE)���
            batk = batk * (100 + 2 * sc_data[SC_PROVOKE].val1) / 100;   //base_atk����
        if (sc_data[SC_CURSE].timer != -1)  //����Ă�����
            batk -= batk * 25 / 100;    //base_atk��25%����
        if (sc_data[SC_CONCENTRATION].timer != -1 && bl->type != BL_PC) //�R���Z���g���[�V����
            batk += batk * (5 * sc_data[SC_CONCENTRATION].val1) / 100;
    }
    if (batk < 1)
        batk = 1;               //base_atk�͍Œ�ł�1
    return batk;
}

/*==========================================
 * �Ώۂ�Atk��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_atk (struct block_list *bl)
{
    struct status_change *sc_data;
    int  atk = 0;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        atk = ((struct map_session_data *) bl)->watk;
    else if (bl->type == BL_MOB && (struct mob_data *) bl)
        atk = ((struct mob_data *) bl)->stats[MOB_ATK1];

    if (sc_data)
    {
        if (sc_data[SC_PROVOKE].timer != -1 && bl->type != BL_PC)
            atk = atk * (100 + 2 * sc_data[SC_PROVOKE].val1) / 100;
        if (sc_data[SC_CURSE].timer != -1)
            atk -= atk * 25 / 100;
        if (sc_data[SC_CONCENTRATION].timer != -1 && bl->type != BL_PC) //�R���Z���g���[�V����
            atk += atk * (5 * sc_data[SC_CONCENTRATION].val1) / 100;
    }
    if (atk < 0)
        atk = 0;
    return atk;
}

/*==========================================
 * �Ώۂ̍���Atk��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_atk_ (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        int  atk = ((struct map_session_data *) bl)->watk_;

        if (((struct map_session_data *) bl)->sc_data[SC_CURSE].timer != -1)
            atk -= atk * 25 / 100;
        return atk;
    }
    else
        return 0;
}

/*==========================================
 * �Ώۂ�Atk2��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_atk2 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->watk2;
    else
    {
        struct status_change *sc_data = battle_get_sc_data (bl);
        int  atk2 = 0;
        if (bl->type == BL_MOB && (struct mob_data *) bl)
            atk2 = ((struct mob_data *) bl)->stats[MOB_ATK2];
        if (sc_data)
        {
            if (sc_data[SC_IMPOSITIO].timer != -1)
                atk2 += sc_data[SC_IMPOSITIO].val1 * 5;
            if (sc_data[SC_PROVOKE].timer != -1)
                atk2 = atk2 * (100 + 2 * sc_data[SC_PROVOKE].val1) / 100;
            if (sc_data[SC_CURSE].timer != -1)
                atk2 -= atk2 * 25 / 100;
            if (sc_data[SC_DRUMBATTLE].timer != -1)
                atk2 += sc_data[SC_DRUMBATTLE].val2;
            if (sc_data[SC_NIBELUNGEN].timer != -1
                && (battle_get_element (bl) / 10) >= 8)
                atk2 += sc_data[SC_NIBELUNGEN].val2;
            if (sc_data[SC_STRIPWEAPON].timer != -1)
                atk2 = atk2 * 90 / 100;
            if (sc_data[SC_CONCENTRATION].timer != -1)  //�R���Z���g���[�V����
                atk2 += atk2 * (5 * sc_data[SC_CONCENTRATION].val1) / 100;
        }

        if (atk2 < 0)
            atk2 = 0;
        return atk2;
    }
    return 0;
}

/*==========================================
 * �Ώۂ̍���Atk2��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_atk_2 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC)
        return ((struct map_session_data *) bl)->watk_2;
    else
        return 0;
}

/*==========================================
 * �Ώۂ�MAtk1��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_matk1 (struct block_list *bl)
{
    struct status_change *sc_data;
    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB)
    {
        int  matk, int_ = battle_get_int (bl);
        matk = int_ + (int_ / 5) * (int_ / 5);

        if (sc_data)
            if (sc_data[SC_MINDBREAKER].timer != -1 && bl->type != BL_PC)
                matk = matk * (100 + 2 * sc_data[SC_MINDBREAKER].val1) / 100;
        return matk;
    }
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->matk1;
    else
        return 0;
}

/*==========================================
 * �Ώۂ�MAtk2��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_matk2 (struct block_list *bl)
{
    struct status_change *sc_data = battle_get_sc_data (bl);
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB)
    {
        int  matk, int_ = battle_get_int (bl);
        matk = int_ + (int_ / 7) * (int_ / 7);

        if (sc_data)
            if (sc_data[SC_MINDBREAKER].timer != -1 && bl->type != BL_PC)
                matk = matk * (100 + 2 * sc_data[SC_MINDBREAKER].val1) / 100;
        return matk;
    }
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->matk2;
    else
        return 0;
}

/*==========================================
 * �Ώۂ�Def��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_def (struct block_list *bl)
{
    struct status_change *sc_data;
    int  def = 0, skilltimer = -1, skillid = 0;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        def = ((struct map_session_data *) bl)->def;
        skilltimer = ((struct map_session_data *) bl)->skilltimer;
        skillid = ((struct map_session_data *) bl)->skillid;
    }
    else if (bl->type == BL_MOB && (struct mob_data *) bl)
    {
        def = ((struct mob_data *) bl)->stats[MOB_DEF];
        skilltimer = ((struct mob_data *) bl)->skilltimer;
        skillid = ((struct mob_data *) bl)->skillid;
    }

    if (def < 1000000)
    {
        if (sc_data)
        {
            //�L�[�s���O����DEF100
            if (sc_data[SC_KEEPING].timer != -1)
                def = 100;
            //�v���{�b�N���͌��Z
            if (sc_data[SC_PROVOKE].timer != -1 && bl->type != BL_PC)
                def = (def * (100 - 6 * sc_data[SC_PROVOKE].val1) + 50) / 100;
            //�푾�ۂ̋������͉��Z
            if (sc_data[SC_DRUMBATTLE].timer != -1 && bl->type != BL_PC)
                def += sc_data[SC_DRUMBATTLE].val3;
            //�łɂ������Ă��鎞�͌��Z
            if (sc_data[SC_POISON].timer != -1 && bl->type != BL_PC)
                def = def * 75 / 100;
            //�X�g���b�v�V�[���h���͌��Z
            if (sc_data[SC_STRIPSHIELD].timer != -1 && bl->type != BL_PC)
                def = def * 85 / 100;
            //�V�O�i���N���V�X���͌��Z
            if (sc_data[SC_SIGNUMCRUCIS].timer != -1 && bl->type != BL_PC)
                def = def * (100 - sc_data[SC_SIGNUMCRUCIS].val2) / 100;
            //�i���̍��׎���DEF0�ɂȂ�
            if (sc_data[SC_ETERNALCHAOS].timer != -1 && bl->type != BL_PC)
                def = 0;
            //�����A�Ή����͉E�V�t�g
            if (sc_data[SC_FREEZE].timer != -1
                || (sc_data[SC_STONE].timer != -1
                    && sc_data[SC_STONE].val2 == 0))
                def >>= 1;
            //�R���Z���g���[�V�������͌��Z
            if (sc_data[SC_CONCENTRATION].timer != -1 && bl->type != BL_PC)
                def =
                    (def * (100 - 5 * sc_data[SC_CONCENTRATION].val1)) / 100;
        }
        //�r�����͉r�������Z���Ɋ�Â��Č��Z
        if (skilltimer != -1)
        {
            int  def_rate = skill_get_castdef (skillid);
            if (def_rate != 0)
                def = (def * (100 - def_rate)) / 100;
        }
    }
    if (def < 0)
        def = 0;
    return def;
}

/*==========================================
 * �Ώۂ�MDef��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_mdef (struct block_list *bl)
{
    struct status_change *sc_data;
    int  mdef = 0;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        mdef = ((struct map_session_data *) bl)->mdef;
    else if (bl->type == BL_MOB && (struct mob_data *) bl)
        mdef = ((struct mob_data *) bl)->stats[MOB_MDEF];

    if (mdef < 1000000)
    {
        if (sc_data)
        {
            //�o���A�[��Ԏ���MDEF100
            if (mdef < 90 && sc_data[SC_MBARRIER].timer != -1)
            {
                mdef += sc_data[SC_MBARRIER].val1;
                if (mdef > 90)
                    mdef = 90;
            }
            if (sc_data[SC_BARRIER].timer != -1)
                mdef = 100;
            //�����A�Ή�����1.25�{
            if (sc_data[SC_FREEZE].timer != -1
                || (sc_data[SC_STONE].timer != -1
                    && sc_data[SC_STONE].val2 == 0))
                mdef = mdef * 125 / 100;
            if (sc_data[SC_MINDBREAKER].timer != -1 && bl->type != BL_PC)
                mdef -= (mdef * 6 * sc_data[SC_MINDBREAKER].val1) / 100;
        }
    }
    if (mdef < 0)
        mdef = 0;
    return mdef;
}

/*==========================================
 * �Ώۂ�Def2��Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 *------------------------------------------
 */
int battle_get_def2 (struct block_list *bl)
{
    struct status_change *sc_data;
    int  def2 = 1;

    nullpo_retr (1, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_PC)
        def2 = ((struct map_session_data *) bl)->def2;
    else if (bl->type == BL_MOB)
        def2 = ((struct mob_data *) bl)->stats[MOB_VIT];

    if (sc_data)
    {
        if (sc_data[SC_ANGELUS].timer != -1 && bl->type != BL_PC)
            def2 = def2 * (110 + 5 * sc_data[SC_ANGELUS].val1) / 100;
        if (sc_data[SC_PROVOKE].timer != -1 && bl->type != BL_PC)
            def2 = (def2 * (100 - 6 * sc_data[SC_PROVOKE].val1) + 50) / 100;
        if (sc_data[SC_POISON].timer != -1 && bl->type != BL_PC)
            def2 = def2 * 75 / 100;
        //�R���Z���g���[�V�������͌��Z
        if (sc_data[SC_CONCENTRATION].timer != -1 && bl->type != BL_PC)
            def2 = def2 * (100 - 5 * sc_data[SC_CONCENTRATION].val1) / 100;
    }
    if (def2 < 1)
        def2 = 1;
    return def2;
}

/*==========================================
 * �Ώۂ�MDef2��Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
int battle_get_mdef2 (struct block_list *bl)
{
    int  mdef2 = 0;
    struct status_change *sc_data = battle_get_sc_data (bl);

    nullpo_retr (0, bl);
    if (bl->type == BL_MOB)
        mdef2 =
            ((struct mob_data *) bl)->stats[MOB_INT] +
            (((struct mob_data *) bl)->stats[MOB_VIT] >> 1);
    else if (bl->type == BL_PC)
        mdef2 =
            ((struct map_session_data *) bl)->mdef2 +
            (((struct map_session_data *) bl)->paramc[2] >> 1);
    if (sc_data)
    {
        if (sc_data[SC_MINDBREAKER].timer != -1 && bl->type != BL_PC)
            mdef2 -= (mdef2 * 6 * sc_data[SC_MINDBREAKER].val1) / 100;
    }
    if (mdef2 < 0)
        mdef2 = 0;
    return mdef2;
}

/*==========================================
 * �Ώۂ�Speed(�ړ����x)��Ԃ�(�ėp)
 * �߂�͐�����1�ȏ�
 * Speed�͏������ق����ړ����x������
 *------------------------------------------
 */
int battle_get_speed (struct block_list *bl)
{
    nullpo_retr (1000, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->speed;
    else
    {
        struct status_change *sc_data = battle_get_sc_data (bl);
        int  speed = 1000;
        if (bl->type == BL_MOB && (struct mob_data *) bl)
            speed = ((struct mob_data *) bl)->stats[MOB_SPEED];

        if (sc_data)
        {
            //���x��������25%���Z
            if (sc_data[SC_INCREASEAGI].timer != -1
                && sc_data[SC_DONTFORGETME].timer == -1)
                speed -= speed * 25 / 100;
            //���x��������25%���Z
            if (sc_data[SC_DECREASEAGI].timer != -1)
                speed = speed * 125 / 100;
            //�N�@�O�}�C�A����50%���Z
            if (sc_data[SC_QUAGMIRE].timer != -1)
                speed = speed * 3 / 2;
            //����Y��Ȃ��Łc���͉��Z
            if (sc_data[SC_DONTFORGETME].timer != -1)
                speed =
                    speed * (100 + sc_data[SC_DONTFORGETME].val1 * 2 +
                             sc_data[SC_DONTFORGETME].val2 +
                             (sc_data[SC_DONTFORGETME].val3 & 0xffff)) / 100;
            //��������25%���Z
            if (sc_data[SC_STEELBODY].timer != -1)
                speed = speed * 125 / 100;
            //�f�B�t�F���_�[���͉��Z
            if (sc_data[SC_DEFENDER].timer != -1)
                speed = (speed * (155 - sc_data[SC_DEFENDER].val1 * 5)) / 100;
            //�x���Ԃ�4�{�x��
            if (sc_data[SC_DANCING].timer != -1)
                speed *= 4;
            //�􂢎���450���Z
            if (sc_data[SC_CURSE].timer != -1)
                speed = speed + 450;
            //�E�B���h�E�H�[�N����Lv*2%���Z
            if (sc_data[SC_WINDWALK].timer != -1)
                speed -= (speed * (sc_data[SC_WINDWALK].val1 * 2)) / 100;
        }
        if (speed < 1)
            speed = 1;
        return speed;
    }

    return 1000;
}

/*==========================================
 * �Ώۂ�aDelay(�U�����f�B���C)��Ԃ�(�ėp)
 * aDelay�͏������ق����U�����x������
 *------------------------------------------
 */
int battle_get_adelay (struct block_list *bl)
{
    nullpo_retr (4000, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return (((struct map_session_data *) bl)->aspd << 1);
    else
    {
        struct status_change *sc_data = battle_get_sc_data (bl);
        int  adelay = 4000, aspd_rate = 100, i;
        if (bl->type == BL_MOB && (struct mob_data *) bl)
            adelay = ((struct mob_data *) bl)->stats[MOB_ADELAY];

        if (sc_data)
        {
            //�c�[�n���h�N�C�b�P���g�p���ŃN�@�O�}�C�A�ł�����Y��Ȃ��Łc�ł��Ȃ�����3�����Z
            if (sc_data[SC_TWOHANDQUICKEN].timer != -1 && sc_data[SC_QUAGMIRE].timer == -1 && sc_data[SC_DONTFORGETME].timer == -1) // 2HQ
                aspd_rate -= 30;
            //�A�h���i�������b�V���g�p���Ńc�[�n���h�N�C�b�P���ł��N�@�O�}�C�A�ł�����Y��Ȃ��Łc�ł��Ȃ�����
            if (sc_data[SC_ADRENALINE].timer != -1
                && sc_data[SC_TWOHANDQUICKEN].timer == -1
                && sc_data[SC_QUAGMIRE].timer == -1
                && sc_data[SC_DONTFORGETME].timer == -1)
            {                   // �A�h���i�������b�V��
                //�g�p�҂ƃp�[�e�B�����o�[�Ŋi�����o��ݒ�łȂ����3�����Z
                if (sc_data[SC_ADRENALINE].val2
                    || !battle_config.party_skill_penaly)
                    aspd_rate -= 30;
                //�����łȂ����2.5�����Z
                else
                    aspd_rate -= 25;
            }
            //�X�s�A�N�B�b�P�����͌��Z
            if (sc_data[SC_SPEARSQUICKEN].timer != -1 && sc_data[SC_ADRENALINE].timer == -1 && sc_data[SC_TWOHANDQUICKEN].timer == -1 && sc_data[SC_QUAGMIRE].timer == -1 && sc_data[SC_DONTFORGETME].timer == -1)  // �X�s�A�N�B�b�P��
                aspd_rate -= sc_data[SC_SPEARSQUICKEN].val2;
            //�[���̃A�T�V���N���X���͌��Z
            if (sc_data[SC_ASSNCROS].timer != -1 && // �[�z�̃A�T�V���N���X
                sc_data[SC_TWOHANDQUICKEN].timer == -1
                && sc_data[SC_ADRENALINE].timer == -1
                && sc_data[SC_SPEARSQUICKEN].timer == -1
                && sc_data[SC_DONTFORGETME].timer == -1)
                aspd_rate -=
                    5 + sc_data[SC_ASSNCROS].val1 +
                    sc_data[SC_ASSNCROS].val2 + sc_data[SC_ASSNCROS].val3;
            //����Y��Ȃ��Łc���͉��Z
            if (sc_data[SC_DONTFORGETME].timer != -1)   // ����Y��Ȃ���
                aspd_rate +=
                    sc_data[SC_DONTFORGETME].val1 * 3 +
                    sc_data[SC_DONTFORGETME].val2 +
                    (sc_data[SC_DONTFORGETME].val3 >> 16);
            //������25%���Z
            if (sc_data[SC_STEELBODY].timer != -1)  // ����
                aspd_rate += 25;
            //�����|�[�V�����g�p���͌��Z
            if (sc_data[i = SC_SPEEDPOTION2].timer != -1
                || sc_data[i = SC_SPEEDPOTION1].timer != -1
                || sc_data[i = SC_SPEEDPOTION0].timer != -1)
                aspd_rate -= sc_data[i].val1;
            // Fate's `haste' spell works the same as the above
            if (sc_data[SC_HASTE].timer != -1)
                aspd_rate -= sc_data[SC_HASTE].val1;
            //�f�B�t�F���_�[���͉��Z
            if (sc_data[SC_DEFENDER].timer != -1)
                adelay += (1100 - sc_data[SC_DEFENDER].val1 * 100);
        }

        if (aspd_rate != 100)
            adelay = adelay * aspd_rate / 100;
        if (adelay < battle_config.monster_max_aspd << 1)
            adelay = battle_config.monster_max_aspd << 1;
        return adelay;
    }
    return 4000;
}

int battle_get_amotion (struct block_list *bl)
{
    nullpo_retr (2000, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->amotion;
    else
    {
        struct status_change *sc_data = battle_get_sc_data (bl);
        int  amotion = 2000, aspd_rate = 100, i;
        if (bl->type == BL_MOB && (struct mob_data *) bl)
            amotion = mob_db[((struct mob_data *) bl)->class].amotion;

        if (sc_data)
        {
            if (sc_data[SC_TWOHANDQUICKEN].timer != -1 && sc_data[SC_QUAGMIRE].timer == -1 && sc_data[SC_DONTFORGETME].timer == -1) // 2HQ
                aspd_rate -= 30;
            if (sc_data[SC_ADRENALINE].timer != -1
                && sc_data[SC_TWOHANDQUICKEN].timer == -1
                && sc_data[SC_QUAGMIRE].timer == -1
                && sc_data[SC_DONTFORGETME].timer == -1)
            {                   // �A�h���i�������b�V��
                if (sc_data[SC_ADRENALINE].val2
                    || !battle_config.party_skill_penaly)
                    aspd_rate -= 30;
                else
                    aspd_rate -= 25;
            }
            if (sc_data[SC_SPEARSQUICKEN].timer != -1 && sc_data[SC_ADRENALINE].timer == -1 && sc_data[SC_TWOHANDQUICKEN].timer == -1 && sc_data[SC_QUAGMIRE].timer == -1 && sc_data[SC_DONTFORGETME].timer == -1)  // �X�s�A�N�B�b�P��
                aspd_rate -= sc_data[SC_SPEARSQUICKEN].val2;
            if (sc_data[SC_ASSNCROS].timer != -1 && // �[�z�̃A�T�V���N���X
                sc_data[SC_TWOHANDQUICKEN].timer == -1
                && sc_data[SC_ADRENALINE].timer == -1
                && sc_data[SC_SPEARSQUICKEN].timer == -1
                && sc_data[SC_DONTFORGETME].timer == -1)
                aspd_rate -=
                    5 + sc_data[SC_ASSNCROS].val1 +
                    sc_data[SC_ASSNCROS].val2 + sc_data[SC_ASSNCROS].val3;
            if (sc_data[SC_DONTFORGETME].timer != -1)   // ����Y��Ȃ���
                aspd_rate +=
                    sc_data[SC_DONTFORGETME].val1 * 3 +
                    sc_data[SC_DONTFORGETME].val2 +
                    (sc_data[SC_DONTFORGETME].val3 >> 16);
            if (sc_data[SC_STEELBODY].timer != -1)  // ����
                aspd_rate += 25;
            if (sc_data[i = SC_SPEEDPOTION2].timer != -1
                || sc_data[i = SC_SPEEDPOTION1].timer != -1
                || sc_data[i = SC_SPEEDPOTION0].timer != -1)
                aspd_rate -= sc_data[i].val1;
            if (sc_data[SC_HASTE].timer != -1)
                aspd_rate -= sc_data[SC_HASTE].val1;
            if (sc_data[SC_DEFENDER].timer != -1)
                amotion += (550 - sc_data[SC_DEFENDER].val1 * 50);
        }

        if (aspd_rate != 100)
            amotion = amotion * aspd_rate / 100;
        if (amotion < battle_config.monster_max_aspd)
            amotion = battle_config.monster_max_aspd;
        return amotion;
    }
    return 2000;
}

int battle_get_dmotion (struct block_list *bl)
{
    int  ret;
    struct status_change *sc_data;

    nullpo_retr (0, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
    {
        ret = mob_db[((struct mob_data *) bl)->class].dmotion;
        if (battle_config.monster_damage_delay_rate != 100)
            ret = ret * battle_config.monster_damage_delay_rate / 400;
    }
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        ret = ((struct map_session_data *) bl)->dmotion;
        if (battle_config.pc_damage_delay_rate != 100)
            ret = ret * battle_config.pc_damage_delay_rate / 400;
    }
    else
        return 2000;

    if ((sc_data && sc_data[SC_ENDURE].timer != -1) ||
        (bl->type == BL_PC
         && ((struct map_session_data *) bl)->special_state.infinite_endure))
        ret = 0;

    return ret;
}

int battle_get_element (struct block_list *bl)
{
    int  ret = 20;
    struct status_change *sc_data;

    nullpo_retr (ret, bl);
    sc_data = battle_get_sc_data (bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)   // 10�̈ʁ�Lv*2�A�P�̈ʁ�����
        ret = ((struct mob_data *) bl)->def_ele;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        ret = 20 + ((struct map_session_data *) bl)->def_ele;   // �h�䑮��Lv1

    if (sc_data)
    {
        if (sc_data[SC_BENEDICTIO].timer != -1) // ���̍~��
            ret = 26;
        if (sc_data[SC_FREEZE].timer != -1) // ����
            ret = 21;
        if (sc_data[SC_STONE].timer != -1 && sc_data[SC_STONE].val2 == 0)
            ret = 22;
    }

    return ret;
}

int battle_get_attack_element (struct block_list *bl)
{
    int  ret = 0;
    struct status_change *sc_data = battle_get_sc_data (bl);

    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        ret = 0;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        ret = ((struct map_session_data *) bl)->atk_ele;

    if (sc_data)
    {
        if (sc_data[SC_FROSTWEAPON].timer != -1)    // �t���X�g�E�F�|��
            ret = 1;
        if (sc_data[SC_SEISMICWEAPON].timer != -1)  // �T�C�Y�~�b�N�E�F�|��
            ret = 2;
        if (sc_data[SC_FLAMELAUNCHER].timer != -1)  // �t���[�������`���[
            ret = 3;
        if (sc_data[SC_LIGHTNINGLOADER].timer != -1)    // ���C�g�j���O���[�_�[
            ret = 4;
        if (sc_data[SC_ENCPOISON].timer != -1)  // �G���`�����g�|�C�Y��
            ret = 5;
        if (sc_data[SC_ASPERSIO].timer != -1)   // �A�X�y���V�I
            ret = 6;
    }

    return ret;
}

int battle_get_attack_element2 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
    {
        int  ret = ((struct map_session_data *) bl)->atk_ele_;
        struct status_change *sc_data =
            ((struct map_session_data *) bl)->sc_data;

        if (sc_data)
        {
            if (sc_data[SC_FROSTWEAPON].timer != -1)    // �t���X�g�E�F�|��
                ret = 1;
            if (sc_data[SC_SEISMICWEAPON].timer != -1)  // �T�C�Y�~�b�N�E�F�|��
                ret = 2;
            if (sc_data[SC_FLAMELAUNCHER].timer != -1)  // �t���[�������`���[
                ret = 3;
            if (sc_data[SC_LIGHTNINGLOADER].timer != -1)    // ���C�g�j���O���[�_�[
                ret = 4;
            if (sc_data[SC_ENCPOISON].timer != -1)  // �G���`�����g�|�C�Y��
                ret = 5;
            if (sc_data[SC_ASPERSIO].timer != -1)   // �A�X�y���V�I
                ret = 6;
        }
        return ret;
    }
    return 0;
}

int battle_get_party_id (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->status.party_id;
    else if (bl->type == BL_MOB && (struct mob_data *) bl)
    {
        struct mob_data *md = (struct mob_data *) bl;
        if (md->master_id > 0)
            return -md->master_id;
        return -md->bl.id;
    }
    else if (bl->type == BL_SKILL && (struct skill_unit *) bl)
        return ((struct skill_unit *) bl)->group->party_id;
    else
        return 0;
}

int battle_get_guild_id (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->status.guild_id;
    else if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->class;
    else if (bl->type == BL_SKILL && (struct skill_unit *) bl)
        return ((struct skill_unit *) bl)->group->guild_id;
    else
        return 0;
}

int battle_get_race (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return mob_db[((struct mob_data *) bl)->class].race;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return 7;
    else
        return 0;
}

int battle_get_size (struct block_list *bl)
{
    nullpo_retr (1, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return mob_db[((struct mob_data *) bl)->class].size;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return 1;
    else
        return 1;
}

int battle_get_mode (struct block_list *bl)
{
    nullpo_retr (0x01, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return mob_db[((struct mob_data *) bl)->class].mode;
    else
        return 0x01;            // �Ƃ肠���������Ƃ������Ƃ�1
}

int battle_get_mexp (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
    {
        const struct mob_data *mob = (struct mob_data *) bl;
        const int retval =
            (mob_db[mob->class].mexp *
             (int) (mob->stats[MOB_XP_BONUS])) >> MOB_XP_BONUS_SHIFT;
        fprintf (stderr, "Modifier of %x: -> %d\n", mob->stats[MOB_XP_BONUS],
                 retval);
        return retval;
    }
    else
        return 0;
}

int battle_get_stat (int stat_id /* SP_VIT or similar */ ,
                     struct block_list *bl)
{
    switch (stat_id)
    {
        case SP_STR:
            return battle_get_str (bl);
        case SP_AGI:
            return battle_get_agi (bl);
        case SP_DEX:
            return battle_get_dex (bl);
        case SP_VIT:
            return battle_get_vit (bl);
        case SP_INT:
            return battle_get_int (bl);
        case SP_LUK:
            return battle_get_luk (bl);
        default:
            return 0;
    }
}

// StatusChange�n�̏���
struct status_change *battle_get_sc_data (struct block_list *bl)
{
    nullpo_retr (NULL, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return ((struct mob_data *) bl)->sc_data;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return ((struct map_session_data *) bl)->sc_data;
    return NULL;
}

short *battle_get_sc_count (struct block_list *bl)
{
    nullpo_retr (NULL, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return &((struct mob_data *) bl)->sc_count;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return &((struct map_session_data *) bl)->sc_count;
    return NULL;
}

short *battle_get_opt1 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return &((struct mob_data *) bl)->opt1;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return &((struct map_session_data *) bl)->opt1;
    else if (bl->type == BL_NPC && (struct npc_data *) bl)
        return &((struct npc_data *) bl)->opt1;
    return 0;
}

short *battle_get_opt2 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return &((struct mob_data *) bl)->opt2;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return &((struct map_session_data *) bl)->opt2;
    else if (bl->type == BL_NPC && (struct npc_data *) bl)
        return &((struct npc_data *) bl)->opt2;
    return 0;
}

short *battle_get_opt3 (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return &((struct mob_data *) bl)->opt3;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return &((struct map_session_data *) bl)->opt3;
    else if (bl->type == BL_NPC && (struct npc_data *) bl)
        return &((struct npc_data *) bl)->opt3;
    return 0;
}

short *battle_get_option (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB && (struct mob_data *) bl)
        return &((struct mob_data *) bl)->option;
    else if (bl->type == BL_PC && (struct map_session_data *) bl)
        return &((struct map_session_data *) bl)->status.option;
    else if (bl->type == BL_NPC && (struct npc_data *) bl)
        return &((struct npc_data *) bl)->option;
    return 0;
}

//-------------------------------------------------------------------

// �_���[�W�̒x��
struct battle_delay_damage_
{
    struct block_list *src, *target;
    int  damage;
    int  flag;
};
int battle_delay_damage_sub (int tid, unsigned int tick, int id, int data)
{
    struct battle_delay_damage_ *dat = (struct battle_delay_damage_ *) data;
    if (dat && map_id2bl (id) == dat->src && dat->target->prev != NULL)
        battle_damage (dat->src, dat->target, dat->damage, dat->flag);
    free (dat);
    return 0;
}

int battle_delay_damage (unsigned int tick, struct block_list *src,
                         struct block_list *target, int damage, int flag)
{
    struct battle_delay_damage_ *dat =
        (struct battle_delay_damage_ *) aCalloc (1,
                                                 sizeof (struct
                                                         battle_delay_damage_));

    nullpo_retr (0, src);
    nullpo_retr (0, target);

    dat->src = src;
    dat->target = target;
    dat->damage = damage;
    dat->flag = flag;
    add_timer (tick, battle_delay_damage_sub, src->id, (int) dat);
    return 0;
}

// ���ۂ�HP�𑀍�
int battle_damage (struct block_list *bl, struct block_list *target,
                   int damage, int flag)
{
    struct map_session_data *sd = NULL;
    struct status_change *sc_data = battle_get_sc_data (target);
    short *sc_count;
    int  i;

    nullpo_retr (0, target);    //bl��NULL�ŌĂ΂�邱�Ƃ�����̂ő��Ń`�F�b�N

    if (damage == 0)
        return 0;

    if (target->prev == NULL)
        return 0;

    if (bl)
    {
        if (bl->prev == NULL)
            return 0;

        if (bl->type == BL_PC)
            sd = (struct map_session_data *) bl;
    }

    if (damage < 0)
        return battle_heal (bl, target, -damage, 0, flag);

    if (!flag && (sc_count = battle_get_sc_count (target)) != NULL
        && *sc_count > 0)
    {
        // �����A�Ή��A����������
        if (sc_data[SC_FREEZE].timer != -1)
            skill_status_change_end (target, SC_FREEZE, -1);
        if (sc_data[SC_STONE].timer != -1 && sc_data[SC_STONE].val2 == 0)
            skill_status_change_end (target, SC_STONE, -1);
        if (sc_data[SC_SLEEP].timer != -1)
            skill_status_change_end (target, SC_SLEEP, -1);
    }

    if (target->type == BL_MOB)
    {                           // MOB
        struct mob_data *md = (struct mob_data *) target;
        if (md && md->skilltimer != -1 && md->state.skillcastcancel)    // �r���W�Q
            skill_castcancel (target, 0);
        return mob_damage (bl, md, damage, 0);
    }
    else if (target->type == BL_PC)
    {                           // PC

        struct map_session_data *tsd = (struct map_session_data *) target;

        if (tsd && tsd->sc_data && tsd->sc_data[SC_DEVOTION].val1)
        {                       // �f�B�{�[�V�������������Ă���
            struct map_session_data *md =
                map_id2sd (tsd->sc_data[SC_DEVOTION].val1);
            if (md && skill_devotion3 (&md->bl, target->id))
            {
                skill_devotion (md, target->id);
            }
            else if (md && bl)
                for (i = 0; i < 5; i++)
                    if (md->dev.val1[i] == target->id)
                    {
                        clif_damage (bl, &md->bl, gettick (), 0, 0,
                                     damage, 0, 0, 0);
                        pc_damage (&md->bl, md, damage);

                        return 0;
                    }
        }

        if (tsd && tsd->skilltimer != -1)
        {                       // �r���W�Q
            // �t�F���J�[�h��W�Q����Ȃ��X�L�����̌���
            if ((!tsd->special_state.no_castcancel || map[bl->m].flag.gvg)
                && tsd->state.skillcastcancel
                && !tsd->special_state.no_castcancel2)
                skill_castcancel (target, 0);
        }

        return pc_damage (bl, tsd, damage);

    }
    else if (target->type == BL_SKILL)
        return skill_unit_ondamaged ((struct skill_unit *) target, bl, damage,
                                     gettick ());
    return 0;
}

int battle_heal (struct block_list *bl, struct block_list *target, int hp,
                 int sp, int flag)
{
    nullpo_retr (0, target);    //bl��NULL�ŌĂ΂�邱�Ƃ�����̂ő��Ń`�F�b�N

    if (target->type == BL_PC
        && pc_isdead ((struct map_session_data *) target))
        return 0;
    if (hp == 0 && sp == 0)
        return 0;

    if (hp < 0)
        return battle_damage (bl, target, -hp, flag);

    if (target->type == BL_MOB)
        return mob_heal ((struct mob_data *) target, hp);
    else if (target->type == BL_PC)
        return pc_heal ((struct map_session_data *) target, hp, sp);
    return 0;
}

// �U����~
int battle_stopattack (struct block_list *bl)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB)
        return mob_stopattack ((struct mob_data *) bl);
    else if (bl->type == BL_PC)
        return pc_stopattack ((struct map_session_data *) bl);
    return 0;
}

// �ړ���~
int battle_stopwalking (struct block_list *bl, int type)
{
    nullpo_retr (0, bl);
    if (bl->type == BL_MOB)
        return mob_stop_walking ((struct mob_data *) bl, type);
    else if (bl->type == BL_PC)
        return pc_stop_walking ((struct map_session_data *) bl, type);
    return 0;
}

/*==========================================
 * �_���[�W�̑����C��
 *------------------------------------------
 */
int battle_attr_fix (int damage, int atk_elem, int def_elem)
{
    int  def_type = def_elem % 10, def_lv = def_elem / 10 / 2;

    if (atk_elem < 0 || atk_elem > 9 || def_type < 0 || def_type > 9 ||
        def_lv < 1 || def_lv > 4)
    {                           // �� ���l�����������̂łƂ肠�������̂܂ܕԂ�
        if (battle_config.error_log)
            printf
                ("battle_attr_fix: unknown attr type: atk=%d def_type=%d def_lv=%d\n",
                 atk_elem, def_type, def_lv);
        return damage;
    }

    return damage * attr_fix_table[def_lv - 1][atk_elem][def_type] / 100;
}

/*==========================================
 * �_���[�W�ŏI�v�Z
 *------------------------------------------
 */
int battle_calc_damage (struct block_list *src, struct block_list *bl,
                        int damage, int div_, int skill_num, int skill_lv,
                        int flag)
{
    struct map_session_data *sd = NULL;
    struct mob_data *md = NULL;
    struct status_change *sc_data, *sc;
    short *sc_count;
    int  class;

    nullpo_retr (0, bl);

    class = battle_get_class (bl);
    if (bl->type == BL_MOB)
        md = (struct mob_data *) bl;
    else
        sd = (struct map_session_data *) bl;

    sc_data = battle_get_sc_data (bl);
    sc_count = battle_get_sc_count (bl);

    if (sc_count != NULL && *sc_count > 0)
    {

        if (sc_data[SC_SAFETYWALL].timer != -1 && damage > 0
            && flag & BF_WEAPON && flag & BF_SHORT
            && skill_num != NPC_GUIDEDATTACK)
        {
            // �Z�[�t�e�B�E�H�[��
            struct skill_unit *unit =
                (struct skill_unit *) sc_data[SC_SAFETYWALL].val2;
            if (unit && unit->alive && (--unit->group->val2) <= 0)
                skill_delunit (unit);
            skill_unit_move (bl, gettick (), 1);    // �d�ˊ|���`�F�b�N
            damage = 0;
        }
        if (sc_data[SC_PNEUMA].timer != -1 && damage > 0 && flag & BF_WEAPON
            && flag & BF_LONG && skill_num != NPC_GUIDEDATTACK)
        {
            // �j���[�}
            damage = 0;
        }

        if (sc_data[SC_ROKISWEIL].timer != -1 && damage > 0 &&
            flag & BF_MAGIC)
        {
            // �j���[�}
            damage = 0;
        }

        if (sc_data[SC_AETERNA].timer != -1 && damage > 0)
        {                       // ���b�N�X�G�[�e���i
            damage <<= 1;
            skill_status_change_end (bl, SC_AETERNA, -1);
        }

        //������̃_���[�W����
        if (sc_data[SC_VOLCANO].timer != -1)
        {                       // �{���P�[�m
            if (flag & BF_SKILL && skill_get_pl (skill_num) == 3)
                damage += damage * sc_data[SC_VOLCANO].val4 / 100;
            else if (!(flag & BF_SKILL) && (battle_get_attack_element (bl) == 3))
                damage += damage * sc_data[SC_VOLCANO].val4 / 100;
        }

        if (sc_data[SC_VIOLENTGALE].timer != -1)
        {                       // �o�C�I�����g�Q�C��
            if (flag & BF_SKILL && skill_get_pl (skill_num) == 4)
                damage += damage * sc_data[SC_VIOLENTGALE].val4 / 100;
            else if (!(flag & BF_SKILL) && (battle_get_attack_element (bl) == 4))
                damage += damage * sc_data[SC_VIOLENTGALE].val4 / 100;
        }

        if (sc_data[SC_DELUGE].timer != -1)
        {                       // �f�����[�W
            if (flag & BF_SKILL && skill_get_pl (skill_num) == 1)
                damage += damage * sc_data[SC_DELUGE].val4 / 100;
            else if (!(flag & BF_SKILL) && (battle_get_attack_element (bl) == 1))
                damage += damage * sc_data[SC_DELUGE].val4 / 100;
        }

        if (sc_data[SC_ENERGYCOAT].timer != -1 && damage > 0
            && flag & BF_WEAPON)
        {                       // �G�i�W�[�R�[�g
            if (sd)
            {
                if (sd->status.sp > 0)
                {
                    int  per = sd->status.sp * 5 / (sd->status.max_sp + 1);
                    sd->status.sp -= sd->status.sp * (per * 5 + 10) / 1000;
                    if (sd->status.sp < 0)
                        sd->status.sp = 0;
                    damage -= damage * ((per + 1) * 6) / 100;
                    clif_updatestatus (sd, SP_SP);
                }
                if (sd->status.sp <= 0)
                    skill_status_change_end (bl, SC_ENERGYCOAT, -1);
            }
            else
                damage -= damage * (sc_data[SC_ENERGYCOAT].val1 * 6) / 100;
        }

        if (sc_data[SC_KYRIE].timer != -1 && damage > 0)
        {                       // �L���G�G���C�\��
            sc = &sc_data[SC_KYRIE];
            sc->val2 -= damage;
            if (flag & BF_WEAPON)
            {
                if (sc->val2 >= 0)
                    damage = 0;
                else
                    damage = -sc->val2;
            }
            if ((--sc->val3) <= 0 || (sc->val2 <= 0)
                || skill_num == AL_HOLYLIGHT)
                skill_status_change_end (bl, SC_KYRIE, -1);
        }

        if (sc_data[SC_BASILICA].timer != -1 && damage > 0)
        {
            // �j���[�}
            damage = 0;
        }
        if (sc_data[SC_LANDPROTECTOR].timer != -1 && damage > 0
            && flag & BF_MAGIC)
        {
            // �j���[�}
            damage = 0;
        }

        if (sc_data[SC_AUTOGUARD].timer != -1 && damage > 0
            && flag & BF_WEAPON)
        {
            if (MRAND (100) < sc_data[SC_AUTOGUARD].val2)
            {
                damage = 0;
                clif_skill_nodamage (bl, bl, CR_AUTOGUARD,
                                     sc_data[SC_AUTOGUARD].val1, 1);
                if (sd)
                    sd->canmove_tick = gettick () + 300;
                else if (md)
                    md->canmove_tick = gettick () + 300;
            }
        }
// -- moonsoul (chance to block attacks with new Lord Knight skill parrying)
//
        if (sc_data[SC_PARRYING].timer != -1 && damage > 0
            && flag & BF_WEAPON)
        {
            if (MRAND (100) < sc_data[SC_PARRYING].val2)
            {
                damage = 0;
                clif_skill_nodamage (bl, bl, LK_PARRYING,
                                     sc_data[SC_PARRYING].val1, 1);
            }
        }
        // ���W�F�N�g�\�[�h
        if (sc_data[SC_REJECTSWORD].timer != -1 && damage > 0
            && flag & BF_WEAPON
            &&
            ((src->type == BL_PC
              && ((struct map_session_data *) src)->status.weapon == (1 || 2
                                                                      || 3))
             || src->type == BL_MOB))
        {
            if (MRAND (100) < (10 + 5 * sc_data[SC_REJECTSWORD].val1))
            {                   //���ˊm����10+5*Lv
                damage = damage * 50 / 100;
                battle_damage (bl, src, damage, 0);
                //�_���[�W��^�����̂͗ǂ��񂾂��A��������ǂ����ĕ\������񂾂��킩��˂�
                //�G�t�F�N�g������ł����̂��킩��˂�
                clif_skill_nodamage (bl, bl, ST_REJECTSWORD,
                                     sc_data[SC_REJECTSWORD].val1, 1);
                if ((--sc_data[SC_REJECTSWORD].val2) <= 0)
                    skill_status_change_end (bl, SC_REJECTSWORD, -1);
            }
        }
    }

    if (class == 1288 || class == 1287 || class == 1286 || class == 1285)
    {
//  if(class == 1288) {
        if (class == 1288 && flag & BF_SKILL)
            damage = 0;
        if (src->type == BL_PC)
        {
            struct guild *g =
                guild_search (((struct map_session_data *) src)->
                              status.guild_id);
            struct guild_castle *gc = guild_mapname2gc (map[bl->m].name);
            if (!((struct map_session_data *) src)->status.guild_id)
                damage = 0;
            if (gc && agit_flag == 0 && class != 1288)  // guardians cannot be damaged during non-woe [Valaris]
                damage = 0;     // end woe check [Valaris]
            if (g == NULL)
                damage = 0;     //�M���h�������Ȃ�_���[�W����
            else if ((gc != NULL) && guild_isallied (g, gc))
                damage = 0;     //����̃M���h�̃G���y�Ȃ�_���[�W����
            else if (g && guild_checkskill (g, GD_APPROVAL) <= 0)
                damage = 0;     //���K�M���h���F���Ȃ��ƃ_���[�W����
            else if (battle_config.guild_max_castles != 0
                     && guild_checkcastles (g) >=
                     battle_config.guild_max_castles)
                damage = 0;     // [MouseJstr]
        }
        else
            damage = 0;
    }

    if (map[bl->m].flag.gvg && damage > 0)
    {                           //GvG
        if (flag & BF_WEAPON)
        {
            if (flag & BF_SHORT)
                damage = damage * battle_config.gvg_short_damage_rate / 100;
            if (flag & BF_LONG)
                damage = damage * battle_config.gvg_long_damage_rate / 100;
        }
        if (flag & BF_MAGIC)
            damage = damage * battle_config.gvg_magic_damage_rate / 100;
        if (flag & BF_MISC)
            damage = damage * battle_config.gvg_misc_damage_rate / 100;
        if (damage < 1)
            damage = 1;
    }

    if (battle_config.skill_min_damage || flag & BF_MISC)
    {
        if (div_ < 255)
        {
            if (damage > 0 && damage < div_)
                damage = div_;
        }
        else if (damage > 0 && damage < 3)
            damage = 3;
    }

    if (md != NULL && md->hp > 0 && damage > 0) // �����Ȃǂ�MOB�X�L������
        mobskill_event (md, flag);

    return damage;
}

/*==========================================
 * �C���_���[�W
 *------------------------------------------
 */
int battle_addmastery (struct map_session_data *sd, struct block_list *target,
                       int dmg, int type)
{
    int  damage, skill;
    int  race = battle_get_race (target);
    int  weapon;
    damage = 0;

    nullpo_retr (0, sd);

    // �f�[�����x�C��(+3 �` +30) vs �s�� or ���� (���l�͊܂߂Ȃ��H)
    if ((skill = pc_checkskill (sd, AL_DEMONBANE)) > 0
        && (battle_check_undead (race, battle_get_elem_type (target))
            || race == 6))
        damage += (skill * 3);

    // �r�[�X�g�x�C��(+4 �` +40) vs ���� or ����
    if ((skill = pc_checkskill (sd, HT_BEASTBANE)) > 0
        && (race == 2 || race == 4))
        damage += (skill * 4);

    if (type == 0)
        weapon = sd->weapontype1;
    else
        weapon = sd->weapontype2;
    switch (weapon)
    {
        case 0x01:             // �Z�� (Updated By AppleGirl)
        case 0x02:             // 1HS
        {
            // ���C��(+4 �` +40) �Ў茕 �Z���܂�
            if ((skill = pc_checkskill (sd, SM_SWORD)) > 0)
            {
                damage += (skill * 4);
            }
            break;
        }
        case 0x03:             // 2HS
        {
            // ���茕�C��(+4 �` +40) ���茕
            if ((skill = pc_checkskill (sd, SM_TWOHAND)) > 0)
            {
                damage += (skill * 4);
            }
            break;
        }
        case 0x04:             // 1HL
        {
            // ���C��(+4 �` +40,+5 �` +50) ��
            if ((skill = pc_checkskill (sd, KN_SPEARMASTERY)) > 0)
            {
                if (!pc_isriding (sd))
                    damage += (skill * 4);  // �y�R�ɏ���ĂȂ�
                else
                    damage += (skill * 5);  // �y�R�ɏ���Ă�
            }
            break;
        }
        case 0x05:             // 2HL
        {
            // ���C��(+4 �` +40,+5 �` +50) ��
            if ((skill = pc_checkskill (sd, KN_SPEARMASTERY)) > 0)
            {
                if (!pc_isriding (sd))
                    damage += (skill * 4);  // �y�R�ɏ���ĂȂ�
                else
                    damage += (skill * 5);  // �y�R�ɏ���Ă�
            }
            break;
        }
        case 0x06:             // �Ў蕀
        {
            if ((skill = pc_checkskill (sd, AM_AXEMASTERY)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x07:             // Axe by Tato
        {
            if ((skill = pc_checkskill (sd, AM_AXEMASTERY)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x08:             // ���C�X
        {
            // ���C�X�C��(+3 �` +30) ���C�X
            if ((skill = pc_checkskill (sd, PR_MACEMASTERY)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x09:             // �Ȃ�?
            break;
        case 0x0a:             // ��
            break;
        case 0x0b:             // �|
            break;
        case 0x00:             // �f��
        case 0x0c:             // Knuckles
        {
            // �S��(+3 �` +30) �f��,�i�b�N��
            if ((skill = pc_checkskill (sd, MO_IRONHAND)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x0d:             // Musical Instrument
        {
            // �y��̗��K(+3 �` +30) �y��
            if ((skill = pc_checkskill (sd, BA_MUSICALLESSON)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x0e:             // Dance Mastery
        {
            // Dance Lesson Skill Effect(+3 damage for every lvl = +30) ��
            if ((skill = pc_checkskill (sd, DC_DANCINGLESSON)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x0f:             // Book
        {
            // Advance Book Skill Effect(+3 damage for every lvl = +30) {
            if ((skill = pc_checkskill (sd, SA_ADVANCEDBOOK)) > 0)
            {
                damage += (skill * 3);
            }
            break;
        }
        case 0x10:             // Katars
        {
            // �J�^�[���C��(+3 �` +30) �J�^�[��
            if ((skill = pc_checkskill (sd, AS_KATAR)) > 0)
            {
                //�\�j�b�N�u���[���͕ʏ����i1���ɕt��1/8�K��)
                damage += (skill * 3);
            }
            break;
        }
    }
    damage = dmg + damage;
    return (damage);
}

static struct Damage battle_calc_mob_weapon_attack (struct block_list *src,
                                                    struct block_list *target,
                                                    int skill_num,
                                                    int skill_lv, int wflag)
{
    struct map_session_data *tsd = NULL;
    struct mob_data *md = (struct mob_data *) src, *tmd = NULL;
    int  hitrate, flee, cri = 0, atkmin, atkmax;
    int  luk, target_count = 1;
    int  def1 = battle_get_def (target);
    int  def2 = battle_get_def2 (target);
    int  t_vit = battle_get_vit (target);
    struct Damage wd;
    int  damage, damage2 = 0, type, div_, blewcount =
        skill_get_blewcount (skill_num, skill_lv);
    int  flag, skill, ac_flag = 0, dmg_lv = 0;
    int  t_mode = 0, t_race = 0, t_size = 1, s_race = 0, s_ele = 0;
    struct status_change *sc_data, *t_sc_data;
    short *sc_count;
    short *option, *opt1, *opt2;

    //return�O�̏���������̂ŏ��o�͕��̂ݕύX
    if (src == NULL || target == NULL || md == NULL)
    {
        nullpo_info (NLP_MARK);
        memset (&wd, 0, sizeof (wd));
        return wd;
    }

    s_race = battle_get_race (src);
    s_ele = battle_get_attack_element (src);
    sc_data = battle_get_sc_data (src);
    sc_count = battle_get_sc_count (src);
    option = battle_get_option (src);
    opt1 = battle_get_opt1 (src);
    opt2 = battle_get_opt2 (src);

    // �^�[�Q�b�g
    if (target->type == BL_PC)
        tsd = (struct map_session_data *) target;
    else if (target->type == BL_MOB)
        tmd = (struct mob_data *) target;
    t_race = battle_get_race (target);
    t_size = battle_get_size (target);
    t_mode = battle_get_mode (target);
    t_sc_data = battle_get_sc_data (target);

    if ((skill_num == 0
         || (target->type == BL_PC && battle_config.pc_auto_counter_type & 2)
         || (target->type == BL_MOB
             && battle_config.monster_auto_counter_type & 2))
        && skill_lv >= 0)
    {
        if (skill_num != CR_GRANDCROSS && t_sc_data
            && t_sc_data[SC_AUTOCOUNTER].timer != -1)
        {
            int  dir = map_calc_dir (src, target->x, target->y), t_dir =
                battle_get_dir (target);
            int  dist = distance (src->x, src->y, target->x, target->y);
            if (dist <= 0 || map_check_dir (dir, t_dir))
            {
                memset (&wd, 0, sizeof (wd));
                t_sc_data[SC_AUTOCOUNTER].val3 = 0;
                t_sc_data[SC_AUTOCOUNTER].val4 = 1;
                if (sc_data && sc_data[SC_AUTOCOUNTER].timer == -1)
                {
                    int  range = battle_get_range (target);
                    if ((target->type == BL_PC
                         && ((struct map_session_data *) target)->
                         status.weapon != 11 && dist <= range + 1)
                        || (target->type == BL_MOB && range <= 3
                            && dist <= range + 1))
                        t_sc_data[SC_AUTOCOUNTER].val3 = src->id;
                }
                return wd;
            }
            else
                ac_flag = 1;
        }
    }
    flag = BF_SHORT | BF_WEAPON | BF_NORMAL;    // �U���̎�ނ̐ݒ�

    // ��𗦌v�Z�A��𔻒�͌��
    flee = battle_get_flee (target);
    if (battle_config.agi_penaly_type > 0
        || battle_config.vit_penaly_type > 0)
        target_count +=
            battle_counttargeted (target, src,
                                  battle_config.agi_penaly_count_lv);
    if (battle_config.agi_penaly_type > 0)
    {
        if (target_count >= battle_config.agi_penaly_count)
        {
            if (battle_config.agi_penaly_type == 1)
                flee =
                    (flee *
                     (100 -
                      (target_count -
                       (battle_config.agi_penaly_count -
                        1)) * battle_config.agi_penaly_num)) / 100;
            else if (battle_config.agi_penaly_type == 2)
                flee -=
                    (target_count -
                     (battle_config.agi_penaly_count -
                      1)) * battle_config.agi_penaly_num;
            if (flee < 1)
                flee = 1;
        }
    }
    hitrate = battle_get_hit (src) - flee + 80;

    type = 0;                   // normal
    div_ = 1;                   // single attack

    luk = battle_get_luk (src);

    if (battle_config.enemy_str)
        damage = battle_get_baseatk (src);
    else
        damage = 0;
    if (skill_num == HW_MAGICCRASHER)
    {                           /* �}�W�b�N�N���b�V���[��MATK�ŉ��� */
        atkmin = battle_get_matk1 (src);
        atkmax = battle_get_matk2 (src);
    }
    else
    {
        atkmin = battle_get_atk (src);
        atkmax = battle_get_atk2 (src);
    }
    if (mob_db[md->class].range > 3)
        flag = (flag & ~BF_RANGEMASK) | BF_LONG;

    if (atkmin > atkmax)
        atkmin = atkmax;

    if (sc_data != NULL && sc_data[SC_MAXIMIZEPOWER].timer != -1)
    {                           // �}�L�V�}�C�Y�p���[
        atkmin = atkmax;
    }

    cri = battle_get_critical (src);
    cri -= battle_get_luk (target) * 3;
    if (battle_config.enemy_critical_rate != 100)
    {
        cri = cri * battle_config.enemy_critical_rate / 100;
        if (cri < 1)
            cri = 1;
    }
    if (t_sc_data != NULL && t_sc_data[SC_SLEEP].timer != -1)   // �������̓N���e�B�J�����{��
        cri <<= 1;

    if (ac_flag)
        cri = 1000;

    if (skill_num == KN_AUTOCOUNTER)
    {
        if (!(battle_config.monster_auto_counter_type & 1))
            cri = 1000;
        else
            cri <<= 1;
    }

    if (tsd && tsd->critical_def)
        cri = cri * (100 - tsd->critical_def) / 100;

    if ((skill_num == 0 || skill_num == KN_AUTOCOUNTER) && skill_lv >= 0 && battle_config.enemy_critical && (MRAND (1000)) < cri)   // ����i�X�L���̏ꍇ�͖����j
        // �G�̔���
    {
        damage += atkmax;
        type = 0x0a;
    }
    else
    {
        int  vitbonusmax;

        if (atkmax > atkmin)
            damage += atkmin + MRAND ((atkmax - atkmin + 1));
        else
            damage += atkmin;
        // �X�L���C���P�i�U���͔{���n�j
        // �I�[�o�[�g���X�g(+5% �` +25%),���U���n�X�L���̏ꍇ�����ŕ␳
        // �o�b�V��,�}�O�i���u���C�N,
        // �{�[�����O�o�b�V��,�X�s�A�u�[������,�u�����f�B�b�V���X�s�A,�X�s�A�X�^�b�u,
        // ���}�[�i�C�g,�J�[�g���{�����[�V����
        // �_�u���X�g���C�t�B���O,�A���[�V�����[,�`���[�W�A���[,
        // �\�j�b�N�u���[
        if (sc_data)
        {                       //��Ԉُ풆�̃_���[�W�ǉ�
            if (sc_data[SC_OVERTHRUST].timer != -1) // �I�[�o�[�g���X�g
                damage += damage * (5 * sc_data[SC_OVERTHRUST].val1) / 100;
            if (sc_data[SC_TRUESIGHT].timer != -1)  // �g�D���[�T�C�g
                damage += damage * (2 * sc_data[SC_TRUESIGHT].val1) / 100;
            if (sc_data[SC_BERSERK].timer != -1)    // �o�[�T�[�N
                damage += damage * 50 / 100;
        }

        if (skill_num > 0)
        {
            int  i;
            if ((i = skill_get_pl (skill_num)) > 0)
                s_ele = i;

            flag = (flag & ~BF_SKILLMASK) | BF_SKILL;
            switch (skill_num)
            {
                case SM_BASH:  // �o�b�V��
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    hitrate = (hitrate * (100 + 5 * skill_lv)) / 100;
                    break;
                case SM_MAGNUM:    // �}�O�i���u���C�N
                    damage =
                        damage * (5 * skill_lv + (wflag) ? 65 : 115) / 100;
                    break;
                case MC_MAMMONITE: // ���}�[�i�C�g
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    break;
                case AC_DOUBLE:    // �_�u���X�g���C�t�B���O
                    damage = damage * (180 + 20 * skill_lv) / 100;
                    div_ = 2;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case AC_SHOWER:    // �A���[�V�����[
                    damage = damage * (75 + 5 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case AC_CHARGEARROW:   // �`���[�W�A���[
                    damage = damage * 150 / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case KN_PIERCE:    // �s�A�[�X
                    damage = damage * (100 + 10 * skill_lv) / 100;
                    hitrate = hitrate * (100 + 5 * skill_lv) / 100;
                    div_ = t_size + 1;
                    damage *= div_;
                    break;
                case KN_SPEARSTAB: // �X�s�A�X�^�u
                    damage = damage * (100 + 15 * skill_lv) / 100;
                    break;
                case KN_SPEARBOOMERANG:    // �X�s�A�u�[������
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case KN_BRANDISHSPEAR: // �u�����f�B�b�V���X�s�A
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    if (skill_lv > 3 && wflag == 1)
                        damage2 += damage / 2;
                    if (skill_lv > 6 && wflag == 1)
                        damage2 += damage / 4;
                    if (skill_lv > 9 && wflag == 1)
                        damage2 += damage / 8;
                    if (skill_lv > 6 && wflag == 2)
                        damage2 += damage / 2;
                    if (skill_lv > 9 && wflag == 2)
                        damage2 += damage / 4;
                    if (skill_lv > 9 && wflag == 3)
                        damage2 += damage / 2;
                    damage += damage2;
                    blewcount = 0;
                    break;
                case KN_BOWLINGBASH:   // �{�E�����O�o�b�V��
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    blewcount = 0;
                    break;
                case KN_AUTOCOUNTER:
                    if (battle_config.monster_auto_counter_type & 1)
                        hitrate += 20;
                    else
                        hitrate = 1000000;
                    flag = (flag & ~BF_SKILLMASK) | BF_NORMAL;
                    break;
                case AS_SONICBLOW: // �\�j�b�N�u���E
                    damage = damage * (300 + 50 * skill_lv) / 100;
                    div_ = 8;
                    break;
                case TF_SPRINKLESAND:  // ���܂�
                    damage = damage * 125 / 100;
                    break;
                case MC_CARTREVOLUTION:    // �J�[�g���{�����[�V����
                    damage = (damage * 150) / 100;
                    break;
                    // �ȉ�MOB
                case NPC_COMBOATTACK:  // ���i�U��
                    div_ = skill_get_num (skill_num, skill_lv);
                    damage *= div_;
                    break;
                case NPC_RANDOMATTACK: // �����_��ATK�U��
                    damage = damage * (MPRAND (50, 150)) / 100;
                    break;
                    // �����U���i�K���j
                case NPC_WATERATTACK:
                case NPC_GROUNDATTACK:
                case NPC_FIREATTACK:
                case NPC_WINDATTACK:
                case NPC_POISONATTACK:
                case NPC_HOLYATTACK:
                case NPC_DARKNESSATTACK:
                case NPC_TELEKINESISATTACK:
                    damage = damage * (100 + 25 * (skill_lv - 1)) / 100;
                    break;
                case NPC_GUIDEDATTACK:
                    hitrate = 1000000;
                    break;
                case NPC_RANGEATTACK:
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case NPC_PIERCINGATT:
                    flag = (flag & ~BF_RANGEMASK) | BF_SHORT;
                    break;
                case RG_BACKSTAP:  // �o�b�N�X�^�u
                    damage = damage * (300 + 40 * skill_lv) / 100;
                    hitrate = 1000000;
                    break;
                case RG_RAID:  // �T�v���C�Y�A�^�b�N
                    damage = damage * (100 + 40 * skill_lv) / 100;
                    break;
                case RG_INTIMIDATE:    // �C���e�B�~�f�C�g
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    break;
                case CR_SHIELDCHARGE:  // �V�[���h�`���[�W
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_SHORT;
                    s_ele = 0;
                    break;
                case CR_SHIELDBOOMERANG:   // �V�[���h�u�[������
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    s_ele = 0;
                    break;
                case CR_HOLYCROSS: // �z�[���[�N���X
                    damage = damage * (100 + 35 * skill_lv) / 100;
                    div_ = 2;
                    break;
                case CR_GRANDCROSS:
                    hitrate = 1000000;
                    break;
                case AM_DEMONSTRATION: // �f�����X�g���[�V����
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    break;
                case AM_ACIDTERROR:    // �A�V�b�h�e���[
                    damage = damage * (100 + 40 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 40 * skill_lv) / 100;
                    break;
                case MO_FINGEROFFENSIVE:   //�w�e
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    div_ = 1;
                    break;
                case MO_INVESTIGATE:   // �� ��
                    if (def1 < 1000000)
                        damage =
                            damage * (100 + 75 * skill_lv) / 100 * (def1 +
                                                                    def2) /
                            100;
                    hitrate = 1000000;
                    s_ele = 0;
                    break;
                case MO_EXTREMITYFIST: // ���C���e�P��
                    damage = damage * 8 + 250 + (skill_lv * 150);
                    hitrate = 1000000;
                    s_ele = 0;
                    break;
                case MO_CHAINCOMBO:    // �A�ŏ�
                    damage = damage * (150 + 50 * skill_lv) / 100;
                    div_ = 4;
                    break;
                case BA_MUSICALSTRIKE: // �~���[�W�J���X�g���C�N
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case DC_THROWARROW:    // ���
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case MO_COMBOFINISH:   // �җ���
                    damage = damage * (240 + 60 * skill_lv) / 100;
                    break;
                case CH_TIGERFIST: // ���Ռ�
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    break;
                case CH_CHAINCRUSH:    // �A������
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    div_ = skill_get_num (skill_num, skill_lv);
                    break;
                case CH_PALMSTRIKE:    // �ҌՍd�h�R
                    damage = damage * (50 + 100 * skill_lv) / 100;
                    break;
                case LK_SPIRALPIERCE:  /* �X�p�C�����s�A�[�X */
                    damage = damage * (100 + 50 * skill_lv) / 100;  //�����ʂ�������Ȃ��̂œK����
                    div_ = 5;
                    if (tsd)
                        tsd->canmove_tick = gettick () + 1000;
                    else if (tmd)
                        tmd->canmove_tick = gettick () + 1000;
                    break;
                case LK_HEADCRUSH: /* �w�b�h�N���b�V�� */
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    break;
                case LK_JOINTBEAT: /* �W���C���g�r�[�g */
                    damage = damage * (50 + 10 * skill_lv) / 100;
                    break;
                case ASC_METEORASSAULT:    /* ���e�I�A�T���g */
                    damage = damage * (40 + 40 * skill_lv) / 100;
                    break;
                case SN_SHARPSHOOTING: /* �V���[�v�V���[�e�B���O */
                    damage += damage * (30 * skill_lv) / 100;
                    break;
                case CG_ARROWVULCAN:   /* �A���[�o���J�� */
                    damage = damage * (160 + 40 * skill_lv) / 100;
                    div_ = 9;
                    break;
                case AS_SPLASHER:  /* �x�i���X�v���b�V���[ */
                    damage = damage * (200 + 20 * skill_lv) / 100;
                    break;
            }
        }

        if (skill_num != NPC_CRITICALSLASH)
        {
            // �� �ۂ̖h��͂ɂ��_���[�W�̌���
            // �f�B�o�C���v���e�N�V�����i�����ł����̂��ȁH�j
            if (skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST
                && skill_num != KN_AUTOCOUNTER && def1 < 1000000)
            {                   //DEF, VIT����
                int  t_def;
                target_count =
                    1 + battle_counttargeted (target, src,
                                              battle_config.vit_penaly_count_lv);
                if (battle_config.vit_penaly_type > 0)
                {
                    if (target_count >= battle_config.vit_penaly_count)
                    {
                        if (battle_config.vit_penaly_type == 1)
                        {
                            def1 =
                                (def1 *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                            def2 =
                                (def2 *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                            t_vit =
                                (t_vit *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                        }
                        else if (battle_config.vit_penaly_type == 2)
                        {
                            def1 -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                            def2 -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                            t_vit -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                        }
                        if (def1 < 0)
                            def1 = 0;
                        if (def2 < 1)
                            def2 = 1;
                        if (t_vit < 1)
                            t_vit = 1;
                    }
                }
                t_def = def2 * 8 / 10;
                if (battle_check_undead (s_race, battle_get_elem_type (src))
                    || s_race == 6)
                    if (tsd && (skill = pc_checkskill (tsd, AL_DP)) > 0)
                        t_def += skill * 3;

                vitbonusmax = (t_vit / 20) * (t_vit / 20) - 1;
                if (battle_config.monster_defense_type)
                {
                    damage =
                        damage - (def1 * battle_config.monster_defense_type) -
                        t_def -
                        ((vitbonusmax < 1) ? 0 : MRAND ((vitbonusmax + 1)));
                }
                else
                {
                    damage =
                        damage * (100 - def1) / 100 - t_def -
                        ((vitbonusmax < 1) ? 0 : MRAND ((vitbonusmax + 1)));
                }
            }
        }
    }

    // 0�����������ꍇ1�ɕ␳
    if (damage < 1)
        damage = 1;

    // ����C��
    if (hitrate < 1000000)
        hitrate = ((hitrate > 95) ? 95 : ((hitrate < 5) ? 5 : hitrate));
    if (hitrate < 1000000 &&    // �K���U��
        (t_sc_data != NULL && (t_sc_data[SC_SLEEP].timer != -1 ||   // �����͕K��
                               t_sc_data[SC_STAN].timer != -1 ||    // �X�^���͕K��
                               t_sc_data[SC_FREEZE].timer != -1 || (t_sc_data[SC_STONE].timer != -1 && t_sc_data[SC_STONE].val2 == 0))))    // �����͕K��
        hitrate = 1000000;
    if (type == 0 && MRAND (100) >= hitrate)
    {
        damage = damage2 = 0;
        dmg_lv = ATK_FLEE;
    }
    else
    {
        dmg_lv = ATK_DEF;
    }

    if (tsd)
    {
        int  cardfix = 100, i;
        cardfix = cardfix * (100 - tsd->subele[s_ele]) / 100;   // �� ���ɂ��_���[�W�ϐ�
        cardfix = cardfix * (100 - tsd->subrace[s_race]) / 100; // �푰�ɂ��_���[�W�ϐ�
        if (mob_db[md->class].mode & 0x20)
            cardfix = cardfix * (100 - tsd->subrace[10]) / 100;
        else
            cardfix = cardfix * (100 - tsd->subrace[11]) / 100;
        for (i = 0; i < tsd->add_def_class_count; i++)
        {
            if (tsd->add_def_classid[i] == md->class)
            {
                cardfix = cardfix * (100 - tsd->add_def_classrate[i]) / 100;
                break;
            }
        }
        if (flag & BF_LONG)
            cardfix = cardfix * (100 - tsd->long_attack_def_rate) / 100;
        if (flag & BF_SHORT)
            cardfix = cardfix * (100 - tsd->near_attack_def_rate) / 100;
        damage = damage * cardfix / 100;
    }
    if (t_sc_data)
    {
        int  cardfix = 100;
        if (t_sc_data[SC_DEFENDER].timer != -1 && flag & BF_LONG)
            cardfix = cardfix * (100 - t_sc_data[SC_DEFENDER].val2) / 100;
        if (cardfix != 100)
            damage = damage * cardfix / 100;
    }
    if (t_sc_data && t_sc_data[SC_ASSUMPTIO].timer != -1)
    {                           //�A�V�����v�e�B�I
        if (!map[target->m].flag.pvp)
            damage = damage / 3;
        else
            damage = damage / 2;
    }

    if (damage < 0)
        damage = 0;

    // �� ���̓K�p
    if (!((battle_config.mob_ghostring_fix == 1) && (battle_get_element (target) == 8) && (target->type == BL_PC))) // [MouseJstr]
        if (skill_num != 0 || s_ele != 0
            || !battle_config.mob_attack_attr_none)
            damage =
                battle_attr_fix (damage, s_ele, battle_get_element (target));

    if (sc_data && sc_data[SC_AURABLADE].timer != -1)   /* �I�[���u���[�h �K�� */
        damage += sc_data[SC_AURABLADE].val1 * 10;
    if (skill_num == PA_PRESSURE)   /* �v���b�V���[ �K��? */
        damage = 700 + 100 * skill_lv;

    // �C���x�i���C��
    if (skill_num == TF_POISON)
    {
        damage =
            battle_attr_fix (damage + 15 * skill_lv, s_ele,
                             battle_get_element (target));
    }
    if (skill_num == MC_CARTREVOLUTION)
    {
        damage = battle_attr_fix (damage, 0, battle_get_element (target));
    }

    // ���S����̔���
    if (skill_num == 0 && skill_lv >= 0 && tsd != NULL
        && MRAND (1000) < battle_get_flee2 (target))
    {
        damage = 0;
        type = 0x0b;
        dmg_lv = ATK_LUCKY;
    }

    if (battle_config.enemy_perfect_flee)
    {
        if (skill_num == 0 && skill_lv >= 0 && tmd != NULL
            && MRAND (1000) < battle_get_flee2 (target))
        {
            damage = 0;
            type = 0x0b;
            dmg_lv = ATK_LUCKY;
        }
    }

//  if(def1 >= 1000000 && damage > 0)
    if (t_mode & 0x40 && damage > 0)
        damage = 1;

    if (tsd && tsd->special_state.no_weapon_damage)
        damage = 0;

    if (skill_num != CR_GRANDCROSS)
        damage =
            battle_calc_damage (src, target, damage, div_, skill_num,
                                skill_lv, flag);

    wd.damage = damage;
    wd.damage2 = 0;
    wd.type = type;
    wd.div_ = div_;
    wd.amotion = battle_get_amotion (src);
    if (skill_num == KN_AUTOCOUNTER)
        wd.amotion >>= 1;
    wd.dmotion = battle_get_dmotion (target);
    wd.blewcount = blewcount;
    wd.flag = flag;
    wd.dmg_lv = dmg_lv;
    return wd;
}

int battle_is_unarmed (struct block_list *bl)
{
    if (!bl)
        return 0;
    if (bl->type == BL_PC)
    {
        struct map_session_data *sd = (struct map_session_data *) bl;

        return (sd->equip_index[EQUIP_SHIELD] == -1
                && sd->equip_index[EQUIP_WEAPON] == -1);
    }
    else
        return 0;
}

/*
 * =========================================================================
 * PC�̕���ɂ��U��
 *-------------------------------------------------------------------------
 */
static struct Damage battle_calc_pc_weapon_attack (struct block_list *src,
                                                   struct block_list *target,
                                                   int skill_num,
                                                   int skill_lv, int wflag)
{
    struct map_session_data *sd = (struct map_session_data *) src, *tsd =
        NULL;
    struct mob_data *tmd = NULL;
    int  hitrate, flee, cri = 0, atkmin, atkmax;
    int  dex, luk, target_count = 1;
    int  def1 = battle_get_def (target);
    int  def2 = battle_get_def2 (target);
    int  t_vit = battle_get_vit (target);
    struct Damage wd;
    int  damage, damage2, damage3 = 0, damage4 = 0, type, div_, blewcount =
        skill_get_blewcount (skill_num, skill_lv);
    int  flag, skill, dmg_lv = 0;
    int  t_mode = 0, t_race = 0, t_size = 1, s_race = 7, s_ele = 0;
    struct status_change *sc_data, *t_sc_data;
    short *sc_count;
    short *option, *opt1, *opt2;
    int  atkmax_ = 0, atkmin_ = 0, s_ele_;  //�񓁗��p
    int  watk, watk_, cardfix, t_ele;
    int  da = 0, i, t_class, ac_flag = 0;
    int  idef_flag = 0, idef_flag_ = 0;
    int  target_distance;

    //return�O�̏���������̂ŏ��o�͕��̂ݕύX
    if (src == NULL || target == NULL || sd == NULL)
    {
        nullpo_info (NLP_MARK);
        memset (&wd, 0, sizeof (wd));
        return wd;
    }

    // �A�^�b�J�[
    s_race = battle_get_race (src); //�푰
    s_ele = battle_get_attack_element (src);    //����
    s_ele_ = battle_get_attack_element2 (src);  //���葮��
    sc_data = battle_get_sc_data (src); //�X�e�[�^�X�ُ�
    sc_count = battle_get_sc_count (src);   //�X�e�[�^�X�ُ�̐�
    option = battle_get_option (src);   //��Ƃ��y�R�Ƃ��J�[�g�Ƃ�
    opt1 = battle_get_opt1 (src);   //�Ή��A�����A�X�^���A�����A�È�
    opt2 = battle_get_opt2 (src);   //�ŁA�􂢁A���فA�ÈŁH

    if (skill_num != CR_GRANDCROSS) //�O�����h�N���X�łȂ��Ȃ�
        sd->state.attack_type = BF_WEAPON;  //�U���^�C�v�͕���U��

    // �^�[�Q�b�g
    if (target->type == BL_PC)  //�Ώۂ�PC�Ȃ�
        tsd = (struct map_session_data *) target;   //tsd�ɑ��(tmd��NULL)
    else if (target->type == BL_MOB)    //�Ώۂ�Mob�Ȃ�
        tmd = (struct mob_data *) target;   //tmd�ɑ��(tsd��NULL)
    t_race = battle_get_race (target);  //�Ώۂ̎푰
    t_ele = battle_get_elem_type (target);  //�Ώۂ̑���
    t_size = battle_get_size (target);  //�Ώۂ̃T�C�Y
    t_mode = battle_get_mode (target);  //�Ώۂ�Mode
    t_sc_data = battle_get_sc_data (target);    //�Ώۂ̃X�e�[�^�X�ُ�

//�I�[�g�J�E���^�[������������
    if ((skill_num == 0
         || (target->type == BL_PC && battle_config.pc_auto_counter_type & 2)
         || (target->type == BL_MOB
             && battle_config.monster_auto_counter_type & 2))
        && skill_lv >= 0)
    {
        if (skill_num != CR_GRANDCROSS && t_sc_data
            && t_sc_data[SC_AUTOCOUNTER].timer != -1)
        {                       //�O�����h�N���X�łȂ��A�Ώۂ��I�[�g�J�E���^�[��Ԃ̏ꍇ
            int  dir = map_calc_dir (src, target->x, target->y), t_dir =
                battle_get_dir (target);
            int  dist = distance (src->x, src->y, target->x, target->y);
            if (dist <= 0 || map_check_dir (dir, t_dir))
            {                   //�ΏۂƂ̋�����0�ȉ��A�܂��͑Ώۂ̐��ʁH
                memset (&wd, 0, sizeof (wd));
                t_sc_data[SC_AUTOCOUNTER].val3 = 0;
                t_sc_data[SC_AUTOCOUNTER].val4 = 1;
                if (sc_data && sc_data[SC_AUTOCOUNTER].timer == -1)
                {               //�������I�[�g�J�E���^�[���
                    int  range = battle_get_range (target);
                    if ((target->type == BL_PC && ((struct map_session_data *) target)->status.weapon != 11 && dist <= range + 1) ||    //�Ώۂ�PC�ŕ��킪�|��łȂ��˒���
                        (target->type == BL_MOB && range <= 3 && dist <= range + 1))    //�܂��͑Ώۂ�Mob�Ŏ˒���3�ȉ��Ŏ˒���
                        t_sc_data[SC_AUTOCOUNTER].val3 = src->id;
                }
                return wd;      //�_���[�W�\���̂�Ԃ��ďI��
            }
            else
                ac_flag = 1;
        }
    }
//�I�[�g�J�E���^�[���������܂�

    flag = BF_SHORT | BF_WEAPON | BF_NORMAL;    // �U���̎�ނ̐ݒ�

    // ��𗦌v�Z�A��𔻒�͌��
    flee = battle_get_flee (target);
    if (battle_config.agi_penaly_type > 0 || battle_config.vit_penaly_type > 0) //AGI�AVIT�y�i���e�B�ݒ肪�L��
        target_count += battle_counttargeted (target, src, battle_config.agi_penaly_count_lv);  //�Ώۂ̐����Z�o
    if (battle_config.agi_penaly_type > 0)
    {
        if (target_count >= battle_config.agi_penaly_count)
        {                       //�y�i���e�B�ݒ���Ώۂ�����
            if (battle_config.agi_penaly_type == 1) //��𗦂�agi_penaly_num%������
                flee =
                    (flee *
                     (100 -
                      (target_count -
                       (battle_config.agi_penaly_count -
                        1)) * battle_config.agi_penaly_num)) / 100;
            else if (battle_config.agi_penaly_type == 2)    //��𗦂�agi_penaly_num������
                flee -=
                    (target_count -
                     (battle_config.agi_penaly_count -
                      1)) * battle_config.agi_penaly_num;
            if (flee < 1)
                flee = 1;       //��𗦂͍Œ�ł�1
        }
    }
    hitrate = battle_get_hit (src) - flee + 80; //�������v�Z

    {                           // [fate] Reduce hit chance by distance
        int  dx = abs (src->x - target->x);
        int  dy = abs (src->y - target->y);
        int  malus_dist;

        target_distance = MAX (dx, dy);
        malus_dist =
            MAX (0, target_distance - (skill_power (sd, AC_OWL) / 75));
        hitrate -= (malus_dist * (malus_dist + 1));
    }

    dex = battle_get_dex (src); //DEX
    luk = battle_get_luk (src); //LUK
    watk = battle_get_atk (src);    //ATK
    watk_ = battle_get_atk_ (src);  //ATK����

    type = 0;                   // normal
    div_ = 1;                   // single attack

    if (skill_num == HW_MAGICCRASHER)
    {                           /* �}�W�b�N�N���b�V���[��MATK�ŉ��� */
        damage = damage2 = battle_get_matk1 (src);  //damega,damega2���o��Abase_atk�̎擾
    }
    else
    {
        damage = damage2 = battle_get_baseatk (&sd->bl);    //damega,damega2���o��Abase_atk�̎擾
    }
    if (sd->attackrange > 2)
    {                           // [fate] ranged weapon?
        const int range_damage_bonus = 80;  // up to 31.25% bonus for long-range hit
        damage =
            damage * (256 +
                      ((range_damage_bonus * target_distance) /
                       sd->attackrange)) >> 8;
        damage2 =
            damage2 * (256 +
                       ((range_damage_bonus * target_distance) /
                        sd->attackrange)) >> 8;
    }

    atkmin = atkmin_ = dex;     //�Œ�ATK��DEX�ŏ������H
    sd->state.arrow_atk = 0;    //arrow_atk������
    if (sd->equip_index[9] >= 0 && sd->inventory_data[sd->equip_index[9]])
        atkmin =
            atkmin * (80 +
                      sd->inventory_data[sd->equip_index[9]]->wlv * 20) / 100;
    if (sd->equip_index[8] >= 0 && sd->inventory_data[sd->equip_index[8]])
        atkmin_ =
            atkmin_ * (80 +
                       sd->inventory_data[sd->equip_index[8]]->wlv * 20) /
            100;
    if (sd->status.weapon == 11)
    {                           //���킪�|��̏ꍇ
        atkmin = watk * ((atkmin < watk) ? atkmin : watk) / 100;    //�|�p�Œ�ATK�v�Z
        flag = (flag & ~BF_RANGEMASK) | BF_LONG;    //�������U���t���O��L��
        if (sd->arrow_ele > 0)  //������Ȃ瑮�����̑����ɕύX
            s_ele = sd->arrow_ele;
        sd->state.arrow_atk = 1;    //arrow_atk�L����
    }

    // �T�C�Y�C��
    // �y�R�R�悵�Ă��āA���ōU�������ꍇ�͒��^�̃T�C�Y�C����100�ɂ���
    // �E�F�|���p�[�t�F�N�V����,�h���C�NC
    if (((sd->special_state.no_sizefix)
         || (pc_isriding (sd)
             && (sd->status.weapon == 4 || sd->status.weapon == 5)
             && t_size == 1) || skill_num == MO_EXTREMITYFIST))
    {                           //�y�R�R�悵�Ă��āA���Œ��^���U��
        atkmax = watk;
        atkmax_ = watk_;
    }
    else
    {
        atkmax = (watk * sd->atkmods[t_size]) / 100;
        atkmin = (atkmin * sd->atkmods[t_size]) / 100;
        atkmax_ = (watk_ * sd->atkmods_[t_size]) / 100;
        atkmin_ = (atkmin_ * sd->atkmods[t_size]) / 100;
    }
    if ((sc_data != NULL && sc_data[SC_WEAPONPERFECTION].timer != -1)
        || (sd->special_state.no_sizefix))
    {                           // �E�F�|���p�[�t�F�N�V���� || �h���C�N�J�[�h
        atkmax = watk;
        atkmax_ = watk_;
    }

    if (atkmin > atkmax && !(sd->state.arrow_atk))
        atkmin = atkmax;        //�|�͍ŒႪ����ꍇ����
    if (atkmin_ > atkmax_)
        atkmin_ = atkmax_;

    if (sc_data != NULL && sc_data[SC_MAXIMIZEPOWER].timer != -1)
    {                           // �}�L�V�}�C�Y�p���[
        atkmin = atkmax;
        atkmin_ = atkmax_;
    }

    //�_�u���A�^�b�N����
    if (sd->weapontype1 == 0x01)
    {
        if (skill_num == 0 && skill_lv >= 0
            && (skill = pc_checkskill (sd, TF_DOUBLE)) > 0)
            da = (MRAND (100) < (skill * 5)) ? 1 : 0;
    }

    //�O�i��
    if (skill_num == 0 && skill_lv >= 0
        && (skill = pc_checkskill (sd, MO_TRIPLEATTACK)) > 0
        && sd->status.weapon <= 16 && !sd->state.arrow_atk)
    {
        da = (MRAND (100) < (30 - skill)) ? 2 : 0;
    }

    if (sd->double_rate > 0 && da == 0 && skill_num == 0 && skill_lv >= 0)
        da = (MRAND (100) < sd->double_rate) ? 1 : 0;

    // �ߏ萸�B�{�[�i�X
    if (sd->overrefine > 0)
        damage += MPRAND (1, sd->overrefine);
    if (sd->overrefine_ > 0)
        damage2 += MPRAND (1, sd->overrefine_);

    if (da == 0)
    {                           //�_�u���A�^�b�N���������Ă��Ȃ�
        // �N���e�B�J���v�Z
        cri = battle_get_critical (src);

        if (sd->state.arrow_atk)
            cri += sd->arrow_cri;
        if (sd->status.weapon == 16)
            // �J�^�[���̏ꍇ�A�N���e�B�J����{��
            cri <<= 1;
        cri -= battle_get_luk (target) * 3;
        if (t_sc_data != NULL && t_sc_data[SC_SLEEP].timer != -1)   // �������̓N���e�B�J�����{��
            cri <<= 1;
        if (ac_flag)
            cri = 1000;

        if (skill_num == KN_AUTOCOUNTER)
        {
            if (!(battle_config.pc_auto_counter_type & 1))
                cri = 1000;
            else
                cri <<= 1;
        }

        if (skill_num == SN_SHARPSHOOTING && MRAND (100) < 50)
            cri = 1000;
    }

    if (tsd && tsd->critical_def)
        cri = cri * (100 - tsd->critical_def) / 100;

    if (da == 0 && (skill_num == 0 || skill_num == KN_AUTOCOUNTER || skill_num == SN_SHARPSHOOTING) && skill_lv >= 0 && //�_�u���A�^�b�N���������Ă��Ȃ�
        (MRAND (1000)) < cri)   // ����i�X�L���̏ꍇ�͖����j
    {
        damage += atkmax;
        damage2 += atkmax_;
        if (sd->atk_rate != 100)
        {
            damage = (damage * sd->atk_rate) / 100;
            damage2 = (damage2 * sd->atk_rate) / 100;
        }
        if (sd->state.arrow_atk)
            damage += sd->arrow_atk;
        type = 0x0a;

/*		if(def1 < 1000000) {
			if(sd->def_ratio_atk_ele & (1<<t_ele) || sd->def_ratio_atk_race & (1<<t_race)) {
				damage = (damage * (def1 + def2))/100;
				idef_flag = 1;
			}
			if(sd->def_ratio_atk_ele_ & (1<<t_ele) || sd->def_ratio_atk_race_ & (1<<t_race)) {
				damage2 = (damage2 * (def1 + def2))/100;
				idef_flag_ = 1;
			}
			if(t_mode & 0x20) {
				if(!idef_flag && sd->def_ratio_atk_race & (1<<10)) {
					damage = (damage * (def1 + def2))/100;
					idef_flag = 1;
				}
				if(!idef_flag_ && sd->def_ratio_atk_race_ & (1<<10)) {
					damage2 = (damage2 * (def1 + def2))/100;
					idef_flag_ = 1;
				}
			}
			else {
				if(!idef_flag && sd->def_ratio_atk_race & (1<<11)) {
					damage = (damage * (def1 + def2))/100;
					idef_flag = 1;
				}
				if(!idef_flag_ && sd->def_ratio_atk_race_ & (1<<11)) {
					damage2 = (damage2 * (def1 + def2))/100;
					idef_flag_ = 1;
				}
			}
		}*/
    }
    else
    {
        int  vitbonusmax;

        if (atkmax > atkmin)
            damage += atkmin + MRAND ((atkmax - atkmin + 1));
        else
            damage += atkmin;
        if (atkmax_ > atkmin_)
            damage2 += atkmin_ + MRAND ((atkmax_ - atkmin_ + 1));
        else
            damage2 += atkmin_;
        if (sd->atk_rate != 100)
        {
            damage = (damage * sd->atk_rate) / 100;
            damage2 = (damage2 * sd->atk_rate) / 100;
        }

        if (sd->state.arrow_atk)
        {
            if (sd->arrow_atk > 0)
                damage += MRAND ((sd->arrow_atk + 1));
            hitrate += sd->arrow_hit;
        }

        if (skill_num != MO_INVESTIGATE && def1 < 1000000)
        {
            if (sd->def_ratio_atk_ele & (1 << t_ele)
                || sd->def_ratio_atk_race & (1 << t_race))
            {
                damage = (damage * (def1 + def2)) / 100;
                idef_flag = 1;
            }
            if (sd->def_ratio_atk_ele_ & (1 << t_ele)
                || sd->def_ratio_atk_race_ & (1 << t_race))
            {
                damage2 = (damage2 * (def1 + def2)) / 100;
                idef_flag_ = 1;
            }
            if (t_mode & 0x20)
            {
                if (!idef_flag && sd->def_ratio_atk_race & (1 << 10))
                {
                    damage = (damage * (def1 + def2)) / 100;
                    idef_flag = 1;
                }
                if (!idef_flag_ && sd->def_ratio_atk_race_ & (1 << 10))
                {
                    damage2 = (damage2 * (def1 + def2)) / 100;
                    idef_flag_ = 1;
                }
            }
            else
            {
                if (!idef_flag && sd->def_ratio_atk_race & (1 << 11))
                {
                    damage = (damage * (def1 + def2)) / 100;
                    idef_flag = 1;
                }
                if (!idef_flag_ && sd->def_ratio_atk_race_ & (1 << 11))
                {
                    damage2 = (damage2 * (def1 + def2)) / 100;
                    idef_flag_ = 1;
                }
            }
        }

        // �X�L���C���P�i�U���͔{���n�j
        // �I�[�o�[�g���X�g(+5% �` +25%),���U���n�X�L���̏ꍇ�����ŕ␳
        // �o�b�V��,�}�O�i���u���C�N,
        // �{�[�����O�o�b�V��,�X�s�A�u�[������,�u�����f�B�b�V���X�s�A,�X�s�A�X�^�b�u,
        // ���}�[�i�C�g,�J�[�g���{�����[�V����
        // �_�u���X�g���C�t�B���O,�A���[�V�����[,�`���[�W�A���[,
        // �\�j�b�N�u���[
        if (sc_data)
        {                       //��Ԉُ풆�̃_���[�W�ǉ�
            if (sc_data[SC_OVERTHRUST].timer != -1)
            {                   // �I�[�o�[�g���X�g
                damage += damage * (5 * sc_data[SC_OVERTHRUST].val1) / 100;
                damage2 += damage2 * (5 * sc_data[SC_OVERTHRUST].val1) / 100;
            }
            if (sc_data[SC_TRUESIGHT].timer != -1)
            {                   // �g�D���[�T�C�g
                damage += damage * (2 * sc_data[SC_TRUESIGHT].val1) / 100;
                damage2 += damage2 * (2 * sc_data[SC_TRUESIGHT].val1) / 100;
            }
            if (sc_data[SC_BERSERK].timer != -1)
            {                   // �o�[�T�[�N
                damage += damage * 50 / 100;
                damage2 += damage2 * 50 / 100;
            }
        }

        if (skill_num > 0)
        {
            int  i;
            if ((i = skill_get_pl (skill_num)) > 0)
                s_ele = s_ele_ = i;

            flag = (flag & ~BF_SKILLMASK) | BF_SKILL;
            switch (skill_num)
            {
                case SM_BASH:  // �o�b�V��
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 30 * skill_lv) / 100;
                    hitrate = (hitrate * (100 + 5 * skill_lv)) / 100;
                    break;
                case SM_MAGNUM:    // �}�O�i���u���C�N
                    damage =
                        damage * (5 * skill_lv + (wflag) ? 65 : 115) / 100;
                    damage2 =
                        damage2 * (5 * skill_lv + (wflag) ? 65 : 115) / 100;
                    break;
                case MC_MAMMONITE: // ���}�[�i�C�g
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                    break;
                case AC_DOUBLE:    // �_�u���X�g���C�t�B���O
                    if (!sd->state.arrow_atk && sd->arrow_atk > 0)
                    {
                        int  arr = MRAND ((sd->arrow_atk + 1));
                        damage += arr;
                        damage2 += arr;
                    }
                    damage = damage * (180 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (180 + 20 * skill_lv) / 100;
                    div_ = 2;
                    if (sd->arrow_ele > 0)
                    {
                        s_ele = sd->arrow_ele;
                        s_ele_ = sd->arrow_ele;
                    }
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    sd->state.arrow_atk = 1;
                    break;
                case AC_SHOWER:    // �A���[�V�����[
                    if (!sd->state.arrow_atk && sd->arrow_atk > 0)
                    {
                        int  arr = MRAND ((sd->arrow_atk + 1));
                        damage += arr;
                        damage2 += arr;
                    }
                    damage = damage * (75 + 5 * skill_lv) / 100;
                    damage2 = damage2 * (75 + 5 * skill_lv) / 100;
                    if (sd->arrow_ele > 0)
                    {
                        s_ele = sd->arrow_ele;
                        s_ele_ = sd->arrow_ele;
                    }
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    sd->state.arrow_atk = 1;
                    break;
                case AC_CHARGEARROW:   // �`���[�W�A���[
                    if (!sd->state.arrow_atk && sd->arrow_atk > 0)
                    {
                        int  arr = MRAND ((sd->arrow_atk + 1));
                        damage += arr;
                        damage2 += arr;
                    }
                    damage = damage * 150 / 100;
                    damage2 = damage2 * 150 / 100;
                    if (sd->arrow_ele > 0)
                    {
                        s_ele = sd->arrow_ele;
                        s_ele_ = sd->arrow_ele;
                    }
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    sd->state.arrow_atk = 1;
                    break;
                case KN_PIERCE:    // �s�A�[�X
                    damage = damage * (100 + 10 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 10 * skill_lv) / 100;
                    hitrate = hitrate * (100 + 5 * skill_lv) / 100;
                    div_ = t_size + 1;
                    damage *= div_;
                    damage2 *= div_;
                    break;
                case KN_SPEARSTAB: // �X�s�A�X�^�u
                    damage = damage * (100 + 15 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 15 * skill_lv) / 100;
                    break;
                case KN_SPEARBOOMERANG:    // �X�s�A�u�[������
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case KN_BRANDISHSPEAR: // �u�����f�B�b�V���X�s�A
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    if (skill_lv > 3 && wflag == 1)
                        damage3 += damage / 2;
                    if (skill_lv > 6 && wflag == 1)
                        damage3 += damage / 4;
                    if (skill_lv > 9 && wflag == 1)
                        damage3 += damage / 8;
                    if (skill_lv > 6 && wflag == 2)
                        damage3 += damage / 2;
                    if (skill_lv > 9 && wflag == 2)
                        damage3 += damage / 4;
                    if (skill_lv > 9 && wflag == 3)
                        damage3 += damage / 2;
                    damage += damage3;
                    if (skill_lv > 3 && wflag == 1)
                        damage4 += damage2 / 2;
                    if (skill_lv > 6 && wflag == 1)
                        damage4 += damage2 / 4;
                    if (skill_lv > 9 && wflag == 1)
                        damage4 += damage2 / 8;
                    if (skill_lv > 6 && wflag == 2)
                        damage4 += damage2 / 2;
                    if (skill_lv > 9 && wflag == 2)
                        damage4 += damage2 / 4;
                    if (skill_lv > 9 && wflag == 3)
                        damage4 += damage2 / 2;
                    damage2 += damage4;
                    blewcount = 0;
                    break;
                case KN_BOWLINGBASH:   // �{�E�����O�o�b�V��
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                    blewcount = 0;
                    break;
                case KN_AUTOCOUNTER:
                    if (battle_config.pc_auto_counter_type & 1)
                        hitrate += 20;
                    else
                        hitrate = 1000000;
                    flag = (flag & ~BF_SKILLMASK) | BF_NORMAL;
                    break;
                case AS_SONICBLOW: // �\�j�b�N�u���E
                    hitrate += 30;  // hitrate +30, thanks to midas
                    damage = damage * (300 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (300 + 50 * skill_lv) / 100;
                    div_ = 8;
                    break;
                case TF_SPRINKLESAND:  // ���܂�
                    damage = damage * 125 / 100;
                    damage2 = damage2 * 125 / 100;
                    break;
                case MC_CARTREVOLUTION:    // �J�[�g���{�����[�V����
                    if (sd->cart_max_weight > 0 && sd->cart_weight > 0)
                    {
                        damage =
                            (damage *
                             (150 + pc_checkskill (sd, BS_WEAPONRESEARCH) +
                              (sd->cart_weight * 100 /
                               sd->cart_max_weight))) / 100;
                        damage2 =
                            (damage2 *
                             (150 + pc_checkskill (sd, BS_WEAPONRESEARCH) +
                              (sd->cart_weight * 100 /
                               sd->cart_max_weight))) / 100;
                    }
                    else
                    {
                        damage = (damage * 150) / 100;
                        damage2 = (damage2 * 150) / 100;
                    }
                    break;
                    // �ȉ�MOB
                case NPC_COMBOATTACK:  // ���i�U��
                    div_ = skill_get_num (skill_num, skill_lv);
                    damage *= div_;
                    damage2 *= div_;
                    break;
                case NPC_RANDOMATTACK: // �����_��ATK�U��
                    damage = damage * (MPRAND (50, 150)) / 100;
                    damage2 = damage2 * (MPRAND (50, 150)) / 100;
                    break;
                    // �����U���i�K���j
                case NPC_WATERATTACK:
                case NPC_GROUNDATTACK:
                case NPC_FIREATTACK:
                case NPC_WINDATTACK:
                case NPC_POISONATTACK:
                case NPC_HOLYATTACK:
                case NPC_DARKNESSATTACK:
                case NPC_TELEKINESISATTACK:
                    damage = damage * (100 + 25 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 25 * skill_lv) / 100;
                    break;
                case NPC_GUIDEDATTACK:
                    hitrate = 1000000;
                    break;
                case NPC_RANGEATTACK:
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    break;
                case NPC_PIERCINGATT:
                    flag = (flag & ~BF_RANGEMASK) | BF_SHORT;
                    break;
                case RG_BACKSTAP:  // �o�b�N�X�^�u
                    if (battle_config.backstab_bow_penalty == 1
                        && sd->status.weapon == 11)
                    {
                        damage = (damage * (300 + 40 * skill_lv) / 100) / 2;
                        damage2 = (damage2 * (300 + 40 * skill_lv) / 100) / 2;
                    }
                    else
                    {
                        damage = damage * (300 + 40 * skill_lv) / 100;
                        damage2 = damage2 * (300 + 40 * skill_lv) / 100;
                    }
                    hitrate = 1000000;
                    break;
                case RG_RAID:  // �T�v���C�Y�A�^�b�N
                    damage = damage * (100 + 40 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 40 * skill_lv) / 100;
                    break;
                case RG_INTIMIDATE:    // �C���e�B�~�f�C�g
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 30 * skill_lv) / 100;
                    break;
                case CR_SHIELDCHARGE:  // �V�[���h�`���[�W
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_SHORT;
                    s_ele = 0;
                    break;
                case CR_SHIELDBOOMERANG:   // �V�[���h�u�[������
                    damage = damage * (100 + 30 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 30 * skill_lv) / 100;
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    s_ele = 0;
                    break;
                case CR_HOLYCROSS: // �z�[���[�N���X
                    damage = damage * (100 + 35 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 35 * skill_lv) / 100;
                    div_ = 2;
                    break;
                case CR_GRANDCROSS:
                    hitrate = 1000000;
                    break;
                case AM_DEMONSTRATION: // �f�����X�g���[�V����
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    break;
                case AM_ACIDTERROR:    // �A�V�b�h�e���[
                    damage = damage * (100 + 40 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 40 * skill_lv) / 100;
                    break;
                case MO_FINGEROFFENSIVE:   //�w�e
                    if (battle_config.finger_offensive_type == 0)
                    {
                        damage =
                            damage * (100 +
                                      50 * skill_lv) / 100 *
                            sd->spiritball_old;
                        damage2 =
                            damage2 * (100 +
                                       50 * skill_lv) / 100 *
                            sd->spiritball_old;
                        div_ = sd->spiritball_old;
                    }
                    else
                    {
                        damage = damage * (100 + 50 * skill_lv) / 100;
                        damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                        div_ = 1;
                    }
                    break;
                case MO_INVESTIGATE:   // �� ��
                    if (def1 < 1000000)
                    {
                        damage =
                            damage * (100 + 75 * skill_lv) / 100 * (def1 +
                                                                    def2) /
                            100;
                        damage2 =
                            damage2 * (100 + 75 * skill_lv) / 100 * (def1 +
                                                                     def2) /
                            100;
                    }
                    hitrate = 1000000;
                    s_ele = 0;
                    s_ele_ = 0;
                    break;
                case MO_EXTREMITYFIST: // ���C���e�P��
                    damage =
                        damage * (8 + ((sd->status.sp) / 10)) + 250 +
                        (skill_lv * 150);
                    damage2 =
                        damage2 * (8 + ((sd->status.sp) / 10)) + 250 +
                        (skill_lv * 150);
                    sd->status.sp = 0;
                    clif_updatestatus (sd, SP_SP);
                    hitrate = 1000000;
                    s_ele = 0;
                    s_ele_ = 0;
                    break;
                case MO_CHAINCOMBO:    // �A�ŏ�
                    damage = damage * (150 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (150 + 50 * skill_lv) / 100;
                    div_ = 4;
                    break;
                case MO_COMBOFINISH:   // �җ���
                    damage = damage * (240 + 60 * skill_lv) / 100;
                    damage2 = damage2 * (240 + 60 * skill_lv) / 100;
                    break;
                case BA_MUSICALSTRIKE: // �~���[�W�J���X�g���C�N
                    if (!sd->state.arrow_atk && sd->arrow_atk > 0)
                    {
                        int  arr = MRAND ((sd->arrow_atk + 1));
                        damage += arr;
                        damage2 += arr;
                    }
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                    if (sd->arrow_ele > 0)
                    {
                        s_ele = sd->arrow_ele;
                        s_ele_ = sd->arrow_ele;
                    }
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    sd->state.arrow_atk = 1;
                    break;
                case DC_THROWARROW:    // ���
                    if (!sd->state.arrow_atk && sd->arrow_atk > 0)
                    {
                        int  arr = MRAND ((sd->arrow_atk + 1));
                        damage += arr;
                        damage2 += arr;
                    }
                    damage = damage * (100 + 50 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;
                    if (sd->arrow_ele > 0)
                    {
                        s_ele = sd->arrow_ele;
                        s_ele_ = sd->arrow_ele;
                    }
                    flag = (flag & ~BF_RANGEMASK) | BF_LONG;
                    sd->state.arrow_atk = 1;
                    break;
                case CH_TIGERFIST: // ���Ռ�
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    break;
                case CH_CHAINCRUSH:    // �A������
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    div_ = skill_get_num (skill_num, skill_lv);
                    break;
                case CH_PALMSTRIKE:    // �ҌՍd�h�R
                    damage = damage * (50 + 100 * skill_lv) / 100;
                    damage2 = damage2 * (50 + 100 * skill_lv) / 100;
                    break;
                case LK_SPIRALPIERCE:  /* �X�p�C�����s�A�[�X */
                    damage = damage * (100 + 50 * skill_lv) / 100;  //�����ʂ�������Ȃ��̂œK����
                    damage2 = damage2 * (100 + 50 * skill_lv) / 100;    //�����ʂ�������Ȃ��̂œK����
                    div_ = 5;
                    if (tsd)
                        tsd->canmove_tick = gettick () + 1000;
                    else if (tmd)
                        tmd->canmove_tick = gettick () + 1000;
                    break;
                case LK_HEADCRUSH: /* �w�b�h�N���b�V�� */
                    damage = damage * (100 + 20 * skill_lv) / 100;
                    damage2 = damage2 * (100 + 20 * skill_lv) / 100;
                    break;
                case LK_JOINTBEAT: /* �W���C���g�r�[�g */
                    damage = damage * (50 + 10 * skill_lv) / 100;
                    damage2 = damage2 * (50 + 10 * skill_lv) / 100;
                    break;
                case ASC_METEORASSAULT:    /* ���e�I�A�T���g */
                    damage = damage * (40 + 40 * skill_lv) / 100;
                    damage2 = damage2 * (40 + 40 * skill_lv) / 100;
                    break;
                case SN_SHARPSHOOTING: /* �V���[�v�V���[�e�B���O */
                    damage += damage * (30 * skill_lv) / 100;
                    damage2 += damage2 * (30 * skill_lv) / 100;
                    break;
                case CG_ARROWVULCAN:   /* �A���[�o���J�� */
                    damage = damage * (160 + 40 * skill_lv) / 100;
                    damage2 = damage2 * (160 + 40 * skill_lv) / 100;
                    div_ = 9;
                    break;
                case AS_SPLASHER:  /* �x�i���X�v���b�V���[ */
                    damage =
                        damage * (200 + 20 * skill_lv +
                                  20 * pc_checkskill (sd,
                                                      AS_POISONREACT)) / 100;
                    damage2 =
                        damage2 * (200 + 20 * skill_lv +
                                   20 * pc_checkskill (sd,
                                                       AS_POISONREACT)) / 100;
                    break;
                case PA_SACRIFICE:
                    if (sd)
                    {
                        int  hp, mhp, damage3;
                        hp = battle_get_hp (src);
                        mhp = battle_get_max_hp (src);
                        damage3 = mhp * ((skill_lv / 2) + (50 / 100)) / 100;
                        damage =
                            (((skill_lv * 15) + 90) / 100) * damage3 / 100;
                        damage2 =
                            (((skill_lv * 15) + 90) / 100) * damage3 / 100;
                    }
                    break;
                case ASC_BREAKER:  // -- moonsoul (special damage for ASC_BREAKER skill)
                    if (sd)
                    {
                        int  damage3;
                        int  mdef1 = battle_get_mdef (target);
                        int  mdef2 = battle_get_mdef2 (target);
                        int  imdef_flag = 0;

                        damage =
                            ((damage * 5) +
                             (skill_lv * battle_get_int (src) * 5) +
                             MRAND (500) + 500) / 2;
                        damage2 =
                            ((damage2 * 5) +
                             (skill_lv * battle_get_int (src) * 5) +
                             MRAND (500) + 500) / 2;
                        damage3 = damage;
                        hitrate = 1000000;

                        if (sd->ignore_mdef_ele & (1 << t_ele)
                            || sd->ignore_mdef_race & (1 << t_race))
                            imdef_flag = 1;
                        if (t_mode & 0x20)
                        {
                            if (sd->ignore_mdef_race & (1 << 10))
                                imdef_flag = 1;
                        }
                        else
                        {
                            if (sd->ignore_mdef_race & (1 << 11))
                                imdef_flag = 1;
                        }
                        if (!imdef_flag)
                        {
                            if (battle_config.magic_defense_type)
                            {
                                damage3 =
                                    damage3 -
                                    (mdef1 *
                                     battle_config.magic_defense_type) -
                                    mdef2;
                            }
                            else
                            {
                                damage3 =
                                    (damage3 * (100 - mdef1)) / 100 - mdef2;
                            }
                        }

                        if (damage3 < 1)
                            damage3 = 1;

                        damage3 =
                            battle_attr_fix (damage2, s_ele_,
                                             battle_get_element (target));
                    }
                    break;
            }
        }
        if (da == 2)
        {                       //�O�i�����������Ă��邩
            type = 0x08;
            div_ = 255;         //�O�i���p�Ɂc
            damage =
                damage * (100 +
                          20 * pc_checkskill (sd, MO_TRIPLEATTACK)) / 100;
        }

        if (skill_num != NPC_CRITICALSLASH)
        {
            // �� �ۂ̖h��͂ɂ��_���[�W�̌���
            // �f�B�o�C���v���e�N�V�����i�����ł����̂��ȁH�j
            if (skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST
                && skill_num != KN_AUTOCOUNTER && def1 < 1000000)
            {                   //DEF, VIT����
                int  t_def;
                target_count =
                    1 + battle_counttargeted (target, src,
                                              battle_config.vit_penaly_count_lv);
                if (battle_config.vit_penaly_type > 0)
                {
                    if (target_count >= battle_config.vit_penaly_count)
                    {
                        if (battle_config.vit_penaly_type == 1)
                        {
                            def1 =
                                (def1 *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                            def2 =
                                (def2 *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                            t_vit =
                                (t_vit *
                                 (100 -
                                  (target_count -
                                   (battle_config.vit_penaly_count -
                                    1)) * battle_config.vit_penaly_num)) /
                                100;
                        }
                        else if (battle_config.vit_penaly_type == 2)
                        {
                            def1 -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                            def2 -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                            t_vit -=
                                (target_count -
                                 (battle_config.vit_penaly_count -
                                  1)) * battle_config.vit_penaly_num;
                        }
                        if (def1 < 0)
                            def1 = 0;
                        if (def2 < 1)
                            def2 = 1;
                        if (t_vit < 1)
                            t_vit = 1;
                    }
                }
                t_def = def2 * 8 / 10;
                vitbonusmax = (t_vit / 20) * (t_vit / 20) - 1;
                if (sd->ignore_def_ele & (1 << t_ele)
                    || sd->ignore_def_race & (1 << t_race))
                    idef_flag = 1;
                if (sd->ignore_def_ele_ & (1 << t_ele)
                    || sd->ignore_def_race_ & (1 << t_race))
                    idef_flag_ = 1;
                if (t_mode & 0x20)
                {
                    if (sd->ignore_def_race & (1 << 10))
                        idef_flag = 1;
                    if (sd->ignore_def_race_ & (1 << 10))
                        idef_flag_ = 1;
                }
                else
                {
                    if (sd->ignore_def_race & (1 << 11))
                        idef_flag = 1;
                    if (sd->ignore_def_race_ & (1 << 11))
                        idef_flag_ = 1;
                }

                if (!idef_flag)
                {
                    if (battle_config.player_defense_type)
                    {
                        damage =
                            damage -
                            (def1 * battle_config.player_defense_type) -
                            t_def -
                            ((vitbonusmax <
                              1) ? 0 : MRAND ((vitbonusmax + 1)));
                    }
                    else
                    {
                        damage =
                            damage * (100 - def1) / 100 - t_def -
                            ((vitbonusmax <
                              1) ? 0 : MRAND ((vitbonusmax + 1)));
                    }
                }
                if (!idef_flag_)
                {
                    if (battle_config.player_defense_type)
                    {
                        damage2 =
                            damage2 -
                            (def1 * battle_config.player_defense_type) -
                            t_def -
                            ((vitbonusmax <
                              1) ? 0 : MRAND ((vitbonusmax + 1)));
                    }
                    else
                    {
                        damage2 =
                            damage2 * (100 - def1) / 100 - t_def -
                            ((vitbonusmax <
                              1) ? 0 : MRAND ((vitbonusmax + 1)));
                    }
                }
            }
        }
    }
    // ���B�_���[�W�̒ǉ�
    if (skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST)
    {                           //DEF, VIT����
        damage += battle_get_atk2 (src);
        damage2 += battle_get_atk_2 (src);
    }
    if (skill_num == CR_SHIELDBOOMERANG)
    {
        if (sd->equip_index[8] >= 0)
        {
            int  index = sd->equip_index[8];
            if (sd->inventory_data[index]
                && sd->inventory_data[index]->type == 5)
            {
                damage += sd->inventory_data[index]->weight / 10;
                damage +=
                    sd->status.inventory[index].refine * pc_getrefinebonus (0,
                                                                            1);
            }
        }
    }
    if (skill_num == LK_SPIRALPIERCE)
    {                           /* �X�p�C�����s�A�[�X */
        if (sd->equip_index[9] >= 0)
        {                       //�d�ʂŒǉ��_���[�W�炵���̂ŃV�[���h�u�[���������Q�l�ɒǉ�
            int  index = sd->equip_index[9];
            if (sd->inventory_data[index]
                && sd->inventory_data[index]->type == 4)
            {
                damage +=
                    (int) (double) (sd->inventory_data[index]->weight *
                                    (0.8 * skill_lv * 4 / 10));
                damage +=
                    sd->status.inventory[index].refine * pc_getrefinebonus (0,
                                                                            1);
            }
        }
    }

    // 0�����������ꍇ1�ɕ␳
    if (damage < 1)
        damage = 1;
    if (damage2 < 1)
        damage2 = 1;

    // �X�L���C���Q�i�C���n�j
    // �C���_���[�W(�E��̂�) �\�j�b�N�u���[���͕ʏ����i1���ɕt��1/8�K��)
    if (skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST
        && skill_num != CR_GRANDCROSS)
    {                           //�C���_���[�W����
        damage = battle_addmastery (sd, target, damage, 0);
        damage2 = battle_addmastery (sd, target, damage2, 1);
    }

    if (sd->perfect_hit > 0)
    {
        if (MRAND (100) < sd->perfect_hit)
            hitrate = 1000000;
    }

    // ����C��
    hitrate = (hitrate < 5) ? 5 : hitrate;
    if (hitrate < 1000000 &&    // �K���U��
        (t_sc_data != NULL && (t_sc_data[SC_SLEEP].timer != -1 ||   // �����͕K��
                               t_sc_data[SC_STAN].timer != -1 ||    // �X�^���͕K��
                               t_sc_data[SC_FREEZE].timer != -1 || (t_sc_data[SC_STONE].timer != -1 && t_sc_data[SC_STONE].val2 == 0))))    // �����͕K��
        hitrate = 1000000;
    if (type == 0 && MRAND (100) >= hitrate)
    {
        damage = damage2 = 0;
        dmg_lv = ATK_FLEE;
    }
    else
    {
        dmg_lv = ATK_DEF;
    }
    // �X�L���C���R�i���팤���j
    if ((skill = pc_checkskill (sd, BS_WEAPONRESEARCH)) > 0)
    {
        damage += skill * 2;
        damage2 += skill * 2;
    }
    //Advanced Katar Research by zanetheinsane
    if (sd->weapontype1 == 0x10 || sd->weapontype2 == 0x10)
    {
        if ((skill = pc_checkskill (sd, ASC_KATAR)) > 0)
        {
            damage += (damage * ((skill * 2) + 10)) / 100;
        }
    }

//�X�L���ɂ��_���[�W�␳�����܂�

//�J�[�h�ɂ��_���[�W�ǉ�������������
    cardfix = 100;
    if (!sd->state.arrow_atk)
    {                           //�|��ȊO
        if (!battle_config.left_cardfix_to_right)
        {                       //����J�[�h�␳�ݒ薳��
            cardfix = cardfix * (100 + sd->addrace[t_race]) / 100;  // �푰�ɂ��_���[�W�C��
            cardfix = cardfix * (100 + sd->addele[t_ele]) / 100;    // �����ɂ��_���[�W�C��
            cardfix = cardfix * (100 + sd->addsize[t_size]) / 100;  // �T�C�Y�ɂ��_���[�W�C��
        }
        else
        {
            cardfix = cardfix * (100 + sd->addrace[t_race] + sd->addrace_[t_race]) / 100;   // �푰�ɂ��_���[�W�C��(����ɂ��ǉ�����)
            cardfix = cardfix * (100 + sd->addele[t_ele] + sd->addele_[t_ele]) / 100;   // �����ɂ��_���[�W�C��(����ɂ��ǉ�����)
            cardfix = cardfix * (100 + sd->addsize[t_size] + sd->addsize_[t_size]) / 100;   // �T�C�Y�ɂ��_���[�W�C��(����ɂ��ǉ�����)
        }
    }
    else
    {                           //�|��
        cardfix = cardfix * (100 + sd->addrace[t_race] + sd->arrow_addrace[t_race]) / 100;  // �푰�ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
        cardfix = cardfix * (100 + sd->addele[t_ele] + sd->arrow_addele[t_ele]) / 100;  // �����ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
        cardfix = cardfix * (100 + sd->addsize[t_size] + sd->arrow_addsize[t_size]) / 100;  // �T�C�Y�ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
    }
    if (t_mode & 0x20)
    {                           //�{�X
        if (!sd->state.arrow_atk)
        {                       //�|��U���ȊO�Ȃ�
            if (!battle_config.left_cardfix_to_right)   //����J�[�h�␳�ݒ薳��
                cardfix = cardfix * (100 + sd->addrace[10]) / 100;  //�{�X�����X�^�[�ɒǉ��_���[�W
            else                //����J�[�h�␳�ݒ肠��
                cardfix = cardfix * (100 + sd->addrace[10] + sd->addrace_[10]) / 100;   //�{�X�����X�^�[�ɒǉ��_���[�W(����ɂ��ǉ�����)
        }
        else                    //�|��U��
            cardfix = cardfix * (100 + sd->addrace[10] + sd->arrow_addrace[10]) / 100;  //�{�X�����X�^�[�ɒǉ��_���[�W(�|��ɂ��ǉ�����)
    }
    else
    {                           //�{�X����Ȃ�
        if (!sd->state.arrow_atk)
        {                       //�|��U���ȊO
            if (!battle_config.left_cardfix_to_right)   //����J�[�h�␳�ݒ薳��
                cardfix = cardfix * (100 + sd->addrace[11]) / 100;  //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W
            else                //����J�[�h�␳�ݒ肠��
                cardfix = cardfix * (100 + sd->addrace[11] + sd->addrace_[11]) / 100;   //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W(����ɂ��ǉ�����)
        }
        else
            cardfix = cardfix * (100 + sd->addrace[11] + sd->arrow_addrace[11]) / 100;  //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W(�|��ɂ��ǉ�����)
    }
    //����Class�p�␳����(�����̓��L���{���S���p�H)
    t_class = battle_get_class (target);
    for (i = 0; i < sd->add_damage_class_count; i++)
    {
        if (sd->add_damage_classid[i] == t_class)
        {
            cardfix = cardfix * (100 + sd->add_damage_classrate[i]) / 100;
            break;
        }
    }
    if (skill_num != CR_GRANDCROSS || !battle_config.gx_cardfix)
        damage = damage * cardfix / 100;    //�J�[�h�␳�ɂ��_���[�W����
//�J�[�h�ɂ��_���[�W�������������܂�

//�J�[�h�ɂ��_���[�W�ǉ�����(����)��������
    cardfix = 100;
    if (!battle_config.left_cardfix_to_right)
    {                           //����J�[�h�␳�ݒ薳��
        cardfix = cardfix * (100 + sd->addrace_[t_race]) / 100; // �푰�ɂ��_���[�W�C������
        cardfix = cardfix * (100 + sd->addele_[t_ele]) / 100;   // �� ���ɂ��_���[�W�C������
        cardfix = cardfix * (100 + sd->addsize_[t_size]) / 100; // �T�C�Y�ɂ��_���[�W�C������
        if (t_mode & 0x20)      //�{�X
            cardfix = cardfix * (100 + sd->addrace_[10]) / 100; //�{�X�����X�^�[�ɒǉ��_���[�W����
        else
            cardfix = cardfix * (100 + sd->addrace_[11]) / 100; //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W����
    }
    //����Class�p�␳��������(�����̓��L���{���S���p�H)
    for (i = 0; i < sd->add_damage_class_count_; i++)
    {
        if (sd->add_damage_classid_[i] == t_class)
        {
            cardfix = cardfix * (100 + sd->add_damage_classrate_[i]) / 100;
            break;
        }
    }
    if (skill_num != CR_GRANDCROSS)
        damage2 = damage2 * cardfix / 100;  //�J�[�h�␳�ɂ�鍶��_���[�W����
//�J�[�h�ɂ��_���[�W��������(����)�����܂�

// -- moonsoul (cardfix for magic damage portion of ASC_BREAKER)
    if (skill_num == ASC_BREAKER)
        damage3 = damage3 * cardfix / 100;

//�J�[�h�ɂ��_���[�W����������������
    if (tsd)
    {                           //�Ώۂ�PC�̏ꍇ
        cardfix = 100;
        cardfix = cardfix * (100 - tsd->subrace[s_race]) / 100; // �푰�ɂ��_���[�W�ϐ�
        cardfix = cardfix * (100 - tsd->subele[s_ele]) / 100;   // �����ɂ��_���[�W�ϐ�
        if (battle_get_mode (src) & 0x20)
            cardfix = cardfix * (100 - tsd->subrace[10]) / 100; //�{�X����̍U���̓_���[�W����
        else
            cardfix = cardfix * (100 - tsd->subrace[11]) / 100; //�{�X�ȊO����̍U���̓_���[�W����
        //����Class�p�␳��������(�����̓��L���{���S���p�H)
        for (i = 0; i < tsd->add_def_class_count; i++)
        {
            if (tsd->add_def_classid[i] == sd->status.class)
            {
                cardfix = cardfix * (100 - tsd->add_def_classrate[i]) / 100;
                break;
            }
        }
        if (flag & BF_LONG)
            cardfix = cardfix * (100 - tsd->long_attack_def_rate) / 100;    //�������U���̓_���[�W����(�z����C�Ƃ�)
        if (flag & BF_SHORT)
            cardfix = cardfix * (100 - tsd->near_attack_def_rate) / 100;    //�ߋ����U���̓_���[�W����(�Y�������H)
        damage = damage * cardfix / 100;    //�J�[�h�␳�ɂ��_���[�W����
        damage2 = damage2 * cardfix / 100;  //�J�[�h�␳�ɂ�鍶��_���[�W����
    }
//�J�[�h�ɂ��_���[�W�������������܂�

//�ΏۂɃX�e�[�^�X�ُ킪����ꍇ�̃_���[�W���Z������������
    if (t_sc_data)
    {
        cardfix = 100;
        if (t_sc_data[SC_DEFENDER].timer != -1 && flag & BF_LONG)   //�f�B�t�F���_�[��Ԃŉ������U��
            cardfix = cardfix * (100 - t_sc_data[SC_DEFENDER].val2) / 100;  //�f�B�t�F���_�[�ɂ�錸��
        if (cardfix != 100)
        {
            damage = damage * cardfix / 100;    //�f�B�t�F���_�[�␳�ɂ��_���[�W����
            damage2 = damage2 * cardfix / 100;  //�f�B�t�F���_�[�␳�ɂ�鍶��_���[�W����
        }
        if (t_sc_data[SC_ASSUMPTIO].timer != -1)
        {                       //�A�X���v�e�B�I
            if (!map[target->m].flag.pvp)
            {
                damage = damage / 3;
                damage2 = damage2 / 3;
            }
            else
            {
                damage = damage / 2;
                damage2 = damage2 / 2;
            }
        }
    }
//�ΏۂɃX�e�[�^�X�ُ킪����ꍇ�̃_���[�W���Z���������܂�

    if (damage < 0)
        damage = 0;
    if (damage2 < 0)
        damage2 = 0;

    // �� ���̓K�p
    damage = battle_attr_fix (damage, s_ele, battle_get_element (target));
    damage2 = battle_attr_fix (damage2, s_ele_, battle_get_element (target));

    // ���̂�����A�C���̓K�p
    damage += sd->star;
    damage2 += sd->star_;
    damage += sd->spiritball * 3;
    damage2 += sd->spiritball * 3;

    if (sc_data && sc_data[SC_AURABLADE].timer != -1)
    {                           /* �I�[���u���[�h �K�� */
        damage += sc_data[SC_AURABLADE].val1 * 10;
        damage2 += sc_data[SC_AURABLADE].val1 * 10;
    }
    if (skill_num == PA_PRESSURE)
    {                           /* �v���b�V���[ �K��? */
        damage = 700 + 100 * skill_lv;
        damage2 = 700 + 100 * skill_lv;
    }

    // >�񓁗��̍��E�_���[�W�v�Z�N������Ă��ꂥ�������������I
    // >map_session_data �ɍ���_���[�W(atk,atk2)�ǉ�����
    // >pc_calcstatus()�ł��ׂ����ȁH
    // map_session_data �ɍ��蕐��(atk,atk2,ele,star,atkmods)�ǉ�����
    // pc_calcstatus()�Ńf�[�^����͂��Ă��܂�

    //����̂ݕ��푕��
    if (sd->weapontype1 == 0 && sd->weapontype2 > 0)
    {
        damage = damage2;
        damage2 = 0;
    }
    // �E��A����C���̓K�p
    if (sd->status.weapon > 16)
    {                           // �񓁗���?
        int  dmg = damage, dmg2 = damage2;
        // �E��C��(60% �` 100%) �E��S��
        skill = pc_checkskill (sd, AS_RIGHT);
        damage = damage * (50 + (skill * 10)) / 100;
        if (dmg > 0 && damage < 1)
            damage = 1;
        // ����C��(40% �` 80%) ����S��
        skill = pc_checkskill (sd, AS_LEFT);
        damage2 = damage2 * (30 + (skill * 10)) / 100;
        if (dmg2 > 0 && damage2 < 1)
            damage2 = 1;
    }
    else                        //�񓁗��łȂ���΍���_���[�W��0
        damage2 = 0;

    // �E��,�Z���̂�
    if (da == 1)
    {                           //�_�u���A�^�b�N���������Ă��邩
        div_ = 2;
        damage += damage;
        type = 0x08;
    }

    if (sd->status.weapon == 16)
    {
        // �J�^�[���ǌ��_���[�W
        skill = pc_checkskill (sd, TF_DOUBLE);
        damage2 = damage * (1 + (skill * 2)) / 100;
        if (damage > 0 && damage2 < 1)
            damage2 = 1;
    }

    // �C���x�i���C��
    if (skill_num == TF_POISON)
    {
        damage =
            battle_attr_fix (damage + 15 * skill_lv, s_ele,
                             battle_get_element (target));
    }
    if (skill_num == MC_CARTREVOLUTION)
    {
        damage = battle_attr_fix (damage, 0, battle_get_element (target));
    }

    // ���S����̔���
    if (skill_num == 0 && skill_lv >= 0 && tsd != NULL && div_ < 255
        && MRAND (1000) < battle_get_flee2 (target))
    {
        damage = damage2 = 0;
        type = 0x0b;
        dmg_lv = ATK_LUCKY;
    }

    // �Ώۂ����S���������ݒ肪ON�Ȃ�
    if (battle_config.enemy_perfect_flee)
    {
        if (skill_num == 0 && skill_lv >= 0 && tmd != NULL && div_ < 255
            && MRAND (1000) < battle_get_flee2 (target))
        {
            damage = damage2 = 0;
            type = 0x0b;
            dmg_lv = ATK_LUCKY;
        }
    }

    //Mob��Mode�Ɋ拭�t���O�������Ă���Ƃ��̏���
    if (t_mode & 0x40)
    {
        if (damage > 0)
            damage = 1;
        if (damage2 > 0)
            damage2 = 1;
    }

    //bNoWeaponDamage(�ݒ�A�C�e�������H)�ŃO�����h�N���X����Ȃ��ꍇ�̓_���[�W��0
    if (tsd && tsd->special_state.no_weapon_damage
        && skill_num != CR_GRANDCROSS)
        damage = damage2 = 0;

    if (skill_num != CR_GRANDCROSS && (damage > 0 || damage2 > 0))
    {
        if (damage2 < 1)        // �_���[�W�ŏI�C��
            damage =
                battle_calc_damage (src, target, damage, div_, skill_num,
                                    skill_lv, flag);
        else if (damage < 1)    // �E�肪�~�X�H
            damage2 =
                battle_calc_damage (src, target, damage2, div_, skill_num,
                                    skill_lv, flag);
        else
        {                       // �� ��/�J�^�[���̏ꍇ�͂�����ƌv�Z��₱����
            int  d1 = damage + damage2, d2 = damage2;
            damage =
                battle_calc_damage (src, target, damage + damage2, div_,
                                    skill_num, skill_lv, flag);
            damage2 = (d2 * 100 / d1) * damage / 100;
            if (damage > 1 && damage2 < 1)
                damage2 = 1;
            damage -= damage2;
        }
    }

    /*              For executioner card [Valaris]              */
    if (src->type == BL_PC && sd->random_attack_increase_add > 0
        && sd->random_attack_increase_per > 0 && skill_num == 0)
    {
        if (MRAND (100) < sd->random_attack_increase_per)
        {
            if (damage > 0)
                damage *= sd->random_attack_increase_add / 100;
            if (damage2 > 0)
                damage2 *= sd->random_attack_increase_add / 100;
        }
    }
    /*                  End addition                    */

// -- moonsoul (final combination of phys, mag damage for ASC_BREAKER)
    if (skill_num == ASC_BREAKER)
    {
        damage += damage3;
        damage2 += damage3;
    }

    wd.damage = damage;
    wd.damage2 = damage2;
    wd.type = type;
    wd.div_ = div_;
    wd.amotion = battle_get_amotion (src);
    if (skill_num == KN_AUTOCOUNTER)
        wd.amotion >>= 1;
    wd.dmotion = battle_get_dmotion (target);
    wd.blewcount = blewcount;
    wd.flag = flag;
    wd.dmg_lv = dmg_lv;

    return wd;
}

/*==========================================
 * ����_���[�W�v�Z
 *------------------------------------------
 */
struct Damage battle_calc_weapon_attack (struct block_list *src,
                                         struct block_list *target,
                                         int skill_num, int skill_lv,
                                         int wflag)
{
    struct Damage wd;

    //return�O�̏���������̂ŏ��o�͕��̂ݕύX
    if (src == NULL || target == NULL)
    {
        nullpo_info (NLP_MARK);
        memset (&wd, 0, sizeof (wd));
        return wd;
    }

    else if (src->type == BL_PC)
        wd = battle_calc_pc_weapon_attack (src, target, skill_num, skill_lv, wflag);    // weapon breaking [Valaris]
    else if (src->type == BL_MOB)
        wd = battle_calc_mob_weapon_attack (src, target, skill_num, skill_lv,
                                            wflag);
    else
        memset (&wd, 0, sizeof (wd));

    if (battle_config.equipment_breaking && src->type == BL_PC
        && (wd.damage > 0 || wd.damage2 > 0))
    {
        struct map_session_data *sd = (struct map_session_data *) src;
        if (sd->status.weapon && sd->status.weapon != 11)
        {
            int  breakrate = 1;
            if (target->type == BL_PC && sd->sc_data[SC_MELTDOWN].timer != -1)
            {
                breakrate += 100 * sd->sc_data[SC_MELTDOWN].val1;
                if (MRAND (10000) <
                    breakrate * battle_config.equipment_break_rate / 100
                    || breakrate >= 10000)
                    pc_breakweapon ((struct map_session_data *) target);
            }
            if (sd->sc_data[SC_OVERTHRUST].timer != -1)
                breakrate += 20 * sd->sc_data[SC_OVERTHRUST].val1;
            if (wd.type == 0x0a)
                breakrate *= 2;
            if (MRAND (10000) <
                breakrate * battle_config.equipment_break_rate / 100
                || breakrate >= 10000)
            {
                pc_breakweapon (sd);
                memset (&wd, 0, sizeof (wd));
            }
        }
    }

    if (battle_config.equipment_breaking && target->type == BL_PC
        && (wd.damage > 0 || wd.damage2 > 0))
    {
        int  breakrate = 1;
        if (src->type == BL_PC
            && ((struct map_session_data *) src)->
            sc_data[SC_MELTDOWN].timer != -1)
            breakrate +=
                70 *
                ((struct map_session_data *) src)->sc_data[SC_MELTDOWN].val1;
        if (wd.type == 0x0a)
            breakrate *= 2;
        if (MRAND (10000) <
            breakrate * battle_config.equipment_break_rate / 100
            || breakrate >= 10000)
        {
            pc_breakarmor ((struct map_session_data *) target);
        }
    }

    return wd;
}

/*==========================================
 * ���@�_���[�W�v�Z
 *------------------------------------------
 */
struct Damage battle_calc_magic_attack (struct block_list *bl,
                                        struct block_list *target,
                                        int skill_num, int skill_lv, int flag)
{
    int  mdef1 = battle_get_mdef (target);
    int  mdef2 = battle_get_mdef2 (target);
    int  matk1, matk2, damage = 0, div_ = 1, blewcount =
        skill_get_blewcount (skill_num, skill_lv), rdamage = 0;
    struct Damage md;
    int  aflag;
    int  normalmagic_flag = 1;
    int  ele = 0, race = 7, t_ele = 0, t_race = 7, t_mode =
        0, cardfix, t_class, i;
    struct map_session_data *sd = NULL, *tsd = NULL;
    struct mob_data *tmd = NULL;

    //return�O�̏���������̂ŏ��o�͕��̂ݕύX
    if (bl == NULL || target == NULL)
    {
        nullpo_info (NLP_MARK);
        memset (&md, 0, sizeof (md));
        return md;
    }

    matk1 = battle_get_matk1 (bl);
    matk2 = battle_get_matk2 (bl);
    ele = skill_get_pl (skill_num);
    race = battle_get_race (bl);
    t_ele = battle_get_elem_type (target);
    t_race = battle_get_race (target);
    t_mode = battle_get_mode (target);

#define MATK_FIX( a,b ) { matk1=matk1*(a)/(b); matk2=matk2*(a)/(b); }

    if (bl->type == BL_PC && (sd = (struct map_session_data *) bl))
    {
        sd->state.attack_type = BF_MAGIC;
        if (sd->matk_rate != 100)
            MATK_FIX (sd->matk_rate, 100);
        sd->state.arrow_atk = 0;
    }
    if (target->type == BL_PC)
        tsd = (struct map_session_data *) target;
    else if (target->type == BL_MOB)
        tmd = (struct mob_data *) target;

    aflag = BF_MAGIC | BF_LONG | BF_SKILL;

    if (skill_num > 0)
    {
        switch (skill_num)
        {                       // ��{�_���[�W�v�Z(�X�L�����Ƃɏ���)
                // �q�[��or����
            case AL_HEAL:
            case PR_BENEDICTIO:
                damage = skill_calc_heal (bl, skill_lv) / 2;
                normalmagic_flag = 0;
                break;
            case PR_ASPERSIO:  /* �A�X�y���V�I */
                damage = 40;    //�Œ�_���[�W
                normalmagic_flag = 0;
                break;
            case PR_SANCTUARY: // �T���N�`���A��
                damage = (skill_lv > 6) ? 388 : skill_lv * 50;
                normalmagic_flag = 0;
                blewcount |= 0x10000;
                break;
            case ALL_RESURRECTION:
            case PR_TURNUNDEAD:    // �U�����U���N�V�����ƃ^�[���A���f�b�h
                if (target->type != BL_PC
                    && battle_check_undead (t_race, t_ele))
                {
                    int  hp, mhp, thres;
                    hp = battle_get_hp (target);
                    mhp = battle_get_max_hp (target);
                    thres = (skill_lv * 20) + battle_get_luk (bl) +
                        battle_get_int (bl) + battle_get_lv (bl) +
                        ((200 - hp * 200 / mhp));
                    if (thres > 700)
                        thres = 700;
//              if(battle_config.battle_log)
//                  printf("�^�[���A���f�b�h�I �m��%d ��(�番��)\n", thres);
                    if (MRAND (1000) < thres && !(t_mode & 0x20))   // ����
                        damage = hp;
                    else        // ���s
                        damage =
                            battle_get_lv (bl) + battle_get_int (bl) +
                            skill_lv * 10;
                }
                normalmagic_flag = 0;
                break;

            case MG_NAPALMBEAT:    // �i�p�[���r�[�g�i���U�v�Z���݁j
                MATK_FIX (70 + skill_lv * 10, 100);
                if (flag > 0)
                {
                    MATK_FIX (1, flag);
                }
                else
                {
                    if (battle_config.error_log)
                        printf
                            ("battle_calc_magic_attack(): napam enemy count=0 !\n");
                }
                break;
            case MG_FIREBALL:  // �t�@�C���[�{�[��
            {
                const int drate[] = { 100, 90, 70 };
                if (flag > 2)
                    matk1 = matk2 = 0;
                else
                    MATK_FIX ((95 + skill_lv * 5) * drate[flag], 10000);
            }
                break;
            case MG_FIREWALL:  // �t�@�C���[�E�H�[��
/*
			if( (t_ele!=3 && !battle_check_undead(t_race,t_ele)) || target->type==BL_PC ) //PC�͉Α����ł���ԁH���������_���[�W�󂯂�H
				blewcount |= 0x10000;
			else
				blewcount = 0;
*/
                if ((t_ele == 3 || battle_check_undead (t_race, t_ele))
                    && target->type != BL_PC)
                    blewcount = 0;
                else
                    blewcount |= 0x10000;
                MATK_FIX (1, 2);
                break;
            case MG_THUNDERSTORM:  // �T���_�[�X�g�[��
                MATK_FIX (80, 100);
                break;
            case MG_FROSTDIVER:    // �t���X�g�_�C�o
                MATK_FIX (100 + skill_lv * 10, 100);
                break;
            case WZ_FROSTNOVA: // �t���X�g�_�C�o
                MATK_FIX (((100 + skill_lv * 10) * (2 / 3)), 100);
                break;
            case WZ_FIREPILLAR:    // �t�@�C���[�s���[
                if (mdef1 < 1000000)
                    mdef1 = mdef2 = 0;  // MDEF����
                MATK_FIX (1, 5);
                matk1 += 50;
                matk2 += 50;
                break;
            case WZ_SIGHTRASHER:
                MATK_FIX (100 + skill_lv * 20, 100);
                break;
            case WZ_METEOR:
            case WZ_JUPITEL:   // ���s�e���T���_�[
                break;
            case WZ_VERMILION: // ���[�h�I�u�o�[�~���I��
                MATK_FIX (skill_lv * 20 + 80, 100);
                break;
            case WZ_WATERBALL: // �E�H�[�^�[�{�[��
                matk1 += skill_lv * 30;
                matk2 += skill_lv * 30;
                break;
            case WZ_STORMGUST: // �X�g�[���K�X�g
                MATK_FIX (skill_lv * 40 + 100, 100);
                blewcount |= 0x10000;
                break;
            case AL_HOLYLIGHT: // �z�[���[���C�g
                MATK_FIX (125, 100);
                break;
            case AL_RUWACH:
                MATK_FIX (145, 100);
                break;
            case HW_NAPALMVULCAN:  // �i�p�[���r�[�g�i���U�v�Z���݁j
                MATK_FIX (70 + skill_lv * 10, 100);
                if (flag > 0)
                {
                    MATK_FIX (1, flag);
                }
                else
                {
                    if (battle_config.error_log)
                        printf
                            ("battle_calc_magic_attack(): napalmvulcan enemy count=0 !\n");
                }
                break;
        }
    }

    if (normalmagic_flag)
    {                           // ��ʖ��@�_���[�W�v�Z
        int  imdef_flag = 0;
        if (matk1 > matk2)
            damage = matk2 + MRAND ((matk1 - matk2 + 1));
        else
            damage = matk2;
        if (sd)
        {
            if (sd->ignore_mdef_ele & (1 << t_ele)
                || sd->ignore_mdef_race & (1 << t_race))
                imdef_flag = 1;
            if (t_mode & 0x20)
            {
                if (sd->ignore_mdef_race & (1 << 10))
                    imdef_flag = 1;
            }
            else
            {
                if (sd->ignore_mdef_race & (1 << 11))
                    imdef_flag = 1;
            }
        }
        if (!imdef_flag)
        {
            if (battle_config.magic_defense_type)
            {
                damage =
                    damage - (mdef1 * battle_config.magic_defense_type) -
                    mdef2;
            }
            else
            {
                damage = (damage * (100 - mdef1)) / 100 - mdef2;
            }
        }

        if (damage < 1)
            damage = 1;
    }

    if (sd)
    {
        cardfix = 100;
        cardfix = cardfix * (100 + sd->magic_addrace[t_race]) / 100;
        cardfix = cardfix * (100 + sd->magic_addele[t_ele]) / 100;
        if (t_mode & 0x20)
            cardfix = cardfix * (100 + sd->magic_addrace[10]) / 100;
        else
            cardfix = cardfix * (100 + sd->magic_addrace[11]) / 100;
        t_class = battle_get_class (target);
        for (i = 0; i < sd->add_magic_damage_class_count; i++)
        {
            if (sd->add_magic_damage_classid[i] == t_class)
            {
                cardfix =
                    cardfix * (100 + sd->add_magic_damage_classrate[i]) / 100;
                break;
            }
        }
        damage = damage * cardfix / 100;
    }

    if (tsd)
    {
        int  s_class = battle_get_class (bl);
        cardfix = 100;
        cardfix = cardfix * (100 - tsd->subele[ele]) / 100; // �� ���ɂ��_���[�W�ϐ�
        cardfix = cardfix * (100 - tsd->subrace[race]) / 100;   // �푰�ɂ��_���[�W�ϐ�
        cardfix = cardfix * (100 - tsd->magic_subrace[race]) / 100;
        if (battle_get_mode (bl) & 0x20)
            cardfix = cardfix * (100 - tsd->magic_subrace[10]) / 100;
        else
            cardfix = cardfix * (100 - tsd->magic_subrace[11]) / 100;
        for (i = 0; i < tsd->add_mdef_class_count; i++)
        {
            if (tsd->add_mdef_classid[i] == s_class)
            {
                cardfix = cardfix * (100 - tsd->add_mdef_classrate[i]) / 100;
                break;
            }
        }
        cardfix = cardfix * (100 - tsd->magic_def_rate) / 100;
        damage = damage * cardfix / 100;
    }
    if (damage < 0)
        damage = 0;

    damage = battle_attr_fix (damage, ele, battle_get_element (target));    // �� ���C��

    if (skill_num == CR_GRANDCROSS)
    {                           // �O�����h�N���X
        struct Damage wd;
        wd = battle_calc_weapon_attack (bl, target, skill_num, skill_lv,
                                        flag);
        damage = (damage + wd.damage) * (100 + 40 * skill_lv) / 100;
        if (battle_config.gx_dupele)
            damage = battle_attr_fix (damage, ele, battle_get_element (target));    //����2�񂩂���
        if (bl == target)
            damage = damage / 2;    //�����͔���
    }

    div_ = skill_get_num (skill_num, skill_lv);

    if (div_ > 1 && skill_num != WZ_VERMILION)
        damage *= div_;

//  if(mdef1 >= 1000000 && damage > 0)
    if (t_mode & 0x40 && damage > 0)
        damage = 1;

    if (tsd && tsd->special_state.no_magic_damage)
    {
        if (battle_config.gtb_pvp_only != 0)
        {                       // [MouseJstr]
            if ((map[target->m].flag.pvp || map[target->m].flag.gvg)
                && target->type == BL_PC)
                damage = (damage * (100 - battle_config.gtb_pvp_only)) / 100;
        }
        else
            damage = 0;         // �� ��峃J�[�h�i���@�_���[�W�O�j
    }

    damage = battle_calc_damage (bl, target, damage, div_, skill_num, skill_lv, aflag); // �ŏI�C��

    /* magic_damage_return by [AppleGirl] and [Valaris]     */
    if (target->type == BL_PC && tsd && tsd->magic_damage_return > 0)
    {
        rdamage += damage * tsd->magic_damage_return / 100;
        if (rdamage < 1)
            rdamage = 1;
        clif_damage (target, bl, gettick (), 0, 0, rdamage, 0, 0, 0);
        battle_damage (target, bl, rdamage, 0);
    }
    /*          end magic_damage_return         */

    md.damage = damage;
    md.div_ = div_;
    md.amotion = battle_get_amotion (bl);
    md.dmotion = battle_get_dmotion (target);
    md.damage2 = 0;
    md.type = 0;
    md.blewcount = blewcount;
    md.flag = aflag;

    return md;
}

/*==========================================
 * ���̑��_���[�W�v�Z
 *------------------------------------------
 */
struct Damage battle_calc_misc_attack (struct block_list *bl,
                                       struct block_list *target,
                                       int skill_num, int skill_lv, int flag)
{
    int  int_ = battle_get_int (bl);
//  int luk=battle_get_luk(bl);
    int  dex = battle_get_dex (bl);
    int  skill, ele, race, cardfix;
    struct map_session_data *sd = NULL, *tsd = NULL;
    int  damage = 0, div_ = 1, blewcount =
        skill_get_blewcount (skill_num, skill_lv);
    struct Damage md;
    int  damagefix = 1;

    int  aflag = BF_MISC | BF_LONG | BF_SKILL;

    //return�O�̏���������̂ŏ��o�͕��̂ݕύX
    if (bl == NULL || target == NULL)
    {
        nullpo_info (NLP_MARK);
        memset (&md, 0, sizeof (md));
        return md;
    }

    if (bl->type == BL_PC && (sd = (struct map_session_data *) bl))
    {
        sd->state.attack_type = BF_MISC;
        sd->state.arrow_atk = 0;
    }

    if (target->type == BL_PC)
        tsd = (struct map_session_data *) target;

    switch (skill_num)
    {

        case HT_LANDMINE:      // �����h�}�C��
            damage = skill_lv * (dex + 75) * (100 + int_) / 100;
            break;

        case HT_BLASTMINE:     // �u���X�g�}�C��
            damage = skill_lv * (dex / 2 + 50) * (100 + int_) / 100;
            break;

        case HT_CLAYMORETRAP:  // �N���C���A�[�g���b�v
            damage = skill_lv * (dex / 2 + 75) * (100 + int_) / 100;
            break;

        case HT_BLITZBEAT:     // �u���b�c�r�[�g
            if (sd == NULL || (skill = pc_checkskill (sd, HT_STEELCROW)) <= 0)
                skill = 0;
            damage = (dex / 10 + int_ / 2 + skill * 3 + 40) * 2;
            if (flag > 1)
                damage /= flag;
            break;

        case TF_THROWSTONE:    // �Γ���
            damage = 30;
            damagefix = 0;
            break;

        case BA_DISSONANCE:    // �s���a��
            damage =
                (skill_lv) * 20 + pc_checkskill (sd, BA_MUSICALLESSON) * 3;
            break;

        case NPC_SELFDESTRUCTION:  // ����
            damage = battle_get_hp (bl) - (bl == target ? 1 : 0);
            damagefix = 0;
            break;

        case NPC_SMOKING:      // �^�o�R���z��
            damage = 3;
            damagefix = 0;
            break;

        case NPC_DARKBREATH:
        {
            struct status_change *sc_data = battle_get_sc_data (target);
            int  hitrate =
                battle_get_hit (bl) - battle_get_flee (target) + 80;
            hitrate = ((hitrate > 95) ? 95 : ((hitrate < 5) ? 5 : hitrate));
            if (sc_data
                && (sc_data[SC_SLEEP].timer != -1
                    || sc_data[SC_STAN].timer != -1
                    || sc_data[SC_FREEZE].timer != -1
                    || (sc_data[SC_STONE].timer != -1
                        && sc_data[SC_STONE].val2 == 0)))
                hitrate = 1000000;
            if (MRAND (100) < hitrate)
            {
                damage = 500 + (skill_lv - 1) * 1000 + MRAND (1000);
                if (damage > 9999)
                    damage = 9999;
            }
        }
            break;
        case SN_FALCONASSAULT: /* �t�@���R���A�T���g */
            skill = pc_checkskill (sd, HT_BLITZBEAT);
            damage =
                (100 + 50 * skill_lv +
                 (dex / 10 + int_ / 2 + skill * 3 + 40) * 2);
            break;
    }

    ele = skill_get_pl (skill_num);
    race = battle_get_race (bl);

    if (damagefix)
    {
        if (damage < 1 && skill_num != NPC_DARKBREATH)
            damage = 1;

        if (tsd)
        {
            cardfix = 100;
            cardfix = cardfix * (100 - tsd->subele[ele]) / 100; // �����ɂ��_���[�W�ϐ�
            cardfix = cardfix * (100 - tsd->subrace[race]) / 100;   // �푰�ɂ��_���[�W�ϐ�
            cardfix = cardfix * (100 - tsd->misc_def_rate) / 100;
            damage = damage * cardfix / 100;
        }
        if (damage < 0)
            damage = 0;
        damage = battle_attr_fix (damage, ele, battle_get_element (target));    // �����C��
    }

    div_ = skill_get_num (skill_num, skill_lv);
    if (div_ > 1)
        damage *= div_;

    if (damage > 0
        && (damage < div_
            || (battle_get_def (target) >= 1000000
                && battle_get_mdef (target) >= 1000000)))
    {
        damage = div_;
    }

    damage = battle_calc_damage (bl, target, damage, div_, skill_num, skill_lv, aflag); // �ŏI�C��

    md.damage = damage;
    md.div_ = div_;
    md.amotion = battle_get_amotion (bl);
    md.dmotion = battle_get_dmotion (target);
    md.damage2 = 0;
    md.type = 0;
    md.blewcount = blewcount;
    md.flag = aflag;
    return md;

}

/*==========================================
 * �_���[�W�v�Z�ꊇ�����p
 *------------------------------------------
 */
struct Damage battle_calc_attack (int attack_type,
                                  struct block_list *bl,
                                  struct block_list *target, int skill_num,
                                  int skill_lv, int flag)
{
    struct Damage d;
    memset (&d, 0, sizeof (d));

    switch (attack_type)
    {
        case BF_WEAPON:
            return battle_calc_weapon_attack (bl, target, skill_num, skill_lv,
                                              flag);
        case BF_MAGIC:
            return battle_calc_magic_attack (bl, target, skill_num, skill_lv,
                                             flag);
        case BF_MISC:
            return battle_calc_misc_attack (bl, target, skill_num, skill_lv,
                                            flag);
        default:
            if (battle_config.error_log)
                printf ("battle_calc_attack: unknwon attack type ! %d\n",
                        attack_type);
            break;
    }
    return d;
}

/*==========================================
 * �ʏ�U�������܂Ƃ�
 *------------------------------------------
 */
int battle_weapon_attack (struct block_list *src, struct block_list *target,
                          unsigned int tick, int flag)
{
    struct map_session_data *sd = NULL;
    struct status_change *sc_data = battle_get_sc_data (src), *t_sc_data =
        battle_get_sc_data (target);
    short *opt1;
    int  race = 7, ele = 0;
    int  damage, rdamage = 0;
    struct Damage wd;

    nullpo_retr (0, src);
    nullpo_retr (0, target);

    if (src->type == BL_PC)
        sd = (struct map_session_data *) src;

    if (src->prev == NULL || target->prev == NULL)
        return 0;
    if (src->type == BL_PC && pc_isdead (sd))
        return 0;
    if (target->type == BL_PC
        && pc_isdead ((struct map_session_data *) target))
        return 0;

    opt1 = battle_get_opt1 (src);
    if (opt1 && *opt1 > 0)
    {
        battle_stopattack (src);
        return 0;
    }
    if (sc_data && sc_data[SC_BLADESTOP].timer != -1)
    {
        battle_stopattack (src);
        return 0;
    }

    race = battle_get_race (target);
    ele = battle_get_elem_type (target);
    if (battle_check_target (src, target, BCT_ENEMY) > 0 &&
        battle_check_range (src, target, 0))
    {
        // �U���ΏۂƂȂ肤��̂ōU��
        if (sd && sd->status.weapon == 11)
        {
            if (sd->equip_index[10] >= 0)
            {
                if (battle_config.arrow_decrement)
                    pc_delitem (sd, sd->equip_index[10], 1, 0);
            }
            else
            {
                clif_arrow_fail (sd, 0);
                return 0;
            }
        }
        if (flag & 0x8000)
        {
            if (sd && battle_config.pc_attack_direction_change)
                sd->dir = sd->head_dir =
                    map_calc_dir (src, target->x, target->y);
            else if (src->type == BL_MOB
                     && battle_config.monster_attack_direction_change)
                ((struct mob_data *) src)->dir =
                    map_calc_dir (src, target->x, target->y);
            wd = battle_calc_weapon_attack (src, target, KN_AUTOCOUNTER,
                                            flag & 0xff, 0);
        }
        else
            wd = battle_calc_weapon_attack (src, target, 0, 0, 0);

        // significantly increase injuries for hasted characters
        if (wd.damage > 0 && (t_sc_data[SC_HASTE].timer != -1))
        {
            wd.damage = (wd.damage * (16 + t_sc_data[SC_HASTE].val1)) >> 4;
        }

        if (wd.damage > 0
            && t_sc_data[SC_PHYS_SHIELD].timer != -1 && target->type == BL_PC)
        {
            int  reduction = t_sc_data[SC_PHYS_SHIELD].val1;
            if (reduction > wd.damage)
                reduction = wd.damage;

            wd.damage -= reduction;
            MAP_LOG_PC (((struct map_session_data *) target),
                        "MAGIC-ABSORB-DMG %d", reduction);
        }

        if ((damage = wd.damage + wd.damage2) > 0 && src != target)
        {
            if (wd.flag & BF_SHORT)
            {
                if (target->type == BL_PC)
                {
                    struct map_session_data *tsd =
                        (struct map_session_data *) target;
                    if (tsd && tsd->short_weapon_damage_return > 0)
                    {
                        rdamage +=
                            damage * tsd->short_weapon_damage_return / 100;
                        if (rdamage < 1)
                            rdamage = 1;
                    }
                }
                if (t_sc_data && t_sc_data[SC_REFLECTSHIELD].timer != -1)
                {
                    rdamage +=
                        damage * t_sc_data[SC_REFLECTSHIELD].val2 / 100;
                    if (rdamage < 1)
                        rdamage = 1;
                }
            }
            else if (wd.flag & BF_LONG)
            {
                if (target->type == BL_PC)
                {
                    struct map_session_data *tsd =
                        (struct map_session_data *) target;
                    if (tsd && tsd->long_weapon_damage_return > 0)
                    {
                        rdamage +=
                            damage * tsd->long_weapon_damage_return / 100;
                        if (rdamage < 1)
                            rdamage = 1;
                    }
                }
            }

            if (rdamage > 0)
                clif_damage (src, src, tick, wd.amotion, 0, rdamage, 1, 4, 0);
        }

        if (wd.div_ == 255 && sd)
        {                       //�O�i��
            int  delay =
                1000 - 4 * battle_get_agi (src) - 2 * battle_get_dex (src);
            int  skilllv;
            if (wd.damage + wd.damage2 < battle_get_hp (target))
            {
                if ((skilllv = pc_checkskill (sd, MO_CHAINCOMBO)) > 0)
                    delay += 300 * battle_config.combo_delay_rate / 100;    //�ǉ��f�B���C��conf�ɂ�蒲��

                skill_status_change_start (src, SC_COMBO, MO_TRIPLEATTACK,
                                           skilllv, 0, 0, delay, 0);
            }
            sd->attackabletime = sd->canmove_tick = tick + delay;
            clif_combo_delay (src, delay);
            clif_skill_damage (src, target, tick, wd.amotion, wd.dmotion,
                               wd.damage, 3, MO_TRIPLEATTACK,
                               pc_checkskill (sd, MO_TRIPLEATTACK), -1);
        }
        else
        {
            clif_damage (src, target, tick, wd.amotion, wd.dmotion,
                         wd.damage, wd.div_, wd.type, wd.damage2);
            //�񓁗�����ƃJ�^�[���ǌ��̃~�X�\��(�������`)
            if (sd && sd->status.weapon >= 16 && wd.damage2 == 0)
                clif_damage (src, target, tick + 10, wd.amotion, wd.dmotion,
                             0, 1, 0, 0);
        }
        if (sd && sd->splash_range > 0 && (wd.damage > 0 || wd.damage2 > 0))
            skill_castend_damage_id (src, target, 0, -1, tick, 0);
        map_freeblock_lock ();

        if (src->type == BL_PC)
        {
            int  weapon_index = sd->equip_index[9];
            int  weapon = 0;
            if (sd->inventory_data[weapon_index]
                && sd->status.inventory[weapon_index].equip & 0x2)
                weapon = sd->inventory_data[weapon_index]->nameid;

            MAP_LOG ("PC%d %d:%d,%d WPNDMG %s%d %d FOR %d WPN %d",
                     sd->status.char_id, src->m, src->x, src->y,
                     (target->type == BL_PC) ? "PC" : "MOB",
                     (target->type ==
                      BL_PC) ? ((struct map_session_data *) target)->
                     status.char_id : target->id,
                     (target->type ==
                      BL_PC) ? 0 : ((struct mob_data *) target)->class,
                     wd.damage + wd.damage2, weapon);
        }

        if (target->type == BL_PC)
        {
            struct map_session_data *sd2 = (struct map_session_data *) target;
            MAP_LOG ("PC%d %d:%d,%d WPNINJURY %s%d %d FOR %d",
                     sd2->status.char_id, target->m, target->x, target->y,
                     (src->type == BL_PC) ? "PC" : "MOB",
                     (src->type ==
                      BL_PC) ? ((struct map_session_data *) src)->
                     status.char_id : src->id,
                     (src->type ==
                      BL_PC) ? 0 : ((struct mob_data *) src)->class,
                     wd.damage + wd.damage2);
        }

        battle_damage (src, target, (wd.damage + wd.damage2), 0);
        if (target->prev != NULL &&
            (target->type != BL_PC
             || (target->type == BL_PC
                 && !pc_isdead ((struct map_session_data *) target))))
        {
            if (wd.damage > 0 || wd.damage2 > 0)
            {
                skill_additional_effect (src, target, 0, 0, BF_WEAPON, tick);
                if (sd)
                {
                    if (sd->weapon_coma_ele[ele] > 0
                        && MRAND (10000) < sd->weapon_coma_ele[ele])
                        battle_damage (src, target,
                                       battle_get_max_hp (target), 1);
                    if (sd->weapon_coma_race[race] > 0
                        && MRAND (10000) < sd->weapon_coma_race[race])
                        battle_damage (src, target,
                                       battle_get_max_hp (target), 1);
                    if (battle_get_mode (target) & 0x20)
                    {
                        if (sd->weapon_coma_race[10] > 0
                            && MRAND (10000) < sd->weapon_coma_race[10])
                            battle_damage (src, target,
                                           battle_get_max_hp (target), 1);
                    }
                    else
                    {
                        if (sd->weapon_coma_race[11] > 0
                            && MRAND (10000) < sd->weapon_coma_race[11])
                            battle_damage (src, target,
                                           battle_get_max_hp (target), 1);
                    }
                }
            }
        }
        if (sc_data && sc_data[SC_AUTOSPELL].timer != -1
            && MRAND (100) < sc_data[SC_AUTOSPELL].val4)
        {
            int  skilllv = sc_data[SC_AUTOSPELL].val3, i, f = 0;
            i = MRAND (100);
            if (i >= 50)
                skilllv -= 2;
            else if (i >= 15)
                skilllv--;
            if (skilllv < 1)
                skilllv = 1;
            if (sd)
            {
                int  sp = skill_get_sp (sc_data[SC_AUTOSPELL].val2,
                                        skilllv) * 2 / 3;
                if (sd->status.sp >= sp)
                {
                    if ((i = skill_get_inf (sc_data[SC_AUTOSPELL].val2) == 2)
                        || i == 32)
                        f = skill_castend_pos2 (src, target->x, target->y,
                                                sc_data[SC_AUTOSPELL].val2,
                                                skilllv, tick, flag);
                    else
                    {
                        switch (skill_get_nk (sc_data[SC_AUTOSPELL].val2))
                        {
                            case 0:
                            case 2:
                                f = skill_castend_damage_id (src, target,
                                                             sc_data
                                                             [SC_AUTOSPELL].val2,
                                                             skilllv, tick,
                                                             flag);
                                break;
                            case 1:    /* �x���n */
                                if ((sc_data[SC_AUTOSPELL].val2 == AL_HEAL
                                     || (sc_data[SC_AUTOSPELL].val2 ==
                                         ALL_RESURRECTION
                                         && target->type != BL_PC))
                                    && battle_check_undead (race, ele))
                                    f = skill_castend_damage_id (src, target,
                                                                 sc_data
                                                                 [SC_AUTOSPELL].val2,
                                                                 skilllv,
                                                                 tick, flag);
                                else
                                    f = skill_castend_nodamage_id (src,
                                                                   target,
                                                                   sc_data
                                                                   [SC_AUTOSPELL].val2,
                                                                   skilllv,
                                                                   tick,
                                                                   flag);
                                break;
                        }
                    }
                    if (!f)
                        pc_heal (sd, 0, -sp);
                }
            }
            else
            {
                if ((i = skill_get_inf (sc_data[SC_AUTOSPELL].val2) == 2)
                    || i == 32)
                    skill_castend_pos2 (src, target->x, target->y,
                                        sc_data[SC_AUTOSPELL].val2, skilllv,
                                        tick, flag);
                else
                {
                    switch (skill_get_nk (sc_data[SC_AUTOSPELL].val2))
                    {
                        case 0:
                        case 2:
                            skill_castend_damage_id (src, target,
                                                     sc_data
                                                     [SC_AUTOSPELL].val2,
                                                     skilllv, tick, flag);
                            break;
                        case 1:    /* �x���n */
                            if ((sc_data[SC_AUTOSPELL].val2 == AL_HEAL
                                 || (sc_data[SC_AUTOSPELL].val2 ==
                                     ALL_RESURRECTION
                                     && target->type != BL_PC))
                                && battle_check_undead (race, ele))
                                skill_castend_damage_id (src, target,
                                                         sc_data
                                                         [SC_AUTOSPELL].val2,
                                                         skilllv, tick, flag);
                            else
                                skill_castend_nodamage_id (src, target,
                                                           sc_data
                                                           [SC_AUTOSPELL].val2,
                                                           skilllv, tick,
                                                           flag);
                            break;
                    }
                }
            }
        }
        if (sd)
        {
            if (sd->autospell_id > 0 && sd->autospell_lv > 0
                && MRAND (100) < sd->autospell_rate)
            {
                int  skilllv = sd->autospell_lv, i, f = 0, sp;
                i = MRAND (100);
                if (i >= 50)
                    skilllv -= 2;
                else if (i >= 15)
                    skilllv--;
                if (skilllv < 1)
                    skilllv = 1;
                sp = skill_get_sp (sd->autospell_id, skilllv) * 2 / 3;
                if (sd->status.sp >= sp)
                {
                    if ((i = skill_get_inf (sd->autospell_id) == 2)
                        || i == 32)
                        f = skill_castend_pos2 (src, target->x, target->y,
                                                sd->autospell_id, skilllv,
                                                tick, flag);
                    else
                    {
                        switch (skill_get_nk (sd->autospell_id))
                        {
                            case 0:
                            case 2:
                                f = skill_castend_damage_id (src, target,
                                                             sd->autospell_id,
                                                             skilllv, tick,
                                                             flag);
                                break;
                            case 1:    /* �x���n */
                                if ((sd->autospell_id == AL_HEAL
                                     || (sd->autospell_id == ALL_RESURRECTION
                                         && target->type != BL_PC))
                                    && battle_check_undead (race, ele))
                                    f = skill_castend_damage_id (src, target,
                                                                 sd->autospell_id,
                                                                 skilllv,
                                                                 tick, flag);
                                else
                                    f = skill_castend_nodamage_id (src,
                                                                   target,
                                                                   sd->autospell_id,
                                                                   skilllv,
                                                                   tick,
                                                                   flag);
                                break;
                        }
                    }
                    if (!f)
                        pc_heal (sd, 0, -sp);
                }
            }
            if (wd.flag & BF_WEAPON && src != target
                && (wd.damage > 0 || wd.damage2 > 0))
            {
                int  hp = 0, sp = 0;
                if (sd->hp_drain_rate && sd->hp_drain_per > 0 && wd.damage > 0
                    && MRAND (100) < sd->hp_drain_rate)
                {
                    hp += (wd.damage * sd->hp_drain_per) / 100;
                    if (sd->hp_drain_rate > 0 && hp < 1)
                        hp = 1;
                    else if (sd->hp_drain_rate < 0 && hp > -1)
                        hp = -1;
                }
                if (sd->hp_drain_rate_ && sd->hp_drain_per_ > 0
                    && wd.damage2 > 0 && MRAND (100) < sd->hp_drain_rate_)
                {
                    hp += (wd.damage2 * sd->hp_drain_per_) / 100;
                    if (sd->hp_drain_rate_ > 0 && hp < 1)
                        hp = 1;
                    else if (sd->hp_drain_rate_ < 0 && hp > -1)
                        hp = -1;
                }
                if (sd->sp_drain_rate && sd->sp_drain_per > 0 && wd.damage > 0
                    && MRAND (100) < sd->sp_drain_rate)
                {
                    sp += (wd.damage * sd->sp_drain_per) / 100;
                    if (sd->sp_drain_rate > 0 && sp < 1)
                        sp = 1;
                    else if (sd->sp_drain_rate < 0 && sp > -1)
                        sp = -1;
                }
                if (sd->sp_drain_rate_ && sd->sp_drain_per_ > 0
                    && wd.damage2 > 0 && MRAND (100) < sd->sp_drain_rate_)
                {
                    sp += (wd.damage2 * sd->sp_drain_per_) / 100;
                    if (sd->sp_drain_rate_ > 0 && sp < 1)
                        sp = 1;
                    else if (sd->sp_drain_rate_ < 0 && sp > -1)
                        sp = -1;
                }
                if (hp || sp)
                    pc_heal (sd, hp, sp);
            }
        }

        if (rdamage > 0)
            battle_damage (target, src, rdamage, 0);
        if (t_sc_data && t_sc_data[SC_AUTOCOUNTER].timer != -1
            && t_sc_data[SC_AUTOCOUNTER].val4 > 0)
        {
            if (t_sc_data[SC_AUTOCOUNTER].val3 == src->id)
                battle_weapon_attack (target, src, tick,
                                      0x8000 |
                                      t_sc_data[SC_AUTOCOUNTER].val1);
            skill_status_change_end (target, SC_AUTOCOUNTER, -1);
        }
        if (t_sc_data && t_sc_data[SC_BLADESTOP_WAIT].timer != -1)
        {
            int  lv = t_sc_data[SC_BLADESTOP_WAIT].val1;
            skill_status_change_end (target, SC_BLADESTOP_WAIT, -1);
            skill_status_change_start (src, SC_BLADESTOP, lv, 1, (int) src,
                                       (int) target,
                                       skill_get_time2 (MO_BLADESTOP, lv), 0);
            skill_status_change_start (target, SC_BLADESTOP, lv, 2,
                                       (int) target, (int) src,
                                       skill_get_time2 (MO_BLADESTOP, lv), 0);
        }
        if (t_sc_data && t_sc_data[SC_SPLASHER].timer != -1)    //�������̂őΏۂ̃x�i���X�v���b�V���[��Ԃ�����
            skill_status_change_end (target, SC_SPLASHER, -1);

        map_freeblock_unlock ();
    }
    return wd.dmg_lv;
}

int battle_check_undead (int race, int element)
{
    if (battle_config.undead_detect_type == 0)
    {
        if (element == 9)
            return 1;
    }
    else if (battle_config.undead_detect_type == 1)
    {
        if (race == 1)
            return 1;
    }
    else
    {
        if (element == 9 || race == 1)
            return 1;
    }
    return 0;
}

/*==========================================
 * �G��������(1=�m��,0=�ے�,-1=�G���[)
 * flag&0xf0000 = 0x00000:�G����Ȃ�������iret:1���G�ł͂Ȃ��j
 *				= 0x10000:�p�[�e�B�[����iret:1=�p�[�e�B�[�����o)
 *				= 0x20000:�S��(ret:1=�G��������)
 *				= 0x40000:�G������(ret:1=�G)
 *				= 0x50000:�p�[�e�B�[����Ȃ�������(ret:1=�p�[�e�B�łȂ�)
 *------------------------------------------
 */
int battle_check_target (struct block_list *src, struct block_list *target,
                         int flag)
{
    int  s_p, s_g, t_p, t_g;
    struct block_list *ss = src;

    nullpo_retr (0, src);
    nullpo_retr (0, target);

    if (flag & 0x40000)
    {                           // ���]�t���O
        int  ret = battle_check_target (src, target, flag & 0x30000);
        if (ret != -1)
            return !ret;
        return -1;
    }

    if (flag & 0x20000)
    {
        if (target->type == BL_MOB || target->type == BL_PC)
            return 1;
        else
            return -1;
    }

    if (src->type == BL_SKILL && target->type == BL_SKILL)  // �Ώۂ��X�L�����j�b�g�Ȃ疳�����m��
        return -1;

    if (target->type == BL_PC
        && ((struct map_session_data *) target)->invincible_timer != -1)
        return -1;

    if (target->type == BL_SKILL)
    {
        switch (((struct skill_unit *) target)->group->unit_id)
        {
            case 0x8d:
            case 0x8f:
            case 0x98:
                return 0;
                break;
        }
    }

    // �X�L�����j�b�g�̏ꍇ�A�e�����߂�
    if (src->type == BL_SKILL)
    {
        int  inf2 =
            skill_get_inf2 (((struct skill_unit *) src)->group->skill_id);
        if ((ss =
             map_id2bl (((struct skill_unit *) src)->group->src_id)) == NULL)
            return -1;
        if (ss->prev == NULL)
            return -1;
        if (inf2 & 0x80 && (map[src->m].flag.pvp || pc_iskiller ((struct map_session_data *) src, (struct map_session_data *) target)) &&   // [MouseJstr]
            !(target->type == BL_PC
              && pc_isinvisible ((struct map_session_data *) target)))
            return 0;
        if (ss == target)
        {
            if (inf2 & 0x100)
                return 0;
            if (inf2 & 0x200)
                return -1;
        }
    }
    // Mob��master_id��������special_mob_ai�Ȃ�A����������߂�
    if (src->type == BL_MOB)
    {
        struct mob_data *md = (struct mob_data *) src;
        if (md && md->master_id > 0)
        {
            if (md->master_id == target->id)    // ��Ȃ�m��
                return 1;
            if (md->state.special_mob_ai)
            {
                if (target->type == BL_MOB)
                {               //special_mob_ai�őΏۂ�Mob
                    struct mob_data *tmd = (struct mob_data *) target;
                    if (tmd)
                    {
                        if (tmd->master_id != md->master_id)    //�����傪�ꏏ�łȂ���Δے�
                            return 0;
                        else
                        {       //�����傪�ꏏ�Ȃ̂ōm�肵�������ǎ����͔ے�
                            if (md->state.special_mob_ai > 2)
                                return 0;
                            else
                                return 1;
                        }
                    }
                }
            }
            if ((ss = map_id2bl (md->master_id)) == NULL)
                return -1;
        }
    }

    if (src == target || ss == target)  // �����Ȃ�m��
        return 1;

    if (target->type == BL_PC
        && pc_isinvisible ((struct map_session_data *) target))
        return -1;

    if (src->prev == NULL ||    // ����ł�Ȃ�G���[
        (src->type == BL_PC && pc_isdead ((struct map_session_data *) src)))
        return -1;

    if ((ss->type == BL_PC && target->type == BL_MOB) ||
        (ss->type == BL_MOB && target->type == BL_PC))
        return 0;               // PCvsMOB�Ȃ�ے�

    s_p = battle_get_party_id (ss);
    s_g = battle_get_guild_id (ss);

    t_p = battle_get_party_id (target);
    t_g = battle_get_guild_id (target);

    if (flag & 0x10000)
    {
        if (s_p && t_p && s_p == t_p)   // �����p�[�e�B�Ȃ�m��i�����j
            return 1;
        else                    // �p�[�e�B�����Ȃ瓯���p�[�e�B����Ȃ����_�Ŕے�
            return 0;
    }

    if (ss->type == BL_MOB && s_g > 0 && t_g > 0 && s_g == t_g) // �����M���h/mob�N���X�Ȃ�m��i�����j
        return 1;

//printf("ss:%d src:%d target:%d flag:0x%x %d %d ",ss->id,src->id,target->id,flag,src->type,target->type);
//printf("p:%d %d g:%d %d\n",s_p,t_p,s_g,t_g);

    if (ss->type == BL_PC && target->type == BL_PC)
    {                           // ����PVP���[�h�Ȃ�ے�i�G�j
        struct skill_unit *su = NULL;
        if (src->type == BL_SKILL)
            su = (struct skill_unit *) src;
        if (map[ss->m].flag.pvp
            || pc_iskiller ((struct map_session_data *) ss,
                            (struct map_session_data *) target))
        {                       // [MouseJstr]
            if (su && su->group->target_flag == BCT_NOENEMY)
                return 1;
            else if (battle_config.pk_mode
                     && (((struct map_session_data *) ss)->status.class == 0
                         || ((struct map_session_data *) target)->
                         status.class == 0))
                return 1;       // prevent novice engagement in pk_mode [Valaris]
            else if (map[ss->m].flag.pvp_noparty && s_p > 0 && t_p > 0
                     && s_p == t_p)
                return 1;
            else if (map[ss->m].flag.pvp_noguild && s_g > 0 && t_g > 0
                     && s_g == t_g)
                return 1;
            return 0;
        }
        if (map[src->m].flag.gvg)
        {
            struct guild *g = NULL;
            if (su && su->group->target_flag == BCT_NOENEMY)
                return 1;
            if (s_g > 0 && s_g == t_g)
                return 1;
            if (map[src->m].flag.gvg_noparty && s_p > 0 && t_p > 0
                && s_p == t_p)
                return 1;
            if ((g = guild_search (s_g)))
            {
                int  i;
                for (i = 0; i < MAX_GUILDALLIANCE; i++)
                {
                    if (g->alliance[i].guild_id > 0
                        && g->alliance[i].guild_id == t_g)
                    {
                        if (g->alliance[i].opposition)
                            return 0;   //�G�΃M���h�Ȃ疳�����ɓG
                        else
                            return 1;   //�����M���h�Ȃ疳�����ɖ���
                    }
                }
            }
            return 0;
        }
    }

    return 1;                   // �Y�����Ȃ��̂Ŗ��֌W�l���i�܂��G����Ȃ��̂Ŗ����j
}

/*==========================================
 * �˒�����
 *------------------------------------------
 */
int battle_check_range (struct block_list *src, struct block_list *bl,
                        int range)
{

    int  dx, dy;
    struct walkpath_data wpd;
    int  arange;

    nullpo_retr (0, src);
    nullpo_retr (0, bl);

    dx = abs (bl->x - src->x);
    dy = abs (bl->y - src->y);
    arange = ((dx > dy) ? dx : dy);

    if (src->m != bl->m)        // �Ⴄ�}�b�v
        return 0;

    if (range > 0 && range < arange)    // ��������
        return 0;

    if (arange < 2)             // �����}�X���א�
        return 1;

//  if(bl->type == BL_SKILL && ((struct skill_unit *)bl)->group->unit_id == 0x8d)
//      return 1;

    // ��Q������
    wpd.path_len = 0;
    wpd.path_pos = 0;
    wpd.path_half = 0;
    if (path_search (&wpd, src->m, src->x, src->y, bl->x, bl->y, 0x10001) !=
        -1)
        return 1;

    dx = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
    dy = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
    return (path_search (&wpd, src->m, src->x + dx, src->y + dy,
                         bl->x - dx, bl->y - dy, 0x10001) != -1) ? 1 : 0;
}

/*==========================================
 * Return numerical value of a switch configuration (modified by [Yor])
 * on/off, english, fran�ais, deutsch, espa�ol
 *------------------------------------------
 */
int battle_config_switch (const char *str)
{
    if (strcmpi (str, "on") == 0 || strcmpi (str, "yes") == 0
        || strcmpi (str, "oui") == 0 || strcmpi (str, "ja") == 0
        || strcmpi (str, "si") == 0)
        return 1;
    if (strcmpi (str, "off") == 0 || strcmpi (str, "no") == 0
        || strcmpi (str, "non") == 0 || strcmpi (str, "nein") == 0)
        return 0;
    return atoi (str);
}

/*==========================================
 * �ݒ�t�@�C����ǂݍ���
 *------------------------------------------
 */
int battle_config_read (const char *cfgName)
{
    int  i;
    char line[1024], w1[1024], w2[1024];
    FILE *fp;
    static int count = 0;

    if ((count++) == 0)
    {
        battle_config.warp_point_debug = 0;
        battle_config.enemy_critical = 0;
        battle_config.enemy_critical_rate = 100;
        battle_config.enemy_str = 1;
        battle_config.enemy_perfect_flee = 0;
        battle_config.cast_rate = 100;
        battle_config.delay_rate = 100;
        battle_config.delay_dependon_dex = 0;
        battle_config.sdelay_attack_enable = 0;
        battle_config.left_cardfix_to_right = 0;
        battle_config.pc_skill_add_range = 0;
        battle_config.skill_out_range_consume = 1;
        battle_config.mob_skill_add_range = 0;
        battle_config.pc_damage_delay = 1;
        battle_config.pc_damage_delay_rate = 100;
        battle_config.defnotenemy = 1;
        battle_config.random_monster_checklv = 1;
        battle_config.attr_recover = 1;
        battle_config.flooritem_lifetime = LIFETIME_FLOORITEM * 1000;
        battle_config.item_auto_get = 0;
        battle_config.drop_pickup_safety_zone = 20;
        battle_config.item_first_get_time = 3000;
        battle_config.item_second_get_time = 1000;
        battle_config.item_third_get_time = 1000;
        battle_config.mvp_item_first_get_time = 10000;
        battle_config.mvp_item_second_get_time = 10000;
        battle_config.mvp_item_third_get_time = 2000;

        battle_config.drop_rate0item = 0;
        battle_config.base_exp_rate = 100;
        battle_config.job_exp_rate = 100;
        battle_config.pvp_exp = 1;
        battle_config.gtb_pvp_only = 0;
        battle_config.death_penalty_type = 0;
        battle_config.death_penalty_base = 0;
        battle_config.death_penalty_job = 0;
        battle_config.zeny_penalty = 0;
        battle_config.restart_hp_rate = 0;
        battle_config.restart_sp_rate = 0;
        battle_config.mvp_item_rate = 100;
        battle_config.mvp_exp_rate = 100;
        battle_config.mvp_hp_rate = 100;
        battle_config.monster_hp_rate = 100;
        battle_config.monster_max_aspd = 199;
        battle_config.atc_gmonly = 0;
        battle_config.gm_allskill = 0;
        battle_config.gm_allequip = 0;
        battle_config.gm_skilluncond = 0;
        battle_config.guild_max_castles = 0;
        battle_config.skillfree = 0;
        battle_config.skillup_limit = 0;
        battle_config.wp_rate = 100;
        battle_config.pp_rate = 100;
        battle_config.monster_active_enable = 1;
        battle_config.monster_damage_delay_rate = 100;
        battle_config.monster_loot_type = 0;
        battle_config.mob_skill_use = 1;
        battle_config.mob_count_rate = 100;
        battle_config.quest_skill_learn = 0;
        battle_config.quest_skill_reset = 1;
        battle_config.basic_skill_check = 1;
        battle_config.guild_emperium_check = 1;
        battle_config.guild_exp_limit = 50;
        battle_config.pc_invincible_time = 5000;
        battle_config.skill_min_damage = 0;
        battle_config.finger_offensive_type = 0;
        battle_config.heal_exp = 0;
        battle_config.resurrection_exp = 0;
        battle_config.shop_exp = 0;
        battle_config.combo_delay_rate = 100;
        battle_config.item_check = 1;
        battle_config.wedding_modifydisplay = 0;
        battle_config.natural_healhp_interval = 6000;
        battle_config.natural_healsp_interval = 8000;
        battle_config.natural_heal_skill_interval = 10000;
        battle_config.natural_heal_weight_rate = 50;
        battle_config.itemheal_regeneration_factor = 1;
        battle_config.item_name_override_grffile = 1;
        battle_config.arrow_decrement = 1;
        battle_config.max_aspd = 199;
        battle_config.max_hp = 32500;
        battle_config.max_sp = 32500;
        battle_config.max_lv = 99;  // [MouseJstr]
        battle_config.max_parameter = 99;
        battle_config.max_cart_weight = 8000;
        battle_config.pc_skill_log = 0;
        battle_config.mob_skill_log = 0;
        battle_config.battle_log = 0;
        battle_config.save_log = 0;
        battle_config.error_log = 1;
        battle_config.etc_log = 1;
        battle_config.save_clothcolor = 0;
        battle_config.undead_detect_type = 0;
        battle_config.pc_auto_counter_type = 1;
        battle_config.monster_auto_counter_type = 1;
        battle_config.agi_penaly_type = 0;
        battle_config.agi_penaly_count = 3;
        battle_config.agi_penaly_num = 0;
        battle_config.agi_penaly_count_lv = ATK_FLEE;
        battle_config.vit_penaly_type = 0;
        battle_config.vit_penaly_count = 3;
        battle_config.vit_penaly_num = 0;
        battle_config.vit_penaly_count_lv = ATK_DEF;
        battle_config.player_defense_type = 0;
        battle_config.monster_defense_type = 0;
        battle_config.magic_defense_type = 0;
        battle_config.pc_skill_reiteration = 0;
        battle_config.monster_skill_reiteration = 0;
        battle_config.pc_skill_nofootset = 0;
        battle_config.monster_skill_nofootset = 0;
        battle_config.pc_cloak_check_type = 0;
        battle_config.monster_cloak_check_type = 0;
        battle_config.gvg_short_damage_rate = 100;
        battle_config.gvg_long_damage_rate = 100;
        battle_config.gvg_magic_damage_rate = 100;
        battle_config.gvg_misc_damage_rate = 100;
        battle_config.gvg_eliminate_time = 7000;
        battle_config.mob_changetarget_byskill = 0;
        battle_config.pc_attack_direction_change = 1;
        battle_config.monster_attack_direction_change = 1;
        battle_config.pc_undead_nofreeze = 0;
        battle_config.pc_land_skill_limit = 1;
        battle_config.monster_land_skill_limit = 1;
        battle_config.party_skill_penaly = 1;
        battle_config.monster_class_change_full_recover = 0;
        battle_config.produce_item_name_input = 1;
        battle_config.produce_potion_name_input = 1;
        battle_config.making_arrow_name_input = 1;
        battle_config.holywater_name_input = 1;
        battle_config.display_delay_skill_fail = 1;
        battle_config.chat_warpportal = 0;
        battle_config.mob_warpportal = 0;
        battle_config.dead_branch_active = 0;
        battle_config.show_steal_in_same_party = 0;
        battle_config.enable_upper_class = 0;
        battle_config.pc_attack_attr_none = 0;
        battle_config.mob_attack_attr_none = 1;
        battle_config.mob_ghostring_fix = 0;
        battle_config.gx_allhit = 0;
        battle_config.gx_cardfix = 0;
        battle_config.gx_dupele = 1;
        battle_config.gx_disptype = 1;
        battle_config.player_skill_partner_check = 1;
        battle_config.hide_GM_session = 0;
        battle_config.unit_movement_type = 0;
        battle_config.invite_request_check = 1;
        battle_config.skill_removetrap_type = 0;
        battle_config.disp_experience = 0;
        battle_config.item_rate_common = 100;
        battle_config.item_rate_equip = 100;
        battle_config.item_rate_card = 100;
        battle_config.item_rate_heal = 100; // Added by Valaris
        battle_config.item_rate_use = 100;  // End
        battle_config.item_drop_common_min = 1; // Added by TyrNemesis^
        battle_config.item_drop_common_max = 10000;
        battle_config.item_drop_equip_min = 1;
        battle_config.item_drop_equip_max = 10000;
        battle_config.item_drop_card_min = 1;
        battle_config.item_drop_card_max = 10000;
        battle_config.item_drop_mvp_min = 1;
        battle_config.item_drop_mvp_max = 10000;    // End Addition
        battle_config.item_drop_heal_min = 1;   // Added by Valaris
        battle_config.item_drop_heal_max = 10000;
        battle_config.item_drop_use_min = 1;
        battle_config.item_drop_use_max = 10000;    // End
        battle_config.prevent_logout = 1;   // Added by RoVeRT
        battle_config.maximum_level = 255;  // Added by Valaris
        battle_config.drops_by_luk = 0; // [Valaris]
        battle_config.equipment_breaking = 0;   // [Valaris]
        battle_config.equipment_break_rate = 100;   // [Valaris]
        battle_config.pk_mode = 0;  // [Valaris]
        battle_config.multi_level_up = 0;   // [Valaris]
        battle_config.backstab_bow_penalty = 0; // Akaru
        battle_config.night_at_start = 0;   // added by [Yor]
        battle_config.day_duration = 2 * 60 * 60 * 1000;    // added by [Yor] (2 hours)
        battle_config.night_duration = 30 * 60 * 1000;  // added by [Yor] (30 minutes)
        battle_config.show_mob_hp = 0;  // [Valaris]
        battle_config.hack_info_GM_level = 60;  // added by [Yor] (default: 60, GM level)
        battle_config.any_warp_GM_min_level = 20;   // added by [Yor]
        battle_config.packet_ver_flag = 63; // added by [Yor]
        battle_config.min_hair_style = 0;
        battle_config.max_hair_style = 20;
        battle_config.min_hair_color = 0;
        battle_config.max_hair_color = 9;
        battle_config.min_cloth_color = 0;
        battle_config.max_cloth_color = 4;

        battle_config.castrate_dex_scale = 150;

        battle_config.area_size = 14;

        battle_config.chat_lame_penalty = 2;
        battle_config.chat_spam_threshold = 10;
        battle_config.chat_spam_flood = 10;
        battle_config.chat_spam_ban = 1;
        battle_config.chat_spam_warn = 8;
        battle_config.chat_maxline = 255;

        battle_config.trade_spam_threshold = 10;
        battle_config.trade_spam_flood = 10;
        battle_config.trade_spam_ban = 0;
        battle_config.trade_spam_warn = 8;

        battle_config.sit_spam_threshold = 1;
        battle_config.sit_spam_flood = 15;
        battle_config.sit_spam_ban = 0;
        battle_config.sit_spam_warn = 3;

        battle_config.packet_spam_threshold = 2;
        battle_config.packet_spam_flood = 30;
        battle_config.packet_spam_kick = 1;

    }

    fp = fopen_ (cfgName, "r");
    if (fp == NULL)
    {
        printf ("file not found: %s\n", cfgName);
        return 1;
    }
    while (fgets (line, 1020, fp))
    {
        const struct
        {
            char str[128];
            int *val;
        } data[] =
        {
            {
            "warp_point_debug", &battle_config.warp_point_debug},
            {
            "enemy_critical", &battle_config.enemy_critical},
            {
            "enemy_critical_rate", &battle_config.enemy_critical_rate},
            {
            "enemy_str", &battle_config.enemy_str},
            {
            "enemy_perfect_flee", &battle_config.enemy_perfect_flee},
            {
            "casting_rate", &battle_config.cast_rate},
            {
            "delay_rate", &battle_config.delay_rate},
            {
            "delay_dependon_dex", &battle_config.delay_dependon_dex},
            {
            "skill_delay_attack_enable",
                    &battle_config.sdelay_attack_enable},
            {
            "left_cardfix_to_right", &battle_config.left_cardfix_to_right},
            {
            "player_skill_add_range", &battle_config.pc_skill_add_range},
            {
            "skill_out_range_consume",
                    &battle_config.skill_out_range_consume},
            {
            "monster_skill_add_range", &battle_config.mob_skill_add_range},
            {
            "player_damage_delay", &battle_config.pc_damage_delay},
            {
            "player_damage_delay_rate",
                    &battle_config.pc_damage_delay_rate},
            {
            "defunit_not_enemy", &battle_config.defnotenemy},
            {
            "random_monster_checklv",
                    &battle_config.random_monster_checklv},
            {
            "attribute_recover", &battle_config.attr_recover},
            {
            "flooritem_lifetime", &battle_config.flooritem_lifetime},
            {
            "item_auto_get", &battle_config.item_auto_get},
            {
            "drop_pickup_safety_zone",
                    &battle_config.drop_pickup_safety_zone},
            {
            "item_first_get_time", &battle_config.item_first_get_time},
            {
            "item_second_get_time", &battle_config.item_second_get_time},
            {
            "item_third_get_time", &battle_config.item_third_get_time},
            {
            "mvp_item_first_get_time",
                    &battle_config.mvp_item_first_get_time},
            {
            "mvp_item_second_get_time",
                    &battle_config.mvp_item_second_get_time},
            {
            "mvp_item_third_get_time",
                    &battle_config.mvp_item_third_get_time},
            {
            "item_rate", &battle_config.item_rate},
            {
            "drop_rate0item", &battle_config.drop_rate0item},
            {
            "base_exp_rate", &battle_config.base_exp_rate},
            {
            "job_exp_rate", &battle_config.job_exp_rate},
            {
            "pvp_exp", &battle_config.pvp_exp},
            {
            "gtb_pvp_only", &battle_config.gtb_pvp_only},
            {
            "guild_max_castles", &battle_config.guild_max_castles},
            {
            "death_penalty_type", &battle_config.death_penalty_type},
            {
            "death_penalty_base", &battle_config.death_penalty_base},
            {
            "death_penalty_job", &battle_config.death_penalty_job},
            {
            "zeny_penalty", &battle_config.zeny_penalty},
            {
            "restart_hp_rate", &battle_config.restart_hp_rate},
            {
            "restart_sp_rate", &battle_config.restart_sp_rate},
            {
            "mvp_hp_rate", &battle_config.mvp_hp_rate},
            {
            "mvp_item_rate", &battle_config.mvp_item_rate},
            {
            "mvp_exp_rate", &battle_config.mvp_exp_rate},
            {
            "monster_hp_rate", &battle_config.monster_hp_rate},
            {
            "monster_max_aspd", &battle_config.monster_max_aspd},
            {
            "atcommand_gm_only", &battle_config.atc_gmonly},
            {
            "atcommand_spawn_quantity_limit",
                    &battle_config.atc_spawn_quantity_limit},
            {
            "gm_all_skill", &battle_config.gm_allskill},
            {
            "gm_all_skill_add_abra", &battle_config.gm_allskill_addabra},
            {
            "gm_all_equipment", &battle_config.gm_allequip},
            {
            "gm_skill_unconditional", &battle_config.gm_skilluncond},
            {
            "player_skillfree", &battle_config.skillfree},
            {
            "player_skillup_limit", &battle_config.skillup_limit},
            {
            "weapon_produce_rate", &battle_config.wp_rate},
            {
            "potion_produce_rate", &battle_config.pp_rate},
            {
            "monster_active_enable", &battle_config.monster_active_enable},
            {
            "monster_damage_delay_rate",
                    &battle_config.monster_damage_delay_rate},
            {
            "monster_loot_type", &battle_config.monster_loot_type},
            {
            "mob_skill_use", &battle_config.mob_skill_use},
            {
            "mob_count_rate", &battle_config.mob_count_rate},
            {
            "quest_skill_learn", &battle_config.quest_skill_learn},
            {
            "quest_skill_reset", &battle_config.quest_skill_reset},
            {
            "basic_skill_check", &battle_config.basic_skill_check},
            {
            "guild_emperium_check", &battle_config.guild_emperium_check},
            {
            "guild_exp_limit", &battle_config.guild_exp_limit},
            {
            "player_invincible_time", &battle_config.pc_invincible_time},
            {
            "skill_min_damage", &battle_config.skill_min_damage},
            {
            "finger_offensive_type", &battle_config.finger_offensive_type},
            {
            "heal_exp", &battle_config.heal_exp},
            {
            "resurrection_exp", &battle_config.resurrection_exp},
            {
            "shop_exp", &battle_config.shop_exp},
            {
            "combo_delay_rate", &battle_config.combo_delay_rate},
            {
            "item_check", &battle_config.item_check},
            {
            "wedding_modifydisplay", &battle_config.wedding_modifydisplay},
            {
            "natural_healhp_interval",
                    &battle_config.natural_healhp_interval},
            {
            "natural_healsp_interval",
                    &battle_config.natural_healsp_interval},
            {
            "natural_heal_skill_interval",
                    &battle_config.natural_heal_skill_interval},
            {
            "natural_heal_weight_rate",
                    &battle_config.natural_heal_weight_rate},
            {
            "itemheal_regeneration_factor",
                    &battle_config.itemheal_regeneration_factor},
            {
            "item_name_override_grffile",
                    &battle_config.item_name_override_grffile},
            {
            "arrow_decrement", &battle_config.arrow_decrement},
            {
            "max_aspd", &battle_config.max_aspd},
            {
            "max_hp", &battle_config.max_hp},
            {
            "max_sp", &battle_config.max_sp},
            {
            "max_lv", &battle_config.max_lv},
            {
            "max_parameter", &battle_config.max_parameter},
            {
            "max_cart_weight", &battle_config.max_cart_weight},
            {
            "player_skill_log", &battle_config.pc_skill_log},
            {
            "monster_skill_log", &battle_config.mob_skill_log},
            {
            "battle_log", &battle_config.battle_log},
            {
            "save_log", &battle_config.save_log},
            {
            "error_log", &battle_config.error_log},
            {
            "etc_log", &battle_config.etc_log},
            {
            "save_clothcolor", &battle_config.save_clothcolor},
            {
            "undead_detect_type", &battle_config.undead_detect_type},
            {
            "player_auto_counter_type",
                    &battle_config.pc_auto_counter_type},
            {
            "monster_auto_counter_type",
                    &battle_config.monster_auto_counter_type},
            {
            "agi_penaly_type", &battle_config.agi_penaly_type},
            {
            "agi_penaly_count", &battle_config.agi_penaly_count},
            {
            "agi_penaly_num", &battle_config.agi_penaly_num},
            {
            "agi_penaly_count_lv", &battle_config.agi_penaly_count_lv},
            {
            "vit_penaly_type", &battle_config.vit_penaly_type},
            {
            "vit_penaly_count", &battle_config.vit_penaly_count},
            {
            "vit_penaly_num", &battle_config.vit_penaly_num},
            {
            "vit_penaly_count_lv", &battle_config.vit_penaly_count_lv},
            {
            "player_defense_type", &battle_config.player_defense_type},
            {
            "monster_defense_type", &battle_config.monster_defense_type},
            {
            "magic_defense_type", &battle_config.magic_defense_type},
            {
            "player_skill_reiteration",
                    &battle_config.pc_skill_reiteration},
            {
            "monster_skill_reiteration",
                    &battle_config.monster_skill_reiteration},
            {
            "player_skill_nofootset", &battle_config.pc_skill_nofootset},
            {
            "monster_skill_nofootset",
                    &battle_config.monster_skill_nofootset},
            {
            "player_cloak_check_type", &battle_config.pc_cloak_check_type},
            {
            "monster_cloak_check_type",
                    &battle_config.monster_cloak_check_type},
            {
            "gvg_short_attack_damage_rate",
                    &battle_config.gvg_short_damage_rate},
            {
            "gvg_long_attack_damage_rate",
                    &battle_config.gvg_long_damage_rate},
            {
            "gvg_magic_attack_damage_rate",
                    &battle_config.gvg_magic_damage_rate},
            {
            "gvg_misc_attack_damage_rate",
                    &battle_config.gvg_misc_damage_rate},
            {
            "gvg_eliminate_time", &battle_config.gvg_eliminate_time},
            {
            "mob_changetarget_byskill",
                    &battle_config.mob_changetarget_byskill},
            {
            "player_attack_direction_change",
                    &battle_config.pc_attack_direction_change},
            {
            "monster_attack_direction_change",
                    &battle_config.monster_attack_direction_change},
            {
            "player_land_skill_limit", &battle_config.pc_land_skill_limit},
            {
            "monster_land_skill_limit",
                    &battle_config.monster_land_skill_limit},
            {
            "party_skill_penaly", &battle_config.party_skill_penaly},
            {
            "monster_class_change_full_recover",
                    &battle_config.monster_class_change_full_recover},
            {
            "produce_item_name_input",
                    &battle_config.produce_item_name_input},
            {
            "produce_potion_name_input",
                    &battle_config.produce_potion_name_input},
            {
            "making_arrow_name_input",
                    &battle_config.making_arrow_name_input},
            {
            "holywater_name_input", &battle_config.holywater_name_input},
            {
            "display_delay_skill_fail",
                    &battle_config.display_delay_skill_fail},
            {
            "chat_warpportal", &battle_config.chat_warpportal},
            {
            "mob_warpportal", &battle_config.mob_warpportal},
            {
            "dead_branch_active", &battle_config.dead_branch_active},
            {
            "show_steal_in_same_party",
                    &battle_config.show_steal_in_same_party},
            {
            "enable_upper_class", &battle_config.enable_upper_class},
            {
            "mob_attack_attr_none", &battle_config.mob_attack_attr_none},
            {
            "mob_ghostring_fix", &battle_config.mob_ghostring_fix},
            {
            "pc_attack_attr_none", &battle_config.pc_attack_attr_none},
            {
            "gx_allhit", &battle_config.gx_allhit},
            {
            "gx_cardfix", &battle_config.gx_cardfix},
            {
            "gx_dupele", &battle_config.gx_dupele},
            {
            "gx_disptype", &battle_config.gx_disptype},
            {
            "player_skill_partner_check",
                    &battle_config.player_skill_partner_check},
            {
            "hide_GM_session", &battle_config.hide_GM_session},
            {
            "unit_movement_type", &battle_config.unit_movement_type},
            {
            "invite_request_check", &battle_config.invite_request_check},
            {
            "skill_removetrap_type", &battle_config.skill_removetrap_type},
            {
            "disp_experience", &battle_config.disp_experience},
            {
            "castle_defense_rate", &battle_config.castle_defense_rate},
            {
            "riding_weight", &battle_config.riding_weight},
            {
            "item_rate_common", &battle_config.item_rate_common},   // Added by RoVeRT
            {
            "item_rate_equip", &battle_config.item_rate_equip},
            {
            "item_rate_card", &battle_config.item_rate_card},   // End Addition
            {
            "item_rate_heal", &battle_config.item_rate_heal},   // Added by Valaris
            {
            "item_rate_use", &battle_config.item_rate_use}, // End
            {
            "item_drop_common_min", &battle_config.item_drop_common_min},   // Added by TyrNemesis^
            {
            "item_drop_common_max", &battle_config.item_drop_common_max},
            {
            "item_drop_equip_min", &battle_config.item_drop_equip_min},
            {
            "item_drop_equip_max", &battle_config.item_drop_equip_max},
            {
            "item_drop_card_min", &battle_config.item_drop_card_min},
            {
            "item_drop_card_max", &battle_config.item_drop_card_max},
            {
            "item_drop_mvp_min", &battle_config.item_drop_mvp_min},
            {
            "item_drop_mvp_max", &battle_config.item_drop_mvp_max}, // End Addition
            {
            "prevent_logout", &battle_config.prevent_logout},   // Added by RoVeRT
            {
            "alchemist_summon_reward", &battle_config.alchemist_summon_reward}, // [Valaris]
            {
            "maximum_level", &battle_config.maximum_level}, // [Valaris]
            {
            "drops_by_luk", &battle_config.drops_by_luk},   // [Valaris]
            {
            "monsters_ignore_gm", &battle_config.monsters_ignore_gm},   // [Valaris]
            {
            "equipment_breaking", &battle_config.equipment_breaking},   // [Valaris]
            {
            "equipment_break_rate", &battle_config.equipment_break_rate},   // [Valaris]
            {
            "pk_mode", &battle_config.pk_mode}, // [Valaris]
            {
            "multi_level_up", &battle_config.multi_level_up},   // [Valaris]
            {
            "backstab_bow_penalty", &battle_config.backstab_bow_penalty},
            {
            "night_at_start", &battle_config.night_at_start},   // added by [Yor]
            {
            "day_duration", &battle_config.day_duration},   // added by [Yor]
            {
            "night_duration", &battle_config.night_duration},   // added by [Yor]
            {
            "show_mob_hp", &battle_config.show_mob_hp}, // [Valaris]
            {
            "hack_info_GM_level", &battle_config.hack_info_GM_level},   // added by [Yor]
            {
            "any_warp_GM_min_level", &battle_config.any_warp_GM_min_level}, // added by [Yor]
            {
            "packet_ver_flag", &battle_config.packet_ver_flag}, // added by [Yor]
            {
            "min_hair_style", &battle_config.min_hair_style},   // added by [MouseJstr]
            {
            "max_hair_style", &battle_config.max_hair_style},   // added by [MouseJstr]
            {
            "min_hair_color", &battle_config.min_hair_color},   // added by [MouseJstr]
            {
            "max_hair_color", &battle_config.max_hair_color},   // added by [MouseJstr]
            {
            "min_cloth_color", &battle_config.min_cloth_color}, // added by [MouseJstr]
            {
            "max_cloth_color", &battle_config.max_cloth_color}, // added by [MouseJstr]
            {
            "castrate_dex_scale", &battle_config.castrate_dex_scale},   // added by [MouseJstr]
            {
            "area_size", &battle_config.area_size}, // added by [MouseJstr]
            {
            "muting_players", &battle_config.muting_players},   // added by [Apple]
            {
            "chat_lame_penalty", &battle_config.chat_lame_penalty},
            {
            "chat_spam_threshold", &battle_config.chat_spam_threshold},
            {
            "chat_spam_flood", &battle_config.chat_spam_flood},
            {
            "chat_spam_ban", &battle_config.chat_spam_ban},
            {
            "chat_spam_warn", &battle_config.chat_spam_warn},
            {
            "chat_maxline", &battle_config.chat_maxline},
            {
            "trade_spam_threshold", &battle_config.trade_spam_threshold},
            {
            "trade_spam_flood", &battle_config.trade_spam_flood},
            {
            "trade_spam_ban", &battle_config.trade_spam_ban},
            {
            "trade_spam_warn", &battle_config.trade_spam_warn},
            {
            "sit_spam_threshold", &battle_config.sit_spam_threshold},
            {
            "sit_spam_flood", &battle_config.sit_spam_flood},
            {
            "sit_spam_ban", &battle_config.sit_spam_ban},
            {
            "sit_spam_warn", &battle_config.sit_spam_warn},
            {
            "packet_spam_threshold", &battle_config.packet_spam_threshold},
            {
            "packet_spam_flood", &battle_config.packet_spam_flood},
            {
            "packet_spam_kick", &battle_config.packet_spam_kick}
        };

        if (line[0] == '/' && line[1] == '/')
            continue;
        if (sscanf (line, "%[^:]:%s", w1, w2) != 2)
            continue;
        for (i = 0; i < sizeof (data) / (sizeof (data[0])); i++)
            if (strcmpi (w1, data[i].str) == 0)
                *data[i].val = battle_config_switch (w2);

        if (strcmpi (w1, "import") == 0)
            battle_config_read (w2);
    }
    fclose_ (fp);

    if (--count == 0)
    {
        if (battle_config.flooritem_lifetime < 1000)
            battle_config.flooritem_lifetime = LIFETIME_FLOORITEM * 1000;
        if (battle_config.restart_hp_rate < 0)
            battle_config.restart_hp_rate = 0;
        else if (battle_config.restart_hp_rate > 100)
            battle_config.restart_hp_rate = 100;
        if (battle_config.restart_sp_rate < 0)
            battle_config.restart_sp_rate = 0;
        else if (battle_config.restart_sp_rate > 100)
            battle_config.restart_sp_rate = 100;
        if (battle_config.natural_healhp_interval < NATURAL_HEAL_INTERVAL)
            battle_config.natural_healhp_interval = NATURAL_HEAL_INTERVAL;
        if (battle_config.natural_healsp_interval < NATURAL_HEAL_INTERVAL)
            battle_config.natural_healsp_interval = NATURAL_HEAL_INTERVAL;
        if (battle_config.natural_heal_skill_interval < NATURAL_HEAL_INTERVAL)
            battle_config.natural_heal_skill_interval = NATURAL_HEAL_INTERVAL;
        if (battle_config.natural_heal_weight_rate < 50)
            battle_config.natural_heal_weight_rate = 50;
        if (battle_config.natural_heal_weight_rate > 101)
            battle_config.natural_heal_weight_rate = 101;
        battle_config.monster_max_aspd =
            2000 - battle_config.monster_max_aspd * 10;
        if (battle_config.monster_max_aspd < 10)
            battle_config.monster_max_aspd = 10;
        if (battle_config.monster_max_aspd > 1000)
            battle_config.monster_max_aspd = 1000;
        battle_config.max_aspd = 2000 - battle_config.max_aspd * 10;
        if (battle_config.max_aspd < 10)
            battle_config.max_aspd = 10;
        if (battle_config.max_aspd > 1000)
            battle_config.max_aspd = 1000;
        if (battle_config.max_hp > 1000000)
            battle_config.max_hp = 1000000;
        if (battle_config.max_hp < 100)
            battle_config.max_hp = 100;
        if (battle_config.max_sp > 1000000)
            battle_config.max_sp = 1000000;
        if (battle_config.max_sp < 100)
            battle_config.max_sp = 100;
        if (battle_config.max_parameter < 10)
            battle_config.max_parameter = 10;
        if (battle_config.max_parameter > 10000)
            battle_config.max_parameter = 10000;
        if (battle_config.max_cart_weight > 1000000)
            battle_config.max_cart_weight = 1000000;
        if (battle_config.max_cart_weight < 100)
            battle_config.max_cart_weight = 100;
        battle_config.max_cart_weight *= 10;

        if (battle_config.agi_penaly_count < 2)
            battle_config.agi_penaly_count = 2;
        if (battle_config.vit_penaly_count < 2)
            battle_config.vit_penaly_count = 2;

        if (battle_config.guild_exp_limit > 99)
            battle_config.guild_exp_limit = 99;
        if (battle_config.guild_exp_limit < 0)
            battle_config.guild_exp_limit = 0;

        if (battle_config.castle_defense_rate < 0)
            battle_config.castle_defense_rate = 0;
        if (battle_config.castle_defense_rate > 100)
            battle_config.castle_defense_rate = 100;
        if (battle_config.item_drop_common_min < 1) // Added by TyrNemesis^
            battle_config.item_drop_common_min = 1;
        if (battle_config.item_drop_common_max > 10000)
            battle_config.item_drop_common_max = 10000;
        if (battle_config.item_drop_equip_min < 1)
            battle_config.item_drop_equip_min = 1;
        if (battle_config.item_drop_equip_max > 10000)
            battle_config.item_drop_equip_max = 10000;
        if (battle_config.item_drop_card_min < 1)
            battle_config.item_drop_card_min = 1;
        if (battle_config.item_drop_card_max > 10000)
            battle_config.item_drop_card_max = 10000;
        if (battle_config.item_drop_mvp_min < 1)
            battle_config.item_drop_mvp_min = 1;
        if (battle_config.item_drop_mvp_max > 10000)
            battle_config.item_drop_mvp_max = 10000;    // End Addition

        if (battle_config.night_at_start < 0)   // added by [Yor]
            battle_config.night_at_start = 0;
        else if (battle_config.night_at_start > 1)  // added by [Yor]
            battle_config.night_at_start = 1;
        if (battle_config.day_duration < 0) // added by [Yor]
            battle_config.day_duration = 0;
        if (battle_config.night_duration < 0)   // added by [Yor]
            battle_config.night_duration = 0;

        if (battle_config.hack_info_GM_level < 0)   // added by [Yor]
            battle_config.hack_info_GM_level = 0;
        else if (battle_config.hack_info_GM_level > 100)
            battle_config.hack_info_GM_level = 100;

        if (battle_config.any_warp_GM_min_level < 0)    // added by [Yor]
            battle_config.any_warp_GM_min_level = 0;
        else if (battle_config.any_warp_GM_min_level > 100)
            battle_config.any_warp_GM_min_level = 100;

        if (battle_config.chat_spam_ban < 0)
            battle_config.chat_spam_ban = 0;
        else if (battle_config.chat_spam_ban > 32767)
            battle_config.chat_spam_ban = 32767;

        if (battle_config.chat_spam_flood < 0)
            battle_config.chat_spam_flood = 0;
        else if (battle_config.chat_spam_flood > 32767)
            battle_config.chat_spam_flood = 32767;

        if (battle_config.chat_spam_warn < 0)
            battle_config.chat_spam_warn = 0;
        else if (battle_config.chat_spam_warn > 32767)
            battle_config.chat_spam_warn = 32767;

        if (battle_config.chat_spam_threshold < 0)
            battle_config.chat_spam_threshold = 0;
        else if (battle_config.chat_spam_threshold > 32767)
            battle_config.chat_spam_threshold = 32767;

        if (battle_config.chat_maxline < 1)
            battle_config.chat_maxline = 1;
        else if (battle_config.chat_maxline > 512)
            battle_config.chat_maxline = 512;

        if (battle_config.trade_spam_ban < 0)
            battle_config.trade_spam_ban = 0;
        else if (battle_config.trade_spam_ban > 32767)
            battle_config.trade_spam_ban = 32767;

        if (battle_config.trade_spam_flood < 0)
            battle_config.trade_spam_flood = 0;
        else if (battle_config.trade_spam_flood > 32767)
            battle_config.trade_spam_flood = 32767;

        if (battle_config.trade_spam_warn < 0)
            battle_config.trade_spam_warn = 0;
        else if (battle_config.trade_spam_warn > 32767)
            battle_config.trade_spam_warn = 32767;

        if (battle_config.trade_spam_threshold < 0)
            battle_config.trade_spam_threshold = 0;
        else if (battle_config.trade_spam_threshold > 32767)
            battle_config.trade_spam_threshold = 32767;

        if (battle_config.sit_spam_ban < 0)
            battle_config.sit_spam_ban = 0;
        else if (battle_config.sit_spam_ban > 32767)
            battle_config.sit_spam_ban = 32767;

        if (battle_config.sit_spam_flood < 0)
            battle_config.sit_spam_flood = 0;
        else if (battle_config.sit_spam_flood > 32767)
            battle_config.sit_spam_flood = 32767;

        if (battle_config.sit_spam_warn < 0)
            battle_config.sit_spam_warn = 0;
        else if (battle_config.sit_spam_warn > 32767)
            battle_config.sit_spam_warn = 32767;

        if (battle_config.sit_spam_threshold < 0)
            battle_config.sit_spam_threshold = 0;
        else if (battle_config.sit_spam_threshold > 32767)
            battle_config.sit_spam_threshold = 32767;

        if (battle_config.packet_spam_threshold < 0)
            battle_config.packet_spam_threshold = 0;
        else if (battle_config.packet_spam_threshold > 32767)
            battle_config.packet_spam_threshold = 32767;

        if (battle_config.packet_spam_flood < 0)
            battle_config.packet_spam_flood = 0;
        else if (battle_config.packet_spam_flood > 32767)
            battle_config.packet_spam_flood = 32767;

        if (battle_config.packet_spam_kick < 0)
            battle_config.packet_spam_kick = 0;
        else if (battle_config.packet_spam_kick > 1)
            battle_config.packet_spam_kick = 1;

        // at least 1 client must be accepted
        if ((battle_config.packet_ver_flag & 63) == 0)  // added by [Yor]
            battle_config.packet_ver_flag = 63; // accept all clients

        add_timer_func_list (battle_delay_damage_sub,
                             "battle_delay_damage_sub");
    }

    return 0;
}
