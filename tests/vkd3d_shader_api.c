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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API
#define INITGUID
#define VKD3D_TEST_DECLARE_MAIN
#include "d3d12_crosstest.h"
#include "vkd3d_test.h"
#include <vkd3d_shader.h>
#include "vkd3d_shader_private.h"

#include <locale.h>

static void test_invalid_shaders(void)
{
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
    static const struct vkd3d_shader_code ps_break = {ps_break_code, sizeof(ps_break_code)};

    rc = vkd3d_shader_compile_dxbc(&ps_break, &spirv, VKD3D_SHADER_STRIP_DEBUG, NULL, NULL);
    ok(rc == VKD3D_ERROR_INVALID_SHADER, "Got unexpected error code %d.\n", rc);
}

static void test_vkd3d_shader_pfns(void)
{
    PFN_vkd3d_shader_serialize_root_signature pfn_vkd3d_shader_serialize_root_signature;
    PFN_vkd3d_shader_find_signature_element pfn_vkd3d_shader_find_signature_element;
    PFN_vkd3d_shader_free_shader_signature pfn_vkd3d_shader_free_shader_signature;
    PFN_vkd3d_shader_parse_input_signature pfn_vkd3d_shader_parse_input_signature;
    PFN_vkd3d_shader_parse_root_signature pfn_vkd3d_shader_parse_root_signature;
    PFN_vkd3d_shader_free_root_signature pfn_vkd3d_shader_free_root_signature;
    PFN_vkd3d_shader_free_shader_code pfn_vkd3d_shader_free_shader_code;
    PFN_vkd3d_shader_compile_dxbc pfn_vkd3d_shader_compile_dxbc;
    PFN_vkd3d_shader_scan_dxbc pfn_vkd3d_shader_scan_dxbc;
    vkd3d_shader_hash_t compat_hash;

    struct vkd3d_versioned_root_signature_desc root_signature_desc;
    struct vkd3d_shader_signature_element *element;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_signature signature;
    struct vkd3d_shader_code dxbc, spirv;
    int rc;

    static const struct vkd3d_versioned_root_signature_desc empty_rs_desc =
    {
        .version = VKD3D_ROOT_SIGNATURE_VERSION_1_0,
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

    pfn_vkd3d_shader_serialize_root_signature = vkd3d_shader_serialize_root_signature;
    pfn_vkd3d_shader_find_signature_element = vkd3d_shader_find_signature_element;
    pfn_vkd3d_shader_free_shader_signature = vkd3d_shader_free_shader_signature;
    pfn_vkd3d_shader_parse_input_signature = vkd3d_shader_parse_input_signature;
    pfn_vkd3d_shader_parse_root_signature = vkd3d_shader_parse_root_signature;
    pfn_vkd3d_shader_free_root_signature = vkd3d_shader_free_root_signature;
    pfn_vkd3d_shader_free_shader_code = vkd3d_shader_free_shader_code;
    pfn_vkd3d_shader_compile_dxbc = vkd3d_shader_compile_dxbc;
    pfn_vkd3d_shader_scan_dxbc = vkd3d_shader_scan_dxbc;

    rc = pfn_vkd3d_shader_serialize_root_signature(&empty_rs_desc, &dxbc);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    rc = pfn_vkd3d_shader_parse_root_signature(&dxbc, &root_signature_desc, &compat_hash);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    pfn_vkd3d_shader_free_root_signature(&root_signature_desc);
    pfn_vkd3d_shader_free_shader_code(&dxbc);

    rc = pfn_vkd3d_shader_parse_input_signature(&vs, &signature);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    element = pfn_vkd3d_shader_find_signature_element(&signature, "position", 0, 0);
    ok(element, "Could not find shader signature element.\n");
    pfn_vkd3d_shader_free_shader_signature(&signature);

    rc = pfn_vkd3d_shader_compile_dxbc(&vs, &spirv, 0, NULL, NULL);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
    pfn_vkd3d_shader_free_shader_code(&spirv);

    memset(&scan_info, 0, sizeof(scan_info));
    rc = pfn_vkd3d_shader_scan_dxbc(&vs, &scan_info);
    ok(rc == VKD3D_OK, "Got unexpected error code %d.\n", rc);
}

static void test_vkd3d_dxbc_checksum(void)
{
    /* Sanity check output so we can verified we migrated correctly. */
    uint32_t checksum[4];
    uint8_t code[256];
    size_t i, size;

    static const uint32_t expected_results[][4] = {
        { 0xb056e814, 0x63f1ae31, 0x32241b97, 0x6f09f7cb },
        { 0x9c13f928, 0x2e55bd04, 0x34e240ca, 0xcce18d5c },
        { 0xab86554e, 0xcaa7e724, 0x54a5aff4, 0xabdb4a35 },
        { 0x8b4dae7e, 0xb9e3f732, 0x84614089, 0xd27b105e },
        { 0xc3c1705b, 0xe7b3ae45, 0x19035e2e, 0xfbd4d2e1 },
        { 0xa95bf6bb, 0xa76a9242, 0xcd95028e, 0x0afe32d8 },
        { 0x96d97753, 0x62ebd472, 0x57eb159b, 0x1b1f418f },
        { 0x10794b48, 0x6e193a8a, 0xd551b733, 0x130954ac },
        { 0x26044f32, 0x868fd691, 0xc7af44b0, 0x6a19533a },
        { 0x43187d41, 0x43d30bb9, 0x80ff212c, 0x373ffbf2 },
        { 0x834c0e76, 0x6e0d2390, 0x51742fae, 0xb0e05724 },
        { 0x8740703f, 0xed6c12bf, 0xf56a6f06, 0x18027b89 },
        { 0xd0dcd098, 0xec68ca76, 0x2fee6683, 0x7e79f7cd },
        { 0x0a253664, 0xbb6041d7, 0x6d40f215, 0xb58f1136 },
        { 0x2632faeb, 0xfd8132e2, 0xcfdd237a, 0x3967bcd5 },
        { 0x785c8bc4, 0x25da3bd0, 0x4a33a193, 0x42856657 },
        { 0xa816efea, 0x05ec405d, 0xec4db22e, 0x1e34f70e },
        { 0xa7feaf8d, 0xdd74ff24, 0xbf4abf35, 0x0bfaf265 },
        { 0x673783b7, 0xfbeb99b4, 0x5ac905a4, 0xb86795fb },
        { 0xc96c60be, 0x68d0d582, 0x0fff7b0d, 0xc6b3123e },
        { 0x4a9cf16d, 0x6dfd3595, 0x5bf4c6e1, 0x1dcb7f49 },
        { 0x0285106a, 0x98fb8e44, 0x9282c103, 0x0cb5dd41 },
        { 0x098867bf, 0xe0b60a8e, 0xa48ab924, 0x568cf867 },
        { 0x8968718d, 0x7a81afdb, 0x80a9cef1, 0xcf0d96d9 },
        { 0xdf8f0654, 0x6f18e508, 0x9feb8c85, 0x3f572c75 },
        { 0x309e1ab7, 0x5bc33dee, 0xf38c34fc, 0xfc9364ea },
        { 0xb5520b7f, 0xc301819d, 0xb48c68da, 0x45871cc0 },
        { 0x25912d69, 0x7fd20ef3, 0x077989b6, 0x32709438 },
        { 0xeeaee0ac, 0xe786da6f, 0xbda8ceaa, 0xde7d6cb8 },
        { 0xdd88eba3, 0x952ec627, 0xe27332c6, 0x74e42137 },
        { 0xea1748be, 0xe75eb5c0, 0xe34b1f96, 0x96fafeb2 },
        { 0xda43f677, 0xb37e358f, 0x670a11dc, 0xcb03dd3f },
        { 0x3d401ed8, 0x9fc387d9, 0x32d46df2, 0x467f4f5b },
        { 0x1ad223eb, 0x3a765806, 0xcc808561, 0x43e70660 },
        { 0x1d8ab102, 0x2893d901, 0x666b5075, 0xca7d0cca },
        { 0x52f1915c, 0xd371c993, 0xc7dd381f, 0x2264fff1 },
        { 0x10fc364f, 0xade0a514, 0x00d8c734, 0x526b0904 },
        { 0xa6771919, 0xffd1370d, 0x9efc5832, 0x6f6341a4 },
        { 0x08d851f7, 0x08a57b15, 0xdbca0117, 0xd7c8a72a },
        { 0x384c3e7f, 0xcf9b9009, 0x662e6019, 0x4411db77 },
        { 0xd94963c6, 0xb8b0a957, 0x44abede6, 0x0f382f21 },
        { 0xd11014eb, 0x46596dba, 0x1b8b437f, 0x981a0dd1 },
        { 0x5744ee21, 0xf50f2317, 0x23b897a6, 0xbe915ba6 },
        { 0x296613ea, 0xc063e065, 0x24e1c10c, 0x5bf5863c },
        { 0xb711006d, 0x9edfa79f, 0xbf18770c, 0x1549f277 },
        { 0x80480205, 0x6ff8eb6f, 0xfcc2c55b, 0x0c39c9fa },
        { 0x6da2e9f8, 0xda69138f, 0x53ddebbb, 0xc445a1fb },
        { 0x9dfc0038, 0x1a1a7662, 0x622c6264, 0x7a6f8547 },
        { 0x35c7885b, 0x722b99ff, 0x9710a2e1, 0x2f905cdf },
        { 0x69d9f389, 0x2f49436b, 0x015afc83, 0x340dd46f },
        { 0x91aad09e, 0x02751766, 0x71c20cd4, 0x8c6c99f4 },
        { 0x5c602d7b, 0x23f1ec25, 0xe196f064, 0x58c5eb45 },
        { 0x0018ec32, 0x9f86a342, 0x3db1c6d7, 0x7a50eac8 },
        { 0x161127fb, 0xf8a52295, 0xa2cc9a23, 0x4c9c36a3 },
        { 0xa5868d00, 0x9d618be7, 0x1ebb5ba5, 0xec6d00c9 },
        { 0x8007c6a6, 0x42d63d13, 0x455c959f, 0x62414d07 },
        { 0x9ce0ba86, 0x5c32b41e, 0x731fcc76, 0x7f9373a6 },
        { 0xdba0e5ff, 0xf6cd120c, 0x818fcbd2, 0x99060636 },
        { 0xc352078d, 0x540393fc, 0x19c71d2d, 0xcf995c94 },
        { 0xc0b2bfbf, 0xded86852, 0x104588ae, 0x80321491 },
        { 0x3de2a48d, 0xbbd8b631, 0xba38f9c5, 0x4665d6c1 },
        { 0x713f05c9, 0x65b677fa, 0xa5b13fdb, 0x387ee0fd },
        { 0x307beee5, 0x7de06113, 0x0ae69e07, 0xca2e6a92 },
        { 0x06adf74b, 0x5fb916d2, 0x2ebe5340, 0x200e4eea },
        { 0x4cd59add, 0xb38ad558, 0x69b13471, 0x298ffa6e },
        { 0x34046e3b, 0xa2c6ee1a, 0xb39ecea4, 0xe2bdf54d },
        { 0x4294c65e, 0x12f2dc43, 0x74159cce, 0x17e84cf5 },
        { 0xe0d72eb6, 0x3d6081b2, 0x3634978c, 0x7501833e },
        { 0xc15bd438, 0x0d7a1c16, 0xa2cfc35b, 0x7f3cac2c },
        { 0x55e72a12, 0x91f70512, 0x417a07dd, 0xcd000fd4 },
        { 0x99d8b5e0, 0x5c51e85f, 0x077b949a, 0x8130c193 },
        { 0x38e8b829, 0x61c09815, 0x3aa67906, 0x3ded0a6c },
        { 0xab5fc87e, 0x352a176f, 0x4505118e, 0x1281436d },
        { 0xf04f6093, 0xe7bf7388, 0x08f01c78, 0xcf3edfdf },
        { 0x9348d7ac, 0x471c6630, 0x53e52a51, 0x52b2daa5 },
        { 0xe2c28a9a, 0xd5bb2f12, 0xe6f763da, 0x01ea256a },
        { 0x38ca01b6, 0x067334f4, 0xcb5ef634, 0x09f395a4 },
        { 0x8d8ffc3e, 0x6b2d1d84, 0x9c15945d, 0xe91cb47a },
        { 0x7228de18, 0x1ce8cd86, 0xace421d8, 0xfef98fd2 },
        { 0x059bebfa, 0xef024248, 0xdbf055b5, 0x2958bce2 },
        { 0x671fe83c, 0x42545cd5, 0xc0ccefd1, 0xbb175617 },
        { 0xa175749a, 0xf7b549ad, 0x7fc55802, 0x4283446d },
        { 0xd66b4792, 0xfd23ad79, 0xc08037ea, 0x94ee2332 },
        { 0xf28991ab, 0x5545bbdc, 0x5e7fbfb1, 0xb708bd82 },
        { 0x65233214, 0x19806ad9, 0xed5810c6, 0x058ff99d },
        { 0xf6ba8625, 0xf43a5f50, 0xf6bca3b7, 0x3906f444 },
        { 0x4282382c, 0xc8fac4bc, 0x5ac5a2be, 0x38851781 },
        { 0x3ef310a3, 0x77fb424a, 0x58adc64d, 0x180a7903 },
        { 0xdf7824e9, 0x884fffbb, 0x69b62bd6, 0xbae40b21 },
        { 0xd6361da1, 0x7fef0a74, 0xd58e6b55, 0x66f317cd },
        { 0xcd926b93, 0xb6990b85, 0xacca7208, 0x18224144 },
        { 0x3d774c51, 0xfe8302c4, 0xc400e1a9, 0x85037353 },
        { 0x42ab6cff, 0x3b184fd2, 0x72680eb2, 0x31e24e23 },
        { 0x92be9d60, 0x85ad69b4, 0xc43817b9, 0x1742ef34 },
        { 0xf0f2809f, 0xdaee1cf0, 0xe9158c20, 0x51b9507c },
        { 0xea6bf944, 0x1b65d443, 0x38460b27, 0xfe01a562 },
        { 0xb47d95f6, 0xdb1bbcfc, 0xcde4b674, 0x028d1980 },
        { 0x23c33145, 0x61298690, 0x1a676534, 0x62db6f67 },
        { 0xde4b6006, 0x9f77198b, 0x3d76c520, 0x0b8c0a75 },
        { 0x88092da2, 0x419f5044, 0x1efee5b1, 0x3a83152a },
        { 0x06e9d605, 0x0771e748, 0x2da34c5b, 0x39463345 },
        { 0x365f4717, 0xeaad01ff, 0xfebbd378, 0x19eef9dd },
        { 0x20082f66, 0x0d159512, 0x44fefe13, 0xa64d6caa },
        { 0x5d4289de, 0x64bcc682, 0x4239b048, 0x3ad50b8a },
        { 0xe2f0ed00, 0x6d0da128, 0x4098bcad, 0x6eb7dec2 },
        { 0xb940905b, 0xb9ef115d, 0x22d05b7c, 0xb96ab37b },
        { 0x7b532659, 0x05751164, 0x31f53345, 0x87fd962f },
        { 0x3fff7969, 0x1d03b9ba, 0x047d1547, 0x7e16c1f8 },
        { 0x5161191e, 0x0024c8c2, 0xc7f64b15, 0xe4b80b31 },
        { 0x2c555498, 0xc31b839d, 0x9b53d8b6, 0xe67e3f8c },
        { 0xd8d8c53a, 0xdb8061cb, 0x59eb8b1f, 0x621af173 },
        { 0x10e2c249, 0x7a6c2dee, 0x59dca8e6, 0xc7c355fa },
        { 0xdcfc1270, 0x6e9b3c7e, 0x6b545c21, 0x7a0ffd29 },
        { 0x0346039d, 0x46c89230, 0xf67f41df, 0x458c34f9 },
        { 0x3b1951b8, 0xa366be0b, 0xe6a8b89f, 0x7c8b6cef },
        { 0x97f61275, 0x7702dc26, 0x42b58f67, 0x8dde1062 },
        { 0xb8503307, 0x5285f35c, 0xe4003efa, 0x68014c57 },
        { 0x689e4931, 0x7f595b8e, 0x625bc749, 0xf3cb5723 },
        { 0x766a5c99, 0x6d6f104a, 0xd4027dee, 0x2cbcb6a5 },
        { 0xcf69f71a, 0xbde9fa11, 0xb9418985, 0x08180cca },
        { 0xdaf83348, 0xc622b68b, 0xafefad36, 0xec16e8bf },
        { 0xf0465f6e, 0x51ad1919, 0x514e86d3, 0x6bd253e9 },
        { 0x8537b308, 0xd7b5af5c, 0x3bb54d49, 0x3ce8da79 },
        { 0xc3e845bf, 0xfc3dece5, 0x9576e3e1, 0x3ddec605 },
        { 0x40cbd778, 0xba052d53, 0xb7836380, 0xcc266b62 },
        { 0xdd8fcae6, 0xa2567d29, 0x9db6b5b5, 0xe9660c94 },
        { 0x3baeace4, 0x28cad2fe, 0xb4303893, 0x1914c0a4 },
        { 0xd6feb143, 0x7fbe826d, 0x58f51b07, 0x3ee29b1f },
        { 0x44ea0549, 0x6ac66495, 0x95dcf8a9, 0x1bc9be57 },
        { 0x99148385, 0xaeaeb778, 0x830ea622, 0xd50aaae2 },
        { 0xab3b8d78, 0x825b2105, 0x663d9ad7, 0x9e6a8e6d },
        { 0x3752283e, 0xaf59b432, 0x826eeab2, 0x467eba63 },
        { 0x2fc5583b, 0x9bf5e847, 0x76299530, 0x1a67331b },
        { 0xde7e5094, 0x70c83474, 0x6de7a384, 0xf663b1d8 },
        { 0x31020e62, 0x8c942258, 0xb3f3e41e, 0x7dbcbaa6 },
        { 0xf7c93e80, 0x9b0da534, 0x1ad5ed7f, 0x0ae325e8 },
        { 0x27057339, 0x409bb701, 0xa55f6450, 0x356157b5 },
        { 0x09c2ccac, 0x79534d3e, 0xde023a5e, 0xb6f48363 },
        { 0x53847bed, 0xd28eba03, 0x5e02dec6, 0xed0e0dcc },
        { 0x7d79404f, 0xd8891724, 0xa0bafed8, 0x8d168508 },
        { 0x546afe9c, 0x9029370b, 0xdbe6fc0e, 0xa58b5381 },
        { 0xb54ed308, 0x7cf6fcdb, 0xfd8fe6ec, 0x2f24a8c3 },
        { 0x0789328b, 0x69121092, 0x62b806f4, 0x78862c6d },
        { 0x15c86057, 0xed665999, 0x292271f2, 0x8a0c535f },
        { 0xe1295aaf, 0x656dcc3f, 0x603b21d7, 0x03e64d24 },
        { 0xedaf3f0d, 0xff06b7da, 0x65c1a7b4, 0xd7972dc2 },
        { 0x337ca758, 0xbe4e6781, 0xfe66c394, 0x653b3605 },
        { 0x47cd7993, 0x42863aec, 0x60c89e80, 0x1bc0c03a },
        { 0xb6507f3e, 0x4bbb6c00, 0x9846de40, 0x947e9678 },
        { 0xdb92da53, 0xfe9150bf, 0xd170831c, 0x8dfa3b4e },
        { 0xfec23b21, 0xf9c8c9b9, 0x537acc0c, 0x314c3079 },
        { 0x1ef24dec, 0xf5877468, 0xbc45ad3e, 0x689b8c8b },
        { 0x65e0521f, 0xadc18269, 0xc305a33c, 0x7540f5ea },
        { 0x205e634d, 0x16905d02, 0x1db56950, 0x56b86264 },
        { 0x756421d3, 0xd1a8ac84, 0x75ce9ba9, 0x9aa0b4d3 },
        { 0x0844b820, 0xd785615d, 0x9f2938d4, 0x9ecefd8e },
        { 0x38ad708e, 0x14857f44, 0x2d61b7d9, 0x9aad036d },
        { 0xf317830e, 0x5460c81e, 0x933b51ff, 0xd0453cc3 },
        { 0xb3213a87, 0xfa8b754b, 0xca208e59, 0xb6aba847 },
        { 0xdd3dd516, 0x51678e75, 0x5ef24e9c, 0x767b4cd5 },
        { 0xe9d0a81c, 0xb8460161, 0x24e59ae1, 0xbcec59db },
        { 0xfc6133bb, 0x870fa56a, 0x805a843d, 0x35748761 },
        { 0x586e4ae7, 0xb353aa3e, 0x0bfd5097, 0xaf919638 },
        { 0xe794dd15, 0xedba1d19, 0x46ece2f7, 0x343c0fde },
        { 0x3b4e878c, 0x9ce15feb, 0x45f17445, 0x5cd28fde },
        { 0x09571e4d, 0xeec43744, 0x4c5757f6, 0xf23d0ac9 },
        { 0xc342484e, 0x228283d5, 0x9f7941a5, 0xc6d86a46 },
        { 0x3f2938b9, 0x5b282285, 0xf36b9a6f, 0x9fe7ad34 },
        { 0xbb583f6e, 0x3bcacc60, 0x84e31222, 0xf921a7d9 },
        { 0xfdbb7e49, 0x66f63300, 0x214cefad, 0x6a25a922 },
        { 0x14af5aa1, 0x1a9ddf95, 0x2913e1cf, 0xfc7e3da9 },
        { 0x04a8563c, 0x7d1af49e, 0xb6953490, 0xfe955251 },
        { 0x9f25962d, 0x99737ac6, 0x5fe4fab8, 0x64eee6a3 },
        { 0x254b996a, 0x39230ac1, 0x79830e0b, 0xc52454c8 },
        { 0xeac4c333, 0x83774201, 0x0989a6d5, 0xd345d782 },
        { 0x81f1e166, 0x67ed8518, 0x1860aee0, 0x7d5576a7 },
        { 0x273cfd3d, 0x705386fc, 0x2f471835, 0xd09f928e },
        { 0x097bad86, 0xec1127b8, 0x9d3d66e7, 0x076de44c },
        { 0xc15fb62e, 0x9f8d0913, 0x4dbaac27, 0x0ff5c2f6 },
        { 0x7f36c2e5, 0x865a1408, 0xcebb4b83, 0x16025da6 },
        { 0x2cce46ab, 0x9cd4f3af, 0x75b61938, 0x0e7f58b7 },
        { 0x07e97536, 0xfe13755b, 0x2f8bbac7, 0xe0e47daf },
        { 0x27502b06, 0x04774f3f, 0x78e6457b, 0x38eccaf8 },
        { 0x74586d24, 0xa3b5d94c, 0xe983439d, 0xdd1d3aab },
        { 0xaace6f95, 0xc5b8b80f, 0xa58fc9f5, 0xbef85da8 },
        { 0x1588f8f5, 0xe89a5129, 0x4e7a810f, 0x76a16f41 },
        { 0x2b1287e3, 0x24532ef8, 0xdbe9b402, 0xdfa69472 },
        { 0x2cf259d9, 0x95e46dee, 0xf3b9bbc6, 0x0a5646cf },
        { 0x2acc1728, 0x7d52bc54, 0xcc54f46f, 0x8ea890c3 },
        { 0xb6f0dd6e, 0x71947e12, 0x0992c386, 0xd3972518 },
        { 0xdd8a6368, 0x7b23caff, 0x88d008fe, 0x0621b01d },
        { 0x8ba7f4d0, 0x6e035ddc, 0x638eec52, 0x958c5317 },
        { 0x3f216d72, 0x34b4db1e, 0x5976bf85, 0xe5d35ea3 },
        { 0x662a6693, 0x38ecacc5, 0xbd6ebb8a, 0xd4f6c373 },
        { 0x81d2cf11, 0xcb2abaa6, 0x55a7f4a6, 0xea0e6f61 },
        { 0x836e365d, 0x5b89de1b, 0xada55817, 0xb40b67be },
        { 0xcca7f10a, 0x8839da91, 0xc056ad60, 0x604a2faf },
        { 0x2dd9ff50, 0x8611892c, 0x95632342, 0x2e704fb2 },
        { 0x1e740980, 0x958d2d4e, 0x028d4a64, 0x3c5cbbf3 },
        { 0x3df83ef3, 0xef0c0ab5, 0x6b58a7da, 0xa1e03fb1 },
        { 0x039bac55, 0xf4d4afbd, 0x704d32cd, 0x51dd89d9 },
        { 0x1cdfb3e1, 0xe9a309d8, 0xfc1699e6, 0x007d21ec },
        { 0x7bce3e85, 0x92f123fb, 0x2b3ed0d2, 0xa9510d7c },
        { 0x0ea9a9cb, 0x5f0cdfef, 0xb4945c3f, 0xbd96e9c2 },
        { 0x0c7bb4cf, 0x5c1618f4, 0x2dc73d15, 0x26cbcfd6 },
        { 0x709fb517, 0x59b2c390, 0x76140392, 0xfe932df9 },
        { 0xf77c19a4, 0x17b42990, 0xa28154f6, 0xd6d0eda1 },
        { 0x99897aaf, 0x22dc1a0a, 0xada474bd, 0x37bad530 },
        { 0x3fa3e9e1, 0x29941aaf, 0x47e5420a, 0xa5a2564d },
        { 0x3cde0569, 0xaa974601, 0x4a6e4cbe, 0xc5841b47 },
        { 0xb3172e00, 0x5bd117b2, 0x8264a85d, 0x281fd232 },
        { 0xcacff20e, 0x168d7f31, 0xdeb00443, 0x4e846cc9 },
        { 0x2dfa0d48, 0xd8c3ec08, 0x561957b5, 0xb2c38393 },
        { 0xd848eadf, 0x45c25f18, 0xc18f0cdf, 0x56825204 },
        { 0x0b259f53, 0xd0304a67, 0x86243631, 0x5b5bf53a },
        { 0xba111a62, 0xe4235003, 0xbec1fee3, 0xc098e0f2 },
        { 0xae4af51c, 0xaa59563b, 0x304569f2, 0xa1dc73aa },
        { 0x32d0c193, 0xf7bb30fb, 0x66569609, 0x91d6998b },
        { 0x50946f10, 0xfdb896d3, 0xe25fe743, 0x314be3de },
        { 0x5ec42b90, 0x1dc57e33, 0x66e16874, 0xfd9d34e8 },
        { 0x8731ce66, 0x9f61be0b, 0xa1e261d0, 0xca5c5b6d },
        { 0x603dd402, 0xae89d069, 0x9157a472, 0x64a66c1f },
        { 0xdc5ce301, 0x30dd140d, 0x83a1fe25, 0x14409c0d },
        { 0xbc57bbb9, 0xcc01afc8, 0x5232a452, 0xd70652b3 },
        { 0x4e582fa5, 0x07fb05a5, 0x52de2481, 0x6f84cb86 },
        { 0x6e5b09da, 0x60b01b14, 0xe7299de3, 0xfb2ec82c },
        { 0x87abc82e, 0xe0fd3356, 0xbfff365a, 0x4a444023 },
        { 0xd1cad559, 0x37e91b20, 0xf203f25c, 0xf7ee1f9b },
        { 0x91144148, 0xf7f4f508, 0x9d1ee44e, 0x933b5d64 },
        { 0x8e298bce, 0x7dcc05e4, 0x8e78da08, 0x799c7bf1 },
        { 0x28d9d75c, 0x3df5f52a, 0x86525f94, 0x629ed12f },
        { 0x29b15cfb, 0xbba0ca64, 0xddb1dd28, 0xbcab091d },
        { 0x645b38c3, 0xcf7e52f3, 0x74b3b30a, 0xc1c11dc0 },
        { 0x8fd69ec2, 0xe72c3490, 0x824e35c7, 0x73cc3887 },
        { 0x17ab1348, 0x58cb066e, 0xc57774d1, 0xfc5456d3 },
        { 0x3f7f7574, 0x2725bea2, 0x174966ce, 0x45a2e63e },
    };

    /* We don't care about correct headers here. */
    for (i = 0; i < ARRAY_SIZE(code); i++)
        code[i] = (uint8_t)i;

    for (i = 0, size = 21; size <= sizeof(code); size++, i++)
    {
        vkd3d_compute_dxbc_checksum(code, size, checksum);
        /* Generate reference: printf("{ 0x%08x, 0x%08x, 0x%08x, 0x%08x },\n", checksum[0], checksum[1], checksum[2], checksum[3]); */
        ok(memcmp(&expected_results[i], checksum, sizeof(checksum)) == 0,
                "size = %zu, mismatch in checksum. Expected { 0x%x, 0x%x, 0x%x, 0x%x }, got { 0x%x, 0x%x, 0x%x, 0x%x }.\n",
                size, expected_results[i][0], expected_results[i][1], expected_results[i][2], expected_results[i][3],
                checksum[0], checksum[1], checksum[2], checksum[3]);
    }
}

START_TEST(vkd3d_shader_api)
{
    setlocale(LC_ALL, "");

    run_test(test_invalid_shaders);
    run_test(test_vkd3d_shader_pfns);
    run_test(test_vkd3d_dxbc_checksum);
}
