// $Id: battle.h,v 1.6 2004/09/29 21:08:17 Akitasha Exp $
#ifndef _BATTLE_H_
#define _BATTLE_H_

// �_���[�W
struct Damage
{
    int  damage, damage2;
    int  type, div_;
    int  amotion, dmotion;
    int  blewcount;
    int  flag;
    int  dmg_lv;                //�͂܂ꌸ�Z�v�Z�p�@0:�X�L���U�� ATK_LUCKY,ATK_FLEE,ATK_DEF
};

// �����\�i�ǂݍ��݂�pc.c�Abattle_attr_fix�Ŏg�p�j
extern int attr_fix_table[4][10][10];

struct map_session_data;
struct mob_data;
struct block_list;

// �_���[�W�v�Z

struct Damage battle_calc_attack (int attack_type,
                                  struct block_list *bl,
                                  struct block_list *target, int skill_num,
                                  int skill_lv, int flag);
struct Damage battle_calc_weapon_attack (struct block_list *bl,
                                         struct block_list *target,
                                         int skill_num, int skill_lv,
                                         int flag);
struct Damage battle_calc_magic_attack (struct block_list *bl,
                                        struct block_list *target,
                                        int skill_num, int skill_lv,
                                        int flag);
struct Damage battle_calc_misc_attack (struct block_list *bl,
                                       struct block_list *target,
                                       int skill_num, int skill_lv, int flag);

// �����C���v�Z
int  battle_attr_fix (int damage, int atk_elem, int def_elem);

// �_���[�W�ŏI�v�Z
int  battle_calc_damage (struct block_list *src, struct block_list *bl,
                         int damage, int div_, int skill_num, int skill_lv,
                         int flag);
enum
{                               // �ŏI�v�Z�̃t���O
    BF_WEAPON = 0x0001,
    BF_MAGIC = 0x0002,
    BF_MISC = 0x0004,
    BF_SHORT = 0x0010,
    BF_LONG = 0x0040,
    BF_SKILL = 0x0100,
    BF_NORMAL = 0x0200,
    BF_WEAPONMASK = 0x000f,
    BF_RANGEMASK = 0x00f0,
    BF_SKILLMASK = 0x0f00,
};

// ���ۂ�HP�𑝌�
int  battle_delay_damage (unsigned int tick, struct block_list *src,
                          struct block_list *target, int damage, int flag);
int  battle_damage (struct block_list *bl, struct block_list *target,
                    int damage, int flag);
int  battle_heal (struct block_list *bl, struct block_list *target, int hp,
                  int sp, int flag);

// �U����ړ����~�߂�
int  battle_stopattack (struct block_list *bl);
int  battle_stopwalking (struct block_list *bl, int type);

// �ʏ�U�������܂Ƃ�
int  battle_weapon_attack (struct block_list *bl, struct block_list *target,
                           unsigned int tick, int flag);

// �e��p�����[�^�𓾂�
int  battle_counttargeted (struct block_list *bl, struct block_list *src,
                           int target_lv);
int  battle_is_unarmed (struct block_list *bl);
int  battle_get_class (struct block_list *bl);
int  battle_get_dir (struct block_list *bl);
int  battle_get_lv (struct block_list *bl);
int  battle_get_range (struct block_list *bl);
int  battle_get_hp (struct block_list *bl);
int  battle_get_max_hp (struct block_list *bl);
int  battle_get_str (struct block_list *bl);
int  battle_get_agi (struct block_list *bl);
int  battle_get_vit (struct block_list *bl);
int  battle_get_int (struct block_list *bl);
int  battle_get_dex (struct block_list *bl);
int  battle_get_luk (struct block_list *bl);
int  battle_get_hit (struct block_list *bl);
int  battle_get_flee (struct block_list *bl);
int  battle_get_def (struct block_list *bl);
int  battle_get_mdef (struct block_list *bl);
int  battle_get_flee2 (struct block_list *bl);
int  battle_get_def2 (struct block_list *bl);
int  battle_get_mdef2 (struct block_list *bl);
int  battle_get_baseatk (struct block_list *bl);
int  battle_get_atk (struct block_list *bl);
int  battle_get_atk2 (struct block_list *bl);
int  battle_get_speed (struct block_list *bl);
int  battle_get_adelay (struct block_list *bl);
int  battle_get_amotion (struct block_list *bl);
int  battle_get_dmotion (struct block_list *bl);
int  battle_get_element (struct block_list *bl);
int  battle_get_attack_element (struct block_list *bl);
int  battle_get_attack_element2 (struct block_list *bl);    //���蕐�푮���擾
#define battle_get_elem_type(bl)	(battle_get_element(bl)%10)
#define battle_get_elem_level(bl)	(battle_get_element(bl)/10/2)
int  battle_get_party_id (struct block_list *bl);
int  battle_get_guild_id (struct block_list *bl);
int  battle_get_race (struct block_list *bl);
int  battle_get_size (struct block_list *bl);
int  battle_get_mode (struct block_list *bl);
int  battle_get_mexp (struct block_list *bl);
int  battle_get_stat (int stat_id /* SP_VIT or similar */ ,
                      struct block_list *bl);

