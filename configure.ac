AC_PREREQ([2.69])
AC_INIT([vkd3d], 0.0.0)

AC_CONFIG_AUX_DIR([bin])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([portable])
AC_CONFIG_HEADERS(include/config.h)

dnl Check for progs
AM_PROG_AR
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_SED
AC_PROG_MKDIR_P

AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_MAINTAINER_MODE([enable])

LT_PREREQ([2.4.2])
LT_INIT

dnl Check compiler specific flags
AC_SUBST(VKD3D_CLFAGS, "")
if test "x${GCC}" = "xyes"
then
    VKD3D_CFLAGS="-Wall -pipe"

    VKD3D_CHECK_CFLAGS([-std=c99])
    VKD3D_CHECK_CFLAGS([-Wdeclaration-after-statement])
    VKD3D_CHECK_CFLAGS([-Wmissing-prototypes])
    VKD3D_CHECK_CFLAGS([-Wunused-but-set-parameter])
    VKD3D_CHECK_CFLAGS([-Wvla])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
