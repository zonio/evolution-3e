/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2010 Zonio s.r.o <developers@zonio.net>
 *
 * This file is part of evolution-3e.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libedataserverui/e-source-selector.h>
#include <calendar/gui/e-cal-config.h>
#include <shell/es-event.h>
#include <mail/em-config.h>
#include <libintl.h>

#if EVOLUTION_VERSION >= 230
#include <e-util/e-alert.h>
#include <e-util/e-alert-dialog.h>
#include <misc/e-popup-action.h>
#include <shell/e-shell-window.h>
#include <shell/e-shell-view.h>
#include <shell/e-shell-sidebar.h>

#else
#include <libgnomeui/libgnomeui.h>
#include <calendar/gui/e-cal-popup.h>
#include <mail/em-menu.h>
#include <e-util/e-error.h>
#endif /* EVOLUTION_VERSION >= 230 */

#define _(String) gettext(String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"
#include "utils.h"
#include "subscribe.h"
#include "acl.h"
#include "dns-txt-search.h"

#if EVOLUTION_VERSION >= 230
static void eee_calendar_state_changed(EShell *shell);
#endif /* EVOLUTION_VERSION >= 230 */

/* plugin intialization */

static EeeAccountsManager *mgr()
{
    static EeeAccountsManager *_mgr = NULL;

    if (_mgr == NULL)
    {
        _mgr = eee_accounts_manager_new();
    }
    return _mgr;
}

