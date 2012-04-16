/*
 * Author: Ondrej Jirman <ondrej.jirman@zonio.net>
 *
 * Copyright 2007-2008 Zonio, s.r.o.
 *
 * This file is part of evolution-3e.
 *
 * Libxr is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of the License, or (at your option) any
 * later version.
 *
 * Libxr is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with evolution-3e.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libedataserver/e-account-list.h>

#include "dns-txt-search.h"
#include "eee-calendar-config.h"
#include "eee-accounts-manager.h"
#include "utils.h"

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define EEE_KEY "/apps/evolution/calendar/eee/"

/* How this stuff works:
 *
 * The main purpose of EeeAccountsManager is to maintain list of EeeAccounts
 * and keep ESourceList of calendars in sync with it. EeeAccountsManager is
 * also responsible for determining state of EeeAccount (online/offline/disabled).
 *
 * Situations:
 *
 * 1) User opens evolution:
 *  - there may be preexisting ESources and ESourceGroups for 3e accounts
 *  - some accounts may be disabled and should be hidden from the list
 *  - there may be extra accounts in the list
 *  - evolution may be started in online or offline mode
 *
 * First ESourceList is processed and context menus initialized as if evolution
 * was in offline mode. Items that are related to disabled accounts are removed.
 * Items related to non-existing accounts are removed too.
 *
 * Then synchronization thread is started and it will run as soon as evolution
 * gets into online mode.
 *
 * 2) Evolution runs in online mode:
 *  - servers are periodically contacted and calendar lists are loaded
 *  - if server is unavailable, account is marked apropriately
 *  - accounts that failed to authentize are ignored (for the rest of session)
 *
 * Server communication process is slow and error prone. It is thus done in
 * separate thread.
 */

struct _EeeAccountsManagerPriv
{
    GConfClient *gconf;         /**< Gconf client. */
    EAccountList *ealist;       /**< EAccountList instance used internally to watch for changes. */
    ESourceList *eslist;        /**< Source list for calendar. */
    GSList *access_accounts;    /**< List of names of accessible accounts (user can connect to). */
    GSList *accounts;           /**< List of EeeAccount obejcts managed by this EeeAccountsManager. */

    // calendar list synchronization thread
    GThread *sync_thread;       /**< Synchronization thread. */
    volatile gint sync_request; /**< Synchronization request. */
    GSList *sync_accounts;      /**< List of account objects loaded by sync thrad. */
};

/* idle runner */

struct idle_runner
{
    GMutex *mutex;
    GCond *cond;
    gboolean done;
    gpointer data;
    GSourceFunc func;
};

static gboolean run_idle_cb(struct idle_runner *r)
{
    r->func(r->data);
    g_mutex_lock(r->mutex);
    r->done = TRUE;
    g_cond_signal(r->cond);
    g_mutex_unlock(r->mutex);
    return FALSE;
}

static void run_idle(GSourceFunc func, gpointer data)
{
    if (func == NULL)
    {
        return;
    }
    struct idle_runner *r = g_new0(struct idle_runner, 1);
    r->mutex = g_mutex_new();
    r->cond = g_cond_new();
    r->data = data;
    r->func = func;
    g_mutex_lock(r->mutex);
    g_idle_add((GSourceFunc)run_idle_cb, r);
    while (!r->done)
    {
        g_cond_wait(r->cond, r->mutex);
    }
    g_mutex_unlock(r->mutex);
    g_cond_free(r->cond);
    g_mutex_free(r->mutex);
    g_free(r);
}

/*
 * Synchronization loop:
 * - EeeAccountsManager is initialized
 * - eee_accounts_manager_activate_accounts() creates list of EeeAccount
 * - sync_starter() copies EeeAccount list for sync thread in the main loop
 *   (this is necessary because there is no easy way to protect EeeAccount from
 *   being accessed by main thread while it is manipulated by sync thread)
 * - sync thread runs eee_accounts_manager_sync_phase1() which will contact eee
 *   servers and load data
 * - sync thread runs sync_completer() in the main loop which will take
 *   sync_accounts list and merge it into ESourceList
 */

