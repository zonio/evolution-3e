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

/** Notify listeners (CUA GUI) of error, pass message from @b GError object.
 * 
 * @param backend Calendar backend object.
 * @param message Error message.
 * @param err Error pointer.
 */
void e_cal_backend_notify_gerror_error(ECalBackend * backend, char *message, GError* err)
{
  if (err == NULL)
    return;

  char *error_message = g_strdup_printf("%s (%s)", message, err->message);
  e_cal_backend_notify_error(backend, error_message);
  g_free(error_message);
}

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
    const char *str = icalproperty_get_x_name (iter);

    if (str && !strcmp (str, key))
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
static const char* icomp_x_prop_get(icalcomponent *comp, const char *key)
{
	icalproperty *iter;

  g_return_val_if_fail(comp != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
	
  for (iter = icalcomponent_get_first_property(comp, ICAL_X_PROPERTY);
       iter;
       iter = icalcomponent_get_next_property(comp, ICAL_X_PROPERTY))
  {
    const char *str = icalproperty_get_x_name (iter);

		if (str && !strcmp (str, key))
      return icalproperty_get_value_as_string (iter);	
  }

	return NULL;
}

/** Set iCal component's X-EEE-CACHE-STATE property.
 * 
 * @param comp iCal component.
 * @param state ECalComponentCacheState enumeration value.
 */
void icalcomponent_set_cache_state(icalcomponent* comp, int state)
{
  char* val;

  if (comp == NULL)
    return;
  
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
int icalcomponent_get_cache_state(icalcomponent* comp)
{
  int cache_state = E_CAL_COMPONENT_CACHE_STATE_NONE;
  const char* state_str;

  if (comp == NULL)
    return E_CAL_COMPONENT_CACHE_STATE_NONE;
  
  state_str = icomp_x_prop_get(comp, "X-EEE-CACHE-STATE");
  if (state_str)
    cache_state = atoi(state_str);

  if (cache_state < 0 || cache_state > E_CAL_COMPONENT_CACHE_STATE_REMOVED)
    cache_state = E_CAL_COMPONENT_CACHE_STATE_NONE;

  return cache_state;
}

/** Set iCal component's X-EEE-CACHE-STATE property.
 * 
 * @param comp ECalComponent object.
 * @param state ECalComponentCacheState enumeration value.
 */
void e_cal_component_set_cache_state(ECalComponent* comp, ECalComponentCacheState state)
{
  icalcomponent_set_cache_state(e_cal_component_get_icalcomponent(comp), state);
}

/** Get iCal component's X-EEE-CACHE-STATE property value.
 * 
 * @param comp ECalComponent object.
 * 
 * @return Property value (ECalComponentCacheState).
 */
ECalComponentCacheState e_cal_component_get_cache_state(ECalComponent* comp)
{
  return icalcomponent_get_cache_state(e_cal_component_get_icalcomponent(comp));
}

/** Get TZID property value from iCal component.
 * 
 * @param comp iCal component.
 * 
 * @return TZID value (string) or NULL.
 */
const char* icalcomponent_get_tzid(icalcomponent* comp)
{
  icalproperty* prop;

  if (comp)
  {
    prop = icalcomponent_get_first_property(comp, ICAL_TZID_PROPERTY);
    if (prop)
      return icalproperty_get_tzid(prop);
  }

  return NULL;
}

/** Check if iCal component is marked as deleted by the 3E server.
 * 
 * @param comp iCal component.
 * 
 * @return TRUE if deleted, FALSE otherwise.
 */
gboolean icalcomponent_3e_status_is_deleted(icalcomponent* comp)
{
  const char* status = icomp_x_prop_get(comp, "X-3E-STATUS");

  return status && !g_ascii_strcasecmp(status, "deleted");
}

/** Collect attendee emails from the iCal component.
 *
 * To exclude username from the list, pass his name using @a organizer
 * parameter.
 * 
 * @param icomp iCal component.
 * @param organizer Username to exclude from the list.
 * @param recipients Pointer to the list of recipients' emails. It will be
 * appended to.
 */
void icalcomponent_collect_recipients(icalcomponent* icomp, const char* organizer, GSList** recipients)
{
  ECalComponent *comp;
  GSList *attendees = NULL, *iter;

  comp = e_cal_component_new();
  e_cal_component_set_icalcomponent(comp, icalcomponent_new_clone(icomp));

  e_cal_component_get_attendee_list(comp, &attendees);
  for (iter = attendees; iter; iter = iter->next)
  {
    ECalComponentAttendee *attendee = iter->data;
    /* priv->username is the organizer - mail sender, do not send him invitation */
    if (organizer == NULL || g_ascii_strcasecmp(organizer, attendee->value + 7))
      *recipients = g_slist_append(*recipients, g_strdup(attendee->value + 7));
  }

  e_cal_component_free_attendee_list(attendees);
  g_object_unref (comp);	
}

gboolean e_cal_component_id_compare(ECalComponentId* id1, ECalComponentId* id2)
{
  if (id1 == NULL || id2 == NULL)
    return FALSE;
  if (strcmp(id1->uid, id2->uid))
    return FALSE;
  if (id1->rid == id2->rid && id1->rid == NULL) /* both rids are NULL */
    return TRUE;
  if (id1->rid == NULL || id2->rid == NULL) /* one of rids is NULL */
    return FALSE;
  return !strcmp(id1->rid, id1->rid);
}

/** Check if component's ID matches given ID.
 * 
 * @param comp ECalComponent object.
 * @param id ECalComponentId object.
 * 
 * @return TRUE if ID (both UID and RID) matches.
 */
gboolean e_cal_component_match_id(ECalComponent* comp, ECalComponentId* id)
{
  ECalComponentId* comp_id = e_cal_component_get_id(comp);
  gboolean retval = e_cal_component_id_compare(comp_id, id);
  e_cal_component_free_id(comp_id);
  return retval;
}

/** @} */
