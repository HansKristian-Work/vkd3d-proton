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

AS_IF([test "x$WIDL" = "xno"], [AC_MSG_ERROR([widl is required to build header files.])])

AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

LT_PREREQ([2.4.2])
LT_INIT

dnl Check compiler specific flags
AC_SUBST(VKD3D_CFLAGS, "")
AS_IF([test "x${GCC}" = "xyes"],
      [VKD3D_CFLAGS="-Wall -pipe"
      VKD3D_CHECK_CFLAGS([-std=c99])
      VKD3D_CHECK_CFLAGS([-Wdeclaration-after-statement])
      VKD3D_CHECK_CFLAGS([-Wmissing-prototypes])
      VKD3D_CHECK_CFLAGS([-Wunused-but-set-parameter])
      VKD3D_CHECK_CFLAGS([-Wvla])])

dnl Check for cross compilers
VKD3D_CHECK_MINGW32_PROG([CROSSCC32], [CROSSTARGET32], [false])
VKD3D_CHECK_MINGW64_PROG([CROSSCC64], [CROSSTARGET64], [false])

dnl Check for headers
AC_CHECK_HEADERS([pthread.h vulkan/vulkan.h])
AS_IF([test "x$ac_cv_header_pthread_h" != "xyes"], [AC_MSG_ERROR([pthread.h not found.])])
AS_IF([test "x$ac_cv_header_vulkan_vulkan_h" != "xyes"], [AC_MSG_ERROR([vulkan.h not found.])])

dnl Check for libraries
AC_ARG_VAR([PTHREAD_LIBS], [linker flags for pthreads])
AC_CHECK_LIB([pthread], [pthread_create],
             [AC_SUBST(PTHREAD_LIBS, "-lpthread")],
             [AC_MSG_ERROR(libpthread not found.)])

AC_CHECK_LIB([vulkan], [vkGetInstanceProcAddr], [], [AC_MSG_ERROR([libvulkan not found.])])

dnl Check for functions
VKD3D_CHECK_SYNC_ADD_AND_FETCH_FUNC
VKD3D_CHECK_SYNC_SUB_AND_FETCH_FUNC

AM_CONDITIONAL([HAS_CROSSTARGET32], [test "x$CROSSTARGET32" != "xfalse"])
AM_CONDITIONAL([HAS_CROSSTARGET64], [test "x$CROSSTARGET64" != "xfalse"])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

dnl Output configuration info
echo ""
WIDL_VERSION=`$WIDL -V`
AS_IF([test $? -eq 0], [WIDL_VERSION=`echo $WIDL_VERSION | cut -d' ' -f5`])
echo "  widl version: $WIDL_VERSION"

echo ""
echo "  Run '${MAKE-make}' to build vkD3D."
echo ""
