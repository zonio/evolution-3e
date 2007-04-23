#include <stdio.h>
#include <string.h>
#include "eee-settings.h"

struct _EeeSettingsPriv
{
  char* title;                /**< Calendar title. */
  guint32 color;              /**< Calendar color. */
};

EeeSettings* eee_settings_new(const char* string)
{
  EeeSettings *obj = g_object_new(EEE_TYPE_SETTINGS, NULL);
  if (eee_settings_parse(obj, string))
    return obj;
  g_object_unref(obj);
  return NULL;
}

gboolean eee_settings_parse(EeeSettings* settings, const char* string)
{
  g_return_val_if_fail(settings != NULL, FALSE);

  guint i;
  if (string == NULL)
    return TRUE;

  char** pairs = g_strsplit(string, ";", 0);
  for (i=0; i<g_strv_length(pairs); i++)
  {
    pairs[i] = g_strstrip(pairs[i]);
    if (strlen(pairs[i]) < 1 || strchr(pairs[i], '=') == NULL)
      continue;
    char* key = pairs[i];
    char* val = strchr(key, '=');
    *val = '\0';
    ++val;
    // now we have key and value
    if (!strcmp(key, "title"))
      eee_settings_set_title(settings, val);
    else if (!strcmp(key, "color"))
      sscanf(val, "#%x", &settings->priv->color);
  }
  g_strfreev(pairs);

  return TRUE;
}

char* eee_settings_encode(EeeSettings* settings)
{
  g_return_val_if_fail(settings != NULL, NULL);

  return g_strdup_printf("title=%s;color=#%06x;", settings->priv->title ? settings->priv->title : "", settings->priv->color);
}

void eee_settings_set_title(EeeSettings* settings, const char* title)
{
  g_return_if_fail(settings != NULL);

  g_free(settings->priv->title);
  settings->priv->title = g_strdup(title);
}

const char* eee_settings_get_title(EeeSettings* settings)
{
  g_return_val_if_fail(settings != NULL, NULL);

  return settings->priv->title;
}

void eee_settings_set_color(EeeSettings* settings, guint32 color)
{
  g_return_if_fail(settings != NULL);

  settings->priv->color = color;
}

guint32 eee_settings_get_color(EeeSettings* settings)
{
  g_return_val_if_fail(settings != NULL, 0);

  return settings->priv->color;
}

/* GObject foo */

G_DEFINE_TYPE(EeeSettings, eee_settings, G_TYPE_OBJECT);

static void eee_settings_init(EeeSettings *self)
{
  self->priv = g_new0(EeeSettingsPriv, 1);
}

static void eee_settings_dispose(GObject *object)
{
  EeeSettings *self = EEE_SETTINGS(object);

  G_OBJECT_CLASS(eee_settings_parent_class)->dispose(object);
}

static void eee_settings_finalize(GObject *object)
{
  EeeSettings *self = EEE_SETTINGS(object);

  g_free(self->priv->title);
  g_free(self->priv);
  g_signal_handlers_destroy(object);

  G_OBJECT_CLASS(eee_settings_parent_class)->finalize(object);
}

static void eee_settings_class_init(EeeSettingsClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = eee_settings_dispose;
  gobject_class->finalize = eee_settings_finalize;
}
