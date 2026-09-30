#!/usr/bin/env python3
"""Host test of the benchmark measurements (src/extras/benchmark_metrics.cpp).

Builds the production file with tools/vita/test_support/bench_metrics_test.cpp
and a deterministic fake platform (bench_fake_platform.cpp) under ASan, runs a
synthetic benchmark (graphics tests with known frame times, GPU fence data,
two isolation viewpoints, sim_running/no_audio, a streaming hitch, teleports,
skipped and interrupted tests, frame buffer overflow), then checks
results.json against the relcs-bench/1 schema and the expected values,
frames.csv against BENCH_CSV_HEADER, and the French summary.txt.
Also compiles benchmark_metrics.cpp alone in C++14 (MSVC default mode).
Usage: python tools/vita/check-bench-metrics.py (native Windows or Linux)
"""
from pathlib import Path
import csv
import io
import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import unicodedata
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]
EXTRAS = ROOT/'src/extras'
SUPPORT = ROOT/'tools/vita/test_support'
SOURCE = EXTRAS/'benchmark_metrics.cpp'
STAMP = '2026-09-30_14-05-00'


def fail(msg):
    raise SystemExit('FAIL: ' + msg)


def check(cond, msg):
    if not cond:
        fail(msg)


def near(a, b, eps=0.0015):
    return a is not None and abs(a - b) <= eps


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def load_json(path):
    def no_constants(name):
        fail(f'{path.name}: non-finite number {name}')
    text = path.read_text(encoding='utf-8')
    return json.loads(text, parse_constant=no_constants)


def csv_header():
    text = (EXTRAS/'benchmark_metrics.h').read_text()
    m = re.search(r'#define BENCH_CSV_HEADER(.*?)\n\n', text, re.S)
    return ''.join(re.findall(r'"([^"]*)"', m.group(1)))


def compile_cxx14(env):
    """benchmark_metrics.cpp must build in MSVC's default C++14 mode."""
    if os.name == 'nt':
        path = next(v for k, v in env.items() if k.lower() == 'path')
        cl = shutil.which('cl.exe', path=path)
        cmd = [cl, '/nologo', '/std:c++14', '/Zs', '/W4', '/DRELCS_BENCHMARK', '/I' + str(EXTRAS), str(SOURCE)]
    else:
        cmd = ['g++', '-std=c++14', '-fsyntax-only', '-Wall', '-Wextra', '-DRELCS_BENCHMARK',
               '-I' + str(EXTRAS), str(SOURCE)]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    lines = [l for l in out.splitlines() if l.strip() and not l.strip().endswith('benchmark_metrics.cpp')]
    if r.returncode or lines:
        print(out)
    check(r.returncode == 0, 'benchmark_metrics.cpp does not compile as C++14')
    check(not lines, 'benchmark_metrics.cpp has C++14 warnings')


# ---- schema -------------------------------------------------------------------

TOP = ['schema', 'partial', 'aborted', 'timestamp', 'device', 'caps', 'settings', 'boot', 'scores',
       'tests', 'deltas', 'targets']
DEVICE = ['platform', 'model', 'firmware', 'build', 'cpu_mhz', 'bus_mhz', 'gpu_mhz', 'xbar_mhz', 'gpu',
          'screen', 'notes']
CAPS = ['gpu_fence', 'gpu_sync', 'viewport', 'draw_mode', 'flat_tex', 'sections', 'gl_counters', 'disk',
        'threads', 'cpu_load', 'alloc']
TEST = ['id', 'group', 'name', 'variant', 'viewpoint', 'kind', 'skipped', 'skip_reason', 'interrupted',
        'scene', 'frames', 'first_frame', 'seconds', 'fps', 'frame_ms', 'work_ms', 'swap_ms', 'gpu_ms',
        'gpu_sync_ms', 'gpu_samples', 'cpu_bound_pct', 'gpu_bound_pct', 'groups_ms', 'hitches',
        'worst_hitch_ms', 'worst_hitch_group', 'per_frame', 'totals', 'threads', 'cpu_load_pct', 'memory',
        'load_ms', 'settle_ms', 'sections']
