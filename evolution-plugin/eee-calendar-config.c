#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libedataserverui/e-source-selector.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-cal-popup.h>
#include <shell/es-event.h>
#include <mail/em-menu.h>

#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"
#include "subscribe.h"
#include "acl.h"

/* plugin intialization */

static EeeAccountsManager* _mgr = NULL;

int e_plugin_lib_enable(EPluginLib* ep, int enable)
{
  xr_debug_enabled = XR_DEBUG_CALL;
  return 0;
}

/* calendar add/properties dialog */

static GtkWidget* hidden = NULL;
static GtkWidget* notice_label = NULL;
static GtkWidget* offline_label = NULL;

static gboolean is_new_calendar_dialog(ESource* source)
{
  // if ESource does not have relative_uri, dialog is for creating new calendar
  const char *rel_uri = e_source_peek_relative_uri(source);
  return !(rel_uri && strlen(rel_uri));
}

/* create toplevel notices in dialog (offline mode notice, iaccessible calendar account notice) */
GtkWidget *eee_calendar_properties_factory_top(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);

  // ignore non 3e calendars
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return gtk_label_new("");

  if (!hidden)
    hidden = gtk_label_new("");

  if (data->old)
  {
    //XXX: free widgets? WTF?
    gtk_widget_destroy(notice_label);
    gtk_widget_destroy(offline_label);
    notice_label = NULL;
    offline_label = NULL;
  }

  int row = GTK_TABLE(data->parent)->nrows;

  char* msg = g_markup_printf_escaped("<span weight=\"bold\" foreground=\"#ff0000\">%s</span>", 
    "Evolution is in offline mode. You cannot create or modify calendars now.\n"
    "Please switch to online mode for such operations.");
  offline_label = gtk_label_new("");
  gtk_label_set_markup(GTK_LABEL(offline_label), msg);
  g_free(msg);
  gtk_table_attach(GTK_TABLE(data->parent), offline_label, 0, 2, row, row+1, GTK_FILL|GTK_EXPAND, 0, 0, 0);

  row++;
  msg = g_markup_printf_escaped("<span weight=\"bold\" foreground=\"#ff0000\">%s</span>", 
    "You cannot create calendars for this account.");
  notice_label = gtk_label_new("");
  gtk_label_set_markup(GTK_LABEL(notice_label), msg);
  g_free(msg);
  gtk_table_attach(GTK_TABLE(data->parent), notice_label, 0, 2, row, row+1, GTK_FILL|GTK_EXPAND, 0, 0, 0);

  return notice_label;
}

GtkWidget *eee_calendar_properties_factory(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);

  // ignore non 3e calendars
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return gtk_label_new("");

  g_debug("** EEE ** Properties Dialog Items Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  if (!hidden)
    hidden = gtk_label_new("");

  // can't do anything in offline mode
  if (!eee_plugin_online)
  {
    gtk_widget_show(offline_label);
    return hidden;
  }

  // can't do anything in offline mode
  if (is_new_calendar_dialog(target->source))
  {
    EeeAccount* account = eee_accounts_manager_find_account_by_group(_mgr, group);
    if (account == NULL)
    {
      g_debug("** EEE ** internal error, can't find account");
      return hidden;
    }

    if (!account->accessible)
    {
      gtk_widget_show(notice_label);
      return hidden;    
    }
  }

  return hidden;
}

gboolean eee_calendar_properties_check(EPlugin* epl, EConfigHookPageCheckData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);

  // ignore non 3e calendars (assume they are OK)
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return TRUE;

  g_debug("** EEE ** Properties Dialog Check Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  // check if we should bother with detailed checks
  if (!eee_plugin_online)
    return FALSE;

  if (is_new_calendar_dialog(target->source))
  {
    // creating new calendar
    EeeAccount* account = eee_accounts_manager_find_account_by_group(_mgr, group);
    if (account == NULL)
    {
      g_debug("** EEE ** internal error, can't find account");
      return FALSE;
    }
    if (!account->accessible)
      return FALSE;   
  }
  else
  {
    // editting properties of existing calendar
    EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, target->source);
    if (cal == NULL)
    {
      g_debug("** EEE ** internal error, can't find calendar");
      return FALSE;
    }
  }

  return TRUE;
}