enum
{
    SYNC_REQ_PAUSE,
    SYNC_REQ_RUN,
    SYNC_REQ_RESTART,
    SYNC_REQ_START,
    SYNC_REQ_STOP
};

/* prepare sync_accounts list for sync pahse1 */
static gboolean sync_starter(gpointer data)
{
    EeeAccountsManager *mgr = data;
    GSList *accounts = NULL;
    GSList *iter;

    for (iter = mgr->priv->access_accounts; iter; iter = iter->next)
    {
        char *name = iter->data;
        EeeAccount *account = eee_accounts_manager_find_account_by_name(mgr, name);
        if (account)
        {
            account = eee_account_new_copy(account);
        }
        else
        {
            account = eee_account_new(name);
        }
        accounts = g_slist_append(accounts, account);
    }

    g_slist_foreach(mgr->priv->sync_accounts, (GFunc)g_object_unref, NULL);
    g_slist_free(mgr->priv->sync_accounts);

    mgr->priv->sync_accounts = accounts;

    return FALSE;
}

static void eee_accounts_manager_sync_phase1(EeeAccountsManager *self);
static gboolean eee_accounts_manager_sync_phase2(EeeAccountsManager *self);

/* run sync phase 2 */
static gboolean sync_completer(gpointer data)
{
    EeeAccountsManager *mgr = data;

    eee_accounts_manager_sync_phase2(mgr);

    return FALSE;
}

static gpointer sync_thread_func(gpointer data)
{
    EeeAccountsManager *mgr = data;
    int i;

    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr), NULL);

    while (TRUE)
    {
loop:
        switch (g_atomic_int_get(&mgr->priv->sync_request))
        {
        case SYNC_REQ_PAUSE:
            g_usleep(1000000);
            g_thread_yield();
            break;

        case SYNC_REQ_START:
            g_usleep(5000000);
            g_atomic_int_set(&mgr->priv->sync_request, SYNC_REQ_RESTART);
            break;

        case SYNC_REQ_RUN:
            for (i = 0; i < 30; i++)
            {
                g_usleep(1000000);
                if (g_atomic_int_get(&mgr->priv->sync_request) != SYNC_REQ_RUN)
                {
                    goto loop;
                }
            }

        case SYNC_REQ_RESTART:
            g_atomic_int_set(&mgr->priv->sync_request, SYNC_REQ_RUN);
            run_idle(sync_starter, mgr);
            if (g_atomic_int_get(&mgr->priv->sync_request) != SYNC_REQ_RUN)
            {
                break;
            }
            eee_accounts_manager_sync_phase1(mgr);
            if (g_atomic_int_get(&mgr->priv->sync_request) != SYNC_REQ_RUN)
            {
                break;
            }
            run_idle(sync_completer, mgr);
            break;

        case SYNC_REQ_STOP:
            return NULL;
        }
    }

    return NULL;
}

void eee_accounts_manager_restart_sync(EeeAccountsManager *self)
{
    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

    g_atomic_int_set(&self->priv->sync_request, SYNC_REQ_RESTART);
}

void eee_accounts_manager_pause_sync(EeeAccountsManager *self)
{
    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

    g_atomic_int_set(&self->priv->sync_request, SYNC_REQ_PAUSE);
}

