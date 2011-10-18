// $Id: int_storage.c,v 1.1.1.1 2004/09/10 17:26:51 MagicalTux Exp $

#include <string.h>
#include <stdlib.h>

#include "../common/mmo.h"
#include "../common/socket.h"
#include "../common/db.h"
#include "../common/lock.h"
#include "../common/malloc.h"
#include "char.h"
#include "inter.h"
#include "int_storage.h"
#include "int_guild.h"

// �t�@�C�����̃f�t�H���g
// inter_config_read()�ōĐݒ肳���
char storage_txt[1024] = "save/storage.txt";
char guild_storage_txt[1024] = "save/g_storage.txt";

static struct dbt *storage_db;
static struct dbt *guild_storage_db;

// �q�Ƀf�[�^�𕶎���ɕϊ�
int storage_tostr (char *str, struct storage *p)
{
    int  i, f = 0;
    char *str_p = str;
    str_p += sprintf (str_p, "%d,%d\t", p->account_id, p->storage_amount);

    for (i = 0; i < MAX_STORAGE; i++)
        if ((p->storage_[i].nameid) && (p->storage_[i].amount))
        {
            str_p += sprintf (str_p, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d ",
                              p->storage_[i].id, p->storage_[i].nameid,
                              p->storage_[i].amount, p->storage_[i].equip,
                              p->storage_[i].identify, p->storage_[i].refine,
                              p->storage_[i].attribute,
                              p->storage_[i].card[0], p->storage_[i].card[1],
                              p->storage_[i].card[2], p->storage_[i].card[3]);
            f++;
        }

    *(str_p++) = '\t';

    *str_p = '\0';
    if (!f)
        str[0] = 0;
    return 0;
}

// �������q�Ƀf�[�^�ɕϊ�
int storage_fromstr (char *str, struct storage *p)
{
    int  tmp_int[256];
    int  set, next, len, i;

    set = sscanf (str, "%d,%d%n", &tmp_int[0], &tmp_int[1], &next);
    p->storage_amount = tmp_int[1];

    if (set != 2)
        return 1;
    if (str[next] == '\n' || str[next] == '\r')
        return 0;
    next++;
    for (i = 0; str[next] && str[next] != '\t' && i < MAX_STORAGE; i++)
    {
        if (sscanf (str + next, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d%n",
                    &tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
                    &tmp_int[4], &tmp_int[5], &tmp_int[6],
                    &tmp_int[7], &tmp_int[8], &tmp_int[9], &tmp_int[10],
                    &tmp_int[10], &len) == 12)
        {
            p->storage_[i].id = tmp_int[0];
            p->storage_[i].nameid = tmp_int[1];
            p->storage_[i].amount = tmp_int[2];
            p->storage_[i].equip = tmp_int[3];
            p->storage_[i].identify = tmp_int[4];
            p->storage_[i].refine = tmp_int[5];
            p->storage_[i].attribute = tmp_int[6];
            p->storage_[i].card[0] = tmp_int[7];
            p->storage_[i].card[1] = tmp_int[8];
            p->storage_[i].card[2] = tmp_int[9];
            p->storage_[i].card[3] = tmp_int[10];
            next += len;
            if (str[next] == ' ')
                next++;
        }

        else if (sscanf (str + next, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d%n",
                         &tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
                         &tmp_int[4], &tmp_int[5], &tmp_int[6],
                         &tmp_int[7], &tmp_int[8], &tmp_int[9], &tmp_int[10],
                         &len) == 11)
        {
            p->storage_[i].id = tmp_int[0];
            p->storage_[i].nameid = tmp_int[1];
            p->storage_[i].amount = tmp_int[2];
            p->storage_[i].equip = tmp_int[3];
            p->storage_[i].identify = tmp_int[4];
            p->storage_[i].refine = tmp_int[5];
            p->storage_[i].attribute = tmp_int[6];
            p->storage_[i].card[0] = tmp_int[7];
            p->storage_[i].card[1] = tmp_int[8];
            p->storage_[i].card[2] = tmp_int[9];
            p->storage_[i].card[3] = tmp_int[10];
            next += len;
            if (str[next] == ' ')
                next++;
        }

        else
            return 1;
    }
    if (i >= MAX_STORAGE && str[next] && str[next] != '\t')
        printf
            ("storage_fromstr: Found a storage line with more items than MAX_STORAGE (%d), remaining items have been discarded!\n",
             MAX_STORAGE);
    return 0;
}