#if EVOLUTION_VERSION >= 230
int e_plugin_lib_enable(EPlugin *ep, int enable)
{
    EShell *shell = e_shell_get_default();

    if (shell) {
        g_signal_handlers_disconnect_by_func(shell, G_CALLBACK (eee_calendar_state_changed), NULL);
        if (enable)
            g_signal_connect(shell, "notify::online", G_CALLBACK (eee_calendar_state_changed), NULL);
    }

#else
int e_plugin_lib_enable(EPluginLib *ep, int enable)
{
#endif /* EVOLUTION_VERSION >= 230 */
    xr_init();
    g_type_class_ref(EEE_TYPE_ACCOUNT);
    g_type_class_ref(EEE_TYPE_ACCOUNTS_MANAGER);
    if (getenv("EEE_EVO_DEBUG"))
    {
        xr_debug_enabled = XR_DEBUG_CALL;
    }
    g_debug("** EEE ** Starting 3e Evolution Plugin %s", PACKAGE_VERSION);
    g_debug("** EEE ** Please report bugs to <%s>", PACKAGE_BUGREPORT);
    bindtextdomain(GETTEXT_PACKAGE, PROGRAMNAME_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    return 0;
}

#if EVOLUTION_VERSION >= 230
void eee_calendar_subscription(GtkAction *action, EShellView *shell_view);

static GtkActionEntry menuItems [] = {
    { "eee-calendar-subscribe",
      NULL,
      N_("Subscribe to 3e calendar..."),
      NULL,
      NULL,
      G_CALLBACK(eee_calendar_subscription) }
};

gboolean e_plugin_ui_init(GtkUIManager *ui_manager, EShellView *shell_view)
{
    EShellWindow *shell_window;
    GtkActionGroup *action_group;

    shell_window = e_shell_view_get_shell_window (shell_view);
    action_group = e_shell_window_get_action_group (shell_window, "calendar");

    gtk_action_group_add_actions (
        action_group, menuItems,
        G_N_ELEMENTS (menuItems), shell_view);

    return TRUE;
}
#endif /* EVOLUTION_VERSION >= 230 */

/* calendar add/properties dialog */

static GtkWidget *hidden = NULL;

static gboolean is_new_calendar_dialog(ESource *source)
{
    gboolean is_new = FALSE;
    const char *rel_uri = e_source_peek_relative_uri(source);

    // if ESource does not have relative_uri, dialog is for creating new calendar,
    // first time this function is called on given ESource, it's result must be
    // stored in ESource, because relative_uri may be modified and we still want
    // to determine if dialog is for new or existing calendar
    is_new = rel_uri == NULL || strlen(rel_uri) == 0 || g_object_get_data(G_OBJECT(source), "is_new");

    if (is_new)
    {
        g_object_set_data(G_OBJECT(source), "is_new", GUINT_TO_POINTER(1));
    }

    return is_new;
}

// handle offline notes list

static GSList *offline_labels = NULL;

#if EVOLUTION_VERSION >= 300
static void on_label_destroy(GtkWidget *object, gpointer data)
#else
static void on_label_destroy(GtkObject *object, gpointer data)
#endif /* EVOLUTION_VERSION >= 300 */
{
    offline_labels = g_slist_remove(offline_labels, object);
}

static void hide_offline_labels()
{
    g_slist_foreach(offline_labels, (GFunc)gtk_widget_hide, NULL);
}

static void show_offline_labels()
{
    g_slist_foreach(offline_labels, (GFunc)gtk_widget_show, NULL);
}

GtkWidget *eee_calendar_properties_factory(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    ECalConfigTargetSource *target = (ECalConfigTargetSource *)data->target;
    ESourceGroup *group = e_source_peek_group(target->source);
    EeeAccount *account;
    GtkWidget *label;
#if EVOLUTION_VERSION >= 300
    int row; 
    gtk_table_get_size(GTK_TABLE(data->parent), &row, NULL);
#else
    int row = GTK_TABLE(data->parent)->nrows;
#endif /* EVOLUTION_VERSION >= 300 */
    char *msg;

    //g_debug("** EEE ** Properties Dialog Items Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

    if (!hidden)
    {
        hidden = gtk_label_new("");
    }

    if (!e_source_group_is_3e(group))
    {
        return hidden;
    }

    account = eee_accounts_manager_find_account_by_group(mgr(), group);

    msg = g_markup_printf_escaped("<span weight=\"bold\" foreground=\"#ff0000\">%s</span>",
                                  _("Evolution is in offline mode. You cannot create or modify calendars now.\n"
                                    "Please switch to online mode for such operations."));
    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label), msg);
    g_free(msg);
    gtk_table_attach(GTK_TABLE(data->parent), label, 0, 2, row, row + 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
    offline_labels = g_slist_append(offline_labels, label);
    g_signal_connect(label, "destroy", G_CALLBACK(on_label_destroy), NULL);

    // can't do anything in offline mode
    if (!eee_plugin_online)
    {
        gtk_widget_show(label);
    }

    if (is_new_calendar_dialog(target->source))
    {
        target->disable_source_update = TRUE;
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            row++;
            msg = g_markup_printf_escaped("<span weight=\"bold\" foreground=\"#ff0000\">%s</span>",
                                          _("You cannot create calendars for this 3e account."));
            label = gtk_label_new("");
            gtk_label_set_markup(GTK_LABEL(label), msg);
            g_free(msg);
            gtk_table_attach(GTK_TABLE(data->parent), label, 0, 2, row, row + 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
            gtk_widget_show(label);
        }
    }

    return hidden;
}

gboolean eee_calendar_properties_check(EPlugin *epl, EConfigHookPageCheckData *data)
{
    ECalConfigTargetSource *target = (ECalConfigTargetSource *)data->target;
    ESourceGroup *group = e_source_peek_group(target->source);

    //g_debug("** EEE ** Properties Dialog Check Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

    if (!e_source_group_is_3e(group))
    {
        return TRUE;
    }
    if (!eee_plugin_online)
    {
        return FALSE;
    }

    if (is_new_calendar_dialog(target->source))
    {
        EeeAccount *account = eee_accounts_manager_find_account_by_group(mgr(), group);
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            return FALSE;
        }
    }
    else
    {
        EeeAccount *account = eee_accounts_manager_find_account_by_source(mgr(), target->source);
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            return FALSE;
        }
    }

    return TRUE;
}