/* synchronization phase1 (load data from the server) */
static void eee_accounts_manager_sync_phase1(EeeAccountsManager *self)
{
    GSList *iter;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

    // go through the list of EeeAccount objects and load calendar lists
    for (iter = self->priv->sync_accounts; iter; iter = iter->next)
    {
        EeeAccount *account = iter->data;

        /* Reasons for aborting sync phase1 are:
         * - evolution has gone offline
         * - run is no longer requested
         */
        if (g_atomic_int_get(&self->priv->sync_request) != SYNC_REQ_RUN)
        {
            return;
        }

        /* account is already disabled for this session */
        if (account->state == EEE_ACCOUNT_STATE_DISABLED)
        {
            continue;
        }

        /* account is int he disabled_accounts list */
        if (eee_accounts_manager_account_is_disabled(self, account->name))
        {
            eee_account_set_state(account, EEE_ACCOUNT_STATE_DISABLED);
            continue;
        }

        /* find server, if not account will still be checked for next time */
        if (!eee_account_find_server(account))
        {
            eee_account_set_state(account, EEE_ACCOUNT_STATE_NOTAVAIL);
            continue;
        }

        if (g_atomic_int_get(&self->priv->sync_request) != SYNC_REQ_RUN)
        {
            return;
        }

        /* connect to server, if not account will still be still checked for next time */
        if (!eee_account_connect(account))
        {
            eee_account_set_state(account, EEE_ACCOUNT_STATE_NOTAVAIL);
            continue;
        }

        /* if authenticate fails, account will be automatically disabled for this session */
        if (!eee_account_auth(account))
        {
            eee_account_set_state(account, EEE_ACCOUNT_STATE_DISABLED);
            eee_account_disconnect(account);
            continue;
        }

        eee_account_set_state(account, EEE_ACCOUNT_STATE_ONLINE);

        /* load cals and say good bye */
        eee_account_load_calendars(account, NULL);
        eee_account_disconnect(account);
    }
}

/* Find ESourceGroup in ESourceList
   e_source_list_peek_group_by_name is deprecated, so this function if for that */
static ESourceGroup *find_group_in_list(ESourceList *eslist, const gchar* group_name)
{
    GSList *iter;

    for (iter = e_source_list_peek_groups(eslist); iter; iter = iter->next)
    {
        ESourceGroup *group = E_SOURCE_GROUP(iter->data);

        if (!(g_strcmp0(e_source_group_peek_name(group), group_name)))
        {
            return group;
        }
    }

    return NULL;
}

