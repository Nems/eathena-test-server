// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/db.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"

#include "storage.h"
#include "chrif.h"
#include "itemdb.h"
#include "clif.h"
#include "intif.h"
#include "pc.h"
#include "guild.h"
#include "battle.h"
#include "atcommand.h"

static struct dbt *storage_db;
static struct dbt *guild_storage_db;

/*==========================================
 * �q�ɓ��A�C�e���\�[�g
 *------------------------------------------
 */
int storage_comp_item (const void *_i1, const void *_i2)
{
    struct item *i1 = (struct item *) _i1;
    struct item *i2 = (struct item *) _i2;

    if (i1->nameid == i2->nameid)
        return 0;
    else if (!(i1->nameid) || !(i1->amount))
        return 1;
    else if (!(i2->nameid) || !(i2->amount))
        return -1;
    return i1->nameid - i2->nameid;
}

static int guild_storage_db_final (void *key, void *data, va_list ap)
{
    struct guild_storage *gstor = (struct guild_storage *) data;
    free (gstor);
    return 0;
}

static int storage_db_final (void *key, void *data, va_list ap)
{
    struct storage *stor = (struct storage *) data;
    free (stor);
    return 0;
}

void sortage_sortitem (struct storage *stor)
{
    nullpo_retv (stor);
    qsort (stor->storage_, MAX_STORAGE, sizeof (struct item),
           storage_comp_item);
}

void sortage_gsortitem (struct guild_storage *gstor)
{
    nullpo_retv (gstor);
    qsort (gstor->storage_, MAX_GUILD_STORAGE, sizeof (struct item),
           storage_comp_item);
}

/*==========================================
 * �������Ƃ�
 *------------------------------------------
 */
int do_init_storage (void)      // map.c::do_init()����Ă΂��
{
    storage_db = numdb_init ();
    guild_storage_db = numdb_init ();
    return 1;
}

void do_final_storage (void)    // by [MC Cameri]
{
    if (storage_db)
        numdb_final (storage_db, storage_db_final);
    if (guild_storage_db)
        numdb_final (guild_storage_db, guild_storage_db_final);
}

static int storage_reconnect_sub (void *key, void *data, va_list ap)
{                               //Parses storage and saves 'dirty' ones upon reconnect. [Skotlex]
    int  type = va_arg (ap, int);
    if (type)
    {                           //Guild Storage
        struct guild_storage *stor = (struct guild_storage *) data;
        if (stor->dirty && stor->storage_status == 0)   //Save closed storages.
            storage_guild_storagesave (0, stor->guild_id, 0);
    }
    else
    {                           //Account Storage
        struct storage *stor = (struct storage *) data;
        if (stor->dirty && stor->storage_status == 0)   //Save closed storages.
            storage_storage_save (stor->account_id, stor->dirty == 2 ? 1 : 0);
    }
    return 0;
}

//Function to be invoked upon server reconnection to char. To save all 'dirty' storages [Skotlex
void do_reconnect_storage (void)
{
    numdb_foreach (storage_db, storage_reconnect_sub, 0);
    numdb_foreach (guild_storage_db, storage_reconnect_sub, 1);
}

struct storage *account2storage (int account_id)
{
    struct storage *stor =
        (struct storage *) numdb_search (storage_db, account_id);
    if (stor == NULL)
    {
        stor = (struct storage *) aCallocA (sizeof (struct storage), 1);
        stor->account_id = account_id;
        numdb_insert (storage_db, stor->account_id, stor);
    }
    return stor;
}

// Just to ask storage, without creation
struct storage *account2storage2 (int account_id)
{
    return (struct storage *) numdb_search (storage_db, account_id);
}

int storage_delete (int account_id)
{
    struct storage *stor =
        (struct storage *) numdb_search (storage_db, account_id);
    if (stor)
    {
        numdb_erase (storage_db, account_id);
        free (stor);
    }
    return 0;
}

/*==========================================
 * �J�v���q�ɂ��J��
 *------------------------------------------
 */
