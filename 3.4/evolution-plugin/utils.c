/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2012 Zonio s.r.o <developers@zonio.net>
 *
 * This file is part of evolution-3e.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <string.h>
#include <libintl.h>

#define _(String) gettext(String)

#include "utils.h"

char *qp_escape_string(const char *s)
{
    if (s == NULL)
    {
        return NULL;
    }
    char *r = g_malloc(strlen(s) * 2 + 3);
    char *c = r;
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
gboolean e_source_group_is_3e(ESourceGroup *group)
{
    const char *base_uri = e_source_group_peek_base_uri(group);
    const char *name = e_source_group_peek_name(group);

    if (base_uri && name)
    {
        return g_str_has_prefix(name, "3e: ") && !strcmp(base_uri, EEE_URI_PREFIX);
    }
    return FALSE;
}

/* check if source is valid 3E source */
gboolean e_source_is_3e(ESource *source)
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

gboolean e_source_is_3e_owned_calendar(ESource *source)
{
    return e_source_is_3e(source) &&
           !strcmp(e_source_get_property(source, "eee-owner"),
                   e_source_get_property(source, "eee-account"));
}

void e_source_set_3e_properties(ESource *source, const char *calname, const char *owner, EeeAccount *account, const char *perm, const char *title, const char *color)
{
    char *relative_uri = g_strdup_printf("%s/%s/%s", account->name, owner, calname);
    char *key = g_strdup_printf("eee://%s", account->name);

//#if EVOLUTION_VERSION >= 232
//    gchar *scolor;
//#endif /* EVOLUTION_VERSION >= 232 */

    e_source_set_relative_uri(source, relative_uri);
    e_source_set_property(source, "auth", "1");
    e_source_set_property(source, "auth-domain", EEE_PASSWORD_COMPONENT);
    e_source_set_property(source, "auth-key", key);
    e_source_set_property(source, "username", account->name);
    e_source_set_property(source, "eee-server", account->server);
    e_source_set_property(source, "eee-owner", owner);
    e_source_set_property(source, "eee-account", account->name);
    if (g_ascii_strcasecmp(account->name, owner))
    {
        e_source_set_property(source, "subscriber", account->name);
    }
    e_source_set_property(source, "eee-calname", calname);
    if (perm)
    {
        e_source_set_property(source, "eee-perm", perm);
    }
    if (title)
    {
        e_source_set_name(source, title);
    }
    if (color)
    {
#if EVOLUTION_VERSION >= 232
        //scolor = gdk_color_to_string (&color);
        e_source_set_color_spec(source, color);
        //g_free(scolor);
#else
        guint32 scolor = 0;
        sscanf(color + 1, "%X", &scolor); //we don't want to read #

        e_source_set_color(source, scolor);
#endif /* EVOLUTION_VERSION >= 232 */
    }

    g_free(relative_uri);
    g_free(key);
}

ESource *e_source_new_3e(const char *calname, const char *owner, EeeAccount *account, const char *perm, const char *title, const char *color)
{
    ESource *source = e_source_new(_("[No Title]"), "");

    e_source_set_3e_properties(source, calname, owner, account, perm, title, color);
    return source;
}

ESource *e_source_new_3e_with_attrs(const char *calname, const char *owner, EeeAccount *account, const char *perm, GSList *attrs)
{
    ESource *source = e_source_new(_("[No Title]"), "");

    e_source_set_3e_properties_with_attrs(source, calname, owner, account, perm, attrs);
    return source;
}

void e_source_set_3e_properties_with_attrs(ESource *source, const char *calname, const char *owner, EeeAccount *account, const char *perm, GSList *attrs)
{
    const char *title = eee_find_attribute_value(attrs, "title");
    const char *color_string = eee_find_attribute_value(attrs, "color");

    if (color_string == NULL)
    {
        color_string = "#FF0000";
    }
    guint32 color = 0;
    sscanf(color_string + 1, "%X", &color); //we don't want to read #

    e_source_set_3e_properties(source, calname, owner, account, perm, title, color_string);
}

/* get ESource by 3E calendar name */
ESource *e_source_group_peek_source_by_calname(ESourceGroup *group, const char *name)
{
    GSList *iter;

    for (iter = e_source_group_peek_sources(group); iter != NULL; iter = iter->next)
    {
        const char *cal_name = e_source_get_property(E_SOURCE(iter->data), "eee-calname");
        if (cal_name && !strcmp(cal_name, name))
        {
            return E_SOURCE(iter->data);
        }
    }
    return NULL;
}

ESAttribute *eee_find_attribute(GSList *attrs, const char *name)
{
    GSList *iter;

    for (iter = attrs; iter; iter = iter->next)
    {
        ESAttribute *attr = iter->data;
        if (!strcmp(attr->name, name))
        {
            return attr;
        }
    }

    return NULL;
}

const char *eee_find_attribute_value(GSList *attrs, const char *name)
{
    ESAttribute *attr = eee_find_attribute(attrs, name);

    if (attr)
    {
        return attr->value;
    }
    return NULL;
}
