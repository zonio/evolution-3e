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

#include <libedataserver/eds-version.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libintl.h>

#define _(String) gettext(String)

#include "acl.h"
#include "eee-calendar-config.h"
#include "utils.h"

static GSList *acl_contexts = NULL;

enum
{
    ACL_USERNAME_COLUMN,
    ACL_REALNAME_COLUMN,
    ACL_PERM_COLUMN,
    ACL_NUM_COLUMNS
};

enum
{
    USERS_USERNAME_COLUMN,
    USERS_REALNAME_COLUMN,
    USERS_ACCOUNT_COLUMN,
    USERS_NUM_COLUMNS
};

struct acl_perm
{
    ESUserPermission perm;
    char *realname;
};

struct acl_list_click_data
{
    GtkTreeIter iter;
    struct acl_context *ctx;
};

/* code to set and get acl mode in GUI */

enum
{
    ACL_MODE_PRIVATE,
    ACL_MODE_PUBLIC,
    ACL_MODE_SHARED
};

static int get_acl_mode(struct acl_context *ctx)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_private)))
    {
        return ACL_MODE_PRIVATE;
    }
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_public)))
    {
        return ACL_MODE_PUBLIC;
    }
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_shared)))
    {
        return ACL_MODE_SHARED;
    }
    return ACL_MODE_PRIVATE;
}

static int set_acl_mode(struct acl_context *ctx, int mode)
{
    switch (mode)
    {
    case ACL_MODE_PRIVATE:
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->rb_private), TRUE);
        break;
    case ACL_MODE_PUBLIC:
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->rb_public), TRUE);
        break;
    case ACL_MODE_SHARED:
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->rb_shared), TRUE);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

static void on_rb_perm_toggled(GtkButton *button, struct acl_context *ctx)
{
    // ignore signal for untoggle events
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    {
        return;
    }

    int mode = get_acl_mode(ctx);
    g_object_set(ctx->users_frame, "visible", mode == ACL_MODE_SHARED, NULL);
    if (mode == ACL_MODE_SHARED)
    {
//        gtk_window_resize(ctx->win, 500, 400);
    }
    else
    {
//        gtk_window_resize(ctx->win, 500, 1);
    }
}

/* ok/cancel buttons callbacks */

static void on_acl_button_cancel_clicked(GtkButton *button, struct acl_context *ctx)
{
    gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

// store ACL to the 3e server
gboolean store_acl(struct acl_context *ctx)
{
    GtkTreeIter iter;
    int new_mode = get_acl_mode(ctx);
    GSList *perms = NULL;
    gboolean retval;

    if (new_mode == ACL_MODE_SHARED)
    {
        // build list of users and permissions
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ctx->acl_model), &iter))
        {
            do
            {
                ESUserPermission *p = ESUserPermission_new();
                gtk_tree_model_get(GTK_TREE_MODEL(ctx->acl_model), &iter,
                                   ACL_USERNAME_COLUMN, &p->user,
                                   ACL_PERM_COLUMN, &p->perm,
                                   -1);
                perms = g_slist_append(perms, p);
            }
            while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ctx->acl_model), &iter));
        }
    }

    const char *calname = e_source_get_property(ctx->source, "eee-calname");
    if (ctx->initial_mode == new_mode)
    {
        if (new_mode == ACL_MODE_SHARED)
        {
            retval = eee_account_calendar_acl_set_shared(ctx->account, calname, perms);
        }
    }
    else
    {
        if (new_mode == ACL_MODE_PRIVATE)
        {
            retval = eee_account_calendar_acl_set_private(ctx->account, calname);
        }
        else if (new_mode == ACL_MODE_PUBLIC)
        {
            retval = eee_account_calendar_acl_set_public(ctx->account, calname);
        }
        else if (new_mode == ACL_MODE_SHARED)
        {
            retval = eee_account_calendar_acl_set_shared(ctx->account, calname, perms);
        }
    }

    g_slist_foreach(perms, (GFunc)ESUserPermission_free, NULL);
    g_slist_free(perms);

    eee_account_disconnect(ctx->account);

    return retval;
}

