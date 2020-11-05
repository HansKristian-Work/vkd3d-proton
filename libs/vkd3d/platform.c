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

#if defined(__linux__)

# include <dlfcn.h>
# include <errno.h>

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

    strncpy(program_name, name, VKD3D_PATH_MAX);
    program_name[VKD3D_PATH_MAX - 1] = '\0';
    free(real_path);
    return true;
}

#elif defined(_WIN32)

# include <windows.h>

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

#endif
