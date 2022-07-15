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
#include "d3d12_crosstest.h"

void test_create_root_signature(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12RootSignature *root_signature;
    ID3D12Device *device, *tmp_device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* descriptor table */
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12RootSignature_GetDevice(root_signature, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(root_signature, &IID_ID3D12Object, true);
    check_interface(root_signature, &IID_ID3D12DeviceChild, true);
    check_interface(root_signature, &IID_ID3D12Pageable, false);
    check_interface(root_signature, &IID_ID3D12RootSignature, true);

    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    /* sampler and SRV in the same descriptor table */
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 2;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 10;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == E_INVALIDARG, "Failed to create root signature, hr %#x.\n", hr);

    /* empty root signature */
    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    /* root constants */
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 8;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    todo ok(hr == E_FAIL || hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12RootSignature_Release(root_signature);
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].Constants.ShaderRegister = 1;
    root_parameters[2].Constants.RegisterSpace = 0;
    root_parameters[2].Constants.Num32BitValues = 3;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 3;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    /* root descriptors */
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    todo ok(hr == E_FAIL || hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12RootSignature_Release(root_signature);
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_root_signature_limits(void)
{
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[D3D12_MAX_ROOT_COST + 1];
    D3D12_ROOT_PARAMETER root_parameters[D3D12_MAX_ROOT_COST + 1];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature;
    ID3D12Device *device;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* A descriptor table costs 1 DWORD. */
    for (i = 0; i < ARRAY_SIZE(root_parameters); ++i)
    {
        descriptor_ranges[i].RangeType = i % 2
                ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_ranges[i].NumDescriptors = 1;
        descriptor_ranges[i].BaseShaderRegister = i / 2;
        descriptor_ranges[i].RegisterSpace = 0;
        descriptor_ranges[i].OffsetInDescriptorsFromTableStart = 0;
        root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[i].DescriptorTable.pDescriptorRanges = &descriptor_ranges[i];
        root_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    root_signature_desc.NumParameters = D3D12_MAX_ROOT_COST;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);
    ID3D12RootSignature_Release(root_signature);

    root_signature_desc.NumParameters = D3D12_MAX_ROOT_COST + 1;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void check_descriptor_range_(unsigned int line, const D3D12_DESCRIPTOR_RANGE *range,
        const D3D12_DESCRIPTOR_RANGE *expected_range)
{
    ok_(line)(range->RangeType == expected_range->RangeType,
            "Got range type %#x, expected %#x.\n", range->RangeType, expected_range->RangeType);
    ok_(line)(range->NumDescriptors == expected_range->NumDescriptors,
            "Got descriptor count %u, expected %u.\n", range->NumDescriptors, expected_range->NumDescriptors);
    ok_(line)(range->BaseShaderRegister == expected_range->BaseShaderRegister,
            "Got base shader register %u, expected %u.\n",
            range->BaseShaderRegister, expected_range->BaseShaderRegister);
    ok_(line)(range->RegisterSpace == expected_range->RegisterSpace,
            "Got register space %u, expected %u.\n", range->RegisterSpace, expected_range->RegisterSpace);
    ok_(line)(range->OffsetInDescriptorsFromTableStart == expected_range->OffsetInDescriptorsFromTableStart,
            "Got offset %u, expected %u.\n", range->OffsetInDescriptorsFromTableStart,
            expected_range->OffsetInDescriptorsFromTableStart);
}

static void check_descriptor_range1_(unsigned int line, const D3D12_DESCRIPTOR_RANGE1 *range,
        const D3D12_DESCRIPTOR_RANGE1 *expected_range, bool converted)
{
    unsigned int expected_flags = converted
            ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE
            : expected_range->Flags;

    ok_(line)(range->RangeType == expected_range->RangeType,
            "Got range type %#x, expected %#x.\n", range->RangeType, expected_range->RangeType);
    ok_(line)(range->NumDescriptors == expected_range->NumDescriptors,
            "Got descriptor count %u, expected %u.\n", range->NumDescriptors, expected_range->NumDescriptors);
    ok_(line)(range->BaseShaderRegister == expected_range->BaseShaderRegister,
            "Got base shader register %u, expected %u.\n",
            range->BaseShaderRegister, expected_range->BaseShaderRegister);
    ok_(line)(range->RegisterSpace == expected_range->RegisterSpace,
            "Got register space %u, expected %u.\n", range->RegisterSpace, expected_range->RegisterSpace);
    ok_(line)(range->Flags == expected_flags,
            "Got descriptor range flags %#x, expected %#x.\n", range->Flags, expected_flags);
    ok_(line)(range->OffsetInDescriptorsFromTableStart == expected_range->OffsetInDescriptorsFromTableStart,
            "Got offset %u, expected %u.\n", range->OffsetInDescriptorsFromTableStart,
            expected_range->OffsetInDescriptorsFromTableStart);
}

static void check_root_parameter_(unsigned int line, const D3D12_ROOT_PARAMETER *parameter,
        const D3D12_ROOT_PARAMETER *expected_parameter)
{
    const D3D12_ROOT_DESCRIPTOR *descriptor, *expected_descriptor;
    const D3D12_ROOT_DESCRIPTOR_TABLE *table, *expected_table;
    const D3D12_ROOT_CONSTANTS *constants, *expected_constants;
    unsigned int i;

    ok_(line)(parameter->ParameterType == expected_parameter->ParameterType,
            "Got type %#x, expected %#x.\n", parameter->ParameterType, expected_parameter->ParameterType);
    if (parameter->ParameterType != expected_parameter->ParameterType)
        return;

    switch (parameter->ParameterType)
    {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            table = &parameter->DescriptorTable;
            expected_table = &expected_parameter->DescriptorTable;
            ok_(line)(table->NumDescriptorRanges == expected_table->NumDescriptorRanges,
                    "Got range count %u, expected %u.\n",
                    table->NumDescriptorRanges, expected_table->NumDescriptorRanges);
            if (table->NumDescriptorRanges == expected_table->NumDescriptorRanges)
            {
                for (i = 0; i < table->NumDescriptorRanges; ++i)
                    check_descriptor_range_(line, &table->pDescriptorRanges[i],
                            &expected_table->pDescriptorRanges[i]);
            }
            break;
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            constants = &parameter->Constants;
            expected_constants = &expected_parameter->Constants;
            ok_(line)(constants->ShaderRegister == expected_constants->ShaderRegister,
                    "Got shader register %u, expected %u.\n",
                    constants->ShaderRegister, expected_constants->ShaderRegister);
            ok_(line)(constants->RegisterSpace == expected_constants->RegisterSpace,
                    "Got register space %u, expected %u.\n",
                    constants->RegisterSpace, expected_constants->RegisterSpace);
            ok_(line)(constants->Num32BitValues == expected_constants->Num32BitValues,
                    "Got 32-bit value count %u, expected %u.\n",
                    constants->Num32BitValues, expected_constants->Num32BitValues);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            descriptor = &parameter->Descriptor;
            expected_descriptor = &expected_parameter->Descriptor;
            ok_(line)(descriptor->ShaderRegister == expected_descriptor->ShaderRegister,
                    "Got shader register %u, expected %u.\n",
                    descriptor->ShaderRegister, expected_descriptor->ShaderRegister);
            ok_(line)(descriptor->RegisterSpace == expected_descriptor->RegisterSpace,
                    "Got register space %u, expected %u.\n",
                    descriptor->RegisterSpace, expected_descriptor->RegisterSpace);
            break;
        default:
            trace("Unhandled type %#x.\n", parameter->ParameterType);
    }

    ok_(line)(parameter->ShaderVisibility == expected_parameter->ShaderVisibility,
            "Got shader visibility %#x, expected %#x.\n",
            parameter->ShaderVisibility, expected_parameter->ShaderVisibility);
}

static void check_root_parameter1_(unsigned int line, const D3D12_ROOT_PARAMETER1 *parameter,
        const D3D12_ROOT_PARAMETER1 *expected_parameter, bool converted)
{
    const D3D12_ROOT_DESCRIPTOR1 *descriptor, *expected_descriptor;
    const D3D12_ROOT_DESCRIPTOR_TABLE1 *table, *expected_table;
    const D3D12_ROOT_CONSTANTS *constants, *expected_constants;
    unsigned int expected_flags;
    unsigned int i;

    ok_(line)(parameter->ParameterType == expected_parameter->ParameterType,
            "Got type %#x, expected %#x.\n", parameter->ParameterType, expected_parameter->ParameterType);
    if (parameter->ParameterType != expected_parameter->ParameterType)
        return;

    switch (parameter->ParameterType)
    {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            table = &parameter->DescriptorTable;
            expected_table = &expected_parameter->DescriptorTable;
            ok_(line)(table->NumDescriptorRanges == expected_table->NumDescriptorRanges,
                    "Got range count %u, expected %u.\n",
                    table->NumDescriptorRanges, expected_table->NumDescriptorRanges);
            if (table->NumDescriptorRanges == expected_table->NumDescriptorRanges)
            {
                for (i = 0; i < table->NumDescriptorRanges; ++i)
                    check_descriptor_range1_(line, &table->pDescriptorRanges[i],
                            &expected_table->pDescriptorRanges[i], converted);
            }
            break;
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            constants = &parameter->Constants;
            expected_constants = &expected_parameter->Constants;
            ok_(line)(constants->ShaderRegister == expected_constants->ShaderRegister,
                    "Got shader register %u, expected %u.\n",
                    constants->ShaderRegister, expected_constants->ShaderRegister);
            ok_(line)(constants->RegisterSpace == expected_constants->RegisterSpace,
                    "Got register space %u, expected %u.\n",
                    constants->RegisterSpace, expected_constants->RegisterSpace);
            ok_(line)(constants->Num32BitValues == expected_constants->Num32BitValues,
                    "Got 32-bit value count %u, expected %u.\n",
                    constants->Num32BitValues, expected_constants->Num32BitValues);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            descriptor = &parameter->Descriptor;
            expected_descriptor = &expected_parameter->Descriptor;
            ok_(line)(descriptor->ShaderRegister == expected_descriptor->ShaderRegister,
                    "Got shader register %u, expected %u.\n",
                    descriptor->ShaderRegister, expected_descriptor->ShaderRegister);
            ok_(line)(descriptor->RegisterSpace == expected_descriptor->RegisterSpace,
                    "Got register space %u, expected %u.\n",
                    descriptor->RegisterSpace, expected_descriptor->RegisterSpace);
            expected_flags = converted ? D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE : expected_descriptor->Flags;
            ok_(line)(descriptor->Flags == expected_flags,
                    "Got root descriptor flags %#x, expected %#x.\n",
                    descriptor->Flags, expected_flags);
            break;
        default:
            trace("Unhandled type %#x.\n", parameter->ParameterType);
    }

    ok_(line)(parameter->ShaderVisibility == expected_parameter->ShaderVisibility,
            "Got shader visibility %#x, expected %#x.\n",
            parameter->ShaderVisibility, expected_parameter->ShaderVisibility);
}

static void check_static_sampler_(unsigned int line, const D3D12_STATIC_SAMPLER_DESC *sampler,
        const D3D12_STATIC_SAMPLER_DESC *expected_sampler)
{
    ok_(line)(sampler->Filter == expected_sampler->Filter,
            "Got filter %#x, expected %#x.\n", sampler->Filter, expected_sampler->Filter);
    ok_(line)(sampler->AddressU == expected_sampler->AddressU,
            "Got address U %#x, expected %#x.\n", sampler->AddressU, expected_sampler->AddressU);
    ok_(line)(sampler->AddressV == expected_sampler->AddressV,
            "Got address V %#x, expected %#x.\n", sampler->AddressV, expected_sampler->AddressV);
    ok_(line)(sampler->AddressW == expected_sampler->AddressW,
            "Got address W %#x, expected %#x.\n", sampler->AddressW, expected_sampler->AddressW);
    ok_(line)(sampler->MipLODBias == expected_sampler->MipLODBias,
            "Got mip LOD bias %.8e, expected %.8e.\n", sampler->MipLODBias, expected_sampler->MipLODBias);
    ok_(line)(sampler->MaxAnisotropy == expected_sampler->MaxAnisotropy,
            "Got max anisotropy %u, expected %u.\n", sampler->MaxAnisotropy, expected_sampler->MaxAnisotropy);
    ok_(line)(sampler->ComparisonFunc == expected_sampler->ComparisonFunc,
            "Got comparison func %#x, expected %#x.\n", sampler->ComparisonFunc, expected_sampler->ComparisonFunc);
    ok_(line)(sampler->BorderColor == expected_sampler->BorderColor,
            "Got border color %#x, expected %#x.\n", sampler->BorderColor, expected_sampler->BorderColor);
    ok_(line)(sampler->MinLOD == expected_sampler->MinLOD,
            "Got min LOD %.8e, expected %.8e.\n", sampler->MinLOD, expected_sampler->MinLOD);
    ok_(line)(sampler->MaxLOD == expected_sampler->MaxLOD,
            "Got max LOD %.8e, expected %.8e.\n", sampler->MaxLOD, expected_sampler->MaxLOD);
    ok_(line)(sampler->ShaderRegister == expected_sampler->ShaderRegister,
            "Got shader register %u, expected %u.\n", sampler->ShaderRegister, expected_sampler->ShaderRegister);
    ok_(line)(sampler->RegisterSpace == expected_sampler->RegisterSpace,
            "Got register space %u, expected %u.\n", sampler->RegisterSpace, expected_sampler->RegisterSpace);
    ok_(line)(sampler->ShaderVisibility == expected_sampler->ShaderVisibility,
            "Got shader visibility %#x, expected %#x.\n",
            sampler->ShaderVisibility, expected_sampler->ShaderVisibility);
}

#define check_root_signature_desc(desc, expected) check_root_signature_desc_(__LINE__, desc, expected)
static void check_root_signature_desc_(unsigned int line, const D3D12_ROOT_SIGNATURE_DESC *desc,
        const D3D12_ROOT_SIGNATURE_DESC *expected_desc)
{
    unsigned int i;

    ok_(line)(desc->NumParameters == expected_desc->NumParameters,
            "Got parameter count %u, expected %u.\n",
            desc->NumParameters, expected_desc->NumParameters);
    if (!expected_desc->pParameters)
    {
        ok_(line)(!desc->pParameters, "Got unexpected parameters %p.\n", desc->pParameters);
    }
    else if (desc->NumParameters == expected_desc->NumParameters)
    {
        for (i = 0; i < desc->NumParameters; ++i)
            check_root_parameter_(line, &desc->pParameters[i], &expected_desc->pParameters[i]);
    }
    ok_(line)(desc->NumStaticSamplers == expected_desc->NumStaticSamplers,
            "Got static sampler count %u, expected %u.\n",
            desc->NumStaticSamplers, expected_desc->NumStaticSamplers);
    if (!expected_desc->pStaticSamplers)
    {
        ok_(line)(!desc->pStaticSamplers, "Got unexpected static samplers %p.\n", desc->pStaticSamplers);
    }
    else if (desc->NumStaticSamplers == expected_desc->NumStaticSamplers)
    {
        for (i = 0; i < desc->NumStaticSamplers; ++i)
            check_static_sampler_(line, &desc->pStaticSamplers[i], &expected_desc->pStaticSamplers[i]);
    }
    ok_(line)(desc->Flags == expected_desc->Flags, "Got flags %#x, expected %#x.\n",
            desc->Flags, expected_desc->Flags);
}

#define check_root_signature_desc1(a, b, c) check_root_signature_desc1_(__LINE__, a, b, c)
static void check_root_signature_desc1_(unsigned int line, const D3D12_ROOT_SIGNATURE_DESC1 *desc,
        const D3D12_ROOT_SIGNATURE_DESC1 *expected_desc, bool converted)
{
    unsigned int i;

    ok_(line)(desc->NumParameters == expected_desc->NumParameters,
            "Got parameter count %u, expected %u.\n",
            desc->NumParameters, expected_desc->NumParameters);
    if (!expected_desc->pParameters)
    {
        ok_(line)(!desc->pParameters, "Got unexpected parameters %p.\n", desc->pParameters);
    }
    else if (desc->NumParameters == expected_desc->NumParameters)
    {
        for (i = 0; i < desc->NumParameters; ++i)
            check_root_parameter1_(line, &desc->pParameters[i], &expected_desc->pParameters[i], converted);
    }
    ok_(line)(desc->NumStaticSamplers == expected_desc->NumStaticSamplers,
            "Got static sampler count %u, expected %u.\n",
            desc->NumStaticSamplers, expected_desc->NumStaticSamplers);
    if (!expected_desc->pStaticSamplers)
    {
        ok_(line)(!desc->pStaticSamplers, "Got unexpected static samplers %p.\n", desc->pStaticSamplers);
    }
    else if (desc->NumStaticSamplers == expected_desc->NumStaticSamplers)
    {
        for (i = 0; i < desc->NumStaticSamplers; ++i)
            check_static_sampler_(line, &desc->pStaticSamplers[i], &expected_desc->pStaticSamplers[i]);
    }
    ok_(line)(desc->Flags == expected_desc->Flags, "Got flags %#x, expected %#x.\n",
            desc->Flags, expected_desc->Flags);
}

#define check_root_signature_deserialization(a, b, c) check_root_signature_deserialization_(__LINE__, a, b, c)
static void check_root_signature_deserialization_(unsigned int line, const D3D12_SHADER_BYTECODE *code,
        const D3D12_ROOT_SIGNATURE_DESC *expected_desc, const D3D12_ROOT_SIGNATURE_DESC1 *expected_desc1)
{
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *versioned_desc, *versioned_desc2;
    ID3D12VersionedRootSignatureDeserializer *versioned_deserializer;
    ID3D12RootSignatureDeserializer *deserializer;
    const D3D12_ROOT_SIGNATURE_DESC *desc;
    ULONG refcount;
    HRESULT hr;

    if (!code->BytecodeLength)
        return;

    hr = D3D12CreateRootSignatureDeserializer(code->pShaderBytecode, code->BytecodeLength,
            &IID_ID3D12RootSignatureDeserializer, (void **)&deserializer);
    ok_(line)(hr == S_OK, "Failed to create deserializer, hr %#x.\n", hr);

    desc = ID3D12RootSignatureDeserializer_GetRootSignatureDesc(deserializer);
    ok(desc, "Got NULL root signature desc.\n");
    check_root_signature_desc_(line, desc, expected_desc);

    refcount = ID3D12RootSignatureDeserializer_Release(deserializer);
    ok_(line)(!refcount, "ID3D12RootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);

    if (!pfn_D3D12CreateVersionedRootSignatureDeserializer)
        return;

    hr = pfn_D3D12CreateVersionedRootSignatureDeserializer(code->pShaderBytecode, code->BytecodeLength,
            &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&versioned_deserializer);
    ok_(line)(hr == S_OK, "Failed to create versioned deserializer, hr %#x.\n", hr);

    versioned_desc = ID3D12VersionedRootSignatureDeserializer_GetUnconvertedRootSignatureDesc(versioned_deserializer);
    ok(versioned_desc, "Got NULL root signature desc.\n");
    ok(versioned_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0, "Got unexpected version %#x.\n", versioned_desc->Version);
    check_root_signature_desc_(line, &versioned_desc->Desc_1_0, expected_desc);

    hr = ID3D12VersionedRootSignatureDeserializer_GetRootSignatureDescAtVersion(versioned_deserializer,
            D3D_ROOT_SIGNATURE_VERSION_1_0, &versioned_desc2);
    ok_(line)(hr == S_OK, "Failed to get root signature 1.0, hr %#x.\n", hr);
    ok_(line)(versioned_desc2 == versioned_desc, "Got unexpected pointer %p.\n", versioned_desc2);

    hr = ID3D12VersionedRootSignatureDeserializer_GetRootSignatureDescAtVersion(versioned_deserializer,
            D3D_ROOT_SIGNATURE_VERSION_1_1, &versioned_desc);
    ok_(line)(hr == S_OK, "Failed to get root signature 1.0, hr %#x.\n", hr);
    ok(versioned_desc, "Got NULL root signature desc.\n");
    ok(versioned_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1, "Got unexpected version %#x.\n", versioned_desc->Version);
    check_root_signature_desc1_(line, &versioned_desc->Desc_1_1, expected_desc1, true);

    refcount = ID3D12VersionedRootSignatureDeserializer_Release(versioned_deserializer);
    ok_(line)(!refcount, "ID3D12VersionedRootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);
}

#define check_root_signature_serialization(a, b) check_root_signature_serialization_(__LINE__, a, b)
static void check_root_signature_serialization_(unsigned int line, const D3D12_SHADER_BYTECODE *bytecode,
        const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    const DWORD *code = bytecode->pShaderBytecode;
    ID3DBlob *blob, *error_blob;
    DWORD *blob_buffer;
    size_t blob_size;
    unsigned int i;
    HRESULT hr;

    if (!bytecode->BytecodeLength)
        return;

    error_blob = (ID3DBlob *)(uintptr_t)0xdeadbeef;
    hr = D3D12SerializeRootSignature(desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error_blob);
    ok_(line)(hr == S_OK, "Failed to serialize root signature, hr %#x.\n", hr);
    ok_(line)(!error_blob, "Got unexpected error blob %p.\n", error_blob);

    blob_buffer = ID3D10Blob_GetBufferPointer(blob);
    blob_size = ID3D10Blob_GetBufferSize(blob);
    ok_(line)(blob_size == bytecode->BytecodeLength, "Got size %u, expected %u.\n",
            (unsigned int)blob_size, (unsigned int)bytecode->BytecodeLength);

    for (i = 0; i < bytecode->BytecodeLength / sizeof(*code); ++i)
    {
        ok_(line)(blob_buffer[i] == code[i], "Got dword %#x, expected %#x at %u.\n",
                (unsigned int)blob_buffer[i], (unsigned int)code[i], i);
    }

    ID3D10Blob_Release(blob);
}

#define check_root_signature_deserialization1(a, b, c) check_root_signature_deserialization1_(__LINE__, a, b, c)
static void check_root_signature_deserialization1_(unsigned int line, const D3D12_SHADER_BYTECODE *code,
        const D3D12_ROOT_SIGNATURE_DESC *expected_desc, const D3D12_ROOT_SIGNATURE_DESC1 *expected_desc1)
{
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *versioned_desc, *versioned_desc2;
    ID3D12VersionedRootSignatureDeserializer *versioned_deserializer;
    ID3D12RootSignatureDeserializer *deserializer;
    const D3D12_ROOT_SIGNATURE_DESC *desc;
    ULONG refcount;
    HRESULT hr;

    hr = pfn_D3D12CreateVersionedRootSignatureDeserializer(code->pShaderBytecode, code->BytecodeLength,
            &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&versioned_deserializer);
    ok_(line)(hr == S_OK, "Failed to create deserializer, hr %#x.\n", hr);

    versioned_desc = ID3D12VersionedRootSignatureDeserializer_GetUnconvertedRootSignatureDesc(versioned_deserializer);
    ok(versioned_desc, "Got NULL root signature desc.\n");
    ok(versioned_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1, "Got unexpected version %#x.\n", versioned_desc->Version);
    check_root_signature_desc1_(line, &versioned_desc->Desc_1_1, expected_desc1, false);

    hr = ID3D12VersionedRootSignatureDeserializer_GetRootSignatureDescAtVersion(versioned_deserializer,
            D3D_ROOT_SIGNATURE_VERSION_1_1, &versioned_desc2);
    ok_(line)(hr == S_OK, "Failed to get root signature 1.1, hr %#x.\n", hr);
    ok_(line)(versioned_desc2 == versioned_desc, "Got unexpected pointer %p.\n", versioned_desc2);

    hr = ID3D12VersionedRootSignatureDeserializer_GetRootSignatureDescAtVersion(versioned_deserializer,
            D3D_ROOT_SIGNATURE_VERSION_1_0, &versioned_desc);
    ok_(line)(hr == S_OK, "Failed to get root signature 1.0, hr %#x.\n", hr);
    ok(versioned_desc, "Got NULL root signature desc.\n");
    ok(versioned_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0, "Got unexpected version %#x.\n", versioned_desc->Version);
    check_root_signature_desc_(line, &versioned_desc->Desc_1_0, expected_desc);

    refcount = ID3D12VersionedRootSignatureDeserializer_Release(versioned_deserializer);
    ok_(line)(!refcount, "ID3D12VersionedRootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);

    hr = D3D12CreateRootSignatureDeserializer(code->pShaderBytecode, code->BytecodeLength,
            &IID_ID3D12RootSignatureDeserializer, (void **)&deserializer);
    ok_(line)(hr == S_OK, "Failed to create deserializer, hr %#x.\n", hr);

    desc = ID3D12RootSignatureDeserializer_GetRootSignatureDesc(deserializer);
    ok(desc, "Got NULL root signature desc.\n");
    check_root_signature_desc_(line, desc, expected_desc);

    refcount = ID3D12RootSignatureDeserializer_Release(deserializer);
    ok_(line)(!refcount, "ID3D12RootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);
}

#define check_root_signature_serialization1(a, b) check_root_signature_serialization1_(__LINE__, a, b)
static void check_root_signature_serialization1_(unsigned int line, const D3D12_SHADER_BYTECODE *bytecode,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_desc;
    const DWORD *code = bytecode->pShaderBytecode;
    ID3DBlob *blob, *error_blob;
    DWORD *blob_buffer;
    size_t blob_size;
    unsigned int i;
    HRESULT hr;

    versioned_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_desc.Desc_1_1 = *desc;

    error_blob = (ID3DBlob *)(uintptr_t)0xdeadbeef;
    hr = pfn_D3D12SerializeVersionedRootSignature(&versioned_desc, &blob, &error_blob);
    ok_(line)(hr == S_OK, "Failed to serialize root signature, hr %#x.\n", hr);
    ok_(line)(!error_blob, "Got unexpected error blob %p.\n", error_blob);

    blob_buffer = ID3D10Blob_GetBufferPointer(blob);
    blob_size = ID3D10Blob_GetBufferSize(blob);
    ok_(line)(blob_size == bytecode->BytecodeLength, "Got size %u, expected %u.\n",
            (unsigned int)blob_size, (unsigned int)bytecode->BytecodeLength);

    for (i = 0; i < bytecode->BytecodeLength / sizeof(*code); ++i)
    {
        ok_(line)(blob_buffer[i] == code[i], "Got dword %#x, expected %#x at %u.\n",
                (unsigned int)blob_buffer[i], (unsigned int)code[i], i);
    }

    ID3D10Blob_Release(blob);
}

void test_root_signature_byte_code(void)
{
    ID3D12VersionedRootSignatureDeserializer *versioned_deserializer;
    ID3D12RootSignatureDeserializer *deserializer;
    ID3DBlob *blob;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

#if 0
    #define RS ""
#endif
    /* /T rootsig_1_0 /E RS */
    static const DWORD empty_rootsig[] =
    {
        0x43425844, 0xd64afc1d, 0x5dc27735, 0x9edacb4a, 0x6bd8a7fa, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000001, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000000,
    };
    /* /T rootsig_1_1 /E RS */
    static const DWORD empty_rootsig1[] =
    {
        0x43425844, 0x791882cb, 0x83c1db39, 0x327edc93, 0x3163085b, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000002, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000000,
    };
    static const D3D12_ROOT_SIGNATURE_DESC empty_rootsig_desc =
    {
        .Flags = 0,
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 empty_rootsig_desc1 =
    {
        .Flags = 0,
    };

#if 0
    #define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)"
#endif
    static const DWORD ia_rootsig[] =
    {
        0x43425844, 0x05bbd62e, 0xc74d3646, 0xde1407a5, 0x0d99273d, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000001, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000001,
    };
    static const DWORD ia_rootsig1[] =
    {
        0x43425844, 0x1e922238, 0xa7743a59, 0x652c0188, 0xe999b061, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000002, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000001,
    };
    static const D3D12_ROOT_SIGNATURE_DESC ia_rootsig_desc =
    {
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 ia_rootsig_desc1 =
    {
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };

#if 0
    #define RS "RootFlags(DENY_PIXEL_SHADER_ROOT_ACCESS)"
#endif
    static const DWORD deny_ps_rootsig[] =
    {
        0x43425844, 0xfad3a4ce, 0xf246286e, 0xeaa9e176, 0x278d5137, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000001, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000020,
    };
    static const DWORD deny_ps_rootsig1[] =
    {
        0x43425844, 0xca541ae8, 0x791dbcaa, 0xe8a61219, 0x697a84c7, 0x00000001, 0x00000044, 0x00000001,
        0x00000024, 0x30535452, 0x00000018, 0x00000002, 0x00000000, 0x00000018, 0x00000000, 0x00000018,
        0x00000020,
    };
    static const D3D12_ROOT_SIGNATURE_DESC deny_ps_rootsig_desc =
    {
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS,
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 deny_ps_rootsig_desc1 =
    {
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS,
    };

#if 0
    #define RS "CBV(b3, space = 0)"
#endif
    static const DWORD cbv_rootsig[] =
    {
        0x43425844, 0x8dc5087e, 0x5cb9bf0d, 0x2e465ae3, 0x6291e0e0, 0x00000001, 0x00000058, 0x00000001,
        0x00000024, 0x30535452, 0x0000002c, 0x00000001, 0x00000001, 0x00000018, 0x00000000, 0x0000002c,
        0x00000000, 0x00000002, 0x00000000, 0x00000024, 0x00000003, 0x00000000,

    };
    static const DWORD cbv_rootsig1[] =
    {
        0x43425844, 0x66f3e4ad, 0x9938583c, 0x4eaf4733, 0x7940ab73, 0x00000001, 0x0000005c, 0x00000001,
        0x00000024, 0x30535452, 0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030,
        0x00000000, 0x00000002, 0x00000000, 0x00000024, 0x00000003, 0x00000000, 0x00000000,
    };
    static const D3D12_ROOT_PARAMETER cbv_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {3, 0}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC cbv_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(cbv_parameters),
        .pParameters = cbv_parameters,
    };
    static const D3D12_ROOT_PARAMETER1 cbv_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {3, 0}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 cbv_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(cbv_parameters1),
        .pParameters = cbv_parameters1,
    };

#if 0
    #define RS "CBV(b4, space = 1, visibility = SHADER_VISIBILITY_GEOMETRY)"
#endif
    static const DWORD cbv2_rootsig[] =
    {
        0x43425844, 0x6d4cfb48, 0xbfecaa8d, 0x379ff9c3, 0x0cc56997, 0x00000001, 0x00000058, 0x00000001,
        0x00000024, 0x30535452, 0x0000002c, 0x00000001, 0x00000001, 0x00000018, 0x00000000, 0x0000002c,
        0x00000000, 0x00000002, 0x00000004, 0x00000024, 0x00000004, 0x00000001,
    };
    static DWORD cbv2_rootsig1[] =
    {
        0x43425844, 0x8450397e, 0x4e136d61, 0xb4fe3b44, 0xc7223872, 0x00000001, 0x0000005c, 0x00000001,
        0x00000024, 0x30535452, 0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030,
        0x00000000, 0x00000002, 0x00000004, 0x00000024, 0x00000004, 0x00000001, 0x00000000,
    };
    static const D3D12_ROOT_PARAMETER cbv2_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {4, 1}, D3D12_SHADER_VISIBILITY_GEOMETRY},
    };
    static const D3D12_ROOT_SIGNATURE_DESC cbv2_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(cbv2_parameters),
        .pParameters = cbv2_parameters,
    };
    static const D3D12_ROOT_PARAMETER1 cbv2_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {4, 1}, D3D12_SHADER_VISIBILITY_GEOMETRY},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 cbv2_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(cbv2_parameters1),
        .pParameters = cbv2_parameters1,
    };

#if 0
    #define RS "RootFlags(DENY_VERTEX_SHADER_ROOT_ACCESS), SRV(t13)"
#endif
    static const DWORD srv_rootsig[] =
    {
        0x43425844, 0xbc00e5e0, 0xffff2fd3, 0x85c2d405, 0xa61db5e5, 0x00000001, 0x00000058, 0x00000001,
        0x00000024, 0x30535452, 0x0000002c, 0x00000001, 0x00000001, 0x00000018, 0x00000000, 0x0000002c,
        0x00000002, 0x00000003, 0x00000000, 0x00000024, 0x0000000d, 0x00000000,
    };
    static const DWORD srv_rootsig1[] =
    {
        0x43425844, 0xe79f4ac0, 0x1ac0829e, 0x94fddf9d, 0xd83d8bbf, 0x00000001, 0x0000005c, 0x00000001,
        0x00000024, 0x30535452, 0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030,
        0x00000002, 0x00000003, 0x00000000, 0x00000024, 0x0000000d, 0x00000000, 0x00000000,
    };
    static const D3D12_ROOT_PARAMETER srv_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {13}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC srv_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(srv_parameters),
        .pParameters = srv_parameters,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS,
    };
    static const D3D12_ROOT_PARAMETER1 srv_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {13}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 srv_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(srv_parameters1),
        .pParameters = srv_parameters1,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS,
    };

#if 0
    #define RS "UAV(u6)"
#endif
    static const DWORD uav_rootsig[] =
    {
        0x43425844, 0xf873c52c, 0x69f5cbea, 0xaf6bc9f4, 0x2ccf8b54, 0x00000001, 0x00000058, 0x00000001,
        0x00000024, 0x30535452, 0x0000002c, 0x00000001, 0x00000001, 0x00000018, 0x00000000, 0x0000002c,
        0x00000000, 0x00000004, 0x00000000, 0x00000024, 0x00000006, 0x00000000,
    };
    static const DWORD uav_rootsig1[] =
    {
        0x43425844, 0xbd670c62, 0x5c35651b, 0xfb9b9bd1, 0x8a4dddde, 0x00000001, 0x0000005c, 0x00000001,
        0x00000024, 0x30535452, 0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030,
        0x00000000, 0x00000004, 0x00000000, 0x00000024, 0x00000006, 0x00000000, 0x00000000,
    };
    static const D3D12_ROOT_PARAMETER uav_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {6}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC uav_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(uav_parameters),
        .pParameters = uav_parameters,
    };
    static const D3D12_ROOT_PARAMETER1 uav_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {6}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 uav_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(uav_parameters1),
        .pParameters = uav_parameters1,
    };

