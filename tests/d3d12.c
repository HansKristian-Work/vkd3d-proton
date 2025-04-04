/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
 * Copyright 2020-2021 Philip Rebohle for Valve Corporation
 * Copyright 2020-2021 Joshua Ashton for Valve Corporation
 * Copyright 2020-2021 Hans-Kristian Arntzen for Valve Corporation
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

#define INITGUID
#define VKD3D_TEST_DECLARE_MAIN
#include "d3d12_crosstest.h"

#define decl_test(x) void x(void);
#include "d3d12_tests.h"
#undef decl_test

/* Uncomment when testing against Agility SDK debug layers. */
#if 0
__declspec(dllexport) extern const UINT D3D12SDKVersion = 613;
__declspec(dllexport) extern const char *D3D12SDKPath = u8".\\D3D12\\";
#endif

START_TEST(d3d12)
{
    pfn_D3D12CreateDevice = get_d3d12_pfn(D3D12CreateDevice);
    pfn_D3D12EnableExperimentalFeatures = get_d3d12_pfn(D3D12EnableExperimentalFeatures);
    pfn_D3D12GetDebugInterface = get_d3d12_pfn(D3D12GetDebugInterface);
    pfn_D3D12GetInterface = get_d3d12_pfn(D3D12GetInterface);

    parse_args(argc, argv);
    enable_d3d12_debug_layer(argc, argv);
    enable_feature_level_override(argc, argv);
    init_adapter_info();

    vkd3d_set_running_in_test_suite();

    pfn_D3D12CreateVersionedRootSignatureDeserializer = get_d3d12_pfn(D3D12CreateVersionedRootSignatureDeserializer);
    pfn_D3D12SerializeVersionedRootSignature = get_d3d12_pfn(D3D12SerializeVersionedRootSignature);

#define decl_test(x) run_test(x)
#include "d3d12_tests.h"
#undef decl_test
}
