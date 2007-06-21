#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libedataserver/e-account-list.h>

#include "dns-txt-search.h"
#include "eee-calendar-config.h"
#include "eee-accounts-manager.h"
#include "eee-settings.h"
#include "utils.h"

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define EEE_KEY "/apps/evolution/calendar/eee/"

struct _EeeAccountsManagerPriv
{
  GConfClient* gconf;         /**< Gconf client. */
  EAccountList* ealist;       /**< EAccountList instance used internally to watch for changes. */
  ESourceList* eslist;        /**< Source list for calendar. */
  GSList* disabled_accounts;  /**< List of names of disabled accounts. */
  GSList* access_accounts;    /**< List of names of accessible accounts (user can connect to). */
  GSList* accounts;           /**< List of EeeAccount obejcts managed by this EeeAccountsManager. */
  
  // calendar list synchronization thread
  GThread* sync_thread;       /**< Synchronization thread. */
  gboolean sync_thread_running; /**< Flag that controls whether sync thread should run. */
  gboolean sync_force;        /**< Flag that forces "immediate" (within few secs) synchronization. */
  gboolean sync_abort;        /**< Flag that forces "abort" of current synchronization. */
  GSList* sync_accounts;      /**< List of account objects loaded by sync thrad. */

  GMutex* accounts_lock;
};

/* sync finish phase */
gboolean eee_accounts_manager_sync_phase2(EeeAccountsManager* self)
{
  GSList *iter, *iter2;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), FALSE);

  g_debug("EEE: sync phase 2");
  if (self->priv->sync_abort)
  {
    self->priv->sync_abort = FALSE;
    return FALSE;
  }

  // unmark groups/sources
  for (iter = e_source_list_peek_groups(self->priv->eslist); iter; iter = iter->next)
  {
    ESourceGroup* group = E_SOURCE_GROUP(iter->data);

    if (!e_source_group_is_3e(group))
      continue;

    g_debug("EEE: group %s", e_source_group_peek_name(group));
    g_object_set_data(G_OBJECT(group), "synced", (gpointer)FALSE);
    for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2->next)
    {
      ESource* source = E_SOURCE(iter2->data);
      g_object_set_data(G_OBJECT(source), "synced", (gpointer)FALSE);
      g_debug("EEE: source %s", e_source_peek_name(source));
    }
  }

  // go through synced account description structures and update ESourceList
  // accordingly
  for (iter = self->priv->sync_accounts; iter; iter = iter->next)
  {
    EeeAccount* account = iter->data;
    EeeAccount* current_account;
    ESourceGroup* group;
    char* group_name = g_strdup_printf("3E: %s", account->name);

    g_debug("EEE: account %s", account->name);

    // find ESourceGroup and EeeAccount
    group = e_source_list_peek_group_by_name(self->priv->eslist, group_name);
    current_account = eee_accounts_manager_find_account_by_name(self, account->name);

    if (account->disabled)
    {
      if (current_account)
        eee_accounts_manager_remove_account(self, current_account);
      if (group)
        e_source_list_remove_group(self->priv->eslist, group);
      eee_accounts_manager_disable_account(self, account->name);
      continue;
    }

    // create account if it does not exist
    if (current_account == NULL)
      eee_accounts_manager_add_account(self, g_object_ref(account));
    else
      eee_account_copy(current_account, account);

    // create group if it does not exist
    if (group == NULL)
    {
      group = e_source_group_new(group_name, EEE_URI_PREFIX);
      e_source_list_add_group(self->priv->eslist, group, -1);
      g_debug("EEE: creating group %s", group_name);
    }
    g_free(group_name);

    // check group sources
    for (iter2 = eee_account_peek_calendars(account); iter2 != NULL; iter2 = iter2->next)
    {
      ESCalendar* cal = iter2->data;
      ESource* source;

      if (!strcmp(cal->owner, account->name))
      {
        g_debug("EEE: owner's calendar %s:%s", cal->owner, cal->name);
        // calendar owned by owner of account that represents current group
        source = e_source_group_peek_source_by_calname(group, cal->name);
        if (source == NULL)
        {
          g_debug("EEE: source not found");
          source = e_source_new_3e(cal->name, cal->owner, account, cal->settings);
          e_source_group_add_source(group, source, -1);
        }
        else
        {
          g_debug("EEE: source found");
          e_source_set_3e_properties(source, cal->name, cal->owner, account, cal->settings);
        }
      }
      else
      {
        g_debug("EEE: shared calendar %s:%s", cal->owner, cal->name);
        char* owner_group_name = g_strdup_printf("3E: %s", cal->owner);
        // shared calendar, it should be put into another group
        ESourceGroup* owner_group = e_source_list_peek_group_by_name(self->priv->eslist, owner_group_name);
        if (owner_group == NULL)
        {
          g_debug("EEE: shared group not found");
          owner_group = e_source_group_new(owner_group_name, EEE_URI_PREFIX);
          e_source_list_add_group(self->priv->eslist, owner_group, -1);
        }
        g_object_set_data(G_OBJECT(owner_group), "synced", (gpointer)TRUE);

        source = e_source_group_peek_source_by_calname(owner_group, cal->name);
        if (source == NULL)
        {
          g_debug("EEE: shared source not found");
          source = e_source_new_3e(cal->name, cal->owner, account, cal->settings);
          e_source_group_add_source(owner_group, source, -1);
        }
        else
        {
          e_source_set_3e_properties(source, cal->name, cal->owner, account, cal->settings);
        }
      }
      g_object_set_data(G_OBJECT(source), "synced", (gpointer)TRUE);
    }

    g_object_set_data(G_OBJECT(group), "synced", (gpointer)TRUE);
  }

  // remove non-marked sources/groups
  for (iter = e_source_list_peek_groups(self->priv->eslist); iter; iter = iter->next)
  {
    ESourceGroup* group = E_SOURCE_GROUP(iter->data);
    if (!e_source_group_is_3e(group))
      continue;

    if (g_object_get_data(G_OBJECT(group), "synced"))
    {
      for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2->next)
      {
        ESource* source = E_SOURCE(iter2->data);
        if (!g_object_get_data(G_OBJECT(source), "synced"))
        {
          g_debug("EEE: removing source %s", e_source_peek_name(source));
          e_source_group_remove_source(group, source);
        }
      }
    }
    else
    {
      g_debug("EEE: removing group %s", e_source_group_peek_name(group));
      e_source_list_remove_group(self->priv->eslist, group);
    }
  }

  e_source_list_sync(self->priv->eslist, NULL);

  return TRUE;
}

