AC_PREREQ(2.57)
AC_INIT([evolution-3e-plugin], 1.0, [3e-list@zonio.net], evolution-3e-plugin)
AC_CONFIG_SRCDIR([Makefile.am])
AC_PROG_RANLIB
AM_CONFIG_HEADER([config.h])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([dist-bzip2 tar-ustar])
AM_MAINTAINER_MODE

IT_PROG_INTLTOOL([0.35.5])
AM_GLIB_GNU_GETTEXT
GETTEXT_PACKAGE=evolution-3e-plugin
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

PROGRAMNAME_LOCALEDIR=[${datadir}/locale]
AC_SUBST(PROGRAMNAME_LOCALEDIR)

# Static plugins are uesless
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_HEADER_STDC

#
# libxr is required
#

PKG_CHECK_MODULES(LIBXR, [libxr >= 1.0])
AC_SUBST(LIBXR_CFLAGS)
AC_SUBST(LIBXR_LIBS)

#
# Get packages for EDS 3e backend
#

edsversion=`$PKG_CONFIG --variable=execversion evolution-data-server-1.2`

PKG_CHECK_MODULES(EDS3E, [
    libecal-1.2
    libedata-cal-1.2
    libedataserver-1.2
    libedataserverui-3.0
    evolution-data-server-1.2 >= 1.12.0
    evolution-plugin-3.0 >= 2.12.0
])

AC_SUBST(EDS3E_CFLAGS)
AC_SUBST(EDS3E_LIBS)

XDL_COMPILER=xdl-compiler
AC_SUBST(XDL_COMPILER)

#
# Get packages for Evolution 3e plugin
#

extensiondir=`$PKG_CONFIG --variable=backenddir libedata-cal-1.2`
evoversion=`$PKG_CONFIG --variable=execversion evolution-shell-3.0`

AC_SUBST(extensiondir)

if [[ "$evoversion" = "3.2" ]]; then
  EVO_VERSION=320
else
  AC_MSG_ERROR([Unsupported evolution version, you need 3.2 or higher.])
fi

PKG_CHECK_MODULES(EVO3E, [
  libglade-2.0
  evolution-plugin-3.0 >= 2.12.0
])

AC_SUBST(EVO3E_CFLAGS)
AC_SUBST(EVO3E_LIBS)

plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin-3.0`
errordir=`$PKG_CONFIG --variable=errordir evolution-plugin-3.0`

AC_SUBST(plugindir)
AC_SUBST(errordir)

AC_DEFINE_UNQUOTED(EVOLUTION_VERSION, $EVO_VERSION, [Evolution version code])

#
# Misc
#

SOEXT='.so'
AC_SUBST(SOEXT)

CFLAGS="$CFLAGS -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -DHANDLE_LIBICAL_MEMORY=1"

#
# Output
#

AC_OUTPUT([
  Makefile
  docs/Makefile
  po/Makefile.in
  interface/Makefile
  eds-plugin/Makefile
  evolution-plugin/Makefile
  evolution-plugin/org-gnome-evolution-eee.eplug
  evolution-plugin/org-gnome-eee-cal-subscription.xml
])

cat << EOF

Plugins configured:

  Evolution     = $evoversion
  EDS           = $edsversion

  plugindir     = $plugindir
  errordir      = $errordir
  extensiondir  = $extensiondir

EOF