int guild_storage_tostr (char *str, struct guild_storage *p)
{
    int  i, f = 0;
    char *str_p = str;
    str_p += sprintf (str, "%d,%d\t", p->guild_id, p->storage_amount);

    for (i = 0; i < MAX_GUILD_STORAGE; i++)
        if ((p->storage_[i].nameid) && (p->storage_[i].amount))
        {
            str_p += sprintf (str_p, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d ",
                              p->storage_[i].id, p->storage_[i].nameid,
                              p->storage_[i].amount, p->storage_[i].equip,
                              p->storage_[i].identify, p->storage_[i].refine,
                              p->storage_[i].attribute,
                              p->storage_[i].card[0], p->storage_[i].card[1],
                              p->storage_[i].card[2], p->storage_[i].card[3]);
            f++;
        }

    *(str_p++) = '\t';

    *str_p = '\0';
    if (!f)
        str[0] = 0;
    return 0;
}

int guild_storage_fromstr (char *str, struct guild_storage *p)
{
    int  tmp_int[256];
    int  set, next, len, i;

    set = sscanf (str, "%d,%d%n", &tmp_int[0], &tmp_int[1], &next);
    p->storage_amount = tmp_int[1];

    if (set != 2)
        return 1;
    if (str[next] == '\n' || str[next] == '\r')
        return 0;
    next++;
    for (i = 0; str[next] && str[next] != '\t' && i < MAX_GUILD_STORAGE; i++)
    {
        if (sscanf (str + next, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d%n",
                    &tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
                    &tmp_int[4], &tmp_int[5], &tmp_int[6],
                    &tmp_int[7], &tmp_int[8], &tmp_int[9], &tmp_int[10],
                    &tmp_int[10], &len) == 12)
        {
            p->storage_[i].id = tmp_int[0];
            p->storage_[i].nameid = tmp_int[1];
            p->storage_[i].amount = tmp_int[2];
            p->storage_[i].equip = tmp_int[3];
            p->storage_[i].identify = tmp_int[4];
            p->storage_[i].refine = tmp_int[5];
            p->storage_[i].attribute = tmp_int[6];
            p->storage_[i].card[0] = tmp_int[7];
            p->storage_[i].card[1] = tmp_int[8];
            p->storage_[i].card[2] = tmp_int[9];
            p->storage_[i].card[3] = tmp_int[10];
            next += len;
            if (str[next] == ' ')
                next++;
        }

        else if (sscanf (str + next, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d%n",
                         &tmp_int[0], &tmp_int[1], &tmp_int[2], &tmp_int[3],
                         &tmp_int[4], &tmp_int[5], &tmp_int[6],
                         &tmp_int[7], &tmp_int[8], &tmp_int[9], &tmp_int[10],
                         &len) == 11)
        {
            p->storage_[i].id = tmp_int[0];
            p->storage_[i].nameid = tmp_int[1];
            p->storage_[i].amount = tmp_int[2];
            p->storage_[i].equip = tmp_int[3];
            p->storage_[i].identify = tmp_int[4];
            p->storage_[i].refine = tmp_int[5];
            p->storage_[i].attribute = tmp_int[6];
            p->storage_[i].card[0] = tmp_int[7];
            p->storage_[i].card[1] = tmp_int[8];
            p->storage_[i].card[2] = tmp_int[9];
            p->storage_[i].card[3] = tmp_int[10];
            next += len;
            if (str[next] == ' ')
                next++;
        }

        else
            return 1;
    }
    if (i >= MAX_GUILD_STORAGE && str[next] && str[next] != '\t')
        printf
            ("guild_storage_fromstr: Found a storage line with more items than MAX_GUILD_STORAGE (%d), remaining items have been discarded!\n",
             MAX_GUILD_STORAGE);
    return 0;
}

