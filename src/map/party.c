// $Id: party.c,v 1.2 2004/09/22 02:59:47 Akitasha Exp $
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "party.h"
#include "db.h"
#include "timer.h"
#include "socket.h"
#include "nullpo.h"
#include "malloc.h"
#include "pc.h"
#include "map.h"
#include "battle.h"
#include "intif.h"
#include "clif.h"
#include "skill.h"
#include "tmw.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

#define PARTY_SEND_XYHP_INVERVAL	1000    // ���W��g�o���M�̊Ԋu

static struct dbt *party_db;

int  party_send_xyhp_timer (int tid, unsigned int tick, int id, int data);
/*==========================================
 * �I��
 *------------------------------------------
 */
static int party_db_final (void *key, void *data, va_list ap)
{
    free (data);
    return 0;
}

void do_final_party (void)
{
    if (party_db)
        numdb_final (party_db, party_db_final);
}

// ������
void do_init_party (void)
{
    party_db = numdb_init ();
    add_timer_func_list (party_send_xyhp_timer, "party_send_xyhp_timer");
    add_timer_interval (gettick () + PARTY_SEND_XYHP_INVERVAL,
                        party_send_xyhp_timer, 0, 0,
                        PARTY_SEND_XYHP_INVERVAL);
}

// ����
struct party *party_search (int party_id)
{
    return numdb_search (party_db, party_id);
}

int party_searchname_sub (void *key, void *data, va_list ap)
{
    struct party *p = (struct party *) data, **dst;
    char *str;
    str = va_arg (ap, char *);
    dst = va_arg (ap, struct party **);
    if (strcmpi (p->name, str) == 0)
        *dst = p;
    return 0;
}

// �p�[�e�B������
struct party *party_searchname (char *str)
{
    struct party *p = NULL;
    numdb_foreach (party_db, party_searchname_sub, str, &p);
    return p;
}

/* Process a party creation request. */
int party_create (struct map_session_data *sd, char *name)
{
    char pname[24];
    nullpo_retr (0, sd);

    strncpy (pname, name, 24);
    pname[23] = '\0';
    tmw_TrimStr (pname);

    /* The party name is empty/invalid. */
    if (!*pname)
        clif_party_created (sd, 1);

    /* Make sure the character isn't already in a party. */
    if (sd->status.party_id == 0)
        intif_create_party (sd, pname);
    else
        clif_party_created (sd, 2);

    return 0;
}

/* Relay the result of a party creation request. */
int party_created (int account_id, int fail, int party_id, char *name)
{
    struct map_session_data *sd;
    sd = map_id2sd (account_id);

    nullpo_retr (0, sd);

    /* The party name is valid and not already taken. */
    if (!fail)
    {
        struct party *p;
        sd->status.party_id = party_id;

        if ((p = numdb_search (party_db, party_id)) != NULL)
        {
            printf ("party_created(): ID already exists!\n");
            exit (1);
        }

        p = (struct party *) aCalloc (1, sizeof (struct party));
        p->party_id = party_id;
        memcpy (p->name, name, 24);
        numdb_insert (party_db, party_id, p);

        /* The party was created successfully. */
        clif_party_created (sd, 0);
    }

    else
        clif_party_created (sd, 1);

    return 0;
}

// ���v��
int party_request_info (int party_id)
{
    return intif_request_partyinfo (party_id);
}

// �����L�����̊m�F
int party_check_member (struct party *p)
{
    int  i;
    struct map_session_data *sd;

    nullpo_retr (0, p);

    for (i = 0; i < fd_max; i++)
    {
        if (session[i] && (sd = session[i]->session_data) && sd->state.auth)
        {
            if (sd->status.party_id == p->party_id)
            {
                int  j, f = 1;
                for (j = 0; j < MAX_PARTY; j++)
                {               // �p�[�e�B�Ƀf�[�^�����邩�m�F
                    if (p->member[j].account_id == sd->status.account_id)
                    {
                        if (strcmp (p->member[j].name, sd->status.name) == 0)
                            f = 0;  // �f�[�^������
                        else
                            p->member[j].sd = NULL; // ���C�ʃL����������
                    }
                }
                if (f)
                {
                    sd->status.party_id = 0;
                    if (battle_config.error_log)
                        printf ("party: check_member %d[%s] is not member\n",
                                sd->status.account_id, sd->status.name);
                }
            }
        }
    }
    return 0;
}

