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

#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libintl.h>

#define _(String) gettext(String)

#include "utils.h"
#include "subscribe.h"
#include "eee-calendar-config.h"

enum
{
    SUB_NAME_COLUMN = 0,
    SUB_TITLE_COLUMN,
    SUB_PERM_COLUMN,
    SUB_IS_CALENDAR_COLUMN,
    SUB_OWNER_COLUMN,
    SUB_NUM_COLUMNS
};

struct subscribe_context
{
    GladeXML *xml;
    GtkWindow *win;
    GtkTreeStore *model;
    GtkTreeView *tview;
    GtkWidget *subscribe_button;
    GtkTreeSelection *selection;
    GtkEditable *search;
    EeeAccountsManager *mgr;
    EeeAccount *account; // no-ref, reference is held by model
};

static struct subscribe_context *active_ctx = NULL;

static gboolean calendar_exists(GSList *cals, ESCalendarInfo *ref_cal)
{
    GSList *iter;

    for (iter = cals; iter; iter = iter->next)
    {
        ESCalendarInfo *cal = iter->data;

        if (!strcmp(cal->name, ref_cal->name) &&
            !strcmp(cal->owner, ref_cal->owner))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean reload_data(struct subscribe_context *ctx, const char *query)
{
    GSList *cals, *existing_cals, *iter;
    GtkTreeIter titer_user;
    GtkTreeIter titer_cal;

    gtk_tree_store_clear(ctx->model);

    if (!eee_account_search_shared_calendars(ctx->account, query, &cals) ||
        !eee_account_load_calendars(ctx->account, &existing_cals))
    {
        eee_account_disconnect(ctx->account);
        return FALSE;
    }

    // for each user get his calendars
    char *prev_owner = NULL;
    for (iter = cals; iter; iter = iter->next)
    {
        const char *cal_title = NULL;
        ESCalendarInfo *cal = iter->data;

        // skip already subscribed cals
        if (calendar_exists(existing_cals, cal))
        {
            continue;
        }

        if (!prev_owner || strcmp(prev_owner, cal->owner))
        {
            GSList *attrs = NULL;
            const char *realname = NULL;
            char *title;

            eee_account_get_user_attributes(ctx->account, cal->owner, &attrs);
            realname = eee_find_attribute_value(attrs, "realname");
            if (realname)
            {
                title = g_strdup_printf("%s (%s)", cal->owner, realname);
            }
            else
            {
                title = g_strdup_printf("%s", cal->owner);
            }

            gtk_tree_store_append(ctx->model, &titer_user, NULL);
            gtk_tree_store_set(ctx->model, &titer_user,
                               SUB_NAME_COLUMN, cal->owner,
                               SUB_TITLE_COLUMN, title,
                               SUB_PERM_COLUMN, "",
                               SUB_OWNER_COLUMN, cal->owner,
                               SUB_IS_CALENDAR_COLUMN, FALSE, -1);
            prev_owner = cal->owner;

            g_free(title);
            eee_account_free_attributes_list(attrs);
        }

        cal_title = eee_find_attribute_value(cal->attrs, "title");

        gtk_tree_store_append(ctx->model, &titer_cal, &titer_user);
        gtk_tree_store_set(ctx->model, &titer_cal,
                           SUB_NAME_COLUMN, cal->name,
                           SUB_TITLE_COLUMN, cal_title ? cal_title : cal->name,
                           SUB_PERM_COLUMN, cal->perm,
                           SUB_OWNER_COLUMN, cal->owner,
                           SUB_IS_CALENDAR_COLUMN, TRUE, -1);
    }

    eee_account_disconnect(ctx->account);
    eee_account_free_calendars_list(cals);

    gtk_tree_view_expand_all(ctx->tview);

    return TRUE;
}

static void account_selected(GtkComboBox *combo, struct subscribe_context *ctx)
{
    GtkTreeIter iter;
    EeeAccount *account = NULL;

    ctx->account = NULL;
    if (gtk_combo_box_get_active_iter(combo, &iter))
    {
        gtk_tree_model_get(gtk_combo_box_get_model(combo), &iter, 1, &account, -1);
        gtk_tree_store_clear(ctx->model);
        if (account)
        {
            ctx->account = account;
            char *text = gtk_editable_get_chars(ctx->search, 0, -1);
            reload_data(ctx, text);
            g_free(text);
        }
    }
}

static void search_entry_changed(GtkEditable *editable, struct subscribe_context *ctx)
{
    char *text = gtk_editable_get_chars(editable, 0, -1);

    if (text)
    {
        reload_data(ctx, text);
    }

    g_free(text);
}

static void calendar_selection_changed(GtkTreeSelection *selection, struct subscribe_context *ctx)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    char *name = NULL;
    char *perm = NULL;
    gboolean is_calendar = FALSE;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        gtk_tree_model_get(model, &iter,
                           SUB_NAME_COLUMN, &name,
                           SUB_PERM_COLUMN, &perm,
                           SUB_IS_CALENDAR_COLUMN, &is_calendar, -1);

        if (is_calendar)
        {
            gtk_widget_set(ctx->subscribe_button, "sensitive", TRUE, NULL);
        }
        else
        {
            gtk_widget_set(ctx->subscribe_button, "sensitive", FALSE, NULL);
        }

        g_free(perm);
        g_free(name);
    }
    else
    {
        gtk_widget_set(ctx->subscribe_button, "sensitive", FALSE, NULL);
    }
}

