#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libedataserverui/e-source-selector.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-cal-popup.h>
#include <shell/es-event.h>
#include <mail/em-menu.h>
#include <mail/em-config.h>

#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"
#include "eee-settings.h"
#include "utils.h"
#include "subscribe.h"
#include "acl.h"

/* plugin intialization */

static EeeAccountsManager* mgr()
{
  static EeeAccountsManager* _mgr = NULL;
  if (_mgr == NULL)
    _mgr = eee_accounts_manager_new();
  return _mgr;
}

int e_plugin_lib_enable(EPluginLib* ep, int enable)
{
  if (getenv("EEE_EVO_DEBUG"))
    xr_debug_enabled = XR_DEBUG_CALL;
  g_debug("** EEE ** Starting 3E Evolution Plugin %s", PACKAGE_VERSION);
  g_debug("** EEE ** Please report bugs to <%s>", PACKAGE_BUGREPORT);
  return 0;
}

/* calendar add/properties dialog */

static GtkWidget* hidden = NULL;
static GtkWidget* notice_label = NULL;
static GtkWidget* offline_label = NULL;

static gboolean is_new_calendar_dialog(ESource* source)
{
  gboolean is_new = FALSE;
  const char *rel_uri = e_source_peek_relative_uri(source);

  // if ESource does not have relative_uri, dialog is for creating new calendar,
  // first time this function is called on given ESource, it's result must be
  // stored in ESource, because relative_uri may be modified and we still want
  // to determine if dialog is for new or existing calendar
  is_new = rel_uri == NULL || strlen(rel_uri) == 0 || g_object_get_data(G_OBJECT(source), "is_new");

  if (is_new)
    g_object_set_data(G_OBJECT(source), "is_new", GUINT_TO_POINTER(1));

  return is_new;
}

/* create toplevel notices in dialog (offline mode notice, inaccessible calendar account notice) */
GtkWidget *eee_calendar_properties_factory_top(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);

  if (!hidden)
    hidden = gtk_label_new("");
  if (!e_source_group_is_3e(group))
    return hidden;

  /*
  if (data->old)
  {
    //XXX: free widgets? WTF?
    gtk_widget_destroy(notice_label);
    gtk_widget_destroy(offline_label);
    notice_label = NULL;
    offline_label = NULL;
  }*/

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
  EeeAccount* account;

  g_debug("** EEE ** Properties Dialog Items Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  if (!hidden)
    hidden = gtk_label_new("");
  if (!e_source_group_is_3e(group))
    return hidden;

  // can't do anything in offline mode
  if (!eee_plugin_online)
  {
    gtk_widget_show(offline_label);
    return hidden;
  }

  //XXX: check if account is online
  account = eee_accounts_manager_find_account_by_group(mgr(), group);
  if (account == NULL)
    return hidden;    

  if (is_new_calendar_dialog(target->source))
  {
    target->disable_source_update = TRUE;
  }
  else
  {
    if (!e_source_is_3e_owned_calendar(target->source))
      gtk_widget_show(notice_label);
  }

  return hidden;
}

gboolean eee_calendar_properties_check(EPlugin* epl, EConfigHookPageCheckData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);

  g_debug("** EEE ** Properties Dialog Check Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  if (!e_source_group_is_3e(group))
    return TRUE;
  if (!eee_plugin_online)
    return FALSE;

  if (is_new_calendar_dialog(target->source))
  {
    EeeAccount* account = eee_accounts_manager_find_account_by_group(mgr(), group);
    if (account == NULL)
      return FALSE;
  }
  else
  {
    EeeAccount* account = eee_accounts_manager_find_account_by_source(mgr(), target->source);
    if (account == NULL)
      return FALSE;
  }

  return TRUE;
}

