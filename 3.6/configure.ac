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

localedir='$(prefix)/$(DATADIRNAME)/locale'
AC_SUBST(localedir)

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

PKG_CHECK_MODULES(LIBXR, [libxr >= 2.0])
AC_SUBST(LIBXR_CFLAGS)
AC_SUBST(LIBXR_LIBS)

#
# Get packages for EDS 3e backend
#

edsversion=`$PKG_CONFIG --variable=execversion evolution-data-server-1.2`

PKG_CHECK_MODULES(EVO3E, [
    libebackend-1.2 >= 3.6.0
    libedata-cal-1.2 >= 3.6.0
    libedataserver-1.2 >= 3.6.0
    libedataserverui-3.0 >= 3.6.0
    evolution-data-server-1.2 >= 3.6.0
    evolution-plugin-3.0 >= 3.6.0
])

AC_SUBST(EVO3E_CFLAGS)
AC_SUBST(EVO3E_LIBS)

XDL_COMPILER=xdl-compiler
AC_SUBST(XDL_COMPILER)

evoversion=`$PKG_CONFIG --variable=execversion evolution-shell-3.0`

if [[ "$evoversion" != "3.6" ]]; then
  AC_MSG_ERROR([Unsupported evolution version, you need 3.6 or higher.])
fi

ecal_backenddir=`$PKG_CONFIG --variable=backenddir libedata-cal-1.2`
plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin-3.0`
privlibdir=`$PKG_CONFIG --variable=privlibdir evolution-data-server-1.2`
evo_moduledir=`$PKG_CONFIG --variable=moduledir evolution-shell-3.0`
eds_moduledir=`$PKG_CONFIG --variable=moduledir libebackend-1.2`
errordir=`$PKG_CONFIG --variable=errordir evolution-plugin-3.0`

AC_SUBST(ecal_backenddir)
AC_SUBST(plugindir)
AC_SUBST(privlibdir)
AC_SUBST(evo_moduledir)
AC_SUBST(eds_moduledir)
AC_SUBST(errordir)

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
  collection/Makefile
  docs/Makefile
  po/Makefile.in
  utils/Makefile
  eds-plugin/Makefile
  evolution-plugin/Makefile
  evolution-plugin/org-gnome-evolution-eee.eplug
])

cat << EOF

Plugins configured:

  Evolution       = $evoversion
  EDS             = $edsversion

  plugindir       = $plugindir
  privlibdir      = $privlibdir
  evo_moduledir   = $evo_moduledir
  eds_moduledir   = $eds_moduledir
  errordir        = $errordir
  ecal_backenddir = $ecal_backenddir

EOF