// ��񏊓����s�i����ID�̃L������S���������ɂ���j
int party_recv_noinfo (int party_id)
{
    int  i;
    struct map_session_data *sd;
    for (i = 0; i < fd_max; i++)
    {
        if (session[i] && (sd = session[i]->session_data) && sd->state.auth)
        {
            if (sd->status.party_id == party_id)
                sd->status.party_id = 0;
        }
    }
    return 0;
}

// ��񏊓�
int party_recv_info (struct party *sp)
{
    struct party *p;
    int  i;

    nullpo_retr (0, sp);

    if ((p = numdb_search (party_db, sp->party_id)) == NULL)
    {
        p = (struct party *) aCalloc (1, sizeof (struct party));
        numdb_insert (party_db, sp->party_id, p);

        // �ŏ��̃��[�h�Ȃ̂Ń��[�U�[�̃`�F�b�N���s��
        party_check_member (sp);
    }
    memcpy (p, sp, sizeof (struct party));

    for (i = 0; i < MAX_PARTY; i++)
    {                           // sd�̐ݒ�
        struct map_session_data *sd = map_id2sd (p->member[i].account_id);
        p->member[i].sd = (sd != NULL
                           && sd->status.party_id == p->party_id) ? sd : NULL;
    }

    clif_party_info (p, -1);

    for (i = 0; i < MAX_PARTY; i++)
    {                           // �ݒ���̑��M
//      struct map_session_data *sd = map_id2sd(p->member[i].account_id);
        struct map_session_data *sd = p->member[i].sd;
        if (sd != NULL && sd->party_sended == 0)
        {
            clif_party_option (p, sd, 0x100);
            sd->party_sended = 1;
        }
    }

    return 0;
}

// �p�[�e�B�ւ̊��U
int party_invite (struct map_session_data *sd, int account_id)
{
    struct map_session_data *tsd = map_id2sd (account_id);
    struct party *p = party_search (sd->status.party_id);
    int  i;

    nullpo_retr (0, sd);

    if (tsd == NULL || p == NULL)
        return 0;

    printf ("\tA\n");

    if (!battle_config.invite_request_check)
    {
        if (tsd->guild_invite > 0 || tsd->trade_partner || tsd->npc_id
            || tsd->npc_shopid || pc_checkskill (tsd, NV_PARTY) < 1)
        {
            clif_party_inviteack (sd, tsd->status.name, 0);
            return 0;
        }
    }
    printf ("\tB\n");
    if (tsd->status.party_id > 0 || tsd->party_invite > 0)
    {                           // ����̏����m�F
        clif_party_inviteack (sd, tsd->status.name, 0);
        return 0;
    }
    printf ("\tC\n");
    for (i = 0; i < MAX_PARTY; i++)
    {                           // ���A�J�E���g�m�F
        if (p->member[i].account_id == account_id)
        {
            clif_party_inviteack (sd, tsd->status.name, 0);
            return 0;
        }
    }
    printf ("\tD\n");
    tsd->party_invite = sd->status.party_id;
    tsd->party_invite_account = sd->status.account_id;

    clif_party_invite (sd, tsd);
    return 0;
}

// �p�[�e�B���U�ւ̕ԓ�
int party_reply_invite (struct map_session_data *sd, int account_id, int flag)
{
    struct map_session_data *tsd = map_id2sd (account_id);

    nullpo_retr (0, sd);

    if (flag == 1)
    {                           // ����
        //inter�I�֒ǉ��v��
        intif_party_addmember (sd->party_invite, sd->status.account_id);
        return 0;
    }
    else
    {                           // ����
        sd->party_invite = 0;
        sd->party_invite_account = 0;
        if (tsd == NULL)
            return 0;
        clif_party_inviteack (tsd, sd->status.name, 1);
    }
    return 0;
}

