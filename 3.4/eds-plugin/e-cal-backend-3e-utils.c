/*
 * Author: Ondrej Jirman <ondrej.jirman@zonio.net>
 *
 * Copyright 2007-2008 Zonio, s.r.o.
 *
 * This file is part of evolution-3e.
 *
 * Libxr is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of the License, or (at your option) any
 * later version.
 *
 * Libxr is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with evolution-3e.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "e-cal-backend-3e-priv.h"

/** @addtogroup eds_misc */
/** @{ */

// {{{ Error notification

/** Notify listeners (CUA GUI) of error, pass message from @b GError object.
 *
 * @param backend Calendar backend object.
 * @param message Error message.
 * @param err Error pointer.
 */
void e_cal_backend_notify_gerror_error(ECalBackend *backend, char *message, GError *err)
{
    if (err == NULL)
    {
        return;
    }

    char *error_message = g_strdup_printf("%s (%s)", message, err->message);
    e_cal_backend_notify_error(backend, error_message);
    g_free(error_message);
}

// }}}
// {{{ Cache path

const char *e_cal_backend_3e_get_cache_path(ECalBackend3e *cb)
{
    if (cb->priv->cache_path == NULL)
    {
        char *mangled_uri = g_strdup(e_cal_backend_get_cache_dir(E_CAL_BACKEND(cb)));
        guint i;

        for (i = 0; i < strlen(mangled_uri); i++)
        {
            if (mangled_uri[i] == ':' || mangled_uri[i] == '/')
            {
                mangled_uri[i] = '_';
            }
        }

        cb->priv->cache_path = g_build_filename(e_cal_backend_get_cache_dir (E_CAL_BACKEND(cb)), "calendar3e", mangled_uri, NULL);
        g_free(mangled_uri);
    }

    return cb->priv->cache_path;
}

// }}}
// {{{ iCalendar X-* property manipulation

/** Set icalcomponent X-* property.
 *
 * @param comp iCal component.
 * @param key Property name (i.e. X-EEE-WHATEVER).
 * @param value Property value.
 */
static void icomp_x_prop_set(icalcomponent *comp, const char *key, const char *value)
{
    icalproperty *iter;

    g_return_if_fail(comp != NULL);
    g_return_if_fail(key != NULL);

again:
    for (iter = icalcomponent_get_first_property(comp, ICAL_X_PROPERTY);
         iter;
         iter = icalcomponent_get_next_property(comp, ICAL_X_PROPERTY))
    {
        const char *str = icalproperty_get_x_name(iter);

        if (str && !strcmp(str, key))
        {
            icalcomponent_remove_property(comp, iter);
            icalproperty_free(iter);
            goto again;
        }
    }

    if (value)
    {
        iter = icalproperty_new_x(value);
        icalproperty_set_x_name(iter, key);
        icalcomponent_add_property(comp, iter);
    }
}

/** Get X-* property value from icalcomponent object.
 *
 * @param comp iCal component.
 * @param key Property name (i.e. X-EEE-WHATEVER).
 *
 * @return Property value or NULL.
 */
static const char *icomp_x_prop_get(icalcomponent *comp, const char *key)
{
    icalproperty *iter;

    g_return_val_if_fail(comp != NULL, NULL);
    g_return_val_if_fail(key != NULL, NULL);

    for (iter = icalcomponent_get_first_property(comp, ICAL_X_PROPERTY);
         iter;
         iter = icalcomponent_get_next_property(comp, ICAL_X_PROPERTY))
    {
        const char *str = icalproperty_get_x_name(iter);

        if (str && !strcmp(str, key))
        {
            return icalproperty_get_value_as_string(iter);
        }
    }

    return NULL;
}

// }}}
// {{{ iCalendar cache state property manipulation

/** Set iCal component's X-EEE-CACHE-STATE property.
 *
 * @param comp iCal component.
 * @param state ECalComponentCacheState enumeration value.
 */
void icalcomponent_set_cache_state(icalcomponent *comp, int state)
{
    char *val;

    if (comp == NULL)
    {
        return;
    }

    val = g_strdup_printf("%d", state);
    icomp_x_prop_set(comp, "X-EEE-CACHE-STATE", state == E_CAL_COMPONENT_CACHE_STATE_NONE ? NULL : val);
    g_free(val);
}

