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
#include <cstring>
#include <iostream>
#include <unordered_map>

#include "dxbc_library.h"

d3dcompiler_library::d3dcompiler_library()
{
    m_d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");

    if (!m_d3dcompiler)
    {
        std::cerr << "Failed to load d3dcompiler_47.dll." << std::endl;
        return;
    }

    m_d3d_compile_proc = reinterpret_cast<pfn_d3d_compile>(
        GetProcAddress(m_d3dcompiler, "D3DCompile"));
    m_d3d_strip_shader_proc = reinterpret_cast<pfn_d3d_strip_shader_proc>(
        GetProcAddress(m_d3dcompiler, "D3DStripShader"));

    if (!m_d3d_compile_proc || !m_d3d_strip_shader_proc)
    {
        std::cerr << "Failed to locate D3DCompile and D3DStripShader functions." << std::endl;
        FreeLibrary(m_d3dcompiler);
        m_d3dcompiler = nullptr;
    }
}


d3dcompiler_library::~d3dcompiler_library()
{
    FreeLibrary(m_d3dcompiler);
}


bool d3dcompiler_library::compile(const std::vector<char> &code, LPCSTR source_name, LPCSTR target,
        LPCSTR entry_point, const std::vector<std::wstring> &arguments, std::vector<char> &binary) const
{
    static const std::unordered_map<std::wstring, UINT> argument_map = {{
        { L"-Vd",                     D3DCOMPILE_SKIP_VALIDATION                },
        { L"-Od",                     D3DCOMPILE_SKIP_OPTIMIZATION              },
        { L"-Zpr",                    D3DCOMPILE_PACK_MATRIX_ROW_MAJOR          },
        { L"-Zpc",                    D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR       },
        { L"-Gpp",                    D3DCOMPILE_PARTIAL_PRECISION              },
        { L"-Gfa",                    D3DCOMPILE_AVOID_FLOW_CONTROL             },
        { L"-Ges",                    D3DCOMPILE_ENABLE_STRICTNESS              },
        { L"-Gis",                    D3DCOMPILE_IEEE_STRICTNESS                },
        { L"-Gec",                    D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY },
        { L"-O0",                     D3DCOMPILE_OPTIMIZATION_LEVEL0            },
        { L"-O1",                     D3DCOMPILE_OPTIMIZATION_LEVEL1            },
        { L"-O2",                     D3DCOMPILE_OPTIMIZATION_LEVEL2            },
        { L"-O3",                     D3DCOMPILE_OPTIMIZATION_LEVEL3            },
        { L"-WX",                     D3DCOMPILE_WARNINGS_ARE_ERRORS            },
        { L"-res_may_alias",          D3DCOMPILE_RESOURCES_MAY_ALIAS            },
        { L"-enable_unbounded_descriptor_tables",
                                      D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES },
        { L"-all_resources_bound",    D3DCOMPILE_ALL_RESOURCES_BOUND            },
    }};

    if (!m_d3dcompiler)
        return false;

    ID3DBlob *blob = nullptr;
    ID3DBlob *errors = nullptr;

    /* Always enable unbounded descriptor tables for convenience, but
     * recognize the fxc argument anyway instead of erroring out. */
    UINT flags = D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;

    for (const auto &arg : arguments) {
        auto entry = argument_map.find(arg);

        if (entry == argument_map.end())
        {
            std::wcerr << "Unknown argument: '" << arg << "'" << std::endl;
            return false;
        }

        flags |= entry->second;
    }

    /* Default to full optimization */
    if (!(flags & (D3DCOMPILE_OPTIMIZATION_LEVEL0 |
            D3DCOMPILE_OPTIMIZATION_LEVEL1 |
            D3DCOMPILE_OPTIMIZATION_LEVEL2 |
            D3DCOMPILE_OPTIMIZATION_LEVEL3 |
            D3DCOMPILE_SKIP_OPTIMIZATION)))
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;

    HRESULT hr = m_d3d_compile_proc(code.data(), code.size(), source_name,
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, target,
        flags, 0, &blob, &errors);

    if (errors)
    {
        std::wcerr << reinterpret_cast<const char*>(
          errors->GetBufferPointer()) << std::endl;
        errors->Release();
    }

    if (FAILED(hr))
        return false;

    ID3DBlob *stripped_blob = nullptr;

    hr = m_d3d_strip_shader_proc(blob->GetBufferPointer(), blob->GetBufferSize(),
      D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO, &stripped_blob);

    blob->Release();

    if (FAILED(hr))
        return false;

    binary.resize(stripped_blob->GetBufferSize());
    std::memcpy(binary.data(), stripped_blob->GetBufferPointer(), binary.size());

    stripped_blob->Release();
    return true;
}