void eee_calendar_properties_commit(EPlugin *epl, ECalConfigTargetSource *target)
{
    ESource *source = target->source;
    ESourceGroup *group = e_source_peek_group(source);
    const char *color = e_source_peek_color_spec(source);
    GdkColor parsed_color;
    char converted_color[COLOR_COMPONENT_SIZE * 3 * 2 + 2]; //3 components, 2 hex chars in byte, 2 additional chars (# and \0)

    if (!gdk_color_parse(color, &parsed_color))
    {
        g_warning("EEE: Unable to convert color \"%s\" from Evolution.", color);
        parsed_color.red = -1;
        parsed_color.green = 0;
        parsed_color.blue = 0;
    }
    parsed_color.red >>= (2 - COLOR_COMPONENT_SIZE) * 8; //GdkColor comonent is 2 byte integer, there are 8 bits in byte
    parsed_color.green >>= (2 - COLOR_COMPONENT_SIZE) * 8;
    parsed_color.blue >>= (2 - COLOR_COMPONENT_SIZE) * 8;
    snprintf(converted_color, COLOR_COMPONENT_SIZE * 3 * 2 + 2, "#%0*X%0*X%0*X",
             COLOR_COMPONENT_SIZE * 2, parsed_color.red, COLOR_COMPONENT_SIZE * 2, parsed_color.green, COLOR_COMPONENT_SIZE * 2, parsed_color.blue);
    converted_color[COLOR_COMPONENT_SIZE * 3 * 2 + 1] = '\0';

    //g_debug("** EEE ** Properties Dialog Commit Hook Call:\n\n%s\n\n", e_source_to_standalone_xml(target->source));

    if (!e_source_group_is_3e(group))
    {
        return;
    }
    if (!eee_plugin_online)
    {
        return;
    }

    if (is_new_calendar_dialog(source))
    {
        char *calname = NULL;
        EeeAccount *account = eee_accounts_manager_find_account_by_group(mgr(), group);
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            return;
        }

        if (eee_account_create_new_calendar(account, &calname))
        {
            eee_account_update_calendar_settings(account, account->name, calname, e_source_peek_name(source), converted_color);
        }
        eee_account_disconnect(account);

        e_source_set_3e_properties(source, calname, account->name, account, "write", NULL, 0); // title and color are already set
        eee_accounts_manager_add_source(mgr(), account->name, g_object_ref(source));
        g_free(calname);
    }
    else
    {
        EeeAccount *account = eee_accounts_manager_find_account_by_source(mgr(), source);
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            return;
        }

        const char *calname = e_source_get_property(source, "eee-calname");
        const char *owner = e_source_get_property(source, "eee-owner");
        eee_account_update_calendar_settings(account, owner, calname, e_source_peek_name(source), converted_color);
        eee_account_disconnect(account);
    }

    eee_accounts_manager_restart_sync(mgr());
}

/* calendar source list popup menu items */

#if EVOLUTION_VERSION >= 230
static void on_permissions_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);
#else
static void on_permissions_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
#endif /* EVOLUTION_VERSION >= 230 */
    ESourceGroup *group = e_source_peek_group(source);
    EeeAccount *account;

    if (!eee_plugin_online)
    {
        return;
    }

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    acl_gui_create(mgr(), account, source);
}

#if EVOLUTION_VERSION >= 230
static void on_unsubscribe_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);
#else
static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
#endif /* EVOLUTION_VERSION >= 230 */
    ESourceGroup *group = e_source_peek_group(source);
    const char *owner = e_source_get_property(source, "eee-owner");
    const char *calname = e_source_get_property(source, "eee-calname");
    EeeAccount *account;
    GError *err = NULL;

    if (!eee_plugin_online)
    {
        return;
    }
    if (e_source_is_3e_owned_calendar(source))
    {
        return;
    }

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (eee_account_unsubscribe_calendar(account, owner, calname))
    {
        // get ECal and remove calendar from the server
        ECal *ecal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
        if (!e_cal_remove(ecal, &err))
        {
            g_warning("** EEE ** ECal remove failed (%d:%s)", err->code, err->message);
            g_clear_error(&err);
        }
        g_object_unref(ecal);

        e_source_group_remove_source(group, source);
    }
    eee_account_disconnect(account);
    eee_accounts_manager_restart_sync(mgr());
}

#if EVOLUTION_VERSION >= 230
static void on_delete_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);

    if (e_alert_run_dialog_for_args(GTK_WINDOW(shell_view),
                    "calendar:prompt-delete-calendar", e_source_peek_name(source), NULL) != GTK_RESPONSE_YES)
#else
static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));

    if (e_error_run((GtkWindow *)gtk_widget_get_toplevel(ep->target->widget),
                        "calendar:prompt-delete-calendar",
                        e_source_peek_name(source), NULL) != GTK_RESPONSE_YES)
