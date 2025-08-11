#!/usr/bin/env python3

"""
Copyright 2025 Hans-Kristian Arntzen for Valve Corporation

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
Compares two timestamp profiles
"""

import sys
import os
import argparse
import collections
import struct
import csv

ProfileCase = collections.namedtuple('ProfileCase', 'type hashes total_time non_ps_invocations ps_invocations count root_signature_hash')
ProfileMeta = collections.namedtuple('ProfileMeta', 'psos frame_count')

def split_hashes(hash_str):
    return hash_str.split('+')

def read_csv(path):
    frame_count = 0
    psos = {}
    with open(path, 'r') as csvfile:
        reader = csv.reader(csvfile)
        for row in reader:
            if row[0] == 'PSO Type':
                continue
            if row[1] == 'SWAPCHAIN':
                frame_count = int(row[6])
                continue

            if int(row[4]) == 0 and int(row[5]) == 0:
                continue
            psos[row[1]] = ProfileCase(row[0], split_hashes(row[2]), float(row[3]), int(row[4]), int(row[5]), int(row[6]), row[7] if len(row) >= 8 else 0)

    if frame_count == 0:
        raise AssertionError('Expected at least one row with SWAPCHAIN count != 0')

    return ProfileMeta(psos, frame_count)

def report_list(cmp_list, first_lookup, second_lookup):
    for v in cmp_list:
        ratio = v[2] / v[1]
        print('  ====', v[0], '====')
        print('    Delta first -> second: {:.3f} %'.format(100.0 * (ratio - 1)))
        print('      Type: {}'.format(first_lookup[v[0]].type))
        print('      Total time: ({:.3f} us) vs ({:.3f} us)'.format(first_lookup[v[0]].total_time * 1e6, second_lookup[v[0]].total_time * 1e6))
        print('      Total non-PS invocations: ({}) vs ({})'.format(first_lookup[v[0]].non_ps_invocations, second_lookup[v[0]].non_ps_invocations))
        print('      Total PS invocations: ({}) vs ({})'.format(first_lookup[v[0]].ps_invocations, second_lookup[v[0]].ps_invocations))
        print('      Total draws/dispatches: ({}) vs ({})'.format(first_lookup[v[0]].count, second_lookup[v[0]].count))
        print('      Shader hashes:')
        for hash in first_lookup[v[0]].hashes:
            print('        {}'.format(hash))
        print('      RootSig: {}'.format(first_lookup[v[0]].root_signature_hash))

def main():
    parser = argparse.ArgumentParser(description = 'Script for parsing profiling data.')
    parser.add_argument('--first', type = str, help = 'The first CSV.')
    parser.add_argument('--second', help = 'The second CSV.')
    parser.add_argument('--count', type = int, default = 10, help = 'Only list top --count entries per type.')
    parser.add_argument('--type', type = str, help = 'Filter on PSO type.')
    parser.add_argument('--threshold', type = float, default = 0.0, help = 'Only include entries which consume at least (seconds).')

    args = parser.parse_args()
    if not args.first:
        raise AssertionError('Need --first.')
    if not args.second:
        raise AssertionError('Need --second.')

    first_csv = read_csv(args.first)
    second_csv = read_csv(args.second)

    per_invocation_analysis = []
    per_frame_analysis = []
    per_dispatch_analysis = []
    total_time_analysis = []

    for pso_entry in second_csv.psos:
        if pso_entry in first_csv.psos:
            first = first_csv.psos[pso_entry]
            second = second_csv.psos[pso_entry]

            if args.type and first.type != args.type:
                continue

            if first.total_time < args.threshold or second.total_time < args.threshold:
                continue

            first_time_per_invocation = first.total_time / (first.non_ps_invocations + first.ps_invocations)
            second_time_per_invocation = second.total_time / (second.non_ps_invocations + second.ps_invocations)
            first_time_per_frame = first.total_time / first_csv.frame_count
            second_time_per_frame = second.total_time / second_csv.frame_count
            first_time_per_count = first.total_time / first.count
            second_time_per_count = second.total_time / second.count

            per_invocation_analysis.append((pso_entry, first_time_per_invocation, second_time_per_invocation))
            per_frame_analysis.append((pso_entry, first_time_per_frame, second_time_per_frame))
            per_dispatch_analysis.append((pso_entry, first_time_per_count, second_time_per_count))
            total_time_analysis.append((pso_entry, first.total_time, second.total_time))

    per_invocation_analysis.sort(reverse = True, key = lambda e: e[2] / e[1])
    per_frame_analysis.sort(reverse = True, key = lambda e: e[2] / e[1])
    per_dispatch_analysis.sort(reverse = True, key = lambda e: e[2] / e[1])
    total_time_analysis_first = total_time_analysis[:]
    total_time_analysis_second = total_time_analysis[:]
    total_time_analysis_first.sort(reverse = True, key = lambda e: e[1])
    total_time_analysis_second.sort(reverse = True, key = lambda e: e[2])

    print('Total frames for first CSV:', first_csv.frame_count)
    print('Total frames for second CSV:', second_csv.frame_count)

    print('\nPer Invocation Analysis:')
    report_list(per_invocation_analysis[0 : args.count], first_csv.psos, second_csv.psos)

    print('\nPer Frame Analysis:')
    report_list(per_frame_analysis[0 : args.count], first_csv.psos, second_csv.psos)

    print('\nPer Dispatch Analysis:')
    report_list(per_frame_analysis[0 : args.count], first_csv.psos, second_csv.psos)

    print('\nMaximal Time Contribution Analysis (first):')
    report_list(total_time_analysis_first[0 : args.count], first_csv.psos, second_csv.psos)

    print('\nMaximal Time Contribution Analysis (second):')
    report_list(total_time_analysis_second[0 : args.count], first_csv.psos, second_csv.psos)

if __name__ == '__main__':
    main()

