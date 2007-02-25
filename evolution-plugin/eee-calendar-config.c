#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <e-util/e-config.h>
#include <e-util/e-error.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-cal-popup.h>
#include <shell/es-event.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-account-list.h>
#include <libedataserverui/e-source-selector.h>
#include <libecal/e-cal.h>

#include <string.h>

#include "eee-accounts-manager.h"

/* plugin intialization */

static EeeAccountsManager* _mgr = NULL;
static void eee_accounts_manager_destroy()
{
  eee_accounts_manager_free(_mgr);
}

int e_plugin_lib_enable(EPluginLib* ep, int enable)
{
  /* create EeeAccountsManager singleton and register it for destruction */
  if (_mgr == NULL)
  {
    _mgr = eee_accounts_manager_new();  
    g_atexit(eee_accounts_manager_destroy);
  }
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

  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return TRUE;
  g_debug("** EEE ** Properties Dialog Check Hook Call (source=%s)", e_source_peek_name(target->source));

  return TRUE;
}

void eee_calendar_properties_commit(EPlugin* epl, ECalConfigTargetSource* target)
{
  ESourceGroup *group = e_source_peek_group(target->source);
  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return;
  g_debug("** EEE ** Properties Dialog Commit Hook Call (source=%s)", e_source_peek_name(target->source));

//  if (e_source_get_property (source, "default"))
//    e_book_set_default_source (source, NULL);
  return;
}

/* calendar source list popup menu items */

static void on_permissions_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  g_debug("** EEE ** on_permissions_cb: (source=%s)", e_source_peek_name(source));
}

static void on_subscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  g_debug("** EEE ** on_subscribe_cb: (source=%s)", e_source_peek_name(source));
}

static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  g_debug("** EEE ** on_unsubscribe_cb: (source=%s)", e_source_peek_name(source));
}

static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
  ECalPopupTargetSource* target = (ECalPopupTargetSource*)ep->target;
  ESource* source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
  ESourceGroup* group = e_source_peek_group(source);

  g_debug("** EEE ** on_delete_cb: This shouldn't happen! (source=%s)", e_source_peek_name(source));
}

static EPopupItem popup_items_shared_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.01", "Subscribe to shared calendar...", on_subscribe_cb, NULL, "stock_new-dir", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, 0xffff },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe this calendar", on_unsubscribe_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, 0xffff },
};

static EPopupItem popup_items_user_cal[] = {
  { E_POPUP_BAR,  "12.eee.00", NULL, NULL, NULL, NULL, 0, 0 },
  { E_POPUP_ITEM, "12.eee.01", "Subscribe to shared calendar...", on_subscribe_cb, NULL, "stock_new-dir", 0, E_CAL_POPUP_SOURCE_PRIMARY }, 
  { E_POPUP_ITEM, "12.eee.02", "Configure ACL...", on_permissions_cb, NULL, "stock_calendar", 0, E_CAL_POPUP_SOURCE_PRIMARY },
  { E_POPUP_ITEM, "12.eee.03", "Unsubscribe this calendar", on_unsubscribe_cb, NULL, "stock_delete", 0, 0xffff },
  { E_POPUP_BAR,  "12.eee.04", NULL, NULL, NULL, NULL, 0, 0 },
  //{ E_POPUP_ITEM, "20.delete", "_Delete", on_delete_cb, NULL, "stock_delete", 0, E_CAL_POPUP_SOURCE_USER|E_CAL_POPUP_SOURCE_PRIMARY },
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
  GSList* menus = NULL;
  guint i;

  if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
    return;

  EeeCalendar* cal = eee_accounts_manager_find_calendar_by_source(_mgr, source);
  if (cal == NULL)
  {
    g_debug("** EEE ** Can't get EeeCalendar for ESource. (%s)", e_source_peek_name(source));
    return;
  }

  if (cal->owner_account->accessible)
  {
    for (i = 0; i < G_N_ELEMENTS(popup_items_user_cal); i++)
      menus = g_slist_prepend(menus, &popup_items_user_cal[i]);
    e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
  }
  else
  {
    for (i = 0; i < G_N_ELEMENTS(popup_items_shared_cal); i++)
      menus = g_slist_prepend(menus, &popup_items_shared_cal[i]);
    e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
  }
}

/* watch evolution state (online/offline) */

void eee_calendar_state_changed(EPlugin *ep, ESEventTargetState *target)
{
  int online = target->state;
  if (online)
  {
  }
  else
  {
  }
}
