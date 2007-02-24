#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <e-util/e-config.h>
#include <e-util/e-plugin.h>
#include <calendar/gui/e-cal-config.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-account-list.h>
#include <libecal/e-cal.h>

#include <string.h>

#include "eee-accounts-manager.h"

/*****************************************************************************/
/* plugin intialization */

static EeeAccountsManager* _eee_accounts_mgr = NULL;

static void _free_eee_accounts_manager()
{
  eee_accounts_manager_free(_eee_accounts_mgr);
}

int e_plugin_lib_enable(EPluginLib * ep, int enable)
{
  if (_eee_accounts_mgr == NULL)
  {
    _eee_accounts_mgr = eee_accounts_manager_new();  
    g_atexit(_free_eee_accounts_manager);
  }
  return 0;
}

/*****************************************************************************/
/* the URL field for 3e sources */

GtkWidget *e_calendar_3e_properties(EPlugin* epl, EConfigHookItemFactoryData* data)
{
  ECalConfigTargetSource *t = (ECalConfigTargetSource*)data->target;
  ESource *source;
  ESourceGroup *group;
  GtkWidget *parent;
  GtkWidget *lurl;
  GtkWidget *location;
  char *uri;
  int row;

  source = t->source;
  group = e_source_peek_group(source);
  if (strcmp(e_source_group_peek_base_uri(group), "eee://"))
    return NULL;

#if 0
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

gboolean e_calendar_3e_check(EPlugin* epl, EConfigHookPageCheckData* data)
{
  ECalConfigTargetSource *t = (ECalConfigTargetSource*)data->target;
  ESourceGroup *group = e_source_peek_group(t->source);

  if (strcmp(e_source_group_peek_base_uri(group), "eee://"))
    return TRUE;

  return TRUE;
}
