#ifndef EEE_ACCOUNTS_H
#define EEE_ACCOUNTS_H

#include <libedataserver/e-account-list.h>
#include <libedataserver/e-source-list.h>

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
  int color;                  /**< Calendar color. */
};

/** 3E calendar info structure.
 */
struct EeeCalendar
{
  char* uid;                  /**< Calendar's ESource UID. */
  char* name;                 /**< Calendar name. */
  char* perm;                 /**< Calendar permissions. (read, write, none) */
  EeeSettings* settings;      /**< Calendar settings. (title, color) */
  EeeAccount* account;        /**< 3E account that is used to access this calendar. */
  EeeAccount* owner_account;  /**< 3E account that this calendar is assigned to (owner's). */
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
  char* uid;                  /**< ESourceGroup UID. */
  char* email;                /**< Username of the account owner. Used to login to the 3E server.
                                   This string is used to match EeeAccount against EAccount. */
  char* server;               /**< 3E server hostname:port. */
  GSList* calendars;          /**< List of EeeCalendar objects owned by this account. */
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

/** Find EeeAccount object by ESourceGroup name.
 *
 * @param mgr EeeAccountsManager object.
 * @param email E-mail of the account.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account(EeeAccountsManager* mgr, const char* email);

/* EeeSettings parser. */
EeeSettings* eee_settings_new(const char* string);
char* eee_settings_get_string(EeeSettings* s);
void eee_settings_free(EeeSettings* s);

G_END_DECLS

#endif
