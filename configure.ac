AC_PREREQ([2.69])
AC_INIT([vkd3d], 0.0.1)

AC_CONFIG_AUX_DIR([bin])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([portable])
AC_CONFIG_HEADERS(include/config.h)

AC_ARG_VAR([WIDL], [widl IDL compiler])
AC_ARG_VAR([GLSLANG], [glslangValidator GLSL compiler])
AC_ARG_VAR([CROSSCC32], [32-bit Windows cross compiler])
AC_ARG_VAR([CROSSCC64], [64-bit Windows cross compiler])
AC_ARG_WITH([spirv-tools], AS_HELP_STRING([--with-spirv-tools],
                                          [Build with SPIRV-Tools library (default: disabled)]))

dnl Check for progs
AM_PROG_AR
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_SED
AC_PROG_MKDIR_P
AC_CHECK_PROG([WIDL], [widl], [widl], [no])
AS_IF([test "x$WIDL" = "xno"], [AC_MSG_WARN([widl is required to build header files.])])
AC_CHECK_PROG([GLSLANG], [glslangValidator], [glslangValidator], [no])
AS_IF([test "x$GLSLANG" = "xno"], [AC_MSG_ERROR([glslangValidator is required to compile shaders.])])

AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects no-dist-gzip dist-xz -Wall -Werror])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

LT_PREREQ([2.4.2])
LT_INIT

dnl Check compiler specific flags
AC_SUBST([VKD3D_CFLAGS], [""])
AS_IF([test "x${GCC}" = "xyes"],
      [VKD3D_CFLAGS="-Wall -pipe"
      VKD3D_CHECK_CFLAGS([-std=c99])
      VKD3D_CHECK_CFLAGS([-Wdeclaration-after-statement])
      VKD3D_CHECK_CFLAGS([-Wmissing-prototypes])
      VKD3D_CHECK_CFLAGS([-Wunused-but-set-parameter])
      VKD3D_CHECK_CFLAGS([-Wvla])
      VKD3D_CHECK_CFLAGS([-Wl,--no-undefined])])

AC_EGREP_CPP(yes,
            [#define _POSIX_C_SOURCE 200809L
             #include <unistd.h>
             #ifdef _POSIX_VERSION
             #if _POSIX_VERSION == 200809L
             yes
             #endif
             #endif],
             [AC_DEFINE([_POSIX_C_SOURCE], [200809L], [Define the POSIX version.])],
             [])

dnl Check for cross compilers
VKD3D_CHECK_MINGW32_PROG([CROSSCC32], [CROSSTARGET32], [no])
VKD3D_CHECK_MINGW64_PROG([CROSSCC64], [CROSSTARGET64], [no])

dnl Check for headers
AC_CHECK_HEADERS([pthread.h vulkan/vulkan.h spirv/1.0/spirv.h spirv/1.0/GLSL.std.450.h])
AS_IF([test "x$ac_cv_header_pthread_h" != "xyes"], [AC_MSG_ERROR([pthread.h not found.])])
AS_IF([test "x$ac_cv_header_vulkan_vulkan_h" != "xyes"], [AC_MSG_ERROR([vulkan.h not found.])])
AS_IF([test "x$ac_cv_header_spirv_1_0_spirv_h" != "xyes"], [AC_MSG_ERROR([spirv.h not found.])])
AS_IF([test "x$ac_cv_header_spirv_1_0_GLSL_std_450_h" != "xyes"], [AC_MSG_ERROR([GLSL.std.450.h not found.])])

AS_IF([test "x$with_spirv_tools" = "xyes"],
      [AC_CHECK_HEADERS([spirv-tools/libspirv.h], [], [AC_MSG_ERROR([spirv-tools/libspirv.h not found.])])])

dnl Check for libraries
m4_ifdef([PKG_PROG_PKG_CONFIG], [PKG_PROG_PKG_CONFIG], [m4_fatal([pkg-config autoconf macros not found.])])

AC_CHECK_LIB([m], [ceilf])

AC_ARG_VAR([PTHREAD_LIBS], [linker flags for pthreads])
AC_CHECK_LIB([pthread], [pthread_create],
             [AC_SUBST([PTHREAD_LIBS], ["-lpthread"])],
             [AC_MSG_ERROR([libpthread not found.])])

AC_SUBST([VULKAN_LIBS])
AC_CHECK_LIB([vulkan], [vkGetInstanceProcAddr],
             [VULKAN_LIBS="-lvulkan"],
             [AC_MSG_ERROR([libvulkan not found.])])

AC_SUBST([SPIRV_TOOLS_LIBS])
AS_IF([test "x$with_spirv_tools" = "xyes"],
      [AC_CHECK_LIB([SPIRV-Tools], [spvTextToBinary],
                    [SPIRV_TOOLS_LIBS="-lstdc++ -lSPIRV-Tools"],
                    [AC_MSG_ERROR([libSPIRV-Tools not found.])], [-lstdc++])])

PKG_CHECK_MODULES([XCB], [xcb xcb-keysyms])

dnl Check for functions
VKD3D_CHECK_SYNC_ADD_AND_FETCH_FUNC
VKD3D_CHECK_SYNC_SUB_AND_FETCH_FUNC

AM_CONDITIONAL([HAS_CROSSTARGET32], [test "x$CROSSTARGET32" != "xno"])
AM_CONDITIONAL([HAS_CROSSTARGET64], [test "x$CROSSTARGET64" != "xno"])

AS_IF([test "x$ac_cv_header_spirv_tools_libspirv_h" = "xyes" -a "x$ac_cv_lib_SPIRV_Tools_spvTextToBinary" = "xyes"],
      [AC_DEFINE([HAVE_SPIRV_TOOLS], [1], [Define to 1 if you have SPIRV-Tools.])
      HAVE_SPIRV_TOOLS=yes],
      [HAVE_SPIRV_TOOLS=no])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

dnl Output configuration summary
AS_IF([test "x$CROSSTARGET32" != "xno" -o "x$CROSSTARGET64" != "xno"],
      [HAVE_CROSSTEST=yes], [HAVE_CROSSTEST=no])

AS_ECHO(["
  Configuration summary for $PACKAGE $VERSION

  Have SPIRV-Tools: ${HAVE_SPIRV_TOOLS}

  Building crosstests: ${HAVE_CROSSTEST}"])
AS_IF([test "x$CROSSTARGET32" != "xno"], [AS_ECHO(["    Using 32-bit cross compiler: $CROSSCC32"])])
AS_IF([test "x$CROSSTARGET64" != "xno"], [AS_ECHO(["    Using 64-bit cross compiler: $CROSSCC64"])])
AS_ECHO([])
