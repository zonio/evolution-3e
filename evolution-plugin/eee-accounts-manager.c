#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libedataserver/e-account-list.h>

#include "dns-txt-search.h"
#include "eee-calendar-config.h"
#include "eee-accounts-manager.h"

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"

struct _EeeAccountsManagerPriv
{
  GConfClient* gconf;         /**< Gconf client. */
  EAccountList* ealist;       /**< EAccountList instance used internally to watch for changes. */
  ESourceList* eslist;        /**< Source list for calendar. */
  GSList* accounts;           /**< List of EeeAccount obejcts managed by this EeeAccountsManager. */
  guint timer;
};

/**
 * ESourceGroup/ESource hierarchy synchronization code 
 */

/* get ESource by 3E calendar name */
static ESource* e_source_group_peek_source_by_cal_name(ESourceGroup *group, const char *name)
{
  GSList *p;
  for (p = e_source_group_peek_sources(group); p != NULL; p = p->next)
  {
    const char* cal_name = e_source_get_property(E_SOURCE(p->data), "eee-calendar-name");
    if (cal_name && !strcmp(cal_name, name))
      return E_SOURCE(p->data);
  }
  return NULL;
}

/* sync ESource list within ESourceGroup */
static void sync_source_list(EeeAccountsManager* mgr, ESourceGroup* group, EeeAccount* acc)
{
  GSList* iter;

  // for each source in the group
  GSList* source_list = g_slist_copy(e_source_group_peek_sources(group));
  for (iter = source_list; iter; iter = iter->next)
  {
    ESource* source = E_SOURCE(iter->data);
    const char* cal_name = e_source_get_property(source, "eee-calendar-name");
    EeeCalendar* cal = eee_account_peek_calendar_by_name(acc, cal_name);

    if (cal == NULL || cal->synced)
    {
      g_debug("** EEE ** Removing source: group=%s source=%s", e_source_group_peek_name(group), e_source_peek_name(source));
      e_source_group_remove_source(group, source);
    }
    else
    {
      const char* source_name = eee_settings_get_title(cal->settings);
      if (source_name == NULL)
        source_name = cal->name;

      g_debug("** EEE ** Updating source: group=%s source=%s", e_source_group_peek_name(group), source_name);

      e_source_set_name(source, source_name);
      e_source_set_relative_uri(source, cal->relative_uri);
      e_source_set_property(source, "auth", "1");
      e_source_set_property(source, "eee-calendar-name", cal->name);
      e_source_set_property(source, "eee-server", cal->access_account->server);
      e_source_set_property(source, "username", cal->access_account->email);
      e_source_set_property(source, "auth-key", cal->access_account->email);
      e_source_set_property(source, "auth-domain", EEE_PASSWORD_COMPONENT);
      if (eee_settings_get_color(cal->settings) > 0)
        e_source_set_color(source, eee_settings_get_color(cal->settings));
      cal->synced = 1;
    }
  }
  g_slist_free(source_list);

  for (iter = eee_account_peek_calendars(acc); iter; iter = iter->next)
  {
    EeeCalendar* cal = iter->data;
    ESource* source;

    if (e_source_group_peek_source_by_cal_name(group, cal->name))
      continue;

    const char* source_name = eee_settings_get_title(cal->settings);
    if (source_name == NULL)
      source_name = cal->name;

    g_debug("** EEE ** Adding source: group=%s source=%s", e_source_group_peek_name(group), source_name);

    source = e_source_new(source_name, cal->relative_uri);
    e_source_set_property(source, "auth", "1");
    e_source_set_property(source, "eee-calendar-name", cal->name);
    e_source_set_property(source, "eee-server", cal->access_account->server);
    e_source_set_property(source, "username", cal->access_account->email);
    e_source_set_property(source, "auth-key", cal->access_account->email);
    e_source_set_property(source, "auth-domain", EEE_PASSWORD_COMPONENT);
    if (eee_settings_get_color(cal->settings) > 0)
      e_source_set_color(source, eee_settings_get_color(cal->settings));
    e_source_group_add_source(group, source, -1);
  }
}

