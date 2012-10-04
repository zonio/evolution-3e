privlib_LTLIBRARIES = libeeeutils.la

BUILT_SOURCES = \
  ESCommon.c \
  ESClient.xrc.c \
  ESClient.c \
  ESCommon.h \
  ESClient.xrc.h \
  ESClient.h

CLEANFILES = \
  $(BUILT_SOURCES)

EXTRA_DIST = \
  $(BUILT_SOURCES) \
  server.xdl

AM_CFLAGS= \
  $(LIBXR_CFLAGS) \
  $(EVO3E_CFLAGS) \
  -D_REENTRANT \
  -D_GNU_SOURCE

$(BUILT_SOURCES): $(srcdir)/server.xdl
	$(XDL_COMPILER) --mode=pub-impl -i $< -o .
	$(XDL_COMPILER) --mode=pub-headers -i $< -o .

libeeeutils_la_SOURCES = \
  ESCommon.c \
  ESClient.xrc.c \
  ESClient.c \
  dns-txt-search.c \
  e-source-eee.c

libeeeutils_la_LDFLAGS = \
  -module -avoid-version
