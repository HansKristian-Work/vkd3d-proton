AC_PREREQ([2.69])
AC_INIT([vkd3d], 0.0.0)

AC_CONFIG_AUX_DIR([bin])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([portable])
AC_CONFIG_HEADERS(include/config.h)

AC_ARG_VAR(WIDL, [widl IDL compiler])

dnl Check for progs
AM_PROG_AR
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_SED
AC_PROG_MKDIR_P
AC_CHECK_PROG(WIDL, [widl], [widl], [no])

if test "x$WIDL" = xno
then
    AC_MSG_ERROR([widl is required to build header files.])
fi

AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_MAINTAINER_MODE([enable])

LT_PREREQ([2.4.2])
LT_INIT

dnl Check compiler specific flags
AC_SUBST(VKD3D_CFLAGS, "")
if test "x${GCC}" = "xyes"
then
    VKD3D_CFLAGS="-Wall -pipe"

    VKD3D_CHECK_CFLAGS([-std=c99])
    VKD3D_CHECK_CFLAGS([-Wdeclaration-after-statement])
    VKD3D_CHECK_CFLAGS([-Wmissing-prototypes])
    VKD3D_CHECK_CFLAGS([-Wunused-but-set-parameter])
    VKD3D_CHECK_CFLAGS([-Wvla])
fi

dnl Check for headers
AC_CHECK_HEADERS([vulkan/vulkan.h], [], [AC_MSG_ERROR([vulkan.h not found.])])

dnl Check for libraries
AC_CHECK_LIB([vulkan], [vkGetInstanceProcAddr], [], [AC_MSG_ERROR([libvulkan not found.])])

dnl Check for __sync_add_and_fetch
AC_MSG_CHECKING([for __sync_add_and_fetch])
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([
        int main(void) { return __sync_add_and_fetch((int *)0, 0); }
    ])],
    [AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_SYNC_ADD_AND_FETCH], [1], [Define to 1 if you have __sync_add_and_fetch.])],
    [AC_MSG_RESULT([no])])

dnl Check for __sync_sub_and_fetch
AC_MSG_CHECKING([for __sync_sub_and_fetch])
AC_LINK_IFELSE(
    [AC_LANG_SOURCE([
        int main(void) { return __sync_sub_and_fetch((int *)0, 0); }
    ])],
    [AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_SYNC_SUB_AND_FETCH], [1], [Define to 1 if you have __sync_sub_and_fetch.])],
    [AC_MSG_RESULT([no])])

dnl Check for cross compilers
VKD3D_CHECK_MINGW32_PROG(CROSSCC32, CROSSTARGET32, false)
VKD3D_CHECK_MINGW64_PROG(CROSSCC64, CROSSTARGET64, false)

AM_CONDITIONAL([HAS_CROSSTARGET32], [test "x$CROSSTARGET32" != xfalse])
AM_CONDITIONAL([HAS_CROSSTARGET64], [test "x$CROSSTARGET64" != xfalse])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

dnl Output configuration info
echo ""
WIDL_VERSION=`$WIDL -V`
if test $? -eq 0
then
    WIDL_VERSION=`echo $WIDL_VERSION | cut -d' ' -f5`
fi
echo "  widl version: $WIDL_VERSION"

echo ""
echo "  Run '${MAKE-make}' to build vkD3D."
echo ""
