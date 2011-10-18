// $Id: mmo.h,v 1.3 2004/09/25 20:12:25 PoW Exp $
// Original : mmo.h 2003/03/14 12:07:02 Rev.1.7

#ifndef	_MMO_H_
#define	_MMO_H_

#include <time.h>
#ifdef CYGWIN
#define RETCODE	"\r\n"          // (CR/LF?FWindows?n)
#else
#define RETCODE "\n"            // (LF?FUnix?n?j
#endif

#define FIFOSIZE_SERVERLINK	256*1024

// set to 0 to not check IP of player between each server.
// set to another value if you want to check (1)
#define CMP_AUTHFIFO_IP 1

#define CMP_AUTHFIFO_LOGIN2 1

#define MAX_MAP_PER_SERVER 512
#define MAX_INVENTORY 100
#define MAX_AMOUNT 30000
#define MAX_ZENY 1000000000     // 1G zeny
#define MAX_CART 100
#define MAX_SKILL 450
#define GLOBAL_REG_NUM 96
#define ACCOUNT_REG_NUM 16
#define ACCOUNT_REG2_NUM 16
#define DEFAULT_WALK_SPEED 150
#define MIN_WALK_SPEED 0
#define MAX_WALK_SPEED 1000
#define MAX_STORAGE 300
#define MAX_GUILD_STORAGE 1000
#define MAX_PARTY 12
#define MAX_GUILD 120            // increased max guild members to accomodate for +2 increase for extension levels [Valaris] (removed) [PoW]
#define MAX_GUILDPOSITION 20    // increased max guild positions to accomodate for all members [Valaris] (removed) [PoW]
#define MAX_GUILDEXPLUSION 32
#define MAX_GUILDALLIANCE 16
#define MAX_GUILDSKILL	8
#define MAX_GUILDCASTLE 24      // increased to include novice castles [Valaris]
#define MAX_GUILDLEVEL 50

#define MIN_HAIR_STYLE battle_config.min_hair_style
#define MAX_HAIR_STYLE battle_config.max_hair_style
#define MIN_HAIR_COLOR battle_config.min_hair_color
#define MAX_HAIR_COLOR battle_config.max_hair_color
#define MIN_CLOTH_COLOR battle_config.min_cloth_color
#define MAX_CLOTH_COLOR battle_config.max_cloth_color

// for produce
#define MIN_ATTRIBUTE 0
#define MAX_ATTRIBUTE 4
#define ATTRIBUTE_NORMAL 0
#define MIN_STAR 0
#define MAX_STAR 3

#define MIN_PORTAL_MEMO 0
#define MAX_PORTAL_MEMO 2

#define MAX_STATUS_TYPE 5

#define CHAR_CONF_NAME  "conf/char_athena.conf"

struct account
{
    int account_id;
    char name[50];
    char password[50];
    char lastlogin[50];
    char sex;
    int num_logins;
    int state;
    char email[50];
    char error_message[50];
    long valitidy_time;
    char last_ip[50];
    char memo[50];
    long ban_time;
};


struct item
{
    int  id;
    short nameid;
    short amount;
    unsigned short equip;
    char identify;
    char refine;
    char attribute;
    short card[4];
    short broken;
};

struct point
{
    char map[24];
    short x, y;
};

struct skill
{
    unsigned short id, lv, flags;
};

struct global_reg
{
    char str[32];
    int  value;
};

struct accreg
{
    int  account_id, reg_num;
    struct global_reg reg[ACCOUNT_REG_NUM];
};

struct mmo_charstatus
{
    int  char_id;
    int  account_id;
    int  partner_id;

    int  base_exp, job_exp, zeny;

    short classb;
    short status_point, skill_point;
    int  hp, max_hp, sp, max_sp;
    short option, karma, manner;
    short hair, hair_color, clothes_color;
    int  party_id, guild_id;

    short weapon, shield;
    short head_top, head_mid, head_bottom;

    char name[24];
    unsigned char base_level, job_level;
    short str, agi, vit, int_, dex, luk;
    unsigned char char_num, sex;

    unsigned long mapip;
    unsigned int mapport;