// �A�J�E���g����q�Ƀf�[�^�C���f�b�N�X�𓾂�i�V�K�q�ɒǉ��\�j
struct storage *account2storage (int account_id)
{
    struct storage *s;
    s = (struct storage *) numdb_search (storage_db, account_id);
    if (s == NULL)
    {
        s = (struct storage *) aCalloc (sizeof (struct storage), 1);
        if (s == NULL)
        {
            printf ("int_storage: out of memory!\n");
            exit (0);
        }
        memset (s, 0, sizeof (struct storage));
        s->account_id = account_id;
        numdb_insert (storage_db, s->account_id, s);
    }
    return s;
}

struct guild_storage *guild2storage (int guild_id)
{
    struct guild_storage *gs = NULL;
    if (inter_guild_search (guild_id) != NULL)
    {
        gs = (struct guild_storage *) numdb_search (guild_storage_db,
                                                    guild_id);
        if (gs == NULL)
        {
            gs = (struct guild_storage *)
                aCalloc (sizeof (struct guild_storage), 1);
            if (gs == NULL)
            {
                printf ("int_storage: out of memory!\n");
                exit (0);
            }
//          memset(gs,0,sizeof(struct guild_storage)); aCalloc does this! [Skotlex]
            gs->guild_id = guild_id;
            numdb_insert (guild_storage_db, gs->guild_id, gs);
        }
    }
    return gs;
}

//---------------------------------------------------------
// �q�Ƀf�[�^��ǂݍ���
int inter_storage_init ()
{
    char line[65536];
    int  c = 0, tmp_int;
    struct storage *s;
    struct guild_storage *gs;
    FILE *fp;

    storage_db = numdb_init ();

    fp = fopen_ (storage_txt, "r");
    if (fp == NULL)
    {
        printf ("cant't read : %s\n", storage_txt);
        return 1;
    }
    while (fgets (line, 65535, fp))
    {
        sscanf (line, "%d", &tmp_int);
        s = (struct storage *) aCalloc (sizeof (struct storage), 1);
        if (s == NULL)
        {
            printf ("int_storage: out of memory!\n");
            exit (0);
        }
//      memset(s,0,sizeof(struct storage)); aCalloc does this...
        s->account_id = tmp_int;
        if (s->account_id > 0 && storage_fromstr (line, s) == 0)
        {
            numdb_insert (storage_db, s->account_id, s);
        }
        else
        {
            printf ("int_storage: broken data [%s] line %d\n", storage_txt,
                    c);
            free (s);
        }
        c++;
    }
    fclose_ (fp);

    c = 0;
    guild_storage_db = numdb_init ();

    fp = fopen_ (guild_storage_txt, "r");
    if (fp == NULL)
    {
        printf ("cant't read : %s\n", guild_storage_txt);
        return 1;
    }
    while (fgets (line, 65535, fp))
    {
        sscanf (line, "%d", &tmp_int);
        gs = (struct guild_storage *) aCalloc (sizeof (struct guild_storage),
                                               1);
        if (gs == NULL)
        {
            printf ("int_storage: out of memory!\n");
            exit (0);
        }
//      memset(gs,0,sizeof(struct guild_storage)); aCalloc...
        gs->guild_id = tmp_int;
        if (gs->guild_id > 0 && guild_storage_fromstr (line, gs) == 0)
        {
            numdb_insert (guild_storage_db, gs->guild_id, gs);
        }
        else
        {
            printf ("int_storage: broken data [%s] line %d\n",
                    guild_storage_txt, c);
            free (gs);
        }
        c++;
    }
    fclose_ (fp);

    return 0;
}