/* sync finish phase */
static gboolean eee_accounts_manager_sync_phase2(EeeAccountsManager *self)
{
    GSList *iter, *iter2, *iter_next, *iter2_next;

    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), FALSE);

    // unmark groups/sources
    for (iter = e_source_list_peek_groups(self->priv->eslist); iter; iter = iter->next)
    {
        ESourceGroup *group = E_SOURCE_GROUP(iter->data);

        if (!e_source_group_is_3e(group))
        {
            continue;
        }

        g_object_set_data(G_OBJECT(group), "synced", (gpointer)FALSE);
        for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2->next)
        {
            ESource *source = E_SOURCE(iter2->data);
            g_object_set_data(G_OBJECT(source), "synced", (gpointer)FALSE);
        }
    }

    // go through synced account description structures and update ESourceList
    // accordingly
    for (iter = self->priv->sync_accounts; iter; iter = iter->next)
    {
        EeeAccount *account = iter->data;
        EeeAccount *current_account;
        ESourceGroup *group;
        char *group_name = g_strdup_printf("3e: %s", account->name);

        // find ESourceGroup and EeeAccount
        group = find_group_in_list(self->priv->eslist, group_name);
//        group = e_source_list_peek_group_by_properties(self->priv->eslist, "name", group_name, NULL);
        current_account = eee_accounts_manager_find_account_by_name(self, account->name);

        if (account->state == EEE_ACCOUNT_STATE_DISABLED)
        {
            if (current_account)
            {
                eee_accounts_manager_remove_account(self, current_account);
            }
            if (group)
            {
                e_source_list_remove_group(self->priv->eslist, group);
            }
            g_object_unref(account);
            continue;
        }

        // create account if it does not exist
        if (current_account == NULL)
        {
            eee_accounts_manager_add_account(self, g_object_ref(account));
        }
        else
        {
            eee_account_copy(current_account, account);
        }

        // create group if it does not exist
        if (group == NULL)
        {
            group = e_source_group_new(group_name, EEE_URI_PREFIX);
            e_source_list_add_group(self->priv->eslist, group, -1);
            g_object_unref(group);
        }
        g_free(group_name);

        // check group sources if account is available, otherwise just mark them as
        // synced
        if (account->state == EEE_ACCOUNT_STATE_NOTAVAIL)
        {
            GSList *iter_grp, *iter_src;
            for (iter_grp = e_source_list_peek_groups(self->priv->eslist); iter_grp; iter_grp = iter_grp->next)
            {
                ESourceGroup *group = iter_grp->data;
                for (iter_src = e_source_group_peek_sources(group); iter_src; iter_src = iter_src->next)
                {
                    ESource *source = iter_src->data;
                    const char *account_name = e_source_get_property(source, "eee-account");
                    if (account_name && !strcmp(account_name, account->name))
                    {
                        g_object_set_data(G_OBJECT(source), "synced", (gpointer)TRUE);
                        g_object_set_data(G_OBJECT(group), "synced", (gpointer)TRUE);
                    }
                }
            }
        }
        else
        {
            for (iter2 = eee_account_peek_calendars(account); iter2 != NULL; iter2 = iter2->next)
            {
                ESCalendarInfo *cal = iter2->data;
                ESource *source;

                if (!strcmp(cal->owner, account->name))
                {
                    // calendar owned by owner of account that represents current group
                    source = e_source_group_peek_source_by_calname(group, cal->name);
                    if (source == NULL)
                    {
                        source = e_source_new_3e_with_attrs(cal->name, cal->owner, account, cal->perm, cal->attrs);
                        e_source_group_add_source(group, source, -1);
                        g_object_unref(source);
                    }
                    else
                    {
                        e_source_set_3e_properties_with_attrs(source, cal->name, cal->owner, account, cal->perm, cal->attrs);
                    }
                }
                else
                {
                    char *owner_group_name = g_strdup_printf("3e: %s", cal->owner);
                    // shared calendar, it should be put into another group
                    ESourceGroup *owner_group = find_group_in_list(self->priv->eslist, owner_group_name);

                    if (owner_group == NULL)
                    {
                        owner_group = e_source_group_new(owner_group_name, EEE_URI_PREFIX);
                        e_source_list_add_group(self->priv->eslist, owner_group, -1);
                        g_object_unref(owner_group);
                    }
                    g_object_set_data(G_OBJECT(owner_group), "synced", (gpointer)TRUE);

                    source = e_source_group_peek_source_by_calname(owner_group, cal->name);
                    if (source == NULL)
                    {
                        source = e_source_new_3e_with_attrs(cal->name, cal->owner, account, cal->perm, cal->attrs);
                        e_source_group_add_source(owner_group, source, -1);
                        g_object_unref(source);
                    }
                    else
                    {
                        e_source_set_3e_properties_with_attrs(source, cal->name, cal->owner, account, cal->perm, cal->attrs);
                    }
                }
                g_object_set_data(G_OBJECT(source), "synced", (gpointer)TRUE);
            }
        }

        g_object_set_data(G_OBJECT(group), "synced", (gpointer)TRUE);
        g_object_unref(account);
    }

    g_slist_free(self->priv->sync_accounts);
    self->priv->sync_accounts = NULL;

    // remove non-marked sources/groups
    for (iter = e_source_list_peek_groups(self->priv->eslist); iter; iter = iter_next)
    {
        ESourceGroup *group = E_SOURCE_GROUP(iter->data);
        iter_next = iter->next;

        if (!e_source_group_is_3e(group))
        {
            continue;
        }

        if (g_object_get_data(G_OBJECT(group), "synced"))
        {
            for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2_next)
            {
                ESource *source = E_SOURCE(iter2->data);
                iter2_next = iter2->next;

                if (!g_object_get_data(G_OBJECT(source), "synced"))
                {
                    e_source_group_remove_source(group, source);
                }
            }
        }
        else
        {
            e_source_list_remove_group(self->priv->eslist, group);
        }
    }

    e_source_list_sync(self->priv->eslist, NULL);

    return TRUE;
}

