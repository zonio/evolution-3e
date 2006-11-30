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

/*****************************************************************************/
/* plugin intialization */

static void ensure_3e_source_group()
{
    ESourceList *slist;
    ESourceGroup *group;

    if (!e_cal_get_sources(&slist, E_CAL_SOURCE_TYPE_EVENT, NULL))
    {
        g_warning("Could not get calendar source list from GConf!");
        return;
    }

    group = e_source_list_peek_group_by_name(slist, "3E");
    if (group == NULL)
    {
        gboolean res;

        group = e_source_group_new("3E", "eee://");
        res = e_source_list_add_group(slist, group, -1);
        if (res == FALSE)
        {
            g_warning("Could not add 3E source group!");
        }
        else
        {
            e_source_list_sync(slist, NULL);
        }
        g_object_unref(group);
    }
    g_object_unref(slist);
}

int e_plugin_lib_enable(EPluginLib * ep, int enable)
{
    if (enable)
    {
        g_print("3E Eplugin starting up ...\n");
        ensure_3e_source_group();
    }
    return 0;
}

/*****************************************************************************/
/* the URL field for 3e sources */

static gchar *print_uri_noproto(EUri * uri)
{
    gchar *uri_noproto;

    if (uri->port != 0)
        uri_noproto = g_strdup_printf("%s%s%s%s%s%s%s:%d%s%s%s", uri->user ? uri->user : "", uri->authmech ? ";auth=" : "", uri->authmech ? uri->authmech : "", uri->passwd ? ":" : "", uri->passwd ? uri->passwd : "", uri->user ? "@" : "", uri->host ? uri->host : "", uri->port, uri->path ? uri->path : "", uri->query ? "?" : "", uri->query ? uri->query : "");
    else
        uri_noproto = g_strdup_printf("%s%s%s%s%s%s%s%s%s%s", uri->user ? uri->user : "", uri->authmech ? ";auth=" : "", uri->authmech ? uri->authmech : "", uri->passwd ? ":" : "", uri->passwd ? uri->passwd : "", uri->user ? "@" : "", uri->host ? uri->host : "", uri->path ? uri->path : "", uri->query ? "?" : "", uri->query ? uri->query : "");
    return uri_noproto;
}

static void location_changed(GtkEntry * editable, ESource * source)
{
    EUri *euri;
    char *ruri;
    const char *uri;

    uri = gtk_entry_get_text(GTK_ENTRY(editable));

    euri = e_uri_new(uri);
    ruri = print_uri_noproto(euri);
    e_source_set_relative_uri(source, ruri);
    g_free(ruri);
    e_uri_free(euri);
}

GtkWidget *e_calendar_3e_properties(EPlugin * epl, EConfigHookItemFactoryData * data)
{
    ECalConfigTargetSource *t = (ECalConfigTargetSource *) data->target;
    ESource *source;
    ESourceGroup *group;
    GtkWidget *parent;
    GtkWidget *lurl;
    GtkWidget *location;
    char *uri;
    int row;

    source = t->source;
    group = e_source_peek_group(source);
    g_warning("factory: grp=%s, uri=%s", e_source_group_peek_base_uri(group), e_source_get_uri(t->source));
    if (strcmp(e_source_group_peek_base_uri(group), "eee://"))
        return NULL;

    uri = e_source_get_uri(source);

    parent = data->parent;
    row = GTK_TABLE(parent)->nrows;

    lurl = gtk_label_new_with_mnemonic("_URL:");
    gtk_widget_show(lurl);
    gtk_misc_set_alignment(GTK_MISC(lurl), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(parent), lurl, 0, 1, row, row + 1, GTK_FILL, 0, 0, 0);

    location = gtk_entry_new();
    gtk_widget_show(location);
    gtk_entry_set_text(GTK_ENTRY(location), uri);
    gtk_table_attach(GTK_TABLE(parent), location, 1, 2, row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    gtk_label_set_mnemonic_widget(GTK_LABEL(lurl), location);

    g_signal_connect(G_OBJECT(location), "changed", G_CALLBACK(location_changed), source);

    g_free(uri);

    return NULL;
}

gboolean e_calendar_3e_check(EPlugin * epl, EConfigHookPageCheckData * data)
{
    ECalConfigTargetSource *t = (ECalConfigTargetSource *) data->target;
    ESourceGroup *group = e_source_peek_group(t->source);
    EUri *uri;
    char *uri_text;
    gboolean ok = FALSE;

    if (strcmp(e_source_group_peek_base_uri(group), "eee://"))
        return TRUE;

    uri_text = e_source_get_uri(t->source);
    uri = e_uri_new(uri_text);
    ok = uri->user && uri->path && uri->host && !uri->query && !uri->fragment && strlen(uri->path) > 1;
    e_uri_free(uri);
    g_free(uri_text);

    return ok;
}

void e_calendar_3e_commit(EPlugin * epl, EConfigTarget * target)
{
    ECalConfigTargetSource *t = (ECalConfigTargetSource *) target;
    ESourceGroup *group = e_source_peek_group(t->source);
    EUri *uri;
    char *uri_text;

    if (strcmp(e_source_group_peek_base_uri(group), "eee://"))
        return;

    uri_text = e_source_get_uri(t->source);
    uri = e_uri_new(uri_text);
    e_source_set_property(t->source, "username", uri->user);
    e_uri_free(uri);
    g_free(uri_text);

    e_source_set_property(t->source, "auth-domain", "EEE");
    e_source_set_property(t->source, "auth-type", "plain");
    e_source_set_property(t->source, "auth", "true");
}
