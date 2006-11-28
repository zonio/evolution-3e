AC_PREREQ(2.57)
AC_INIT([evolution-3e], 0.0.1, [oss@zonio.net], evolution-3e)
AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([dist-bzip2])
AM_MAINTAINER_MODE

# Static plugins are uesless
AM_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_HEADER_STDC

# Get packages for EDS 3e backend
PKG_CHECK_MODULES(EDS3E, [
  lib3es
  libecal-1.2
  libedata-cal-1.2
  libedataserver-1.2
  evolution-data-server-1.2
])

AC_SUBST(EDS3E_CFLAGS)
AC_SUBST(EDS3E_LIBS)

extensiondir=`$PKG_CONFIG --variable=extensiondir evolution-data-server-1.2`
AC_SUBST(extensiondir)

# Get packages for Evolution 3e plugin
PKG_CHECK_MODULES(EVO3E, [
  evolution-plugin-2.8
])

AC_SUBST(EVO3E_CFLAGS)
AC_SUBST(EVO3E_LIBS)

plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin-2.8`
AC_SUBST(plugindir)

SOEXT='.so'
AC_SUBST(SOEXT)

# Output
AC_OUTPUT([
	Makefile
	src/Makefile
	gui/Makefile
    gui/org-gnome-evolution-eee.eplug
])
