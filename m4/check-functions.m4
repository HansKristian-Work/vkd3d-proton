dnl VKD3D_CHECK_FUNC
AC_DEFUN([VKD3D_CHECK_FUNC],
[AC_MSG_CHECKING([for $2])
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return [$3]; }])],
               [AC_MSG_RESULT([yes])
               AC_DEFINE([$1],
                         [1],
                         [Define to 1 if you have $2.])],
               [AC_MSG_RESULT([no])])])

dnl VKD3D_CHECK_LIB_FUNCS
AC_DEFUN([VKD3D_CHECK_LIB_FUNCS],
[vkd3d_libs_saved="$LIBS"
LIBS="$LIBS $2"
AC_CHECK_FUNCS([$1], [$3], [$4])
LIBS="$vkd3d_libs_saved"])