gboolean sync_phase2_idle(gpointer data)
{
  eee_accounts_manager_sync_phase2(data);
  return FALSE;
}

/* synchronization phase1 (load data from the server) */
gboolean eee_accounts_manager_sync_phase1(EeeAccountsManager* self)
{
  GSList* accounts = NULL;
  GSList* iter;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), FALSE);

  // get copy of accounts to work on
  g_mutex_lock(self->priv->accounts_lock);
  for (iter = self->priv->access_accounts; iter; iter = iter->next)
  {
    char* name = iter->data;
    EeeAccount* account = eee_accounts_manager_find_account_by_name(self, name);
    if (account)
      account = eee_account_new_copy(account);
    else
      account = eee_account_new(name);
    accounts = g_slist_append(accounts, account);
  }
  g_mutex_unlock(self->priv->accounts_lock);

  // go through the list of EeeAccount objects and load calendar lists
  for (iter = accounts; iter; iter = iter->next)
  {
    EeeAccount* account = iter->data;

    if (!eee_account_find_server(account))
    {
      eee_account_disable(account);
      continue;
    }

    if (!eee_account_connect(account) || !eee_account_auth(account))
    {
      eee_account_disable(account);
      eee_account_disconnect(account);
      continue;
    }

    eee_account_load_calendars(account);
    eee_account_disconnect(account);
  }

  self->priv->sync_accounts = accounts;
  g_idle_add(sync_phase2_idle, self);
  return TRUE;
}

void eee_accounts_manager_abort_current_sync(EeeAccountsManager* self)
{
  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

  self->priv->sync_abort = TRUE;
}

/* add account to the list, manager takes reference of account object */
void eee_accounts_manager_add_account(EeeAccountsManager* self, EeeAccount* account)
{
  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
  g_return_if_fail(IS_EEE_ACCOUNT(account));

  g_mutex_lock(self->priv->accounts_lock);
  if (!eee_accounts_manager_find_account_by_name(self, account->name))
    self->priv->accounts = g_slist_append(self->priv->accounts, account);
  g_mutex_unlock(self->priv->accounts_lock);
}

/* remove account from the list */
void eee_accounts_manager_remove_account(EeeAccountsManager* self, EeeAccount* account)
{
  EeeAccount* tmp;

  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
  g_return_if_fail(IS_EEE_ACCOUNT(account));

  g_mutex_lock(self->priv->accounts_lock);
  tmp = eee_accounts_manager_find_account_by_name(self, account->name);
  if (tmp)
  {
    self->priv->accounts = g_slist_remove(self->priv->accounts, tmp);
    g_object_unref(tmp);
  }
  g_mutex_unlock(self->priv->accounts_lock);
}