#if 0
    #define RS "CBV(b4, space = 1, visibility = SHADER_VISIBILITY_VERTEX), " \
            "SRV(t13, flags = DATA_STATIC), " \
            "UAV(u6, flags = DATA_STATIC_WHILE_SET_AT_EXECUTE)"
#endif
    static const DWORD root_descriptors_rootsig1[] =
    {
        0x43425844, 0x8ddedbbe, 0xbcfea259, 0x6b35bfbb, 0x23e1de24, 0x00000001, 0x0000008c, 0x00000001,
        0x00000024, 0x30535452, 0x00000060, 0x00000002, 0x00000003, 0x00000018, 0x00000000, 0x00000060,
        0x00000000, 0x00000002, 0x00000001, 0x0000003c, 0x00000003, 0x00000000, 0x00000048, 0x00000004,
        0x00000000, 0x00000054, 0x00000004, 0x00000001, 0x00000000, 0x0000000d, 0x00000000, 0x00000008,
        0x00000006, 0x00000000, 0x00000004,
    };
    static const D3D12_ROOT_PARAMETER root_descriptors_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {4, 1}, D3D12_SHADER_VISIBILITY_VERTEX},
        {D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {13}},
        {D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {6}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC root_descriptors_desc =
    {
        .NumParameters = ARRAY_SIZE(root_descriptors_parameters),
        .pParameters = root_descriptors_parameters,
    };
    static const D3D12_ROOT_PARAMETER1 root_descriptors_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {4, 1}, D3D12_SHADER_VISIBILITY_VERTEX},
        {D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {13, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC}},
        {D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {6, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 root_descriptors_desc1 =
    {
        .NumParameters = ARRAY_SIZE(root_descriptors_parameters1),
        .pParameters = root_descriptors_parameters1,
    };

#if 0
    #define RS "RootConstants(num32BitConstants=3, b4), " \
            "RootConstants(num32BitConstants=4, b5, space = 3)"
#endif
    static const DWORD constants_rootsig[] =
    {
        0x43425844, 0xbc015590, 0xa9a4a345, 0x7e446850, 0x2be05281, 0x00000001, 0x00000074, 0x00000001,
        0x00000024, 0x30535452, 0x00000048, 0x00000001, 0x00000002, 0x00000018, 0x00000000, 0x00000048,
        0x00000000, 0x00000001, 0x00000000, 0x00000030, 0x00000001, 0x00000000, 0x0000003c, 0x00000004,
        0x00000000, 0x00000003, 0x00000005, 0x00000003, 0x00000004,
    };
    static const DWORD constants_rootsig1[] =
    {
        0x43425844, 0xaa6e3eb1, 0x092b0bd3, 0x63af9657, 0xa97a0fe4, 0x00000001, 0x00000074, 0x00000001,
        0x00000024, 0x30535452, 0x00000048, 0x00000002, 0x00000002, 0x00000018, 0x00000000, 0x00000048,
        0x00000000, 0x00000001, 0x00000000, 0x00000030, 0x00000001, 0x00000000, 0x0000003c, 0x00000004,
        0x00000000, 0x00000003, 0x00000005, 0x00000003, 0x00000004,
    };
    static const D3D12_ROOT_PARAMETER constants_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {4, 0, 3}},
        {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {5, 3, 4}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC constants_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(constants_parameters),
        .pParameters = constants_parameters,
    };
    static const D3D12_ROOT_PARAMETER1 constants_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {4, 0, 3}},
        {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {5, 3, 4}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 constants_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(constants_parameters1),
        .pParameters = constants_parameters1,
    };

