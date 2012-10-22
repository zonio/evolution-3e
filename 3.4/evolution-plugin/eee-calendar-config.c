/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2012 Zonio s.r.o <developers@zonio.net>
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

#include <string.h>

#include <libedataserverui/e-source-selector.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-calendar-selector.h>
#include <shell/es-event.h>
#include <mail/em-config.h>
#include <libintl.h>

#include <libecal/e-cal-client.h>
#include <libevolution-utils/e-alert.h>
#include <libevolution-utils/e-alert-dialog.h>
#include <misc/e-popup-action.h>
#include <shell/e-shell-window.h>
#include <shell/e-shell-view.h>
#include <shell/e-shell-sidebar.h>

#define _(String) gettext(String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"
#include "utils.h"
#include "subscribe.h"
#include "acl.h"
#include "dns-txt-search.h"

static void eee_calendar_state_changed(EShell *shell);

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

int e_plugin_lib_enable(EPlugin *ep, int enable)
{
    EShell *shell = e_shell_get_default();

    if (shell) {
        g_signal_handlers_disconnect_by_func(shell, G_CALLBACK (eee_calendar_state_changed), NULL);
        if (enable)
            g_signal_connect(shell, "notify::online", G_CALLBACK (eee_calendar_state_changed), NULL);
    }

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

void eee_calendar_subscription(GtkAction *action, EShellView *shell_view);

static GtkActionEntry menuItems [] = {
    { "eee-calendar-subscribe",
      NULL,
      N_("Subscribe to 3e calendar"),
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

static void on_label_destroy(GtkWidget *object, gpointer data)
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

static void calendar_properties_dialog_polish(EConfigHookItemFactoryData *data)
{
    GtkTable *table = GTK_TABLE(data->parent);
    GtkFrame *frame = gtk_widget_get_parent(
        GTK_WIDGET(gtk_widget_get_parent(GTK_WIDGET(table))));

    gtk_frame_set_label(frame, "General");

    GSList *children = g_slist_reverse(gtk_container_get_children(GTK_CONTAINER(table)));
    gtk_widget_hide(GTK_WIDGET(children->data));
    gtk_widget_hide(GTK_WIDGET(children->next->data));

    // XXX: Changing width of colorButton is not so easy, will see
    //GSList *iter;
    //for (iter = children; iter; iter = iter->next)
    //{
    //    gchar *name = gtk_widget_get_name(GTK_WIDGET(iter->data));
    //    if (g_strcmp0(name, "GtkColorButton") == 0 )
    //    {
    //        gint x, y;
    //        gtk_widget_get_size_request(GTK_WIDGET(iter->data), &x, &y);
    //        printf("\tsize: %dx%d\n", x, y);
    //        gtk_widget_set_size_request(GTK_WIDGET(iter->data), 20, -1);
    //        gtk_widget_get_size_request(GTK_WIDGET(iter->data), &x, &y);
    //        printf("\tsize: %dx%d\n", x, y);
    //    }
    //}

    g_slist_free(children);
}

GtkWidget *eee_calendar_properties_factory(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    ECalConfigTargetSource *target = (ECalConfigTargetSource *)data->target;
    ESourceGroup *group = e_source_peek_group(target->source);
    EeeAccount *account;
    GtkWidget *perms;
    guint row;

    if (!e_plugin_util_is_group_proto (group, "eee"))
        return NULL;

    if (is_new_calendar_dialog (target->source))
        return NULL;

    g_object_get (data->parent, "n-rows", &row, NULL);

    account = eee_accounts_manager_find_account_by_group (mgr(), group);

    calendar_properties_dialog_polish(data);
    struct acl_context * ctx = acl_gui_create (mgr(), account, target->source);
    g_object_set_data (G_OBJECT (target->source), "eee-acl-context", ctx);
    gtk_widget_show (ctx->win);
    gtk_table_attach (GTK_TABLE (data->parent), ctx->win, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    return ctx->win;

/*
    GtkWidget *label;
    guint row; 

    gtk_table_get_size(GTK_TABLE(data->parent), &row, NULL);

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

    return hidden;*/
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

        struct acl_context * ctx = g_object_get_data (G_OBJECT (target->source), "eee-acl-context");
        store_acl (ctx);
        acl_gui_destroy ();
    }

    eee_accounts_manager_restart_sync(mgr());
}

static void
display_error_message (GtkWidget *parent,
                       const gchar *message)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (GTK_WINDOW (parent), 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}


/* calendar source list popup menu items */

static void on_permissions_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
    ESourceSelector *selector;
    g_object_get (shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection (selector);
    ESourceGroup *group = e_source_peek_group (source);
    EeeAccount *account;

    if (!e_source_is_3e (source))
      {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is available only for 3e calendars."));
        g_object_unref (selector);
        return;
      }

    if (!eee_plugin_online)
    {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is not available with 3e plugin in offline mode."));
        g_object_unref (selector);
        return;
    }

    g_object_unref (selector);

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    acl_gui_create(mgr(), account, source);

}

static void on_unsubscribe_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);

    if (!e_source_is_3e (source))
      {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is available only for 3e calendars."));
        g_object_unref (selector);
        return;
      }

    if (!eee_plugin_online)
    {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is not available with 3e plugin in offline mode."));
        g_object_unref (selector);
        return;
    }

    g_object_unref (selector);

    ESourceGroup *group = e_source_peek_group(source);
    const char *owner = e_source_get_property(source, "eee-owner");
    const char *calname = e_source_get_property(source, "eee-calname");

    EeeAccount *account;
    GError *err = NULL;

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (eee_account_unsubscribe_calendar(account, owner, calname))
    {
        // get ECal and remove calendar from the server
        ECalClient *ecal = e_cal_client_new(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, &err);
        if (!e_client_remove_sync((EClient *)ecal, NULL, &err))
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

static void on_delete_cb(GtkAction *action, EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);

    if (e_alert_run_dialog_for_args(GTK_WINDOW(shell_view),
                    "calendar:prompt-delete-calendar", e_source_peek_name(source), NULL) != GTK_RESPONSE_YES)
    {
        g_object_unref (selector);
        return;
    }

    ESourceGroup *group = e_source_peek_group(source);
    char *calname = (char *)e_source_get_property(source, "eee-calname");
    EeeAccount *account;
    GError *err = NULL;

    if (!eee_plugin_online)
    {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is not available with 3e plugin in offline mode."));
        g_object_unref (selector);
        return;
    }

    if (!e_source_is_3e_owned_calendar(source))
    {
        display_error_message (gtk_widget_get_toplevel (GTK_WIDGET (selector)), _("This action is available only for 3e calendars."));
        g_object_unref (selector);
        return;
    }

    g_object_unref (selector);

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    if (eee_account_delete_calendar(account, calname))
    {
        // get ECal and remove calendar from the server
        ECalClient *ecal = e_cal_client_new(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, &err);
        if (!e_client_remove_sync((EClient *)ecal, NULL, &err))
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

gboolean
calendar_actions_init (GtkUIManager *ui_manager, EShellView *shell_view)
{
  EShellWindow *shell_window;
  GtkActionGroup *action_group;
  GtkAction *action;

  shell_window = e_shell_view_get_shell_window (shell_view);

  action_group = e_shell_window_get_action_group (shell_window, "calendar");

/*  action = gtk_action_new ("calendar-permissions", _("Setup permissions"), _("Setup 3e calendar permissions"), "stock_shared-by-me");
  gtk_action_group_add_action (action_group, action);

  g_signal_connect (
    action, "activate",
    G_CALLBACK (on_permissions_cb), shell_view);

  g_object_unref (action);*/

  action = gtk_action_new ("calendar-unsubscribe", _("Unsubscribe"), _("Unsubscribe a previously subscribed 3e calendar"), "remove");
  gtk_action_group_add_action (action_group, action);

  g_signal_connect (
    action, "activate",
    G_CALLBACK (on_unsubscribe_cb), shell_view);

  g_object_unref (action);

  action = gtk_action_new ("calendar-delete-3e", _("Delete from server"), _("Delete calendar from 3e server"), GTK_STOCK_DELETE);
  gtk_action_group_add_action (action_group, action);

  g_signal_connect (
    action, "activate",
    G_CALLBACK (on_delete_cb), shell_view);

  g_object_unref (action);

  return TRUE;
}

/*
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


static void eee_calendar_popup_source_factory(EShellView *shell_view)
{
    EShellSidebar *shell_sidebar = e_shell_view_get_shell_sidebar(shell_view);
    ESourceSelector *selector;
    g_object_get(shell_sidebar, "selector", &selector, NULL);
    ESource *source = e_source_selector_peek_primary_selection(selector);
    ESourceGroup *group = e_source_peek_group(source);
    int items_count;
    EPopupActionEntry *items;
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

    EShellWindow *shell_window = e_shell_view_get_shell_window(shell_view);
    GtkActionGroup *action_group = e_shell_window_get_action_group(shell_window, "calendar");
    e_action_group_add_popup_actions(action_group, items,
                                     G_N_ELEMENTS(items));
}*/

/* watch evolution state (online/offline) */

gboolean eee_plugin_online = FALSE;

static void eee_calendar_state_changed(EShell *shell)
{
    int online = e_shell_get_online(shell);

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
/* TODO: Fix or replace this method
void eee_calendar_component_activated(EPlugin *ep, ESEventTargetComponent *target)
{
    if (strstr(target->id, "OAFIID:GNOME_Evolution_Calendar_Component") == NULL)
    {
        return;
    }

    g_idle_add(activation_cb, NULL);
}
*/
/* calendar subscription menu item callback */

void eee_calendar_subscription(GtkAction *action, EShellView *shell_view)
{
    if (!eee_plugin_online)
    {
        e_alert_run_dialog_for_args(NULL, "eee:subscribe-offline", NULL);
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
    gtk_box_pack_start(GTK_BOX(hbox), vbox = gtk_vbox_new(FALSE, 12), FALSE, TRUE, 0);

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

static inline guint8
eee_color_16to8 (guint16 color)
{
  /* this way we get the same result as round (color * 0xff / 0xffff) */
  return (guint8) (0xff * ((guint32) color + 0x80) / 0xffff);
}

/* color has to be at least 8 bytes long */
/*static void
eee_color_string_16to8 (const gchar * from, gchar * to)
{
  GdkColor c;

  if (!gdk_color_parse (from, &c))
    {
      strcpy (to, "#ffffff");
      return;
    }

  c.red = eee_color_16to8 (c.red);
  c.green = eee_color_16to8 (c.green);
  c.blue = eee_color_16to8 (c.blue);
  sprintf (to, "#%02x%02x%02x", c.red, c.green, c.blue);
}*/

/*static void
copy_calendars_clicked (GtkButton * button, const gchar * name)
{
  ESourceSelector * selector = g_object_get_data (G_OBJECT (button), "calendar-selector");
  ESourceGroup * group = g_object_get_data (G_OBJECT (button), "source-group");
  GSList * selection, * iter;
  EeeAccount * account = eee_accounts_manager_find_account_by_name (mgr(), name);

  g_return_if_fail (account != NULL);

  selection = e_source_selector_get_selection (selector);
  for (iter = selection; iter != NULL; iter = iter->next)
    {
      gchar * calname, color[8], * relative_uri;
      ESource * old, * new;
      
      old = E_SOURCE (iter->data);

      /* how to handle if failed? for now just continue */
/*      if (!eee_account_create_new_calendar (account, &calname))
	continue;

      eee_color_string_16to8 (e_source_peek_color_spec (old), color);

      eee_account_update_calendar_settings (account, account->name, calname, e_source_peek_name (old), color);
      eee_account_disconnect (account);

      new = e_source_new_3e (calname, name, account, "write", e_source_peek_name (old), color);
      e_source_group_add_source (group, new, -1);

      copy_source (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))), old, new, E_CAL_CLIENT_SOURCE_TYPE_EVENTS);

      g_object_unref (new);
    }
  e_source_selector_free_selection (iter);
}*/

GtkWidget *
eee_account_properties_page (EPlugin * epl, EConfigHookItemFactoryData * data)
{
  GtkBuilder * builder;
  GtkWidget * settings/*, * selector*/;
  GtkLabel * settings_desc;
  GtkToggleButton * enable_button;
/*  GtkContainer * scrolled_window;
  GtkButton * copy_button;*/
  const gchar * name = ((EMConfigTargetSettings *) data->config->target)->email_address;
  gchar * desc;
  ESourceList * sourcelist;
  ESourceGroup * group;
  GSList * groups, * iter;

  if (data->old)
    return data->old;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, PLUGINDIR "/org-gnome-evolution-eee.glade", NULL);

  settings = GTK_WIDGET (g_object_ref (gtk_builder_get_object (builder, "account-3e-settings")));
  settings_desc = GTK_LABEL (gtk_builder_get_object (builder, "account-3e-settings_description-label"));
  enable_button = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "account-3e-settings-enable-button"));
/*  scrolled_window = GTK_CONTAINER (gtk_builder_get_object (builder, "copy-calendars-scrolledwindow"));
  copy_button = GTK_BUTTON (gtk_builder_get_object (builder, "copy-calendars-copy-button"));*/

  desc = g_strdup_printf (_("If you have 3e account <i>%s</i>, you can turn it on/off here."), name);
  gtk_label_set_markup (settings_desc, desc);
  g_free (desc);

  gtk_toggle_button_set_active (enable_button, !eee_accounts_manager_account_is_disabled (mgr (), name));
  g_signal_connect (enable_button, "toggled", G_CALLBACK (status_changed), (gpointer) name);
 
/*  e_cal_client_get_sources (&sourcelist, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);*/

  /* we want calendars only from other accounts (or local), so we are removing
     group of this account  */
/*  groups = e_source_list_peek_groups (sourcelist);
  for (iter = groups; iter != NULL; iter = iter->next)
    {
      group = E_SOURCE_GROUP (iter->data);
      const gchar * groupname = e_source_group_peek_3e_name (group);
    
      if (groupname == NULL)
	continue;

      if (!strcmp (name, groupname))
	{
	  e_source_list_remove_group (sourcelist, group);
	  break;
	}
    }

  selector = e_calendar_selector_new (sourcelist);
  g_object_unref (sourcelist);

  gtk_container_add (scrolled_window, selector);
  gtk_widget_show (selector);

  g_object_set_data (G_OBJECT (copy_button), "source-group", group);
  g_object_set_data (G_OBJECT (copy_button), "calendar-selector", selector);
  g_signal_connect (copy_button, "clicked", G_CALLBACK (copy_calendars_clicked), (gpointer) name);*/

  g_object_unref (builder);

  gtk_notebook_insert_page(GTK_NOTEBOOK(data->parent), settings, gtk_label_new(_("3e Settings")), 4);

  return settings;
}

gboolean eee_account_properties_check(EPlugin *epl, EConfigHookPageCheckData *data)
{
    const char *name = ((EMConfigTargetSettings *) data->config->target)->email_address;
    int status = TRUE;

    if (data->pageid == NULL || !g_strcmp0(data->pageid, "40.eee"))
    {
    }

    return status;
}

void eee_account_properties_commit(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    const char *name = ((EMConfigTargetSettings *) data->config->target)->email_address;
}

gboolean wizard_eee_account_activated = TRUE;
gboolean dns_resolv_successful = FALSE;
GtkAssistant *assistant = NULL;
GtkLabel *lbl = NULL;

void wizard_chb_status_changed(GtkToggleButton* button, const char* name)
{
    if (gtk_toggle_button_get_active(button))
        wizard_eee_account_activated = TRUE;
    else
        wizard_eee_account_activated = FALSE;

    g_debug("** EEE **: Checkbox state changed to %s.",
            wizard_eee_account_activated ? "CHECKED" : "EMPTY");
}
/*
static gint skip_3e_page(gint current_page, gpointer data)
{
    switch (current_page)
    {
        case 5:
            return 7;
        default:
            return current_page + 1;
    }
}*/

GtkWidget* eee_account_wizard_page(EPlugin *epl, EConfigHookItemFactoryData *data)
{
    GtkWidget *page, *panel, *section, *checkbutton_status, *label;

    char *title = _("3e Calendar Account");
    assistant = GTK_ASSISTANT(data->parent);

    if (data->old)
        return data->old;

    page = gtk_vbox_new (FALSE, 12);
    gtk_container_set_border_width (GTK_CONTAINER (page), 12);
    // toplevel vbox contains frames that group 3E account settings into various
    // groups

    // Status group
    section = add_section(page, _("Enable 3e calendar account"));

    label = gtk_label_new (NULL);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

    lbl = GTK_LABEL (label);

    gtk_box_pack_start(GTK_BOX(section), label, FALSE, FALSE, 0);

    checkbutton_status = gtk_check_button_new_with_label(_("Enable 3e calendar account"));
    gtk_widget_set_can_focus (checkbutton_status, FALSE);
    gtk_box_pack_start(GTK_BOX(section), checkbutton_status, FALSE, FALSE, 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton_status), TRUE);
    g_signal_connect(checkbutton_status, "toggled", G_CALLBACK(wizard_chb_status_changed), (gpointer)title);

    gtk_widget_show_all(page);

    gtk_assistant_append_page(GTK_ASSISTANT(data->parent), page);
    gtk_assistant_set_page_title (GTK_ASSISTANT(data->parent), page, title);
    gtk_assistant_set_page_type (GTK_ASSISTANT(data->parent), page, GTK_ASSISTANT_PAGE_CONTENT);	

//    g_object_set_data((GObject *)data->parent, "restore", GINT_TO_POINTER(FALSE));
    
    return page;
}

gboolean eee_account_wizard_check(EPlugin *epl, EConfigHookPageCheckData *data)
{
    const char *name = ((EMConfigTargetSettings *) data->config->target)->email_address;
    char *eee_host = NULL;
    GtkWidget *page;

    g_return_val_if_fail (lbl != NULL, FALSE);

    if (name == NULL)
        return TRUE;

    g_debug("** EEE **: Wizard check: E-mail: %s", name);

    if (name != NULL)
        eee_host = get_eee_server_hostname(name);

    if (eee_host != NULL)
    {
        dns_resolv_successful = TRUE;
/*        gtk_assistant_set_forward_page_func(assistant, NULL, NULL, NULL);*/
        gtk_label_set_text(lbl, g_strdup_printf(_("3e calendar server has been found for your domain. You can enable\n"
                                                  "calendar account for your account <i>%s</i> if you have it. If you\n"
                                                  "don't know ask your system administrator or provider of your email\n"
                                                  "service. Go to email account preferences to change this setting later."), name));
        gtk_label_set_use_markup(lbl, TRUE);
//        g_free (eee_host);
    }
    else
    {
        dns_resolv_successful = FALSE;
/*        gtk_assistant_set_forward_page_func(assistant, skip_3e_page, NULL, NULL);*/
    }

    return TRUE;
}

void eee_account_wizard_commit(EPlugin *epl, EMConfigTargetSettings *target)
{
    const char *name = target->email_address;

    if ((wizard_eee_account_activated == TRUE) && (dns_resolv_successful == TRUE)) 
        eee_accounts_manager_enable_account(mgr(), name);
    else
        eee_accounts_manager_disable_account(mgr(), name);

    eee_accounts_manager_restart_sync(mgr());

    g_debug("** EEE **: Wizard commit for e-mail %s. 3e account is %s.",
            name, wizard_eee_account_activated ? "activated" : "disabled");
}
