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

GtkWidget *eee_calendar_properties_items(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *target = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group;

  group = e_source_peek_group(target->source);
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return NULL;
  g_debug("** EEE ** Properties Dialog Items Hook Call (source=%s)", e_source_peek_name(target->source));

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

  guint32 color;
  e_source_get_color(target->source, &color);
  eee_settings_set_color(cal->settings, color);
  eee_settings_set_title(cal->settings, source_name);

  eee_calendar_store_settings(cal);
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

  // get ECal and remove calendar from the server
  GError* err = NULL;
  ECal* ecal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
  if (!e_cal_remove(ecal, &err))
  {
    g_debug("** EEE ** on_delete_cb: ECal remove failed (%d:%s)", err->code, err->message);
    g_clear_error(&err);
  }
  g_object_unref(ecal);

  if (_mgr)
    eee_accounts_manager_sync(_mgr);
}

static EPopupItem popup_items_shared_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, 0xffff },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe", on_unsubscribe_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, 0xffff },
};

static EPopupItem popup_items_user_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe", on_unsubscribe_cb, NULL, "stock_delete", 0, 0xffff },
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
  }
  else
  {
    // shutdown open acl/subscribe dialogs, etc.
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