#if 0
    #define RS "DescriptorTable(CBV(b1, space = 7), " \
            "SRV(t16, numDescriptors = 8), " \
            "UAV(u3, numDescriptors = unbounded, offset = 44))"
#endif
    static const DWORD descriptor_table_rootsig[] =
    {
        0x43425844, 0x0f92e563, 0x4766993f, 0x2304e283, 0x14f0d8dc, 0x00000001, 0x00000094, 0x00000001,
        0x00000024, 0x30535452, 0x00000068, 0x00000001, 0x00000001, 0x00000018, 0x00000000, 0x00000068,
        0x00000000, 0x00000000, 0x00000000, 0x00000024, 0x00000003, 0x0000002c, 0x00000002, 0x00000001,
        0x00000001, 0x00000007, 0xffffffff, 0x00000000, 0x00000008, 0x00000010, 0x00000000, 0xffffffff,
        0x00000001, 0xffffffff, 0x00000003, 0x00000000, 0x0000002c,
    };
    static const DWORD descriptor_table_rootsig1[] =
    {
        0x43425844, 0x739302ac, 0x9db37f96, 0x1ad9eec8, 0x7a5d08cb, 0x00000001, 0x000000a0, 0x00000001,
        0x00000024, 0x30535452, 0x00000074, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000074,
        0x00000000, 0x00000000, 0x00000000, 0x00000024, 0x00000003, 0x0000002c, 0x00000002, 0x00000001,
        0x00000001, 0x00000007, 0x00000000, 0xffffffff, 0x00000000, 0x00000008, 0x00000010, 0x00000000,
        0x00000000, 0xffffffff, 0x00000001, 0xffffffff, 0x00000003, 0x00000000, 0x00000000, 0x0000002c,
    };
    static const D3D12_DESCRIPTOR_RANGE descriptor_ranges[] =
    {
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,        1,  1, 7, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,        8, 16, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX,  3, 0,                                   44},
    };
    static const D3D12_ROOT_PARAMETER descriptor_table_parameters[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable = {ARRAY_SIZE(descriptor_ranges), descriptor_ranges}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC descriptor_table_rootsig_desc =
    {
        .NumParameters = ARRAY_SIZE(descriptor_table_parameters),
        .pParameters = descriptor_table_parameters,
    };
    static const D3D12_DESCRIPTOR_RANGE1 descriptor_ranges1[] =
    {
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,        1,  1, 7, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,        8, 16, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX,  3, 0, 0,                                   44},
    };
    static const D3D12_ROOT_PARAMETER1 descriptor_table_parameters1[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable = {ARRAY_SIZE(descriptor_ranges1), descriptor_ranges1}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 descriptor_table_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(descriptor_table_parameters1),
        .pParameters = descriptor_table_parameters1,
    };

