#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libedataserverui/e-passwords.h>
#include <e-util/e-error.h>
#include <libecal/e-cal.h>
#include "dns-txt-search.h"

#include "eee-accounts-manager.h"

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define SELECTED_CALENDARS "/apps/evolution/calendar/display/selected_calendars"
#define EEE_PASSWORD_COMPONENT "3E Account"

struct _EeeAccountsManagerPriv
{
};

/* 3e server access methods */

static char* get_eee_server_hostname(const char* email)
{
  char* domain = strchr(email, '@');
  char** txt_list;
  guint i;

  if (!domain) // invalid email address
  {
    g_debug("** EEE ** EAccount has invalid email: %s", email);
    return NULL;
  }

  txt_list = get_txt_records(++domain);
  if (txt_list == NULL)
  {
    g_debug("** EEE ** 3E server hostname can't be determined for '%s'. Your admin forgot to setup 3E TXT records in DNS?", email);
    return NULL;
  }

  for (i = 0; i < g_strv_length(txt_list); i++)
  {
    // parse TXT records if any
    if (g_str_has_prefix(txt_list[i], "eee server="))
    {
      char* server = g_strstrip(g_strdup(txt_list[i]+sizeof("eee server=")-1));
      //XXX: check format (hostname:port)
      return server;
    }
  }
  g_strfreev(txt_list);
  return NULL;
}

static gboolean authenticate_to_account(EeeAccount* acc, xr_client_conn* conn)
{
  GError* err = NULL;
  guint32 flags = E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET;
  gboolean remember = TRUE;
  char *fail_msg = ""; 
  char *password;
  int retry_limit = 3;
  gboolean rs;

  while (retry_limit--)
  {
    // get password
    password = e_passwords_get_password(EEE_PASSWORD_COMPONENT, acc->email);
    if (!password)
    {
      // no?, ok ask for it
      char* prompt = g_strdup_printf("%sEnter password for your 3E calendar account (%s).", fail_msg, acc->email);
      // key must have uri format or unpatched evolution segfaults in
      // ep_get_password_keyring()
      char* key = g_strdup_printf("eee://%s", acc->email);
      g_debug("e_passwords_ask_password(key=%s)", key);
      password = e_passwords_ask_password(prompt, EEE_PASSWORD_COMPONENT, key, prompt, flags, &remember, NULL);
      g_free(key);
      g_free(prompt);
      if (!password) 
        goto err;
    }

    // try to authenticate
    rs = ESClient_auth(conn, acc->email, password, &err);
    if (!err && rs == TRUE)
    {
      g_free(acc->password);
      acc->password = password;
      return TRUE;
    }
    g_free(password);

    // process error
    if (err)
    {
      g_debug("** EEE ** Authentization failed for user '%s'. (%d:%s)", acc->email, err->code, err->message);
      if (err->code == 1)
        fail_msg = "User not found. ";
      else if (err->code == 6)
        fail_msg = "Invalid password. ";
      g_clear_error(&err);
    }
    else
    {
      g_debug("** EEE ** Authentization failed for user '%s'.", acc->email);
      fail_msg = "";
    }

    // forget password and retry
    e_passwords_forget_password(EEE_PASSWORD_COMPONENT, acc->email);
    flags |= E_PASSWORDS_REPROMPT;
  }

  e_error_run(NULL, "mail:eee-auth-error", acc->email, NULL);
 err:
  g_free(acc->password);
  acc->password = NULL;
  return FALSE;
}

xr_client_conn* eee_server_connect_to_account(EeeAccount* acc)
{
  xr_client_conn* conn;
  GError* err = NULL;
  char* server_uri;

  g_debug("** EEE ** Connecting to 3E server: server=%s user=%s", acc->server, acc->email);
  conn = xr_client_new(&err);
  if (err)
  {
    g_debug("** EEE ** Can't create client interface. (%d:%s)", err->code, err->message);
    goto err0;
  }
  server_uri = g_strdup_printf("https://%s/ESClient", acc->server);
  xr_client_open(conn, server_uri, &err);
  g_free(server_uri);
  if (err)
  {
    g_debug("** EEE ** Can't open connection to the server. (%d:%s)", err->code, err->message);
    goto err1;
  }
  if (!authenticate_to_account(acc, conn))
    goto err1;

  return conn;

 err1:
  xr_client_free(conn);
 err0:
  g_clear_error(&err);
  return NULL;
}

