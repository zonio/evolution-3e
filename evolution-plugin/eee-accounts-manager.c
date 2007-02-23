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
  ESource* source;
};

struct EeeAccount
{
  char* uid;
  char* email;
  char* eee_server;
  ESourceGroup* group;
  GSList* calendars;                     /**< EeeCalendar */
};

struct EeeAccountsManager
{
  GConfClient* gconf_client;
  EAccountList* eaccount_list;
  GSList* accounts;                      /**< EeeAccount */
};

static int load_calendar_list_from_server(EeeAccount* a)
{
  return 0;
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
  ESourceGroup *group;
  ESource *source;
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
        load_calendar_list_from_server(account);
        break;
      }
    }
    g_strfreev(txt_list);
  }

  g_signal_connect(mgr->eaccount_list, "account_added", G_CALLBACK(e_account_added), mgr);
  g_signal_connect(mgr->eaccount_list, "account_changed", G_CALLBACK(e_account_changed), mgr);
  g_signal_connect(mgr->eaccount_list, "account_removed", G_CALLBACK(e_account_removed), mgr);    

  // synchronize calendar source list with the server
  list = e_source_list_new_for_gconf(mgr->gconf_client, CALENDAR_SOURCES);
  for (iter1 = e_source_list_peek_groups(list); iter1; iter1 = iter1->next)
  {
    group = E_SOURCE_GROUP(iter1->data);
    if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
      continue;
    
    const gchar* group_name = e_source_group_peek_name(group);
    // for each source
    for (iter2 = e_source_group_peek_sources(group); iter2; iter2 = iter2->next)
    {
      source = E_SOURCE(iter2->data);

      g_debug("** EEE ** Found 3E ESource: group=%s source=%s", group_name, e_source_peek_name(source));
    }
  }
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

#if 0
void eee_get_server_calendar_list(const char* email)
{
  // get server hostname
  // open connection to server
  // get password (ask for it if necessary)
  // login
  // get list of calendars
  xr_client_open(priv->conn, priv->server_uri, &err);
  if (err != NULL)
  {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Failed to estabilish connection to the server", err);
      g_clear_error(&err);
      return GNOME_Evolution_Calendar_OtherError;
  }

  priv->is_open = TRUE;

  rs = ESClient_auth(priv->conn, priv->username, priv->password, &err);
  if (err != NULL)
  {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Authentication failed", err);
      g_clear_error(&err);
      xr_client_close(priv->conn);
      priv->is_open = FALSE;
      return GNOME_Evolution_Calendar_OtherError;
  }

  if (!rs)
  {
      xr_client_close(priv->conn);
      priv->is_open = FALSE;
      e_cal_backend_notify_error(E_CAL_BACKEND(backend), "Authentication failed (invalid password or username)");
      return GNOME_Evolution_Calendar_AuthenticationFailed;
  }
}
#endif

#if 0
  group = e_source_group_new (group_name,  GROUPWISE_URI_PREFIX);
  if (!e_source_list_add_group (source_list, group, -1))
    return;
  relative_uri = g_strdup_printf ("%s@%s/", url->user, poa_address);
      if (strcmp (e_source_peek_relative_uri (source), old_relative_uri) == 0)
      {
        new_relative_uri = g_strdup_printf ("%s@%s/", new_url->user, new_poa_address); 
        e_source_group_set_name (group, new_group_name);
        e_source_set_relative_uri (source, new_relative_uri);
        e_source_set_property (source, "username", new_url->user);
        e_source_set_property (source, "port", camel_url_get_param (new_url,"soap_port"));
        e_source_set_property (source, "use_ssl",  camel_url_get_param (url, "use_ssl"));
  e_source_set_property (source, "auth-domain", "Groupwise");
        e_source_set_property (source, "offline_sync",  camel_url_get_param (url, "offline_sync") ? "1" : "0");
        e_source_list_sync (list, NULL);
        found_group = TRUE;
        g_free (new_relative_uri);
        break;
      }
#endif
