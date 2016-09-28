dnl VKD3D_CHECK_SYNC_ADD_AND_FETCH_FUNC
AC_DEFUN([VKD3D_CHECK_SYNC_ADD_AND_FETCH_FUNC],
[AC_MSG_CHECKING([for __sync_add_and_fetch])
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __sync_add_and_fetch((int *)0, 0); }])],
               [AC_MSG_RESULT([yes])
               AC_DEFINE([HAVE_SYNC_ADD_AND_FETCH],
                         [1],
                         [Define to 1 if you have __sync_add_and_fetch.])],
               [AC_MSG_RESULT([no])])])

dnl VKD3D_CHECK_SYNC_SUB_AND_FETCH_FUNC
AC_DEFUN([VKD3D_CHECK_SYNC_SUB_AND_FETCH_FUNC],
[AC_MSG_CHECKING([for __sync_sub_and_fetch])
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __sync_sub_and_fetch((int *)0, 0); }])],
               [AC_MSG_RESULT([yes])
               AC_DEFINE([HAVE_SYNC_SUB_AND_FETCH],
                         [1],
                         [Define to 1 if you have __sync_sub_and_fetch.])],
               [AC_MSG_RESULT([no])])])
