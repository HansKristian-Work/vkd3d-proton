/*
 * Copyright 2018 Józef Kucia for CodeWeavers
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

#include "vkd3d_test.h"
#include <vkd3d_shader.h>

#include <locale.h>

static void test_invalid_shaders(void)
{
    struct vkd3d_shader_compile_info info;
    struct vkd3d_shader_code spirv;
    int rc;

    static const DWORD ps_break_code[] =
    {
#if 0
        ps_4_0
        dcl_constantbuffer cb0[1], immediateIndexed
        dcl_output o0.xyzw
        if_z cb0[0].x
            mov o0.xyzw, l(1.000000,1.000000,1.000000,1.000000)
            break
        endif
        mov o0.xyzw, l(0,0,0,0)
        ret
#endif
        0x43425844, 0x1316702a, 0xb1a7ebfc, 0xf477753e, 0x72605647, 0x00000001, 0x000000f8, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
        0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000080, 0x00000040, 0x00000020,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x0400001f,
        0x0020800a, 0x00000000, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x3f800000,
        0x3f800000, 0x3f800000, 0x3f800000, 0x01000002, 0x01000015, 0x08000036, 0x001020f2, 0x00000000,
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const struct vkd3d_shader_compile_option option =
    {
        .name = VKD3D_SHADER_COMPILE_OPTION_STRIP_DEBUG,
        .value = 1,
    };

    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = NULL;
    info.source.code = ps_break_code;
    info.source.size = sizeof(ps_break_code);
    info.source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
    info.target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;
    info.options = &option;
    info.option_count = 1;
    info.log_level = VKD3D_SHADER_LOG_NONE;
    info.source_name = NULL;

    rc = vkd3d_shader_compile(&info, &spirv, NULL);
    ok(rc == VKD3D_ERROR_INVALID_SHADER, "Got unexpected error code %d.\n", rc);

    rc = vkd3d_shader_scan(&info, NULL);
    ok(rc == VKD3D_ERROR_INVALID_SHADER, "Got unexpected error code %d.\n", rc);
}

static void test_vkd3d_shader_pfns(void)
{
    PFN_vkd3d_shader_get_supported_source_types pfn_vkd3d_shader_get_supported_source_types;
    PFN_vkd3d_shader_get_supported_target_types pfn_vkd3d_shader_get_supported_target_types;
    PFN_vkd3d_shader_free_scan_descriptor_info pfn_vkd3d_shader_free_scan_descriptor_info;
    PFN_vkd3d_shader_serialize_root_signature pfn_vkd3d_shader_serialize_root_signature;
    PFN_vkd3d_shader_find_signature_element pfn_vkd3d_shader_find_signature_element;
    PFN_vkd3d_shader_free_shader_signature pfn_vkd3d_shader_free_shader_signature;
    PFN_vkd3d_shader_parse_input_signature pfn_vkd3d_shader_parse_input_signature;
    PFN_vkd3d_shader_parse_root_signature pfn_vkd3d_shader_parse_root_signature;
    PFN_vkd3d_shader_free_root_signature pfn_vkd3d_shader_free_root_signature;
    PFN_vkd3d_shader_free_shader_code pfn_vkd3d_shader_free_shader_code;
    PFN_vkd3d_shader_get_version pfn_vkd3d_shader_get_version;
    PFN_vkd3d_shader_compile pfn_vkd3d_shader_compile;
    PFN_vkd3d_shader_scan pfn_vkd3d_shader_scan;

    struct vkd3d_shader_versioned_root_signature_desc root_signature_desc;
    unsigned int major, minor, expected_major, expected_minor;
    struct vkd3d_shader_scan_descriptor_info descriptor_info;
    const enum vkd3d_shader_source_type *source_types;
    const enum vkd3d_shader_target_type *target_types;
    struct vkd3d_shader_signature_element *element;
    struct vkd3d_shader_compile_info compile_info;
    unsigned int i, j, source_count, target_count;
    struct vkd3d_shader_signature signature;
    struct vkd3d_shader_code dxbc, spirv;
    const char *version, *p;
    bool b;
    int rc;

    static const struct vkd3d_shader_versioned_root_signature_desc empty_rs_desc =
    {
        .version = VKD3D_SHADER_ROOT_SIGNATURE_VERSION_1_0,
    };
    static const DWORD vs_code[] =
    {
#if 0
        float4 main(int4 p : POSITION) : SV_Position
        {
            return p;
        }
#endif
        0x43425844, 0x3fd50ab1, 0x580a1d14, 0x28f5f602, 0xd1083e3a, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x0500002b, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const struct vkd3d_shader_code vs = {vs_code, sizeof(vs_code)};

    pfn_vkd3d_shader_get_supported_source_types = vkd3d_shader_get_supported_source_types;
    pfn_vkd3d_shader_get_supported_target_types = vkd3d_shader_get_supported_target_types;
    pfn_vkd3d_shader_free_scan_descriptor_info = vkd3d_shader_free_scan_descriptor_info;
    pfn_vkd3d_shader_serialize_root_signature = vkd3d_shader_serialize_root_signature;
    pfn_vkd3d_shader_find_signature_element = vkd3d_shader_find_signature_element;
    pfn_vkd3d_shader_free_shader_signature = vkd3d_shader_free_shader_signature;
    pfn_vkd3d_shader_parse_input_signature = vkd3d_shader_parse_input_signature;
    pfn_vkd3d_shader_parse_root_signature = vkd3d_shader_parse_root_signature;
    pfn_vkd3d_shader_free_root_signature = vkd3d_shader_free_root_signature;
    pfn_vkd3d_shader_free_shader_code = vkd3d_shader_free_shader_code;
    pfn_vkd3d_shader_get_version = vkd3d_shader_get_version;
    pfn_vkd3d_shader_compile = vkd3d_shader_compile;
    pfn_vkd3d_shader_scan = vkd3d_shader_scan;

    sscanf(PACKAGE_VERSION, "%d.%d", &expected_major, &expected_minor);
    version = pfn_vkd3d_shader_get_version(&major, &minor);
    p = strstr(version, "vkd3d-shader " PACKAGE_VERSION);
    ok(p == version, "Got unexpected version string \"%s\"\n", version);
    ok(major == expected_major, "Got unexpected major version %u.\n", major);
    ok(minor == expected_minor, "Got unexpected minor version %u.\n", minor);

    source_types = pfn_vkd3d_shader_get_supported_source_types(&source_count);
    ok(source_types, "Got unexpected source types array %p.\n", source_types);
    ok(source_count, "Got unexpected source type count %u.\n", source_count);

    b = false;
    for (i = 0; i < source_count; ++i)
    {
        target_types = pfn_vkd3d_shader_get_supported_target_types(source_types[i], &target_count);
        ok(target_types, "Got unexpected target types array %p.\n", target_types);
        ok(target_count, "Got unexpected target type count %u.\n", target_count);

        for (j = 0; j < target_count; ++j)
        {
            if (source_types[i] == VKD3D_SHADER_SOURCE_DXBC_TPF
                    && target_types[j] == VKD3D_SHADER_TARGET_SPIRV_BINARY)
                b = true;
        }
    }
    ok(b, "The dxbc-tpf source type with spirv-binary target type is not supported.\n");

    rc = pfn_vkd3d_shader_serialize_root_signature(&empty_rs_desc, &dxbc, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    rc = pfn_vkd3d_shader_parse_root_signature(&dxbc, &root_signature_desc, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    pfn_vkd3d_shader_free_root_signature(&root_signature_desc);
    pfn_vkd3d_shader_free_shader_code(&dxbc);

    rc = pfn_vkd3d_shader_parse_input_signature(&vs, &signature, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    element = pfn_vkd3d_shader_find_signature_element(&signature, "position", 0, 0);
    ok(element, "Could not find shader signature element.\n");
    pfn_vkd3d_shader_free_shader_signature(&signature);

    compile_info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    compile_info.next = NULL;
    compile_info.source = vs;
    compile_info.source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
    compile_info.target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;
    compile_info.options = NULL;
    compile_info.option_count = 0;
    compile_info.log_level = VKD3D_SHADER_LOG_NONE;
    compile_info.source_name = NULL;

    rc = pfn_vkd3d_shader_compile(&compile_info, &spirv, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    pfn_vkd3d_shader_free_shader_code(&spirv);

    memset(&descriptor_info, 0, sizeof(descriptor_info));
    descriptor_info.type = VKD3D_SHADER_STRUCTURE_TYPE_SCAN_DESCRIPTOR_INFO;
    compile_info.next = &descriptor_info;

    rc = pfn_vkd3d_shader_scan(&compile_info, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    pfn_vkd3d_shader_free_scan_descriptor_info(&descriptor_info);
}

static void test_version(void)
{
    unsigned int major, minor, expected_major, expected_minor;
    const char *version, *p;

    sscanf(PACKAGE_VERSION, "%d.%d", &expected_major, &expected_minor);

    version = vkd3d_shader_get_version(NULL, NULL);
    p = strstr(version, "vkd3d-shader " PACKAGE_VERSION);
    ok(p == version, "Got unexpected version string \"%s\"\n", version);

    major = ~0u;
    vkd3d_shader_get_version(&major, NULL);
    ok(major == expected_major, "Got unexpected major version %u.\n", major);

    minor = ~0u;
    vkd3d_shader_get_version(NULL, &minor);
    ok(minor == expected_minor, "Got unexpected minor version %u.\n", minor);

    major = minor = ~0u;
    vkd3d_shader_get_version(&major, &minor);
    ok(major == expected_major, "Got unexpected major version %u.\n", major);
    ok(minor == expected_minor, "Got unexpected minor version %u.\n", minor);
}

START_TEST(vkd3d_shader_api)
{
    setlocale(LC_ALL, "");

    run_test(test_invalid_shaders);
    run_test(test_vkd3d_shader_pfns);
    run_test(test_version);
}