void eee_calendar_properties_commit(EPlugin* epl, ECalConfigTargetSource* target)
{
  ESourceGroup *group = e_source_peek_group(target->source);

  // ignore non 3e calendars
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return;

  g_debug("** EEE ** Properties Dialog Commit Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  // check if we should bother with commit at all
  if (!eee_plugin_online)
    return;

  if (is_new_calendar_dialog(target->source))
  {
    // creating new calendar
    EeeAccount* account = eee_accounts_manager_find_account_by_group(_mgr, group);
    if (account == NULL)
    {
      g_debug("** EEE ** internal error, can't find account");
      return;
    }
    if (!account->accessible)
    {
      g_debug("** EEE ** internal error, account is not accessible");
      return;
    }

    xr_client_conn* conn = eee_account_connect(account);
    if (conn == NULL)
    {
      g_debug("** EEE ** internal error, can't connect to account");
      return;
    }

    // create calname and settings string
    EeeSettings* settings = eee_settings_new(NULL);
    guint32 color = 0;
    e_source_get_color(target->source, &color);
    eee_settings_set_color(settings, color);
    eee_settings_set_title(settings, e_source_peek_name(target->source));
    char* settings_string = eee_settings_encode(settings);
    g_object_unref(settings);
    const char* calname = e_source_peek_name(target->source);

    GError* err = NULL;
    ESClient_newCalendar(conn, (char*)calname, &err);
    if (err == NULL)
    {
      if (!ESClient_updateCalendarSettings(conn, (char*)calname, settings_string, NULL))
        g_debug("** EEE ** failed to update settings on new calendar (%d:%s)", err->code, err->message);
    }
    g_free(settings_string);
    xr_client_free(conn);

    if (err)
    {
      g_debug("** EEE ** internal error, can't create calendar (%d:%s)", err->code, err->message);
      g_clear_error(&err);
    }

    eee_accounts_manager_sync(_mgr);
  }
  else
  {
    // editting properties of existing calendar
    EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, target->source);
    if (cal == NULL)
    {
      g_debug("** EEE ** internal error, can't find calendar");
      return;
    }

    guint32 color;
    e_source_get_color(target->source, &color);
    e_source_set_color(target->source, color);
    eee_settings_set_color(cal->settings, color);
    eee_settings_set_title(cal->settings, e_source_peek_name(target->source));
    eee_calendar_store_settings(cal);
  }
}

/* calendar source list popup menu items */

static void on_permissions_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  if (!eee_plugin_online)
    return;

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);

  g_debug("** EEE ** on_permissions_cb: (source=%s)", e_source_peek_name(source));

  acl_gui_create(cal);
}

static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  if (!eee_plugin_online)
    return;

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);
  if (cal == NULL)
    return;

  // ignore owned calendars
  if (cal->access_account == cal->owner_account)
    return;

  GError* err = NULL;
  xr_client_conn* conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return;

  char* calspec = g_strdup_printf("%s:%s", cal->owner_account->email, cal->name);
  ESClient_unsubscribeCalendar(conn, calspec, &err);
  g_free(calspec);
  xr_client_free(conn);

  if (err)
  {
    g_debug("** EEE ** on_unsubscribe_cb: unsubscribe failed (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return;
  }

  if (_mgr)
    eee_accounts_manager_sync(_mgr);
}

