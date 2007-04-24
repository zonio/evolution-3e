#ifndef __EEE_ACCOUNTS_MANAGER_H__
#define __EEE_ACCOUNTS_MANAGER_H__

#include <libedataserver/e-source-list.h>
#include "eee-account.h"

/** 3E account manager.
 *
 * One instance per evolution process.
 *
 * Role of the EeeAccountsManager is to keep calendar ESourceList in sync with
 * EAccountList (each account must have it's own ESourceGroup) and ESource in
 * each group in sync with list of calendars on the 3e server.
 *
 * First we load list of EAccount objects (email accounts in evolution), then we
 * determine hostnames of the 3e servers for each email account (and if it has
 * one) and automatically load list of calendars from the 3e server.
 *
 * Then we load existing ESourceGroup objects with eee:// URI prefix (i.e. list
 * of eee accounts in the calendar view) and their associated ESources.
 *
 * Last step is to compare lists of ESource obejcts (eee accounts list in
 * calendar view) with list of calendars stored on the server and update list of
 * ESource obejcts to match list of calendars stored on the server.
 *
 * After this initial sync, our local list of EeeAccount and EeeCalendar obejcts
 * will be in sync with either list of calendar sources in gconf (what is shown
 * in calendar view) and list of existing email accounts (EAccount objects).
 *
 * Now we will setup notification mechanism for EAccount and calendar list
 * changes. We will also periodically fetch list of calendars from the 3e server
 * and update list of calendars in the calendars source list.
 *
 * GUI for adding/removing callendars will call methods of EeeAccountsManager
 * instead of directly playing with ESourceList content. This will assure
 * consistency of our local calendar list.
 */

#define EEE_URI_PREFIX "eee://" 

#define EEE_TYPE_ACCOUNTS_MANAGER            (eee_accounts_manager_get_type())
#define EEE_ACCOUNTS_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManager))
#define EEE_ACCOUNTS_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManagerClass))
#define IS_EEE_ACCOUNTS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), EEE_TYPE_ACCOUNTS_MANAGER))
#define IS_EEE_ACCOUNTS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  EEE_TYPE_ACCOUNTS_MANAGER))
#define EEE_ACCOUNTS_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManagerClass))

typedef struct _EeeAccountsManager EeeAccountsManager;
typedef struct _EeeAccountsManagerClass EeeAccountsManagerClass;
typedef struct _EeeAccountsManagerPriv EeeAccountsManagerPriv;

struct _EeeAccountsManager
{
  GObject parent;
  EeeAccountsManagerPriv* priv;
};

struct _EeeAccountsManagerClass
{
  GObjectClass parent_class;
};

G_BEGIN_DECLS

EeeAccountsManager* eee_accounts_manager_new();

gboolean eee_accounts_manager_sync(EeeAccountsManager* mgr);
void eee_accounts_manager_remove_accounts(EeeAccountsManager* mgr);
GSList* eee_accounts_manager_peek_accounts_list(EeeAccountsManager* mgr);
void eee_accounts_manager_add_account(EeeAccountsManager* mgr, EeeAccount* account);
EeeAccount* eee_accounts_manager_find_account_by_email(EeeAccountsManager* mgr, const char* email);
EeeAccount* eee_accounts_manager_find_account_by_group(EeeAccountsManager* mgr, ESourceGroup* group);
EeeCalendar* eee_accounts_manager_find_calendar_by_source(EeeAccountsManager* mgr, ESource* source);

GType eee_accounts_manager_get_type() G_GNUC_CONST;

G_END_DECLS

#endif /* __EEE_ACCOUNTS_MANAGER_H__ */
