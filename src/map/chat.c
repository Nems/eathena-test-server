// $Id: chat.c,v 1.2 2004/09/22 02:59:47 Akitasha Exp $
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "nullpo.h"
#include "malloc.h"
#include "map.h"
#include "clif.h"
#include "pc.h"
#include "chat.h"
#include "npc.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

int  chat_triggerevent (struct chat_data *cd);

/*==========================================
 * �`���b�g���[���쐬
 *------------------------------------------
 */
int chat_createchat (struct map_session_data *sd, int limit, int pub,
                     char *pass, char *title, int titlelen)
{
    struct chat_data *cd;

    nullpo_retr (0, sd);

    cd = aCalloc (1, sizeof (struct chat_data));

    cd->limit = limit;
    cd->pub = pub;
    cd->users = 1;
    memcpy (cd->pass, pass, 8);
    if (titlelen >= sizeof (cd->title) - 1)
        titlelen = sizeof (cd->title) - 1;
    memcpy (cd->title, title, titlelen);
    cd->title[titlelen] = 0;

    cd->owner = (struct block_list **) (&cd->usersd[0]);
    cd->usersd[0] = sd;
    cd->bl.m = sd->bl.m;
    cd->bl.x = sd->bl.x;
    cd->bl.y = sd->bl.y;
    cd->bl.type = BL_CHAT;

    cd->bl.id = map_addobject (&cd->bl);
    if (cd->bl.id == 0)
    {
        clif_createchat (sd, 1);
        free (cd);
        return 0;
    }
    pc_setchatid (sd, cd->bl.id);

    clif_createchat (sd, 0);
    clif_dispchat (cd, 0);

    return 0;
}

/*==========================================
 * �����`���b�g���[���ɎQ��
 *------------------------------------------
 */
int chat_joinchat (struct map_session_data *sd, int chatid, char *pass)
{
    struct chat_data *cd;

    nullpo_retr (0, sd);

    cd = (struct chat_data *) map_id2bl (chatid);
    if (cd == NULL)
        return 1;

    if (cd->bl.m != sd->bl.m || cd->limit <= cd->users)
    {
        clif_joinchatfail (sd, 0);
        return 0;
    }
    if (cd->pub == 0 && strncmp (pass, cd->pass, 8))
    {
        clif_joinchatfail (sd, 1);
        return 0;
    }

    cd->usersd[cd->users] = sd;
    cd->users++;

    pc_setchatid (sd, cd->bl.id);

    clif_joinchatok (sd, cd);   // �V���ɎQ�������l�ɂ͑S���̃��X�g
    clif_addchat (cd, sd);      // ���ɒ��ɋ����l�ɂ͒ǉ������l�̕�
    clif_dispchat (cd, 0);      // ���͂̐l�ɂ͐l���ω���

    chat_triggerevent (cd);     // �C�x���g

    return 0;
}

/*==========================================
 * �`���b�g���[�����甲����
 *------------------------------------------
 */
int chat_leavechat (struct map_session_data *sd)
{
    struct chat_data *cd;
    int  i, leavechar;

    nullpo_retr (1, sd);

    cd = (struct chat_data *) map_id2bl (sd->chatID);
    if (cd == NULL)
        return 1;

    for (i = 0, leavechar = -1; i < cd->users; i++)
    {
        if (cd->usersd[i] == sd)
        {
            leavechar = i;
            break;
        }
    }
    if (leavechar < 0)          // ����chat�ɏ������Ă��Ȃ��炵�� (�o�O���̂�)
        return -1;

    if (leavechar == 0 && cd->users > 1 && (*cd->owner)->type == BL_PC)
    {
        // ���L�҂�����&���ɐl������&PC�̃`���b�g
        clif_changechatowner (cd, cd->usersd[1]);
        clif_clearchat (cd, 0);
    }

    // ������PC�ɂ�����̂�users�����炷�O�Ɏ��s
    clif_leavechat (cd, sd);

    cd->users--;
    pc_setchatid (sd, 0);

    if (cd->users == 0 && (*cd->owner)->type == BL_PC)
    {
        // �S�����Ȃ��Ȃ���&PC�̃`���b�g�Ȃ̂ŏ���
        clif_clearchat (cd, 0);
        map_delobject (cd->bl.id, BL_CHAT); // free�܂ł��Ă����
    }
    else
    {
        for (i = leavechar; i < cd->users; i++)
            cd->usersd[i] = cd->usersd[i + 1];
        if (leavechar == 0 && (*cd->owner)->type == BL_PC)
        {
            // PC�̃`���b�g�Ȃ̂ŏ��L�҂��������̂ňʒu�ύX
            cd->bl.x = cd->usersd[0]->bl.x;
            cd->bl.y = cd->usersd[0]->bl.y;
        }
        clif_dispchat (cd, 0);
    }

    return 0;
}

/*==========================================
 * �`���b�g���[���̎����������
 *------------------------------------------
 */
