#!/usr/bin/env python3
"""Compare independently compiled production paths in alternating paired processes."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time
from run_performance_benchmark import summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--cases', default='30')
    parser.add_argument('--rounds', type=int, default=9)
    parser.add_argument('--frames', type=int, default=16)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    env = os.environ.copy()
    env.update(ALIENORUM_RUN_PERF='1', ALIENORUM_PERF_CASES=args.cases,
               ALIENORUM_PERF_ROUNDS='1', ALIENORUM_PERF_FRAMES=str(args.frames))
    binary_hashes = dict(before=hashlib.sha256(args.before.read_bytes()).hexdigest(),
                         after=hashlib.sha256(args.after.read_bytes()).hexdigest())
    started = time.time()
    fields = ['proposal', 'workload', 'round', 'order', 'before_ms', 'after_ms',
              'change_percent', 'max_channel_error', 'before_cpu_ms', 'after_cpu_ms']
    with tempfile.TemporaryDirectory(prefix='alienorum-fixed-perf-') as temporary:
        with args.output.open('w') as output, args.output.with_suffix('.log').open('w') as log:
            writer = csv.DictWriter(output, fieldnames=fields)
            writer.writeheader()
            for pair in range(args.rounds):
                modes = [0, 1] if pair % 2 == 0 else [1, 0]
                samples = dict()
                for mode in modes:
                    binary = args.before if mode == 0 else args.after
                    sample_path = Path(temporary) / f'{pair}-{mode}.csv'
                    env['ALIENORUM_PERF_OUTPUT'] = str(sample_path)
                    env['ALIENORUM_PERF_CAPTURE_PREFIX'] = str(sample_path.with_suffix('.capture'))
                    command = [str(binary.resolve()), '--gtest_filter=PerformanceBenchmarkTest.PairedProductionPaths']
                    result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, timeout=3600)
                    log.write(f'PAIR {pair} MODE {mode} BINARY {binary}\n' + result.stdout + '\n')
                    log.flush()
                    if result.returncode or 'PERF comparison: fixed' not in result.stdout:
                        raise RuntimeError('Fixed-path benchmark failed or binary lacks fixed comparison mode.')
                    hashes = dict()
                    for proposal, scene, value in re.findall(r'^PERF IMAGE P(\d+) (\S+) (\d+)$', result.stdout, re.M):
                        hashes[(proposal, scene)] = value
                    with sample_path.open() as stream:
                        rows = dict(((row['proposal'], row['workload']), row) for row in csv.DictReader(stream))
                    if not rows or set(rows) != set(hashes):
                        raise RuntimeError('Benchmark image hashes do not cover every workload.')
                    samples[mode] = (rows, hashes)
                errors = dict()
                if set(samples[0][1]) != set(samples[1][1]):
                    raise RuntimeError('Compiled paths expose different workload sets.')
                for key, before_hash in samples[0][1].items():
                    errors[key] = 0
                    if before_hash == samples[1][1][key]:
                        continue
                    if key[0] != '42':
                        raise RuntimeError('Independently compiled paths have different image/state hashes.')
                    images = []
                    for mode in [0, 1]:
                        path = Path(temporary) / f'{pair}-{mode}.capture.P42.{key[1]}.bin'
                        images.append(path.read_bytes())
                    if len(images[0]) != len(images[1]):
                        raise RuntimeError('Captured image sizes differ.')
                    errors[key] = max(abs(a - b) for a, b in zip(*images))
                    if errors[key] > 2:
                        raise RuntimeError('P042 exceeds its two-level image tolerance.')
                for key, before in samples[0][0].items():
                    after = samples[1][0][key]
                    before_ms = statistics.mean(float(before[name]) for name in ['before_ms', 'after_ms'])
                    after_ms = statistics.mean(float(after[name]) for name in ['before_ms', 'after_ms'])
                    before_cpu = statistics.mean(float(before[name]) for name in ['before_cpu_ms', 'after_cpu_ms'])
                    after_cpu = statistics.mean(float(after[name]) for name in ['before_cpu_ms', 'after_cpu_ms'])
                    writer.writerow(dict(proposal=key[0], workload=key[1], round=pair,
                                         order='AB' if pair % 2 == 0 else 'BA', before_ms=before_ms,
                                         after_ms=after_ms, change_percent=(after_ms / before_ms - 1) * 100,
                                         max_channel_error=errors[key], before_cpu_ms=before_cpu, after_cpu_ms=after_cpu))
                output.flush()
                print(f'Fixed comparison pair {pair + 1}/{args.rounds} complete.', flush=True)
    final_hashes = dict(before=hashlib.sha256(args.before.read_bytes()).hexdigest(),
                        after=hashlib.sha256(args.after.read_bytes()).hexdigest())
    if final_hashes != binary_hashes:
        raise RuntimeError('Benchmark executables changed during measurement.')
    metadata = dict(parameters=vars(args), binary_sha256=binary_hashes, elapsed_seconds=time.time() - started,
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                    display=env.get('DISPLAY', ''), requested_depth_bits=env.get('ALIENORUM_PERF_DEPTH', 'default'))
    args.output.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2, default=str) + '\n')
    results = summarize(args.output)
    args.output.with_suffix('.summary.json').write_text(json.dumps(results, indent=2) + '\n')
    for result in results:
        print(f"{result['proposal']} {result['workload']}: {result['median_change_percent']:+.2f}%; {result['status']}")


if __name__ == '__main__':
    main()