#endif /* EVOLUTION_VERSION >= 230 */
    {
        return;
    }

    ESourceGroup *group = e_source_peek_group(source);
    char *calname = (char *)e_source_get_property(source, "eee-calname");
    EeeAccount *account;
    GError *err = NULL;

    if (!eee_plugin_online)
    {
        return;
    }
    if (!e_source_is_3e_owned_calendar(source))
    {
        return;
    }

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (eee_account_delete_calendar(account, calname))
    {
        // get ECal and remove calendar from the server
        ECal *ecal = e_cal_new(source, E_CAL_SOURCE_TYPE_EVENT);
        if (!e_cal_remove(ecal, &err))
        {
            g_warning("** EEE ** ECal remove failed (%d:%s)", err->code, err->message);
            g_clear_error(&err);
        }
        g_object_unref(ecal);

        e_source_group_remove_source(group, source);
    }
    eee_account_disconnect(account);
    eee_accounts_manager_restart_sync(mgr());
}

#if EVOLUTION_VERSION >= 230
static GtkActionEntry calendar_entries[] = {
    { "eee-permissions",
      "stock_shared-by-me",
      N_("Setup Permissions..."),
      NULL,
      NULL,
      G_CALLBACK(on_permissions_cb) },

    { "eee-unsubscribe",
      "remove",
      N_("Unsubscribe"),
      NULL,
      NULL,
      G_CALLBACK(on_unsubscribe_cb) },
    
    { "calendar-delete",
      GTK_STOCK_DELETE,
      N_("_Delete"),
      NULL,
      NULL,
      G_CALLBACK(on_delete_cb) }
};

static EPopupActionEntry popup_items_shared_cal [] = {
    { "eee-popup-unsubscribe", NULL, "eee-unsubscribe" },
    { "calendar-popup-delete", NULL, "calendar-delete" }
};

static EPopupActionEntry popup_items_user_cal [] = {
    { "eee-popup-permissions", NULL, "eee-permissions" },
    { "calendar-popup-delete", NULL, "calendar-delete" }
};

static EPopupActionEntry popup_items_cal_offline [] = {
    // TODO disable Properties
};

#else
static EPopupItem popup_items_shared_cal[] = {
    { E_POPUP_BAR,  "12.eee.00",  NULL,                       NULL,
NULL, NULL,          0, 0                          },
    { E_POPUP_ITEM, "12.eee.03",  N_("Unsubscribe"), on_unsubscribe_cb, NULL,
"remove",      0, E_CAL_POPUP_SOURCE_PRIMARY },
    { E_POPUP_BAR,  "12.eee.04",  NULL,          NULL,        NULL, NULL,
0, 0                          },
    { E_POPUP_ITEM, "20.delete",  N_("_Delete"),     on_delete_cb,      NULL,
"edit-delete", 0, 0xffff                     },
};

static EPopupItem popup_items_user_cal[] = {
    { E_POPUP_BAR,  "12.eee.00",  NULL,                                NULL,
NULL, NULL,                 0, 0
},
    { E_POPUP_ITEM, "12.eee.02",  N_("Setup Permissions..."), on_permissions_cb,
NULL, "stock_shared-by-me", 0, E_CAL_POPUP_SOURCE_PRIMARY
},
    { E_POPUP_BAR,  "12.eee.04",  NULL,               NULL,            NULL,
NULL,                 0, 0                                                    },
    { E_POPUP_ITEM, "20.delete",  N_("_Delete"),              on_delete_cb,
NULL, "edit-delete",        0, E_CAL_POPUP_SOURCE_USER |
E_CAL_POPUP_SOURCE_PRIMARY },
};

