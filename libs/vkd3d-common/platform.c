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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API
#include "vkd3d_debug.h"

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

enum vkd3d_application_engine_class vkd3d_get_engine_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = *minor = *patch = 0;
    return VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
}

#elif defined(_WIN32)

# include <windows.h>
# include <pathcch.h>
# include <psapi.h>
# include <wchar.h>

#ifdef _MSC_VER
/* This struct is exposed on MinGW, but not MSVC.
 * It's not really a public API ... */
typedef enum _PROCESSINFOCLASS
{
    ProcessBasicInformation,
} PROCESSINFOCLASS;

typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    void *PebBaseAddress;
    KAFFINITY AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;
#else
# include <winternl.h>
#endif

typedef NTSTATUS (* NTAPI PFN_NtQueryInformationProcess)(
    HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength);

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

static enum vkd3d_application_engine_class get_engine_version_from_exe(
        const WCHAR *path, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    enum vkd3d_application_engine_class engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
    VS_FIXEDFILEINFO *fi;
    DWORD *translation;
    const void *block;
    WCHAR buf[64], *s;
    HMODULE hmod;
    HGLOBAL hmem;
    HRSRC hrsrc;
    UINT size;

    if (!(hmod = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE)))
        return VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;

    if (!(hrsrc = FindResourceW(hmod, MAKEINTRESOURCEW(VS_VERSION_INFO), (LPCWSTR)RT_VERSION)) ||
        !(hmem = LoadResource(hmod, hrsrc)) ||
        !(block = LockResource(hmem)))
        goto done;

    if (!VerQueryValueW(block, L"\\", (void **)&fi, &size) || size != sizeof(VS_FIXEDFILEINFO))
        goto done;
    if (!VerQueryValueW(block, L"\\VarFileInfo\\Translation", (void **)&translation, &size) || size != 4)
        goto done;

    swprintf(buf, ARRAY_SIZE(buf), L"\\StringFileInfo\\%08lx\\InternalName",
            MAKELONG(HIWORD(*translation), LOWORD(*translation)));
    if (VerQueryValueW(block, buf, (void **)&s, &size))
    {
        TRACE("InternalName: %s\n", debugstr_w(s));
        if (!wcscmp(s, L"UnrealEngine"))
        {
            engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE;
            goto done;
        }
    }

    swprintf(buf, ARRAY_SIZE(buf), L"\\StringFileInfo\\%08lx\\ProductName",
            MAKELONG(HIWORD(*translation), LOWORD(*translation)));
    if (VerQueryValueW(block, buf, (void **)&s, &size))
    {
        TRACE("ProductName: %s\n", debugstr_w(s));
        if (!wcscmp(s, L"UnrealEngine") || !wcscmp(s, L"Unreal Engine"))
            engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE;
    }

done:
    if (engine_class)
    {
        *major = HIWORD(fi->dwProductVersionMS);
        *minor = LOWORD(fi->dwProductVersionMS);
        *patch = HIWORD(fi->dwProductVersionLS);
    }
    FreeLibrary(hmod);
    return engine_class;
}

static bool has_unreal_content_paks(const WCHAR *dir)
{
    WCHAR search_path[VKD3D_PATH_MAX];
    WCHAR pak_path[VKD3D_PATH_MAX];
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle;

    /* Check direct <dir>\Content\Paks */
    if (SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
            L"Content\\Paks", PATHCCH_NONE)))
    {
        if (GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES)
            return true;
    }

    /* Check if <dir>\Engine exists alongside a project subfolder containing Content\Paks.
     * When launching from a non-standard subfolder (e.g. platform\steam\PBZ.exe),
     * an ancestor directory is the game root containing Engine\ and <ProjectName>\Content\Paks. */
    if (SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
            L"Engine", PATHCCH_NONE)) &&
        GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES)
    {
        /* Engine\Binaries alongside Engine\Content or Engine\Plugins is also characteristic. */
        if (SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
                L"Engine\\Binaries", PATHCCH_NONE)) &&
            GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES)
        {
            if ((SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
                    L"Engine\\Content", PATHCCH_NONE)) &&
                GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES) ||
                (SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
                    L"Engine\\Plugins", PATHCCH_NONE)) &&
                GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES))
            {
                return true;
            }
        }

        if (FAILED(PathCchCombineEx(search_path, ARRAY_SIZE(search_path), dir,
                L"*", PATHCCH_NONE)))
            return false;

        find_handle = FindFirstFileW(search_path, &find_data);
        if (find_handle == INVALID_HANDLE_VALUE)
            return false;

        do
        {
            if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(find_data.cFileName, L".") &&
                wcscmp(find_data.cFileName, L".."))
            {
                if (SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), dir,
                        find_data.cFileName, PATHCCH_NONE)) &&
                    SUCCEEDED(PathCchCombineEx(pak_path, ARRAY_SIZE(pak_path), pak_path,
                        L"Content\\Paks", PATHCCH_NONE)))
                {
                    if (GetFileAttributesW(pak_path) != INVALID_FILE_ATTRIBUTES)
                    {
                        FindClose(find_handle);
                        return true;
                    }
                }
            }
        } while (FindNextFileW(find_handle, &find_data));

        FindClose(find_handle);
    }

    return false;
}

