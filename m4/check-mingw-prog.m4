dnl VKD3D_CHECK_MINGW32_PROG(variable, crosstarget-variable, [value-if-not-found], [path])
AC_DEFUN([VKD3D_CHECK_MINGW32_PROG],
[
AC_SUBST([$2], [$3])dnl
ac_prefix_list="m4_foreach([ac_vkd3d_prefix], [w64-mingw32, pc-mingw32, mingw32msvc, mingw32],
    m4_foreach([ac_vkd3d_cpu], [i686, i586, i486, i386], [ac_vkd3d_cpu-ac_vkd3d_prefix-gcc ]))
    mingw32-gcc"
AC_CHECK_PROGS([$1], [$ac_prefix_list], [$3], [$4])
if test "x[$]$1" != x$3
then
    ac_vkd3d_save_CC="$CC"
    CC="[$]$1"
    AC_MSG_CHECKING([whether $CC works])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
                      [AC_MSG_RESULT([yes])
                      set x [$]$1
                      shift
                      while test "[$]#" -ge 1
                      do
                            case "[$]1" in
                                *-gcc) $2=`expr "[$]1" : '\(.*\)-gcc'` ;;
                            esac
                            shift
                      done],
                      [AC_MSG_RESULT([no])])
    CC="$ac_vkd3d_save_CC"
fi
])

dnl VKD3D_CHECK_MINGW64_PROG(variable, crosstarget-variable, [value-if-not-found], [path])
AC_DEFUN([VKD3D_CHECK_MINGW64_PROG],
[
AC_SUBST([$2], [$3])dnl
ac_prefix_list="m4_foreach([ac_vkd3d_prefix], [pc-mingw32, w64-mingw32, mingw32msvc],
    m4_foreach([ac_vkd3d_cpu], [x86_64, amd64], [ac_vkd3d_cpu-ac_vkd3d_prefix-gcc ]))"
AC_CHECK_PROGS([$1], [$ac_prefix_list], [$3], [$4])
if test "x[$]$1" != x$3
then
    ac_vkd3d_save_CC="$CC"
    CC="[$]$1"
    AC_MSG_CHECKING([whether $CC works])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
                      [AC_MSG_RESULT([yes])
                      set x [$]$1
                      shift
                      while test "[$]#" -ge 1
                      do
                            case "[$]1" in
                                *-gcc) $2=`expr "[$]1" : '\(.*\)-gcc'` ;;
                            esac
                            shift
                      done],
                      [AC_MSG_RESULT([no])])
    CC="$ac_vkd3d_save_CC"
fi
])
