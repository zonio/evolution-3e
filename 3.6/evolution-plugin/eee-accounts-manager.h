/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2012 Zonio s.r.o <developers@zonio.net>
 *
 * This file is part of evolution-3e.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __EEE_ACCOUNTS_MANAGER_H__
#define __EEE_ACCOUNTS_MANAGER_H__

#include <libedataserver/libedataserver.h>
#include "eee-account.h"

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
    EeeAccountsManagerPriv *priv;
};

struct _EeeAccountsManagerClass
{
    GObjectClass parent_class;
};

G_BEGIN_DECLS

GType eee_accounts_manager_get_type() G_GNUC_CONST;

EeeAccountsManager *eee_accounts_manager_new();
void                   eee_accounts_manager_add_source(EeeAccountsManager *self, const char *group_name, ESource *source);
void                   eee_accounts_manager_add_account(EeeAccountsManager *self, EeeAccount *account);
void                   eee_accounts_manager_remove_account(EeeAccountsManager *self, EeeAccount *account);
GSList *eee_accounts_manager_peek_accounts_list(EeeAccountsManager *self);
ESourceList *eee_accounts_manager_peek_source_list(EeeAccountsManager *self);
EeeAccount *eee_accounts_manager_find_account_by_name(EeeAccountsManager *self, const char *name);
EeeAccount *eee_accounts_manager_find_account_by_group(EeeAccountsManager *self, ESourceGroup *group);
EeeAccount *eee_accounts_manager_find_account_by_source(EeeAccountsManager *self, ESource *source);
void                   eee_accounts_manager_load_access_accounts_list(EeeAccountsManager *self);
gboolean               eee_accounts_manager_account_is_disabled(EeeAccountsManager *self, const char *name);
void                   eee_accounts_manager_disable_account(EeeAccountsManager *self, const char *name);
void                   eee_accounts_manager_enable_account(EeeAccountsManager *self, const char *name);
void                   eee_accounts_manager_activate_accounts(EeeAccountsManager *self);
void                   eee_accounts_manager_restart_sync(EeeAccountsManager *self);
void                   eee_accounts_manager_pause_sync(EeeAccountsManager *self);

G_END_DECLS

#endif /* __EEE_ACCOUNTS_MANAGER_H__ */