SCENE = ['hour', 'minute', 'weather', 'ped_density', 'car_density', 'fixed_step', 'paused']
STATS = ['mean', 'median', 'p90', 'p95', 'p99', 'min', 'max', 'low1_fps']
GROUPS = ['sim', 'audio', 'renderlist', 'envmap', 'scene', 'fx', '2d', 'eof']
PER_FRAME = ['draws', 'indices', 'uniforms', 'tex_binds', 'upload_kib', 'disk_kib', 'peds', 'vehicles', 'objects']
TOTALS = ['draws', 'indices', 'uniforms', 'tex_binds', 'buf_binds', 'programs', 'state_calls',
          'vertex_upload_bytes', 'index_upload_bytes', 'texture_upload_bytes', 'texture_upload_ms',
          'shader_compiles', 'disk_reads', 'disk_bytes', 'disk_read_ms', 'disk_sync_waits', 'disk_sync_ms',
          'alloc_calls', 'free_calls', 'alloc_bytes']
MEMORY = ['heap_used_mib', 'heap_total_mib', 'gpu_ram_free_mib', 'cdram_free_mib', 'phycont_free_mib']
SECTION = ['name', 'parent', 'depth', 'ms', 'self_ms', 'max_ms', 'calls_per_frame']
DELTA = ['viewpoint', 'variant', 'reference', 'baseline_ms', 'variant_ms', 'saved_ms', 'saved_pct',
         'baseline_gpu_ms', 'variant_gpu_ms', 'baseline_work_ms', 'variant_work_ms']
TARGET = ['what', 'kind', 'ms', 'pct', 'advice']
KINDS = ['flythrough', 'static', 'follow', 'stress', 'streaming', 'teleport']


def keys(obj, expected, where):
    check(isinstance(obj, dict), f'{where}: not an object')
    check(list(obj) == expected, f'{where}: keys {list(obj)} != {expected}')


def num_or_null(v, where):
    check(v is None or is_num(v), f'{where}: {v!r} is not a number or null')


def stats(obj, where, present):
    if not present:
        check(obj is None, f'{where} should be null')
        return
    keys(obj, STATS, where)
    for k in STATS:
        check(is_num(obj[k]), f'{where}.{k} not a number')
    check(obj['min'] <= obj['median'] <= obj['p90'] <= obj['p95'] <= obj['p99'] <= obj['max'], f'{where} not ordered')


