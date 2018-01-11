dnl VKD3D_CHECK_FUNC
AC_DEFUN([VKD3D_CHECK_FUNC],
[AC_MSG_CHECKING([for $2])
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return [$3]; }])],
               [AC_MSG_RESULT([yes])
               AC_DEFINE([$1],
                         [1],
                         [Define to 1 if you have $2.])],
               [AC_MSG_RESULT([no])])])
