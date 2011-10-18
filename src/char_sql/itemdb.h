#ifndef _ITEMDB_H_
#define _ITEMDB_H_

struct item_data
{
    int  nameid;
    char name[24], jname[24];
    int  value_buy, value_sell, value_notdc, value_notoc;
    int  type;
    int  class;
    int  sex;
    int  equip;
    int  weight;
    int  atk;
    int  def;
    int  range;
    int  slot;
    int  look;
    int  elv;
    int  wlv;
    char *use_script;           // �񕜂Ƃ����S�����̒��ł�낤���Ȃ�
    char *equip_script;         // �U��,�h��̑����ݒ�����̒��ŉ\����?
    char available;
};

struct item_data *itemdb_search (int nameid);
#define itemdb_type(n) itemdb_search(n)->type

int  itemdb_isequip (int);
int  itemdb_isequip2 (struct item_data *);

void do_final_itemdb (void);
int  do_init_itemdb (void);

#endif