def validate_schema(doc, partial):
    keys(doc, TOP, 'results.json')
    check(doc['schema'] == 'relcs-bench/1' and doc['partial'] is partial and doc['aborted'] is False, 'header')
    check(doc['timestamp'] == STAMP, 'timestamp')
    keys(doc['device'], DEVICE, 'device')
    check(doc['device']['screen'] == [960, 544], 'screen')
    check(all(c in CAPS for c in doc['caps']), 'caps names')
    check(isinstance(doc['settings'], dict) and all(isinstance(v, str) for v in doc['settings'].values()), 'settings')
    keys(doc['boot'], ['engine_ms', 'game_ms', 'ready_ms'], 'boot')
    keys(doc['scores'], ['graphics', 'cpu', 'overall', 'valid'], 'scores')
    for t in doc['tests']:
        w = t.get('id', '?')
        keys(t, TEST, w)
        check(t['kind'] in KINDS, f'{w}: kind')
        for k in ('id', 'group', 'name', 'variant', 'viewpoint', 'skip_reason', 'worst_hitch_group'):
            check(isinstance(t[k], str), f'{w}.{k} not a string')
        for k in ('skipped', 'interrupted'):
            check(isinstance(t[k], bool), f'{w}.{k} not a bool')
        keys(t['scene'], SCENE, f'{w}.scene')
        has = t['frames'] > 0
        check(isinstance(t['frames'], int) and isinstance(t['first_frame'], int), f'{w}: frame counts')
        check(is_num(t['seconds']), f'{w}.seconds')
        check((t['fps'] is not None) == has, f'{w}: fps null rule')
        for k in ('frame_ms', 'work_ms', 'swap_ms'):
            stats(t[k], f'{w}.{k}', has)
        stats(t['gpu_ms'], f'{w}.gpu_ms', t['gpu_samples'] > 0)
        if t['gpu_sync_ms'] is not None:
            stats(t['gpu_sync_ms'], f'{w}.gpu_sync_ms', True)
        for k in ('cpu_bound_pct', 'gpu_bound_pct', 'worst_hitch_ms', 'load_ms', 'settle_ms'):
            num_or_null(t[k], f'{w}.{k}')
        if t['gpu_samples'] > 0:
            check(abs(t['cpu_bound_pct'] + t['gpu_bound_pct'] - 100) < 0.01, f'{w}: bound pct sum')
        else:
            check(t['cpu_bound_pct'] is None and t['gpu_bound_pct'] is None, f'{w}: unknown CPU/GPU split')
        if has:
            keys(t['groups_ms'], GROUPS, f'{w}.groups_ms')
            keys(t['per_frame'], PER_FRAME, f'{w}.per_frame')
            check(near(t['seconds'] * t['fps'], t['frames'], 0.05), f'{w}: fps = frames / seconds')
        else:
            check(t['groups_ms'] is None and t['per_frame'] is None, f'{w}: null blocks without frames')
        keys(t['totals'], TOTALS, f'{w}.totals')
        for k in TOTALS:
            num_or_null(t['totals'][k], f'{w}.totals.{k}')
        check(isinstance(t['threads'], list), f'{w}.threads')
        for th in t['threads']:
            keys(th, ['name', 'ms_per_s'], f'{w}.threads')
        check(t['cpu_load_pct'] is None or (isinstance(t['cpu_load_pct'], list) and all(is_num(x) for x in t['cpu_load_pct'])),
              f'{w}.cpu_load_pct')
        if t['memory'] is not None:
            keys(t['memory'], MEMORY, f'{w}.memory')
        for i, s in enumerate(t['sections']):
            keys(s, SECTION, f'{w}.sections[{i}]')
            check(s['parent'] == -1 or 0 <= s['parent'] < i, f'{w}.sections[{i}] parent index')
            if s['parent'] >= 0:
                check(t['sections'][s['parent']]['depth'] == s['depth'] - 1, f'{w}.sections[{i}] depth')
            check(s['self_ms'] <= s['ms'] + 1e-6, f'{w}.sections[{i}] self <= inclusive')
    for d in doc['deltas']:
        keys(d, DELTA, 'delta')
        check(d['reference'] == ('sim_running' if d['variant'] == 'no_audio' else 'baseline'), 'delta reference')
        check(near(d['saved_ms'], d['baseline_ms'] - d['variant_ms'], 0.002), 'delta saved_ms')
    for t in doc['targets']:
        keys(t, TARGET, 'target')
        check(t['kind'] in ('section', 'feature', 'gpu', 'streaming', 'cpu'), 'target kind')
        check(t['advice'] and len(t['advice']) < 160, f'target advice length: {t["advice"]!r}')
    ms = [t['ms'] for t in doc['targets']]
    check(ms == sorted(ms, reverse=True) and len(ms) <= 16, 'targets sorted by ms, at most 16')


# ---- expected values ----------------------------------------------------------

def by_id(doc):
    return {t['id']: t for t in doc['tests']}


