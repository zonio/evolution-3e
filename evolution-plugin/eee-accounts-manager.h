#ifndef EEE_ACCOUNTS_H
#define EEE_ACCOUNTS_H

typedef struct EeeAccountsManager EeeAccountsManager;
typedef struct EeeCalendar EeeCalendar;
typedef struct EeeAccount EeeAccount;
typedef struct EeeSettings EeeSettings;

struct EeeSettings
{
  char* title;
  int color;
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
