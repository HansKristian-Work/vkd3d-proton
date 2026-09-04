/*
 * Copyright 2020 Joshua Ashton for Valve Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "vkd3d_platform.h"

#include <assert.h>
#include <stdio.h>

static bool vkd3d_parse_linux_release(const char *release, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    if (sscanf(release, "%u.%u.%u", major, minor, patch) == 3)
    {
        return true;
    }
    else if (sscanf(release, "%u.%u", major, minor) == 2)
    {
        *patch = 0;
        return true;
    }
    else
        return false;
}

#if defined(__linux__)

# include <dlfcn.h>
# include <errno.h>
# include <sys/utsname.h>

vkd3d_module_t vkd3d_dlopen(const char *name)
{
    return dlopen(name, RTLD_NOW);
}

void *vkd3d_dlsym(vkd3d_module_t handle, const char *symbol)
{
    return dlsym(handle, symbol);
}

int vkd3d_dlclose(vkd3d_module_t handle)
{
    return dlclose(handle);
}

const char *vkd3d_dlerror(void)
{
    return dlerror();
}

bool vkd3d_get_program_name(char program_name[VKD3D_PATH_MAX])
{
    char *name, *p, *real_path = NULL;

#ifdef __ANDROID__
    name = getprogname();
#else
    if ((name = strrchr(program_invocation_name, '/')))
    {
        real_path = realpath("/proc/self/exe", NULL);

        /* Try to strip command line arguments. */
        if (real_path && (p = strrchr(real_path, '/'))
                && !strncmp(real_path, program_invocation_name, strlen(real_path)))
        {
            name = p;
        }

        ++name;
    }
    else if ((name = strrchr(program_invocation_name, '\\')))
    {
        ++name;
    }
    else
    {
        name = program_invocation_name;
    }
#endif

    strncpy(program_name, name, VKD3D_PATH_MAX);
    program_name[VKD3D_PATH_MAX - 1] = '\0';
    free(real_path);
    return true;
}

bool vkd3d_get_linux_kernel_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    struct utsname ver;
    if (uname(&ver) < 0)
        return false;
    if (strcmp(ver.sysname, "Linux") != 0)
        return false;

    return vkd3d_parse_linux_release(ver.release, major, minor, patch);
}

bool vkd3d_get_ue_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = *minor = *patch = 0;
    return false;
}

#elif defined(_WIN32)

# include <windows.h>
# include <pathcch.h>

vkd3d_module_t vkd3d_dlopen(const char *name)
{
    return LoadLibraryA(name);
}

void *vkd3d_dlsym(vkd3d_module_t handle, const char *symbol)
{
    return GetProcAddress(handle, symbol);
}

int vkd3d_dlclose(vkd3d_module_t handle)
{
    FreeLibrary(handle);
    return 0;
}

const char *vkd3d_dlerror(void)
{
    return "Not implemented for this platform.";
}

bool vkd3d_get_program_name(char program_name[VKD3D_PATH_MAX])
{
    char *name;
    char exe_path[VKD3D_PATH_MAX];
    GetModuleFileNameA(NULL, exe_path, VKD3D_PATH_MAX);

    if ((name = strrchr(exe_path, '/')))
    {
        ++name;
    }
    else if ((name = strrchr(exe_path, '\\')))
    {
        ++name;
    }
    else
    {
        name = exe_path;
    }

    strncpy(program_name, name, VKD3D_PATH_MAX);
    return true;
}

bool vkd3d_get_linux_kernel_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    void (*get_version)(const char **, const char **);
    const char *release = NULL;
    const char *kernel = NULL;
    HMODULE ntdll;

    ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return false;

    get_version = (void *)GetProcAddress(ntdll, "wine_get_host_version");
    if (!get_version)
        return false;

    if (get_version)
        get_version(&kernel, &release);

    if (kernel && strcmp(kernel, "Linux") == 0 && release)
        return vkd3d_parse_linux_release(release, major, minor, patch);
    else
        return false;
}