def validate_values(doc):
    T = by_id(doc)
    check(len(doc['tests']) == 27, f'27 tests, got {len(doc["tests"])}')
    check(doc['caps'] == ['gpu_fence', 'gpu_sync', 'sections', 'gl_counters', 'disk', 'threads', 'cpu_load', 'alloc'], 'caps list')
    check(doc['settings'] == {'Mode': 'full', 'FixedStep': '1', 'Seed': '1234', 'Tests': ''}, 'settings parse')
    check(doc['device']['notes'] == 'fake "platform"\tnotes' and doc['device']['cpu_mhz'] == 444, 'device strings')
    check(near(doc['boot']['engine_ms'], 1234.5) and near(doc['boot']['game_ms'], 5678.25), 'boot')

    g1 = T['g1_portland_el']
    check(g1['frames'] == 100 and near(g1['fps'], 100 / 1.55) and near(g1['seconds'], 1.55), 'g1 fps')
    f = g1['frame_ms']
    check([f[k] for k in STATS] == [15.5, 15.0, 19.0, 20.0, 20.0, 11.0, 20.0, 50.0], f'g1 frame stats {f}')
    check(near(g1['work_ms']['mean'], 14.5) and near(g1['swap_ms']['mean'], 1.0), 'g1 work/swap')
    check(g1['gpu_ms'] is None and g1['gpu_samples'] == 0 and g1['gpu_sync_ms'] is None, 'g1 no GPU data')
    check(g1['cpu_bound_pct'] is None and g1['gpu_bound_pct'] is None, 'g1 no GPU classification')
    check(g1['groups_ms'] == {'sim': 5.0, 'audio': 1.0, 'renderlist': 0.0, 'envmap': 0.0, 'scene': 4.0,
                              'fx': 0.0, '2d': 0.0, 'eof': 1.0}, f'g1 groups {g1["groups_ms"]}')
    check(g1['per_frame'] == {'draws': 1000.0, 'indices': 100000.0, 'uniforms': 2000.0, 'tex_binds': 800.0,
                              'upload_kib': 50.0, 'disk_kib': 20.0, 'peds': 30.0, 'vehicles': 12.0,
                              'objects': 40.0}, f'g1 per frame {g1["per_frame"]}')
    tot = g1['totals']
    check(tot['draws'] == 100000 and tot['disk_reads'] == 100 and near(tot['disk_read_ms'], 150) and
          tot['alloc_calls'] == 1000 and tot['vertex_upload_bytes'] == 4096000, f'g1 totals {tot}')
    th = {x['name']: x['ms_per_s'] for x in g1['threads']}
    check(near(th.get('main'), 900, 0.01) and near(th.get('CdStream'), 50, 0.01), f'g1 threads {th}')
    check(g1['cpu_load_pct'] == [90.0, 50.0, 10.0, 0.0], f'g1 cpu load {g1["cpu_load_pct"]}')
    check(g1['memory'] == {'heap_used_mib': 64.0, 'heap_total_mib': 256.0, 'gpu_ram_free_mib': 30.0,
                           'cdram_free_mib': 12.0, 'phycont_free_mib': 26.0}, 'g1 memory')
    check(g1['scene'] == {'hour': 13, 'minute': 0, 'weather': 0, 'ped_density': 1.0, 'car_density': 1.0,
                          'fixed_step': True, 'paused': False}, 'g1 scene')
    sec = {s['name']: s for s in g1['sections']}
    check(set(sec) == {'CGame::Process', 'World::Process', 'World.Control', 'DMAudio.Service', 'RenderScene',
                       'Scene.World1Opaque', 'EndOfFrame'}, f'g1 sections {sorted(sec)}')
    names = [s['name'] for s in g1['sections']]
    check(near(sec['CGame::Process']['ms'], 5) and near(sec['CGame::Process']['self_ms'], 2), 'CGame::Process self')
    check(names[sec['World.Control']['parent']] == 'World::Process' and sec['World.Control']['depth'] == 2, 'section parent')
    check(near(sec['World.Control']['calls_per_frame'], 40) and near(sec['EndOfFrame']['max_ms'], 1), 'section calls/max')

    g2 = T['g2_staunton_towers']
    check(g2['frames'] == 150 and near(g2['fps'], 50) and g2['gpu_samples'] == 150, 'g2 frames/GPU samples')
    check(all(near(g2['gpu_ms'][k], 18) for k in STATS if k != 'low1_fps'), f'g2 gpu {g2["gpu_ms"]}')
    check(near(g2['gpu_bound_pct'], 100) and near(g2['cpu_bound_pct'], 0), 'g2 gpu bound')

    c1 = T['c1_crowd_d1']
    check(near(c1['fps'], 40) and near(c1['work_ms']['mean'], 24.02) and near(c1['swap_ms']['mean'], 0.98), 'c1 values')
    check(T['c1_crowd_d3']['interrupted'] is True, 'c1 x3 interrupted')
    check(near(T['c2_traffic_follow']['fps'], 100 / 3), 'c2 fps')

    s1 = T['s1_bridge_run']
    check(s1['frames'] == 60 and near(s1['fps'], 60 / 1.68) and s1['hitches'] == 1, 's1 hitch count')
    check(near(s1['worst_hitch_ms'], 500) and s1['worst_hitch_group'] == 'sim', 's1 worst hitch')
    sec = {s['name']: s for s in s1['sections']}
    la = sec.get('Streaming::LoadAll')
    check(la and near(la['max_ms'], 450) and near(la['calls_per_frame'], 0.05) and near(la['ms'], 7.5), 's1 LoadAll')
    check(near(sec['CGame::Process']['max_ms'], 465) and s1['scene']['fixed_step'] is False, 's1 sections')

    tp = T['s2_tp_redlight']
    check(tp['kind'] == 'teleport' and near(tp['load_ms'], 850) and near(tp['settle_ms'], 1200), 'teleport timings')
    check(tp['totals']['disk_bytes'] == 5 * 1048576 + 30 * 20480, 'teleport totals include the load')
    check(g1['load_ms'] is None and g1['settle_ms'] is None, 'load_ms null when unknown')

    sk = T['iso_a_chinatown_draw_tiny']
    check(sk['skipped'] and sk['skip_reason'] == 'capacite absente (draw_mode)' and sk['frames'] == 0, 'skipped test')
    check(sk['fps'] is None and sk['frame_ms'] is None and sk['groups_ms'] is None, 'skipped test nulls')
    b = T['iso_b_bedford_no_shadows']
    check(b['viewpoint'] == 'b_bedford' and b['variant'] == 'no_shadows', 'viewpoint/variant parsed from the id')
    gs = T['iso_a_chinatown_gpu_sync']
    check(gs['gpu_sync_ms'] and near(gs['gpu_sync_ms']['mean'], 4) and near(gs['frame_ms']['mean'], 34), 'gpu sync')
    check(T['iso_a_chinatown_baseline']['scene']['paused'] is True, 'paused scene flag')

    D = {(d['viewpoint'], d['variant']): d for d in doc['deltas']}
    check(len(doc['deltas']) == 15, f'15 deltas, got {len(doc["deltas"])}')
    expect = {
        ('a_chinatown', 'no_shadows'): (30, 28, 2.0), ('a_chinatown', 'no_peds'): (30, 27, 3.0),
        ('a_chinatown', 'no_water'): (30, 29.8, 0.2), ('a_chinatown', 'viewport_50'): (30, 20, 10.0),
        ('a_chinatown', 'no_vehicles'): (30, 29, 1.0), ('a_chinatown', 'hud_on'): (30, 31.5, -1.5),
        ('b_bedford', 'lod_high'): (40, 41.1, -1.1),
        ('a_chinatown', 'gpu_sync'): (30, 34, -4.0), ('a_chinatown', 'baseline_end'): (30, 30.1, -0.1),
        ('a_chinatown', 'sim_running'): (30, 35, -5.0), ('a_chinatown', 'no_audio'): (35, 33.5, 1.5),
        ('b_bedford', 'no_shadows'): (40, 37, 3.0), ('b_bedford', 'no_peds'): (40, 39, 1.0),
        ('b_bedford', 'sim_running'): (40, 44, -4.0), ('b_bedford', 'no_audio'): (44, 43, 1.0),
    }
    for k, (base, var, saved) in expect.items():
        d = D.get(k)
        check(d is not None, f'missing delta {k}')
        check(near(d['baseline_ms'], base) and near(d['variant_ms'], var) and near(d['saved_ms'], saved),
              f'delta {k}: {d}')
        check(near(d['saved_pct'], 100 * saved / base), f'delta pct {k}')
        check(d['baseline_gpu_ms'] is None and d['variant_gpu_ms'] is None, f'delta gpu null {k}')
    check(near(D[('a_chinatown', 'no_shadows')]['variant_work_ms'], 23), 'delta work')

    tg = {(t['kind'], t['what']): t for t in doc['targets']}
    check(len(doc['targets']) == 16, f'16 targets, got {len(doc["targets"])}: {list(tg)}')
    mean_frame = 6050 / 300
    exp_targets = {
        ('streaming', 'teleportation'): (1300, 0), ('streaming', 's1_bridge_run'): (500, 500 / 1680 * 100),
        ('gpu', 'GPU'): (18, 18 / 18.2 * 100), ('section', 'EndOfFrame'): (2000 / 300, 2000 / 300 / mean_frame * 100),
        ('cpu', 'simulation'): (4.5, 4.5 / 39.5 * 100), ('section', 'Scene.World1Opaque'): (3, 3 / mean_frame * 100),
        ('feature', 'no_shadows'): (2.5, (20 / 3 + 7.5) / 2), ('section', 'CGame::Process'): (2, 2 / mean_frame * 100),
        ('section', 'World.Control'): (2, 2 / mean_frame * 100), ('feature', 'no_peds'): (2, 6.25),
        ('feature', 'no_audio'): (1.25, (150 / 35 + 100 / 44) / 2), ('section', 'World::Process'): (1, 1 / mean_frame * 100),
        ('section', 'DMAudio.Service'): (1, 1 / mean_frame * 100), ('section', 'RenderScene'): (1, 1 / mean_frame * 100),
        ('feature', 'hud_on'): (1.5, 5.0), ('feature', 'lod_high'): (1.1, 2.75),
    }
    check(('feature', 'no_vehicles') not in tg, 'the 17th candidate is dropped (BENCH_MAX_TARGETS)')
    for k, (ms, pct) in exp_targets.items():
        t = tg.get(k)
        check(t is not None, f'missing target {k}')
        check(near(t['ms'], ms) and near(t['pct'], pct), f'target {k}: {t} (expected {ms}, {pct})')
    check('remplissage' in tg[('gpu', 'GPU')]['advice'], 'GPU fill-rate advice')
    check('groupe sim' in tg[('streaming', 's1_bridge_run')]['advice'], 'hitch group in advice')
    check(any(ch in ''.join(t['advice'] for t in doc['targets']) for ch in 'éèà'), 'UTF-8 accents in advice')
    order = [(t['kind'], t['what']) for t in doc['targets']]
    check(order[:3] == [('streaming', 'teleportation'), ('streaming', 's1_bridge_run'), ('gpu', 'GPU')], 'target order')

    sc = doc['scores']
    g = 100 * math.sqrt(100 / 1.55 * 50)
    c = 100 * math.sqrt(40 * 100 / 3)
    o = 100 * math.exp((math.log(100 / 1.55) + math.log(50) + math.log(40) + math.log(100 / 3) + math.log(60 / 1.68)) / 5)
    check(sc['valid'] is True and near(sc['graphics'], g, 0.01) and near(sc['cpu'], c, 0.01) and near(sc['overall'], o, 0.01),
          f'scores {sc} (expected {g:.3f} {c:.3f} {o:.3f})')