void eee_accounts_manager_disable_account(EeeAccountsManager* self, const char* name)
{
  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
  g_return_if_fail(name != NULL);

  if (eee_accounts_manager_account_is_disabled(self, name))
    return;

  self->priv->disabled_accounts = g_slist_append(self->priv->disabled_accounts, g_strdup(name));
  eee_accounts_manager_load_access_accounts_list(self);
  gconf_client_set_list(self->priv->gconf, 
    EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, self->priv->disabled_accounts, NULL);
}

void eee_accounts_manager_enable_account(EeeAccountsManager* self, const char* name)
{
  GSList* item;

  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));
  g_return_if_fail(name != NULL);

  while (TRUE)
  {
    item = g_slist_find_custom(self->priv->disabled_accounts, name, (GCompareFunc)strcmp);
    if (item == NULL)
      break;
    self->priv->disabled_accounts = g_slist_remove_link(self->priv->disabled_accounts, item);
  }

  eee_accounts_manager_load_access_accounts_list(self);
  gconf_client_set_list(self->priv->gconf, 
    EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, self->priv->disabled_accounts, NULL);
}

gboolean eee_accounts_manager_account_is_disabled(EeeAccountsManager* self, const char* name)
{
  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), FALSE);
  g_return_val_if_fail(name != NULL, FALSE);

  return !!g_slist_find_custom(self->priv->disabled_accounts, name, (GCompareFunc)strcmp);
}

GSList* eee_accounts_manager_peek_accounts_list(EeeAccountsManager* self)
{
  return self->priv->accounts;
}

ESourceList* eee_accounts_manager_peek_source_list(EeeAccountsManager* self)
{
  return self->priv->eslist;
}

/* find EeeAccount object */
EeeAccount* eee_accounts_manager_find_account_by_name(EeeAccountsManager* self, const char* name)
{
  GSList* iter;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for (iter = self->priv->accounts; iter; iter = iter->next)
  {
    EeeAccount* account = iter->data;
    if (!strcmp(account->name, name))
      return account;
  }
  return NULL;
}

/* find ESourceGroup by name */
ESourceGroup* eee_accounts_manager_find_group_by_name(EeeAccountsManager *self, const char *name)
{
  char* _name = g_strdup_printf("3E: %s", name);
  ESourceGroup* group = e_source_list_peek_group_by_name(self->priv->eslist, _name);
  g_free(_name);
  return group;
}

/* find EeeAccount for ESourceGroup object */
EeeAccount* eee_accounts_manager_find_account_by_group(EeeAccountsManager* self, ESourceGroup* group)
{
  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
  g_return_val_if_fail(E_IS_SOURCE_GROUP(group), NULL);
  g_return_val_if_fail(e_source_group_is_3e(group), NULL);

  return eee_accounts_manager_find_account_by_name(self, e_source_group_peek_name(group)+4);
}

/* find EeeAccount for ESource object */
EeeAccount* eee_accounts_manager_find_account_by_source(EeeAccountsManager* self, ESource* source)
{
  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(self), NULL);
  g_return_val_if_fail(E_IS_SOURCE(source), NULL);
  g_return_val_if_fail(e_source_is_3e(source), NULL);

  return eee_accounts_manager_find_account_by_name(self, e_source_get_property(source, "eee-account"));
}

/* load list of enabled accessible EEE accounts */
void eee_accounts_manager_load_access_accounts_list(EeeAccountsManager* self)
{
  EIterator* iter;

  g_mutex_lock(self->priv->accounts_lock);
  g_slist_foreach(self->priv->access_accounts, (GFunc)g_free, NULL);
  g_slist_free(self->priv->access_accounts);
  self->priv->access_accounts = NULL;

  for (iter = e_list_get_iterator(E_LIST(self->priv->ealist));
       e_iterator_is_valid(iter);
       e_iterator_next(iter))
  {
    EAccount *account = E_ACCOUNT(e_iterator_get(iter));
    const char* name = e_account_get_string(account, E_ACCOUNT_ID_ADDRESS);
    if (eee_accounts_manager_account_is_disabled(self, name))
      continue;
    self->priv->access_accounts = g_slist_append(self->priv->access_accounts, g_strdup(name));
  }
  g_mutex_unlock(self->priv->accounts_lock);
}

/* callback called when EAccountList changes */
static void account_list_changed(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  eee_accounts_manager_load_access_accounts_list(mgr);
  // go through ESourceGroups and remove/add groups
}

