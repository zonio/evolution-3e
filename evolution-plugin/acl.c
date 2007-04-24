#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "acl.h"

static GSList* acl_contexts = NULL;

struct acl_context
{
  EeeCalendar* cal;
  GladeXML* xml;
  GtkWindow* win;
  GtkWidget* rb_private;
  GtkWidget* rb_public;
  GtkWidget* rb_shared;
  GtkWidget* users_frame;
  GtkListStore *model;
  GtkTreeView *tview;
  GtkTreeSelection* selection;

  // initial state
  int initial_mode;
  GSList* initial_perms;
};

enum
{
  ACL_MODE_PRIVATE,
  ACL_MODE_PUBLIC,
  ACL_MODE_SHARED
};

enum
{
  ACL_USERNAME_COLUMN,
  ACL_PERM_COLUMN,
  ACL_NUM_COLUMNS
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

  gtk_widget_set(ctx->users_frame, "visible", get_acl_mode(ctx) == ACL_MODE_SHARED, NULL);
  gtk_window_resize(ctx->win, 1, 1);
  g_debug("EEE: acl private %d", get_acl_mode(ctx));
}

static void on_acl_button_cancel_clicked(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl cancel");
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_acl_button_ok_clicked(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl ok");
}

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
  gtk_window_resize(ctx->win, 1, 1);

  GSList* iter;
  GtkTreeIter titer;
  for (iter = ctx->initial_perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    gtk_list_store_append(ctx->model, &titer);
    gtk_list_store_set(ctx->model, &titer, 
      ACL_USERNAME_COLUMN, perm->user, 
      ACL_PERM_COLUMN, perm->perm, -1);
  }
}

#define SIGNAL_CONNECT(name) \
  glade_xml_signal_connect_data(c->xml, G_STRINGIFY(name), (GCallback)name, c)

#define SIGNAL_CONNECT_TO(name, cb) \
  glade_xml_signal_connect_data(c->xml, G_STRINGIFY(name), (GCallback)cb, c)

void acl_gui_create(EeeCalendar* cal)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  struct acl_context* c = g_new0(struct acl_context, 1);
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "acl_window", NULL);

  c->cal = cal;
  c->win = GTK_WINDOW(gtk_widget_ref(glade_xml_get_widget(c->xml, "acl_window")));

  c->rb_private = glade_xml_get_widget(c->xml, "rb_perm_private");
  c->rb_public = glade_xml_get_widget(c->xml, "rb_perm_public");
  c->rb_shared = glade_xml_get_widget(c->xml, "rb_perm_shared");

  c->users_frame = glade_xml_get_widget(c->xml, "frame2");
  // users list
  c->tview = GTK_TREE_VIEW(glade_xml_get_widget(c->xml, "acl_users_treeview"));
  c->model = gtk_list_store_new(ACL_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_view_set_model(c->tview, GTK_TREE_MODEL(c->model));
  // add columns to the tree view
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "xalign", 0.0, NULL);
  gtk_tree_view_insert_column_with_attributes(c->tview, -1, "User", renderer, "text", ACL_USERNAME_COLUMN, NULL);
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "xalign", 0.0, NULL);
  gtk_tree_view_insert_column_with_attributes(c->tview, -1, "Permission", renderer, "text", ACL_PERM_COLUMN, NULL);

  // setup the selection handler
  c->selection = gtk_tree_view_get_selection(c->tview);
  gtk_tree_selection_set_mode(c->selection, GTK_SELECTION_SINGLE);

  SIGNAL_CONNECT_TO(on_rb_perm_private_toggled, on_rb_perm_toggled);
  SIGNAL_CONNECT_TO(on_rb_perm_public_toggled, on_rb_perm_toggled);
  SIGNAL_CONNECT_TO(on_rb_perm_shared_toggled, on_rb_perm_toggled);
  SIGNAL_CONNECT(on_acl_button_cancel_clicked);
  SIGNAL_CONNECT(on_acl_button_ok_clicked);
  SIGNAL_CONNECT(on_acl_window_destroy);

  if (!load_state(c))
  {
    gtk_widget_destroy(GTK_WIDGET(c->win));
    return;
  }
  update_gui_state(c);
}