#if 0
    #define RS "DescriptorTable(CBV(b1, space = 7, flags = DESCRIPTORS_VOLATILE), " \
            "SRV(t16, numDescriptors = 8, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
            "UAV(u3, numDescriptors = unbounded, offset = 44, flags = DATA_STATIC))"
#endif
    static const DWORD descriptor_table_flags_rootsig1[] =
    {
        0x43425844, 0xe77ffa8f, 0xfab552d5, 0x586e15d4, 0x4c186c26, 0x00000001, 0x000000a0, 0x00000001,
        0x00000024, 0x30535452, 0x00000074, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000074,
        0x00000000, 0x00000000, 0x00000000, 0x00000024, 0x00000003, 0x0000002c, 0x00000002, 0x00000001,
        0x00000001, 0x00000007, 0x00000001, 0xffffffff, 0x00000000, 0x00000008, 0x00000010, 0x00000000,
        0x00000003, 0xffffffff, 0x00000001, 0xffffffff, 0x00000003, 0x00000000, 0x00000008, 0x0000002c,
    };
    static const D3D12_DESCRIPTOR_RANGE1 descriptor_ranges1_flags[] =
    {
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,        1,  1, 7,
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,        8, 16, 0,
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX,  3, 0,
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 44},
    };
    static const D3D12_ROOT_PARAMETER1 descriptor_table_parameters1_flags[] =
    {
        {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable = {ARRAY_SIZE(descriptor_ranges1_flags), descriptor_ranges1_flags}},
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 descriptor_table_flags_rootsig_desc1 =
    {
        .NumParameters = ARRAY_SIZE(descriptor_table_parameters1_flags),
        .pParameters = descriptor_table_parameters1_flags,
    };

#if 0
    #define RS "StaticSampler(s4)"
#endif
    static const DWORD default_static_sampler_rootsig[] =
    {
        0x43425844, 0x2876b8ff, 0x935aaa0d, 0x5d2d344a, 0xe002147c, 0x00000001, 0x00000078, 0x00000001,
        0x00000024, 0x30535452, 0x0000004c, 0x00000001, 0x00000000, 0x00000018, 0x00000001, 0x00000018,
        0x00000000, 0x00000055, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x00000010, 0x00000004,
        0x00000002, 0x00000000, 0x7f7fffff, 0x00000004, 0x00000000, 0x00000000,
    };
    static const DWORD default_static_sampler_rootsig1[] =
    {
        0x43425844, 0x52b07945, 0x997c0a1e, 0xe4efb9e9, 0x0378e2d4, 0x00000001, 0x00000078, 0x00000001,
        0x00000024, 0x30535452, 0x0000004c, 0x00000002, 0x00000000, 0x00000018, 0x00000001, 0x00000018,
        0x00000000, 0x00000055, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x00000010, 0x00000004,
        0x00000002, 0x00000000, 0x7f7fffff, 0x00000004, 0x00000000, 0x00000000,
    };
    static const D3D12_STATIC_SAMPLER_DESC default_static_sampler_desc =
    {
        .Filter = D3D12_FILTER_ANISOTROPIC,
        .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MaxAnisotropy = 16,
        .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
        .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        .MaxLOD = D3D12_FLOAT32_MAX,
        .ShaderRegister = 4,
    };
    static const D3D12_ROOT_SIGNATURE_DESC default_static_sampler_rootsig_desc =
    {
        .NumStaticSamplers = 1,
        .pStaticSamplers = &default_static_sampler_desc,
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 default_static_sampler_rootsig_desc1 =
    {
        .NumStaticSamplers = 1,
        .pStaticSamplers = &default_static_sampler_desc,
    };

#if 0
    #define RS "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_POINT, " \
            "addressV = TEXTURE_ADDRESS_CLAMP, visibility = SHADER_VISIBILITY_PIXEL), " \
            "StaticSampler(s0, filter = FILTER_MIN_MAG_POINT_MIP_LINEAR, " \
            "AddressW = TEXTURE_ADDRESS_BORDER, MipLODBias = 1, maxLod = 10, " \
            "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK, space = 3)"
#endif
    static const DWORD static_samplers_rootsig[] =
    {
        0x43425844, 0x52ed526c, 0x892c2d7c, 0xb8ab1123, 0x7e3a727d, 0x00000001, 0x000000ac, 0x00000001,
        0x00000024, 0x30535452, 0x00000080, 0x00000001, 0x00000000, 0x00000018, 0x00000002, 0x00000018,
        0x00000000, 0x00000000, 0x00000001, 0x00000003, 0x00000001, 0x00000000, 0x00000010, 0x00000004,
        0x00000002, 0x00000000, 0x7f7fffff, 0x00000000, 0x00000000, 0x00000005, 0x00000001, 0x00000001,
        0x00000001, 0x00000004, 0x3f800000, 0x00000010, 0x00000004, 0x00000001, 0x00000000, 0x41200000,
        0x00000000, 0x00000003, 0x00000000,
    };
    static const DWORD static_samplers_rootsig1[] =
    {
        0x43425844, 0xcf44eb9e, 0xdbeaed6b, 0xb8d52b6f, 0x0be01c3b, 0x00000001, 0x000000ac, 0x00000001,
        0x00000024, 0x30535452, 0x00000080, 0x00000002, 0x00000000, 0x00000018, 0x00000002, 0x00000018,
        0x00000000, 0x00000000, 0x00000001, 0x00000003, 0x00000001, 0x00000000, 0x00000010, 0x00000004,
        0x00000002, 0x00000000, 0x7f7fffff, 0x00000000, 0x00000000, 0x00000005, 0x00000001, 0x00000001,
        0x00000001, 0x00000004, 0x3f800000, 0x00000010, 0x00000004, 0x00000001, 0x00000000, 0x41200000,
        0x00000000, 0x00000003, 0x00000000,
    };
    static const D3D12_STATIC_SAMPLER_DESC static_sampler_descs[] =
    {
        {
            .Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .MaxAnisotropy = 16,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
            .MaxLOD = D3D12_FLOAT32_MAX,
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
        },
        {
            .Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            .MipLODBias = 1.0f,
            .MaxAnisotropy = 16,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            .MaxLOD = 10.0f,
            .RegisterSpace = 3,
        }
    };
    static const D3D12_ROOT_SIGNATURE_DESC static_samplers_rootsig_desc =
    {
        .NumStaticSamplers = ARRAY_SIZE(static_sampler_descs),
        .pStaticSamplers = static_sampler_descs,
    };
    static const D3D12_ROOT_SIGNATURE_DESC1 static_samplers_rootsig_desc1 =
    {
        .NumStaticSamplers = ARRAY_SIZE(static_sampler_descs),
        .pStaticSamplers = static_sampler_descs,
    };

    static const struct test
    {
        D3D12_SHADER_BYTECODE code;
        D3D12_SHADER_BYTECODE code1;
        const D3D12_ROOT_SIGNATURE_DESC *desc;
        const D3D12_ROOT_SIGNATURE_DESC1 *desc1;
    }
    tests[] =
    {
        {
            {empty_rootsig, sizeof(empty_rootsig)},
            {empty_rootsig1, sizeof(empty_rootsig1)},
            &empty_rootsig_desc, &empty_rootsig_desc1,
        },
        {
            {ia_rootsig, sizeof(ia_rootsig)},
            {ia_rootsig1, sizeof(ia_rootsig1)},
            &ia_rootsig_desc, &ia_rootsig_desc1,
        },
        {
            {deny_ps_rootsig, sizeof(deny_ps_rootsig)},
            {deny_ps_rootsig1, sizeof(deny_ps_rootsig1)},
            &deny_ps_rootsig_desc, &deny_ps_rootsig_desc1,
        },
        {
            {cbv_rootsig, sizeof(cbv_rootsig)},
            {cbv_rootsig1, sizeof(cbv_rootsig1)},
            &cbv_rootsig_desc, &cbv_rootsig_desc1,
        },
        {
            {cbv2_rootsig, sizeof(cbv2_rootsig)},
            {cbv2_rootsig1, sizeof(cbv2_rootsig1)},
            &cbv2_rootsig_desc, &cbv2_rootsig_desc1,
        },
        {
            {srv_rootsig, sizeof(srv_rootsig)},
            {srv_rootsig1, sizeof(srv_rootsig1)},
            &srv_rootsig_desc, &srv_rootsig_desc1,
        },
        {
            {uav_rootsig, sizeof(uav_rootsig)},
            {uav_rootsig1, sizeof(uav_rootsig1)},
            &uav_rootsig_desc, &uav_rootsig_desc1,
        },
        {
            {NULL},
            {root_descriptors_rootsig1, sizeof(root_descriptors_rootsig1)},
            &root_descriptors_desc, &root_descriptors_desc1,
        },
        {
            {constants_rootsig, sizeof(constants_rootsig)},
            {constants_rootsig1, sizeof(constants_rootsig1)},
            &constants_rootsig_desc, &constants_rootsig_desc1,
        },
        {
            {descriptor_table_rootsig, sizeof(descriptor_table_rootsig)},
            {descriptor_table_rootsig1, sizeof(descriptor_table_rootsig1)},
            &descriptor_table_rootsig_desc, &descriptor_table_rootsig_desc1,
        },
        {
            {NULL},
            {descriptor_table_flags_rootsig1, sizeof(descriptor_table_flags_rootsig1)},
            &descriptor_table_rootsig_desc, &descriptor_table_flags_rootsig_desc1,
        },
        {
            {default_static_sampler_rootsig, sizeof(default_static_sampler_rootsig)},
            {default_static_sampler_rootsig1, sizeof(default_static_sampler_rootsig1)},
            &default_static_sampler_rootsig_desc, &default_static_sampler_rootsig_desc1,
        },
        {
            {static_samplers_rootsig, sizeof(static_samplers_rootsig)},
            {static_samplers_rootsig1, sizeof(static_samplers_rootsig1)},
            &static_samplers_rootsig_desc, &static_samplers_rootsig_desc1,
        },
    };

    hr = D3D12CreateRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_IUnknown, (void **)&deserializer);
    ok(hr == E_NOINTERFACE, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&deserializer);
    ok(hr == E_NOINTERFACE, "Got unexpected hr %#x.\n", hr);

    hr = D3D12CreateRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_ID3D12RootSignatureDeserializer, (void **)&deserializer);
    ok(hr == S_OK, "Failed to create deserializer, hr %#x.\n", hr);

    check_interface(deserializer, &IID_IUnknown, false);
    check_interface(deserializer, &IID_ID3D12RootSignatureDeserializer, true);
    check_interface(deserializer, &IID_ID3D12VersionedRootSignatureDeserializer, false);
    check_interface(deserializer, &IID_ID3D12Object, false);
    check_interface(deserializer, &IID_ID3D12DeviceChild, false);
    check_interface(deserializer, &IID_ID3D12Pageable, false);

    refcount = ID3D12RootSignatureDeserializer_Release(deserializer);
    ok(!refcount, "ID3D12RootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        const struct test *t = &tests[i];

        vkd3d_test_set_context("Test %u", i);

        check_root_signature_deserialization(&t->code, t->desc, t->desc1);
        check_root_signature_serialization(&t->code, t->desc);

        blob = (ID3DBlob *)(uintptr_t)0xdeadbeef;
        hr = D3D12SerializeRootSignature(t->desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, NULL);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
        ok(blob == (ID3DBlob *)(uintptr_t)0xdeadbeef, "Got unexpected blob %p.\n", blob);

        if (!pfn_D3D12CreateVersionedRootSignatureDeserializer)
            continue;

        check_root_signature_deserialization1(&t->code1, t->desc, t->desc1);
        check_root_signature_serialization1(&t->code1, t->desc1);
    }
    vkd3d_test_set_context(NULL);

    if (!pfn_D3D12CreateVersionedRootSignatureDeserializer)
    {
        skip("D3D12CreateVersionedRootSignatureDeserializer is not available.\n");
        return;
    }

    hr = pfn_D3D12CreateVersionedRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_IUnknown, (void **)&versioned_deserializer);
    ok(hr == E_NOINTERFACE, "Got unexpected hr %#x.\n", hr);
    hr = pfn_D3D12CreateVersionedRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_ID3D12RootSignatureDeserializer, (void **)&versioned_deserializer);
    ok(hr == E_NOINTERFACE, "Got unexpected hr %#x.\n", hr);

    hr = pfn_D3D12CreateVersionedRootSignatureDeserializer(empty_rootsig, sizeof(empty_rootsig),
            &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&versioned_deserializer);
    ok(hr == S_OK, "Failed to create deserializer, hr %#x.\n", hr);

    check_interface(versioned_deserializer, &IID_IUnknown, false);
    check_interface(versioned_deserializer, &IID_ID3D12RootSignatureDeserializer, false);
    check_interface(versioned_deserializer, &IID_ID3D12VersionedRootSignatureDeserializer, true);
    check_interface(versioned_deserializer, &IID_ID3D12Object, false);
    check_interface(versioned_deserializer, &IID_ID3D12DeviceChild, false);
    check_interface(versioned_deserializer, &IID_ID3D12Pageable, false);

    refcount = ID3D12VersionedRootSignatureDeserializer_Release(versioned_deserializer);
    ok(!refcount, "ID3D12VersionedRootSignatureDeserializer has %u references left.\n", (unsigned int)refcount);
}