static bool get_ue_version_from_exe(const WCHAR *path, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    VS_FIXEDFILEINFO *fi;
    DWORD *translation;
    bool ret = false;
    char buf[64];
    void *block;
    UINT size;
    char *s;

    if (!(size = GetFileVersionInfoSizeW(path, NULL)))
        return false;
    if (!(block = malloc(size)))
        return false;
    if (!GetFileVersionInfoW(path, 0, size, block))
        goto done;

    if (!VerQueryValueA(block, "\\", (void **)&fi, &size) || size != sizeof(VS_FIXEDFILEINFO))
        goto done;
    if (!VerQueryValueA(block, "\\VarFileInfo\\Translation", (void **)&translation, &size) || size != 4)
        goto done;

    sprintf(buf, "\\StringFileInfo\\%08lx\\InternalName", MAKELONG(HIWORD(*translation), LOWORD(*translation)));
    if (VerQueryValueA(block, buf, (void **)&s, &size) && !strcmp(s, "UnrealEngine"))
    {
        ret = true;
        goto done;
    }
    sprintf(buf, "\\StringFileInfo\\%08lx\\ProductName", MAKELONG(HIWORD(*translation), LOWORD(*translation)));
    if (VerQueryValueA(block, buf, (void **)&s, &size) && (!strcmp(s, "UnrealEngine") || !strcmp(s, "Unreal Engine")))
        ret = true;

done:
    if (ret)
    {
        *major = HIWORD(fi->dwProductVersionMS);
        *minor = LOWORD(fi->dwProductVersionMS);
        *patch = HIWORD(fi->dwProductVersionLS);
    }
    free(block);
    return ret;
}

bool vkd3d_get_ue_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    WCHAR exe_path[VKD3D_PATH_MAX], path[VKD3D_PATH_MAX];

    *major = *minor = *patch = 0;
    GetModuleFileNameW(NULL, exe_path, VKD3D_PATH_MAX);
    if (get_ue_version_from_exe(exe_path, major, minor, patch))
        return true;

    PathCchRemoveFileSpec(exe_path, ARRAY_SIZE(exe_path));

    if (FAILED(PathCchCombineEx(path, ARRAY_SIZE(path), exe_path,
            L"..\\..\\..\\Engine\\Binaries\\Win64\\CrashReportClient.exe", PATHCCH_NONE)))
        return false;
    if (get_ue_version_from_exe(path, major, minor, patch))
        return true;

    if (FAILED(PathCchCombineEx(path, ARRAY_SIZE(path), exe_path,
            L"..\\..\\..\\Engine\\Binaries\\Win64\\UnrealCEFSubProcess.exe", PATHCCH_NONE)))
        return false;
    return get_ue_version_from_exe(path, major, minor, patch);
}

#else

vkd3d_module_t vkd3d_dlopen(const char *name)
{
    FIXME("Not implemented for this platform.\n");
    return NULL;
}

void *vkd3d_dlsym(vkd3d_module_t handle, const char *symbol)
{
    return NULL;
}

int vkd3d_dlclose(vkd3d_module_t handle)
{
    return 0;
}

const char *vkd3d_dlerror(void)
{
    return "Not implemented for this platform.";
}

bool vkd3d_get_program_name(char program_name[VKD3D_PATH_MAX])
{
    *program_name = '\0';
    return false;
}

bool vkd3d_get_linux_kernel_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = 0;
    *minor = 0;
    *patch = 0;
    return false;
}

bool vkd3d_get_ue_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = *minor = *patch = 0;
    return false;
}

#endif

#if defined(_WIN32)

bool vkd3d_get_env_var(const char *name, char *value, size_t value_size)
{
    DWORD len;
    
    assert(value);
    assert(value_size > 0);

    len = GetEnvironmentVariableA(name, value, value_size);
    if (len > 0 && len <= value_size)
    {
        return true;
    }

    value[0] = '\0';
    return false;
}

#else

bool vkd3d_get_env_var(const char *name, char *value, size_t value_size)
{
    const char *env_value;

    assert(value);
    assert(value_size > 0);

    if ((env_value = getenv(name)))
    {
        snprintf(value, value_size, "%s", env_value);
        return true;
    }

    value[0] = '\0';
    return false;
}

#endif