int storage_db_final (void *k, void *data, va_list ap)
{
    struct storage *p = (struct storage *) data;
    if (p)
        free (p);
    return 0;
}

int guild_storage_db_final (void *k, void *data, va_list ap)
{
    struct guild_storage *p = (struct guild_storage *) data;
    if (p)
        free (p);
    return 0;
}

void inter_storage_final ()
{
    numdb_final (storage_db, storage_db_final);
    numdb_final (guild_storage_db, guild_storage_db_final);
    return;
}

int inter_storage_save_sub (void *key, void *data, va_list ap)
{
    char line[65536];
    FILE *fp;
    storage_tostr (line, (struct storage *) data);
    fp = va_arg (ap, FILE *);
    if (*line)
        fprintf (fp, "%s" RETCODE, line);
    return 0;
}

//---------------------------------------------------------
// �q�Ƀf�[�^����������
int inter_storage_save ()
{
    FILE *fp;
    int  lock;

    if (!storage_db)
        return 1;

    if ((fp = lock_fopen (storage_txt, &lock)) == NULL)
    {
        printf ("int_storage: cant write [%s] !!! data is lost !!!\n",
                storage_txt);
        return 1;
    }
    numdb_foreach (storage_db, inter_storage_save_sub, fp);
    lock_fclose (fp, storage_txt, &lock);
//  printf("int_storage: %s saved.\n",storage_txt);
    return 0;
}

int inter_guild_storage_save_sub (void *key, void *data, va_list ap)
{
    char line[65536];
    FILE *fp;

    if (inter_guild_search (((struct guild_storage *) data)->guild_id) !=
        NULL)
    {
        guild_storage_tostr (line, (struct guild_storage *) data);
        fp = va_arg (ap, FILE *);
        if (*line)
            fprintf (fp, "%s" RETCODE, line);
    }
    return 0;
}

//---------------------------------------------------------
// �q�Ƀf�[�^����������
int inter_guild_storage_save ()
{
    FILE *fp;
    int  lock;

    if (!guild_storage_db)
        return 1;

    if ((fp = lock_fopen (guild_storage_txt, &lock)) == NULL)
    {
        printf ("int_storage: cant write [%s] !!! data is lost !!!\n",
                guild_storage_txt);
        return 1;
    }
    numdb_foreach (guild_storage_db, inter_guild_storage_save_sub, fp);
    lock_fclose (fp, guild_storage_txt, &lock);
//  printf("int_storage: %s saved.\n",guild_storage_txt);
    return 0;
}

// �q�Ƀf�[�^�폜
int inter_storage_delete (int account_id)
{
    struct storage *s =
        (struct storage *) numdb_search (storage_db, account_id);
    if (s)
    {
        numdb_erase (storage_db, account_id);
        free (s);
    }
    return 0;
}

// �M���h�q�Ƀf�[�^�폜
int inter_guild_storage_delete (int guild_id)
{
    struct guild_storage *gs =
        (struct guild_storage *) numdb_search (guild_storage_db, guild_id);
    if (gs)
    {
        numdb_erase (guild_storage_db, guild_id);
        free (gs);
    }
    return 0;
}

//---------------------------------------------------------
// map server�ւ̒ʐM

// �q�Ƀf�[�^�̑��M
int mapif_load_storage (int fd, int account_id)
{
    struct storage *s = account2storage (account_id);
    WFIFOW (fd, 0) = 0x3810;
    WFIFOW (fd, 2) = sizeof (struct storage) + 8;
    WFIFOL (fd, 4) = account_id;
    memcpy (WFIFOP (fd, 8), s, sizeof (struct storage));
    WFIFOSET (fd, WFIFOW (fd, 2));
    return 0;
}

// �q�Ƀf�[�^�ۑ��������M
int mapif_save_storage_ack (int fd, int account_id)
{
    WFIFOW (fd, 0) = 0x3811;
    WFIFOL (fd, 2) = account_id;
    WFIFOB (fd, 6) = 0;
    WFIFOSET (fd, 7);
    return 0;
}