/** Get iCal component's X-EEE-CACHE-STATE property value.
 *
 * @param comp iCal component.
 *
 * @return Property value (ECalComponentCacheState).
 */
int icalcomponent_get_cache_state(icalcomponent *comp)
{
    int cache_state = E_CAL_COMPONENT_CACHE_STATE_NONE;
    const char *state_str;

    if (comp == NULL)
    {
        return E_CAL_COMPONENT_CACHE_STATE_NONE;
    }

    state_str = icomp_x_prop_get(comp, "X-EEE-CACHE-STATE");
    if (state_str)
    {
        cache_state = atoi(state_str);
    }

    if (cache_state < 0 || cache_state > E_CAL_COMPONENT_CACHE_STATE_REMOVED)
    {
        cache_state = E_CAL_COMPONENT_CACHE_STATE_NONE;
    }

    return cache_state;
}

/** Set iCal component's X-EEE-CACHE-STATE property.
 *
 * @param comp ECalComponent object.
 * @param state ECalComponentCacheState enumeration value.
 */
void e_cal_component_set_cache_state(ECalComponent *comp, ECalComponentCacheState state)
{
    icalcomponent_set_cache_state(e_cal_component_get_icalcomponent(comp), state);
}

/** Get iCal component's X-EEE-CACHE-STATE property value.
 *
 * @param comp ECalComponent object.
 *
 * @return Property value (ECalComponentCacheState).
 */
ECalComponentCacheState e_cal_component_get_cache_state(ECalComponent *comp)
{
    return icalcomponent_get_cache_state(e_cal_component_get_icalcomponent(comp));
}

/** Check if iCal component is marked as deleted by the 3E server.
 *
 * @param comp iCal component.
 *
 * @return TRUE if deleted, FALSE otherwise.
 */
gboolean icalcomponent_3e_status_is_deleted(icalcomponent *comp)
{
    const char *status = icomp_x_prop_get(comp, "X-3E-STATUS");

    return status && !g_ascii_strcasecmp(status, "deleted");
}

// }}}
// {{{ TZID extraction

/** Get TZID property value from iCal component.
 *
 * @param comp iCal component.
 *
 * @return TZID value (string) or NULL.
 */
const char *icalcomponent_get_tzid(icalcomponent *comp)
{
    icalproperty *prop;

    if (comp)
    {
        prop = icalcomponent_get_first_property(comp, ICAL_TZID_PROPERTY);
        if (prop)
        {
            return icalproperty_get_tzid(prop);
        }
    }

    return NULL;
}

// }}}
// {{{ iTIP payload/method extraction

/** Get VEVENT from the iTip.
 *
 * VCALENDAR
 *   VTIMEZONE+
 *   VEVENT    <--- will return this
 *
 * @param comp iCal component.
 *
 * @return VEVENT icalcomponent object or NULL.
 */
icalcomponent *icalcomponent_get_itip_payload(icalcomponent *comp)
{
    icalcomponent *ret = NULL;

    g_return_val_if_fail(comp != NULL, NULL);

    if (icalcomponent_isa(comp) == ICAL_VCALENDAR_COMPONENT)
    {
        icalcomponent *vevent = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
        if (vevent)
        {
            ret = vevent;
        }
    }
    else if (icalcomponent_isa(comp) == ICAL_VEVENT_COMPONENT)
    {
        ret = comp;
    }

    return ret;
}

/** Get iTIP method.
 *
 * @param comp iCal component.
 *
 * @return Method.
 */
icalproperty_method icalcomponent_get_itip_method(icalcomponent *comp)
{
    icalproperty_method method = ICAL_METHOD_NONE;

    if (icalcomponent_isa(comp) == ICAL_VCALENDAR_COMPONENT)
    {
        icalcomponent *vevent = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);

        if (vevent && icalcomponent_get_first_property(vevent, ICAL_METHOD_PROPERTY))
        {
            method = icalcomponent_get_method(vevent);
        }
        else
        {
            method = icalcomponent_get_method(comp);
        }
    }
    else if (icalcomponent_isa(comp) == ICAL_VEVENT_COMPONENT)
    {
        method = icalcomponent_get_method(comp);
    }

    return method;
}