static void on_acl_button_ok_clicked(GtkButton *button, struct acl_context *ctx)
{
    store_acl(ctx);
    gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

/* window destructor */

void acl_perm_free(struct acl_perm *p)
{
    g_free(p->realname);
    ESUserPermission_free((ESUserPermission *)p);
}

#if EDS_CHECK_VERSION(3,0,0)
static void on_acl_window_destroy(GtkWidget *object, struct acl_context *ctx)
#else /* !EDS_CHECK_VERSION(3,0,0) */
static void on_acl_window_destroy(GtkObject *object, struct acl_context *ctx)
#endif /* !EDS_CHECK_VERSION(3,0,0) */
{
    g_object_unref(ctx->win);
    g_object_unref(ctx->source);
    g_object_unref(ctx->account);
    g_object_unref(ctx->builder);

    guint i;
    for (i = 0; i < ctx->initial_perms->len; i++)
        acl_perm_free (g_array_index (ctx->initial_perms, struct acl_perm *, i));
    g_array_free (ctx->initial_perms, TRUE);

    acl_contexts = g_slist_remove(acl_contexts, ctx);
    g_free(ctx);
}

static gboolean load_state(struct acl_context *ctx)
{
    GError *err = NULL;
    guint i;

    char *calname = (char *)e_source_get_property(ctx->source, "eee-calname");
    xr_client_conn *conn = eee_account_connect(ctx->account);

    if (!eee_account_auth(ctx->account))
    {
        return FALSE;
    }

    GArray *perms = ESClient_getUserPermissions(conn, calname, &err);
    if (err)
    {
        g_warning("** EEE ** Can't get permissions. (%d:%s)", err->code, err->message);
        g_clear_error(&err);
        eee_account_disconnect(ctx->account);
        return FALSE;
    }

    for (i = 0; i < perms->len; i++)
    {
        GArray *attrs = NULL;
        struct acl_perm *perm = g_renew(struct acl_perm, g_array_index (perms, struct acl_perm *, i), 1);
        eee_account_get_user_attributes(ctx->account, perm->perm.user, &attrs);
        perm->realname = g_strdup(eee_find_attribute_value(attrs, "realname"));
        eee_account_free_attributes_list(attrs);
    }

    eee_account_disconnect(ctx->account);

    // parse permissions
    // - no permissions (empty list) => private
    // - '*' => public
    // - some permissions => shared

    ctx->initial_perms = perms;
    if (perms == NULL || perms->len == 0)
    {
        ctx->initial_mode = ACL_MODE_PRIVATE;
        return TRUE;
    }

    for (i = 0; i < perms->len; i++)
    {
        ESUserPermission *perm = g_array_index (perms, ESUserPermission *, i);
        if (!strcmp(perm->user, "*"))
        {
            ctx->initial_mode = ACL_MODE_PUBLIC;
            return TRUE;
        }
    }

    ctx->initial_mode = ACL_MODE_SHARED;
    return TRUE;
}

void update_gui_state(struct acl_context *ctx)
{
    set_acl_mode(ctx, ctx->initial_mode);
    g_object_set(ctx->users_frame, "visible", ctx->initial_mode == ACL_MODE_SHARED, NULL);
    if (ctx->initial_mode == ACL_MODE_SHARED)
    {
//        gtk_window_resize(ctx->win, 500, 400);
    }
    else
    {
//        gtk_window_resize(ctx->win, 500, 1);
    }

    guint i;
    GtkTreeIter titer;
    for (i = 0; i < ctx->initial_perms->len; i++)
    {
        struct acl_perm *perm = g_array_index (ctx->initial_perms, struct acl_perm *, i);
        if (!strcmp(perm->perm.user, "*"))
            continue;

        gtk_list_store_append(ctx->acl_model, &titer);
        gtk_list_store_set(ctx->acl_model, &titer,
                           ACL_USERNAME_COLUMN, perm->perm.user,
                           ACL_REALNAME_COLUMN, perm->realname,
                           ACL_PERM_COLUMN, perm->perm.perm,
                           -1);
    }
}

static void editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, gpointer user_data)
{
    if (GTK_IS_ENTRY(editable))
    {
        GtkEntry *entry = GTK_ENTRY(editable);
    }
}

