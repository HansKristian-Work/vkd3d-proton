/*
 * Copyright 2019 Józef Kucia for CodeWeavers
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

#include "vkd3d_common.h"
#include "vkd3d_test.h"

static void check_version(const char *v, int expected_major, int expected_minor)
{
    int major, minor;

    vkd3d_parse_version(v, &major, &minor);
    ok(major == expected_major && minor == expected_minor,
            "Got %d.%d, expected %d.%d for %s.\n",
            major, minor, expected_major, expected_minor, v);
}

static void test_parse_version(void)
{
    check_version("", 0, 0);
    check_version(".3", 0, 3);
    check_version(".4.5", 0, 4);

    check_version("1", 1, 0);
    check_version("2", 2, 0);

    check_version("1.0", 1, 0);
    check_version("1.1", 1, 1);
    check_version("2.3.0", 2, 3);
}

START_TEST(vkd3d_common)
{
    run_test(test_parse_version);
}