void test_root_signature_priority(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *shader_root_signature;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12RootSignature *api_root_signature;
    ID3D12PipelineState *pipeline;
    D3D12_GPU_VIRTUAL_ADDRESS va;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *resource;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

#if 0
    RWByteAddressBuffer uav0 : register(u0);
    RWByteAddressBuffer uav1 : register(u1);

    [rootsignature("UAV(u1), UAV(u0)")]
    [numthreads(1,1,1)]
    void main() {
            uav0.Store(0u, 1u);
            uav1.Store(0u, 2u);
    }
#endif
    static const DWORD cs_code[] =
    {
        0x43425844, 0x42fd18b2, 0x996f5350, 0x1ce9d69a, 0x96324a34, 0x00000001, 0x00000138, 0x00000004,
        0x00000030, 0x00000040, 0x00000050, 0x000000e8, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000090, 0x00050051, 0x00000024,
        0x0100086a, 0x0600009d, 0x0031ee46, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0600009d,
        0x0031ee46, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x0400009b, 0x00000001, 0x00000001,
        0x00000001, 0x080000a6, 0x0021e012, 0x00000000, 0x00000000, 0x00004001, 0x00000000, 0x00004001,
        0x00000001, 0x080000a6, 0x0021e012, 0x00000001, 0x00000001, 0x00004001, 0x00000000, 0x00004001,
        0x00000002, 0x0100003e, 0x30535452, 0x00000048, 0x00000002, 0x00000002, 0x00000018, 0x00000000,
        0x00000048, 0x00000000, 0x00000004, 0x00000000, 0x00000030, 0x00000004, 0x00000000, 0x0000003c,
        0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const D3D12_SHADER_BYTECODE cs = { cs_code, sizeof(cs_code) };
    static const uint32_t expected[] = { 1u, 2u, 1u, 2u };

    if (!init_compute_test_context(&context))
        return;
    device = context.device;
    command_list = context.list;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 1;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    hr = create_root_signature(device, &root_signature_desc, &api_root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    hr = ID3D12Device_CreateRootSignature(context.device, 0, cs_code, sizeof(cs_code), &IID_ID3D12RootSignature, (void**)&shader_root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    pipeline = create_compute_pipeline_state(device, api_root_signature, cs);
    resource = create_buffer(device, D3D12_HEAP_TYPE_DEFAULT, sizeof(expected),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    va = ID3D12Resource_GetGPUVirtualAddress(resource);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, api_root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 0, va);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 1, va + sizeof(uint32_t));
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, shader_root_signature);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 0, va + 2 * sizeof(uint32_t));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 1, va + 3 * sizeof(uint32_t));
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, command_list);

    for (i = 0; i < ARRAY_SIZE(expected); i++)
    {
        uint32_t value = get_readback_uint(&rb, i, 0, 0);
        ok(value == expected[i], "Got unexpected value %u at %u, expected %u.\n", value, i, expected[i]);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(resource);
    ID3D12PipelineState_Release(pipeline);
    ID3D12RootSignature_Release(api_root_signature);
    ID3D12RootSignature_Release(shader_root_signature);
    destroy_test_context(&context);
}

