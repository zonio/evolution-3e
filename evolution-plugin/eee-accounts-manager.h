#ifndef EEE_ACCOUNTS_H
#define EEE_ACCOUNTS_H

typedef struct EeeAccountsManager EeeAccountsManager;
typedef struct EeeCalendar EeeCalendar;
typedef struct EeeAccount EeeAccount;

G_BEGIN_DECLS

EeeAccountsManager* eee_accounts_manager_new();
void eee_accounts_manager_free(EeeAccountsManager* mgr);

G_END_DECLS

#endif
