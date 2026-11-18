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
Script to generate app database tables
"""

import sys
import os
import argparse
import hashlib

def main():
    parser = argparse.ArgumentParser(description = 'Script to generate app database tables.')
    parser.add_argument('--output', type = str, help = 'Output header path.')
    parser.add_argument('database', help = 'The database text file.')

    args = parser.parse_args()
    if not args.database:
        raise AssertionError('Need database path.')
    if not args.output:
        raise AssertionError('Need output path.')

    with open(args.database, 'r') as f:
        file_lines = f.readlines()
    db_hash = hashlib.sha1(('\n'.join(file_lines)).encode('utf-8')).hexdigest()

    if file_lines[0] != '{vkd3d-proton-app-db v1}\n':
        print('Unexpected magic line:', file_lines[0])
        sys.exit(1)

    recognized_engines = {
        'UnrealEngine' : 'UNREAL_ENGINE',
        'REEngine' : 'RE_ENGINE'
    }

    with open(args.output, 'w') as f:
        print('#ifndef VKD3D_PROTON_APP_DATABASE_H_', file = f)
        print('#define VKD3D_PROTON_APP_DATABASE_H_', file = f)

        print('static const char application_database_hash[] = "' + db_hash + '";', file = f)
        print('static const struct vkd3d_application_database_entry application_database_entries[] = {', file = f)

        for line in file_lines[1:]:
            if line[0] == '#':
                continue

            if line[-1] == '\n':
                line = line[:-1]
            if len(line) == 0:
                continue

            tokens = line.split(',')

            if len(tokens) < 3:
                print('Invalid line:', line)
                print('Parsed tokens:', tokens)
                sys.exit(1)

            appid = tokens[0]
            if appid == '0':
                appid = 'NULL'
            else:
                appid = '"' + appid + '"'

            engine_name = tokens[2]
            if engine_name not in recognized_engines:
                print('Unrecognized engine:', engine_name)
                sys.exit(1)

            comparator = 'VKD3D_STRING_COMPARE_EXACT'
            appname = tokens[1]

            if appname == '0':
                appname = 'NULL'
                comparator = 'VKD3D_STRING_COMPARE_NEVER'
            else:
                if appname[0] == '@':
                    comparator = 'VKD3D_STRING_COMPARE_HASH_EQUAL'
                    appname = appname[1:]
                elif appname[0] == '<':
                    comparator = 'VKD3D_STRING_COMPARE_STARTS_WITH'
                    appname = appname[1:]
                elif appname[0] == '>':
                    comparator = 'VKD3D_STRING_COMPARE_ENDS_WITH'
                    appname = appname[1:]
                elif appname[0] == '?':
                    comparator = 'VKD3D_STRING_COMPARE_CONTAINS'
                    appname = appname[1:]
                elif appname[0] == '=':
                    appname = appname[1:]

                appname = '"' + appname + '"'

            cline = '{ ' + ', '.join([appid, comparator, appname, 'VKD3D_APPLICATION_ENGINE_CLASS_' + recognized_engines[engine_name]])
            for token in tokens[3:]:
                cline += ', ' + token
            cline += ' },'
            print('    ' + cline, file = f)

        print('};', file = f)
        print('#endif', file = f)

if __name__ == '__main__':
    main()