/* takes ownership of source */
void eee_accounts_manager_add_source(EeeAccountsManager *self, const char *group_name, ESource *source)
{
    char *real_group_name;
    ESourceGroup *group;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
    g_return_if_fail(group_name != NULL);
    g_return_if_fail(source != NULL);
    g_return_if_fail(e_source_is_3e(source));

    real_group_name = g_strdup_printf("3e: %s", group_name);
    group = find_group_in_list(self->priv->eslist, real_group_name);

    if (group == NULL)
    {
        group = e_source_group_new(real_group_name, EEE_URI_PREFIX);
        e_source_list_add_group(self->priv->eslist, group, -1);
        g_object_unref(group);
    }
    g_free(real_group_name);

    e_source_group_add_source(group, source, -1);
    g_object_unref(source);

    e_source_list_sync(self->priv->eslist, NULL);
}

/* add account to the list, manager takes reference of account object */
void eee_accounts_manager_add_account(EeeAccountsManager *self, EeeAccount *account)
{
    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
    g_return_if_fail(IS_EEE_ACCOUNT(account));

    if (!eee_accounts_manager_find_account_by_name(self, account->name))
    {
        self->priv->accounts = g_slist_append(self->priv->accounts, account);
    }
}

/* remove account from the list */
void eee_accounts_manager_remove_account(EeeAccountsManager *self, EeeAccount *account)
{
    EeeAccount *tmp;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
    g_return_if_fail(IS_EEE_ACCOUNT(account));

    tmp = eee_accounts_manager_find_account_by_name(self, account->name);
    if (tmp)
    {
        self->priv->accounts = g_slist_remove(self->priv->accounts, tmp);
        g_object_unref(tmp);
    }
}

/* these method are useful to manipulate disabled accounts list, these are
 * account names that plugin should not try to access or show in any way, user
 * can disable/enable account only manually using evo. account preferences */
void eee_accounts_manager_disable_account(EeeAccountsManager *self, const char *name)
{
    GSList *accounts;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
    g_return_if_fail(name != NULL);

    if (eee_accounts_manager_account_is_disabled(self, name))
    {
        return;
    }

    accounts = gconf_client_get_list(self->priv->gconf, EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, NULL);

    accounts = g_slist_append(accounts, g_strdup(name));

    gconf_client_set_list(self->priv->gconf, EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, accounts, NULL);
    g_slist_foreach(accounts, (GFunc)g_free, NULL);
    g_slist_free(accounts);
}

void eee_accounts_manager_enable_account(EeeAccountsManager *self, const char *name)
{
    GSList *accounts;
    GSList *item;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
    g_return_if_fail(name != NULL);

    accounts = gconf_client_get_list(self->priv->gconf, EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, NULL);

    while (TRUE)
    {
        item = g_slist_find_custom(accounts, name, (GCompareFunc)strcmp);
        if (item == NULL)
        {
            break;
        }
        g_free(item->data);
        accounts = g_slist_remove_link(accounts, item);
    }

    gconf_client_set_list(self->priv->gconf, EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, accounts, NULL);
    g_slist_foreach(accounts, (GFunc)g_free, NULL);
    g_slist_free(accounts);
}

gboolean eee_accounts_manager_account_is_disabled(EeeAccountsManager *self, const char *name)
{
    GSList *accounts;
    gboolean disabled;

    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), FALSE);
    g_return_val_if_fail(name != NULL, FALSE);

    accounts = gconf_client_get_list(self->priv->gconf, EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, NULL);

    disabled = !!g_slist_find_custom(accounts, name, (GCompareFunc)strcmp);

    g_slist_foreach(accounts, (GFunc)g_free, NULL);
    g_slist_free(accounts);

    return disabled;
}

GSList *eee_accounts_manager_peek_accounts_list(EeeAccountsManager *self)
{
    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);

    return self->priv->accounts;
}

ESourceList *eee_accounts_manager_peek_source_list(EeeAccountsManager *self)
{
    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);

    return self->priv->eslist;
}