static gboolean sync_calendar_list_from_server(EeeAccountsManager* mgr, EeeAccount* access_account)
{
  xr_client_conn* conn;
  GError* err = NULL;
  GSList *cals, *iter;
  int rs;

  conn = eee_server_connect_to_account(access_account);
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
    EeeCalendar* ecal;
    EeeAccount* owner_account;
    g_debug("** EEE ** %s: Found calendar on the server (%s:%s:%s:%s)", access_account->email, cal->owner, cal->name, cal->perm, cal->settings);

    ecal = g_new0(EeeCalendar, 1);
    ecal->name = g_strdup(cal->name);
    ecal->perm = g_strdup(cal->perm);
    ecal->relative_uri = g_strdup_printf("%s/%s/%s", access_account->server, access_account->email, cal->name);
    ecal->settings = eee_settings_new(cal->settings);
    ecal->access_account = access_account;

    // find existing owner EeeAccount or create new 
    owner_account = eee_accounts_manager_find_account_by_email(mgr, cal->owner);
    if (owner_account == NULL)
    {
      owner_account = g_new0(EeeAccount, 1);
      owner_account->email = g_strdup(cal->owner);
      owner_account->server = g_strdup(access_account->server);
      mgr->accounts = g_slist_append(mgr->accounts, owner_account);
    }

    ecal->owner_account = owner_account;
    owner_account->calendars = g_slist_append(owner_account->calendars, ecal);
  }
  g_slist_foreach(cals, (GFunc)ESCalendar_free, NULL);
  g_slist_free(cals);
  return 0;
}

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

static void sync_source_list(EeeAccountsManager* mgr, ESourceGroup* group, EeeAccount* acc)
{
  GSList* iter;

  // for each source in the group
  GSList* source_list = g_slist_copy(e_source_group_peek_sources(group));
  for (iter = source_list; iter; iter = iter->next)
  {
    ESource* source = E_SOURCE(iter->data);
    const char* cal_name = e_source_get_property(source, "eee-calendar-name");
    EeeCalendar* cal;

    cal = eee_accounts_manager_find_calendar_by_name(acc, cal_name);
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

  for (iter = acc->calendars; iter; iter = iter->next)
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

static void sync_group_list(EeeAccountsManager* mgr)
{
  GSList* iter;
  g_debug("** EEE ** Updating calendars source group list...");

  // synchronize existing ESource objects
  GSList* groups_list = g_slist_copy(e_source_list_peek_groups(mgr->eslist));
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
      e_source_list_remove_group(mgr->eslist, group);
      continue;
    }

    g_debug("** EEE ** Updating group: %s", group_name);
    sync_source_list(mgr, group, acc);
    acc->synced = 1;
  }
  g_slist_free(groups_list);

  // add new accounts
  for (iter = mgr->accounts; iter; iter = iter->next)
  {
    EeeAccount* acc = iter->data;
    ESourceGroup* group;

    char* group_name = g_strdup_printf("3E: %s", acc->email);
    if (e_source_list_peek_group_by_name(mgr->eslist, group_name))
    {
      g_free(group_name);
      continue;
    }

    group = e_source_group_new(group_name, EEE_URI_PREFIX);
    g_debug("** EEE ** Adding group: %s", group_name);
    g_free(group_name);
    if (!e_source_list_add_group(mgr->eslist, group, -1))
    {
      g_object_unref(group);
      continue;
    }

    sync_source_list(mgr, group, acc);
  }
  e_source_list_sync(mgr->eslist, NULL);
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

  // free all accounts
  g_slist_foreach(mgr->accounts, (GFunc)g_object_unref, NULL);
  g_slist_free(mgr->accounts);

  // go through the list of EAccount objects and create EeeAccount objects
  for (iter = e_list_get_iterator(E_LIST(mgr->ealist));
       e_iterator_is_valid(iter);
       e_iterator_next(iter))
  {
    EAccount *eaccount = E_ACCOUNT(e_iterator_get(iter));
    const char* email = e_account_get_string(eaccount, E_ACCOUNT_ID_ADDRESS);
    char* server_hostname = get_eee_server_hostname(email);
    g_debug("** EEE ** EAccount found, searching for 3E server: email=%s uid=%s", email, eaccount->uid);

    if (server_hostname)
    {
      EeeAccount* account;
      g_debug("** EEE ** Found 3E server enabled account '%s'! (%s)", email, server_hostname);

      account = g_new0(EeeAccount, 1);
      account->accessible = 1;
      account->email = g_strdup(email); 
      account->server = server_hostname;
      mgr->accounts = g_slist_append(mgr->accounts, account);
      sync_calendar_list_from_server(mgr, account);
    }
  }

  sync_group_list(mgr);
  return TRUE;
}

