#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <libedataserver/e-account-list.h>
#include <libedataserver/e-source-list.h>
#include "eee-accounts-manager.h"
#include "dns-txt-search.h"

#include "interface/ESClient.xrc.h"

#define EEE_URI_PREFIX   "eee://" 
#define EEE_PREFIX_LENGTH (sizeof(EEE_URI_PREFIX)-1)
#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define SELECTED_CALENDARS "/apps/evolution/calendar/display/selected_calendars"

/** Role of the EeeAccountsManager is to keep calendar ESourceList in sync with
 * EAccountList (each account must have it's own ESourceGroup) and ESource in
 * each group in sync with list of calendars on the 3e server.
 *
 * First we load list of EAccount objects (email accounts in evolution), then
 * we determine hostnames of the 3e servers for each email account (and if it 
 * has one) and automatically load list of calendars from the 3e server.
 *
 * Then we load existing ESourceGroup objects with eee:// URI prefix (i.e. list of
 * eee accounts in the calendar view) and their associated ESources.
 *
 * Last step is to compare lists of ESource obejcts (eee accounts list in
 * calendar view) with list of calendars stored on the server and update
 * list of ESource obejcts to match list of calendars stored on the server.
 *
 * After this initial sync, our local list of EeeAccount and EeeCalendar obejcts
 * will be in sync with either list of calendar sources in gconf (what is shown
 * in calendar view) and list of existing email accounts (EAccount objects).
 *
 * Now we will setup notification mechanism for EAccount and calendar list changes.
 * We will also periodically fetch list of calendars from the 3e server and
 * update list of calendars in the calendars source list.
 *
 * GUI for adding/removing callendars will call methods of EeeAccountsManager
 * instead of directly playing with ESourceList content. This will assure
 * consistency of our local calendar list.
 */

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

struct EeeAccountsManager
{
  GConfClient* gconf_client;
  EAccountList* eaccount_list;
  GSList* accounts;                      /**< EeeAccount */
};