static EPopupItem popup_items_cal_offline[] = {
    { E_POPUP_BAR,  "12.eee.00",     NULL,               NULL,
NULL, NULL,                      0, 0      },
    { E_POPUP_ITEM, "12.eee.02",     N_("Setup Permissions..."),
on_permissions_cb, NULL, "stock_shared-by-me",      0, 0xffff },
    { E_POPUP_ITEM, "12.eee.03",     N_("Unsubscribe"),      on_unsubscribe_cb,
NULL, "remove",                  0, 0xffff },
    { E_POPUP_BAR,  "12.eee.04",     NULL,               NULL,
NULL, NULL,                      0, 0      },
    { E_POPUP_ITEM, "20.delete",     N_("_Delete"),          on_delete_cb,
NULL, "edit-delete",             0, 0xffff },
#if EVOLUTION_VERSION < 222
    { E_POPUP_ITEM, "30.properties", N_("_Properties..."),   NULL,
NULL, "stock_folder-properties", 0, 0xffff },
#else
    { E_POPUP_ITEM, "99.properties", N_("_Properties..."),   NULL,
NULL, "document-properties",     0, 0xffff },
#endif
};

static void popup_free(EPopup *ep, GSList *items, void *data)
{
    g_slist_free(items);
}
#endif /* EVOLUTION_VERSION >= 230 */

#if EVOLUTION_VERSION >= 230
void eee_calendar_popup_source_factory(EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);
#else
void eee_calendar_popup_source_factory(EPlugin *ep, ECalPopupTargetSource *target)
{
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
#endif /* EVOLUTION_VERSION >= 230 */
    ESourceGroup *group = e_source_peek_group(source);
    int items_count;
#if EVOLUTION_VERSION >= 230
    EPopupActionEntry *items;
#else
    EPopupItem *items;
    GSList *menus = NULL;
#endif /* EVOLUTION_VERSION >= 230 */
    EeeAccount *account;
    int i;

    if (!e_source_is_3e(source))
    {
        return;
    }

    if (eee_plugin_online)
    {
        account = eee_accounts_manager_find_account_by_source(mgr(), source);
        if (account == NULL || account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            goto offline_mode;
        }

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

#if EVOLUTION_VERSION >= 230
    EShellWindow *shell_window = e_shell_view_get_shell_window(shell_view);
    GtkActionGroup *action_group = e_shell_window_get_action_group(shell_window, "calendar");
    e_action_group_add_popup_actions(action_group, items,
                                     G_N_ELEMENTS(items));
#else
    for (i = 0; i < items_count; i++)
    {
        menus = g_slist_prepend(menus, items + i);
    }

    e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
#endif /* EVOLUTION_VERSION >= 230 */
}

/* watch evolution state (online/offline) */

gboolean eee_plugin_online = FALSE;

#if EVOLUTION_VERSION >= 230
static void eee_calendar_state_changed(EShell *shell)
{
    int online = e_shell_get_online(shell);
#else
void eee_calendar_state_changed(EPlugin *ep, ESEventTargetState *target)
{
    int online = !!target->state;
#endif /* EVOLUTION_VERSION >= 230 */
    g_debug("** EEE ** State changed to: %s", online ? "online" : "offline");

    eee_plugin_online = online;
    if (online)
    {
        eee_accounts_manager_restart_sync(mgr());
        hide_offline_labels();
    }
    else
    {
        eee_accounts_manager_pause_sync(mgr());
        show_offline_labels();
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
    if (strstr(target->id, "OAFIID:GNOME_Evolution_Calendar_Component") == NULL)
    {
        return;
    }

    g_idle_add(activation_cb, NULL);
}

/* calendar subscription menu item callback */

#if EVOLUTION_VERSION >= 230
void eee_calendar_subscription(GtkAction *action, EShellView *shell_view)
#else
void eee_calendar_subscription(EPlugin *ep, EMMenuTargetSelect *target)
#endif /* EVOLUTION_VERSION >= 230 */
{
    if (!eee_plugin_online)
    {
#if EVOLUTION_VERSION >= 230
        e_alert_run_dialog_for_args(NULL, "eee:subscribe-offline", NULL);
#else
        e_error_run(NULL, "eee:subscribe-offline", NULL);
#endif /* EVOLUTION_VERSION >= 230 */
    }
    else
    {
        subscribe_gui_create(mgr());
    }
}

/* mail account 3E configuration */

static GtkWidget *add_section(GtkWidget *panel, const char *title)
{
    GtkWidget *vbox, *hbox, *label;
    char *markup_title = g_markup_printf_escaped("<b>%s</b>", title);

    gtk_box_pack_start(GTK_BOX(panel), label = gtk_label_new(markup_title), FALSE, FALSE, 0);
    g_free(markup_title);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(panel), hbox = gtk_hbox_new(FALSE, 12), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(""), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox = gtk_vbox_new(FALSE, 12), FALSE, FALSE, 0);

    return vbox;
}

void status_changed(GtkToggleButton *button, const char *name)
{
    if (gtk_toggle_button_get_active(button))
    {
        eee_accounts_manager_enable_account(mgr(), name);
    }
    else
    {
        eee_accounts_manager_disable_account(mgr(), name);
    }
    eee_accounts_manager_restart_sync(mgr());
}

GtkWidget *eee_account_properties_page(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    EMConfigTargetAccount *target = (EMConfigTargetAccount *)data->config->target;
    const char *name = e_account_get_string(target->account, E_ACCOUNT_ID_ADDRESS);
    GtkWidget *panel, *section, *checkbutton_status, *label;

    if (data->old)
    {
        return data->old;
    }

    // toplevel vbox contains frames that group 3E account settings into various
    // groups
    panel = gtk_vbox_new(FALSE, 12);
    gtk_container_set_border_width(GTK_CONTAINER(panel), 12);

    // Status group
    section = add_section(panel, _("3e Account Status"));
    char *note = g_strdup_printf(_("If you have 3e account <i>%s</i>, you can turn it on/off here."), name);
    label = (GtkWidget *)gtk_object_new(GTK_TYPE_LABEL,
                                        "label", note,
                                        "use-markup", TRUE,
                                        "justify", GTK_JUSTIFY_LEFT,
                                        "xalign", 0,
                                        "yalign", 0.5,
                                        NULL);
    g_free(note);
    gtk_box_pack_start(GTK_BOX(section), label, FALSE, FALSE, 0);
    checkbutton_status = gtk_check_button_new_with_label(_("Enable 3e Account"));
    gtk_widget_set_can_focus (checkbutton_status, FALSE);
    gtk_box_pack_start(GTK_BOX(section), checkbutton_status, FALSE, FALSE, 0);

    //XXX: update button based on live account status
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton_status), !eee_accounts_manager_account_is_disabled(mgr(), name));
    g_signal_connect(checkbutton_status, "toggled", G_CALLBACK(status_changed), (gpointer)name); // <<< this should be ok

    gtk_widget_show_all(panel);
    gtk_notebook_insert_page(GTK_NOTEBOOK(data->parent), panel, gtk_label_new(_("3e Settings")), 4);

    return panel;
}

