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
#define INITGUID

#include <array>
#include <cstring>
#include <iostream>

#include "dxil_library.h"

dxc_library dxc_library::instance;


dxc_library::dxc_library()
{
    m_dxcompiler = LoadLibraryA("dxcompiler.dll");

    if (!m_dxcompiler)
    {
        std::wcerr << "Failed to load dxcompiler.dll." << std::endl;
        return;
    }

    m_dxc_create_instance = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(m_dxcompiler, "DxcCreateInstance"));

    if (!m_dxc_create_instance)
    {
        std::wcerr << "DxcCreateInstance not found in dxcompiler.dll." << std::endl;
        FreeLibrary(m_dxcompiler);
        m_dxcompiler = nullptr;
    }
}


dxc_library::~dxc_library()
{
    FreeLibrary(m_dxcompiler);
}


bool dxc_library::compile(const std::vector<char> &code, LPCWSTR source_name, LPCWSTR target,
        LPCWSTR entry_point, const std::vector<std::wstring> &arguments, std::vector<char> &binary) const
{
    if (!m_dxcompiler)
        return false;

    /* These are not thread-safe, just create new instances each time */
    IDxcUtils *utils = nullptr;
    HRESULT hr = m_dxc_create_instance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));

    if (FAILED(hr))
    {
        std::wcerr << "Failed to create IDxcUtils instance." << std::endl;
        return false;
    }

    IDxcCompiler3 *compiler = nullptr;
    hr = m_dxc_create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

    if (FAILED(hr))
    {
        std::wcerr << "Failed to create IDxcCompiler3 instance." << std::endl;
        utils->Release();
        return false;
    }

    IDxcIncludeHandler *include_handler = nullptr;
    hr = utils->CreateDefaultIncludeHandler(&include_handler);

    if (FAILED(hr))
    {
        std::wcerr << "Failed to create include handler." << std::endl;
        utils->Release();
        compiler->Release();
        return false;
    }

    /* Set up parameters that minimize the generated binary size */
    IDxcCompilerArgs *args = nullptr;

    std::vector<LPCWSTR> argument_list = {
        L"-Qstrip_debug",
        L"-Qstrip_reflect",
    };

    for (const auto &arg : arguments)
        argument_list.push_back(arg.c_str());

    hr = utils->BuildArguments(source_name, entry_point, target,
            argument_list.data(), argument_list.size(), nullptr, 0, &args);

    if (FAILED(hr))
    {
        std::wcerr << "Failed to build compiler arguments." << std::endl;
        include_handler->Release();
        utils->Release();
        compiler->Release();
        return false;
    }

    IDxcResult *result = nullptr;

    /* Assume that shader code is some sort of single-byte encoding */
    DxcBuffer source_buffer = { };
    source_buffer.Ptr = code.data();
    source_buffer.Size = code.size();
    source_buffer.Encoding = DXC_CP_UTF8;

    hr = compiler->Compile(&source_buffer,
        args->GetArguments(), args->GetCount(),
        include_handler, IID_PPV_ARGS(&result));

    /* Compile will still return S_OK if compilation failed, we need to ask
     * the result object for the status of the operation, and for some bizarre
     * reason that call returns a HRESULT itself, so we have three layers of
     * error checking here. Just make sure we end up with an error status at
     * the end if anything fails here. */
    if (SUCCEEDED(hr))
    {
        HRESULT status = S_OK;
        hr = result->GetStatus(&status);

        if (SUCCEEDED(hr))
            hr = status;
    }

    bool status = SUCCEEDED(hr);

    if (status)
    {
        /* Compilation successful, just retrieve the binary blob and return it. */
        IDxcBlob *binary_blob = nullptr;

        if (result && result->HasOutput(DXC_OUT_OBJECT))
        {
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&binary_blob), nullptr);

            if (SUCCEEDED(hr))
            {
                binary.resize(binary_blob->GetBufferSize());
                std::memcpy(binary.data(), binary_blob->GetBufferPointer(), binary.size());
            }
        }

        if (binary_blob)
            binary_blob->Release();
        else
            std::wcerr << "Failed to retrieve shader binary." << std::endl;
    }
    else if (result)
    {
        /* Try to print, retrieve and convert error messages to a format that we
         * can print to the console. More nested error checking madness here. */
        std::wcerr << "Failed to compile DXIL shader '" << source_name << "':" << std::endl;

        if (result->HasOutput(DXC_OUT_ERRORS))
        {
            IDxcBlobEncoding *error_blob = nullptr;
            IDxcBlobWide *error_blob_wide = nullptr;

            hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr);

            if (SUCCEEDED(hr) && SUCCEEDED(utils->GetBlobAsWide(error_blob, &error_blob_wide)))
            {
                std::wcerr << error_blob_wide->GetStringPointer() << std::endl;
                error_blob_wide->Release();
            }
            else
            {
                std::wcerr << "Failed to get errors." << std::endl;
            }

            if (error_blob)
                error_blob->Release();
        }
    }

    if (result)
        result->Release();

    args->Release();
    include_handler->Release();
    compiler->Release();
    utils->Release();
    return status;
}
