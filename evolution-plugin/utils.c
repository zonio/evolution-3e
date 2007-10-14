/***************************************************************************
 *  3E plugin for Evolution                                                *
 *                                                                         *
 *  Copyright (C) 2007 by Zonio                                            *
 *  www.zonio.net                                                          *
 *  Ondrej Jirman <ondrej.jirman@zonio.net>                                *
 *                                                                         *
 ***************************************************************************/

#include <glib.h>
#include <string.h>
#include "utils.h"

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
  return
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

void e_source_set_3e_properties(ESource* source, const char* calname, const char* owner, EeeAccount* account, const char* perm, const char* title, guint32 color)
{
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
  if (perm)
    e_source_set_property(source, "eee-perm", perm);
  if (title)
    e_source_set_name(source, title);
  if (color)
    e_source_set_color(source, color);

  g_free(relative_uri);
  g_free(key);
}

ESource* e_source_new_3e(const char* calname, const char* owner, EeeAccount* account, const char* perm, const char* title, guint32 color)
{
  ESource* source = e_source_new("[No Title]", "");
  e_source_set_3e_properties(source, calname, owner, account, perm, title, color);
  return source;
}

ESource* e_source_new_3e_with_attrs(const char* calname, const char* owner, EeeAccount* account, const char* perm, GSList* attrs)
{
  ESource* source = e_source_new("[No Title]", "");
  e_source_set_3e_properties_with_attrs(source, calname, owner, account, perm, attrs);
  return source;
}

void e_source_set_3e_properties_with_attrs(ESource* source, const char* calname, const char* owner, EeeAccount* account, const char* perm, GSList* attrs)
{
  const char* title = eee_find_attribute_value(attrs, "title");
  const char* color_string = eee_find_attribute_value(attrs, "color");
  guint32 color = 0;

  if (color_string)
    sscanf(color_string, "%x", &color);
  e_source_set_3e_properties(source, calname, owner, account, perm, title, color);
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

ESAttribute* eee_find_attribute(GSList* attrs, const char* name)
{
  GSList *iter;

  for (iter = attrs; iter; iter = iter->next)
  {
    ESAttribute* attr = iter->data;
    if (!strcmp(attr->name, name))
      return attr;
  }

  return NULL;
}

const char* eee_find_attribute_value(GSList* attrs, const char* name)
{
  ESAttribute* attr = eee_find_attribute(attrs, name);
  if (attr)
  {
    return attr->value;
  }
  return NULL;
}
