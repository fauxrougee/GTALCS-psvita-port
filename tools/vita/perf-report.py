#!/usr/bin/env python3
"""Summarise the [PERF] windows of a Vita log.txt (src/skel/vita/vita_perf.cpp).

  python tools/vita/perf-report.py log.txt            summary + top costs
  python tools/vita/perf-report.py A.txt B.txt        A/B comparison

Only gameplay windows are written by the game. Windows shorter than --min-secs
(default 1 s) are ignored; partial windows (ended by a menu, a loading screen)
are kept, since in-game loads belong to the measured routes. Section times are
exclusive here: a parent only keeps what its sub-sections do not cover.
The CPU/GPU/storage label is a reading aid: section times are wall time on the
main thread and include vitaGL waits; confirm with the swap, disk and GL lines.
"""
import argparse
import re
import sys
from collections import defaultdict

WINDOW = re.compile(r'\[PERF\] window=(\d+) build=(\S+) phase=game frames=(\d+) intervals=(\d+) secs=([\d.]+) limit=(\S+) partial=(\d)')
KV = re.compile(r'(\w[\w/+]*)=(-?[\d.]+|--)')
SECTION = re.compile(r'^\[VITA\]   ( *)(\S+)\s+([\d.]+) ms/frame  max\s+([\d.]+)  \(([\d.]+) calls/frame\)')
GLCALL = re.compile(r'^\[VITA\]   (gl\S+)\s+([\d.]+|-) ms/frame\s+([\d.]+) calls/frame')
HELD = re.compile(r'held_1/2/3/4\+=(\d+)/(\d+)/(\d+)/(\d+)')

CATEGORIES = [
    ('EndOfFrame', 'synchronisation: swap (file d\'affichage / GPU)'),
    ('Streaming::LoadAll', 'stockage: chargement synchrone'),
    ('Streaming::Convert', 'streaming: décodage et création GPU (CPU)'),
    ('Streaming::', 'streaming (CPU)'),
    ('TheScripts', 'CPU simulation: scripts'),
    ('Population', 'CPU simulation: population'),
    ('World.Anim', 'CPU simulation: animations'),
    ('World.Control', 'CPU simulation: contrôle/physique/IA'),
    ('World.Collision', 'CPU simulation: collisions'),
    ('World::', 'CPU simulation: monde (autres)'),
    ('CutsceneMgr', 'CPU simulation: cinématiques'),
    ('CGame::Process', 'CPU simulation (autres)'),
    ('DMAudio', 'audio (thread principal)'),
    ('CnstrRenderList', 'CPU préparation du rendu'),
    ('PreRender', 'CPU préparation du rendu'),
    ('', 'soumission GL (CPU, inclut les attentes vitaGL)'),
]

def category(name):
    return next(label for prefix, label in CATEGORIES if name.startswith(prefix))

def kv(line):
    return {k: (None if v == '--' else float(v)) for k, v in KV.findall(line)}

