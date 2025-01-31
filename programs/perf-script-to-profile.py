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
Script which reads perf.data and converts it into the VKD3D_QUEUE_PROFILE format for correlation with D3D12 events.

To use:

Disable paranoid perf (or use sudo when capturing):
# echo -1 > /proc/sys/kernel/perf_event_paranoid

$ VKD3D_QUEUE_PROFILE=/tmp/profile.json VKD3D_QUEUE_PROFILE_ABSOLUTE=1 $game

# While game is running, find the PID and capture it.
# Proton uses MONOTONIC_RAW for its timebase it seems, so need that to correlate with vkd3d-proton profile.
$ perf record -F $rate -k CLOCK_MONOTONIC_RAW --pid $pid

# Add perf info to queue profile
$ python perf-script-to-profile.py --rate $rate perf.data >> /tmp/profile.json
"""

import argparse
import os
import sys
import subprocess
import re
import collections
import math

class ThreadState:
    def __init__(self, comm, tid, expected_delta):
        self.start_ts = 0.0
        self.last_ts = 0.0
        self.idle_delta = expected_delta
        self.comm = comm
        self.current_sym = ''
        self.current_dso = ''
        self.cycles = 0
        self.tid = tid

    def _begin_event(self, ts, cycles, sym, dso):
        self.start_ts = ts
        self.last_ts = ts
        self.current_sym = sym
        self.current_dso = dso
        self.cycles = cycles

    def register_event(self, ts, cycles, sym, dso):
        if self.start_ts == 0.0:
            # First event
            self._begin_event(ts, cycles, sym, dso)
        elif (ts - self.last_ts) > (1.5 * self.idle_delta):
            # Detected some idling
            # Assume that thread went idle in the middle of the sampling period
            self.last_ts += 0.5 * self.idle_delta
            self.flush_event()

            #self.start_ts = self.last_ts
            #self.last_ts = ts
            #self.current_dso = 'idle'
            #self.current_sym = 'idle'
            #self.flush_event()

            self._begin_event(ts, cycles, sym, dso)
        elif self.current_sym != sym or self.current_dso != dso:
            self.last_ts = ts
            self.flush_event()
            self._begin_event(ts, cycles, sym, dso)
        else:
            # Keep going
            self.last_ts = ts
            self.cycles += cycles

    def flush_event(self):
        if self.start_ts != self.last_ts:
            print('{', '"name": "{} ({}) ({} cycles)", "ph" : "X", "tid": "{} ({})", "pid": "perf", "ts": {}, "dur": {}'.format(
                self.current_dso, self.current_sym, self.cycles, self.comm, self.tid, self.start_ts * 1000000.0,
                                                    (self.last_ts - self.start_ts) * 1000000.0), '},')

def main():
    parser = argparse.ArgumentParser(description = 'Script for parsing perf profiling data.')
    parser.add_argument('profile', help = 'The profile binary blob (perf.data).')
    parser.add_argument('--rate', type = int, default = 4000, help = 'The sampling rate used (used to detect discontinuity)')
    parser.add_argument('--start', type = float, default = 0.0, help = 'Start timestamp to emit (in seconds)')
    parser.add_argument('--end', type = float, default = math.inf, help = 'End timestamp to emit (in seconds)')
    parser.add_argument('--filter-tids', nargs = '+', action = 'extend', type = int, help = 'Only emit provided tids')

    args = parser.parse_args()
    if not args.profile:
        raise AssertionError('Need profile folder.')

    expected_delta = 1.0 / args.rate
    thread_state = {}

    with subprocess.Popen(['perf', 'script', '-F', 'trace:sym,tid,time', '--ns', '-i', args.profile], stdout = subprocess.PIPE) as proc:
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            line = line.decode('utf-8')
            if line[-1] == '\n':
                line = line[0:-1]

            if line[0] == '\t':
                # Ignore empty first lines, since they are used for backtraces when -g is used.
                continue

            comm = line[0:16].strip()
            line = line[16:]

            elems = list(filter(None, line.split(' ')))

            if len(elems) < 7:
                continue

            tid = int(elems[0])

            if args.filter_tids and (tid not in args.filter_tids):
                continue

            ts = float(elems[1][0:-1])

            if ts < args.start or ts > args.end:
                continue

            cycles = int(elems[2])
            sym = elems[5]
            dso = elems[6]

            # Massage the DSO so it's more readable.
            dso = os.path.basename(dso)
            if dso[-1] == ')':
                dso = dso[:-1]

            if tid not in thread_state:
                thread_state[tid] = ThreadState(comm, tid, expected_delta)

            state = thread_state[tid]
            state.register_event(ts, cycles, sym, dso)

    for i in thread_state.items():
        i[1].flush_event()

if __name__ == '__main__':
    main()