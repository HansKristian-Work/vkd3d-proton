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

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vkd3d_dxcapi.h>

class dxc_library
{
public:

    static dxc_library instance;

    dxc_library();

    dxc_library(const dxc_library&) = delete;
    dxc_library &operator = (const dxc_library&) = delete;

    ~dxc_library();

    bool compile(const std::vector<char> &code, LPCWSTR source_name, LPCWSTR target,
            LPCWSTR entry_point, const std::vector<std::wstring> &arguments, std::vector<char> &binary) const;

private:

    HMODULE m_dxcompiler = nullptr;

    DxcCreateInstanceProc m_dxc_create_instance = nullptr;

};
