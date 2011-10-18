// $Id: chrif.c,v 1.6 2004/09/25 11:39:17 MouseJstr Exp $
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef LCCWIN32
#include <winsock.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <sys/types.h>
#include <time.h>

#include "socket.h"
#include "timer.h"
#include "map.h"
#include "battle.h"
#include "chrif.h"
#include "clif.h"
#include "intif.h"
#include "npc.h"
#include "pc.h"
#include "nullpo.h"
#include "itemdb.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

static const int packet_len_table[0x20] = {
    60, 3, 10, 27, 22, -1, 6, -1,   // 2af8-2aff
    6, -1, 18, 7, -1, 49, 44, 0,    // 2b00-2b07
    6, 30, -1, 10, 86, 7, 44, 34,   // 2b08-2b0f
    -1, -1, 10, 6, 11, -1, 0, 0,    // 2b10-2b17
};

int  char_fd;
int  srvinfo;
static char char_ip_str[16];
static int char_ip;
static int char_port = 6121;
static char userid[24], passwd[24];
static int chrif_state;

// �ݒ�t�@�C���ǂݍ��݊֌W
/*==========================================
 *
 *------------------------------------------
 */
void chrif_setuserid (char *id)
{
    strncpy (userid, id, sizeof(userid)-1);
    userid[sizeof(userid)-1] = '\0';
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_setpasswd (char *pwd)
{
    strncpy (passwd, pwd, sizeof(passwd)-1);
    passwd[sizeof(passwd)-1] = '\0';
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_setip (char *ip)
{
    strncpy (char_ip_str, ip, sizeof(char_ip_str)-1);
    char_ip_str[sizeof(char_ip_str)-1] = '\0';
    char_ip = inet_addr (char_ip_str);
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_setport (int port)
{
    char_port = port;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_isconnect (void)
{
    return chrif_state == 2;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_save (struct map_session_data *sd)
{
    nullpo_retr (-1, sd);

    if (char_fd < 0)
        return -1;

    pc_makesavestatus (sd);

    WFIFOW (char_fd, 0) = 0x2b01;
    WFIFOW (char_fd, 2) = sizeof (sd->status) + 12;
    WFIFOL (char_fd, 4) = sd->bl.id;
    WFIFOL (char_fd, 8) = sd->char_id;
    memcpy (WFIFOP (char_fd, 12), &sd->status, sizeof (sd->status));
    WFIFOSET (char_fd, WFIFOW (char_fd, 2));

    //For data sync
    if (sd->state.storage_flag == 1)
        storage_storage_save (sd->status.account_id, 0);
    else if (sd->state.storage_flag == 2)
        storage_guild_storagesave (sd->status.account_id, sd->status.guild_id,
                                   0);

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_connect (int fd)
{
    WFIFOW (fd, 0) = 0x2af8;
    memcpy (WFIFOP (fd, 2), userid, 24);
    memcpy (WFIFOP (fd, 26), passwd, 24);
    WFIFOL (fd, 50) = 0;
    WFIFOL (fd, 54) = clif_getip ();
    WFIFOW (fd, 58) = clif_getport ();  // [Valaris] thanks to fov
    WFIFOSET (fd, 60);

    return 0;
}

/*==========================================
 * �}�b�v���M
 *------------------------------------------
 */
int chrif_sendmap (int fd)
{
    int  i;

    WFIFOW (fd, 0) = 0x2afa;
    for (i = 0; i < map_num; i++)
        if (map[i].alias[0] != '\0')    // [MouseJstr] map aliasing
            memcpy (WFIFOP (fd, 4 + i * 16), map[i].alias, 16);
        else
            memcpy (WFIFOP (fd, 4 + i * 16), map[i].name, 16);
    WFIFOW (fd, 2) = 4 + i * 16;
    WFIFOSET (fd, WFIFOW (fd, 2));

    return 0;
}

/*==========================================
 * �}�b�v��M
 *------------------------------------------
 */
int chrif_recvmap (int fd)
{
    int  i, j, ip, port;
    unsigned char *p = (unsigned char *) &ip;

    if (chrif_state < 2)        // �܂�������
        return -1;

    ip = RFIFOL (fd, 4);
    port = RFIFOW (fd, 8);
    for (i = 10, j = 0; i < RFIFOW (fd, 2); i += 16, j++)
    {
        map_setipport (RFIFOP (fd, i), ip, port);
//      if (battle_config.etc_log)
//          printf("recv map %d %s\n", j, RFIFOP(fd,i));
    }
    if (battle_config.etc_log)
        printf ("recv map on %d.%d.%d.%d:%d (%d maps)\n", p[0], p[1], p[2],
                p[3], port, j);

    return 0;
}

/*==========================================
 * �}�b�v�I�Ԉړ��̂��߂̃f�[�^�����v��
 *------------------------------------------
 */
int chrif_changemapserver (struct map_session_data *sd, char *name, int x,
                           int y, int ip, short port)
{
    int  i, s_ip;

    nullpo_retr (-1, sd);

    s_ip = 0;
    for (i = 0; i < fd_max; i++)
        if (session[i] && session[i]->session_data == sd)
        {
            s_ip = session[i]->client_addr.sin_addr.s_addr;
            break;
        }

    WFIFOW (char_fd, 0) = 0x2b05;
    WFIFOL (char_fd, 2) = sd->bl.id;
    WFIFOL (char_fd, 6) = sd->login_id1;
    WFIFOL (char_fd, 10) = sd->login_id2;
    WFIFOL (char_fd, 14) = sd->status.char_id;
    memcpy (WFIFOP (char_fd, 18), name, 16);
    WFIFOW (char_fd, 34) = x;
    WFIFOW (char_fd, 36) = y;
    WFIFOL (char_fd, 38) = ip;
    WFIFOL (char_fd, 42) = port;
    WFIFOB (char_fd, 44) = sd->status.sex;
    WFIFOL (char_fd, 45) = s_ip;
    WFIFOSET (char_fd, 49);

    return 0;
}

/*==========================================
 * �}�b�v�I�Ԉړ�ack
 *------------------------------------------
 */
int chrif_changemapserverack (int fd)
{
    struct map_session_data *sd = map_id2sd (RFIFOL (fd, 2));

    if (sd == NULL || sd->status.char_id != RFIFOL (fd, 14))
        return -1;

    if (RFIFOL (fd, 6) == 1)
    {
        if (battle_config.error_log)
            printf ("map server change failed.\n");
        pc_authfail (sd->fd);
        return 0;
    }
    clif_changemapserver (sd, RFIFOP (fd, 18), RFIFOW (fd, 34),
                          RFIFOW (fd, 36), RFIFOL (fd, 38), RFIFOW (fd, 42));

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_connectack (int fd)
{
    if (RFIFOB (fd, 2))
    {
        printf ("Connected to char-server failed %d.\n", RFIFOB (fd, 2));
        exit (1);
    }
    printf ("Connected to char-server (connection #%d).\n", fd);
    chrif_state = 1;

    chrif_sendmap (fd);

    printf ("chrif: OnCharIfInit event done. (%d events)\n",
            npc_event_doall ("OnCharIfInit"));
    printf ("chrif: OnInterIfInit event done. (%d events)\n",
            npc_event_doall ("OnInterIfInit"));

    // <Agit> Run Event [AgitInit]
//  printf("NPC_Event:[OnAgitInit] do (%d) events (Agit Initialize).\n", npc_event_doall("OnAgitInit"));

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_sendmapack (int fd)
{
    if (RFIFOB (fd, 2))
    {
        printf ("chrif : send map list to char server failed %d\n",
                RFIFOB (fd, 2));
        exit (1);
    }

    memcpy (wisp_server_name, RFIFOP (fd, 3), 24);

    chrif_state = 2;

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_authreq (struct map_session_data *sd)
{
    int  i;

    nullpo_retr (-1, sd);

    if (!sd || !char_fd || !sd->bl.id || !sd->login_id1)
        return -1;

    for (i = 0; i < fd_max; i++)
        if (session[i] && session[i]->session_data == sd)
        {
            WFIFOW (char_fd, 0) = 0x2afc;
            WFIFOL (char_fd, 2) = sd->bl.id;
            WFIFOL (char_fd, 6) = sd->char_id;
            WFIFOL (char_fd, 10) = sd->login_id1;
            WFIFOL (char_fd, 14) = sd->login_id2;
            WFIFOL (char_fd, 18) = session[i]->client_addr.sin_addr.s_addr;
            WFIFOSET (char_fd, 22);
            break;
        }

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_charselectreq (struct map_session_data *sd)
{
    int  i, s_ip;

    nullpo_retr (-1, sd);

    if (!sd || !char_fd || !sd->bl.id || !sd->login_id1)
        return -1;

    s_ip = 0;
    for (i = 0; i < fd_max; i++)
        if (session[i] && session[i]->session_data == sd)
        {
            s_ip = session[i]->client_addr.sin_addr.s_addr;
            break;
        }

    WFIFOW (char_fd, 0) = 0x2b02;
    WFIFOL (char_fd, 2) = sd->bl.id;
    WFIFOL (char_fd, 6) = sd->login_id1;
    WFIFOL (char_fd, 10) = sd->login_id2;
    WFIFOL (char_fd, 14) = s_ip;
    WFIFOSET (char_fd, 18);

    return 0;
}

/*==========================================
 * �L�������₢���킹
 *------------------------------------------
 */
int chrif_searchcharid (int char_id)
{
    if (!char_id)
        return -1;

    WFIFOW (char_fd, 0) = 0x2b08;
    WFIFOL (char_fd, 2) = char_id;
    WFIFOSET (char_fd, 6);

    return 0;
}

/*==========================================
 * GM�ɕω��v��
 *------------------------------------------
 */
int chrif_changegm (int id, const char *pass, int len)
{
    if (battle_config.etc_log)
        printf ("chrif_changegm: account: %d, password: '%s'.\n", id, pass);

    WFIFOW (char_fd, 0) = 0x2b0a;
    WFIFOW (char_fd, 2) = len + 8;
    WFIFOL (char_fd, 4) = id;
    memcpy (WFIFOP (char_fd, 8), pass, len);
    WFIFOSET (char_fd, len + 8);

    return 0;
}

/*==========================================
 * Change Email
 *------------------------------------------
 */
int chrif_changeemail (int id, const char *actual_email,
                       const char *new_email)
{
    if (battle_config.etc_log)
        printf
            ("chrif_changeemail: account: %d, actual_email: '%s', new_email: '%s'.\n",
             id, actual_email, new_email);

    WFIFOW (char_fd, 0) = 0x2b0c;
    WFIFOL (char_fd, 2) = id;
    memcpy (WFIFOP (char_fd, 6), actual_email, 40);
    memcpy (WFIFOP (char_fd, 46), new_email, 40);
    WFIFOSET (char_fd, 86);

    return 0;
}

/*==========================================
 * Send message to char-server with a character name to do some operations (by Yor)
 * Used to ask Char-server about a character name to have the account number to modify account file in login-server.
 * type of operation:
 *   1: block
 *   2: ban
 *   3: unblock
 *   4: unban
 *   5: changesex
 *------------------------------------------
 */
int chrif_char_ask_name (int id, char *character_name, short operation_type,
                         int year, int month, int day, int hour, int minute,
                         int second)
{
    WFIFOW (char_fd, 0) = 0x2b0e;
    WFIFOL (char_fd, 2) = id;   // account_id of who ask (for answer) -1 if nobody
    memcpy (WFIFOP (char_fd, 6), character_name, 24);
    WFIFOW (char_fd, 30) = operation_type;  // type of operation
    if (operation_type == 2)
    {
        WFIFOW (char_fd, 32) = year;
        WFIFOW (char_fd, 34) = month;
        WFIFOW (char_fd, 36) = day;
        WFIFOW (char_fd, 38) = hour;
        WFIFOW (char_fd, 40) = minute;
        WFIFOW (char_fd, 42) = second;
    }
    printf ("chrif : sended 0x2b0e\n");
    WFIFOSET (char_fd, 44);

    return 0;
}

/*==========================================
 * Answer after a request about a character name to do some operations (by Yor)
 * Used to answer of chrif_char_ask_name.
 * type of operation:
 *   1: block
 *   2: ban
 *   3: unblock
 *   4: unban
 *   5: changesex
 * type of answer:
 *   0: login-server resquest done
 *   1: player not found
 *   2: gm level too low
 *   3: login-server offline
 *------------------------------------------
 */
int chrif_char_ask_name_answer (int fd)
{
    int  acc;
    struct map_session_data *sd;
    char output[256];
    char player_name[24];

    acc = RFIFOL (fd, 2);       // account_id of who has asked (-1 if nobody)
    memcpy (player_name, RFIFOP (fd, 6), sizeof (player_name));
    player_name[sizeof (player_name) - 1] = '\0';

    sd = map_id2sd (acc);
    if (acc >= 0 && sd != NULL)
    {
        if (RFIFOW (fd, 32) == 1)   // player not found
            sprintf (output, "The player '%s' doesn't exist.", player_name);
        else
        {
            switch (RFIFOW (fd, 30))
            {
                case 1:        // block
                    switch (RFIFOW (fd, 32))
                    {
                        case 0:    // login-server resquest done
                            sprintf (output,
                                     "Login-server has been asked to block the player '%s'.",
                                     player_name);
                            break;
                            //case 1: // player not found
                        case 2:    // gm level too low
                            sprintf (output,
                                     "Your GM level don't authorise you to block the player '%s'.",
                                     player_name);
                            break;
                        case 3:    // login-server offline
                            sprintf (output,
                                     "Login-server is offline. Impossible to block the the player '%s'.",
                                     player_name);
                            break;
                    }
                    break;
                case 2:        // ban
                    switch (RFIFOW (fd, 32))
                    {
                        case 0:    // login-server resquest done
                            sprintf (output,
                                     "Login-server has been asked to ban the player '%s'.",
                                     player_name);
                            break;
                            //case 1: // player not found
                        case 2:    // gm level too low
                            sprintf (output,
                                     "Your GM level don't authorise you to ban the player '%s'.",
                                     player_name);
                            break;
                        case 3:    // login-server offline
                            sprintf (output,
                                     "Login-server is offline. Impossible to ban the the player '%s'.",
                                     player_name);
                            break;
                    }
                    break;
                case 3:        // unblock
                    switch (RFIFOW (fd, 32))
                    {
                        case 0:    // login-server resquest done
                            sprintf (output,
                                     "Login-server has been asked to unblock the player '%s'.",
                                     player_name);
                            break;
                            //case 1: // player not found
                        case 2:    // gm level too low
                            sprintf (output,
                                     "Your GM level don't authorise you to unblock the player '%s'.",
                                     player_name);
                            break;
                        case 3:    // login-server offline
                            sprintf (output,
                                     "Login-server is offline. Impossible to unblock the the player '%s'.",
                                     player_name);
                            break;
                    }
                    break;
                case 4:        // unban
                    switch (RFIFOW (fd, 32))
                    {
                        case 0:    // login-server resquest done
                            sprintf (output,
                                     "Login-server has been asked to unban the player '%s'.",
                                     player_name);
                            break;
                            //case 1: // player not found
                        case 2:    // gm level too low
                            sprintf (output,
                                     "Your GM level don't authorise you to unban the player '%s'.",
                                     player_name);
                            break;
                        case 3:    // login-server offline
                            sprintf (output,
                                     "Login-server is offline. Impossible to unban the the player '%s'.",
                                     player_name);
                            break;
                    }
                    break;
                case 5:        // changesex
                    switch (RFIFOW (fd, 32))
                    {
                        case 0:    // login-server resquest done
                            sprintf (output,
                                     "Login-server has been asked to change the sex of the player '%s'.",
                                     player_name);
                            break;
                            //case 1: // player not found
                        case 2:    // gm level too low
                            sprintf (output,
                                     "Your GM level don't authorise you to change the sex of the player '%s'.",
                                     player_name);
                            break;
                        case 3:    // login-server offline
                            sprintf (output,
                                     "Login-server is offline. Impossible to change the sex of the the player '%s'.",
                                     player_name);
                            break;
                    }
                    break;
            }
        }
        if (output[0] != '\0')
        {
            output[sizeof (output) - 1] = '\0';
            clif_displaymessage (sd->fd, output);
        }
    }
    else
        printf ("chrif_char_ask_name_answer failed - player not online.\n");

    return 0;
}

/*==========================================
 * End of GM change (@GM) (modified by Yor)
 *------------------------------------------
 */
int chrif_changedgm (int fd)
{
    int  acc, level;
    struct map_session_data *sd = NULL;

    acc = RFIFOL (fd, 2);
    level = RFIFOL (fd, 6);

    sd = map_id2sd (acc);

    if (battle_config.etc_log)
        printf ("chrif_changedgm: account: %d, GM level 0 -> %d.\n", acc,
                level);
    if (sd != NULL)
    {
        if (level > 0)
            clif_displaymessage (sd->fd, "GM modification success.");
        else
            clif_displaymessage (sd->fd, "Failure of GM modification.");
    }

    return 0;
}

/*==========================================
 * ���ʕω��I�� (modified by Yor)
 *------------------------------------------
 */
int chrif_changedsex (int fd)
{
    int  acc, sex, i;
    struct map_session_data *sd;
    struct pc_base_job s_class;

    acc = RFIFOL (fd, 2);
    sex = RFIFOL (fd, 6);
    if (battle_config.etc_log)
        printf ("chrif_changedsex %d.\n", acc);
    sd = map_id2sd (acc);
    if (acc > 0)
    {
        if (sd != NULL && sd->status.sex != sex)
        {
            s_class = pc_calc_base_job (sd->status.class);
            if (sd->status.sex == 0)
            {
                sd->status.sex = 1;
                sd->sex = 1;
            }
            else if (sd->status.sex == 1)
            {
                sd->status.sex = 0;
                sd->sex = 0;
            }
            // to avoid any problem with equipment and invalid sex, equipment is unequiped.
            for (i = 0; i < MAX_INVENTORY; i++)
            {
                if (sd->status.inventory[i].nameid
                    && sd->status.inventory[i].equip)
                    pc_unequipitem ((struct map_session_data *) sd, i, 0);
            }
            // reset skill of some job
            if (s_class.job == 19 || s_class.job == 4020
                || s_class.job == 4042 || s_class.job == 20
                || s_class.job == 4021 || s_class.job == 4043)
            {

                clif_updatestatus (sd, SP_SKILLPOINT);
                // change job if necessary
                if (s_class.job == 20 || s_class.job == 4021
                    || s_class.job == 4043)
                    sd->status.class -= 1;
                else if (s_class.job == 19 || s_class.job == 4020
                         || s_class.job == 4042)
                    sd->status.class += 1;
            }
            // save character
            chrif_save (sd);
            sd->login_id1++;    // change identify, because if player come back in char within the 5 seconds, he can change its characters
            // do same modify in login-server for the account, but no in char-server (it ask again login_id1 to login, and don't remember it)
            clif_displaymessage (sd->fd,
                                 "Your sex has been changed (need disconexion by the server)...");
            clif_setwaitclose (sd->fd); // forced to disconnect for the change
        }
    }
    else
    {
        if (sd != NULL)
        {
            printf ("chrif_changedsex failed.\n");
        }
    }

    return 0;
}

/*==========================================
 * �A�J�E���g�ϐ��ۑ��v��
 *------------------------------------------
 */
int chrif_saveaccountreg2 (struct map_session_data *sd)
{
    int  p, j;
    nullpo_retr (-1, sd);

    p = 8;
    for (j = 0; j < sd->status.account_reg2_num; j++)
    {
        struct global_reg *reg = &sd->status.account_reg2[j];
        if (reg->str[0] && reg->value != 0)
        {
            memcpy (WFIFOP (char_fd, p), reg->str, 32);
            WFIFOL (char_fd, p + 32) = reg->value;
            p += 36;
        }
    }
    WFIFOW (char_fd, 0) = 0x2b10;
    WFIFOW (char_fd, 2) = p;
    WFIFOL (char_fd, 4) = sd->bl.id;
    WFIFOSET (char_fd, p);

    return 0;
}

/*==========================================
 * �A�J�E���g�ϐ��ʒm
 *------------------------------------------
 */
int chrif_accountreg2 (int fd)
{
    int  j, p;
    struct map_session_data *sd;

    if ((sd = map_id2sd (RFIFOL (fd, 4))) == NULL)
        return 1;

    for (p = 8, j = 0; p < RFIFOW (fd, 2) && j < ACCOUNT_REG2_NUM;
         p += 36, j++)
    {
        memcpy (sd->status.account_reg2[j].str, RFIFOP (fd, p), 32);
        sd->status.account_reg2[j].value = RFIFOL (fd, p + 32);
    }
    sd->status.account_reg2_num = j;
//  printf("chrif: accountreg2\n");

    return 0;
}

/*==========================================
 * Divorce request from char server
 * triggered on account deletion or as an
 * ack from a map-server divorce request
 *------------------------------------------
 */
int chrif_divorce (int char_id, int partner_id)
{
    struct map_session_data *sd = NULL;

    if (!char_id || !partner_id)
        return 0;

    sd = map_nick2sd (map_charid2nick (char_id));
    if (sd && sd->status.partner_id == partner_id)
    {
        sd->status.partner_id = 0;

        if (sd->npc_flags.divorce)
        {
            sd->npc_flags.divorce = 0;
            map_scriptcont (sd, sd->npc_id);
        }
    }

    nullpo_retr (0, sd = map_nick2sd (map_charid2nick (partner_id)));
    if (sd->status.partner_id == char_id)
        sd->status.partner_id = 0;

    return 0;
}

/*==========================================
 * Tell character server someone is divorced
 * Needed to divorce when partner is not connected to map server
 *-------------------------------------
 */
int chrif_send_divorce (int char_id)
{
    if (char_fd < 0)
        return -1;

    WFIFOW (char_fd, 0) = 0x2b16;
    WFIFOL (char_fd, 2) = char_id;
    WFIFOSET (char_fd, 6);
    return 0;
}

/*==========================================
 * Disconnection of a player (account has been deleted in login-server) by [Yor]
 *------------------------------------------
 */
int chrif_accountdeletion (int fd)
{
    int  acc;
    struct map_session_data *sd;

    acc = RFIFOL (fd, 2);
    if (battle_config.etc_log)
        printf ("chrif_accountdeletion %d.\n", acc);
    sd = map_id2sd (acc);
    if (acc > 0)
    {
        if (sd != NULL)
        {
            sd->login_id1++;    // change identify, because if player come back in char within the 5 seconds, he can change its characters
            clif_displaymessage (sd->fd,
                                 "Your account has been deleted (disconnection)...");
            clif_setwaitclose (sd->fd); // forced to disconnect for the change
        }
    }
    else
    {
        if (sd != NULL)
            printf ("chrif_accountdeletion failed - player not online.\n");
    }

    return 0;
}

/*==========================================
 * Disconnection of a player (account has been banned of has a status, from login-server) by [Yor]
 *------------------------------------------
 */
int chrif_accountban (int fd)
{
    int  acc;
    struct map_session_data *sd;

    acc = RFIFOL (fd, 2);
    if (battle_config.etc_log)
        printf ("chrif_accountban %d.\n", acc);
    sd = map_id2sd (acc);
    if (acc > 0)
    {
        if (sd != NULL)
        {
            sd->login_id1++;    // change identify, because if player come back in char within the 5 seconds, he can change its characters
            if (RFIFOB (fd, 6) == 0)
            {                   // 0: change of statut, 1: ban
                switch (RFIFOL (fd, 7))
                {               // status or final date of a banishment
                    case 1:    // 0 = Unregistered ID
                        clif_displaymessage (sd->fd,
                                             "Your account has 'Unregistered'.");
                        break;
                    case 2:    // 1 = Incorrect Password
                        clif_displaymessage (sd->fd,
                                             "Your account has an 'Incorrect Password'...");
                        break;
                    case 3:    // 2 = This ID is expired
                        clif_displaymessage (sd->fd,
                                             "Your account has expired.");
                        break;
                    case 4:    // 3 = Rejected from Server
                        clif_displaymessage (sd->fd,
                                             "Your account has been rejected from server.");
                        break;
                    case 5:    // 4 = You have been blocked by the GM Team
                        clif_displaymessage (sd->fd,
                                             "Your account has been blocked by the GM Team.");
                        break;
                    case 6:    // 5 = Your Game's EXE file is not the latest version
                        clif_displaymessage (sd->fd,
                                             "Your Game's EXE file is not the latest version.");
                        break;
                    case 7:    // 6 = Your are Prohibited to log in until %s
                        clif_displaymessage (sd->fd,
                                             "Your account has been prohibited to log in.");
                        break;
                    case 8:    // 7 = Server is jammed due to over populated
                        clif_displaymessage (sd->fd,
                                             "Server is jammed due to over populated.");
                        break;
                    case 9:    // 8 = No MSG (actually, all states after 9 except 99 are No MSG, use only this)
                        clif_displaymessage (sd->fd,
                                             "Your account has not more authorised.");
                        break;
                    case 100:  // 99 = This ID has been totally erased
                        clif_displaymessage (sd->fd,
                                             "Your account has been totally erased.");
                        break;
                    default:
                        clif_displaymessage (sd->fd,
                                             "Your account has not more authorised.");
                        break;
                }
            }
            else if (RFIFOB (fd, 6) == 1)
            {                   // 0: change of statut, 1: ban
                time_t timestamp;
                char tmpstr[2048];
                timestamp = (time_t) RFIFOL (fd, 7);    // status or final date of a banishment
                strcpy (tmpstr, "Your account has been banished until ");
                strftime (tmpstr + strlen (tmpstr), 24, "%d-%m-%Y %H:%M:%S",
                          gmtime (&timestamp));
                clif_displaymessage (sd->fd, tmpstr);
            }
            clif_setwaitclose (sd->fd); // forced to disconnect for the change
        }
    }
    else
    {
        if (sd != NULL)
            printf ("chrif_accountban failed - player not online.\n");
    }

    return 0;
}

/*==========================================
 * Receiving GM accounts and their levels from char-server by [Yor]
 *------------------------------------------
 */
int chrif_recvgmaccounts (int fd)
{
    printf ("From login-server: receiving of %d GM accounts information.\n",
            pc_read_gm_account (fd));

    return 0;
}

/*==========================================
 * Request to reload GM accounts and their levels: send to char-server by [Yor]
 *------------------------------------------
 */
int chrif_reloadGMdb (void)
{

    WFIFOW (char_fd, 0) = 0x2af7;
    WFIFOSET (char_fd, 2);

    return 0;
}

/*==========================================
 * Send rates and motd to char server [Wizputer]
 *------------------------------------------
 */
int chrif_ragsrvinfo (int base_rate, int job_rate, int drop_rate)
{
    char buf[256];
    FILE *fp;
    int  i;

    WFIFOW (char_fd, 0) = 0x2b16;
    WFIFOW (char_fd, 2) = base_rate;
    WFIFOW (char_fd, 4) = job_rate;
    WFIFOW (char_fd, 6) = drop_rate;

    if ((fp = fopen_ (motd_txt, "r")) != NULL)
    {
        if (fgets (buf, 250, fp) != NULL)
        {
            for (i = 0; buf[i]; i++)
            {
                if (buf[i] == '\r' || buf[i] == '\n')
                {
                    buf[i] = 0;
                    break;
                }
            }
            WFIFOW (char_fd, 8) = sizeof (buf) + 10;
            memcpy (WFIFOP (char_fd, 10), buf, sizeof (buf));
        }
        fclose_ (fp);
    }
    else
    {
        WFIFOW (char_fd, 8) = sizeof (buf) + 10;
        memcpy (WFIFOP (char_fd, 10), buf, sizeof (buf));
    }
    WFIFOSET (char_fd, WFIFOW (char_fd, 8));

    return 0;
}

/*=========================================
 * Tell char-server charcter disconnected [Wizputer]
 *-----------------------------------------
 */

int chrif_char_offline (struct map_session_data *sd)
{
    if (char_fd < 0)
        return -1;

    WFIFOW (char_fd, 0) = 0x2b17;
    WFIFOL (char_fd, 2) = sd->status.char_id;
    WFIFOSET (char_fd, 6);

    return 0;
}

/*========================================
 * Map item IDs
 *----------------------------------------
 */

static void ladmin_itemfrob_fix_item (int source, int dest, struct item *item)
{
    if (item && item->nameid == source)
    {
        item->nameid = dest;
        item->equip = 0;
    }
}

static int ladmin_itemfrob_c2 (struct block_list *bl, int source_id,
                               int dest_id)
{
#define IFIX(v) if (v == source_id) {v = dest_id; }
#define FIX(item) ladmin_itemfrob_fix_item(source_id, dest_id, &item)

    if (!bl)
        return 0;

    switch (bl->type)
    {
        case BL_PC:
        {
            struct map_session_data *pc = (struct map_session_data *) bl;
            struct storage *stor = account2storage2 (pc->status.account_id);
            int  j;

            for (j = 0; j < MAX_INVENTORY; j++)
                IFIX (pc->status.inventory[j].nameid);
            for (j = 0; j < MAX_CART; j++)
                IFIX (pc->status.cart[j].nameid);
            IFIX (pc->status.weapon);
            IFIX (pc->status.shield);
            IFIX (pc->status.head_top);
            IFIX (pc->status.head_mid);
            IFIX (pc->status.head_bottom);

            if (stor)
                for (j = 0; j < stor->storage_amount; j++)
                    FIX (stor->storage_[j]);

            for (j = 0; j < MAX_INVENTORY; j++)
            {
                struct item_data *item = pc->inventory_data[j];
                if (item && item->nameid == source_id)
                {
                    item->nameid = dest_id;
                    if (item->equip)
                        pc_unequipitem (pc, j, 0);
                    item->nameid = dest_id;
                }
            }

            break;
        }

        case BL_MOB:
        {
            struct mob_data *mob = (struct mob_data *) bl;
            int  i;
            for (i = 0; i < mob->lootitem_count; i++)
                FIX (mob->lootitem[i]);
            break;
        }

        case BL_ITEM:
        {
            struct flooritem_data *item = (struct flooritem_data *) bl;
            FIX (item->item_data);
            break;
        }
    }
#undef FIX
#undef IFIX

    return 0;
}

int ladmin_itemfrob_c (struct block_list *bl, va_list va_args)
{
    int  source_id = va_arg (va_args, int);
    int  dest_id = va_arg (va_args, int);
    return ladmin_itemfrob_c2 (bl, source_id, dest_id);
}

void ladmin_itemfrob (int fd)
{
    int  source_id = RFIFOL (fd, 2);
    int  dest_id = RFIFOL (fd, 6);
    struct block_list *bl = (struct block_list *) map_get_first_session ();

    // flooritems
    map_foreachobject (ladmin_itemfrob_c, 0 /* any object */ , source_id,
                       dest_id);

    // player characters (and, hopefully, mobs)
    while (bl->next)
    {
        ladmin_itemfrob_c2 (bl, source_id, dest_id);
        bl = bl->next;
    }
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_parse (int fd)
{
    int  packet_len, cmd;

    // only char-server can have an access to here.
    // so, if it isn't the char-server, we disconnect the session (fd != char_fd).
    if (fd != char_fd || session[fd]->eof)
    {
        if (fd == char_fd)
        {
            printf
                ("Map-server can't connect to char-server (connection #%d).\n",
                 fd);
            char_fd = -1;
        }
        close (fd);
        delete_session (fd);
        return 0;
    }

    while (RFIFOREST (fd) >= 2)
    {
        cmd = RFIFOW (fd, 0);
        if (cmd < 0x2af8
            || cmd >=
            0x2af8 +
            (sizeof (packet_len_table) / sizeof (packet_len_table[0]))
            || packet_len_table[cmd - 0x2af8] == 0)
        {

            int  r = intif_parse (fd);  // intif�ɓn��

            if (r == 1)
                continue;       // intif�ŏ�������
            if (r == 2)
                return 0;       // intif�ŏ����������A�f�[�^������Ȃ�

            session[fd]->eof = 1;
            return 0;
        }
        packet_len = packet_len_table[cmd - 0x2af8];
        if (packet_len == -1)
        {
            if (RFIFOREST (fd) < 4)
                return 0;
            packet_len = RFIFOW (fd, 2);
        }
        if (RFIFOREST (fd) < packet_len)
            return 0;

        switch (cmd)
        {
            case 0x2af9:
                chrif_connectack (fd);
                break;
            case 0x2afa:
                ladmin_itemfrob (fd);
                break;
            case 0x2afb:
                chrif_sendmapack (fd);
                break;
            case 0x2afd:
                pc_authok (RFIFOL (fd, 4), RFIFOL (fd, 8),
                           (time_t) RFIFOL (fd, 12), RFIFOW (fd, 16),
                           (struct mmo_charstatus *) RFIFOP (fd, 18));
                break;
            case 0x2afe:
                pc_authfail (RFIFOL (fd, 2));
                break;
            case 0x2b00:
                map_setusers (RFIFOL (fd, 2));
                break;
            case 0x2b03:
                clif_charselectok (RFIFOL (fd, 2));
                break;
            case 0x2b04:
                chrif_recvmap (fd);
                break;
            case 0x2b06:
                chrif_changemapserverack (fd);
                break;
            case 0x2b09:
                map_addchariddb (RFIFOL (fd, 2), RFIFOP (fd, 6));
                break;
            case 0x2b0b:
                chrif_changedgm (fd);
                break;
            case 0x2b0d:
                chrif_changedsex (fd);
                break;
            case 0x2b0f:
                chrif_char_ask_name_answer (fd);
                break;
            case 0x2b11:
                chrif_accountreg2 (fd);
                break;
            case 0x2b12:
                chrif_divorce (RFIFOL (fd, 2), RFIFOL (fd, 6));
                break;
            case 0x2b13:
                chrif_accountdeletion (fd);
                break;
            case 0x2b14:
                chrif_accountban (fd);
                break;
            case 0x2b15:
                chrif_recvgmaccounts (fd);
                break;

            default:
                if (battle_config.error_log)
                    printf ("chrif_parse : unknown packet %d %d\n", fd,
                            RFIFOW (fd, 0));
                session[fd]->eof = 1;
                return 0;
        }
        RFIFOSKIP (fd, packet_len);
    }

    return 0;
}

/*==========================================
 * timer�֐�
 * ������map�I�Ɍq�����Ă���N���C�A���g�l����char�I�֑���
 *------------------------------------------
 */
int send_users_tochar (int tid, unsigned int tick, int id, int data)
{
    int  users = 0, i;
    struct map_session_data *sd;

    if (char_fd <= 0 || session[char_fd] == NULL)
        return 0;

    WFIFOW (char_fd, 0) = 0x2aff;
    for (i = 0; i < fd_max; i++)
    {
        if (session[i] && (sd = session[i]->session_data) && sd->state.auth &&
            !((battle_config.hide_GM_session
               || sd->state.shroud_active
               || (sd->status.option & OPTION_HIDE)) && pc_isGM (sd)))
        {
            WFIFOL (char_fd, 6 + 4 * users) = sd->status.char_id;
            users++;
        }
    }
    WFIFOW (char_fd, 2) = 6 + 4 * users;
    WFIFOW (char_fd, 4) = users;
    WFIFOSET (char_fd, 6 + 4 * users);

    return 0;
}

/*==========================================
 * timer�֐�
 * char�I�Ƃ̐ڑ����m�F���A�����؂�Ă�����ēx�ڑ�����
 *------------------------------------------
 */
int check_connect_char_server (int tid, unsigned int tick, int id, int data)
{
    if (char_fd <= 0 || session[char_fd] == NULL)
    {
        printf ("Attempt to connect to char-server...\n");
        chrif_state = 0;
        if ((char_fd = make_connection (char_ip, char_port)) < 0)
            return 0;
        session[char_fd]->func_parse = chrif_parse;
        realloc_fifo (char_fd, FIFOSIZE_SERVERLINK, FIFOSIZE_SERVERLINK);

        chrif_connect (char_fd);
    }

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int do_init_chrif (void)
{
    add_timer_func_list (check_connect_char_server,
                         "check_connect_char_server");
    add_timer_func_list (send_users_tochar, "send_users_tochar");
    add_timer_interval (gettick () + 1000, check_connect_char_server, 0, 0,
                        10 * 1000);
    add_timer_interval (gettick () + 1000, send_users_tochar, 0, 0, 5 * 1000);

    return 0;
}
