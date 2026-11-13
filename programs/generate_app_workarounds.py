#!/usr/bin/env python3

"""
Copyright 2026 Hans-Kristian Arntzen for Valve Corporation

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
"""

"""
Script to generate app workaround tables
"""

import sys
import os
import argparse
import collections
import struct
import json
import hashlib

def build_config_flags(flags):
    str = 'VKD3D_CONFIG_FLAG_INIT_STATIC('
    for flag in flags:
        str += '.' + flag + ' = 1, '
    str += ')'
    return str

def build_config_flags_small(flags):
    return '[' + ', '.join(flags) + ']'

def build_bitmask_or(base, flags, empty):
    str = ''
    for flag in flags:
        if len(str) != 0:
            str += ' | '
        str += base + '_' + flag

    return empty if len(str) == 0 else str

def build_application_features(flags):
    return build_bitmask_or('VKD3D_APPLICATION_FEATURE', flags, 'VKD3D_APPLICATION_FEATURE_OVERRIDE_NONE')

def build_shader_quirks_mask(flags):
    return build_bitmask_or('VKD3D_SHADER_QUIRK', flags, '0')

def generate_app_workaround(comparator, match, meta):
    match_str = 'VKD3D_STRING_COMPARE_' + comparator + ', '
    match_str += '"' + match + '"' + ', '
    app_str = ''

    if 'configEnable' in meta:
        str = build_config_flags(meta['configEnable'])
        app_str += 'enable(' + build_config_flags_small(meta['configEnable']) + ') '
    else:
        str = 'VKD3D_CONFIG_FLAGS_NONE'
    str += ', '

    if 'configDisable' in meta:
        str += build_config_flags(meta['configDisable'])
        app_str += 'disable(' + build_config_flags_small(meta['configDisable']) + ') '
    else:
        str += 'VKD3D_CONFIG_FLAGS_NONE'
    str += ', '

    if 'applicationFeatures' in meta:
        str += build_application_features(meta['applicationFeatures'])
        app_str += 'features(' + build_config_flags_small(meta['applicationFeatures']) + ') '
    else:
        str += 'VKD3D_APPLICATION_FEATURE_OVERRIDE_NONE'

    str += ', "app: ' + app_str + '"\n'
    return match_str + str

def generate_shader_quirks(comparator, match, quirks):
    tag = 'x' + hashlib.sha1((match + comparator).encode('utf-8')).hexdigest()

    specifics = []
    comment_str = ''

    war = 'static const struct vkd3d_shader_quirk_info quirks_' + tag + ' = {'
    
    if ('perEntryQuirks' not in quirks) and ('perHashQuirks' not in quirks):
        war += ' NULL, 0, '
    else:
        if 'perEntryQuirks' in quirks:
            entry_quirks = quirks['perEntryQuirks']
            for entry, flags in entry_quirks.items():
                mask = build_shader_quirks_mask(flags)
                specifics.append('"' + entry + '"' + ', 0, ' + mask)
                comment_str += '"' + 'entry ' + entry + ' : ' + mask + r'\n"'
        if 'perHashQuirks' in quirks:
            hash_quirks = quirks['perHashQuirks']
            for entry, flags in hash_quirks.items():
                mask = build_shader_quirks_mask(flags)
                specifics.append('NULL, 0x' + entry + 'ull, ' + mask)
                comment_str += '"' + 'hash ' + entry + ' : ' + mask + r'\n"'

        war += ' hashes_' + tag + ', ARRAY_SIZE(hashes_' + tag + '), '

    if 'globalQuirks' in quirks:
        global_quirks = build_shader_quirks_mask(quirks['globalQuirks'])
        war += global_quirks
        comment_str += '"global: ' + global_quirks + r'\n"'

    war += '};'

    if len(specifics) != 0:
        hash_str = 'static const struct vkd3d_shader_quirk_hash hashes_' + tag + '[] = {\n'
        for specific in specifics:
            hash_str += '    {' + specific + '},\n'
        hash_str += '};\n'
        war = hash_str + war

    link = 'VKD3D_STRING_COMPARE_' + comparator + ', '
    link += '"' + match + '"' + ', '
    link += '&quirks_' + tag + ', '
    link += comment_str

    return (war, link)

def main():
    parser = argparse.ArgumentParser(description = 'Script to generate app workaround tables.')
    parser.add_argument('--output', type = str, help = 'Output header path.')
    parser.add_argument('json', help = 'The workaround JSON.')

    args = parser.parse_args()
    if not args.json:
        raise AssertionError('Need json path.')
    if not args.output:
        raise AssertionError('Need output path.')

    with open(args.json, 'r') as f:
        file_data = f.read()
        json_hash = hashlib.sha1(file_data.encode('utf-8')).hexdigest()
        parsed = json.loads(file_data)

    if parsed['asset'] != 'vkd3d-proton-app-workarounds':
        raise AssertionError('Unexpected asset.')
    if parsed['version'] != 1:
        raise AssertionError('Unexpected version.')

    with open(args.output, 'w') as f:
        print('#ifndef VKD3D_PROTON_APP_WORKAROUNDS_H_', file = f)
        print('#define VKD3D_PROTON_APP_WORKAROUNDS_H_', file = f)

        print('static const char application_override_db_hash[] = "' + json_hash + '";', file = f)

        print('static const struct vkd3d_instance_application_meta application_override2[] = {', file = f)
        for app in parsed['applications']:
            if 'SteamAppID' in app:
                war = generate_app_workaround('APPID', app['SteamAppID'], app)
                print('    {', war, '},', file = f)
            if 'name' in app:
                war = generate_app_workaround(app['comparator'] if 'comparator' in app else 'EXACT', app['name'], app)
                print('    {', war, '},', file = f)
        print('};', file = f)

        links = []
        for app in parsed['applications']:
            if 'shaderQuirks' not in app:
                continue
            if 'SteamAppID' in app:
                quirks, link = generate_shader_quirks('APPID', app['SteamAppID'], app['shaderQuirks'])
                print(quirks, file = f)
                links.append(link)
            if 'name' in app:
                quirks, link = generate_shader_quirks(app['comparator'] if 'comparator' in app else 'EXACT', app['name'], app['shaderQuirks'])
                print(quirks, file = f)
                links.append(link)

        print('static const struct vkd3d_shader_quirk_meta application_shader_quirks2[] = {', file = f)
        for link in links:
            print('    {' + link + '},', file = f)
        print('};', file = f)

        print('#endif', file = f)

if __name__ == '__main__':
    main()