/* sync ESourceGroup objects with EeeAccountsManager accounts */
static void sync_group_list(EeeAccountsManager* mgr)
{
  GSList* iter;
  g_debug("** EEE ** Updating calendars source group list...");

  // synchronize existing ESource objects
  GSList* groups_list = g_slist_copy(e_source_list_peek_groups(mgr->priv->eslist));
  for (iter = groups_list; iter; iter = iter->next)
  {
    ESourceGroup* group = E_SOURCE_GROUP(iter->data);
    const char* group_name = e_source_group_peek_name(group);
    EeeAccount* acc = NULL;

    // skip non eee groups
    if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
      continue;
    
    if (group_name && g_str_has_prefix(group_name, "3E: "))
      acc = eee_accounts_manager_find_account_by_email(mgr, group_name+4);

    if (acc == NULL || acc->synced)
    {
      g_debug("** EEE ** Removing group: %s", group_name);
      e_source_list_remove_group(mgr->priv->eslist, group);
      continue;
    }

    g_debug("** EEE ** Updating group: %s", group_name);
    sync_source_list(mgr, group, acc);
    acc->synced = 1;
  }
  g_slist_free(groups_list);

  // add new accounts
  for (iter = mgr->priv->accounts; iter; iter = iter->next)
  {
    EeeAccount* acc = iter->data;
    ESourceGroup* group;

    char* group_name = g_strdup_printf("3E: %s", acc->email);
    if (e_source_list_peek_group_by_name(mgr->priv->eslist, group_name))
    {
      g_free(group_name);
      continue;
    }

    group = e_source_group_new(group_name, EEE_URI_PREFIX);
    g_debug("** EEE ** Adding group: %s", group_name);
    g_free(group_name);
    if (!e_source_list_add_group(mgr->priv->eslist, group, -1))
    {
      g_object_unref(group);
      continue;
    }

    sync_source_list(mgr, group, acc);
  }
  e_source_list_sync(mgr->priv->eslist, NULL);
}

/**
 * EeeAccountsManager accounts synchronization code 
 */

/* load calenars from the server for given account */
static gboolean sync_calendar_list_from_server(EeeAccountsManager* mgr, EeeAccount* access_account)
{
  xr_client_conn* conn;
  GError* err = NULL;
  GSList *cals, *iter;
  int rs;

  // get calendar list from the server
  conn = eee_account_connect(access_account);
  if (conn == NULL)
    return FALSE;

  cals = ESClient_getCalendars(conn, &err);
  if (err)
  {
    g_debug("** EEE ** Failed to get calendars for user '%s'. (%d:%s)", access_account->email, err->code, err->message);
    xr_client_free(conn);
    g_clear_error(&err);
    return FALSE;
  }
  xr_client_free(conn);

  // process retrieved calendars
  for (iter = cals; iter; iter = iter->next)
  {
    ESCalendar* cal = iter->data;
    g_debug("** EEE ** %s: Found calendar on the server (%s:%s:%s:%s)", access_account->email, cal->owner, cal->name, cal->perm, cal->settings);

    // create ecal
    EeeCalendar* ecal = eee_calendar_new();
    eee_calendar_set_name(ecal, cal->name);
    eee_calendar_set_perm(ecal, cal->perm);
    char* relative_uri = g_strdup_printf("%s/%s/%s", access_account->server, access_account->email, cal->name);
    eee_calendar_set_relative_uri(ecal, relative_uri);
    g_free(relative_uri);
    eee_settings_parse(ecal->settings, cal->settings);
    eee_calendar_set_access_account(ecal, access_account);

    // find existing owner EeeAccount or create new 
    EeeAccount* owner_account;
    owner_account = eee_accounts_manager_find_account_by_email(mgr, cal->owner);
    if (owner_account == NULL)
    {
      owner_account = eee_account_new();
      owner_account->email = g_strdup(cal->owner);
      owner_account->server = g_strdup(access_account->server);
      eee_accounts_manager_add_account(mgr, owner_account);
    }

    eee_calendar_set_owner_account(ecal, owner_account);
    eee_account_add_calendar(owner_account, ecal);
  }

  g_slist_foreach(cals, (GFunc)ESCalendar_free, NULL);
  g_slist_free(cals);
  return TRUE;
}

/** Synchrinize source lists in evolution from the 3e server.
 *
 * Adter this call everything should be in sync.
 *
 * @param mgr EeeAccountsManager object.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean eee_accounts_manager_sync(EeeAccountsManager* mgr)
{
  EIterator *iter;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr), FALSE);

  // free all accounts
  eee_accounts_manager_remove_accounts(mgr);

  // go through the list of EAccount objects and create EeeAccount objects
  for (iter = e_list_get_iterator(E_LIST(mgr->priv->ealist));
       e_iterator_is_valid(iter);
       e_iterator_next(iter))
  {
    EAccount *eaccount = E_ACCOUNT(e_iterator_get(iter));
    const char* email = e_account_get_string(eaccount, E_ACCOUNT_ID_ADDRESS);
    g_debug("** EEE ** EAccount found, searching for 3E server: email=%s uid=%s", email, eaccount->uid);
    char* server_hostname = get_eee_server_hostname(email);

    if (server_hostname)
    {
      g_debug("** EEE ** Found 3E server enabled account '%s'! (%s)", email, server_hostname);
      EeeAccount* account = eee_account_new();
      account->accessible = 1;
      account->email = g_strdup(email); 
      account->server = server_hostname;
      eee_accounts_manager_add_account(mgr, account);

      sync_calendar_list_from_server(mgr, account);
    }
  }

  sync_group_list(mgr);
  return TRUE;
}

/**
 * Accounts list management 
 */

/* remove all accounts */
void eee_accounts_manager_remove_accounts(EeeAccountsManager* mgr)
{
  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr));

  // free all accounts
  g_slist_foreach(mgr->priv->accounts, (GFunc)g_object_unref, NULL);
  g_slist_free(mgr->priv->accounts);
  mgr->priv->accounts = NULL;
}

