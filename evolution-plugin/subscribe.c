#include <glade/glade.h>

#include "subscribe.h"

static GSList* subscribe_contexts = NULL;

struct subscribe_context
{
  GladeXML* xml;
  GtkWindow* win;
};

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

void subscribe_gui_create()
{
  struct subscribe_context* c = g_new0(struct subscribe_context, 1);
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "subs_window", NULL);

  c->win = (GtkWindow*)gtk_object_ref(glade_xml_get_widget(c->xml, "subs_window"));

  SIGNAL_CONNECT(on_subs_button_cancel_clicked);
  SIGNAL_CONNECT(on_subs_button_subscribe_clicked);
  SIGNAL_CONNECT(on_subs_window_destroy);
}