static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  if (!eee_plugin_online)
    return;

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);
  if (cal == NULL)
    return;

  // ignore subscribed calendars
  if (cal->access_account != cal->owner_account)
    return;

  GError* err = NULL;
  xr_client_conn* conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return;

  ESClient_deleteCalendar(conn, cal->name, &err);
  xr_client_free(conn);

  if (err)
  {
    g_debug("** EEE ** on_delete_cb: delete failed (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return;
  }

#if 0
  // get ECal and remove calendar from the server
  GError* err = NULL;
  ECal* ecal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
  if (!e_cal_remove(ecal, &err))
  {
    g_debug("** EEE ** on_delete_cb: ECal remove failed (%d:%s)", err->code, err->message);
    g_clear_error(&err);
  }
  g_object_unref(ecal);
#endif

  if (_mgr)
    eee_accounts_manager_sync(_mgr);
}

static EPopupItem popup_items_shared_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe", on_unsubscribe_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, 0xffff },
};

static EPopupItem popup_items_user_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_USER|E_CAL_POPUP_SOURCE_PRIMARY },
};

static EPopupItem popup_items_cal_offline[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, 0xffff },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe", on_unsubscribe_cb, NULL, "stock_delete", 0, 0xffff },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, 0xffff },
  { E_POPUP_ITEM, "30.properties", "_Properties...", NULL, NULL, "stock_folder-properties", 0, 0xffff },
};

static void popup_free(EPopup *ep, GSList *items, void *data)
{
  g_slist_free(items);
}

void eee_calendar_popup_source_factory(EPlugin* ep, ECalPopupTargetSource* target)
{
  int items_count;
  EPopupItem* items;
  GSList* menus = NULL;
  int i;

  // get selected source (which was right-clciked on)
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  // ignore non 3E groups
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return;

  if (eee_plugin_online)
  {
    EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);
    if (cal == NULL)
    {
      g_debug("** EEE ** Can't get EeeCalendar for ESource. (%s)", e_source_peek_name(source));
      goto offline_mode;
    }
    if (cal->owner_account->accessible)
    {
      items_count = G_N_ELEMENTS(popup_items_user_cal);
      items = popup_items_user_cal;
    }
    else
    {
      items_count = G_N_ELEMENTS(popup_items_shared_cal);
      items = popup_items_shared_cal;
    }
  }
  else
  {
   offline_mode:
    items_count = G_N_ELEMENTS(popup_items_cal_offline);
    items = popup_items_cal_offline;
  }

  // add popup items
  for (i = 0; i < items_count; i++)
    menus = g_slist_prepend(menus, items+i);
  e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
}

/* watch evolution state (online/offline) */

gboolean eee_plugin_online;

void eee_calendar_state_changed(EPlugin *ep, ESEventTargetState *target)
{
  int online = target->state;
  g_debug("** EEE ** State changed to: %s", online ? "online" : "offline");
  eee_plugin_online = !!online;
  if (online)
  {
    // force calendar list synchronization, etc.
    if (_mgr)
      eee_accounts_manager_sync(_mgr);
    else
      _mgr = eee_accounts_manager_new();
    
    // hacky hacky hack hack
    if (offline_label)
      gtk_widget_hide(offline_label);
  }
  else
  {
    // shutdown open acl/subscribe dialogs, etc.
    if (offline_label)
      gtk_widget_show(offline_label);
  }
}

/* watch for evolution calendar component activation */

static gint activation_cb(gpointer data)
{
  if (!eee_plugin_online)
    return FALSE;
  if (_mgr == NULL)
    _mgr = eee_accounts_manager_new();
  return FALSE;
}

void eee_calendar_component_activated(EPlugin *ep, ESEventTargetComponent *target)
{
  if (strstr(target->name, "OAFIID:GNOME_Evolution_Calendar_Component") == NULL)
    return;

  /* create EeeAccountsManager singleton and register it for destruction */
  g_idle_add(activation_cb, NULL);  
}

/* calendar subscription menu item callback */

void eee_calendar_subscription(EPlugin *ep, EMMenuTargetSelect *target)
{
  subscribe_gui_create(_mgr);
}