def validate_partial(doc):
    validate_schema(doc, True)
    check([t['id'] for t in doc['tests']] == ['g1_portland_el', 'g2_staunton_towers'], 'partial tests')
    check(doc['tests'][1]['gpu_samples'] == 149, 'partial: last GPU completion not arrived yet')
    check(doc['scores']['valid'] is False and doc['scores']['cpu'] is None, 'partial scores')


def validate_csv(path, doc):
    text = path.read_text(encoding='utf-8')
    header = text.split('\n', 1)[0]
    check(header == csv_header(), f'frames.csv header {header!r}')
    rows = list(csv.DictReader(io.StringIO(text)))
    total = sum(t['frames'] for t in doc['tests'])
    check(len(rows) == total == 820, f'frames.csv rows {len(rows)} (tests say {total})')
    check(text.endswith('\n') and '\r' not in text, 'frames.csv line endings')
    for t in doc['tests']:
        if not t['frames']:
            continue
        first = rows[t['first_frame']]
        last = rows[t['first_frame'] + t['frames'] - 1]
        check(first['test'] == t['id'] == last['test'] and first['frame'] == '0' and
              last['frame'] == str(t['frames'] - 1), f'frames.csv range of {t["id"]}')
        check(first['t_ms'] == '0.000', f'{t["id"]} t_ms starts at 0')
    for r in rows:
        for k in ('t_ms', 'frame_ms', 'work_ms', 'swap_ms', 'gpu_ms', 'gpu_sync_ms', 'sim_ms', 'eof_ms'):
            check(re.fullmatch(r'\d+\.\d{3}', r[k]), f'frames.csv {k}={r[k]!r}')
        int(r['flags'])
    by = {}
    for r in rows:
        by.setdefault(r['test'], []).append(r)
    g1 = by['g1_portland_el']
    check([r['frame_ms'] for r in g1[:3]] == ['11.000', '12.000', '13.000'] and g1[2]['t_ms'] == '23.000', 'g1 rows')
    check(g1[0]['draws'] == '1000' and g1[0]['upload_kib'] == '50.000' and g1[0]['cam_y'] == '-200.500', 'g1 row counters')
    check(all(r['gpu_ms'] == '18.000' for r in by['g2_staunton_towers']), 'g2 gpu_ms rows')
    ns = by['c1_crowd_d1'][10]
    check(int(ns['flags']) & 2 and ns['swap_ms'] == '0.000' and ns['work_ms'] == '25.000', 'no-swap row')
    check(all(int(r['flags']) & 2 == 0 for i, r in enumerate(by['c1_crowd_d1']) if i != 10), 'swap flags')
    h = by['s1_bridge_run'][30]
    check(int(h['flags']) == 1 | 16 and h['frame_ms'] == '500.000' and h['work_ms'] == '247.500' and
          h['swap_ms'] == '5.000' and h['sim_ms'] == '465.000', f'hitch row {h}')
    check(sum(int(r['flags']) & 16 != 0 for r in rows) == 1, 'one hitch flag')
    gs = by['iso_a_chinatown_gpu_sync']
    check(all(int(r['flags']) == 4 | 8 and r['gpu_sync_ms'] == '4.000' for r in gs), 'gpu-sync rows')
    check(all(int(r['flags']) & 8 == 0 for r in by['iso_a_chinatown_sim_running']), 'running rows not paused')