static void update_users_list(struct acl_context *ctx)
{
    // get a list of users to ignore
    GSList *users = NULL;
    guint i;

    for (i = 0; i < ctx->initial_perms->len; i++)
    {
        ESUserPermission *perm = g_array_index (ctx->initial_perms, ESUserPermission *, i);
        users = g_slist_append(users, perm->user);
    }
    gtk_list_store_clear(ctx->users_model);
    eee_account_load_users(ctx->account, NULL, users, ctx->users_model);
    g_slist_free(users);
    eee_account_disconnect(ctx->account);
}

// acl permission for given user in the treeview was changed, update acl
// permissions list store
void acl_perm_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, struct acl_context *ctx)
{
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(ctx->acl_model), &iter, gtk_tree_path_new_from_string(path)))
    {
        gtk_list_store_set(ctx->acl_model, &iter, ACL_PERM_COLUMN, new_text, -1);
    }
}

// clean combobox entry
static gboolean clean_entry(struct acl_context *ctx)
{
    gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ctx->user_entry))), "");
    return FALSE;
}

// add user to the permissions list:
// - remove it from available users model for combobox
// - add it to the model for permissions list
static void add_user(const char *user, struct acl_context *ctx)
{
    GtkTreeIter iter;

    // find user
    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ctx->users_model), &iter))
    {
        return;
    }
    do
    {
        char *user_name;
        char *realname;
        gtk_tree_model_get(GTK_TREE_MODEL(ctx->users_model), &iter,
                           USERS_USERNAME_COLUMN, &user_name,
                           USERS_REALNAME_COLUMN, &realname,
                           -1);
        if (user_name && !strcmp(user, user_name))
        {
            GtkTreeIter iter2;
            gtk_list_store_append(ctx->acl_model, &iter2);
            gtk_list_store_set(ctx->acl_model, &iter2,
                               ACL_USERNAME_COLUMN, user,
                               ACL_REALNAME_COLUMN, realname,
                               ACL_PERM_COLUMN, _("read"),
                               -1);

            gtk_list_store_remove(ctx->users_model, &iter);

            g_idle_add((GSourceFunc)clean_entry, ctx);
            g_free(user_name);
            g_free(realname);
            return;
        }
        g_free(user_name);
    }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ctx->users_model), &iter));
}

// when user clicks on remove button inside popup menu
static void on_remove_clicked(GtkMenuItem *menuitem, struct acl_list_click_data *cd)
{
    char *username;
    char *realname;
    GtkTreeIter iter_user;

    // put user back to users_model
    gtk_tree_model_get(GTK_TREE_MODEL(cd->ctx->acl_model), &cd->iter,
                       ACL_USERNAME_COLUMN, &username,
                       ACL_REALNAME_COLUMN, &realname,
                       -1);
    gtk_list_store_append(cd->ctx->users_model, &iter_user);
    gtk_list_store_set(cd->ctx->users_model, &iter_user,
                       USERS_USERNAME_COLUMN, username,
                       USERS_REALNAME_COLUMN, realname,
                       -1);

    g_free(username);
    g_free(realname);

    // remove him
    gtk_list_store_remove(cd->ctx->acl_model, &cd->iter);

    g_free(cd);
}

// tree view clicked on
static gboolean on_tview_clicked(GtkTreeView *tv, GdkEventButton *event, struct acl_context *ctx)
{
    GtkTreePath *path = NULL;
    GtkTreeIter iter;

    if (event->button != 3)
    {
        return FALSE;
    }

    if (gtk_tree_view_get_path_at_pos(tv, event->x, event->y, &path, NULL, NULL, NULL) &&
        gtk_tree_model_get_iter(GTK_TREE_MODEL(ctx->acl_model), &iter, path))
    {
        // create popup
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *menu_item = gtk_menu_item_new_with_label("Remove");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        struct acl_list_click_data *click = g_new0(struct acl_list_click_data, 1);
        click->iter = iter;
        click->ctx = ctx;
        g_signal_connect(menu_item, "activate", G_CALLBACK(on_remove_clicked), click);
        gtk_widget_show_all(menu);
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event->button, event->time);
        g_object_ref_sink(menu);
        g_object_unref(menu);
    }

    return FALSE;
}