gboolean eee_account_properties_check(EPlugin *epl, EConfigHookPageCheckData *data)
{
    EMConfigTargetAccount *target = (EMConfigTargetAccount *)data->config->target;
    const char *name = e_account_get_string(target->account, E_ACCOUNT_ID_ADDRESS);
    int status = TRUE;

    if (data->pageid == NULL || !strcmp(data->pageid, "40.eee"))
    {
    }

    return status;
}

void eee_account_properties_commit(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    EMConfigTargetAccount *target = (EMConfigTargetAccount *)data->config->target;
    const char *name = e_account_get_string(target->account, E_ACCOUNT_ID_ADDRESS);
}

gboolean wizard_eee_account_activated = TRUE;
gboolean dns_resolv_successful = FALSE;
gboolean searched = FALSE;

GtkWidget *checkbutton_status;  
GtkLabel *lbl;
gchar *prev_name = NULL;

void wizard_chb_status_changed(GtkToggleButton* button, const char* name)
{
    if (gtk_toggle_button_get_active(button))
        wizard_eee_account_activated = TRUE;
    else
        wizard_eee_account_activated = FALSE;

    g_debug("** EEE **: Checkbox state changed to %s.",
            wizard_eee_account_activated ? "CHECKED" : "EMPTY");
}

GtkWidget* eee_account_wizard_page(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    GtkWidget *page, *panel, *section, *label;

    char *title = _("3e calendar");

    if (data->old)
        return data->old;

    page = gtk_vbox_new (FALSE, 12);
    gtk_container_set_border_width (GTK_CONTAINER (page), 12);
    // toplevel vbox contains frames that group 3E account settings into various
    // groups

    // Status group
    // *** section = add_section(page, _("Enable 3e calendar"));

    label = (GtkWidget*)gtk_object_new(GTK_TYPE_LABEL, 
             "label", "", 
             "use-markup", TRUE,
             "justify", GTK_JUSTIFY_LEFT, 
             "xalign", 0, 
             "yalign", 0.5, 
             NULL); 
    lbl = (GtkLabel*)label;

    // *** section removed, place it back = replace page by section

    gtk_box_pack_start(GTK_BOX(page), label, FALSE, FALSE, 0);

    checkbutton_status = gtk_check_button_new_with_label(_("Enable 3e calendar"));
    gtk_box_pack_start(GTK_BOX(page), checkbutton_status, FALSE, FALSE, 0);
    // ***

    g_signal_connect(checkbutton_status, "toggled", G_CALLBACK(wizard_chb_status_changed), (gpointer)title);

    gtk_widget_show_all(page);

#if EVOLUTION_VERSION >= 230
    gtk_assistant_append_page(GTK_ASSISTANT(data->parent), page);
    gtk_assistant_set_page_title (GTK_ASSISTANT(data->parent), page, title);
    gtk_assistant_set_page_type (GTK_ASSISTANT(data->parent), page, GTK_ASSISTANT_PAGE_CONTENT);	

    return GTK_WIDGET(page);
#else
    GtkWidget *page_container = gnome_druid_page_standard_new();
    gnome_druid_page_standard_set_title(GNOME_DRUID_PAGE_STANDARD(page_container), title);

    gtk_container_add(GTK_CONTAINER(GNOME_DRUID_PAGE_STANDARD(page_container)->vbox), page);
    gnome_druid_append_page(GNOME_DRUID(data->parent), GNOME_DRUID_PAGE(page_container));

    return GTK_WIDGET(page_container);
#endif
}