struct status_change *battle_get_sc_data (struct block_list *bl);
short *battle_get_sc_count (struct block_list *bl);
short *battle_get_opt1 (struct block_list *bl);
short *battle_get_opt2 (struct block_list *bl);
short *battle_get_opt3 (struct block_list *bl);
short *battle_get_option (struct block_list *bl);

enum
{
    BCT_NOENEMY = 0x00000,
    BCT_PARTY = 0x10000,
    BCT_ENEMY = 0x40000,
    BCT_NOPARTY = 0x50000,
    BCT_ALL = 0x20000,
    BCT_NOONE = 0x60000,
};

int  battle_check_undead (int race, int element);
int  battle_check_target (struct block_list *src, struct block_list *target,
                          int flag);
int  battle_check_range (struct block_list *src, struct block_list *bl,
                         int range);

// �ݒ�

int  battle_config_switch (const char *str);    // [Valaris]

extern struct Battle_Config
{
    int  warp_point_debug;
    int  enemy_critical;
    int  enemy_critical_rate;
    int  enemy_str;
    int  enemy_perfect_flee;
    int  cast_rate, delay_rate, delay_dependon_dex;
    int  sdelay_attack_enable;
    int  left_cardfix_to_right;
    int  pc_skill_add_range;
    int  skill_out_range_consume;
    int  mob_skill_add_range;
    int  pc_damage_delay;
    int  pc_damage_delay_rate;
    int  defnotenemy;
    int  random_monster_checklv;
    int  attr_recover;
    int  flooritem_lifetime;
    int  item_auto_get;
    int  item_first_get_time;
    int  item_second_get_time;
    int  item_third_get_time;
    int  mvp_item_first_get_time;
    int  mvp_item_second_get_time;
    int  mvp_item_third_get_time;
    int  item_rate, base_exp_rate, job_exp_rate;    // removed item rate, depreciated
    int  drop_rate0item;
    int  death_penalty_type;
    int  death_penalty_base, death_penalty_job;
    int  pvp_exp;               // [MouseJstr]
    int  gtb_pvp_only;          // [MouseJstr]
    int  zeny_penalty;
    int  restart_hp_rate;
    int  restart_sp_rate;
    int  mvp_item_rate, mvp_exp_rate;
    int  mvp_hp_rate;
    int  monster_hp_rate;
    int  monster_max_aspd;
    int  atc_gmonly;
    int  atc_spawn_quantity_limit;
    int  gm_allskill;
    int  gm_allskill_addabra;
    int  gm_allequip;
    int  gm_skilluncond;
    int  skillfree;
    int  skillup_limit;
    int  wp_rate;
    int  pp_rate;
    int  monster_active_enable;
    int  monster_damage_delay_rate;
    int  monster_loot_type;
    int  mob_skill_use;
    int  mob_count_rate;
    int  quest_skill_learn;
    int  quest_skill_reset;
    int  basic_skill_check;
    int  guild_emperium_check;
    int  guild_exp_limit;
    int  guild_max_castles;
    int  pc_invincible_time;
    int  skill_min_damage;
    int  finger_offensive_type;
    int  heal_exp;
    int  resurrection_exp;
    int  shop_exp;
    int  combo_delay_rate;
    int  item_check;
    int  wedding_modifydisplay;
    int  natural_healhp_interval;
    int  natural_healsp_interval;
    int  natural_heal_skill_interval;
    int  natural_heal_weight_rate;
    int  item_name_override_grffile;
    int  arrow_decrement;
    int  max_aspd;
    int  max_hp;
    int  max_sp;
    int  max_lv;
    int  max_parameter;
    int  max_cart_weight;
    int  pc_skill_log;
    int  mob_skill_log;
    int  battle_log;
    int  save_log;
    int  error_log;
    int  etc_log;
    int  save_clothcolor;
    int  undead_detect_type;
    int  pc_auto_counter_type;
    int  monster_auto_counter_type;
    int  agi_penaly_type;
    int  agi_penaly_count;
    int  agi_penaly_num;
    int  vit_penaly_type;
    int  vit_penaly_count;
    int  vit_penaly_num;
    int  player_defense_type;
    int  monster_defense_type;
    int  magic_defense_type;
    int  pc_skill_reiteration;
    int  monster_skill_reiteration;
    int  pc_skill_nofootset;
    int  monster_skill_nofootset;
    int  pc_cloak_check_type;
    int  monster_cloak_check_type;
    int  gvg_short_damage_rate;
    int  gvg_long_damage_rate;
    int  gvg_magic_damage_rate;
    int  gvg_misc_damage_rate;
    int  gvg_eliminate_time;
    int  mob_changetarget_byskill;
    int  pc_attack_direction_change;
    int  monster_attack_direction_change;
    int  pc_undead_nofreeze;
    int  pc_land_skill_limit;
    int  monster_land_skill_limit;
    int  party_skill_penaly;
    int  monster_class_change_full_recover;
    int  produce_item_name_input;
    int  produce_potion_name_input;
    int  making_arrow_name_input;
    int  holywater_name_input;
    int  display_delay_skill_fail;
    int  chat_warpportal;
    int  mob_warpportal;
    int  dead_branch_active;
    int  show_steal_in_same_party;
    int  enable_upper_class;
    int  mob_attack_attr_none;
    int  mob_ghostring_fix;
    int  pc_attack_attr_none;
    int  item_rate_common, item_rate_card, item_rate_equip, item_rate_heal, item_rate_use;  // Added by RoVeRT, Additional Heal and Usable item rate by Val
    int  item_drop_common_min, item_drop_common_max;    // Added by TyrNemesis^
    int  item_drop_card_min, item_drop_card_max;
    int  item_drop_equip_min, item_drop_equip_max;
    int  item_drop_mvp_min, item_drop_mvp_max;  // End Addition
    int  item_drop_heal_min, item_drop_heal_max;    // Added by Valatris
    int  item_drop_use_min, item_drop_use_max;  //End

    int  prevent_logout;        // Added by RoVeRT

    int  alchemist_summon_reward;   // [Valaris]
    int  maximum_level;
    int  drops_by_luk;
    int  monsters_ignore_gm;
    int  equipment_breaking;
    int  equipment_break_rate;
    int  multi_level_up;
    int  pk_mode;
    int  show_mob_hp;           // end additions [Valaris]

    int  agi_penaly_count_lv;
    int  vit_penaly_count_lv;

    int  gx_allhit;
    int  gx_cardfix;
    int  gx_dupele;
    int  gx_disptype;
    int  player_skill_partner_check;
    int  hide_GM_session;
    int  unit_movement_type;
    int  invite_request_check;
    int  skill_removetrap_type;
    int  disp_experience;
    int  castle_defense_rate;
    int  riding_weight;
    int  backstab_bow_penalty;

    int  night_at_start;        // added by [Yor]
    int  day_duration;          // added by [Yor]
    int  night_duration;        // added by [Yor]
    int  hack_info_GM_level;    // added by [Yor]
    int  any_warp_GM_min_level; // added by [Yor]
    int  packet_ver_flag;       // added by [Yor]
    int  muting_players;        // added by [Apple]

    int  min_hair_style;        // added by [MouseJstr]
    int  max_hair_style;        // added by [MouseJstr]
    int  min_hair_color;        // added by [MouseJstr]
    int  max_hair_color;        // added by [MouseJstr]
    int  min_cloth_color;       // added by [MouseJstr]
    int  max_cloth_color;       // added by [MouseJstr]

    int  castrate_dex_scale;    // added by [MouseJstr]
    int  area_size;             // added by [MouseJstr]

    int  chat_lame_penalty;
    int  chat_spam_threshold;
    int  chat_spam_flood;
    int  chat_spam_ban;
    int  chat_spam_warn;
    int  chat_maxline;

    int  trade_spam_threshold;
    int  trade_spam_flood;
    int  trade_spam_ban;
    int  trade_spam_warn;

    int  sit_spam_threshold;
    int  sit_spam_flood;
    int  sit_spam_ban;
    int  sit_spam_warn;

    int  packet_spam_threshold;
    int  packet_spam_flood;
    int  packet_spam_kick;

    int  drop_pickup_safety_zone;   // [Fate] Max. distance to an object dropped by a kill by self in which dropsteal protection works

    int  itemheal_regeneration_factor;  // [Fate] itemheal speed factor

} battle_config;

int  battle_config_read (const char *cfgName);

#endif