// user pressed enter on the entry
static gboolean combo_entry_keypress(GtkEntry *entry, GdkEventKey *event, struct acl_context *ctx)
{
#if EDS_CHECK_VERSION(3,0,0)
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
#else /* !EDS_CHECK_VERSION(3,0,0) */
    if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter)
#endif /* !EDS_CHECK_VERSION(3,0,0) */
    {
        add_user(gtk_entry_get_text(entry), ctx);
    }
    return FALSE;
}

// user selected from the autocompletion menu
static gboolean user_selected(GtkEntryCompletion *widget, GtkTreeModel *model, GtkTreeIter *iter, struct acl_context *ctx)
{
    char *user = NULL;

    gtk_tree_model_get(model, iter, USERS_USERNAME_COLUMN, &user, -1);
    add_user(user, ctx);
    g_free(user);
    return FALSE;
}

// combo box item selected
static void cbe_changed(GtkComboBox *cbe, struct acl_context *ctx)
{
    GtkTreeIter iter;

    if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(cbe), &iter))
    {
        char *user = NULL;
        gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(cbe)), &iter,
                           USERS_USERNAME_COLUMN, &user, -1);
        add_user(user, ctx);
        g_free(user);
    }
}

// add compeltion to the combobox entry
static void combo_add_completion(GtkComboBox *cbe, struct acl_context *ctx)
{
    GtkEntry *entry;
    GtkEntryCompletion *completion;
    GtkTreeModel *model;

    entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(cbe)));
    completion = gtk_entry_get_completion(entry);
    if (completion)
    {
        return;
    }

    /* No completion yet? Set one up. */
    completion = gtk_entry_completion_new();
    model = gtk_combo_box_get_model(GTK_COMBO_BOX(cbe));
    gtk_entry_completion_set_model(completion, model);
    gtk_entry_completion_set_text_column(completion, USERS_USERNAME_COLUMN);
    gtk_entry_completion_set_inline_completion(completion, TRUE);
    gtk_entry_completion_set_popup_single_match(completion, TRUE);
    gtk_entry_set_completion(entry, completion);
    g_signal_connect_after(completion, "match-selected", G_CALLBACK(user_selected), ctx);
    g_signal_connect(cbe, "changed", G_CALLBACK(cbe_changed), ctx);
    g_signal_connect(entry, "key-press-event", G_CALLBACK(combo_entry_keypress), ctx);
    g_object_unref(completion);
}

static void connect_signals (GtkBuilder *b, GObject *obj, const gchar *name, const gchar *handler,
                             GObject *cnct_obj, GConnectFlags flags, gpointer c)
{
    GCallback cb = NULL;

    if (!g_strcmp0 (handler, "on_rb_perm_private_toggled"))
        cb = G_CALLBACK(on_rb_perm_toggled);
    else if (!g_strcmp0 (handler, "on_rb_perm_shared_toggled"))
        cb = G_CALLBACK(on_rb_perm_toggled);
    else if (!g_strcmp0 (handler, "on_rb_perm_public_toggled"))
        cb = G_CALLBACK(on_rb_perm_toggled);
    else if (!g_strcmp0 (handler, "on_acl_button_cancel_clicked"))
        cb = G_CALLBACK(on_acl_button_cancel_clicked);
    else if (!g_strcmp0 (handler, "on_acl_button_ok_clicked"))
        cb = G_CALLBACK(on_acl_button_ok_clicked);
    else if (!g_strcmp0 (handler, "on_acl_window_destroy"))
        cb = G_CALLBACK(on_acl_window_destroy);

    if (cb)
        g_signal_connect (obj, name, cb, c);
}

