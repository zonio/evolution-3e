AM_CFLAGS = \
  -DG_LOG_DOMAIN=\"libecalbackend3e\" \
  -DEXCHANGE_EEE_LOCALEDIR=\"$(localedir)\" \
  $(LIBXR_CFLAGS) \
  $(EVO3E_CFLAGS) \
  -D_GNU_SOURCE \
  -I$(srcdir) \
  -I$(top_srcdir) \
  -I$(top_srcdir)/utils

ecal_backend_LTLIBRARIES = libecalbackend3e.la

libecalbackend3e_la_SOURCES = \
  e-cal-backend-3e-factory.c \
  e-cal-backend-3e.c \
  e-cal-backend-3e.h

libecalbackend3e_la_LIBADD = \
  $(top_builddir)/utils/libeeeutils.la \
  $(LIBXR_LIBS) \
  $(EDS3E_LIBS)

libecalbackend3e_la_LDFLAGS = \
  -module -avoid-version
