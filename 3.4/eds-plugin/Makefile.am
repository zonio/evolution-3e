AM_CFLAGS = \
  -DG_LOG_DOMAIN=\"libecalbackend3e\" \
  $(LIBXR_CFLAGS) \
  $(EVO3E_CFLAGS) \
  -DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\" \
  -D_GNU_SOURCE \
  -I$(srcdir) \
  -I$(top_srcdir)

extension_LTLIBRARIES = libecalbackend3e.la

libecalbackend3e_la_SOURCES = \
  e-cal-backend-3e-factory.c \
  e-cal-backend-3e-factory.h \
  e-cal-backend-3e.c \
  e-cal-backend-3e.h \
  e-cal-backend-3e-priv.h \
  dns-txt-search.c \
  dns-txt-search.h \
  e-cal-backend-3e-utils.c \
  e-cal-backend-3e-sync.c \
  e-cal-backend-3e-attach.c

libecalbackend3e_la_LIBADD = \
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EDS3E_LIBS)

libecalbackend3e_la_LDFLAGS = \
  -module -avoid-version

images_DATA = \
	category_outofsync_16.png

EXTRA_DIST = \
	$(images_DATA)
