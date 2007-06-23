BUILT_SOURCES = \
  ESCommon.c \
  ESClient.xrc.c \
  ESClient.c \
  ESAdmin.xrc.c \
  ESAdmin.c \
  ESCommon.h \
  ESClient.xrc.h \
  ESClient.h \
  ESAdmin.xrc.h \
  ESAdmin.h

CLEANFILES = \
  $(BUILT_SOURCES)

EXTRA_DIST = \
  $(BUILT_SOURCES) \
  server.xdl

AM_CFLAGS= \
  -std=gnu99 \
  $(LIBXR_CFLAGS) \
  -D_REENTRANT \
  -D_GNU_SOURCE

$(BUILT_SOURCES): $(srcdir)/server.xdl
	$(XDL_COMPILER) --mode=pub-impl -i $< -o .
	$(XDL_COMPILER) --mode=pub-headers -i $< -o .

noinst_LTLIBRARIES = \
  libinterface.la

libinterface_la_SOURCES = \
  ESCommon.c \
  ESClient.xrc.c \
  ESClient.c \
  ESAdmin.xrc.c \
  ESAdmin.c