void test_missing_bindings_root_signature(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *shader_root_signature;
    D3D12_COMPUTE_PIPELINE_STATE_DESC cs_desc;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12RootSignature *api_root_signature;
    ID3D12PipelineState *pipeline;
    struct test_context context;
    ID3D12Device *device;
    HRESULT hr;

#if 0
    RWByteAddressBuffer uav0 : register(u0);
    RWByteAddressBuffer uav1 : register(u1);

    [rootsignature("UAV(u1), UAV(u0)")]
    [numthreads(1, 1, 1)]
    void main() {
        uav0.Store(0u, 1u);
        uav1.Store(0u, 2u);
    }
#endif
    static const DWORD cs_code[] =
    {
        0x43425844, 0x42fd18b2, 0x996f5350, 0x1ce9d69a, 0x96324a34, 0x00000001, 0x00000138, 0x00000004,
        0x00000030, 0x00000040, 0x00000050, 0x000000e8, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000090, 0x00050051, 0x00000024,
        0x0100086a, 0x0600009d, 0x0031ee46, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0600009d,
        0x0031ee46, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x0400009b, 0x00000001, 0x00000001,
        0x00000001, 0x080000a6, 0x0021e012, 0x00000000, 0x00000000, 0x00004001, 0x00000000, 0x00004001,
        0x00000001, 0x080000a6, 0x0021e012, 0x00000001, 0x00000001, 0x00004001, 0x00000000, 0x00004001,
        0x00000002, 0x0100003e, 0x30535452, 0x00000048, 0x00000002, 0x00000002, 0x00000018, 0x00000000,
        0x00000048, 0x00000000, 0x00000004, 0x00000000, 0x00000030, 0x00000004, 0x00000000, 0x0000003c,
        0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const D3D12_SHADER_BYTECODE cs = { cs_code, sizeof(cs_code) };

    if (!init_compute_test_context(&context))
        return;
    device = context.device;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 10;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 1;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    hr = create_root_signature(device, &root_signature_desc, &api_root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    hr = ID3D12Device_CreateRootSignature(context.device, 0, cs_code, sizeof(cs_code), &IID_ID3D12RootSignature, (void **)&shader_root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&cs_desc, 0, sizeof(cs_desc));
    cs_desc.pRootSignature = api_root_signature;
    cs_desc.CS = cs;

    hr = ID3D12Device_CreateComputePipelineState(context.device, &cs_desc, &IID_ID3D12PipelineState, (void **)&pipeline);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(pipeline);

    ID3D12RootSignature_Release(api_root_signature);
    ID3D12RootSignature_Release(shader_root_signature);
    destroy_test_context(&context);
}

void test_root_signature_empty_blob(void)
{
    ID3D12RootSignature *root_signature;
    struct test_context context;
    HRESULT hr;

    static const DWORD cs_code[] =
    {
#if 0
    RWStructuredBuffer<uint> RWBuf;

    [numthreads(1, 1, 1)]
    void main(int wg : SV_GroupID)
    {
            RWBuf[wg] = wg;
    }
#endif
        0x43425844, 0x81a88c98, 0x1ab24abd, 0xfdb8fb1f, 0x7e9cb035, 0x00000001, 0x000000a8, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000054, 0x00050050, 0x00000015, 0x0100086a,
        0x0400009e, 0x0011e000, 0x00000000, 0x00000004, 0x0200005f, 0x00021012, 0x0400009b, 0x00000001,
        0x00000001, 0x00000001, 0x070000a8, 0x0011e012, 0x00000000, 0x0002100a, 0x00004001, 0x00000000,
        0x0002100a, 0x0100003e,
    };

    if (!init_compute_test_context(&context))
        return;

    hr = ID3D12Device_CreateRootSignature(context.device, 0, cs_code, sizeof(cs_code), &IID_ID3D12RootSignature, (void **)&root_signature);
    /* Has to be E_FAIL, not E_INVALIDARG, oddly enough. */
    ok(hr == E_FAIL, "Unexpected hr #%x.\n", hr);
    destroy_test_context(&context);
}
