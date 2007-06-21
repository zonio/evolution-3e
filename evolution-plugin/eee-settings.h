#ifndef __EEE_SETTINGS_H__
#define __EEE_SETTINGS_H__

#include <glib-object.h>

/** Settings that can be stored on 3e server.
 */

#define EEE_TYPE_SETTINGS            (eee_settings_get_type())
#define EEE_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), EEE_TYPE_SETTINGS, EeeSettings))
#define EEE_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  EEE_TYPE_SETTINGS, EeeSettingsClass))
#define IS_EEE_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), EEE_TYPE_SETTINGS))
#define IS_EEE_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  EEE_TYPE_SETTINGS))
#define EEE_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  EEE_TYPE_SETTINGS, EeeSettingsClass))

typedef struct _EeeSettings EeeSettings;
typedef struct _EeeSettingsClass EeeSettingsClass;
typedef struct _EeeSettingsPriv EeeSettingsPriv;

struct _EeeSettings
{
  GObject parent;
  EeeSettingsPriv* priv;
};

struct _EeeSettingsClass
{
  GObjectClass parent_class;
};

G_BEGIN_DECLS

GType eee_settings_get_type() G_GNUC_CONST;

EeeSettings*       eee_settings_new                    (const char* string);
gboolean           eee_settings_parse                  (EeeSettings* settings, const char* string);
char*              eee_settings_encode                 (EeeSettings* settings);
void               eee_settings_set_title              (EeeSettings* settings, const char* title);
const char*        eee_settings_get_title              (EeeSettings* settings);
void               eee_settings_set_color              (EeeSettings* settings, guint32 color);
guint32            eee_settings_get_color              (EeeSettings* settings);
char*              eee_settings_string_from_parts      (const char* title, guint32 color);

G_END_DECLS

#endif /* __EEE_SETTINGS_H__ */