static int load_calendar_list_from_server(EeeAccountsManager* mgr, EeeAccount* a)
{
  xr_client_conn* conn;
  GError* err = NULL;
  int rs;
  char* server_uri;
  GSList *cals, *iter;

  g_debug("** EEE ** Loading calendar list from the 3E server: server=%s user=%s", a->eee_server, a->email);

  conn = xr_client_new(&err);
  if (err)
  {
    g_debug("** EEE ** Can't create client interface. (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return -1;
  }

  server_uri = g_strdup_printf("https://%s/ESClient", a->eee_server);
  xr_client_open(conn, server_uri, &err);
  g_free(server_uri);
  if (err)
  {
    g_debug("** EEE ** Can't open connection to the server. (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    xr_client_free(conn);
    return -1;
  }
  
  //XXX: ask for password
  rs = ESClient_auth(conn, a->email, "qwe", &err);
  if (err)
  {
    g_debug("** EEE ** Authentization failed for user '%s'. (%d:%s)", a->email, err->code, err->message);
    g_clear_error(&err);
    xr_client_free(conn);
    return -1;
  }

  cals = ESClient_getCalendars(conn, &err);
  if (err)
  {
    g_debug("** EEE ** Failed to get calendars for user '%s'. (%d:%s)", a->email, err->code, err->message);
    g_clear_error(&err);
    xr_client_free(conn);
    return -1;
  }

  // process retrieved calendars
  for (iter = cals; iter; iter = iter->next)
  {
    ESCalendar* cal = iter->data;
    g_debug("** EEE ** %s: Found calendar on the server (%s:%s:%s:%s)", a->email, cal->owner, cal->name, cal->perm, cal->settings);

    EeeCalendar* ecal = g_new0(EeeCalendar, 1);
    ecal->name = g_strdup(cal->name);
    ecal->perm = g_strdup(cal->perm);
    ecal->login_account = a;
    ecal->settings = eee_settings_new(cal->settings);

    // find existing EeeAccount or create new 
    EeeAccount* acc = eee_accounts_manager_find_account(mgr, cal->owner);
    if (acc == NULL)
    {
      acc = g_new0(EeeAccount, 1);
      acc->email = g_strdup(cal->owner);
      acc->eee_server = g_strdup(a->eee_server); // inherit eee server
      mgr->accounts = g_slist_append(mgr->accounts, acc);
    }

    // add calendar to the account
    acc->calendars = g_slist_append(acc->calendars, ecal);
  }
  
  g_slist_foreach(cals, (GFunc)ESCalendar_free, NULL);
  g_slist_free(cals);

  xr_client_free(conn);
  return 0;
}

EeeAccount* eee_accounts_manager_find_account(EeeAccountsManager* mgr, const char* email)
{
  GSList* iter;
  for (iter = mgr->accounts; iter; iter = iter->next)
  {
    EeeAccount* a = iter->data;
    if (!strcmp(a->email, email))
      return a;
  }
  return NULL;
}

static void e_account_added(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  g_debug("** EEE ** EAccount added (%p)", mgr);
  //parent = (EAccount *)e_account_list_find (account_listener, E_ACCOUNT_FIND_UID, account->parent_uid);
}

static void e_account_removed(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  g_debug("** EEE ** EAccount removed (%p)", mgr);
}

static void e_account_changed(EAccountList *account_list, EAccount *account, EeeAccountsManager* mgr)
{
  g_debug("** EEE ** EAccount changed (%p)", mgr);
}

EeeAccountsManager* eee_accounts_manager_new()
{
  EeeAccountsManager *mgr;
  ESourceList* list;
  GSList* iter1, *iter2;
  EIterator *eiter;
       
  mgr = g_new0(EeeAccountsManager, 1);
  mgr->gconf_client = gconf_client_get_default();
  mgr->eaccount_list = e_account_list_new(mgr->gconf_client);
  g_print("\n\n");
  g_debug("** EEE ** Starting EeeAccountsManager %p", mgr);

  // go through all EAccount objects
  for (eiter = e_list_get_iterator(E_LIST(mgr->eaccount_list));
       e_iterator_is_valid(eiter);
       e_iterator_next(eiter))
  {
    EAccount *eaccount = E_ACCOUNT(e_iterator_get(eiter));
    char* domain;
    const char* email;
    char** txt_list;
    guint i;

    email = e_account_get_string(eaccount, E_ACCOUNT_ID_ADDRESS);
    domain = strchr(email, '@');
    if (!domain) // invalid email address
      continue;

    g_debug("** EEE ** EAccount found, searching for 3E server: email=%s uid=%s", email, eaccount->uid);
    txt_list = get_txt_records(++domain);
    if (txt_list == NULL)
    {
      g_debug("** EEE ** 3E server hostname can't be determined. Your admin forgot to setup 3E TXT records in DNS?");
      continue;
    }

    for (i = 0; i < g_strv_length(txt_list); i++)
    {
      // parse TXT records if any
      if (g_str_has_prefix(txt_list[i], "eee server="))
      {
        EeeAccount* account;
        char* eee_server = g_strstrip(g_strdup(txt_list[i]+sizeof("eee server=")-1)); // expected to be in format hostname:port
        g_debug("** EEE ** Found 3E server enabled account '%s'! (%s)", email, eee_server);

        account = g_new0(EeeAccount, 1);
        mgr->accounts = g_slist_append(mgr->accounts, account);
        account->uid = g_strdup(eaccount->uid);
        account->email = g_strdup(email); 
        account->eee_server = eee_server;
        load_calendar_list_from_server(mgr, account);
        break;
      }
    }
    g_strfreev(txt_list);
  }

  g_signal_connect(mgr->eaccount_list, "account_added", G_CALLBACK(e_account_added), mgr);
  g_signal_connect(mgr->eaccount_list, "account_changed", G_CALLBACK(e_account_changed), mgr);
  g_signal_connect(mgr->eaccount_list, "account_removed", G_CALLBACK(e_account_removed), mgr);    

  g_debug("** EEE ** Updating calendar source list.");
  // synchronize calendar source list with the server
  list = e_source_list_new_for_gconf(mgr->gconf_client, CALENDAR_SOURCES);
  GSList* dup_list = g_slist_copy(e_source_list_peek_groups(list));
  for (iter1 = dup_list; iter1; iter1 = iter1->next)
  {
    ESourceGroup* group = E_SOURCE_GROUP(iter1->data);
    if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
      continue;
    
    g_debug("** EEE ** Removing group: %s.", e_source_group_peek_name(group));
    e_source_list_remove_group(list, group);
#if 0
    const gchar* group_name = e_source_group_peek_name(group);
    // for each source
    for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2->next)
    {
      source = E_SOURCE(iter2->data);

      g_debug("** EEE ** Found 3E ESource: group=%s source=%s", group_name, e_source_peek_name(source));
    }
#endif
  }
  g_slist_free(dup_list);

  // fill in source list from EeeAccount and EeeCalendar objects
  for (iter1 = mgr->accounts; iter1; iter1 = iter1->next)
  {
    EeeAccount* a = iter1->data;

    char* group_name = g_strdup_printf("3E: %s", a->email);
    a->group = e_source_group_new(group_name,  EEE_URI_PREFIX);
    g_free(group_name);
    if (!e_source_list_add_group(list, a->group, -1))
    {
      g_object_unref(a->group);
      a->group = NULL;
      continue;
    }

    for (iter2 = a->calendars; iter2; iter2 = iter2->next)
    {
      EeeCalendar* c = iter2->data;

      char* relative_uri = g_strdup_printf("%s/%s/%s", a->eee_server, c->login_account->email, c->name);
      c->source = e_source_new(c->settings->title ? c->settings->title : c->name, relative_uri);
      g_free(relative_uri);
      e_source_set_property(c->source, "auth", "1");
      e_source_set_property(c->source, "username", c->login_account->email);
      e_source_set_property(c->source, "auth-domain", "3E Accounts");
      e_source_set_color(c->source, c->settings->color > 0 ? c->settings->color : 0xEEBC60);
      e_source_group_add_source(a->group, c->source, -1);
    }
  }
  e_source_list_sync(list, NULL);
  g_object_unref(list);
  g_print("\n\n");

  return mgr;
}

void eee_accounts_manager_free(EeeAccountsManager* mgr)
{
  g_debug("** EEE ** Stoppping EeeAccountsManager %p", mgr);
	g_object_unref(mgr->gconf_client);
	g_object_unref(mgr->eaccount_list);
  g_free(mgr);
}

EeeSettings* eee_settings_new(const char* string)
{
  guint i;
  EeeSettings* s = g_new0(EeeSettings, 1);

  char** pairs = g_strsplit(string, ";", 0);
  for (i=0; i<g_strv_length(pairs); i++)
  {
    pairs[i] = g_strstrip(pairs[i]);
    if (strlen(pairs[i]) < 1 || strchr(pairs[i], '=') == NULL)
      continue;
    char* key = pairs[i];
    char* val = strchr(key, '=');
    *val = '\0';
    ++val;
    // now we have key and value
    if (!strcmp(key, "title"))
      s->title = g_strdup(val);
    else if (!strcmp(key, "color"))
      sscanf(val, "#%x", &s->color);
  }
  g_strfreev(pairs);

  return s;
}

char* eee_settings_get_string(EeeSettings* s)
{
  return g_strdup_printf("title=%s;color=#%06x;", s->title, s->color);
}

void eee_settings_free(EeeSettings* s)
{
  if (s == NULL)
    return;
  g_free(s->title);
  g_free(s);
}
