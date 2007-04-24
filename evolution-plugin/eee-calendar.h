#ifndef __EEE_CALENDAR_H__
#define __EEE_CALENDAR_H__

#include "eee-settings.h"

/** 3E calendar info object.
 */

#define EEE_TYPE_CALENDAR            (eee_calendar_get_type())
#define EEE_CALENDAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), EEE_TYPE_CALENDAR, EeeCalendar))
#define EEE_CALENDAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  EEE_TYPE_CALENDAR, EeeCalendarClass))
#define IS_EEE_CALENDAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), EEE_TYPE_CALENDAR))
#define IS_EEE_CALENDAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  EEE_TYPE_CALENDAR))
#define EEE_CALENDAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  EEE_TYPE_CALENDAR, EeeCalendarClass))

typedef struct _EeeCalendar EeeCalendar;
typedef struct _EeeCalendarClass EeeCalendarClass;
typedef struct _EeeCalendarPriv EeeCalendarPriv;

#include "eee-account.h"

struct _EeeCalendar
{
  GObject parent;
  EeeCalendarPriv* priv;

  char* name;                 /**< Calendar name. */
  char* perm;                 /**< Calendar permissions. (read, write, none) */
  char* relative_uri;         /**< Relative URI of the calendar. Feeded to the backend. */
  EeeSettings* settings;      /**< Calendar settings. (title, color) */
  EeeAccount* access_account; /**< 3E account that is used to access this calendar. */
  EeeAccount* owner_account;  /**< 3E account that this calendar is assigned to (owner's). */

  int synced;
};

struct _EeeCalendarClass
{
  GObjectClass parent_class;
};

G_BEGIN_DECLS

EeeCalendar* eee_calendar_new();

void eee_calendar_set_name(EeeCalendar* cal, const char* name);
void eee_calendar_set_perm(EeeCalendar* cal, const char* perm);
void eee_calendar_set_relative_uri(EeeCalendar* cal, const char* uri);
void eee_calendar_set_access_account(EeeCalendar* cal, EeeAccount* account);
void eee_calendar_set_owner_account(EeeCalendar* cal, EeeAccount* account);
gboolean eee_calendar_store_settings(EeeCalendar* cal);

GType eee_calendar_get_type() G_GNUC_CONST;

G_END_DECLS

#endif /* __EEE_CALENDAR_H__ */