int chat_changechatowner (struct map_session_data *sd, char *nextownername)
{
    struct chat_data *cd;
    struct map_session_data *tmp_sd;
    int  i, nextowner;

    nullpo_retr (1, sd);

    cd = (struct chat_data *) map_id2bl (sd->chatID);
    if (cd == NULL || (struct block_list *) sd != (*cd->owner))
        return 1;

    for (i = 1, nextowner = -1; i < cd->users; i++)
    {
        if (strcmp (cd->usersd[i]->status.name, nextownername) == 0)
        {
            nextowner = i;
            break;
        }
    }
    if (nextowner < 0)          // ����Ȑl�͋��Ȃ�
        return -1;

    clif_changechatowner (cd, cd->usersd[nextowner]);
    // ��U����
    clif_clearchat (cd, 0);

    // userlist�̏��ԕύX (0�����L�҂Ȃ̂�)
    if ((tmp_sd = cd->usersd[0]) == NULL)
        return 1;               //���肦��̂��ȁH
    cd->usersd[0] = cd->usersd[nextowner];
    cd->usersd[nextowner] = tmp_sd;

    // �V�������L�҂̈ʒu�֕ύX
    cd->bl.x = cd->usersd[0]->bl.x;
    cd->bl.y = cd->usersd[0]->bl.y;

    // �ēx�\��
    clif_dispchat (cd, 0);

    return 0;
}

/*==========================================
 * �`���b�g�̏��(�^�C�g����)��ύX
 *------------------------------------------
 */
int chat_changechatstatus (struct map_session_data *sd, int limit, int pub,
                           char *pass, char *title, int titlelen)
{
    struct chat_data *cd;

    nullpo_retr (1, sd);

    cd = (struct chat_data *) map_id2bl (sd->chatID);
    if (cd == NULL || (struct block_list *) sd != (*cd->owner))
        return 1;

    cd->limit = limit;
    cd->pub = pub;
    memcpy (cd->pass, pass, 8);
    if (titlelen >= sizeof (cd->title) - 1)
        titlelen = sizeof (cd->title) - 1;
    memcpy (cd->title, title, titlelen);
    cd->title[titlelen] = 0;

    clif_changechatstatus (cd);
    clif_dispchat (cd, 0);

    return 0;
}

/*==========================================
 * �`���b�g���[������R��o��
 *------------------------------------------
 */
int chat_kickchat (struct map_session_data *sd, char *kickusername)
{
    struct chat_data *cd;
    int  i, kickuser;

    nullpo_retr (1, sd);

    cd = (struct chat_data *) map_id2bl (sd->chatID);
    if (cd == NULL || (struct block_list *) sd != (*cd->owner))
        return 1;

    for (i = 0, kickuser = -1; i < cd->users; i++)
    {
        if (strcmp (cd->usersd[i]->status.name, kickusername) == 0)
        {
            kickuser = i;
            break;
        }
    }
    if (kickuser < 0)           // ����Ȑl�͋��Ȃ�
        return -1;

    chat_leavechat (cd->usersd[kickuser]);

    return 0;
}

/*==========================================
 * npc�`���b�g���[���쐬
 *------------------------------------------
 */
int chat_createnpcchat (struct npc_data *nd, int limit, int pub, int trigger,
                        char *title, int titlelen, const char *ev)
{
    struct chat_data *cd;

    nullpo_retr (1, nd);

    cd = aCalloc (1, sizeof (struct chat_data));

    cd->limit = cd->trigger = limit;
    if (trigger > 0)
        cd->trigger = trigger;
    cd->pub = pub;
    cd->users = 0;
    memcpy (cd->pass, "", 8);
    if (titlelen >= sizeof (cd->title) - 1)
        titlelen = sizeof (cd->title) - 1;
    memcpy (cd->title, title, titlelen);
    cd->title[titlelen] = 0;

    cd->bl.m = nd->bl.m;
    cd->bl.x = nd->bl.x;
    cd->bl.y = nd->bl.y;
    cd->bl.type = BL_CHAT;
    cd->owner_ = (struct block_list *) nd;
    cd->owner = &cd->owner_;
    memcpy (cd->npc_event, ev, sizeof (cd->npc_event));

    cd->bl.id = map_addobject (&cd->bl);
    if (cd->bl.id == 0)
    {
        free (cd);
        return 0;
    }
    nd->chat_id = cd->bl.id;

    clif_dispchat (cd, 0);

    return 0;
}

/*==========================================
 * npc�`���b�g���[���폜
 *------------------------------------------
 */
int chat_deletenpcchat (struct npc_data *nd)
{
    struct chat_data *cd;

    nullpo_retr (0, nd);
    nullpo_retr (0, cd = (struct chat_data *) map_id2bl (nd->chat_id));

    chat_npckickall (cd);
    clif_clearchat (cd, 0);
    map_delobject (cd->bl.id, BL_CHAT); // free�܂ł��Ă����
    nd->chat_id = 0;

    return 0;
}

/*==========================================
 * �K��l���ȏ�ŃC�x���g����`����Ă�Ȃ���s
 *------------------------------------------
 */
int chat_triggerevent (struct chat_data *cd)
{
    nullpo_retr (0, cd);

    if (cd->users >= cd->trigger && cd->npc_event[0])
        npc_event_do (cd->npc_event);
    return 0;
}

/*==========================================
 * �C�x���g�̗L����
 *------------------------------------------
 */
int chat_enableevent (struct chat_data *cd)
{
    nullpo_retr (0, cd);

    cd->trigger &= 0x7f;
    chat_triggerevent (cd);
    return 0;
}

/*==========================================
 * �C�x���g�̖�����
 *------------------------------------------
 */
int chat_disableevent (struct chat_data *cd)
{
    nullpo_retr (0, cd);

    cd->trigger |= 0x80;
    return 0;
}

/*==========================================
 * �`���b�g���[������S���R��o��
 *------------------------------------------
 */
int chat_npckickall (struct chat_data *cd)
{
    nullpo_retr (0, cd);

    while (cd->users > 0)
    {
        chat_leavechat (cd->usersd[cd->users - 1]);
    }
    return 0;
}

/*==========================================
 * �I��
 *------------------------------------------
 */
int do_final_chat (void)
{
    return 0;
}
