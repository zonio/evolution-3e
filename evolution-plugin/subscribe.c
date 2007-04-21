#include <glade/glade.h>

#include "subscribe.h"
#include "eee-accounts-manager.h"

static GSList* subscribe_contexts = NULL;

enum
{
  SUB_NAME_COLUMN = 0,
  SUB_PERM_COLUMN,
  SUB_NUM_COLUMNS
};

struct subscribe_context
{
  GladeXML* xml;
  GtkWindow* win;
  GtkTreeStore *model;
  GtkTreeView *tview;
};

static gboolean load_calendars(EeeAccount* acc, char* prefix, GtkTreeStore* model)
{
  xr_client_conn* conn;
  GError* err = NULL;
  GSList *users, *cals, *iter, *iter2;
  GtkTreeIter titer_user;
  GtkTreeIter titer_cal;
  int rs;

  conn = eee_server_connect_to_account(acc);
  if (conn == NULL)
    return FALSE;

  users = ESClient_getUsers(conn, prefix ? prefix : "", &err);
  if (err)
  {
    g_debug("** EEE ** Failed to get users list for user '%s'. (%d:%s)", acc->email, err->code, err->message);
    xr_client_free(conn);
    g_clear_error(&err);
    return FALSE;
  }

  // for each user get his calendars
  for (iter = users; iter; iter = iter->next)
  {
    char* user = iter->data;

    cals = ESClient_getSharedCalendars(conn, user, &err);
    if (err)
    {
      g_debug("** EEE ** Failed to get calendars for user '%s'. (%d:%s)", acc->email, err->code, err->message);
      xr_client_free(conn);
      g_clear_error(&err);
      return FALSE;
    }

    if (cals)
    {
      gtk_tree_store_append(model, &titer_user, NULL);
      gtk_tree_store_set(model, &titer_user, SUB_NAME_COLUMN, user, SUB_PERM_COLUMN, "", -1);
      for (iter2 = cals; iter2; iter2 = iter2->next)
      {
        ESCalendar* cal = iter2->data;
        gtk_tree_store_append(model, &titer_cal, &titer_user);
        gtk_tree_store_set(model, &titer_cal, SUB_NAME_COLUMN, cal->name, SUB_PERM_COLUMN, cal->perm, -1);
      }
    }

    g_slist_foreach(cals, (GFunc)ESCalendar_free, NULL);
    g_slist_free(cals);
  }

  g_slist_foreach(users, (GFunc)g_free, NULL);
  g_slist_free(users);

  xr_client_free(conn);
  return 0;
}

static void on_subs_button_subscribe_clicked(GtkButton* button, struct subscribe_context* ctx)
{
  g_debug("EEE: sub subscribe");
}

static void on_subs_button_cancel_clicked(GtkButton* button, struct subscribe_context* ctx)
{
  g_debug("EEE: sub cacnel");
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_subs_window_destroy(GtkObject* object, struct subscribe_context* ctx)
{
  g_debug("EEE: sub destroy");
  gtk_object_unref(GTK_OBJECT(ctx->win));
  g_object_unref(ctx->xml);
  g_free(ctx);
}

#define SIGNAL_CONNECT(name) \
  glade_xml_signal_connect_data(c->xml, G_STRINGIFY(name), (GCallback)name, c)

void subscribe_gui_create(EeeAccount* acc)
{
  int col_offset;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  struct subscribe_context* c = g_new0(struct subscribe_context, 1);
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "subs_window", NULL);

  c->win = GTK_WINDOW(gtk_widget_ref(glade_xml_get_widget(c->xml, "subs_window")));
  c->tview = GTK_TREE_VIEW(glade_xml_get_widget(c->xml, "treeview_calendars"));

  // create model
  c->model = gtk_tree_store_new(SUB_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_view_set_model(c->tview, GTK_TREE_MODEL(c->model));

  // add columns to the tree view
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "xalign", 0.0, NULL);
  col_offset = gtk_tree_view_insert_column_with_attributes(c->tview, -1, "Calendar Name", renderer, "text", SUB_NAME_COLUMN, NULL);
  //column = gtk_tree_view_get_column(c->tview, col_offset - 1);
  //gtk_tree_view_column_set_clickable(column, TRUE);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "xalign", 0.0, NULL);
  col_offset = gtk_tree_view_insert_column_with_attributes(c->tview, -1, "Permission", renderer, "text", SUB_PERM_COLUMN, NULL);
  //column = gtk_tree_view_get_column(c->tview, col_offset - 1);
  //gtk_tree_view_column_set_clickable(column, TRUE);

  load_calendars(acc, "", c->model);

  SIGNAL_CONNECT(on_subs_button_cancel_clicked);
  SIGNAL_CONNECT(on_subs_button_subscribe_clicked);
  SIGNAL_CONNECT(on_subs_window_destroy);
}
