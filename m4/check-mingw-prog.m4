dnl VKD3D_CHECK_MINGW32_PROG(variable, crosstarget-variable, [value-if-not-found], [path])
AC_DEFUN([VKD3D_CHECK_MINGW32_PROG],
[
AC_SUBST([$2], [$3])dnl
vkd3d_mingw_list="m4_foreach([vkd3d_mingw_prefix], [w64-mingw32, pc-mingw32, mingw32, mingw32msvc],
    m4_foreach([vkd3d_cpu], [i686, i586, i486, i386], [vkd3d_cpu-vkd3d_mingw_prefix-gcc ]))
    mingw32-gcc"
AC_CHECK_PROGS([$1], [$vkd3d_mingw_list], [$3], [$4])
if test "x[$]$1" != x$3
then
    vkd3d_cc_saved="$CC"
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
    CC="$vkd3d_cc_saved"
fi
])

dnl VKD3D_CHECK_MINGW64_PROG(variable, crosstarget-variable, [value-if-not-found], [path])
AC_DEFUN([VKD3D_CHECK_MINGW64_PROG],
[
AC_SUBST([$2], [$3])dnl
vkd3d_mingw_list="m4_foreach([vkd3d_mingw_prefix], [pc-mingw32, w64-mingw32, mingw32msvc],
    m4_foreach([vkd3d_cpu], [x86_64, amd64], [vkd3d_cpu-vkd3d_mingw_prefix-gcc ]))"
AC_CHECK_PROGS([$1], [$vkd3d_mingw_list], [$3], [$4])
if test "x[$]$1" != x$3
then
    vkd3d_cc_saved="$CC"
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
    CC="$vkd3d_cc_saved"
fi
])
