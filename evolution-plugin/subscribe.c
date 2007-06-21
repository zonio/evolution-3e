#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "utils.h"

#include "subscribe.h"
#include "eee-calendar-config.h"
#include "eee-settings.h"

enum
{
  SUB_NAME_COLUMN = 0,
  SUB_PERM_COLUMN,
  SUB_ACCOUNT_COLUMN,
  SUB_IS_CALENDAR_COLUMN,
  SUB_OWNER_COLUMN,
  SUB_NUM_COLUMNS
};

struct subscribe_context
{
  GladeXML* xml;
  GtkWindow* win;
  GtkTreeStore *model;
  GtkTreeView *tview;
  EeeAccountsManager* mgr;
  GtkWidget* subscribe_button;
  GtkTreeSelection* selection;
};

static struct subscribe_context* active_ctx = NULL;

static gboolean load_calendars(EeeAccount* account, char* prefix, GtkTreeStore* model, struct subscribe_context* ctx)
{
  GSList *cals, *iter;
  GtkTreeIter titer_user;
  GtkTreeIter titer_cal;
  gboolean rs;

  g_debug("** EEE ** load_users account=%s prefix=%s", account->name, prefix);

  rs = eee_account_get_shared_calendars_by_username_prefix(account, prefix, &cals);
  eee_account_disconnect(account);
  if (!rs)
    return FALSE;

  // for each user get his calendars
  char* prev_owner = NULL;
  //XXX: probably we shouldn't assume that calendar list is sorted by calendar
  // owner by the server
  for (iter = cals; iter; iter = iter->next)
  {
    ESCalendar* cal = iter->data;
    // skip calendars owned by logegd in user
    if (!strcmp(account->name, cal->owner))
      continue;

    if (!prev_owner || strcmp(prev_owner, cal->owner))
    {
      gtk_tree_store_append(model, &titer_user, NULL);
      gtk_tree_store_set(model, &titer_user, 
        SUB_NAME_COLUMN, cal->owner, 
        SUB_PERM_COLUMN, "", 
        SUB_OWNER_COLUMN, cal->owner, 
        SUB_ACCOUNT_COLUMN, account, 
        SUB_IS_CALENDAR_COLUMN, FALSE, -1);
      prev_owner = cal->owner;
    }

    gtk_tree_store_append(model, &titer_cal, &titer_user);
    gtk_tree_store_set(model, &titer_cal,
      SUB_NAME_COLUMN, cal->name,
      SUB_PERM_COLUMN, cal->perm, 
      SUB_OWNER_COLUMN, cal->owner, 
      SUB_ACCOUNT_COLUMN, account, 
      SUB_IS_CALENDAR_COLUMN, TRUE, -1);
  }
  g_slist_foreach(cals, (GFunc)ESCalendar_free, NULL);
  g_slist_free(cals);

  gtk_tree_view_expand_all(ctx->tview);

  return TRUE;
}

static gboolean user_selected(GtkEntryCompletion *widget, GtkTreeModel *model, GtkTreeIter *iter, struct subscribe_context* ctx)
{
  char* user = NULL;
  EeeAccount* account = NULL;
  gtk_tree_model_get(model, iter, 0, &user, 1, &account, -1);
  gtk_tree_store_clear(ctx->model);
  if (user && account)
    load_calendars(account, user, ctx->model, ctx);
  g_free(user);
  return FALSE;
}

static gboolean user_insert_prefix(GtkEntryCompletion *widget, char* prefix, struct subscribe_context* ctx)
{
  GSList* iter, *list;

  if (prefix == NULL)
    return FALSE;

  gtk_tree_store_clear(ctx->model);
  list = eee_accounts_manager_peek_accounts_list(ctx->mgr);
  for (iter = list; iter; iter = iter->next)
  {
    EeeAccount* account = iter->data;
    load_calendars(account, prefix, ctx->model, ctx);
  }
  
  return FALSE;
}

static void calendar_selection_changed(GtkTreeSelection *selection, struct subscribe_context* ctx)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  char* name = NULL;
  char* perm = NULL;
  EeeAccount* account = NULL;
  gboolean is_calendar = FALSE;

  if (gtk_tree_selection_get_selected(selection, &model, &iter))
  {
    gtk_tree_model_get(model, &iter, 
      SUB_NAME_COLUMN, &name,
      SUB_PERM_COLUMN, &perm,
      SUB_ACCOUNT_COLUMN, &account,
      SUB_IS_CALENDAR_COLUMN, &is_calendar, -1);

    if (is_calendar)
      gtk_widget_set(ctx->subscribe_button, "sensitive", TRUE, NULL);
    else
      gtk_widget_set(ctx->subscribe_button, "sensitive", FALSE, NULL);

    g_free(perm);
    g_free(name);
  }
  else
    gtk_widget_set(ctx->subscribe_button, "sensitive", FALSE, NULL);
}