    struct point last_point, save_point, memo_point[10];
    struct item inventory[MAX_INVENTORY], cart[MAX_CART];
    struct skill skill[MAX_SKILL];
    int  global_reg_num;
    struct global_reg global_reg[GLOBAL_REG_NUM];
    int  account_reg_num;
    struct global_reg account_reg[ACCOUNT_REG_NUM];
    int  account_reg2_num;
    struct global_reg account_reg2[ACCOUNT_REG2_NUM];
};

struct storage
{
    int  dirty;
    int  account_id;
    short storage_status;
    short storage_amount;
    struct item storage_[MAX_STORAGE];
};

struct guild_storage
{
    int  dirty;
    int  guild_id;
    short storage_status;
    short storage_amount;
    struct item storage_[MAX_GUILD_STORAGE];
};

struct map_session_data;

struct gm_account
{
    int  account_id;
    int  level;
};

struct party_member
{
    int  account_id;
    char name[24], map[24];
    int  leader, online, lv;
    struct map_session_data *sd;
};

struct party
{
    int  party_id;
    char name[24];
    int  exp;
    int  item;
    struct party_member member[MAX_PARTY];
};

struct guild_member
{
    int  account_id, char_id;
    short hair, hair_color, gender, classb, lv;
    int  exp, exp_payper;
    short online, position;
    int  rsv1, rsv2;
    char name[24];
    struct map_session_data *sd;
};

struct guild_position
{
    char name[24];
    int  mode;
    int  exp_mode;
};

struct guild_alliance
{
    int  opposition;
    int  guild_id;
    char name[24];
};

struct guild_explusion
{
    char name[24];
    char mes[40];
    char acc[40];
    int  account_id;
    int  rsv1, rsv2, rsv3;
};

struct guild_skill
{
    int  id, lv;
};

struct guild
{
    int  guild_id;
    short guild_lv, connect_member, max_member, average_lv;
    int  exp, next_exp, skill_point, castle_id;
    char name[24], master[24];
    struct guild_member member[MAX_GUILD];
    struct guild_position position[MAX_GUILDPOSITION];
    char mes1[60], mes2[120];
    int  emblem_len, emblem_id;
    char emblem_data[2048];
    struct guild_alliance alliance[MAX_GUILDALLIANCE];
    struct guild_explusion explusion[MAX_GUILDEXPLUSION];
    struct guild_skill skill[MAX_GUILDSKILL];
};

struct guild_castle
{
    int  castle_id;
    char map_name[24];
    char castle_name[24];
    char castle_event[24];
    int  guild_id;
    int  economy;
    int  defense;
    int  triggerE;
    int  triggerD;
    int  nextTime;
    int  payTime;
    int  createTime;
    int  visibleC;
    int  visibleG0;
    int  visibleG1;
    int  visibleG2;
    int  visibleG3;
    int  visibleG4;
    int  visibleG5;
    int  visibleG6;
    int  visibleG7;
    int  Ghp0;                  // added Guardian HP [Valaris]
    int  Ghp1;
    int  Ghp2;
    int  Ghp3;
    int  Ghp4;
    int  Ghp5;
    int  Ghp6;
    int  Ghp7;
    int  GID0;
    int  GID1;
    int  GID2;
    int  GID3;
    int  GID4;
    int  GID5;
    int  GID6;
    int  GID7;                  // end addition [Valaris]
};
struct square
{
    int  val1[5];
    int  val2[5];
};

enum
{
    GBI_EXP = 1,                // ?M???h??EXP
    GBI_GUILDLV = 2,            // ?M???h??Lv
    GBI_SKILLPOINT = 3,         // ?M???h?~X?L???|?C???g
    GBI_SKILLLV = 4,            // ?M???h?X?L??Lv

    GMI_POSITION = 0,           // ?????o?[???E??X
    GMI_EXP = 1,                // ?????o?[??EXP

};

#ifndef LCCWIN32
#ifndef strcmpi
#define strcmpi strcasecmp
#endif
#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strncmpi
#define strncmpi strncasecmp
#endif
#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#endif

#endif // _MMO_H_