def validate_summary(path):
    raw = path.read_bytes()
    text = raw.decode('utf-8')
    check('\r' not in text, 'summary line endings')
    for s in ('reLCS Benchmark - Résultats', 'Scores', 'Graphismes :', 'Tests', 'FPS moy.', '1 % bas', 'p95 ms',
              'CPU ms', 'GPU ms', '% limité CPU/GPU', 'Statut', 'Démarrage', 'À optimiser en priorité',
              'Isolation', 'Point de vue a_chinatown', 'Point de vue b_bedford', 'Streaming et téléportations',
              'Pics', 'Fichiers', 'results.json', 'frames.csv', 'Comment lire ces résultats',
              'ignoré : capacite absente (draw_mode)', 'interrompu', 'sans ombres', 'firmware'.capitalize(),
              'test-build-1', 'CPU 444 MHz'):
        check(s in text, f'summary.txt lacks {s!r}')
    lines = text.splitlines()
    iso = lines.index(next(l for l in lines if l.startswith('Point de vue a_chinatown')))
    gains = []
    for l in lines[iso + 2:]:
        if not l.startswith('  ') or 'ignor' in l:
            break
        gains.append(float(l.split()[3]))
    check(gains == sorted(gains, reverse=True) and len(gains) == 10, f'isolation rows sorted by gain {gains}')
    check(lines[0] == 'reLCS Benchmark - Résultats' and lines[1] == '=' * len(lines[0]), 'summary title')
    for i, l in enumerate(lines):
        if l and set(l) == {'-'}:
            check(len(l) == len(lines[i - 1]), f'underline of {lines[i - 1]!r}')
    check(any(l.startswith('s1_bridge_run') and '1 pic(s), pire 500 ms (groupe sim)' in l and 'chargement' not in l
              for l in lines), 's1 streaming line')
    check('Point de vue a_chinatown : référence 30.00 ms (CPU 25.00 ms, GPU non mesuré)' in lines, 'viewpoint header')


