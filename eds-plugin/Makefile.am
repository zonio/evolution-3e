AM_CFLAGS = \
  -DG_LOG_DOMAIN=\"libecalbackend3e\" \
  $(LIBXR_CFLAGS) \
  $(EDS3E_CFLAGS) \
  -D_GNU_SOURCE \
  -I$(srcdir) \
  -I$(top_srcdir)

extension_LTLIBRARIES = libecalbackend3e.la

libecalbackend3e_la_SOURCES = \
  e-cal-backend-3e-factory.c \
  e-cal-backend-3e-factory.h \
  e-cal-backend-3e.c \
  e-cal-backend-3e.h

libecalbackend3e_la_LIBADD = \
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EDS3E_LIBS)

libecalbackend3e_la_LDFLAGS = \
  -module -avoid-version
