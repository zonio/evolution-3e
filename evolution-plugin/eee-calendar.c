#include "eee-calendar.h"

struct _EeeCalendarPriv
{
};

EeeCalendar* eee_calendar_new()
{
  EeeCalendar *obj = g_object_new(EEE_TYPE_CALENDAR, NULL);
  return obj;
}

G_DEFINE_TYPE(EeeCalendar, eee_calendar, G_TYPE_OBJECT);

static void eee_calendar_init(EeeCalendar *self)
{
  self->priv = g_new0(EeeCalendarPriv, 1);
}

static void eee_calendar_dispose(GObject *object)
{
  EeeCalendar *self = EEE_CALENDAR(object);

  G_OBJECT_CLASS(eee_calendar_parent_class)->dispose(object);
}

static void eee_calendar_finalize(GObject *object)
{
  EeeCalendar *self = EEE_CALENDAR(object);

  g_free(self->priv);
  g_signal_handlers_destroy(object);

  G_OBJECT_CLASS(eee_calendar_parent_class)->finalize(object);
}

static void eee_calendar_class_init(EeeCalendarClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = eee_calendar_dispose;
  gobject_class->finalize = eee_calendar_finalize;
}