/* callback listening for changes in account list */
static void e_account_list_changed(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  g_debug("** EEE ** EAccountList changed (%p)", mgr);
  eee_accounts_manager_sync(mgr);
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
       
  mgr = g_new0(EeeAccountsManager, 1);
  mgr->gconf = gconf_client_get_default();
  mgr->ealist = e_account_list_new(mgr->gconf);
  mgr->eslist = e_source_list_new_for_gconf(mgr->gconf, CALENDAR_SOURCES);

  g_print("\n\n");
  g_debug("** EEE ** Starting EeeAccountsManager %p", mgr);

  eee_accounts_manager_sync(mgr);

  g_signal_connect(mgr->ealist, "account_added", G_CALLBACK(e_account_list_changed), mgr);
  g_signal_connect(mgr->ealist, "account_changed", G_CALLBACK(e_account_list_changed), mgr);
  g_signal_connect(mgr->ealist, "account_removed", G_CALLBACK(e_account_list_changed), mgr);    

  g_print("\n\n");
  return mgr;
}

/* store calendar settings on the server */
gboolean eee_server_store_calendar_settings(EeeCalendar* cal)
{
  xr_client_conn* conn;
  GError* err = NULL;

  conn = eee_server_connect_to_account(cal->access_account);
  if (conn == NULL)
    return FALSE;

  char* settings_str = eee_settings_encode(cal->settings);
  char* calspec = g_strdup_printf("%s:%s", cal->owner_account->email, cal->name);
  ESClient_updateCalendarSettings(conn, calspec, settings_str, &err);
  g_free(settings_str);
  g_free(calspec);
  xr_client_free(conn);

  if (err)
  {
    g_debug("** EEE ** Failed to store settings for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

/** Find EeeCalendar object by name.
 *
 * @param acc EeeAccount object.
 * @param name Name.
 *
 * @return Matching EeeCalendar object or NULL.
 */
EeeCalendar* eee_accounts_manager_find_calendar_by_name(EeeAccount* acc, const char* name)
{
  GSList* iter;
  if (acc == NULL || name == NULL)
    return NULL;
  for (iter = acc->calendars; iter; iter = iter->next)
  {
    EeeCalendar* c = iter->data;
    if (c->name && !strcmp(c->name, name))
      return c;
  }
  return NULL;
}

/** Find EeeAccount object by ESourceGroup name/EAccount email.
 *
 * @param mgr EeeAccountsManager object.
 * @param email E-mail of the account.
 *
 * @return Matching EeeAccount object or NULL.
 */
EeeAccount* eee_accounts_manager_find_account_by_email(EeeAccountsManager* mgr, const char* email)
{
  GSList* iter;
  if (mgr == NULL || email == NULL)
    return NULL;
  for (iter = mgr->accounts; iter; iter = iter->next)
  {
    EeeAccount* a = iter->data;
    if (a->email && !strcmp(a->email, email))
      return a;
  }
  return NULL;
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
  return eee_accounts_manager_find_calendar_by_name(account, e_source_get_property(source, "eee-calendar-name"));
}

/** Remove Calendar or Calendar subscription.
 *
 * @param mgr EeeAccountsManager object.
 * @param cal ESource object of the calendar.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean eee_accounts_manager_remove_calendar(EeeAccountsManager* mgr, ESource* source)
{
  GError* err = NULL;
  EeeCalendar* calendar = eee_accounts_manager_find_calendar_by_source(mgr, source);
  if (calendar == NULL)
    return FALSE;

  // get ECal and remove calendar from the server
  ECal* cal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
  if (!e_cal_remove(cal, &err))
  {
    // failure
  }
  g_object_unref(cal);

  return eee_accounts_manager_sync(mgr);
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

/* GObject foo */

G_DEFINE_TYPE(EeeAccountsManager, eee_accounts_manager, G_TYPE_OBJECT);

static void eee_accounts_manager_init(EeeAccountsManager *self)
{
  self->priv = g_new0(EeeAccountsManagerPriv, 1);
}

static void eee_accounts_manager_dispose(GObject *object)
{
  EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);

  G_OBJECT_CLASS(eee_accounts_manager_parent_class)->dispose(object);
}

static void eee_accounts_manager_finalize(GObject *object)
{
  EeeAccountsManager *self = EEE_ACCOUNTS_MANAGER(object);

  g_debug("** EEE ** Stoppping EeeAccountsManager %p", self);
  g_slist_foreach(self->accounts, (GFunc)g_object_unref, NULL);
  g_slist_free(self->accounts);
  g_object_unref(self->gconf);
  g_object_unref(self->ealist);
  g_object_unref(self->eslist);

  g_free(self->priv);
  g_signal_handlers_destroy(object);

  G_OBJECT_CLASS(eee_accounts_manager_parent_class)->finalize(object);
}

static void eee_accounts_manager_class_init(EeeAccountsManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = eee_accounts_manager_dispose;
  gobject_class->finalize = eee_accounts_manager_finalize;
}
