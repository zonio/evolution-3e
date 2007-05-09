#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>

#include "acl.h"

static GSList* acl_contexts = NULL;

enum
{
  ACL_USERNAME_COLUMN,
  ACL_PERM_COLUMN,
  ACL_NUM_COLUMNS
};

struct acl_context
{
  EeeCalendar* cal;
  GladeXML* xml;
  GtkWindow* win;
  GtkWidget* rb_private;
  GtkWidget* rb_public;
  GtkWidget* rb_shared;
  GtkWidget* users_frame;
  GtkWidget* users_menu;
  GtkWidget* user_entry;
  GtkListStore *acl_model;
  GtkListStore *users_model;
  GtkTreeView *tview;

  // initial state
  int initial_mode;
  GSList* initial_perms;
};

/* code to set and get acl mode in GUI */

enum
{
  ACL_MODE_PRIVATE,
  ACL_MODE_PUBLIC,
  ACL_MODE_SHARED
};

static int get_acl_mode(struct acl_context* ctx)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_private)))
    return ACL_MODE_PRIVATE;
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_public)))
    return ACL_MODE_PUBLIC;
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_shared)))
    return ACL_MODE_SHARED;
  g_debug("EEE: should not happen");
  return ACL_MODE_PRIVATE;
}

static int set_acl_mode(struct acl_context* ctx, int mode)
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
      g_debug("EEE: should not happen");
      return FALSE;
  }
  return TRUE;
}

static void on_rb_perm_toggled(GtkButton* button, struct acl_context* ctx)
{
  // ignore signal for untoggle events
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    return;

  int mode = get_acl_mode(ctx);
  gtk_widget_set(ctx->users_frame, "visible", mode == ACL_MODE_SHARED, NULL);
  if (mode == ACL_MODE_SHARED)
    gtk_window_resize(ctx->win, 500, 400);
  else
    gtk_window_resize(ctx->win, 500, 1);
  g_debug("EEE: acl private %d", get_acl_mode(ctx));
}

/* ok/cancel buttons callbacks */

static void on_acl_button_cancel_clicked(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl cancel");
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

// store ACL to the 3e server
static gboolean store_acl(struct acl_context* ctx)
{
  GtkTreeIter iter;
  int new_mode = get_acl_mode(ctx);
  GSList* perms = NULL;
  gboolean retval;

  if (new_mode == ACL_MODE_SHARED)
  {
    // build list of users and permissions
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ctx->acl_model), &iter))
    {
      do
      {
        ESPermission* p = ESPermission_new();
        gtk_tree_model_get(GTK_TREE_MODEL(ctx->acl_model), &iter, 0, &p->user, 1, &p->perm, -1);
        perms = g_slist_append(perms, p);
      }
      while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ctx->acl_model), &iter));
    }
  }

  if (ctx->initial_mode == new_mode)
  {
    if (new_mode == ACL_MODE_SHARED)
      retval = eee_calendar_set_shared(ctx->cal, perms);
  }
  else
  {
    if (new_mode == ACL_MODE_PRIVATE)
      retval = eee_calendar_set_private(ctx->cal);
    else if (new_mode == ACL_MODE_PUBLIC)
      retval = eee_calendar_set_public(ctx->cal);
    else if (new_mode == ACL_MODE_SHARED)
      retval = eee_calendar_set_shared(ctx->cal, perms);
  }

  g_slist_foreach(perms, (GFunc)ESPermission_free, NULL);
  g_slist_free(perms);
  return retval;
}