// �p�[�e�B���ǉ����ꂽ
int party_member_added (int party_id, int account_id, int flag)
{
    struct map_session_data *sd = map_id2sd (account_id), *sd2;
    struct party *p = party_search (party_id);

    if (sd == NULL)
    {
        if (flag == 0)
        {
            if (battle_config.error_log)
                printf ("party: member added error %d is not online\n",
                        account_id);
            intif_party_leave (party_id, account_id);   // �L�������ɓo�^�ł��Ȃ��������ߒE�ޗv�����o��
        }
        return 0;
    }
    sd2 = map_id2sd (sd->party_invite_account);
    sd->party_invite = 0;
    sd->party_invite_account = 0;

    if (p == NULL)
    {
        printf ("party_member_added: party %d not found.\n", party_id);
        intif_party_leave (party_id, account_id);
        return 0;
    }

    if (flag == 1)
    {                           // ���s
        if (sd2 != NULL)
            clif_party_inviteack (sd2, sd->status.name, 0);
        return 0;
    }

    // ����
    sd->party_sended = 0;
    sd->status.party_id = party_id;

    if (sd2 != NULL)
        clif_party_inviteack (sd2, sd->status.name, 2);

    // �������������m�F
    party_check_conflict (sd);

    party_send_xy_clear (p);

    return 0;
}

// �p�[�e�B�����v��
int party_removemember (struct map_session_data *sd, int account_id,
                        char *name)
{
    struct party *p;
    int  i;

    nullpo_retr (0, sd);

    if ((p = party_search (sd->status.party_id)) == NULL)
        return 0;

    for (i = 0; i < MAX_PARTY; i++)
    {                           // ���[�_�[���ǂ����`�F�b�N
        if (p->member[i].account_id == sd->status.account_id)
            if (p->member[i].leader == 0)
                return 0;
    }

    for (i = 0; i < MAX_PARTY; i++)
    {                           // �������Ă��邩���ׂ�
        if (p->member[i].account_id == account_id)
        {
            intif_party_leave (p->party_id, account_id);
            return 0;
        }
    }
    return 0;
}

// �p�[�e�B�E�ޗv��
int party_leave (struct map_session_data *sd)
{
    struct party *p;
    int  i;

    nullpo_retr (0, sd);

    if ((p = party_search (sd->status.party_id)) == NULL)
        return 0;

    for (i = 0; i < MAX_PARTY; i++)
    {                           // �������Ă��邩
        if (p->member[i].account_id == sd->status.account_id)
        {
            intif_party_leave (p->party_id, sd->status.account_id);
            return 0;
        }
    }
    return 0;
}

// �p�[�e�B�����o���E�ނ���
int party_member_leaved (int party_id, int account_id, char *name)
{
    struct map_session_data *sd = map_id2sd (account_id);
    struct party *p = party_search (party_id);
    if (p != NULL)
    {
        int  i;
        for (i = 0; i < MAX_PARTY; i++)
            if (p->member[i].account_id == account_id)
            {
                clif_party_leaved (p, sd, account_id, name, 0x00);
                p->member[i].account_id = 0;
                p->member[i].sd = NULL;
            }
    }
    if (sd != NULL && sd->status.party_id == party_id)
    {
        sd->status.party_id = 0;
        sd->party_sended = 0;
    }
    return 0;
}

// �p�[�e�B���U�ʒm
int party_broken (int party_id)
{
    struct party *p;
    int  i;
    if ((p = party_search (party_id)) == NULL)
        return 0;

    for (i = 0; i < MAX_PARTY; i++)
    {
        if (p->member[i].sd != NULL)
        {
            clif_party_leaved (p, p->member[i].sd,
                               p->member[i].account_id, p->member[i].name,
                               0x10);
            p->member[i].sd->status.party_id = 0;
            p->member[i].sd->party_sended = 0;
        }
    }
    numdb_erase (party_db, party_id);
    return 0;
}

// �p�[�e�B�̐ݒ�ύX�v��
int party_changeoption (struct map_session_data *sd, int exp, int item)
{
    struct party *p;

    nullpo_retr (0, sd);

    if (sd->status.party_id == 0
        || (p = party_search (sd->status.party_id)) == NULL)
        return 0;
    intif_party_changeoption (sd->status.party_id, sd->status.account_id, exp,
                              item);
    return 0;
}