void eee_accounts_manager_activate_accounts(EeeAccountsManager* self)
{
  GSList *iter, *iter2, *iter2_next, *iter_next;

  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(self));

  // for each accessible account
  for (iter = self->priv->access_accounts; iter; iter = iter->next)
  {
    EeeAccount* account;
    ESourceGroup* group;
    char* name = iter->data;
    char* group_name = g_strdup_printf("3E: %s", name);

    // find ESourceGroup and EeeAccount
    group = e_source_list_peek_group_by_name(self->priv->eslist, group_name);
    account = eee_accounts_manager_find_account_by_name(self, name);

    // create account if it does not exist
    if (account == NULL)
    {
      account = eee_account_new(name);
      eee_accounts_manager_add_account(self, account);
    }

    // create group if it does not exist
    if (group == NULL)
    {
      group = e_source_group_new(group_name, EEE_URI_PREFIX);
      e_source_list_add_group(self->priv->eslist, group, -1);
    }
    else
    {
      // check group sources
      for (iter2 = e_source_group_peek_sources(group); iter2 != NULL; iter2 = iter2_next)
      {
        // we may be removing sources so ensure that we have valid next pointer
        iter2_next = iter2->next;
        ESource* source = iter2->data;

        if (e_source_is_3e(source))
        {
          const char* calname = e_source_get_property(source, "eee-calname");
          e_source_set_3e_properties(source, calname, account->name, account, NULL);
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
    ESourceGroup* group = iter->data;
    gboolean contains_source = FALSE;

    // skip non-3E groups and accessible groups initialized above
    if (!e_source_group_is_3e(group) || g_object_get_data(G_OBJECT(group), "accessible"))
      continue;

    for (iter2 = e_source_group_peek_sources(group); iter2 != NULL; iter2 = iter2_next)
    {
      // we may be removing sources so ensure that we have valid next pointer
      GSList* iter2_next = iter2->next;
      ESource* source = iter2->data;

      // these ESources are probably for shared calendars, if we can't find
      // account for them, remove them
      if (eee_accounts_manager_find_account_by_source(self, source))
        contains_source = TRUE;
      else
        e_source_group_remove_source(group, source);
    }

    if (!contains_source)
      e_source_list_remove_group(self->priv->eslist, group);
  }
}

/* create new EeeAccountsManager */
EeeAccountsManager* eee_accounts_manager_new()
{
  EeeAccountsManager *self = g_object_new(EEE_TYPE_ACCOUNTS_MANAGER, NULL);

  eee_accounts_manager_activate_accounts(self);

  return self;
}

void eee_accounts_manager_force_sync(EeeAccountsManager* self)
{
  self->priv->sync_force = TRUE;
}

/* sync thread */
static gpointer sync_thread_func(gpointer data)
{
  EeeAccountsManager* mgr = data;
  int i;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr), NULL);

  while (mgr->priv->sync_thread_running)
  {
    eee_accounts_manager_sync_phase1(mgr);

    // wait for 60 seconds (or less if forced to)
    for (i=0; i<5 && !mgr->priv->sync_force; i++)
      g_usleep(1000000);
    mgr->priv->sync_force = FALSE;
  }

  return NULL;
}

/* GObject foo */

G_DEFINE_TYPE(EeeAccountsManager, eee_accounts_manager, G_TYPE_OBJECT);

static void eee_accounts_manager_init(EeeAccountsManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManagerPriv);

  self->priv->accounts_lock = g_mutex_new();
  self->priv->gconf = gconf_client_get_default();
  self->priv->ealist = e_account_list_new(self->priv->gconf);
  self->priv->eslist = e_source_list_new_for_gconf(self->priv->gconf, CALENDAR_SOURCES);
  self->priv->disabled_accounts = gconf_client_get_list(self->priv->gconf, 
    EEE_KEY "disabled_accounts", GCONF_VALUE_STRING, NULL);

  eee_accounts_manager_load_access_accounts_list(self);

  g_signal_connect(self->priv->ealist, "account_added", G_CALLBACK(account_list_changed), self);
  g_signal_connect(self->priv->ealist, "account_changed", G_CALLBACK(account_list_changed), self);
  g_signal_connect(self->priv->ealist, "account_removed", G_CALLBACK(account_list_changed), self);    

  self->priv->sync_thread_running = TRUE;
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
  g_slist_foreach(self->priv->disabled_accounts, (GFunc)g_free, NULL);
  g_slist_free(self->priv->disabled_accounts);
  g_object_unref(self->priv->gconf);
  g_object_unref(self->priv->eslist);
  g_object_unref(self->priv->ealist);
  g_mutex_free(self->priv->accounts_lock);

  G_OBJECT_CLASS(eee_accounts_manager_parent_class)->finalize(object);
}

static void eee_accounts_manager_class_init(EeeAccountsManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_accounts_manager_dispose;
  gobject_class->finalize = eee_accounts_manager_finalize;
  g_type_class_add_private(klass, sizeof(EeeAccountsManagerPriv));
}
