#!/usr/bin/env python3
"""Run bounded, paired horizon trials with movement and image checks."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import random
import signal
import statistics
import subprocess


def interval(values):
    rng = random.Random(701)
    samples = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(3000))
    return samples[75], samples[2924]


def summarize(rows):
    results = []
    for mode in sorted(set(row['mode'] for row in rows)):
        selected = [row for row in rows if row['mode'] == mode]
        for metric in ('prep_ms', 'cpu_ms', 'frame_ms'):
            for tail in (False, True):
                before, after, changes = [], [], []
                for round_id in sorted(set(row['round'] for row in selected)):
                    pair = []
                    for arm in ('before', 'after'):
                        data = sorted(float(row[metric]) for row in selected
                                      if row['round'] == round_id and row['arm'] == arm)
                        pair.append(data[math.ceil(.95 * len(data)) - 1] if tail else statistics.median(data))
                    before.append(pair[0])
                    after.append(pair[1])
                    changes.append((pair[1] / pair[0] - 1) * 100)
                low, high = interval(changes)
                results.append(dict(mode=mode, metric=metric, statistic='p95' if tail else 'median',
                                    before_ms=statistics.median(before), after_ms=statistics.median(after),
                                    change_percent=statistics.median(changes), ci_low=low, ci_high=high,
                                    verdict='pilot' if len(changes) < 8 else
                                    'slower' if low > 0 else 'faster' if high < 0 else 'unresolved'))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trial', type=int, required=True)
    parser.add_argument('--scenes', default='ice,earth,ocean,forest,venus,airless,lava')
    parser.add_argument('--rounds', type=int, default=8)
    parser.add_argument('--frames', type=int, default=6)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('bin/performance_benchmark_test'))
    parser.add_argument('--headless', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    all_rows, all_results = [], []
    for scene in args.scenes.split(','):
        env = os.environ.copy()
        env.update(ALIENORUM_HORIZON_TRIAL=str(args.trial), ALIENORUM_HORIZON_SCENE=scene,
                   ALIENORUM_HORIZON_ROUNDS=str(args.rounds), ALIENORUM_HORIZON_FRAMES=str(args.frames))
        command = ['timeout', '--kill-after=2s', '90s', str(args.binary.resolve()),
                   '--gtest_filter=HorizonTrialTest.PairedHorizonPaths']
        if args.headless:
            command = ['xvfb-run', '-a'] + command
        log_path = args.output / (scene + '.log')
        with log_path.open('w') as log:
            child = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = child.wait(timeout=100)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
                raise
        if code:
            print(log_path.read_text()[-4000:], flush=True)
            raise SystemExit(f'Trial process returned {code}: {log_path}')
        rows = []
        for line in log_path.read_text().splitlines():
            if line.startswith('HORIZON_SAMPLE,'):
                fields = line.split(',')[1:]
                rows.append(dict(zip(('trial', 'scene', 'mode', 'round', 'arm', 'frame',
                                      'prep_ms', 'cpu_ms', 'frame_ms', 'stride'), fields)))
        if not rows:
            raise SystemExit(f'No samples: {log_path}')
        results = summarize(rows)
        for result in results:
            result['scene'] = scene
            if result['metric'] == 'frame_ms' and result['statistic'] == 'median':
                print(f"Trial {args.trial} {scene} {result['mode']}: "
                      f"{result['before_ms']:.3f} -> {result['after_ms']:.3f} ms "
                      f"({result['change_percent']:+.2f}%, {result['verdict']})", flush=True)
        all_rows.extend(rows)
        all_results.extend(results)
        with (args.output / 'samples.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(all_rows[0]))
            writer.writeheader()
            writer.writerows(all_rows)
        (args.output / 'summary.json').write_text(json.dumps(all_results, indent=2) + '\n')


if __name__ == '__main__':
    main()