def parse(path, min_secs):
    windows, cur = [], None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        m = WINDOW.search(line)
        if m:
            cur = {'build': m[2], 'frames': int(m[3]), 'intervals': int(m[4]), 'secs': float(m[5]),
                   'limit': m[6], 'partial': m[7] == '1', 'lines': {}, 'sections': [], 'gl': {}, 'held': [0, 0, 0, 0]}
            windows.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith('[VITA] ') and 'swaps/s outside gameplay' in line:
            cur = None
            continue
        if line.startswith('[PERF] '):
            tag = line.split()[1]
            if tag in ('window', 'first'):
                continue
            cur['lines'][tag] = kv(line)
            m = HELD.search(line)
            if m:
                cur['held'] = [int(x) for x in m.groups()]
            continue
        m = SECTION.match(line)
        if m:
            cur['sections'].append((len(m[1]) // 2, m[2], float(m[3]), float(m[4])))
            continue
        m = GLCALL.match(line)
        if m:
            cur['gl'][m[1]] = (None if m[2] == '-' else float(m[2]), float(m[3]))
    return [w for w in windows if w['secs'] >= min_secs and w['intervals'] > 0]

def exclusive(sections):
    """Self time of each section: minus its direct children (nearest shallower predecessor)."""
    own = [t for _, _, t, _ in sections]
    for i, (depth, _, t, _) in enumerate(sections):
        for j in range(i - 1, -1, -1):
            if sections[j][0] < depth:
                if sections[j][0] == depth - 1:
                    own[j] -= t
                break
    result = defaultdict(float)
    for i in range(len(sections)):
        result[sections[i][1]] += max(0.0, own[i])
    return dict(result)

def aggregate(windows):
    total = sum(w['intervals'] for w in windows)
    def wmean(tag, key):
        vals = [(w['lines'].get(tag, {}).get(key), w['intervals']) for w in windows]
        vals = [(v, n) for v, n in vals if v is not None]
        n = sum(n for _, n in vals)
        return sum(v * n for v, n in vals) / n if n else None
    def worst(tag, key, pick=max):
        vals = [w['lines'].get(tag, {}).get(key) for w in windows]
        vals = [v for v in vals if v is not None]
        return pick(vals) if vals else None
    held = [sum(w['held'][i] for w in windows) for i in range(4)]
    selfs = defaultdict(float)
    for w in windows:
        for name, t in exclusive(w['sections']).items():
            selfs[name] += t * w['frames']
    frames = sum(w['frames'] for w in windows)
    r = {
        'windows': len(windows), 'frames': frames, 'intervals': total,
        'secs': sum(w['secs'] for w in windows),
        'builds': sorted({w['build'] for w in windows}), 'limits': sorted({w['limit'] for w in windows}),
        'frame_mean': wmean('frame_ms', 'mean'), 'frame_p95_worst': worst('frame_ms', 'p95'),
        'frame_p99_worst': worst('frame_ms', 'p99'), 'frame_max': worst('frame_ms', 'max'),
        'over33': wmean('frame_ms', 'over33'), 'over50': wmean('frame_ms', 'over50'),
        'fps1s_min': worst('fps_1s', 'min', min),
        'work': wmean('split_ms', 'work'), 'swap': wmean('split_ms', 'swap'),
        'wait': wmean('split_ms', 'wait'), 'post': wmean('split_ms', 'post'),
        'report_max': worst('split_ms', 'report_included'),
        'report_total': sum((w['lines'].get('split_ms', {}).get('report_included') or 0) for w in windows),
        'display_fps': wmean('present', 'display_fps'), 'latency': wmean('present', 'mean'),
        'game_per_real': wmean('gametime', 'game_per_real'),
        'sync_ms': sum((w['lines'].get('disk', {}).get('sync_ms') or 0) for w in windows),
        'selfs': {k: v / frames for k, v in selfs.items()} if frames else {},
        'held': held,
    }
    r['fps'] = 1000.0 / r['frame_mean'] if r['frame_mean'] else None
    return r

def top_costs(r, count=3):
    costs = [(t, name, category(name)) for name, t in r['selfs'].items()]
    if r['wait']:
        costs.append((r['wait'], '(attente entre images)', 'limiteur / boucle principale'))
    # CdStreamSync already belongs to the timed section that called it. Show
    # it separately, not as an additional exclusive cost in the ranking.
    return sorted(costs, reverse=True)[:count]

def fmt(v, spec='.2f'):
    return '--' if v is None else format(v, spec)

def report(path, args):
    windows = parse(path, args.min_secs)
    if not windows:
        sys.exit(f'{path}: no gameplay [PERF] window of at least {args.min_secs} s')
    r = aggregate(windows)
    print(f'== {path}: {r["windows"]} windows, {r["frames"]} gameplay frames, {r["secs"]:.1f} s; '
          f'build {", ".join(r["builds"])}; limit {", ".join(r["limits"])}')
    print(f'   FPS {fmt(r["fps"], ".1f")} (1 s windows min {fmt(r["fps1s_min"], ".1f")}), frame mean {fmt(r["frame_mean"])} ms, '
          f'worst p95 {fmt(r["frame_p95_worst"])} / p99 {fmt(r["frame_p99_worst"])} / max {fmt(r["frame_max"])} ms, '
          f'>33.3 ms {fmt(r["over33"], ".1f")} %, >50 ms {fmt(r["over50"], ".1f")} %')
    print(f'   per frame: work {fmt(r["work"])} + swap {fmt(r["swap"])} + wait {fmt(r["wait"])} + post {fmt(r["post"])} ms; '
          f'display estimate {fmt(r["display_fps"], ".1f")} images/s, submit->callback {fmt(r["latency"])} ms, '
          f'game/real time {fmt(r["game_per_real"], ".4f")}')
    print('   images held 1/2/3/4+ vblanks: ' + '/'.join(str(n) for n in r['held']))
    print(f'   Main-thread report cost recorded: total {fmt(r["report_total"])} ms, max {fmt(r["report_max"])} ms/window '
          '(included in frame times; last report may be outside the capture)')
    print(f'   CdStreamSync included in section times: {r["sync_ms"] / r["frames"]:.2f} ms/frame')
    print('   Top costs (exclusive ms/frame, share of the mean frame):')
    for t, name, label in top_costs(r, args.top):
        share = 100.0 * t / r['frame_mean'] if r['frame_mean'] else 0.0
        print(f'   {t:7.2f} ms {share:5.1f} %  {name:28s} {label}')
    if any(l in ('20', '30') for l in r['limits']):
        print('   Note: FrameLimit 20/30 paces by vsync interval: swap and EndOfFrame include that wait.')
    if 'stable' in r['limits']:
        print('   Note: stable uses the original 30 FPS limiter when enabled; wait includes its busy polling and report costs.')
    budget = [(45, 22.22), (60, 16.67)]
    if r['frame_mean']:
        print('   Gap to target frame time (includes limiter/sync wait, NOT a required rendering speedup): ' + ', '.join(
            f'{fps} FPS {max(0.0, 100.0 * (1 - b / r["frame_mean"])):.0f} %' for fps, b in budget))
    logger = [w['lines']['logger'] for w in windows if 'logger' in w['lines']]
    if logger:
        lost = max(x.get('dropped_bytes', 0) for x in logger)
        errors = max(x.get('io_errors', 0) for x in logger)
        print(f'   Async log: dropped {lost:.0f} bytes, I/O errors {errors:.0f} (cumulative); '
              'interpret incomplete reports with care.' if lost or errors else
              '   Async log: no dropped bytes or I/O errors reported.')
    return r

def compare(a, b):
    print('== A/B (B - A)')
    for key, label in [('fps', 'FPS'), ('frame_mean', 'frame ms'), ('frame_p95_worst', 'worst p95 ms'),
                       ('frame_p99_worst', 'worst p99 ms'), ('over33', '>33.3 ms %'), ('over50', '>50 ms %'),
                       ('work', 'work ms'), ('swap', 'swap ms'), ('wait', 'wait ms'), ('display_fps', 'display images/s'),
                       ('latency', 'submit->flip ms'), ('game_per_real', 'game/real')]:
        va, vb = a[key], b[key]
        delta = '--' if va is None or vb is None else f'{vb - va:+.2f}'
        print(f'   {label:18s} {fmt(va):>9s} {fmt(vb):>9s} {delta:>9s}')
    names = sorted(set(a['selfs']) | set(b['selfs']), key=lambda n: -max(a['selfs'].get(n, 0), b['selfs'].get(n, 0)))
    for n in names[:10]:
        va, vb = a['selfs'].get(n, 0.0), b['selfs'].get(n, 0.0)
        print(f'   {n:18.18s} {va:9.2f} {vb:9.2f} {vb - va:+9.2f}')
    print('   Accept a change only if it repeats over 3 comparable runs beyond their spread.')

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('logs', nargs='+')
    p.add_argument('--min-secs', type=float, default=1.0)
    p.add_argument('--top', type=int, default=3)
    args = p.parse_args()
    results = [report(path, args) for path in args.logs]
    if len(results) == 2:
        compare(*results)

if __name__ == '__main__':
    main()
