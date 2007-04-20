#include <glade/glade.h>

#include "acl.h"

static GSList* acl_contexts = NULL;

struct acl_context
{
  GladeXML* xml;
  GtkWindow* win;
};

static void on_rb_perm_private_toggled(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl private");
}

static void on_rb_perm_public_toggled(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl public");
}

static void on_rb_perm_shared_toggled(GtkButton* button, struct acl_context* ctx)
{
  g_debug("EEE: acl shared");
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
  g_free(ctx);
}

#define SIGNAL_CONNECT(name) \
  glade_xml_signal_connect_data(c->xml, G_STRINGIFY(name), (GCallback)name, c)

void acl_gui_create()
{
  struct acl_context* c = g_new0(struct acl_context, 1);
  c->xml = glade_xml_new(PLUGINDIR "/org-gnome-evolution-eee.glade", "acl_window", NULL);

  c->win = (GtkWindow*)gtk_object_ref(glade_xml_get_widget(c->xml, "acl_window"));

  SIGNAL_CONNECT(on_rb_perm_private_toggled);
  SIGNAL_CONNECT(on_rb_perm_public_toggled);
  SIGNAL_CONNECT(on_rb_perm_shared_toggled);
  SIGNAL_CONNECT(on_acl_button_cancel_clicked);
  SIGNAL_CONNECT(on_acl_button_ok_clicked);
  SIGNAL_CONNECT(on_acl_window_destroy);
}