def validate_overflow(folder):
    doc = load_json(folder/'results.json')
    validate_schema(doc, False)
    check(len(doc['tests']) == 1 and doc['tests'][0]['frames'] == 5 and doc['scores']['valid'] is False, 'overflow run')
    rows = (folder/'frames.csv').read_text(encoding='utf-8').strip().split('\n')
    check(len(rows) == 6, 'overflow csv rows')
    check('images non enregistrées' in (folder/'summary.txt').read_text(encoding='utf-8'), 'overflow warning')


# ---- static check of the built-in advice tables --------------------------------

def decode_c(lit):
    b = bytearray()
    i = 0
    while i < len(lit):
        ch = lit[i]
        if ch == '\\':
            n = lit[i + 1]
            if n in '01234567':
                j = i + 1
                while j < i + 4 and j < len(lit) and lit[j] in '01234567':
                    j += 1
                b.append(int(lit[i + 1:j], 8))
                i = j
                continue
            b += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\'}[n]
            i += 2
            continue
        b += ch.encode('ascii')
        i += 1
    return bytes(b)


def ascii_len(text):
    text = text.replace('œ', 'oe')
    return len(unicodedata.normalize('NFKD', text).encode('ascii', 'ignore'))


def validate_tables():
    src = SOURCE.read_text(encoding='ascii')
    macros = {m.group(1): decode_c(m.group(2)) for m in re.finditer(r'#define (FR_\w+)\s+"([^"]*)"', src)}
    count = 0
    for table in ('kVariants', 'kSectionAdvice'):
        body = re.search(table + r'\[\] = \{(.*?)\n\};', src, re.S).group(1)
        for entry in re.findall(r'\{(.*?)\},?\s*(?=\{|$)', body, re.S):
            fields, cur = [], []
            for tok in re.findall(r'"(?:\\.|[^"\\])*"|FR_\w+|,|[^\s,"]+', entry):
                if tok == ',':
                    fields.append(cur)
                    cur = []
                else:
                    cur.append(decode_c(tok[1:-1]) if tok.startswith('"') else macros.get(tok))
            fields.append(cur)
            for f in fields:
                if not all(isinstance(x, bytes) for x in f):
                    continue
                value = b''.join(f).decode('utf-8')
                check(len(value.encode()) < 256 and ascii_len(value) < 160, f'{table}: text too long {value!r}')
                count += 1
    check(count > 60, f'advice tables parsed ({count} strings)')


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        out = tmp/'out'
        out.mkdir()
        exe, env = build(tmp/'bench_metrics_test',
                         [SUPPORT/'bench_metrics_test.cpp', SUPPORT/'bench_fake_platform.cpp', SOURCE],
                         [EXTRAS], ['RELCS_BENCHMARK'])
        print(run(exe, env, [str(out)]).strip())
        compile_cxx14(env)
        print('PASS: benchmark_metrics.cpp builds as C++14')
        folder = out/STAMP
        doc = load_json(folder/'results.json')
        validate_schema(doc, False)
        print('PASS: results.json follows the relcs-bench/1 schema')
        validate_values(doc)
        print('PASS: statistics, GPU fence times, deltas, targets and scores match the synthetic run')
        validate_partial(load_json(out/'partial.json'))
        print('PASS: partial results.json after each test')
        validate_csv(folder/'frames.csv', doc)
        print('PASS: frames.csv header, rows and flags')
        validate_summary(folder/'summary.txt')
        print('PASS: summary.txt (UTF-8 French)')
        validate_overflow(out/'overflow/deep')
        print('PASS: frame buffer overflow')
        unavailable = load_json(out/'unavailable/results.json')
        t = unavailable['tests'][0]
        check(t['gpu_samples'] == 0 and t['gpu_ms'] is None, 'unavailable GPU timer stays null')
        check(t['cpu_bound_pct'] is None and t['gpu_bound_pct'] is None, 'long swap is not GPU classification')
        check(not any(x['kind'] == 'gpu' for x in unavailable['targets']), 'no advice from unknown GPU time')
        print('PASS: unavailable GPU fence does not classify display/GC waits as GPU work')
    validate_tables()
    print('PASS: built-in advice texts fit BenchTarget::advice')


if __name__ == '__main__':
    main()