gboolean eee_account_wizard_check(EPlugin *epl, EConfigHookPageCheckData *data)
{
    EMConfigTargetAccount* target = (EMConfigTargetAccount*)data->config->target;
    const char* name = e_account_get_string(target->account, E_ACCOUNT_ID_ADDRESS);
    char *eee_host = NULL;

    if (name == NULL)
        return TRUE;

    if (g_strcmp0(name, prev_name) != 0)
    {
        searched = FALSE;
        prev_name = g_strdup(name);
        return TRUE;
    }

    if (searched == TRUE)
        return TRUE;

    g_debug("** EEE **: Wizard check: E-mail: %s", name);

    eee_host = get_eee_server_hostname(name);
    searched = TRUE;

    if (eee_host != NULL)
    {
        dns_resolv_successful = TRUE;
        gtk_label_set_text(lbl, g_strdup_printf(_("3e calendar server has been found for your domain. You can\n"
                                                  "enable 3e calendar for your account:\n"
                                                  "<i>%s</i>\n"
                                                  "If you are not sure you have 3e calendar, ask your system\n"
                                                  "administrator or provider of your email service. Go to email\n"
                                                  "account preferences to change this setting later."), name));
        gtk_label_set_use_markup(lbl, TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton_status), TRUE);
        gtk_widget_set_sensitive(checkbutton_status, TRUE);
    }
    else
    {
        dns_resolv_successful = FALSE;
        gtk_label_set_text(lbl, g_strdup_printf(_("No 3e calendar server has been found for your domain. Ask\n"
                                                  "your system administrator or provider of your email service\n"
                                                  "for more about 3e calendar. Go to email account preferences\n"
                                                  "to change this setting later.")));
        gtk_label_set_use_markup(lbl, TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton_status), FALSE);
        gtk_widget_set_sensitive(checkbutton_status, FALSE);
    }

    return TRUE;
}

void eee_account_wizard_commit(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    EMConfigTargetAccount* target = (EMConfigTargetAccount*)data->config->target;
    const char* name = e_account_get_string(target->account, E_ACCOUNT_ID_ADDRESS);
        if ((wizard_eee_account_activated == TRUE) && (dns_resolv_successful == TRUE)) 
            eee_accounts_manager_enable_account(mgr(), name);
        else
            eee_accounts_manager_disable_account(mgr(), name);

    eee_accounts_manager_restart_sync(mgr());

    g_debug("** EEE **: Wizard commit for e-mail %s. 3e account is %s.",
            name, wizard_eee_account_activated ? "activated" : "disabled");
}
