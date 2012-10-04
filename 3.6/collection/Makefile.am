AM_CFLAGS = \
  -DG_LOG_DOMAIN=\"module-eee-backend\" \
  -DEXCHANGE_EEE_LOCALEDIR=\"$(localedir)\" \
  $(LIBXR_CFLAGS) \
  $(EVO3E_CFLAGS) \
  -D_GNU_SOURCE \
  -I$(srcdir) \
  -I$(top_srcdir) \
  -I$(top_srcdir)/utils

eds_module_LTLIBRARIES = module-eee-backend.la

module_eee_backend_la_SOURCES = \
  module-eee-backend.c

module_eee_backend_la_LIBADD = \
  $(top_builddir)/utils/libeeeutils.la \
  $(LIBXR_LIBS) \
  $(EDS3E_LIBS)

module_eee_backend_la_LDFLAGS = \
  -module -avoid-version