/* find EeeAccount object */
EeeAccount *eee_accounts_manager_find_account_by_name(EeeAccountsManager *self, const char *name)
{
    GSList *iter;

    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
    g_return_val_if_fail(name != NULL, NULL);

    for (iter = self->priv->accounts; iter; iter = iter->next)
    {
        EeeAccount *account = iter->data;
        if (!strcmp(account->name, name))
        {
            return account;
        }
    }
    return NULL;
}

/* find EeeAccount for ESourceGroup object */
EeeAccount *eee_accounts_manager_find_account_by_group(EeeAccountsManager *self, ESourceGroup *group)
{
    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
    g_return_val_if_fail(E_IS_SOURCE_GROUP(group), NULL);
    g_return_val_if_fail(e_source_group_is_3e(group), NULL);

    return eee_accounts_manager_find_account_by_name(self, e_source_group_peek_name(group) + 4);
}

/* find EeeAccount for ESource object */
EeeAccount *eee_accounts_manager_find_account_by_source(EeeAccountsManager *self, ESource *source)
{
    g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
    g_return_val_if_fail(E_IS_SOURCE(source), NULL);
    g_return_val_if_fail(e_source_is_3e(source), NULL);

    return eee_accounts_manager_find_account_by_name(self, e_source_get_property(source, "eee-account"));
}

/* load list of enabled accessible EEE accounts */
void eee_accounts_manager_load_access_accounts_list(EeeAccountsManager *self)
{
    EIterator *iter;

    g_slist_foreach(self->priv->access_accounts, (GFunc)g_free, NULL);
    g_slist_free(self->priv->access_accounts);
    self->priv->access_accounts = NULL;

    for (iter = e_list_get_iterator(E_LIST(self->priv->ealist));
         e_iterator_is_valid(iter);
         e_iterator_next(iter))
    {
        EAccount *account = E_ACCOUNT(e_iterator_get(iter));
        const char *name = e_account_get_string(account, E_ACCOUNT_ID_ADDRESS);
        if (!account->enabled)
        {
            continue;
        }
        if (eee_accounts_manager_account_is_disabled(self, name))
        {
            continue;
        }
        if (g_slist_find_custom(self->priv->access_accounts, (gpointer)name, (GCompareFunc)strcmp))
        {
            continue;
        }
        self->priv->access_accounts = g_slist_append(self->priv->access_accounts, g_strdup(name));
    }
}

/* callback called when EAccountList changes */
static void account_list_changed(EAccountList *account_list, EAccount *account, EeeAccountsManager *mgr)
{
    eee_accounts_manager_load_access_accounts_list(mgr);
    eee_accounts_manager_restart_sync(mgr);
}

