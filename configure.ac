AC_PREREQ(2.57)
AC_INIT([evolution-3e], 0.9.3, [3e@zhost.zonio.net], evolution-3e)
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

#
# libxr is required
#

PKG_CHECK_MODULES(LIBXR, [libxr >= 0.9.90])
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
  libedataserverui-1.2
  evolution-data-server-1.2
])

XDL_COMPILER=xdl-compiler
AC_SUBST(XDL_COMPILER)

AC_SUBST(EDS3E_CFLAGS)
AC_SUBST(EDS3E_LIBS)

extensiondir=`$PKG_CONFIG --variable=extensiondir evolution-data-server-1.2`
AC_SUBST(extensiondir)

#
# Get packages for Evolution 3e plugin
#

PKG_CHECK_EXISTS([evolution-plugin-2.10], [evoversion=2.10], [])
PKG_CHECK_EXISTS([evolution-plugin], [evoversion=2.12], [])

if [[ "$evoversion" = "2.10" ]]; then
  evoplugpkg=evolution-plugin-2.10
elif [[ "$evoversion" = "2.12" ]]; then
  evoplugpkg=evolution-plugin
else
  AC_MSG_ERROR([Unsupported evolution version, you need 2.10 or 2.12])
fi

PKG_CHECK_MODULES(EVO3E, [
  $evoplugpkg
  libglade-2.0
])

AC_SUBST(EVO3E_CFLAGS)
AC_SUBST(EVO3E_LIBS)

plugindir=`$PKG_CONFIG --variable=plugindir $evoplugpkg`
errordir=`$PKG_CONFIG --variable=errordir $evoplugpkg`
AC_SUBST(plugindir)
AC_SUBST(errordir)

#
# Misc
#

SOEXT='.so'
AC_SUBST(SOEXT)

CFLAGS="$CFLAGS -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable"

#
# Output
#

AC_OUTPUT([
  Makefile
  docs/Makefile
  interface/Makefile
  eds-plugin/Makefile
  evolution-plugin/Makefile
  patches/Makefile
  evolution-plugin/org-gnome-evolution-eee.eplug
  evolution-plugin/org-gnome-eee-cal-subscription.xml
])

cat << EOF

Plugins configured:

  Evolution     = $evoversion
  EDS           = $evoversion

  plugindir     = $plugindir
  errordir      = $errordir
  extensiondir  = $extensiondir

EOF