static void on_acl_button_ok_clicked(GtkButton* button, struct acl_context* ctx)
{
  store_acl(ctx);
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

/* window destructor */

static void on_acl_window_destroy(GtkObject* object, struct acl_context* ctx)
{
  g_debug("EEE: acl destroy");
  gtk_object_unref(GTK_OBJECT(ctx->win));
  g_object_unref(ctx->xml);
  g_slist_foreach(ctx->initial_perms, (GFunc)ESPermission_free, NULL);
  g_slist_free(ctx->initial_perms);
  g_free(ctx);
}

static gboolean load_state(struct acl_context* ctx)
{
  GError* err = NULL;

  xr_client_conn* conn = eee_account_connect(ctx->cal->access_account);
  if (conn == NULL)
    return FALSE;

  GSList* perms = ESClient_getPermissions(conn, ctx->cal->name, &err);
  xr_client_free(conn);

  if (err)
  {
    g_debug("** EEE ** can't get permissions (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  // parse permissions
  // - no permissions (empty list) => private
  // - '*' => public
  // - some permissions => shared
  
  ctx->initial_perms = perms;
  if (perms == NULL)
  {
    ctx->initial_mode = ACL_MODE_PRIVATE;
    return TRUE;
  }

  GSList* iter;
  for (iter = perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    if (!strcmp(perm->user, "*"))
    {
      ctx->initial_mode = ACL_MODE_PUBLIC;
      return TRUE;
    }
  }

  ctx->initial_mode = ACL_MODE_SHARED;
  return TRUE;
}

void update_gui_state(struct acl_context* ctx)
{
  set_acl_mode(ctx, ctx->initial_mode);
  gtk_widget_set(ctx->users_frame, "visible", ctx->initial_mode == ACL_MODE_SHARED, NULL);
  if (ctx->initial_mode == ACL_MODE_SHARED)
    gtk_window_resize(ctx->win, 500, 400);
  else
    gtk_window_resize(ctx->win, 500, 1);

  GSList* iter;
  GtkTreeIter titer;
  for (iter = ctx->initial_perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    if (!strcmp(perm->user, "*"))
      continue;
    gtk_list_store_append(ctx->acl_model, &titer);
    gtk_list_store_set(ctx->acl_model, &titer, 
      ACL_USERNAME_COLUMN, perm->user, 
      ACL_PERM_COLUMN, perm->perm, -1);
  }
}

static void editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, gpointer user_data)
{
  if (GTK_IS_ENTRY(editable)) 
  {
    GtkEntry *entry = GTK_ENTRY(editable);
  }
}

static void update_users_list(struct acl_context* ctx)
{
  // get a list of users to ignore
  GSList* users = NULL;
  GSList* iter;
  for (iter = ctx->initial_perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    users = g_slist_append(users, perm->user);
  }
  gtk_list_store_clear(ctx->users_model);
  eee_account_load_users(ctx->cal->access_account, NULL, users, ctx->users_model);
  g_slist_free(users);
}

static gboolean popup_cb(GtkWidget *widget, GdkEvent *event, GtkWidget *menu)
{
  GdkEventButton *bevent = (GdkEventButton *)event;
  if (event->type != GDK_BUTTON_PRESS)
    return FALSE;
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, bevent->button, bevent->time);
  return TRUE;
}

void menu_activate_remove(GtkMenuItem* item, struct acl_context* ctx)
{
}

// acl permission for given user in the treeview was changed, update acl
// permissions list store
void acl_perm_edited(GtkCellRendererText* renderer, gchar* path, gchar* new_text, struct acl_context* ctx)
{
  GtkTreeIter iter;
  if (gtk_tree_model_get_iter(GTK_TREE_MODEL(ctx->acl_model), &iter, gtk_tree_path_new_from_string(path)))
    gtk_list_store_set(ctx->acl_model, &iter, ACL_PERM_COLUMN, new_text, -1);
}

// clean combobox entry
static gboolean clean_entry(struct acl_context* ctx)
{
  gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ctx->user_entry))), "");
  return FALSE;
}

// add user to the permissions list:
// - remove it from available users model for combobox
// - add it to the model for permissions list
static void add_user(const char* user, struct acl_context* ctx)
{
  GtkTreeIter iter;

  // find user
  if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ctx->users_model), &iter))
    return;
  do
  {
    char* user_name;
    gtk_tree_model_get(GTK_TREE_MODEL(ctx->users_model), &iter, 0, &user_name, -1);
    if (user_name && !strcmp(user, user_name))
    {
      GtkTreeIter iter2;
      gtk_list_store_append(ctx->acl_model, &iter2);
      gtk_list_store_set(ctx->acl_model, &iter2, 
        ACL_USERNAME_COLUMN, user, 
        ACL_PERM_COLUMN, "read", -1);

      gtk_list_store_remove(ctx->users_model, &iter);

      g_idle_add((GSourceFunc)clean_entry, ctx);
      g_free(user_name);
      return;
    }
    g_free(user_name);
  }
  while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ctx->users_model), &iter));
}

// user pressed enter on the entry
static gboolean combo_entry_keypress(GtkEntry* entry, GdkEventKey* event, struct acl_context* ctx)
{
  if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter)
    add_user(gtk_entry_get_text(entry), ctx);
  return FALSE;
}

// user selected from the autocompletion menu
static gboolean user_selected(GtkEntryCompletion *widget, GtkTreeModel *model, GtkTreeIter *iter, struct acl_context* ctx)
{
  char* user = NULL;
  gtk_tree_model_get(model, iter, 0, &user, -1);
  add_user(user, ctx);
  g_free(user);
  return FALSE;
}

// combo box item selected
static void cbe_changed(GtkComboBoxEntry* cbe, struct acl_context* ctx)
{
  GtkTreeIter iter;
  if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(cbe), &iter))
  {
    char* user = NULL;
    gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(cbe)), &iter, 0, &user, -1);
    add_user(user, ctx);
    g_free(user);
  }
}

// add compeltion to the combobox entry
static void combo_add_completion(GtkComboBoxEntry *cbe, struct acl_context* ctx)
{
  GtkEntry *entry;
  GtkEntryCompletion *completion;
  GtkTreeModel *model;

  entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(cbe)));
  completion = gtk_entry_get_completion(entry);
  if (completion)
    return;

  /* No completion yet? Set one up. */
  completion = gtk_entry_completion_new();
  model = gtk_combo_box_get_model(GTK_COMBO_BOX(cbe));
  gtk_entry_completion_set_model(completion, model);
  gtk_entry_completion_set_text_column(completion, 0);
  gtk_entry_completion_set_inline_completion(completion, TRUE);
  gtk_entry_completion_set_popup_single_match(completion, TRUE);
  gtk_entry_set_completion(entry, completion);
  g_signal_connect(completion, "match-selected", G_CALLBACK(user_selected), ctx);
  g_signal_connect(cbe, "changed", G_CALLBACK(cbe_changed), ctx);
  g_signal_connect(entry, "key-press-event", G_CALLBACK(combo_entry_keypress), ctx);
  g_object_unref(completion);
}