static const char *strip_mailto(const char *address)
{
    if (address && !g_ascii_strncasecmp(address, "mailto:", 7))
    {
        address += 7;
    }
    return address;
}

// }}}
// {{{ iTIP recipients extraction

/** Collect recipients from the iTip (based on the METHOD).
 *
 * To exclude username from the list, pass his name using @a sender
 * parameter.
 *
 * @param icomp iCal component.
 * @param sender Username to exclude from the list.
 * @param recipients Pointer to the list of recipients' emails. It will be
 * appended to.
 */
void icalcomponent_collect_recipients(icalcomponent *icomp, const char *sender, GSList * *recipients)
{
    GSList *to_list = NULL, *attendees = NULL, *iter;
    ECalComponentOrganizer organizer;
    icalproperty_method method;
    icalcomponent *payload;
    ECalComponent *ecomp = NULL;

    payload = icalcomponent_get_itip_payload(icomp);
    method = icalcomponent_get_itip_method(icomp);

    if (payload == NULL)
    {
        goto out;
    }

    ecomp = e_cal_component_new();
    e_cal_component_set_icalcomponent(ecomp, icalcomponent_new_clone(payload));
    e_cal_component_get_attendee_list(ecomp, &attendees);
    e_cal_component_get_organizer(ecomp, &organizer);
    if (organizer.value == NULL)
    {
        goto out;
    }

    switch (method)
    {
    case ICAL_METHOD_REQUEST:
    case ICAL_METHOD_CANCEL:
        for (iter = attendees; iter; iter = iter->next)
        {
            ECalComponentAttendee *att = iter->data;

            if (!g_ascii_strcasecmp(att->value, organizer.value))
            {
                continue;
            }
            else if (att->sentby && !g_ascii_strcasecmp(att->sentby, organizer.sentby))
            {
                continue;
            }
            else if (!g_ascii_strcasecmp(strip_mailto(att->value), sender))
            {
                continue;
            }
            else if (att->status == ICAL_PARTSTAT_DELEGATED && (att->delto && *att->delto)
                     && !(att->rsvp) && method == ICAL_METHOD_REQUEST)
            {
                continue;
            }

            to_list = g_slist_append(to_list, g_strdup(strip_mailto(att->value)));
        }
        break;

    case ICAL_METHOD_REPLY:
        to_list = g_slist_append(to_list, g_strdup(strip_mailto(organizer.value)));
        break;

    case ICAL_METHOD_ADD:
    case ICAL_METHOD_REFRESH:
    case ICAL_METHOD_COUNTER:
    case ICAL_METHOD_DECLINECOUNTER:
        to_list = g_slist_append(to_list, g_strdup(strip_mailto(organizer.value)));
        // send the status to delegatee to the delegate also is missing
        break;

    case ICAL_METHOD_PUBLISH:
    default:
        break;
    }

out:
    e_cal_component_free_attendee_list(attendees);
    if (ecomp)
    {
        g_object_unref(ecomp);
    }
    if (recipients)
    {
        *recipients = to_list;
    }
}

// }}}
// {{{ iCalendar component ID comparison (UID@RID)

gboolean e_cal_component_id_compare(ECalComponentId *id1, ECalComponentId *id2)
{
    if (id1 == NULL || id2 == NULL)
    {
        return FALSE;
    }
    if (strcmp(id1->uid, id2->uid))
    {
        return FALSE;
    }
    if (id1->rid == id2->rid && id1->rid == NULL) /* both rids are NULL */
    {
        return TRUE;
    }
    if (id1->rid == NULL || id2->rid == NULL) /* one of rids is NULL */
    {
        return FALSE;
    }
    return !strcmp(id1->rid, id1->rid);
}

/** Check if component's ID matches given ID.
 *
 * @param comp ECalComponent object.
 * @param id ECalComponentId object.
 *
 * @return TRUE if ID (both UID and RID) matches.
 */
gboolean e_cal_component_match_id(ECalComponent *comp, ECalComponentId *id)
{
    ECalComponentId *comp_id = e_cal_component_get_id(comp);
    gboolean retval = e_cal_component_id_compare(comp_id, id);

    e_cal_component_free_id(comp_id);
    return retval;
}

// }}}

/** @} */
