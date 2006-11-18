AC_PREREQ(2.57)
AC_INIT([evolution-3e], 0.0.1, [oss@zonio.net], evolution-3e)
AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([dist-bzip2])
AM_MAINTAINER_MODE

AM_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for pkg-config packages
PKG_CHECK_MODULES(EVO3E, [libxr >= 1.0 libecal-1.2 >= 1.2 libedata-cal-1.2 >= 1.2 libedataserver-1.2 >= 1.2 evolution-data-server-1.2])

AC_SUBST(EVO3E_CFLAGS)
AC_SUBST(EVO3E_LIBS)

extensiondir=`pkg-config --variable=extensiondir evolution-data-server-1.2`
AC_SUBST(extensiondir)

# Checks for header files.
AC_HEADER_STDC

# Output
AC_OUTPUT([
	Makefile
	src/Makefile
])
