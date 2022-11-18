/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_UTILS_H
#define __VKD3D_UTILS_H

#include <vkd3d.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VKD3D_WAIT_OBJECT_0 (0)
#define VKD3D_WAIT_TIMEOUT (1)
#define VKD3D_WAIT_FAILED (~0u)
#define VKD3D_INFINITE (~0u)

#ifdef _WIN32
# ifdef _MSC_VER
#  define VKD3D_UTILS_EXPORT
# else
  /* We need to specify the __declspec(dllexport) attribute
   * on MinGW because otherwise the stdcall aliases/fixups
   * don't get exported.
   */
#  ifdef VKD3D_UTILS_EXPORTS
#   define VKD3D_UTILS_EXPORT __declspec(dllexport)
#  else
#   define VKD3D_UTILS_EXPORT __declspec(dllimport)
#  endif
# endif
#elif defined(__GNUC__)
# define VKD3D_UTILS_EXPORT DECLSPEC_VISIBLE
#else
# define VKD3D_UTILS_EXPORT
#endif

/* 1.0 */
#ifndef _WIN32
VKD3D_UTILS_EXPORT HANDLE vkd3d_create_eventfd(void);
VKD3D_UTILS_EXPORT unsigned int vkd3d_wait_eventfd(HANDLE event, unsigned int milliseconds);
VKD3D_UTILS_EXPORT void vkd3d_signal_eventfd(HANDLE event);
VKD3D_UTILS_EXPORT void vkd3d_destroy_eventfd(HANDLE event);
#endif

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL feature_level, REFIID iid, void **device);
VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size, REFIID iid, void **deserializer);
VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void **debug);
VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);

/* 1.2 */
VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data,
        SIZE_T data_size, REFIID iid, void **deserializer);
VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_UTILS_H */
