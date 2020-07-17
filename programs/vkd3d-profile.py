#!/usr/bin/env python3

"""
Copyright 2020 Hans-Kristian Arntzen for Valve Corporation

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
Ad-hoc script to display profiling data
"""

import sys
import os
import argparse
import collections
import struct

ProfileCase = collections.namedtuple('ProfileCase', 'name iterations ticks')


def is_valid_block(block):
    if len(block) != 64:
        return False
    ticks = struct.unpack('=Q', block[0:8])[0]
    iterations = struct.unpack('=Q', block[8:16])[0]
    return ticks != 0 and iterations != 0 and block[16] != 0


def parse_block(block):
    ticks = struct.unpack('=Q', block[0:8])[0]
    iterations = struct.unpack('=Q', block[8:16])[0]
    name = block[16:].split(b'\0', 1)[0].decode('ascii')
    return ProfileCase(ticks = ticks, iterations = iterations, name = name)


def filter_name(name, allow):
    if allow is None:
        return True
    ret = name in allow
    return ret


def find_record_by_name(blocks, name):
    for block in blocks:
        if block.name == name:
            return block

    return None


def normalize_block(block, iter):
    return ProfileCase(name = block.name, iterations = block.iterations / iter, ticks = block.ticks / iter)


def per_iteration_normalize(block):
    return ProfileCase(name = block.name, iterations = block.iterations, ticks = block.ticks / block.iterations)


def main():
    parser = argparse.ArgumentParser(description = 'Script for parsing profiling data.')
    parser.add_argument('--divider', type = str, help = 'Represent data in terms of count per divider. Divider is another counter name.')
    parser.add_argument('--per-iteration', action = 'store_true', help = 'Represent ticks in terms of ticks / iteration. Cannot be used with --divider.')
    parser.add_argument('--name', nargs = '+', type = str, help = 'Only display data for certain counters.')
    parser.add_argument('--sort', type = str, default = 'none', help = 'Sorts input data according to "iterations" or "ticks".')
    parser.add_argument('profile', help = 'The profile binary blob.')

    args = parser.parse_args()
    if not args.profile:
        raise AssertionError('Need profile folder.')

    blocks = []
    with open(args.profile, 'rb') as f:
        for block in iter(lambda: f.read(64), b''):
            if is_valid_block(block):
                blocks.append(parse_block(block))

    if args.divider is not None:
        if args.per_iteration:
            raise AssertionError('Cannot use --per-iteration alongside --divider.')
        divider_block = find_record_by_name(blocks, args.divider)
        if divider_block is None:
            raise AssertionError('Divider block: ' + args.divider + ' does not exist.')
        print('Dividing other results by number of iterations of {}.'.format(args.divider))
        blocks = [normalize_block(block, divider_block.iterations) for block in blocks]
    elif args.per_iteration:
        blocks = [per_iteration_normalize(block) for block in blocks]

    if args.sort == 'iterations':
        blocks.sort(reverse = True, key = lambda a: a.iterations)
    elif args.sort == 'ticks':
        blocks.sort(reverse = True, key = lambda a: a.ticks)
    elif args.sort != 'none':
        raise AssertionError('Invalid argument for --sort.')

    for block in blocks:
        if filter_name(block.name, args.name):
            print(block.name + ':')
            if args.divider is not None:
                print('    Normalized iterations (iterations per {}):'.format(args.divider), block.iterations)
            else:
                print('    Iterations:', block.iterations)

            if args.divider is not None:
                print('    Time spent per iteration of {}: {:.3f}'.format(args.divider, block.ticks / 1000.0), "us")
            elif args.per_iteration:
                print('    Time spent per iteration: {:.3f}'.format(block.ticks / 1000.0), "us")
            else:
                print('    Total time spent: {:.3f}'.format(block.ticks / 1000.0), "us")

if __name__ == '__main__':
    main()