// buid acl dialog
void acl_gui_create(EeeCalendar* cal)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget* menu_item;
  int col_id;

  // create context and load glade file
  struct acl_context* c = g_new0(struct acl_context, 1);
  c->cal = cal;
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "acl_window", NULL);
  c->win = GTK_WINDOW(gtk_widget_ref(glade_xml_get_widget(c->xml, "acl_window")));
  c->rb_private = glade_xml_get_widget(c->xml, "rb_perm_private");
  c->rb_public = glade_xml_get_widget(c->xml, "rb_perm_public");
  c->rb_shared = glade_xml_get_widget(c->xml, "rb_perm_shared");
  c->users_frame = glade_xml_get_widget(c->xml, "frame2");
  c->tview = GTK_TREE_VIEW(glade_xml_get_widget(c->xml, "treeview_acl_users"));
  c->user_entry = glade_xml_get_widget(c->xml, "comboboxentry1");

  // create users popup menu
  c->users_menu = gtk_menu_new();
  menu_item = gtk_menu_item_new_with_label("Remove");
  gtk_menu_shell_append(GTK_MENU_SHELL(c->users_menu), menu_item);
  g_signal_connect_swapped(menu_item, "activate", G_CALLBACK(menu_activate_remove), c);
  gtk_widget_show(menu_item);

  // users list for autocompletion inside acl table combo cells
  c->users_model = gtk_list_store_new(2, G_TYPE_STRING, EEE_TYPE_ACCOUNT);
  gtk_combo_box_set_model(GTK_COMBO_BOX(c->user_entry), GTK_TREE_MODEL(c->users_model));
  combo_add_completion(GTK_COMBO_BOX_ENTRY(c->user_entry), c);
  g_object_set(c->user_entry, "text-column", 0, NULL);

  // acl list
  c->acl_model = gtk_list_store_new(ACL_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_view_set_model(c->tview, GTK_TREE_MODEL(c->acl_model));
  // add columns to the tree view
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "xalign", 0.0, NULL);
  col_id = gtk_tree_view_insert_column_with_attributes(c->tview, -1, "User", renderer, "text", ACL_USERNAME_COLUMN, NULL);
  //column = gtk_tree_view_get_column(c->tview, col_id);
  renderer = gtk_cell_renderer_combo_new();
  g_signal_connect(renderer, "editing-started", G_CALLBACK(editing_started), c);
  g_signal_connect(renderer, "edited", G_CALLBACK(acl_perm_edited), c);
  GtkTreeIter iter;
  GtkListStore* perm_model = gtk_list_store_new(1, G_TYPE_STRING);
  gtk_list_store_append(perm_model, &iter);
  gtk_list_store_set(perm_model, &iter, 0, "read", -1);
  gtk_list_store_append(perm_model, &iter);
  gtk_list_store_set(perm_model, &iter, 0, "write", -1);
  g_object_set(renderer, "model", perm_model, NULL);
  g_object_set(renderer, "text-column", 0, NULL);
  g_object_set(renderer, "editable", TRUE, NULL);
  g_object_set(renderer, "has-entry", FALSE, NULL);
  g_object_set(renderer, "sensitive", TRUE, NULL);
  g_object_set(renderer, "xalign", 0.0, NULL);
  gtk_tree_view_insert_column_with_attributes(c->tview, -1, "Permission", renderer, "text", ACL_PERM_COLUMN, NULL);

  glade_xml_signal_connect_data(c->xml, "on_rb_perm_private_toggled", G_CALLBACK(on_rb_perm_toggled), c);
  glade_xml_signal_connect_data(c->xml, "on_rb_perm_shared_toggled", G_CALLBACK(on_rb_perm_toggled), c);
  glade_xml_signal_connect_data(c->xml, "on_rb_perm_public_toggled", G_CALLBACK(on_rb_perm_toggled), c);
  glade_xml_signal_connect_data(c->xml, "on_acl_button_cancel_clicked", G_CALLBACK(on_acl_button_cancel_clicked), c);
  glade_xml_signal_connect_data(c->xml, "on_acl_button_ok_clicked", G_CALLBACK(on_acl_button_ok_clicked), c);
  glade_xml_signal_connect_data(c->xml, "on_acl_window_destroy", G_CALLBACK(on_acl_window_destroy), c);

  if (!load_state(c))
  {
    gtk_widget_destroy(GTK_WIDGET(c->win));
    return;
  }
  update_users_list(c);
  update_gui_state(c);
  gtk_widget_show(GTK_WIDGET(c->win));
}
