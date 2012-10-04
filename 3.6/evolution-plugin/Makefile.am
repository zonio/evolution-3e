AM_CFLAGS = \
  $(EVO3E_CFLAGS) \
  -DEXCHANGE_EEE_LOCALEDIR=\"$(localedir)\" \
  -DPLUGINDIR=\""$(plugindir)"\" \
  $(LIBXR_CFLAGS) \
  -I$(top_srcdir) \
  -I$(top_srcdir)/utils

#plugin_DATA = \
#  org-gnome-evolution-eee.eplug \
#  org-gnome-evolution-eee.glade

error_DATA = \
  org-gnome-evolution-eee.error

#plugin_LTLIBRARIES = \
#  liborg-gnome-evolution-eee.la

evo_module_LTLIBRARIES = \
  module-eee.la

module_eee_la_SOURCES = \
  evolution-cal-config-eee.c \
  e-mail-config-eee-summary.c \
  e-mail-config-eee-summary.h

# liborg_gnome_evolution_eee_la_SOURCES = \
#  evolution-cal-config-eee.c \
#  e-mail-config-eee-summary.c \
#  e-mail-config-eee-summary.h \
#  eee-account.c \
#  eee-account.h \
#  eee-accounts-manager.c \
#  eee-accounts-manager.h \
#  eee-calendar-config.c \
#  eee-calendar-config.h \
#  utils.c \
#  utils.h \
#  subscribe.c \
#  subscribe.h \
#  acl.c \
#  acl.h

liborg_gnome_evolution_eee_la_LIBADD = \
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EVO3E_LIBS)

liborg_gnome_evolution_eee_la_LDFLAGS = \
  -module -avoid-version

module_eee_la_LIBADD = \
  $(top_builddir)/utils/libeeeutils.la \
  $(LIBXR_LIBS) \
  $(EVO3E_LIBS)

module_eee_la_LDFLAGS = \
  -module -avoid-version