int storage_storageopen (struct map_session_data *sd)
{
    struct storage *stor;
    nullpo_retr (0, sd);

    if (sd->state.storage_flag)
        return 1;               //Already open?

    if ((stor =
         (struct storage *) numdb_search (storage_db,
                                          sd->status.account_id)) == NULL)
    {                           //Request storage.
        intif_request_storage (sd->status.account_id);
        return 1;
    }

    if (stor->storage_status)
        return 1;               //Already open/player already has it open...

    stor->storage_status = 1;
    sd->state.storage_flag = 1;
    clif_storageitemlist (sd, stor);
    clif_storageequiplist (sd, stor);
    clif_updatestorageamount (sd, stor);
    return 0;
}

/*==========================================
 * Internal add-item function.
 *------------------------------------------
 */
static int storage_additem (struct map_session_data *sd, struct storage *stor,
                            struct item *item_data, int amount)
{
    struct item_data *data;
    int  i;

    if (item_data->nameid <= 0 || amount <= 0)
        return 1;

    data = itemdb_search (item_data->nameid);

    if (!itemdb_isequip2 (data))
    {                           //Stackable
        for (i = 0; i < MAX_STORAGE; i++)
        {
            if (compare_item (&stor->storage_[i], item_data))
            {
                if (amount > MAX_AMOUNT - stor->storage_[i].amount)
                    return 1;
                stor->storage_[i].amount += amount;
                clif_storageitemadded (sd, stor, i, amount);
                stor->dirty = 1;
                return 0;
            }
        }
    }
    //Add item
    for (i = 0; i < MAX_STORAGE && stor->storage_[i].nameid; i++);

    if (i >= MAX_STORAGE)
        return 1;

    memcpy (&stor->storage_[i], item_data, sizeof (stor->storage_[0]));
    stor->storage_[i].amount = amount;
    stor->storage_amount++;
    clif_storageitemadded (sd, stor, i, amount);
    clif_updatestorageamount (sd, stor);
    stor->dirty = 1;
    return 0;
}

/*==========================================
 * Internal del-item function
 *------------------------------------------
 */
static int storage_delitem (struct map_session_data *sd, struct storage *stor,
                            int n, int amount)
{

    if (stor->storage_[n].nameid == 0 || stor->storage_[n].amount < amount)
        return 1;

    stor->storage_[n].amount -= amount;
    if (stor->storage_[n].amount == 0)
    {
        memset (&stor->storage_[n], 0, sizeof (stor->storage_[0]));
        stor->storage_amount--;
        clif_updatestorageamount (sd, stor);
    }
    clif_storageitemremoved (sd, n, amount);

    stor->dirty = 1;
    return 0;
}

/*==========================================
 * Add an item to the storage from the inventory.
 *------------------------------------------
 */
int storage_storageadd (struct map_session_data *sd, int index, int amount)
{
    struct storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = account2storage2 (sd->status.account_id));

    if ((stor->storage_amount > MAX_STORAGE) || !stor->storage_status)
        return 0;               // storage full / storage closed

    if (index < 0 || index >= MAX_INVENTORY)
        return 0;

    if (sd->status.inventory[index].nameid <= 0)
        return 0;               //No item on that spot

    if (amount < 1 || amount > sd->status.inventory[index].amount)
        return 0;

//  log_tostorage(sd, index, 0);
    if (storage_additem (sd, stor, &sd->status.inventory[index], amount) == 0)
    {
        // remove item from inventory
        pc_unequipinvyitem (sd, index, 0);
        pc_delitem (sd, index, amount, 0);
    }

    return 1;
}

/*==========================================
 * Retrieve an item from the storage.
 *------------------------------------------
 */
int storage_storageget (struct map_session_data *sd, int index, int amount)
{
    struct storage *stor;
    int  flag;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = account2storage2 (sd->status.account_id));

    if (index < 0 || index >= MAX_STORAGE)
        return 0;

    if (stor->storage_[index].nameid <= 0)
        return 0;               //Nothing there

    if (amount < 1 || amount > stor->storage_[index].amount)
        return 0;

    if ((flag = pc_additem (sd, &stor->storage_[index], amount)) == 0)
        storage_delitem (sd, stor, index, amount);
    else
        clif_additem (sd, 0, 0, flag);
