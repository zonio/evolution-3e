/*
 * Author: Ondrej Jirman <ondrej.jirman@zonio.net>
 *
 * Copyright 2007-2008 Zonio, s.r.o.
 *
 * This file is part of evolution-3e.
 *
 * Libxr is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of the License, or (at your option) any
 * later version.
 *
 * Libxr is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with evolution-3e.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libedataserverui/e-source-selector.h>
#include <calendar/gui/e-cal-config.h>
#include <calendar/gui/e-cal-popup.h>
#include <shell/es-event.h>
#include <mail/em-menu.h>
#include <mail/em-config.h>
#include <e-util/e-error.h>
#include <libintl.h>

#define _(String) gettext(String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

#include "eee-accounts-manager.h"
#include "eee-calendar-config.h"
#include "utils.h"
#include "subscribe.h"
#include "acl.h"

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

int e_plugin_lib_enable(EPluginLib *ep, int enable)
{
    xr_init();
    g_type_class_ref(EEE_TYPE_ACCOUNT);
    g_type_class_ref(EEE_TYPE_ACCOUNTS_MANAGER);
    if (getenv("EEE_EVO_DEBUG"))
    {
        xr_debug_enabled = XR_DEBUG_CALL;
    }
    g_debug("** EEE ** Starting 3E Evolution Plugin %s", PACKAGE_VERSION);
    g_debug("** EEE ** Please report bugs to <%s>", PACKAGE_BUGREPORT);
    return 0;
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

static void on_label_destroy(GtkObject *object, gpointer data)
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
    int row = GTK_TABLE(data->parent)->nrows;
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

static void on_permissions_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
    ESourceGroup *group = e_source_peek_group(source);
    EeeAccount *account;

    if (!eee_plugin_online)
    {
        return;
    }

    account = eee_accounts_manager_find_account_by_source(mgr(), source);
    acl_gui_create(mgr(), account, source);
}

static void on_unsubscribe_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
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

static void on_delete_cb(EPopup *ep, EPopupItem *pitem, void *data)
{
    ECalPopupTargetSource *target = (ECalPopupTargetSource *)ep->target;
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));

    if (e_error_run((GtkWindow *)gtk_widget_get_toplevel(ep->target->widget),
                    "calendar:prompt-delete-calendar", e_source_peek_name(source), NULL) != GTK_RESPONSE_YES)
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

static EPopupItem popup_items_shared_cal[] = {
    { E_POPUP_BAR,	"12.eee.00",  NULL,                       NULL,				NULL, NULL,          0, 0                          },
    { E_POPUP_ITEM, "12.eee.03",  N_("Unsubscribe"), on_unsubscribe_cb, NULL, "remove",      0, E_CAL_POPUP_SOURCE_PRIMARY },
    { E_POPUP_BAR,	"12.eee.04",  NULL,          NULL,		  NULL, NULL,          0, 0                          },
    { E_POPUP_ITEM, "20.delete",  N_("_Delete"),     on_delete_cb,		NULL, "edit-delete", 0, 0xffff                     },
};

static EPopupItem popup_items_user_cal[] = {
    { E_POPUP_BAR,	"12.eee.00",  NULL,                                NULL,              NULL, NULL,                 0, 0                                                    },
    { E_POPUP_ITEM, "12.eee.02",  N_("Setup Permissions..."), on_permissions_cb, NULL, "stock_shared-by-me", 0, E_CAL_POPUP_SOURCE_PRIMARY                           },
    { E_POPUP_BAR,	"12.eee.04",  NULL,               NULL,            NULL, NULL,                 0, 0                                                    },
    { E_POPUP_ITEM, "20.delete",  N_("_Delete"),              on_delete_cb,      NULL, "edit-delete",        0, E_CAL_POPUP_SOURCE_USER | E_CAL_POPUP_SOURCE_PRIMARY },
};

static EPopupItem popup_items_cal_offline[] = {
    { E_POPUP_BAR,	"12.eee.00",     NULL,               NULL,				NULL, NULL,                      0, 0      },
    { E_POPUP_ITEM, "12.eee.02",     N_("Setup Permissions..."), on_permissions_cb, NULL, "stock_shared-by-me",      0, 0xffff },
    { E_POPUP_ITEM, "12.eee.03",     N_("Unsubscribe"),      on_unsubscribe_cb, NULL, "remove",                  0, 0xffff },
    { E_POPUP_BAR,	"12.eee.04",     NULL,               NULL,				NULL, NULL,                      0, 0      },
    { E_POPUP_ITEM, "20.delete",     N_("_Delete"),          on_delete_cb,		NULL, "edit-delete",             0, 0xffff },
#if EVOLUTION_VERSION < 222
    { E_POPUP_ITEM, "30.properties", N_("_Properties..."),   NULL,				NULL, "stock_folder-properties", 0, 0xffff },
#else
    { E_POPUP_ITEM, "99.properties", N_("_Properties..."),   NULL,				NULL, "document-properties",     0, 0xffff },
#endif
};

static void popup_free(EPopup *ep, GSList *items, void *data)
{
    g_slist_free(items);
}

void eee_calendar_popup_source_factory(EPlugin *ep, ECalPopupTargetSource *target)
{
    // get selected source (which was right-clciked on)
    ESource *source = e_source_selector_peek_primary_selection(E_SOURCE_SELECTOR(target->selector));
    ESourceGroup *group = e_source_peek_group(source);
    int items_count;
    EPopupItem *items;
    GSList *menus = NULL;
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

    for (i = 0; i < items_count; i++)
    {
        menus = g_slist_prepend(menus, items + i);
    }

    e_popup_add_items(target->target.popup, menus, NULL, popup_free, NULL);
}

/* watch evolution state (online/offline) */

gboolean eee_plugin_online = FALSE;

void eee_calendar_state_changed(EPlugin *ep, ESEventTargetState *target)
{
    int online = !!target->state;

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

void eee_calendar_subscription(EPlugin *ep, EMMenuTargetSelect *target)
{
    if (!eee_plugin_online)
    {
        e_error_run(NULL, "eee:subscribe-offline", NULL);
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
                                        "xaling", 0,
                                        "yalign", 0.5,
                                        NULL);
    g_free(note);
    gtk_box_pack_start(GTK_BOX(section), label, FALSE, FALSE, 0);
    checkbutton_status = gtk_check_button_new_with_label(_("Enable 3e Account"));
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