static void on_subs_button_subscribe_clicked(GtkButton *button, struct subscribe_context *ctx)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    char *name = NULL;
    char *owner = NULL;
    char *perm = NULL;
    char *title = NULL;
    char *color_string = NULL;
    gboolean is_calendar = FALSE;
    ESource *source;
    ESourceGroup *group;
    char *settings_string;
    char *group_name;
    guint32 color;

    if (!gtk_tree_selection_get_selected(ctx->selection, &model, &iter))
    {
        goto err0;
    }

    gtk_tree_model_get(model, &iter,
                       SUB_NAME_COLUMN, &name,
                       SUB_TITLE_COLUMN, &title,
                       SUB_PERM_COLUMN, &perm,
                       SUB_OWNER_COLUMN, &owner,
                       SUB_IS_CALENDAR_COLUMN, &is_calendar, -1);

    if (!is_calendar || ctx->account == NULL || name == NULL)
    {
        goto err1;
    }

    if (!eee_account_subscribe_calendar(ctx->account, owner, name))
    {
        goto err1;
    }

    color = g_random_int_range(0x100000, 0x1000000);
    color_string = g_strdup_printf("#%06X", color);

    eee_account_set_calendar_attribute(ctx->account, owner, name, "title", title, FALSE);
    eee_account_set_calendar_attribute(ctx->account, owner, name, "color", color_string, FALSE);

    source = e_source_new_3e(name, owner, ctx->account, perm, title, color);

    eee_accounts_manager_add_source(ctx->mgr, owner, source);
    eee_accounts_manager_restart_sync(ctx->mgr);

err1:
    eee_account_disconnect(ctx->account);
    g_free(color_string);
    g_free(name);
    g_free(title);
    g_free(owner);
    g_free(perm);
err0:
    gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_subs_button_cancel_clicked(GtkButton *button, struct subscribe_context *ctx)
{
    gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_subs_window_destroy(GtkObject *object, struct subscribe_context *ctx)
{
    gtk_object_unref(GTK_OBJECT(ctx->win));
    g_object_unref(ctx->xml);
    g_free(ctx);
    active_ctx = NULL;
}

void subscribe_gui_create(EeeAccountsManager *mgr)
{
    int col_offset;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    if (!eee_plugin_online)
    {
        return;
    }

    if (active_ctx)
    {
        return;
    }

    struct subscribe_context *c = g_new0(struct subscribe_context, 1);
    c->mgr = mgr;
    c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "subs_window", NULL);

    c->win = GTK_WINDOW(gtk_widget_ref(glade_xml_get_widget(c->xml, "subs_window")));
    c->tview = GTK_TREE_VIEW(glade_xml_get_widget(c->xml, "treeview_calendars"));

    // create model for calendar list
    c->model = gtk_tree_store_new(SUB_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
    gtk_tree_view_set_model(c->tview, GTK_TREE_MODEL(c->model));
    // add columns to the tree view
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes(c->tview, -1, _("Calendar Name"), renderer, "text", SUB_TITLE_COLUMN, NULL);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes(c->tview, -1, _("Permission"), renderer, "text", SUB_PERM_COLUMN, NULL);
    // setup the selection handler
    c->selection = gtk_tree_view_get_selection(c->tview);
    gtk_tree_selection_set_mode(c->selection, GTK_SELECTION_SINGLE);
    g_signal_connect(c->selection, "changed", G_CALLBACK(calendar_selection_changed), c);

    // setup account list combo box
    GSList *iter, *list;
    GtkListStore *accounts_store = gtk_list_store_new(2, G_TYPE_STRING, EEE_TYPE_ACCOUNT);
    GtkWidget *accounts_combo = glade_xml_get_widget(c->xml, "combo_account");
    list = eee_accounts_manager_peek_accounts_list(mgr);
    for (iter = list; iter; iter = iter->next)
    {
        GtkTreeIter titer;
        EeeAccount *account = iter->data;
        if (account->state != EEE_ACCOUNT_STATE_ONLINE)
        {
            continue;
        }
        gtk_list_store_append(accounts_store, &titer);
        gtk_list_store_set(accounts_store, &titer, 0, account->name, 1, account, -1);
    }
    gtk_combo_box_set_model(GTK_COMBO_BOX(accounts_combo), GTK_TREE_MODEL(accounts_store));
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(accounts_combo), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(accounts_combo), renderer, "text", 0, NULL);
    g_signal_connect(accounts_combo, "changed", G_CALLBACK(account_selected), c);
    gtk_combo_box_set_active(GTK_COMBO_BOX(accounts_combo), 0);

    c->search = GTK_EDITABLE(glade_xml_get_widget(c->xml, "entry_search"));
    g_signal_connect(c->search, "changed", G_CALLBACK(search_entry_changed), c);

    // activate buttons
    c->subscribe_button = glade_xml_get_widget(c->xml, "subs_button_subscribe");
    gtk_widget_set(c->subscribe_button, "sensitive", FALSE, NULL);
    glade_xml_signal_connect_data(c->xml, "on_subs_button_subscribe_clicked", G_CALLBACK(on_subs_button_subscribe_clicked), c);
    glade_xml_signal_connect_data(c->xml, "on_subs_button_cancel_clicked", G_CALLBACK(on_subs_button_cancel_clicked), c);
    glade_xml_signal_connect_data(c->xml, "on_subs_window_destroy", G_CALLBACK(on_subs_window_destroy), c);

    active_ctx = c;
}

void subscribe_gui_destroy()
{
    if (active_ctx)
    {
        gtk_widget_destroy(GTK_WIDGET(active_ctx->win));
    }
}