//  log_fromstorage(sd, index, 0);
    return 1;
}

/*==========================================
 * Move an item from cart to storage.
 *------------------------------------------
 */
int storage_storageaddfromcart (struct map_session_data *sd, int index,
                                int amount)
{
    struct storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = account2storage2 (sd->status.account_id));

    if (stor->storage_amount > MAX_STORAGE || !stor->storage_status)
        return 0;               // storage full / storage closed

    if (index < 0 || index >= MAX_CART)
        return 0;

    if (sd->status.cart[index].nameid <= 0)
        return 0;               //No item there.

    if (amount < 1 || amount > sd->status.cart[index].amount)
        return 0;

    if (storage_additem (sd, stor, &sd->status.cart[index], amount) == 0)
        pc_cart_delitem (sd, index, amount, 0);

    return 1;
}

/*==========================================
 * Get from Storage to the Cart
 *------------------------------------------
 */
int storage_storagegettocart (struct map_session_data *sd, int index,
                              int amount)
{
    struct storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = account2storage2 (sd->status.account_id));

    if (!stor->storage_status)
        return 0;

    if (index < 0 || index >= MAX_STORAGE)
        return 0;

    if (stor->storage_[index].nameid <= 0)
        return 0;               //Nothing there.

    if (amount < 1 || amount > stor->storage_[index].amount)
        return 0;

    if (pc_cart_additem (sd, &stor->storage_[index], amount) == 0)
        storage_delitem (sd, stor, index, amount);

    return 1;
}

/*==========================================
 * Modified By Valaris to save upon closing [massdriller]
 *------------------------------------------
 */
int storage_storageclose (struct map_session_data *sd)
{
    struct storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = account2storage2 (sd->status.account_id));

    clif_storageclose (sd);
    if (stor->storage_status)
    {
        if (save_settings & 4)
            chrif_save (sd);    //Invokes the storage saving as well.
        else
            storage_storage_save (sd->status.account_id, 0);
    }
    stor->storage_status = 0;
    sd->state.storage_flag = 0;

    if (sd->npc_flags.storage)
    {
        sd->npc_flags.storage = 0;
        map_scriptcont (sd, sd->npc_id);
    }

    return 0;
}

/*==========================================
 * When quitting the game.
 *------------------------------------------
 */
int storage_storage_quit (struct map_session_data *sd)
{
    struct storage *stor;

    nullpo_retr (0, sd);

    stor = account2storage2 (sd->status.account_id);
    if (stor)
    {
        chrif_save (sd);        //Invokes the storage saving as well.
        stor->storage_status = 0;
        sd->state.storage_flag = 0;
    }

    return 0;
}

void storage_storage_dirty (struct map_session_data *sd)
{
    struct storage *stor;

    stor = account2storage2 (sd->status.account_id);

    if (stor)
        stor->dirty = 1;
}

int storage_storage_save (int account_id, int final)
{
    struct storage *stor;

    stor = account2storage2 (account_id);
    if (!stor)
        return 0;

    if (stor->dirty)
    {
        if (final)
        {
            stor->dirty = 2;
            stor->storage_status = 0;   //To prevent further manipulation of it.
        }
        intif_send_storage (stor);
        return 1;
    }
    if (final)
    {                           //Clear storage from memory. Nothing to save.
        storage_delete (account_id);
        return 1;
    }

    return 0;
}

//Ack from Char-server indicating the storage was saved. [Skotlex]
int storage_storage_saved (int account_id)
{
    struct storage *stor;

    if ((stor = account2storage2 (account_id)) != NULL)
    {                           //Only mark it clean if it's not in use. [Skotlex]
        if (stor->dirty && stor->storage_status == 0)
        {
            stor->dirty = 0;
            sortage_sortitem (stor);
        }
        return 1;
    }
    return 0;
}