// buid acl dialog
struct acl_context *
acl_gui_create(EeeAccountsManager *mgr, EeeAccount *account, ESource *source)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *menu_item;
    struct acl_context *c;

    if (!eee_plugin_online || account == NULL || !e_source_is_3e_owned_calendar(source))
    {
        return NULL;
    }

    // create context and load glade file
    c = g_new0(struct acl_context, 1);
    c->source = g_object_ref(source);
    c->account = g_object_ref(account);
    c->builder = gtk_builder_new ();
    gtk_builder_add_from_file (c->builder, PLUGINDIR "/org-gnome-evolution-eee.glade", NULL);
    c->win = GTK_WIDGET(g_object_ref(gtk_builder_get_object(c->builder, "acl_vbox")));
    c->rb_private = GTK_WIDGET(gtk_builder_get_object(c->builder, "rb_perm_private"));
    c->rb_public = GTK_WIDGET(gtk_builder_get_object(c->builder, "rb_perm_public"));
    c->rb_shared = GTK_WIDGET(gtk_builder_get_object(c->builder, "rb_perm_shared"));
    c->users_frame = GTK_WIDGET(gtk_builder_get_object(c->builder, "frame2"));
    c->tview = GTK_TREE_VIEW(gtk_builder_get_object(c->builder, "treeview_acl_users"));
    c->user_entry = GTK_WIDGET(gtk_builder_get_object(c->builder, "combobox1"));

    // users list for autocompletion inside acl table combo cells
    c->users_model = gtk_list_store_new(USERS_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, EEE_TYPE_ACCOUNT);
    gtk_combo_box_set_model(GTK_COMBO_BOX(c->user_entry), GTK_TREE_MODEL(c->users_model));
    combo_add_completion(GTK_COMBO_BOX(c->user_entry), c);
    g_object_set(c->user_entry, "text-column", USERS_USERNAME_COLUMN, NULL);

    // acl list
    c->acl_model = gtk_list_store_new(ACL_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(c->tview, GTK_TREE_MODEL(c->acl_model));
    // add columns to the tree view
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    (void) gtk_tree_view_insert_column_with_attributes(c->tview, -1, _("Username"), renderer, "text", ACL_USERNAME_COLUMN, NULL);
    (void) gtk_tree_view_insert_column_with_attributes(c->tview, -1, _("Real Name"), renderer, "text", ACL_REALNAME_COLUMN, NULL);
    //column = gtk_tree_view_get_column(c->tview, col_id);
    renderer = gtk_cell_renderer_combo_new();
    g_signal_connect(renderer, "editing-started", G_CALLBACK(editing_started), c);
    g_signal_connect(renderer, "edited", G_CALLBACK(acl_perm_edited), c);
    GtkTreeIter iter;
    GtkListStore *perm_model = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(perm_model, &iter);
    gtk_list_store_set(perm_model, &iter, 0, _("read"), -1);
    gtk_list_store_append(perm_model, &iter);
    gtk_list_store_set(perm_model, &iter, 0, _("write"), -1);
    g_object_set(renderer, "model", perm_model, NULL);
    g_object_set(renderer, "text-column", 0, NULL);
    g_object_set(renderer, "editable", TRUE, NULL);
    g_object_set(renderer, "has-entry", FALSE, NULL);
    g_object_set(renderer, "sensitive", TRUE, NULL);
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes(c->tview, -1, _("Permission"), renderer, "text", ACL_PERM_COLUMN, NULL);
    g_signal_connect(c->tview, "button-press-event", G_CALLBACK(on_tview_clicked), c);

    gtk_builder_connect_signals_full (c->builder, connect_signals, c);

    if (!load_state(c))
    {
        gtk_widget_destroy(c->win);
        return NULL;
    }

    update_users_list(c);
    update_gui_state(c);
    acl_contexts = g_slist_append(acl_contexts, c);
    gtk_widget_show(c->win);
    return c;
}

static void destroy_ctx(struct acl_context *ctx)
{
    gtk_widget_destroy(ctx->win);
}

void acl_gui_destroy()
{
    g_slist_foreach(acl_contexts, (GFunc)destroy_ctx, NULL);
}
