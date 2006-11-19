INCLUDES = \
	-DG_LOG_DOMAIN=\"libecalbackend3e\" \
	$(EDS3E_CFLAGS)

extension_LTLIBRARIES = libecalbackend3e.la

libecalbackend3e_la_SOURCES = \
	e-cal-backend-3e-factory.c \
	e-cal-backend-3e-factory.h \
	e-cal-backend-3e.c \
	e-cal-backend-3e.h

libecalbackend3e_la_LIBADD = \
	$(EDS3E_LIBS)

libecalbackend3e_la_LDFLAGS = \
	-module -avoid-version