struct guild_storage *guild2storage (int guild_id)
{
    struct guild_storage *gs = NULL;
    if (guild_search (guild_id) != NULL)
    {
        gs = (struct guild_storage *) numdb_search (guild_storage_db,
                                                    guild_id);
        if (gs == NULL)
        {
            gs = (struct guild_storage *)
                aCallocA (sizeof (struct guild_storage), 1);
            if (gs == NULL)
            {
                printf ("storage: out of memory!\n");
                exit (0);
            }
            gs->guild_id = guild_id;
            numdb_insert (guild_storage_db, gs->guild_id, gs);
        }
    }
    return gs;
}

struct guild_storage *guild2storage2 (int guild_id)
{                               //For just locating a storage without creating one. [Skotlex]
    return (struct guild_storage *) numdb_search (guild_storage_db, guild_id);
}

int guild_storage_delete (int guild_id)
{
    struct guild_storage *gstor =
        (struct guild_storage *) numdb_search (guild_storage_db, guild_id);
    if (gstor)
    {
        numdb_erase (guild_storage_db, guild_id);
        free (gstor);
    }
    return 0;
}

int storage_guild_storageopen (struct map_session_data *sd)
{
    struct guild_storage *gstor;

    nullpo_retr (0, sd);

    if (sd->status.guild_id <= 0)
        return 2;

    if (sd->state.storage_flag)
        return 1;               //Can't open both storages at a time.

    if ((gstor = guild2storage2 (sd->status.guild_id)) == NULL)
    {
        intif_request_guild_storage (sd->status.account_id,
                                     sd->status.guild_id);
        return 0;
    }
    if (gstor->storage_status)
        return 1;

    gstor->storage_status = 1;
    sd->state.storage_flag = 2;
    clif_guildstorageitemlist (sd, gstor);
    clif_guildstorageequiplist (sd, gstor);
    clif_updateguildstorageamount (sd, gstor);
    return 0;
}

int guild_storage_additem (struct map_session_data *sd,
                           struct guild_storage *stor, struct item *item_data,
                           int amount)
{
    struct item_data *data;
    int  i;

    nullpo_retr (1, sd);
    nullpo_retr (1, stor);
    nullpo_retr (1, item_data);
    nullpo_retr (1, data = itemdb_search (item_data->nameid));

    if (item_data->nameid <= 0 || amount <= 0)
        return 1;

    if (!itemdb_isequip2 (data))
    {                           //Stackable
        for (i = 0; i < MAX_GUILD_STORAGE; i++)
        {
            if (compare_item (&stor->storage_[i], item_data))
            {
                if (stor->storage_[i].amount + amount > MAX_AMOUNT)
                    return 1;
                stor->storage_[i].amount += amount;
                clif_guildstorageitemadded (sd, stor, i, amount);
                stor->dirty = 1;
                return 0;
            }
        }
    }
    //Add item
    for (i = 0; i < MAX_GUILD_STORAGE && stor->storage_[i].nameid; i++);

    if (i >= MAX_GUILD_STORAGE)
        return 1;

    memcpy (&stor->storage_[i], item_data, sizeof (stor->storage_[0]));
    stor->storage_[i].amount = amount;
    stor->storage_amount++;
    clif_guildstorageitemadded (sd, stor, i, amount);
    clif_updateguildstorageamount (sd, stor);
    stor->dirty = 1;
    return 0;
}

int guild_storage_delitem (struct map_session_data *sd,
                           struct guild_storage *stor, int n, int amount)
{
    nullpo_retr (1, sd);
    nullpo_retr (1, stor);

    if (stor->storage_[n].nameid == 0 || stor->storage_[n].amount < amount)
        return 1;

    stor->storage_[n].amount -= amount;
    if (stor->storage_[n].amount == 0)
    {
        memset (&stor->storage_[n], 0, sizeof (stor->storage_[0]));
        stor->storage_amount--;
        clif_updateguildstorageamount (sd, stor);
    }
    clif_storageitemremoved (sd, n, amount);
    stor->dirty = 1;
    return 0;
}