static void on_subs_button_subscribe_clicked(GtkButton* button, struct subscribe_context* ctx)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  char* name = NULL;
  char* owner = NULL;
  EeeAccount* account = NULL;
  gboolean is_calendar = FALSE;
  ESource* source;
  ESourceGroup* group;
  EeeSettings* settings;
  char* settings_string;

  if (!gtk_tree_selection_get_selected(ctx->selection, &model, &iter))
    goto err0;

  gtk_tree_model_get(model, &iter, 
    SUB_NAME_COLUMN, &name,
    SUB_OWNER_COLUMN, &owner,
    SUB_ACCOUNT_COLUMN, &account,
    SUB_IS_CALENDAR_COLUMN, &is_calendar, -1);

  if (!is_calendar || account == NULL || name == NULL)
    goto err1;

  group = eee_accounts_manager_find_group_by_name(ctx->mgr, owner);
  if (group == NULL)
    goto err1;

  if (!eee_account_subscribe_calendar(account, owner, name))
    goto err1;

  settings = eee_settings_new(NULL);
  eee_settings_set_title(settings, name);
  settings_string = eee_settings_encode(settings);
  source = e_source_new_3e(name, owner, account, settings_string);
  g_free(settings_string);
  g_object_unref(settings);
  e_source_group_add_source(group, source, -1);
  e_source_list_sync(eee_accounts_manager_peek_source_list(ctx->mgr), NULL);
  eee_accounts_manager_abort_current_sync(ctx->mgr);

 err1:
  g_free(name);
  g_free(owner);
 err0:
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_subs_button_cancel_clicked(GtkButton* button, struct subscribe_context* ctx)
{
  gtk_widget_destroy(GTK_WIDGET(ctx->win));
}

static void on_subs_window_destroy(GtkObject* object, struct subscribe_context* ctx)
{
  gtk_object_unref(GTK_OBJECT(ctx->win));
  g_object_unref(ctx->xml);
  g_free(ctx);
  active_ctx = NULL;
}

void subscribe_gui_create(EeeAccountsManager* mgr)
{
  int col_offset;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  if (!eee_plugin_online)
    return;

  if (active_ctx)
    return;

  struct subscribe_context* c = g_new0(struct subscribe_context, 1);
  c->mgr = mgr;
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "subs_window", NULL);

  c->win = GTK_WINDOW(gtk_widget_ref(glade_xml_get_widget(c->xml, "subs_window")));
  c->tview = GTK_TREE_VIEW(glade_xml_get_widget(c->xml, "treeview_calendars"));

  // create model
  c->model = gtk_tree_store_new(SUB_NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN, G_TYPE_STRING);
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

  // setup the selection handler
  c->selection = gtk_tree_view_get_selection(c->tview);
  gtk_tree_selection_set_mode(c->selection, GTK_SELECTION_SINGLE);
  g_signal_connect(c->selection, "changed", G_CALLBACK(calendar_selection_changed), c);

  // setup autocompletion (0 == username, 1 == EeeAccount*)
  GtkListStore* users_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
  GSList* iter, *list;
  list = eee_accounts_manager_peek_accounts_list(mgr);
  for (iter = list; iter; iter = iter->next)
  {
    EeeAccount* account = iter->data;
    eee_account_load_users(account, "", NULL, users_store);
    load_calendars(account, "", c->model, c);
  }
  GtkEntryCompletion *completion;
  completion = gtk_entry_completion_new();
  gtk_entry_set_completion(GTK_ENTRY(glade_xml_get_widget(c->xml, "entry_email")), completion);
  g_object_unref(completion);
  gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(users_store));
  g_object_unref(users_store);
  gtk_entry_completion_set_text_column(completion, 0);
  gtk_entry_completion_set_inline_completion(completion, TRUE);
  gtk_entry_completion_set_popup_single_match(completion, FALSE);
  g_signal_connect(completion, "match-selected", G_CALLBACK(user_selected), c);
  g_signal_connect(completion, "insert-prefix", G_CALLBACK(user_insert_prefix), c);

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
    gtk_widget_destroy(GTK_WIDGET(active_ctx->win));
}