void eee_accounts_manager_activate_accounts(EeeAccountsManager *self)
{
    GSList *iter, *iter2, *iter2_next, *iter_next;

    g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

    // for each accessible account
    for (iter = self->priv->access_accounts; iter; iter = iter->next)
    {
        EeeAccount *account;
        ESourceGroup *group;
        char *name = iter->data;
        char *group_name = g_strdup_printf("3e: %s", name);

        // find ESourceGroup and EeeAccount
        group = find_group_in_list(self->priv->eslist, group_name);
        account = eee_accounts_manager_find_account_by_name(self, name);

        // create account if it does not exist
        if (account == NULL)
        {
            account = eee_account_new(name);
            eee_account_set_state(account, EEE_ACCOUNT_STATE_NOTAVAIL);
            eee_accounts_manager_add_account(self, account);
        }

        // create group if it does not exist
        if (group == NULL)
        {
            group = e_source_group_new(group_name, EEE_URI_PREFIX);
            e_source_list_add_group(self->priv->eslist, group, -1);
            g_object_unref(group);
        }
        else
        {
            // check group sources
            for (iter2 = e_source_group_peek_sources(group); iter2 != NULL; iter2 = iter2_next)
            {
                // we may be removing sources so ensure that we have valid next pointer
                iter2_next = iter2->next;
                ESource *source = iter2->data;

                if (e_source_is_3e(source))
                {
                    const char *calname = e_source_get_property(source, "eee-calname");
                    e_source_set_3e_properties(source, calname, account->name, account, NULL, NULL, 0);
                }
                else
                {
                    // ESource without calname is useless, drop it
                    e_source_group_remove_source(group, source);
                }
            }
            g_free(group_name);
        }

        g_object_set_data(G_OBJECT(group), "accessible", (gpointer)TRUE);
    }

    // for each ESourceGroup that does not represent accessible account
    for (iter = e_source_list_peek_groups(self->priv->eslist); iter; iter = iter_next)
    {
        iter_next = iter->next;
        ESourceGroup *group = iter->data;
        gboolean contains_source = FALSE;

        // skip non-3E groups and accessible groups initialized above
        if (!e_source_group_is_3e(group) || g_object_get_data(G_OBJECT(group), "accessible"))
        {
            continue;
        }

        for (iter2 = e_source_group_peek_sources(group); iter2 != NULL; iter2 = iter2_next)
        {
            // we may be removing sources so ensure that we have valid next pointer
            iter2_next = iter2->next;
            ESource *source = iter2->data;

            // these ESources are probably for shared calendars, if we can't find
            // account for them, remove them
            if (eee_accounts_manager_find_account_by_source(self, source))
            {
                contains_source = TRUE;
            }
            else
            {
                e_source_group_remove_source(group, source);
            }
        }

        if (!contains_source)
        {
            e_source_list_remove_group(self->priv->eslist, group);
        }
    }

    e_source_list_sync(self->priv->eslist, NULL);
}

/* create new EeeAccountsManager */
EeeAccountsManager *eee_accounts_manager_new()
{
    EeeAccountsManager *self = g_object_new(EEE_TYPE_ACCOUNTS_MANAGER, NULL);

    return self;
}

/* GObject foo */

G_DEFINE_TYPE(EeeAccountsManager, eee_accounts_manager, G_TYPE_OBJECT);

static void eee_accounts_manager_init(EeeAccountsManager *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManagerPriv);

    self->priv->gconf = gconf_client_get_default();
    self->priv->ealist = e_account_list_new(self->priv->gconf);
    self->priv->eslist = e_source_list_new_for_gconf(self->priv->gconf, CALENDAR_SOURCES);

    eee_accounts_manager_load_access_accounts_list(self);
    eee_accounts_manager_activate_accounts(self);

    g_signal_connect(self->priv->ealist, "account_added", G_CALLBACK(account_list_changed), self);
    g_signal_connect(self->priv->ealist, "account_changed", G_CALLBACK(account_list_changed), self);
    g_signal_connect(self->priv->ealist, "account_removed", G_CALLBACK(account_list_changed), self);

    if (!eee_plugin_online)
    {
        self->priv->sync_request = SYNC_REQ_PAUSE;
    }
    else
    {
        self->priv->sync_request = SYNC_REQ_START;
    }

    self->priv->sync_thread = g_thread_create(sync_thread_func, self, FALSE, NULL);
}

static void eee_accounts_manager_dispose(GObject *object)
{
    EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);

    G_OBJECT_CLASS(eee_accounts_manager_parent_class)->dispose(object);
}

static void eee_accounts_manager_finalize(GObject *object)
{
    EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);

    g_slist_foreach(self->priv->accounts, (GFunc)g_object_unref, NULL);
    g_slist_free(self->priv->accounts);
    g_object_unref(self->priv->gconf);
    g_object_unref(self->priv->eslist);
    g_object_unref(self->priv->ealist);
    g_atomic_int_set(&self->priv->sync_request, SYNC_REQ_STOP);
    g_thread_join(self->priv->sync_thread);

    G_OBJECT_CLASS(eee_accounts_manager_parent_class)->finalize(object);
}

static void eee_accounts_manager_class_init(EeeAccountsManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = eee_accounts_manager_dispose;
    gobject_class->finalize = eee_accounts_manager_finalize;
    g_type_class_add_private(klass, sizeof(EeeAccountsManagerPriv));
}