void eee_calendar_properties_commit(EPlugin* epl, ECalConfigTargetSource* target)
{
  ESource* source = target->source;
  ESourceGroup *group = e_source_peek_group(source);

  g_debug("** EEE ** Properties Dialog Commit Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

  if (!e_source_group_is_3e(group))
    return;
  if (!eee_plugin_online)
    return;

  if (is_new_calendar_dialog(target->source))
  {
    EeeAccount* account = eee_accounts_manager_find_account_by_group(mgr(), group);
    if (account == NULL)
      return;

    // create settings string
    guint32 color = 0;
    e_source_get_color(target->source, &color);
    char* settings_string = eee_settings_string_from_parts(e_source_peek_name(source), color);
    char* calname = NULL;

    eee_account_create_new_calendar(account, settings_string, &calname);
    eee_account_disconnect(account);

    e_source_set_3e_properties(source, calname, account->name, account, settings_string);
    g_free(settings_string);
    g_free(calname);
  }
  else
  {
    // editting properties of existing calendar
    EeeAccount* account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (account == NULL)
      return;

    // create settings string
    guint32 color = 0;
    e_source_get_color(target->source, &color);
    char* settings_string = eee_settings_string_from_parts(e_source_peek_name(source), color);
    const char* calname = e_source_get_property(source, "eee-calname");
    const char* owner = e_source_get_property(source, "eee-owner");

    eee_account_update_calendar_settings(account, owner, calname, settings_string);
    eee_account_disconnect(account);

    g_free(settings_string);
  }

  eee_accounts_manager_abort_current_sync(mgr());
}

/* calendar source list popup menu items */

static void on_permissions_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  EeeAccount* account;

  if (!eee_plugin_online)
    return;

  account = eee_accounts_manager_find_account_by_source(mgr(), source);
  acl_gui_create(mgr(), account, source);
}

static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  const char* owner = e_source_get_property(source, "eee-owner");
  const char* calname = e_source_get_property(source, "eee-calname");
  EeeAccount* account;

  if (!eee_plugin_online)
    return;
  if (e_source_is_3e_owned_calendar(source))
    return;

  account = eee_accounts_manager_find_account_by_source(mgr(), source);
  if (eee_account_unsubscribe_calendar(account, owner, calname))
    e_source_group_remove_source(group, source);
  eee_account_disconnect(account);
  eee_accounts_manager_abort_current_sync(mgr());
}

static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  char* calname = (char*)e_source_get_property(source, "eee-calname");
  EeeAccount* account;
  GError* err = NULL;

  if (!eee_plugin_online)
    return;
  if (!e_source_is_3e_owned_calendar(source))
    return;

  account = eee_accounts_manager_find_account_by_source(mgr(), source);
  if (eee_account_delete_calendar(account, calname))
  {
    e_source_group_remove_source(group, source);

    // get ECal and remove calendar from the server
    ECal* ecal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
    if (!e_cal_remove(ecal, &err))
    {
      g_debug("** EEE ** on_delete_cb: ECal remove failed (%d:%s)", err->code, err->message);
      g_clear_error(&err);
    }
    g_object_unref(ecal);
  }
  eee_account_disconnect(account);
  eee_accounts_manager_abort_current_sync(mgr());
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
  // get selected source (which was right-clciked on)
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  int items_count;
  EPopupItem* items;
  GSList* menus = NULL;
  EeeAccount* account;
  int i;

  if (!e_source_is_3e(source))
    return;

  if (eee_plugin_online)
  {
    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (account == NULL)
      goto offline_mode;

    if (e_source_is_3e_owned_calendar(source))
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

  for (i = 0; i < items_count; i++)
    menus = g_slist_prepend(menus, items+i);

  e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
}

/* watch evolution state (online/offline) */

gboolean eee_plugin_online = FALSE;

void eee_calendar_state_changed(EPlugin *ep, ESEventTargetState *target)
{
  int online = target->state;

  g_debug("** EEE ** State changed to: %s", online ? "online" : "offline");

  eee_plugin_online = !!online;
  if (online)
  {
    mgr();
    // hacky hacky hack hack
    if (offline_label)
      gtk_widget_hide(offline_label);
  }
  else
  {
    // shutdown open acl/subscribe dialogs, etc.
    if (offline_label)
      gtk_widget_show(offline_label);
    acl_gui_destroy();
    subscribe_gui_destroy();
  }
}

/* watch for evolution calendar component activation */

static gint activation_cb(gpointer data)
{
  mgr();
  return FALSE;
}

void eee_calendar_component_activated(EPlugin *ep, ESEventTargetComponent *target)
{
  if (strstr(target->name, "OAFIID:GNOME_Evolution_Calendar_Component") == NULL)
    return;

  g_idle_add(activation_cb, NULL);  
}

/* calendar subscription menu item callback */

void eee_calendar_subscription(EPlugin *ep, EMMenuTargetSelect *target)
{
  subscribe_gui_create(mgr());
}