// �p�[�e�B�̐ݒ�ύX�ʒm
int party_optionchanged (int party_id, int account_id, int exp, int item,
                         int flag)
{
    struct party *p;
    struct map_session_data *sd = map_id2sd (account_id);
    if ((p = party_search (party_id)) == NULL)
        return 0;

    if (!(flag & 0x01))
        p->exp = exp;
    if (!(flag & 0x10))
        p->item = item;
    clif_party_option (p, sd, flag);
    return 0;
}

// �p�[�e�B�����o�̈ړ��ʒm
int party_recv_movemap (int party_id, int account_id, char *map, int online,
                        int lv)
{
    struct party *p;
    int  i;
    if ((p = party_search (party_id)) == NULL)
        return 0;
    for (i = 0; i < MAX_PARTY; i++)
    {
        struct party_member *m = &p->member[i];
        if (m == NULL)
        {
            printf ("party_recv_movemap nullpo?\n");
            return 0;
        }
        if (m->account_id == account_id)
        {
            memcpy (m->map, map, 16);
            m->online = online;
            m->lv = lv;
            break;
        }
    }
    if (i == MAX_PARTY)
    {
        if (battle_config.error_log)
            printf ("party: not found member %d on %d[%s]", account_id,
                    party_id, p->name);
        return 0;
    }

    for (i = 0; i < MAX_PARTY; i++)
    {                           // sd�Đݒ�
        struct map_session_data *sd = map_id2sd (p->member[i].account_id);
        p->member[i].sd = (sd != NULL
                           && sd->status.party_id == p->party_id) ? sd : NULL;
    }

    party_send_xy_clear (p);    // ���W�Ēʒm�v��

    clif_party_info (p, -1);
    return 0;
}

// �p�[�e�B�����o�̈ړ�
int party_send_movemap (struct map_session_data *sd)
{
    struct party *p;

    nullpo_retr (0, sd);

    if (sd->status.party_id == 0)
        return 0;
    intif_party_changemap (sd, 1);

    if (sd->party_sended != 0)  // �����p�[�e�B�f�[�^�͑��M�ς�
        return 0;

    // �����m�F 
    party_check_conflict (sd);

    // ����Ȃ�p�[�e�B��񑗐M
    if ((p = party_search (sd->status.party_id)) != NULL)
    {
        party_check_member (p); // �������m�F����
        if (sd->status.party_id == p->party_id)
        {
            clif_party_info (p, sd->fd);
            clif_party_option (p, sd, 0x100);
            sd->party_sended = 1;
        }
    }

    return 0;
}

// �p�[�e�B�����o�̃��O�A�E�g
int party_send_logout (struct map_session_data *sd)
{
    struct party *p;

    nullpo_retr (0, sd);

    if (sd->status.party_id > 0)
        intif_party_changemap (sd, 0);

    // sd�������ɂȂ�̂Ńp�[�e�B��񂩂�폜
    if ((p = party_search (sd->status.party_id)) != NULL)
    {
        int  i;
        for (i = 0; i < MAX_PARTY; i++)
            if (p->member[i].sd == sd)
                p->member[i].sd = NULL;
    }

    return 0;
}

// �p�[�e�B���b�Z�[�W���M
int party_send_message (struct map_session_data *sd, char *mes, int len)
{
    if (sd->status.party_id == 0)
        return 0;
    intif_party_message (sd->status.party_id, sd->status.account_id, mes,
                         len);
    return 0;
}

// �p�[�e�B���b�Z�[�W��M
int party_recv_message (int party_id, int account_id, char *mes, int len)
{
    struct party *p;
    if ((p = party_search (party_id)) == NULL)
        return 0;
    clif_party_message (p, account_id, mes, len);
    return 0;
}

// �p�[�e�B�����m�F
int party_check_conflict (struct map_session_data *sd)
{
    nullpo_retr (0, sd);

    intif_party_checkconflict (sd->status.party_id, sd->status.account_id,
                               sd->status.name);
    return 0;
}