int mapif_load_guild_storage (int fd, int account_id, int guild_id)
{
    struct guild_storage *gs = guild2storage (guild_id);
    WFIFOW (fd, 0) = 0x3818;
    if (gs)
    {
        WFIFOW (fd, 2) = sizeof (struct guild_storage) + 12;
        WFIFOL (fd, 4) = account_id;
        WFIFOL (fd, 8) = guild_id;
        memcpy (WFIFOP (fd, 12), gs, sizeof (struct guild_storage));
    }
    else
    {
        WFIFOW (fd, 2) = 12;
        WFIFOL (fd, 4) = account_id;
        WFIFOL (fd, 8) = 0;
    }
    WFIFOSET (fd, WFIFOW (fd, 2));

    return 0;
}

int mapif_save_guild_storage_ack (int fd, int account_id, int guild_id,
                                  int fail)
{
    WFIFOW (fd, 0) = 0x3819;
    WFIFOL (fd, 2) = account_id;
    WFIFOL (fd, 6) = guild_id;
    WFIFOB (fd, 10) = fail;
    WFIFOSET (fd, 11);
    return 0;
}

//---------------------------------------------------------
// map server����̒ʐM

// �q�Ƀf�[�^�v����M
int mapif_parse_LoadStorage (int fd)
{
    mapif_load_storage (fd, RFIFOL (fd, 2));
    return 0;
}

// �q�Ƀf�[�^��M���ۑ�
int mapif_parse_SaveStorage (int fd)
{
    struct storage *s;
    int  account_id = RFIFOL (fd, 4);
    int  len = RFIFOW (fd, 2);
    if (sizeof (struct storage) != len - 8)
    {
        printf ("inter storage: data size error %d %d\n",
                sizeof (struct storage), len - 8);
    }
    else
    {
        s = account2storage (account_id);
        memcpy (s, RFIFOP (fd, 8), sizeof (struct storage));
        mapif_save_storage_ack (fd, account_id);
    }
    return 0;
}

int mapif_parse_LoadGuildStorage (int fd)
{
    mapif_load_guild_storage (fd, RFIFOL (fd, 2), RFIFOL (fd, 6));
    return 0;
}

int mapif_parse_SaveGuildStorage (int fd)
{
    struct guild_storage *gs;
    int  guild_id = RFIFOL (fd, 8);
    int  len = RFIFOW (fd, 2);
    if (sizeof (struct guild_storage) != len - 12)
    {
        printf ("inter storage: data size error %d %d\n",
                sizeof (struct guild_storage), len - 12);
    }
    else
    {
        gs = guild2storage (guild_id);
        if (gs)
        {
            memcpy (gs, RFIFOP (fd, 12), sizeof (struct guild_storage));
            mapif_save_guild_storage_ack (fd, RFIFOL (fd, 4), guild_id, 0);
        }
        else
            mapif_save_guild_storage_ack (fd, RFIFOL (fd, 4), guild_id, 1);
    }
    return 0;
}

// map server ����̒ʐM
// �E�P�p�P�b�g�̂݉�͂��邱��
// �E�p�P�b�g���f�[�^��inter.c�ɃZ�b�g���Ă�������
// �E�p�P�b�g���`�F�b�N��ARFIFOSKIP�͌Ăяo�����ōs����̂ōs���Ă͂Ȃ�Ȃ�
// �E�G���[�Ȃ�0(false)�A�����łȂ��Ȃ�1(true)���������Ȃ���΂Ȃ�Ȃ�
int inter_storage_parse_frommap (int fd)
{
    switch (RFIFOW (fd, 0))
    {
        case 0x3010:
            mapif_parse_LoadStorage (fd);
            break;
        case 0x3011:
            mapif_parse_SaveStorage (fd);
            break;
        case 0x3018:
            mapif_parse_LoadGuildStorage (fd);
            break;
        case 0x3019:
            mapif_parse_SaveGuildStorage (fd);
            break;
        default:
            return 0;
    }
    return 1;
}
