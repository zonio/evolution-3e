#ifndef EEE_ACCOUNTS_H
#define EEE_ACCOUNTS_H

#include <libedataserver/e-account-list.h>
#include <libedataserver/e-source-list.h>
#include "interface/ESClient.xrc.h"

#define EEE_URI_PREFIX "eee://" 

typedef struct EeeAccountsManager EeeAccountsManager;
typedef struct EeeCalendar EeeCalendar;
typedef struct EeeAccount EeeAccount;
typedef struct EeeSettings EeeSettings;

/** Settings structure that can be stored on 3e server.
 */
struct EeeSettings
{
  char* title;                /**< Calendar title. */
  guint32 color;              /**< Calendar color. */
};

/** 3E calendar info structure.
 */
struct EeeCalendar
{
  char* name;                 /**< Calendar name. */
  char* perm;                 /**< Calendar permissions. (read, write, none) */
  char* relative_uri;         /**< Relative URI of the calendar. Feeded to the backend. */
  EeeSettings* settings;      /**< Calendar settings. (title, color) */
  EeeAccount* access_account; /**< 3E account that is used to access this calendar. */
  EeeAccount* owner_account;  /**< 3E account that this calendar is assigned to (owner's). */
  int synced;
};

/** 3E user's account.
 *
 * This structure is also used to represent accounts of the owners of subscribed
 * shared calendars. So strictly speaking, EeeAccount may not be related to
 * EAccount. UID is then set to NULL.
 *
 * Calendars grouped under this account may be accessd through different account.
 */
struct EeeAccount
{
  int accessible;             /**< Account is accessible (we can login). */
  char* email;                /**< Username of the account owner. Used to login to the 3E server.
                                   This string is used to match EeeAccount against EAccount. */
  char* password;             /**< This is password used to authenticate to an account. */
  char* server;               /**< 3E server hostname:port. */
  GSList* calendars;          /**< List of EeeCalendar objects owned by this account. */
  int synced;
};

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
struct EeeAccountsManager
{
  GConfClient* gconf;         /**< Gconf client. */
  EAccountList* ealist;       /**< EAccountList instance used internally to watch for changes. */
  ESourceList* eslist;        /**< Source list for calendar. */
  GSList* accounts;           /**< List of EeeAccount obejcts managed by this EeeAccountsManager. */
};

G_BEGIN_DECLS

/** Create new EeeAccountsManager.
 *
 * This function should be called only once per evolution instance.
 *
 * @return EeeAccountsManager object.
 */
EeeAccountsManager* eee_accounts_manager_new();

/** Release EeeAccountsManager.
 *
 * @param mgr EeeAccountsManager object.
 */
void eee_accounts_manager_free(EeeAccountsManager* mgr);

/** Release EeeCalendar.
 *
 * @param c EeeCalendar object.
 */
void eee_calendar_free(EeeCalendar* c);

/** Release EeeAccount.
 *
 * @param a EeeAccount object.
 */
void eee_account_free(EeeAccount* a);

/** Synchrinize source lists in evolution from the 3e server.
 *
 * Adter this call everything should be in sync.
 *
 * @param mgr EeeAccountsManager object.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean eee_accounts_manager_sync(EeeAccountsManager* mgr);

/** Find EeeAccount object by ESourceGroup name/EAccount email.
 *
 * @param mgr EeeAccountsManager object.
 * @param email E-mail of the account.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account_by_email(EeeAccountsManager* mgr, const char* email);

/** Find EeeCalendar object by name.
 *
 * @param acc EeeAccount object.
 * @param name Name.
 *
 * @return Matching EeeCalendar object or NULL.
 */
EeeCalendar* eee_accounts_manager_find_calendar_by_name(EeeAccount* acc, const char* name);

/** Find EeeCalendar object by ESource.
 *
 * @param mgr EeeAccountsManager object.
 * @param source ESource object.
 *
 * @return Matching EeeCalendar object or NULL.
 */
EeeCalendar* eee_accounts_manager_find_calendar_by_source(EeeAccountsManager* mgr, ESource* source);

/** Remove Calendar or Calendar subscription.
 *
 * @param mgr EeeAccountsManager object.
 * @param cal ESource object of the calendar.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean eee_accounts_manager_remove_calendar(EeeAccountsManager* mgr, ESource* source);

/** Find EeeAccount object by ESourceGroup.
 *
 * @param mgr EeeAccountsManager object.
 * @param group ESourceGroup object.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account_by_group(EeeAccountsManager* mgr, ESourceGroup* group);

/* server comm methods */
gboolean eee_server_store_calendar_settings(EeeCalendar* cal);

xr_client_conn* eee_server_connect_to_account(EeeAccount* acc);

/* EeeSettings parser. */
EeeSettings* eee_settings_new(const char* string);
char* eee_settings_get_string(EeeSettings* s);
void eee_settings_free(EeeSettings* s);

G_END_DECLS

#endif
