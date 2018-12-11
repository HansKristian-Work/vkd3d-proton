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

START_TEST(vkd3d_shader_api)
{
    run_test(test_invalid_shaders);
}