int storage_guild_storageadd (struct map_session_data *sd, int index,
                              int amount)
{
    struct guild_storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    if (!stor->storage_status || stor->storage_amount > MAX_GUILD_STORAGE)
        return 0;

    if (index < 0 || index >= MAX_INVENTORY)
        return 0;

    if (sd->status.inventory[index].nameid <= 0)
        return 0;

    if (amount < 1 || amount > sd->status.inventory[index].amount)
        return 0;

//  log_tostorage(sd, index, 1);
    if (guild_storage_additem (sd, stor, &sd->status.inventory[index], amount)
        == 0)
        pc_delitem (sd, index, amount, 0);

    return 1;
}

int storage_guild_storageget (struct map_session_data *sd, int index,
                              int amount)
{
    struct guild_storage *stor;
    int  flag;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    if (!stor->storage_status)
        return 0;

    if (index < 0 || index >= MAX_GUILD_STORAGE)
        return 0;

    if (stor->storage_[index].nameid <= 0)
        return 0;

    if (amount < 1 || amount > stor->storage_[index].amount)
        return 0;

    if ((flag = pc_additem (sd, &stor->storage_[index], amount)) == 0)
        guild_storage_delitem (sd, stor, index, amount);
    else
        clif_additem (sd, 0, 0, flag);
//  log_fromstorage(sd, index, 1);

    return 0;
}

int storage_guild_storageaddfromcart (struct map_session_data *sd, int index,
                                      int amount)
{
    struct guild_storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    if (!stor->storage_status || stor->storage_amount > MAX_GUILD_STORAGE)
        return 0;

    if (index < 0 || index >= MAX_CART)
        return 0;

    if (sd->status.cart[index].nameid <= 0)
        return 0;

    if (amount < 1 || amount > sd->status.cart[index].amount)
        return 0;

    if (guild_storage_additem (sd, stor, &sd->status.cart[index], amount) ==
        0)
        pc_cart_delitem (sd, index, amount, 0);

    return 1;
}

int storage_guild_storagegettocart (struct map_session_data *sd, int index,
                                    int amount)
{
    struct guild_storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    if (!stor->storage_status)
        return 0;

    if (index < 0 || index >= MAX_GUILD_STORAGE)
        return 0;

    if (stor->storage_[index].nameid <= 0)
        return 0;

    if (amount < 1 || amount > stor->storage_[index].amount)
        return 0;

    if (pc_cart_additem (sd, &stor->storage_[index], amount) == 0)
        guild_storage_delitem (sd, stor, index, amount);

    return 1;
}

int storage_guild_storagesave (int account_id, int guild_id, int flag)
{
    struct guild_storage *stor = guild2storage2 (guild_id);

    if (stor)
    {
        if (flag)               //Char quitting, close it.
            stor->storage_status = 0;
        if (stor->dirty)
            intif_send_guild_storage (account_id, stor);
        return 1;
    }
    return 0;
}

int storage_guild_storagesaved (int guild_id)
{
    struct guild_storage *stor;

    if ((stor = guild2storage2 (guild_id)) != NULL)
    {
        if (stor->dirty && stor->storage_status == 0)
        {                       //Storage has been correctly saved.
            stor->dirty = 0;
            sortage_gsortitem (stor);
        }
        return 1;
    }
    return 0;
}

int storage_guild_storageclose (struct map_session_data *sd)
{
    struct guild_storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    clif_storageclose (sd);
    chrif_save (sd);            //This one also saves the storage. [Skotlex]

    stor->storage_status = 0;
    sd->state.storage_flag = 0;

    return 0;
}

int storage_guild_storage_quit (struct map_session_data *sd, int flag)
{
    struct guild_storage *stor;

    nullpo_retr (0, sd);
    nullpo_retr (0, stor = guild2storage2 (sd->status.guild_id));

    if (flag)
    {                           //Only during a guild break flag is 1 (don't save storage)
        sd->state.storage_flag = 0;
        stor->storage_status = 0;
        clif_storageclose (sd);
        if (save_settings & 4)
            chrif_save (sd);
        return 0;
    }

    if (stor->storage_status)
    {
        if (save_settings & 4)
            chrif_save (sd);
        else
            storage_guild_storagesave (sd->status.account_id,
                                       sd->status.guild_id, 1);
    }
    sd->state.storage_flag = 0;
    stor->storage_status = 0;

    return 0;
}
