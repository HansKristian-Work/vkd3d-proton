/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* We need to specify the __declspec(dllexport) attribute
 * on MinGW because otherwise the stdcall aliases/fixups
 * don't get exported.
 */
#if defined(_MSC_VER)
  #define DLLEXPORT
#elif defined(__MINGW32__)
  #define DLLEXPORT __declspec(dllexport)
#endif

/* Some applications check if this file exists to determine if Agility SDK is supported,
 * but we implement everything in d3d12.dll and ignore app-provided d3d12core.dll anyways.
 * Just expose a stub .dll. */

HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug)
{
    /* Don't bother with logging here, it just ends up bloating this stub .dll.
     * We can inspect if this is loaded from wine logs if we have to. */
    return E_NOINTERFACE;
}

/* Just expose the latest stable AgilitySDK version.
 * This is actually exported as a UINT and not a function it seems. */
DLLEXPORT const UINT D3D12SDKVersion = 608;

