#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <e-util/e-config.h>
#include <e-util/e-error.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-cal-popup.h>
#include <mail/em-menu.h>
#include <shell/es-event.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-account-list.h>
#include <libedataserverui/e-source-selector.h>
#include <libecal/e-cal.h>
#include <xr-lib.h>

#include <string.h>

#include "subscribe.h"
#include "acl.h"
#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"

/* plugin intialization */

static EeeAccountsManager* _mgr = NULL;

int e_plugin_lib_enable(EPluginLib* ep, int enable)
{
  xr_debug_enabled = XR_DEBUG_CALL;
  return 0;
}

/* calendar add/properties dialog */

GtkWidget *eee_calendar_properties_items(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group;

  group = e_source_peek_group(target->source);
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return NULL;
  g_debug("** EEE ** Properties Dialog Items Hook Call (source=%s)", e_source_peek_name(target->source));

#if 0
  GtkWidget *parent;
  GtkWidget *lurl;
  GtkWidget *location;
  char *uri;
  int row;

  uri = e_source_get_uri(source);

  parent = data->parent;
  row = GTK_TABLE(parent)->nrows;

  lurl = gtk_label_new_with_mnemonic("_URL:");
  gtk_widget_show(lurl);
  gtk_misc_set_alignment(GTK_MISC(lurl), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(parent), lurl, 0, 1, row, row + 1, GTK_FILL, 0, 0, 0);

  location = gtk_entry_new();
  gtk_widget_show(location);
  g_signal_connect(G_OBJECT(location), "changed", G_CALLBACK(location_changed), source);
  gtk_entry_set_text(GTK_ENTRY(location), uri);
  gtk_table_attach(GTK_TABLE(parent), location, 1, 2, row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

  gtk_label_set_mnemonic_widget(GTK_LABEL(lurl), location);

  g_free(uri);
#endif
  return NULL;
}

gboolean eee_calendar_properties_check(EPlugin* epl, EConfigHookPageCheckData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(target->source);
  const char* source_name = e_source_peek_name(target->source);

  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return TRUE;
  g_debug("** EEE ** Properties Dialog Check Hook Call (source=%s)", source_name);

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, target->source);
  if (cal == NULL)
  {
    g_debug("** EEE ** Can't get EeeCalendar for ESource. (%s)", source_name);
    return FALSE;
  }

  return TRUE;
}

void eee_calendar_properties_commit(EPlugin* epl, ECalConfigTargetSource* target)
{
  ESourceGroup *group = e_source_peek_group(target->source);
  const char* source_name = e_source_peek_name(target->source);

  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return;
  g_debug("** EEE ** Properties Dialog Commit Hook Call (source=%s)", source_name);

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, target->source);
  if (cal == NULL)
  {
    g_debug("** EEE ** Can't get EeeCalendar for ESource. (%s)", source_name);
    return;
  }

  if (cal->settings == NULL)
    cal->settings = eee_settings_new(NULL);
  e_source_get_color(target->source, &cal->settings->color);
  g_free(cal->settings->title);
  cal->settings->title = g_strdup(source_name);

  eee_server_store_calendar_settings(cal);
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

  acl_gui_create();
}

static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);

  if (!eee_plugin_online)
    return;

  g_debug("** EEE ** on_unsubscribe_cb: (source=%s)", e_source_peek_name(source));
}

static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);
  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);

  if (!eee_plugin_online)
    return;

  g_debug("** EEE ** on_delete_cb: This shouldn't happen! (source=%s)", e_source_peek_name(source));
}

static EPopupItem popup_items_shared_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, 0xffff },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe this calendar", on_unsubscribe_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, 0xffff },
};

static EPopupItem popup_items_user_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe this calendar", on_unsubscribe_cb, NULL, "stock_delete", 0, 0xffff },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  //{ E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_USER|E_CAL_POPUP_SOURCE_PRIMARY },
};

static EPopupItem popup_items_cal_offline[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, 0xffff },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe this calendar", on_unsubscribe_cb, NULL, "stock_delete", 0, 0xffff },
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
    // force callist synchronization, etc.
  }
  else
  {
    // shutdown open acl/subscribe dialogs, etc.
  }
}

/* watch for evolution calendar component activation */

static gint activation_cb(gpointer data)
{
  if (_mgr == NULL)
    _mgr = eee_accounts_manager_new();  
  return FALSE;
}

void eee_calendar_component_activated(EPlugin *ep, ESEventTargetComponent *target)
{
  g_debug("** EEE ** Component changed to: %s", target->name);
  if (strstr(target->name, "OAFIID:GNOME_Evolution_Calendar_Component") == NULL)
    return;

  /* create EeeAccountsManager singleton and register it for destruction */
  g_idle_add(activation_cb, NULL);  
}

void eee_calendar_subscription(EPlugin *ep, EMMenuTargetSelect *target)
{
  g_debug("** EEE ** subscribe");

  subscribe_gui_create(_mgr);
}
