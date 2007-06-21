#include <glib.h>
#include <string.h>
#include "utils.h"
#include "eee-settings.h"

char* qp_escape_string(const char* s)
{
  if (s == NULL)
    return NULL;
  char* r = g_malloc(strlen(s)*2+3);
  char* c = r;
  *c = '\'';
  c++;
  for ( ; *s != '\0'; s++)
  {
    if (*s == '\'' || *s == '\\')
    {
      *c = '\\';
      c++;
    }
    *c = *s;
    c++;
  }
  *c = '\'';
  c++;
  *c = '\0';
  return r;
}

/* check if group is valid 3E group */
gboolean e_source_group_is_3e(ESourceGroup* group)
{
  const char* base_uri = e_source_group_peek_base_uri(group);
  const char* name = e_source_group_peek_name(group);
  if (base_uri && name)
    return g_str_has_prefix(name, "3E: ") && !strcmp(base_uri, EEE_URI_PREFIX);
  return FALSE;
}

/* check if source is valid 3E source */
gboolean e_source_is_3e(ESource* source)
{
  return e_source_group_is_3e(e_source_peek_group(source)) &&
    e_source_get_property(source, "auth") &&
    e_source_get_property(source, "auth-domain") &&
    e_source_get_property(source, "auth-key") &&
    e_source_get_property(source, "username") &&
    e_source_get_property(source, "eee-calname") &&
    e_source_get_property(source, "eee-owner") &&
    e_source_get_property(source, "eee-account");
}

gboolean e_source_is_3e_owned_calendar(ESource* source)
{
  return e_source_is_3e(source) &&
    !strcmp(e_source_get_property(source, "eee-owner"), 
      e_source_get_property(source, "eee-account"));
}

void e_source_set_3e_properties(ESource* source, const char* calname, const char* owner, EeeAccount* account, const char* settings)
{
  EeeSettings* s;
  char* relative_uri = g_strdup_printf("%s/%s/%s", account->name, owner, calname);
  char* key = g_strdup_printf("eee://%s", account->name);
  
  e_source_set_relative_uri(source, relative_uri);
  e_source_set_property(source, "auth", "1");
  e_source_set_property(source, "auth-domain", EEE_PASSWORD_COMPONENT);
  e_source_set_property(source, "auth-key", key);
  e_source_set_property(source, "username", account->name);
  e_source_set_property(source, "eee-server", account->server);
  e_source_set_property(source, "eee-owner", owner);
  e_source_set_property(source, "eee-account", account->name);
  e_source_set_property(source, "eee-calname", calname);

  if (settings)
  {
    s = eee_settings_new(settings);
    if (eee_settings_get_title(s))
      e_source_set_name(source, eee_settings_get_title(s));
    if (eee_settings_get_color(s) > 0)
      e_source_set_color(source, eee_settings_get_color(s));
    g_object_unref(s);
  }

  g_free(relative_uri);
  g_free(key);
}

ESource* e_source_new_3e(const char* calname, const char* owner, EeeAccount* account, const char* settings)
{
  ESource* source = e_source_new("[No Title]", "");
  e_source_set_3e_properties(source, calname, owner, account, settings);
  return source;
}

/* get ESource by 3E calendar name */
ESource* e_source_group_peek_source_by_calname(ESourceGroup *group, const char *name)
{
  GSList *iter;
  for (iter = e_source_group_peek_sources(group); iter != NULL; iter = iter->next)
  {
    const char* cal_name = e_source_get_property(E_SOURCE(iter->data), "eee-calname");
    if (cal_name && !strcmp(cal_name, name))
      return E_SOURCE(iter->data);
  }
  return NULL;
}
