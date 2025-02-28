#!/usr/bin/env bash

# default directories
vkd3d_lib32=${vkd3d_lib32:-"x86"}
vkd3d_lib64=${vkd3d_lib64:-"x64"}

# figure out where we are
basedir=$(dirname "$(readlink -f "$0")")

# figure out which action to perform
action="$1"

case "$action" in
install)
  ;;
uninstall)
  ;;
*)
  echo "Unrecognized action: $action"
  echo "Usage: $0 [install|uninstall] [--symlink]"
  exit 1
esac

# process arguments
shift

file_cmd="cp -v"

while (($# > 0)); do
  case "$1" in
  "--symlink")
    file_cmd="ln -s -v"
    ;;
  esac
  shift
done

# check wine prefix before invoking wine, so that we
# don't accidentally create one if the user screws up
if [ -n "$WINEPREFIX" ] && ! [ -f "$WINEPREFIX/system.reg" ]; then
  echo "$WINEPREFIX:"' Not a valid wine prefix.' >&2
  exit 1
fi

# find wine executable
export WINEDEBUG="${WINEDEBUG:--all}"
# disable mscoree and mshtml to avoid downloading
# wine gecko and mono
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-}${WINEDLLOVERRIDES:+,}mscoree,mshtml="

wine_path="$(which wine 2>/dev/null)"
wine64_path="$(which wine64 2>/dev/null)"

if [ -n "$wine_path" ] && [ -n "$wine64_path" ] && [ "$(dirname "$wine_path")" != "$(dirname "$wine64_path")" ]; then
    echo "Multiple Wine installations detected:"
    echo "1) $wine_path"
    echo "2) $wine64_path"

    while true; do
        read -p "Select the Wine binary to use (1 or 2): " choice
        case "$choice" in
          1) wine="wine"; break ;;
          2) wine="wine64"; break ;;
          *) echo "Invalid choice. Please enter 1 or 2." ;;
        esac
    done
else
    if [ -n "$wine_path" ]; then
      wine="wine"
    fi
    if [ -n "$wine64_path" ]; then
      wine="wine64"
    fi
fi

winever=$($wine --version | grep wine)
if [ -z "$winever" ]; then
    echo "$wine:"' Not a wine executable. Check your $wine.' >&2
    exit 1
fi
echo "Using: $winever"

wineboot="$wine wineboot"
win64=true
win32=true

# ensure wine placeholder dlls are recreated
# if they are missing
$wineboot -u

win64_sys_path="$($wine cmd /c '%SystemRoot%\system32\winepath.exe -u C:\windows\system32' 2>/dev/null)"
win64_sys_path="${win64_sys_path/$'\r'/}"

[ -z "$win64_sys_path" ] && win64=false

if grep --quiet -e '#arch=win32' "${WINEPREFIX:-$HOME/.wine}/system.reg"; then
  win32_sys_path=$win64_sys_path
  win64=false
  win32=true
else
  win32_sys_path="$($wine cmd /c '%SystemRoot%\syswow64\winepath.exe -u C:\windows\system32' 2>/dev/null)"
  win32_sys_path="${win32_sys_path/$'\r'/}"

  [ -z "$win32_sys_path" ] && win32=false
fi

if [ -z "$win32_sys_path" ] && [ -z "$win64_sys_path" ]; then
  echo 'Failed to resolve C:\windows\system32.' >&2
  exit 1
fi

# create native dll override
overrideDll() {
  if ! $wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$1" /d native /f >/dev/null 2>&1
  then
    echo -e "Failed to add override for $1"
    exit 1
  fi
}

# remove dll override
restoreDll() {
  if ! $wine reg delete 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$1" /f > /dev/null 2>&1
  then
    echo "Failed to remove override for $1"
  fi
}

# copy or link vkd3d dll, back up original file
installFile() {
  dstfile="${1}/${3}.dll"
  srcfile="${basedir}/${2}/${3}.dll"

  if ! [ -f "${srcfile}" ]; then
    echo "${srcfile}: File not found. Skipping." >&2
    return 1
  fi

  if [ -n "$1" ]; then
    if [ -f "${dstfile}" ] || [ -h "${dstfile}" ]; then
      if ! [ -f "${dstfile}.old" ]; then
        mv -v "${dstfile}" "${dstfile}.old"
      else
        rm -v "${dstfile}"
      fi
    else
      touch "${dstfile}.old_none"
    fi
    $file_cmd "${srcfile}" "${dstfile}"
  fi
  return 0
}

# remove vkd3d dll, restore original file
uninstallFile() {
  dstfile="${1}/${3}.dll"
  srcfile="${basedir}/${2}/${3}.dll"

  if ! [ -f "${srcfile}" ]; then
    echo "${srcfile}: File not found. Skipping." >&2
    return 1
  fi

  if ! [ -f "${dstfile}" ] && ! [ -h "${dstfile}" ]; then
    echo "${dstfile}: File not found. Skipping." >&2
    return 1
  fi

  if [ -f "${dstfile}.old" ]; then
    rm -v "${dstfile}"
    mv -v "${dstfile}.old" "${dstfile}"
    return 0
  elif [ -f "${dstfile}.old_none" ]; then
    rm -v "${dstfile}.old_none"
    rm -v "${dstfile}"
    return 0
  else
    return 1
  fi
}

install() {
  inst64_ret=-1
  if [ $win64 = "true" ]; then
    installFile "$win64_sys_path" "$vkd3d_lib64" "$1"
    inst64_ret="$?"
  fi

  inst32_ret=-1
  if [ $win32 = "true" ]; then
    installFile "$win32_sys_path" "$vkd3d_lib32" "$1"
    inst32_ret="$?"
  fi

  if (( ($inst32_ret == 0) || ($inst64_ret == 0) )); then
    overrideDll "$1"
  fi
}

uninstall() {
  uninst64_ret=-1
  if [ $win64 = "true" ]; then
    uninstallFile "$win64_sys_path" "$vkd3d_lib64" "$1"
    uninst64_ret="$?"
  fi

  uninst32_ret=-1
  if [ $win32 = "true" ]; then
    uninstallFile "$win32_sys_path" "$vkd3d_lib32" "$1"
    uninst32_ret="$?"
  fi

  if (( ($uninst32_ret == 0) || ($uninst64_ret == 0) )); then
    restoreDll "$1"
  fi
}

$action d3d12
$action d3d12core
