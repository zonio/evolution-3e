#ifndef EEE_ACCOUNTS_H
#define EEE_ACCOUNTS_H

#include <libedataserver/e-account-list.h>
#include <libedataserver/e-source-list.h>

#define EEE_URI_PREFIX "eee://" 

typedef struct EeeAccountsManager EeeAccountsManager;
typedef struct EeeCalendar EeeCalendar;
typedef struct EeeAccount EeeAccount;
typedef struct EeeSettings EeeSettings;

struct EeeSettings
{
  char* title;
  int color;
};

struct EeeCalendar
{
  char* name;
  char* perm;
  EeeAccount* login_account;
  ESource* source;
  EeeSettings* settings;
};

struct EeeAccount
{
  char* uid; // may be null for subscription "accounts"
  char* email;
  char* eee_server; // may be null for subscription "accounts"
  ESourceGroup* group;
  GSList* calendars;                     /**< EeeCalendar */
};

G_BEGIN_DECLS

EeeAccountsManager* eee_accounts_manager_new();
void eee_accounts_manager_free(EeeAccountsManager* mgr);
EeeAccount* eee_accounts_manager_find_account(EeeAccountsManager* mgr, const char* email);

EeeSettings* eee_settings_new(const char* string);
char* eee_settings_get_string(EeeSettings* s);
void eee_settings_free(EeeSettings* s);

G_END_DECLS

#endif
