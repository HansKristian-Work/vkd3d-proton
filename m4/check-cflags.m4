dnl VKD3D_CHECK_CFLAGS(flags)
AC_DEFUN([VKD3D_CHECK_CFLAGS],
[AS_VAR_PUSHDEF([ac_var], ac_cv_cflags_[[$1]])dnl
AC_CACHE_CHECK([whether the compiler supports $1], ac_var,
[ac_vkd3d_check_cflags_saved=$CFLAGS
CFLAGS="$CFLAGS $1 -Werror"
AC_LINK_IFELSE([AC_LANG_SOURCE([[int main(int argc, char **argv) { return 0; }]])],
               [AS_VAR_SET(ac_var, yes)], [AS_VAR_SET(ac_var, no)])
CFLAGS=$ac_vkd3d_check_cflags_saved])
AS_VAR_IF([ac_var], [yes], [VKD3D_CFLAGS="$VKD3D_CFLAGS $1"])dnl
AS_VAR_POPDEF([ac_var])])
