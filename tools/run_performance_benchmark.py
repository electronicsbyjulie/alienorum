#!/usr/bin/env python3
"""Run isolated paired production-path comparisons and summarize their raw samples."""
import argparse
import csv
import json
import os
import random
from pathlib import Path
import statistics
import subprocess
import time


def summarize(path):
    groups = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            groups.setdefault((int(row['proposal']), row['workload']), []).append(row)
    results = []
    for (proposal, workload), rows in sorted(groups.items()):
        before = statistics.median(float(row['before_ms']) for row in rows)
        after = statistics.median(float(row['after_ms']) for row in rows)
        deltas = [float(row['change_percent']) for row in rows]
        rng = random.Random(42)
        medians = sorted(statistics.median(rng.choices(deltas, k=len(deltas))) for _ in range(4000))
        low, high = medians[100], medians[3899]
        consistent_loss = len(rows) >= 5 and low > 0
        consistent_gain = len(rows) >= 5 and high < 0
        status = 'slower' if consistent_loss else 'faster' if consistent_gain else 'mixed/neutral'
        results.append(dict(proposal=f'P{proposal:03}', workload=workload, pairs=len(rows),
                            before_ms=before, after_ms=after, median_change_percent=statistics.median(deltas),
                            status=status, change_ci_low=low, change_ci_high=high,
                            before_cpu_ms=statistics.median(float(row.get('before_cpu_ms', 'nan')) for row in rows),
                            after_cpu_ms=statistics.median(float(row.get('after_cpu_ms', 'nan')) for row in rows),
                            maximum_channel_error=max(int(row['max_channel_error']) for row in rows)))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/alienorum-perf-bin/performance_benchmark_test'))
    parser.add_argument('--cases', default='1,2,3,4,5,20,21,22,23,24,31,39,40,41,42,50,51,52')
    parser.add_argument('--rounds', type=int, default=5)
    parser.add_argument('--frames', type=int, default=8)
    parser.add_argument('--population', type=int, default=12000)
    parser.add_argument('--stars', type=int, default=30000)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--output', type=Path, default=Path('PERFORMANCE_BENCHMARK_RESULTS.csv'))
    parser.add_argument('--summarize-only', action='store_true')
    args = parser.parse_args()
    if not args.summarize_only:
        env = os.environ.copy()
        env.update(ALIENORUM_RUN_PERF='1', ALIENORUM_PERF_CASES=args.cases,
                   ALIENORUM_PERF_ROUNDS=str(args.rounds), ALIENORUM_PERF_FRAMES=str(args.frames),
                   ALIENORUM_PERF_POPULATION=str(args.population), ALIENORUM_PERF_STARS=str(args.stars), ALIENORUM_PERF_WIDTH=str(args.width),
                   ALIENORUM_PERF_HEIGHT=str(args.height), ALIENORUM_PERF_OUTPUT=str(args.output.resolve()))
        command = [str(args.binary.resolve()), '--gtest_filter=PerformanceBenchmarkTest.PairedProductionPaths']
        start = time.time()
        with args.output.with_suffix('.log').open('w') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
        metadata = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                        source_diff=subprocess.check_output(['git', 'diff', '--stat'], text=True),
                        parameters=vars(args), elapsed_seconds=time.time() - start,
                        exit_code=result.returncode, display=env.get('DISPLAY', ''),
                        requested_depth_bits=env.get('ALIENORUM_PERF_DEPTH', 'default'))
        args.output.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2, default=str) + '\n')
        if result.returncode:
            raise SystemExit(f'Benchmark failed ({result.returncode}); inspect {args.output.with_suffix(".log")}.')
    results = summarize(args.output)
    args.output.with_suffix('.summary.json').write_text(json.dumps(results, indent=2) + '\n')
    for row in results:
        print(f'{row["proposal"]} {row["workload"]}: {row["before_ms"]:.3f} -> {row["after_ms"]:.3f} ms; '
              f'{row["median_change_percent"]:+.2f}%; {row["status"]}; {row["pairs"]} pairs')


if __name__ == '__main__':
    main()
