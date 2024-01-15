/*
 * Copyright 2024 Philip Rebohle for Valve Software
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
#pragma once

#include <vector>

#include <d3dcompiler.h>

using pfn_d3d_compile = HRESULT (STDMETHODCALLTYPE *) (LPCVOID src_data,
    SIZE_T src_data_size, LPCSTR source_name, const D3D_SHADER_MACRO *defines,
    ID3DInclude *includes, LPCSTR entry_point, LPCSTR target, UINT flags1,
    UINT flags2, ID3DBlob **code, ID3DBlob **error_msgs);

using pfn_d3d_strip_shader_proc = HRESULT (STDMETHODCALLTYPE *) (LPCVOID shader_bytecode,
    SIZE_T bytecode_length, UINT strip_flags, ID3DBlob **stripped_blob);

class d3dcompiler_library
{
public:

    d3dcompiler_library();

    d3dcompiler_library(const d3dcompiler_library&) = delete;
    d3dcompiler_library &operator = (const d3dcompiler_library&) = delete;

    ~d3dcompiler_library();

    bool compile(const std::vector<char> &code, LPCSTR source_name, LPCSTR target,
            LPCSTR entry_point, const std::vector<std::wstring> &arguments, std::vector<char> &binary) const;

private:

    HMODULE m_d3dcompiler = nullptr;

    pfn_d3d_compile m_d3d_compile_proc = nullptr;
    pfn_d3d_strip_shader_proc m_d3d_strip_shader_proc = nullptr;

};