// �ʒu��g�o�ʒm�p
int party_send_xyhp_timer_sub (void *key, void *data, va_list ap)
{
    struct party *p = (struct party *) data;
    int  i;

    nullpo_retr (0, p);

    for (i = 0; i < MAX_PARTY; i++)
    {
        struct map_session_data *sd;
        if ((sd = p->member[i].sd) != NULL)
        {
            // ���W�ʒm
            if (sd->party_x != sd->bl.x || sd->party_y != sd->bl.y)
            {
                clif_party_xy (p, sd);
                sd->party_x = sd->bl.x;
                sd->party_y = sd->bl.y;
            }
            // �g�o�ʒm
            if (sd->party_hp != sd->status.hp)
            {
                clif_party_hp (p, sd);
                sd->party_hp = sd->status.hp;
            }

        }
    }
    return 0;
}

// �ʒu��g�o�ʒm
int party_send_xyhp_timer (int tid, unsigned int tick, int id, int data)
{
    numdb_foreach (party_db, party_send_xyhp_timer_sub, tick);
    return 0;
}

// �ʒu�ʒm�N���A
int party_send_xy_clear (struct party *p)
{
    int  i;

    nullpo_retr (0, p);

    for (i = 0; i < MAX_PARTY; i++)
    {
        struct map_session_data *sd;
        if ((sd = p->member[i].sd) != NULL)
        {
            sd->party_x = -1;
            sd->party_y = -1;
            sd->party_hp = -1;
        }
    }
    return 0;
}

// HP�ʒm�̕K�v�������p�imap_foreachinmovearea����Ă΂��j
int party_send_hp_check (struct block_list *bl, va_list ap)
{
    int  party_id;
    int *flag;
    struct map_session_data *sd;

    nullpo_retr (0, bl);
    nullpo_retr (0, ap);
    nullpo_retr (0, sd = (struct map_session_data *) bl);

    party_id = va_arg (ap, int);
    flag = va_arg (ap, int *);

    if (sd->status.party_id == party_id)
    {
        *flag = 1;
        sd->party_hp = -1;
    }
    return 0;
}

// �o���l�������z
int party_exp_share (struct party *p, int map, int base_exp, int job_exp)
{
    struct map_session_data *sd;
    int  i, c;

    nullpo_retr (0, p);

    for (i = c = 0; i < MAX_PARTY; i++)
        if ((sd = p->member[i].sd) != NULL && sd->bl.m == map)
            c++;
    if (c == 0)
        return 0;
    for (i = 0; i < MAX_PARTY; i++)
        if ((sd = p->member[i].sd) != NULL && sd->bl.m == map)
            pc_gainexp (sd, base_exp / c + 1, job_exp / c + 1);
    return 0;
}

// �����}�b�v�̃p�[�e�B�����o�[�S�̂ɏ�����������
// type==0 �����}�b�v
//     !=0 ��ʓ�
void party_foreachsamemap (int (*func) (struct block_list *, va_list),
                           struct map_session_data *sd, int type, ...)
{
    struct party *p;
    va_list ap;
    int  i;
    int  x0, y0, x1, y1;
    struct block_list *list[MAX_PARTY];
    int  blockcount = 0;

    nullpo_retv (sd);

    if ((p = party_search (sd->status.party_id)) == NULL)
        return;

    x0 = sd->bl.x - AREA_SIZE;
    y0 = sd->bl.y - AREA_SIZE;
    x1 = sd->bl.x + AREA_SIZE;
    y1 = sd->bl.y + AREA_SIZE;

    va_start (ap, type);

    for (i = 0; i < MAX_PARTY; i++)
    {
        struct party_member *m = &p->member[i];
        if (m->sd != NULL)
        {
            if (sd->bl.m != m->sd->bl.m)
                continue;
            if (type != 0 &&
                (m->sd->bl.x < x0 || m->sd->bl.y < y0 ||
                 m->sd->bl.x > x1 || m->sd->bl.y > y1))
                continue;
            list[blockcount++] = &m->sd->bl;
        }
    }

    map_freeblock_lock ();      // ����������̉�����֎~����

    for (i = 0; i < blockcount; i++)
        if (list[i]->prev)      // �L�����ǂ����`�F�b�N
            func (list[i], ap);

    map_freeblock_unlock ();    // �����������

    va_end (ap);
}