/* get accounts list */
GSList* eee_accounts_manager_peek_accounts_list(EeeAccountsManager* mgr)
{
  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr), NULL);

  return mgr->priv->accounts;
}

/* add account to the list */
void eee_accounts_manager_add_account(EeeAccountsManager* mgr, EeeAccount* account)
{
  g_return_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr));
  g_return_if_fail(IS_EEE_ACCOUNT(account));

  mgr->priv->accounts = g_slist_append(mgr->priv->accounts, account);
}

/**
 * EeeAccountsManager search functions.
 */

/** Find EeeAccount object by email.
 *
 * @param mgr EeeAccountsManager object.
 * @param email E-mail of the account.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account_by_email(EeeAccountsManager* mgr, const char* email)
{
  GSList* iter;

  g_return_val_if_fail(IS_EEE_ACCOUNTS_MANAGER(mgr), NULL);
  g_return_val_if_fail(email != NULL, NULL);

  for (iter = mgr->priv->accounts; iter; iter = iter->next)
  {
    EeeAccount* a = iter->data;
    if (a->email && !strcmp(a->email, email))
      return a;
  }

  return NULL;
}

/** Find EeeAccount object by ESourceGroup.
 *
 * @param mgr EeeAccountsManager object.
 * @param group ESourceGroup object.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account_by_group(EeeAccountsManager* mgr, ESourceGroup* group)
{
  const char* name = e_source_group_peek_name(group);
  if (!g_str_has_prefix(name, "3E: "))
    return eee_accounts_manager_find_account_by_email(mgr, name);
  return eee_accounts_manager_find_account_by_email(mgr, name+4);
}

/** Find EeeCalendar object by ESource.
 *
 * @param mgr EeeAccountsManager object.
 * @param source ESource object.
 *
 * @return Matching EeeCalendar object or NULL.
 */
EeeCalendar* eee_accounts_manager_find_calendar_by_source(EeeAccountsManager* mgr, ESource* source)
{
  EeeAccount* account = eee_accounts_manager_find_account_by_group(mgr, e_source_peek_group(source));
  return eee_account_peek_calendar_by_name(account, e_source_get_property(source, "eee-calendar-name"));
}

/* callback listening for changes in account list */
static void e_account_list_changed(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  eee_accounts_manager_sync(mgr);
}

static gboolean update_timer_cb(gpointer data)
{
  EeeAccountsManager* mgr = EEE_ACCOUNTS_MANAGER(data);
  if (eee_plugin_online)
    eee_accounts_manager_sync(mgr);
  return TRUE;
}

/** Create new EeeAccountsManager.
 *
 * This function should be called only once per evolution instance.
 *
 * @return EeeAccountsManager object.
 */
EeeAccountsManager* eee_accounts_manager_new()
{
  EeeAccountsManager *mgr = g_object_new(EEE_TYPE_ACCOUNTS_MANAGER, NULL);
       
  mgr->priv->gconf = gconf_client_get_default();
  mgr->priv->ealist = e_account_list_new(mgr->priv->gconf);
  mgr->priv->eslist = e_source_list_new_for_gconf(mgr->priv->gconf, CALENDAR_SOURCES);

  g_debug("** EEE ** Starting EeeAccountsManager %p", mgr);
  eee_accounts_manager_sync(mgr);

  g_signal_connect(mgr->priv->ealist, "account_added", G_CALLBACK(e_account_list_changed), mgr);
  g_signal_connect(mgr->priv->ealist, "account_changed", G_CALLBACK(e_account_list_changed), mgr);
  g_signal_connect(mgr->priv->ealist, "account_removed", G_CALLBACK(e_account_list_changed), mgr);    

  mgr->priv->timer = g_timeout_add(20000, update_timer_cb, mgr);

  return mgr;
}

/* GObject foo */

G_DEFINE_TYPE(EeeAccountsManager, eee_accounts_manager, G_TYPE_OBJECT);

static void eee_accounts_manager_init(EeeAccountsManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_ACCOUNTS_MANAGER, EeeAccountsManagerPriv);
}

static void eee_accounts_manager_dispose(GObject *object)
{
  EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);
  eee_accounts_manager_remove_accounts(self);
  g_object_unref(self->priv->gconf);
  g_object_unref(self->priv->ealist);
  g_object_unref(self->priv->eslist);
  g_source_remove(self->priv->timer);
  G_OBJECT_CLASS(eee_accounts_manager_parent_class)->dispose(object);
}

static void eee_accounts_manager_finalize(GObject *object)
{
  EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);
  g_debug("** EEE ** Stoppping EeeAccountsManager %p", self);
  G_OBJECT_CLASS(eee_accounts_manager_parent_class)->finalize(object);
}

static void eee_accounts_manager_class_init(EeeAccountsManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_accounts_manager_dispose;
  gobject_class->finalize = eee_accounts_manager_finalize;
  g_type_class_add_private(klass, sizeof(EeeAccountsManagerPriv));
}