enum vkd3d_application_engine_class vkd3d_get_engine_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    WCHAR exe_path[VKD3D_PATH_MAX], cur_dir[VKD3D_PATH_MAX], path[VKD3D_PATH_MAX];
    enum vkd3d_application_engine_class engine_class;
    HMODULE ntdll;
    DWORD size;

    *major = *minor = *patch = 0;
    GetModuleFileNameW(NULL, exe_path, VKD3D_PATH_MAX);
    if ((engine_class = get_engine_version_from_exe(exe_path, major, minor, patch)))
        return engine_class;
    PathCchRemoveFileSpec(exe_path, ARRAY_SIZE(exe_path));

    /* Check ancestor directories for Epic binaries that retain engine version metadata.
     * The executable may be in the game root, a subfolder like platform\steam,
     * or the standard <ProjectName>\Binaries\Win64. */
    wcscpy(cur_dir, exe_path);
    for (unsigned int depth = 0; depth < 5; ++depth)
    {
        if (SUCCEEDED(PathCchCombineEx(path, ARRAY_SIZE(path), cur_dir,
                L"Engine\\Binaries\\Win64\\CrashReportClient.exe", PATHCCH_NONE)))
        {
            if ((engine_class = get_engine_version_from_exe(path, major, minor, patch)))
                return engine_class;
        }

        if (SUCCEEDED(PathCchCombineEx(path, ARRAY_SIZE(path), cur_dir,
                L"Engine\\Binaries\\Win64\\UnrealCEFSubProcess.exe", PATHCCH_NONE)))
        {
            if ((engine_class = get_engine_version_from_exe(path, major, minor, patch)))
                return engine_class;
        }

        if (SUCCEEDED(PathCchCombineEx(path, ARRAY_SIZE(path), cur_dir,
                L"Engine\\Binaries\\Win32\\CrashReportClient.exe", PATHCCH_NONE)))
        {
            if ((engine_class = get_engine_version_from_exe(path, major, minor, patch)))
                return engine_class;
        }

        if (SUCCEEDED(PathCchCombineEx(path, ARRAY_SIZE(path), cur_dir,
                L"Engine\\Binaries\\Win32\\UnrealCEFSubProcess.exe", PATHCCH_NONE)))
        {
            if ((engine_class = get_engine_version_from_exe(path, major, minor, patch)))
                return engine_class;
        }

        if (FAILED(PathCchRemoveFileSpec(cur_dir, ARRAY_SIZE(cur_dir))) || !cur_dir[0])
            break;
    }

    if ((ntdll = GetModuleHandleA("ntdll.dll")))
    {
        PFN_NtQueryInformationProcess pfn_NtQueryInformationProcess;
        PROCESS_BASIC_INFORMATION pbi;
        HANDLE parent;

        pfn_NtQueryInformationProcess =
            (PFN_NtQueryInformationProcess)(void *)GetProcAddress(ntdll, "NtQueryInformationProcess");

        if (pfn_NtQueryInformationProcess &&
            !pfn_NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL) &&
           (parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pbi.InheritedFromUniqueProcessId)))
        {
            size = ARRAY_SIZE(path);
            if (QueryFullProcessImageNameW(parent, 0, path, &size))
                engine_class = get_engine_version_from_exe(path, major, minor, patch);
            CloseHandle(parent);
            if (engine_class)
                return engine_class;
        }
    }

    /* Dubious checks where we spelunk the filesystem across ancestor directories. */
    wcscpy(cur_dir, exe_path);
    for (unsigned int depth = 0; depth < 5; ++depth)
    {
        /* This file specifically seems to exist as top-level asset file.
         * An earlier attempt was made to check for CAPCOM CompanyName,
         * but some games do not have that. This check seems more robust overall. */
        if (SUCCEEDED(PathCchCombineEx(path, ARRAY_SIZE(path), cur_dir,
                L"re_chunk_000.pak", PATHCCH_NONE)))
        {
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                return VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE;
        }

        /* Some games override their metadata to not mention UnrealEngine at all.
         * As a last ditch effort, try to see if Content/Paks folder exists.
         * This one seems to be rather universal. */
        if (has_unreal_content_paks(cur_dir))
            return VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE;

        if (FAILED(PathCchRemoveFileSpec(cur_dir, ARRAY_SIZE(cur_dir))) || !cur_dir[0])
            break;
    }

    return VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
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

enum vkd3d_application_engine_class vkd3d_get_engine_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = *minor = *patch = 0;
    return VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
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
