AM_CFLAGS = \
  $(EVO3E_CFLAGS) \
  -DPLUGINDIR=\""$(plugindir)"\" \
  $(LIBXR_CFLAGS) \
  -DPROGRAMNAME_LOCALEDIR=\"${PROGRAMNAME_LOCALEDIR}\" \
  -I$(top_srcdir)

#plugin_DATA = \
#  org-gnome-evolution-eee.eplug \
#  org-gnome-evolution-eee.glade

error_DATA = \
  org-gnome-evolution-eee.error

#plugin_LTLIBRARIES = \
#  liborg-gnome-evolution-eee.la

module_LTLIBRARIES = \
  module-eee.la

module_eee_la_SOURCES = \
  module-eee-backend.c \
  evolution-cal-config-eee.c \
  e-mail-config-eee-summary.c \
  e-mail-config-eee-summary.h \
  dns-txt-search.c \
  dns-txt-search.h

liborg_gnome_evolution_eee_la_SOURCES = \
  dns-txt-search.c \
  dns-txt-search.h
#  evolution-cal-config-eee.c \
#  e-mail-config-eee-summary.c \
#  e-mail-config-eee-summary.h \
#  eee-account.c \
#  eee-account.h \
#  eee-accounts-manager.c \
#  eee-accounts-manager.h \
#  eee-calendar-config.c \
#  eee-calendar-config.h \
#  dns-txt-search.c \
#  dns-txt-search.h \
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
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EVO3E_LIBS)

module_eee_la_LDFLAGS = \
  -module -avoid-version


