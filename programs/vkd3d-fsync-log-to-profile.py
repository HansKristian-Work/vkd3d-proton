#!/usr/bin/env python3

"""
Copyright 2024 Hans-Kristian Arntzen for Valve Corporation

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
Hacky script that parses PROTON_LOG=+fsync,+microsecs and emits wait-states in VKD3D_QUEUE_PROFILE format with VKD3D_QUEUE_PROFILE_ABSOLUTE=1.
Equivalent in more plain Wine would be WINEDEBUG=+fsync,+timestamp,+pid,+tid,+threadname,+microsecs WINEFSYNC=1.
Can be used to directly append to vkd3d-proton generated JSON.
"""

import re
import sys
import argparse

def print_json_wait(tid, desc, start_ts, end_ts):
    print('{', '"name": "{}", "ph" : "X", "tid": "{}", "pid": "fsync wait", "ts": {}, "dur": {}'.format(desc, tid, start_ts, end_ts - start_ts), '},')

def print_json_signal(tid, desc, ts):
    print('{', '"name": "{}", "ph" : "i", "tid": "{}", "pid": "fsync signal", "ts": {}'.format(desc, tid, ts), '},')

def print_nt_read_file(tid, desc, ts):
    print('{', '"name": "{}", "ph" : "i", "tid": "{}", "pid": "NtReadFile", "ts": {}'.format(desc, tid, ts), '},')

def strip_hex_prefix(x):
    if x.startswith('0x'):
        return x[2:]
    else:
        return x

def time_in_bounds(time, start_time, end_time):
    if start_time == end_time:
        return True
    else:
        return time >= start_time and time <= end_time

def parse_proton_log(file, start_time, duration_time):
    end_time = start_time + duration_time
    find_threadname = re.compile(r'.+:([0-9A-Fa-f]+):warn:threadname:.*renamed to L"(.+)"$')
    find_wait = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:fsync:__fsync_wait_objects Waiting for (all|any) of \d+ handles: ([^,]+),.*$')
    find_wake = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:fsync:__fsync_wait_objects Woken up by handle 0x([0-9A-Fa-f]+) .*$')
    find_wake_timeout = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:fsync:__fsync_wait_objects Wait timed out.$')
    find_set_event = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:fsync:fsync_set_event 0x([0-9A-Fa-f]+)\.$')
    find_release_sem = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:fsync:fsync_release_semaphore 0x([0-9A-Fa-f]+),.*$')
    find_nt_read_file = re.compile(r'(\d+\.\d+):.+:([0-9A-Fa-f]+):trace:file:NtReadFile \((.+)\)$')
    thread_id_to_name = {}
    sleep_states = {}
    signal_cause = {}

    for line in file.readlines():
        m = find_threadname.match(line)
        if m:
            thread_id_to_name[m[1]] = m[1] + ' (' + m[2] + ')'

        m = find_wait.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            any_or_all = m[3]
            handle_list = m[4]
            if tid in sleep_states:
                raise Exception('{} has a sleep state already. line: "{}"'.format(tid, m[0]))
            sleep_states[tid] = (any_or_all, [strip_hex_prefix(x) for x in handle_list.split(' ')], time)
            continue

        m = find_wake_timeout.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            try:
                state = sleep_states.pop(tid)
                name = thread_id_to_name.get(tid, tid)
                pretty_list = ', '.join(state[1])
                desc = '{} {} timeout'.format(state[0], pretty_list)
                if time_in_bounds(time, start_time, end_time):
                    print_json_wait(name, desc, state[2], time)
            except KeyError as e:
                raise Exception('{} timed out, but there is no wait state? line: "{}"'.format(tid, m[0]))
            continue

        m = find_wake.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            handle = m[3]
            try:
                state = sleep_states.pop(tid)
                name = thread_id_to_name.get(tid, tid)

                pretty_list = ', '.join(state[1])
                if len(state[1]) > 1 and state[0] == 'any':
                    desc = '{} {} woken by {}'.format(state[0], pretty_list, handle)
                else:
                    desc = '{} {}'.format(state[0], pretty_list)

                cause_tid = signal_cause.get(handle)
                if cause_tid:
                    cause_tid = signal_cause.pop(handle)
                    desc += ' [signal by {}]'.format(thread_id_to_name.get(cause_tid, cause_tid))

                if time_in_bounds(time, start_time, end_time) or time_in_bounds(state[2], start_time, end_time):
                    print_json_wait(name, desc, state[2], time)
            except KeyError as e:
                raise Exception('{} was woken up, but there is no wait state? line: "{}"'.format(tid, m[0]))
            continue

        m = find_set_event.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            handle = m[3]
            name = thread_id_to_name.get(tid, tid)
            if time_in_bounds(time, start_time, end_time):
                print_json_signal(name, 'event ' + handle, time)
            signal_cause[handle] = tid
            continue

        m = find_release_sem.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            handle = m[3]
            name = thread_id_to_name.get(tid, tid)
            if time_in_bounds(time, start_time, end_time):
                print_json_signal(name, 'semaphore ' + handle, time)
            signal_cause[handle] = tid
            continue

        m = find_nt_read_file.search(line)
        if m:
            time = int(float(m[1]) * 1e6)
            tid = m[2]
            name = thread_id_to_name.get(tid, tid)
            if 'vkd3d' in name or 'wine_' in name:
                continue
            if time_in_bounds(time, start_time, end_time):
                args = m[3].split(',')
                print_nt_read_file(name, 'handle {}, {} bytes'.format(args[0], int(args[6], 16)), time)

def main():
    parser = argparse.ArgumentParser(description = 'Script for dumping Proton fsync to trace.')
    parser.add_argument('--start', default = 0, type = float, help = 'Filter events starting from time in seconds.')
    parser.add_argument('--duration', default = 0, type = float, help = 'Duration for event filtering in seconds.')
    parser.add_argument('log', help = 'Path to PROTON_LOG=+fsync,+microsecs output.')
    args = parser.parse_args()

    if not args.log:
        raise AssertionError('Missing log.')

    with open(args.log, 'r') as f:
        parse_proton_log(f, int(args.start * 1e6), int(args.duration * 1e6))

if __name__ == '__main__':
    main()
