#!/usr/bin/env python3
"""Synthetic reLCS Benchmark results folder (results.json + frames.csv).

Writes a plausible run in schema "relcs-bench/1" covering every test id of the
catalogue (docs/BENCHMARK.md) so that tools/vita/bench-report.py can be
exercised without hardware:

  python tools/vita/test_support/bench_sample.py OUT [--variant N] [--profile vita|pc] [--partial]

  --variant N   changes the numbers a little (GPU a bit faster, simulation a bit
                slower, different noise) to test --compare
  --profile pc  Windows D3D9 run: no GPU timing, no sections, no counters, caps []
  --partial     run cut after a few tests: "partial": true, results.json only

Numbers come from a small cost model (ms per profiler group, GPU ms) close to
what the Vita logs show; nothing here is measured.
"""
import argparse
import csv
import json
import math
import random
from pathlib import Path

GROUPS = ['sim', 'audio', 'renderlist', 'envmap', 'scene', 'fx', '2d', 'eof']
CPU_GROUPS = GROUPS[:-1]
CSV_HEADER = ('test,frame,t_ms,frame_ms,work_ms,swap_ms,gpu_ms,gpu_sync_ms,'
              'sim_ms,audio_ms,renderlist_ms,envmap_ms,scene_ms,fx_ms,2d_ms,eof_ms,'
              'draws,indices,uniforms,tex_binds,upload_kib,disk_kib,peds,vehicles,objects,stream_req,'
              'cam_x,cam_y,cam_z,flags').split(',')
BF_GPU_SYNC, BF_PAUSED, BF_HITCH = 4, 8, 16
EOF_PRE_SWAP = 0.30          # part of EndOfFrame before the swap (counted in work)
ALL_CAPS = ['gpu_fence', 'gpu_sync', 'viewport', 'draw_mode', 'flat_tex', 'sections',
            'gl_counters', 'disk', 'threads', 'cpu_load']
VITA_CAPS = [c for c in ALL_CAPS if c != 'flat_tex']
CAP_NEEDED = {'viewport_75': 'viewport', 'viewport_50': 'viewport', 'viewport_25': 'viewport',
              'viewport_min': 'viewport', 'flat_textures': 'flat_tex', 'draw_tiny': 'draw_mode',
              'draw_null': 'draw_mode', 'gpu_sync': 'gpu_sync'}


def M(sim, audio, renderlist, envmap, scene, fx, twod, gpu, draws, peds, veh, obj):
    return {'sim': sim, 'audio': audio, 'renderlist': renderlist, 'envmap': envmap,
            'scene': scene, 'fx': fx, '2d': twod, 'gpu': gpu, 'draws': draws,
            'peds': peds, 'vehicles': veh, 'objects': obj}


# id, name, kind, (hour, minute, weather), densities, model, frames, camera path
GAMEPLAY = [
    ('g1_portland_el', 'graphics', 'Portland - metro aerien', 'flythrough', (13, 0, 0), (1.0, 1.0),
     M(4.8, 1.3, 2.0, 1.2, 8.8, 6.2, 0.3, 31.0, 1080, 16, 12, 40), 900,
     [(1200, -560, 22), (1180, -420, 24), (1120, -300, 23), (1010, -250, 22)]),
    ('g2_staunton_towers', 'graphics', 'Staunton - gratte-ciel', 'flythrough', (13, 0, 0), (1.0, 1.0),
     M(4.6, 1.3, 2.6, 1.3, 10.5, 7.0, 0.3, 36.0, 1390, 14, 11, 55), 900,
     [(147, -1700, 60), (147, -1550, 60), (147, -1350, 60), (147, -1250, 80), (147, -1150, 100), (350, -900, 80)]),
    ('g3_shoreside_hills', 'graphics', 'Shoreside - collines', 'flythrough', (17, 0, 4), (1.0, 1.0),
     M(4.5, 1.2, 1.8, 1.0, 9.0, 5.5, 0.3, 29.5, 960, 10, 9, 30), 900,
     [(-736, -627, 60), (-736, -424, 60), (-850, -290, 75), (-975, -174, 75), (-1020, 22, 90), (-1033, 99, 95)]),
    ('g4_portland_night_rain', 'graphics', 'Portland - nuit et pluie', 'flythrough', (23, 0, 2), (1.0, 1.0),
     M(4.4, 1.4, 2.1, 1.2, 8.0, 8.5, 0.3, 38.0, 1210, 7, 12, 38), 900,
     [(1091, -129, 14), (977, -170, 12), (990, -263, 11), (978, -339, 12), (911, -399, 13),
      (866, -454, 13), (836, -534, 14), (846, -624, 14), (901, -779, 15)]),
    ('c1_crowd_d0', 'cpu', 'Foule x0', 'static', (12, 0, 0), (0.0, 0.0),
     M(2.9, 1.2, 1.6, 1.2, 8.4, 4.6, 0.3, 24.0, 880, 0, 2, 35), 240, [(900.8, -788.8, 15.8)]),
    ('c1_crowd_d1', 'cpu', 'Foule x1', 'static', (12, 0, 0), (1.0, 1.0),
     M(5.6, 1.3, 2.0, 1.2, 8.6, 6.1, 0.3, 27.0, 1020, 18, 14, 36), 240, [(900.8, -788.8, 15.8)]),
    ('c1_crowd_d2', 'cpu', 'Foule x2', 'static', (12, 0, 0), (2.0, 2.0),
     M(8.9, 1.3, 2.4, 1.2, 8.7, 7.6, 0.3, 30.0, 1170, 34, 26, 37), 240, [(900.8, -788.8, 15.8)]),
    ('c1_crowd_d3', 'cpu', 'Foule x3', 'static', (12, 0, 0), (3.0, 3.0),
     M(12.2, 1.3, 2.8, 1.2, 8.8, 8.9, 0.3, 32.5, 1310, 47, 35, 37), 240, [(900.8, -788.8, 15.8)]),
    ('c2_traffic_follow', 'cpu', 'Conduite IA', 'follow', (13, 0, 0), (1.0, 1.0),
     M(7.4, 1.5, 2.2, 1.2, 8.5, 7.0, 1.0, 29.0, 1120, 15, 16, 30), 900,
     [(1051, -949, 14), (1060, -760, 14), (1000, -560, 14), (930, -420, 13)]),
    ('c3_mayhem', 'cpu', 'Chaos: explosions et emeutes', 'stress', (13, 0, 0), (2.0, 1.0),
     M(9.5, 1.6, 2.6, 1.2, 8.2, 11.0, 0.3, 37.0, 1450, 32, 12, 44), 750, [(1010, -700, 30)]),
    ('s1_bridge_run', 'streaming', 'Streaming: pont Callahan', 'streaming', (13, 0, 0), (1.0, 1.0),
     M(5.8, 1.3, 2.2, 1.2, 9.0, 6.5, 0.3, 31.0, 1150, 12, 13, 40), 560,
     [(900, -800, 25), (734, -930, 45), (498, -930, 45), (300, -1000, 35), (147, -1300, 35)]),
]

TELEPORTS = [
    ('s2_tp_redlight', 'Teleportation: Red Light', (1010, -520, 14), 1150, 1900),
    ('s2_tp_bedford', 'Teleportation: Bedford Point', (140, -1360, 26), 2350, 3600),
    ('s2_tp_airport', 'Teleportation: aeroport', (-1210, -560, 12), 2600, 4200),
    ('s2_tp_harbor', 'Teleportation: port', (1310, -800, 13), 980, 1700),
    ('s2_tp_rockford', 'Teleportation: Rockford', (40, -300, 30), 1900, 2900),
    ('s2_tp_cedargrove', 'Teleportation: Cedar Grove', (-60, 280, 25), 2100, 3300),
]

VIEWPOINTS = [
    ('a_chinatown', (900.8, -788.8, 15.8), M(0.25, 1.3, 2.2, 1.4, 7.6, 6.7, 0.3, 27.5, 1100, 17, 13, 36)),
    ('b_bedford', (146.9, -1436.4, 27.0), M(0.25, 1.3, 3.0, 1.4, 10.5, 8.0, 0.3, 23.0, 1480, 12, 10, 58)),
]

# variant -> (component multipliers, gpu: ('+', ms) / ('x', factor) / ('=', ms))
ISO_VARIANTS = [
    ('baseline', {}, None),
    ('no_peds', {'renderlist': 0.85, 'fx': 0.84, 'scene': 0.98}, ('+', -1.6)),
    ('no_vehicles', {'renderlist': 0.9, 'fx': 0.72}, ('+', -2.8)),
    ('no_objects', {'scene': 0.9, 'renderlist': 0.93}, ('+', -1.1)),
    ('no_world_lod', {'scene': 0.93}, ('+', -1.4)),
    ('no_world_opaque', {'scene': 0.6, 'renderlist': 0.85}, ('+', -6.2)),
    ('no_world_transparent', {'fx': 0.72}, ('+', -4.1)),
    ('no_water', {'scene': 0.97}, ('+', -0.9)),
    ('no_sky', {'scene': 0.98}, ('+', -0.5)),
    ('no_shadows', {'fx': 0.9}, ('+', -1.3)),
    ('no_coronas', {'fx': 0.96}, ('+', -0.4)),
    ('no_particles_fx', {'fx': 0.92}, ('+', -0.6)),
    ('no_envmap', {'envmap': 0.05}, ('+', -2.4)),
    ('no_postfx', {'fx': 0.97}, ('+', -1.9)),
    ('hud_on', {'2d': 3.2}, ('+', 0.5)),
    ('ps2_alpha_off', {'scene': 0.99}, ('+', -1.0)),
    ('backface_cull', {}, ('+', -0.8)),
    ('lod_low', {'scene': 0.8, 'renderlist': 0.8, 'fx': 0.9}, ('+', -3.1)),
    ('lod_high', {'scene': 1.35, 'renderlist': 1.4, 'fx': 1.15}, ('+', 5.8)),
    ('farclip_300', {'scene': 0.78, 'renderlist': 0.8}, ('+', -3.9)),
    ('viewport_75', {}, ('x', 0.82)),
    ('viewport_50', {}, ('x', 0.55)),
    ('viewport_25', {}, ('x', 0.36)),
    ('viewport_min', {}, ('x', 0.26)),
    ('flat_textures', {}, ('+', -2.0)),
    ('draw_tiny', {'scene': 0.97, 'fx': 0.97}, ('=', 2.6)),
    ('draw_null', {'scene': 0.35, 'fx': 0.45, 'envmap': 0.5}, ('=', 1.0)),
    ('gpu_sync', {}, None),
    ('no_render_3d', {'scene': 0.05, 'fx': 0.05, 'envmap': 0.05}, ('=', 0.8)),
    ('baseline_end', {}, None),
    ('sim_running', {'sim': 26.0}, ('+', 0.3)),
    ('no_audio', {'sim': 26.0, 'audio': 0.04}, ('+', 0.3)),
]
UNPAUSED = ('sim_running', 'no_audio')


def lerp_path(path, f):
    if len(path) == 1:
        return path[0]
    seg = min(len(path) - 2, int(f * (len(path) - 1)))
    u = f * (len(path) - 1) - seg
    a, b = path[seg], path[seg + 1]
    return tuple(a[i] + (b[i] - a[i]) * u for i in range(3))


def percentile(sorted_vals, p):
    n = len(sorted_vals)
    return sorted_vals[max(0, min(n - 1, math.ceil(p / 100.0 * n) - 1))]


def stats(vals):
    if not vals:
        return None
    s = sorted(vals)
    n = len(s)
    worst = s[-max(1, n // 100):]
    wmean = sum(worst) / len(worst)
    out = {'mean': sum(s) / n, 'median': percentile(s, 50), 'p90': percentile(s, 90),
           'p95': percentile(s, 95), 'p99': percentile(s, 99), 'min': s[0], 'max': s[-1],
           'low1_fps': 1000.0 / wmean if wmean > 0 else None}
    return {k: (round(v, 3) if v is not None else None) for k, v in out.items()}


class Gen:
    def __init__(self, variant, profile):
        self.variant = variant
        self.pc = profile == 'pc'
        self.rng = random.Random(1234 + 97 * variant + (5000 if self.pc else 0))
        self.caps = [] if self.pc else list(VITA_CAPS)
        self.tests = []
        self.rows = []

    # ---- frames -------------------------------------------------------------
    def frames(self, test_id, m, n, path, paused=False, gpu_sync=False, hitches=None,
               stream_burst=None, wobble=0.10, noise=0.035):
        rng = self.rng
        v = self.variant
        m = dict(m)
        if self.pc:
            for g in CPU_GROUPS:
                m[g] *= 0.22
            m['gpu'] = 0.0
        else:
            m['gpu'] *= 1.0 - 0.05 * v
            m['sim'] *= 1.0 + 0.03 * v
        ph = rng.random() * 6.28
        rows = []
        t = 0.0
        hitches = hitches or {}
        for i in range(n):
            f = i / max(1, n - 1)
            k = 1 + wobble * math.sin(2 * math.pi * f * 1.7 + ph) + wobble * 0.5 * math.sin(2 * math.pi * f * 4.3 + 2 * ph)
            if paused:
                k = 1.0
            comp = {}
            for g in CPU_GROUPS:
                kk = 1.0 if g in ('audio', '2d') else k
                comp[g] = max(0.0, m[g] * kk * (1 + rng.gauss(0, noise)))
            comp['sim'] += hitches.get(i, 0.0)
            work = sum(comp.values()) + (EOF_PRE_SWAP * (0.3 if self.pc else 1.0))
            gpu = max(0.0, m['gpu'] * k * (1 + rng.gauss(0, noise))) if m['gpu'] > 0 else 0.0
            sync = 0.0
            if gpu_sync and gpu > 0:
                sync = gpu + 0.15 + abs(rng.gauss(0, 0.1))
                frame = work + sync + 0.25
            elif gpu > 0:
                frame = max(work + 0.25, gpu + 0.35) + abs(rng.gauss(0, 0.15))
            else:
                frame = work + 0.1 + abs(rng.gauss(0, 0.2))
            swap = frame - work
            burst = stream_burst(i) if stream_burst else 0.0
            draws = max(0, m['draws'] * k * (1 + rng.gauss(0, 0.03)))
            if m['scene'] < 1.0:
                draws *= 0.1
            gpu_out = gpu
            if not self.pc and rng.random() < 0.03:
                gpu_out = 0.0      # fence sample lost
            cam = lerp_path(path, f)
            flags = (BF_PAUSED if paused else 0) | (BF_GPU_SYNC if gpu_sync else 0)
            row = {'test': test_id, 'frame': i, 't_ms': t, 'frame_ms': frame, 'work_ms': work,
                   'swap_ms': swap, 'gpu_ms': gpu_out, 'gpu_sync_ms': sync}
            for g in CPU_GROUPS:
                row[g + '_ms'] = comp[g] * (0.0 if self.pc else 1.0)
            row['eof_ms'] = 0.0 if self.pc else EOF_PRE_SWAP + swap
            gl = 0.0 if self.pc else 1.0
            row.update({'draws': round(draws * gl), 'indices': round(draws * 114 * gl),
                        'uniforms': round(draws * 1.9 * gl), 'tex_binds': round(draws * 0.85 * gl),
                        'upload_kib': (40 + abs(rng.gauss(0, 12)) + burst * 0.3) * gl,
                        'disk_kib': (burst if not self.pc else 0.0),
                        'peds': max(0, round(m['peds'] + rng.gauss(0, 1.0))) if m['peds'] else 0,
                        'vehicles': max(0, round(m['vehicles'] + rng.gauss(0, 0.8))) if m['vehicles'] else 0,
                        'objects': max(0, round(m['objects'] + rng.gauss(0, 1.5))),
                        'stream_req': (int(burst / 60) if burst else 0),
                        'cam_x': cam[0], 'cam_y': cam[1], 'cam_z': cam[2], 'flags': flags})
            rows.append(row)
            t += frame
        med = sorted(r['frame_ms'] for r in rows)[len(rows) // 2]
        for r in rows:
            if r['frame_ms'] > max(100.0, 3 * med):
                r['flags'] |= BF_HITCH
        return rows

    # ---- one test result --------------------------------------------------------
    def result(self, test_id, group, name, kind, scene, rows, variant='', viewpoint='',
               load_ms=None, settle_ms=None, interrupted=False):
        rng = self.rng
        n = len(rows)
        fr = [r['frame_ms'] for r in rows]
        gpu = [r['gpu_ms'] for r in rows if r['gpu_ms'] > 0]
        sync = [r['gpu_sync_ms'] for r in rows if r['gpu_sync_ms'] > 0]
        secs = sum(fr) / 1000.0
        both = [(r['work_ms'], r['gpu_ms']) for r in rows if r['gpu_ms'] > 0]
        cpu_b = 100.0 * sum(1 for w, g in both if w >= g) / len(both) if both else None
        groups = {g: round(sum(r[g + '_ms'] for r in rows) / n, 3) for g in GROUPS}
        hitch_rows = [r for r in rows if r['flags'] & BF_HITCH]
        worst = max(hitch_rows, key=lambda r: r['frame_ms']) if hitch_rows else None
        worst_group = ''
        if worst is not None:
            worst_group = max(GROUPS, key=lambda g: worst[g + '_ms']) if not self.pc else ''
        mean = lambda key: round(sum(r[key] for r in rows) / n, 3)
        draws_total = sum(r['draws'] for r in rows)
        disk_total = sum(r['disk_kib'] for r in rows) * 1024
        t = {
            'id': test_id, 'group': group, 'name': name, 'variant': variant, 'viewpoint': viewpoint,
            'kind': kind, 'skipped': False, 'skip_reason': '', 'interrupted': interrupted,
            'scene': scene, 'frames': n, 'first_frame': len(self.rows), 'seconds': round(secs, 3),
            'fps': round(n / secs, 3) if secs > 0 else None,
            'frame_ms': stats(fr), 'work_ms': stats([r['work_ms'] for r in rows]),
            'swap_ms': stats([r['swap_ms'] for r in rows]),
            'gpu_ms': stats(gpu) if gpu else None, 'gpu_sync_ms': stats(sync) if sync else None,
            'gpu_samples': len(gpu),
            'cpu_bound_pct': round(cpu_b, 1) if cpu_b is not None else None,
            'gpu_bound_pct': round(100.0 - cpu_b, 1) if cpu_b is not None else None,
            'groups_ms': groups, 'hitches': len(hitch_rows),
            'worst_hitch_ms': round(worst['frame_ms'], 3) if worst else 0.0,
            'worst_hitch_group': worst_group,
            'per_frame': {'draws': mean('draws'), 'indices': mean('indices'), 'uniforms': mean('uniforms'),
                          'tex_binds': mean('tex_binds'), 'upload_kib': mean('upload_kib'),
                          'disk_kib': mean('disk_kib'), 'peds': mean('peds'), 'vehicles': mean('vehicles'),
                          'objects': mean('objects')},
            'totals': self.totals(rows, draws_total, disk_total),
            'threads': [] if self.pc else self.threads(secs, disk_total),
            'cpu_load_pct': None if self.pc else [round(min(99.0, 88 + rng.random() * 9), 1),
                                                  round(12 + rng.random() * 14, 1),
                                                  round(8 + rng.random() * 8, 1), round(2 + rng.random() * 4, 1)],
            'memory': None if self.pc else {'heap_used_mib': round(118 + rng.random() * 14, 1), 'heap_total_mib': 180.0,
                                            'gpu_ram_free_mib': round(22 + rng.random() * 9, 1),
                                            'cdram_free_mib': round(19 + rng.random() * 6, 1),
                                            'phycont_free_mib': round(9 + rng.random() * 4, 1)},
            'load_ms': load_ms, 'settle_ms': settle_ms,
            'sections': [] if self.pc else self.sections(rows, groups),
        }
        self.tests.append(t)
        self.rows.extend(rows)
        return t

    def totals(self, rows, draws, disk_bytes):
        if self.pc:
            keys = ['draws', 'indices', 'uniforms', 'tex_binds', 'buf_binds', 'programs', 'state_calls',
                    'vertex_upload_bytes', 'index_upload_bytes', 'texture_upload_bytes', 'texture_upload_ms',
                    'shader_compiles', 'disk_reads', 'disk_bytes', 'disk_read_ms', 'disk_sync_waits',
                    'disk_sync_ms', 'alloc_calls', 'free_calls', 'alloc_bytes']
            return {k: 0 for k in keys}
        up = sum(r['upload_kib'] for r in rows) * 1024
        reads = int(disk_bytes / 65536)
        waits = sum(1 for r in rows if r['flags'] & BF_HITCH)
        return {'draws': int(draws), 'indices': int(sum(r['indices'] for r in rows)),
                'uniforms': int(sum(r['uniforms'] for r in rows)), 'tex_binds': int(sum(r['tex_binds'] for r in rows)),
                'buf_binds': int(draws * 0.53), 'programs': int(len(rows) * 13), 'state_calls': int(draws * 1.25),
                'vertex_upload_bytes': int(up * 0.7), 'index_upload_bytes': int(up * 0.05),
                'texture_upload_bytes': int(up * 0.25), 'texture_upload_ms': round(up / 1048576.0 * 2.1, 2),
                'shader_compiles': 2 if disk_bytes > 0 else 0, 'disk_reads': reads, 'disk_bytes': int(disk_bytes),
                'disk_read_ms': round(reads * 3.2, 2), 'disk_sync_waits': waits,
                'disk_sync_ms': round(waits * 38.0, 2), 'alloc_calls': len(rows) * 410,
                'free_calls': len(rows) * 405, 'alloc_bytes': len(rows) * 96000}

    def threads(self, secs, disk_bytes):
        rng = self.rng
        cd = 40 + min(90.0, disk_bytes / max(secs, 0.1) / 1048576.0 * 25)
        return [{'name': 'main', 'ms_per_s': round(975 + rng.random() * 15, 1)},
                {'name': 'CdStream', 'ms_per_s': round(cd + rng.random() * 8, 1)},
                {'name': 'AudioStream', 'ms_per_s': round(38 + rng.random() * 15, 1)},
                {'name': 'vitaGL', 'ms_per_s': round(12 + rng.random() * 6, 1)},
                {'name': 'LogWriter', 'ms_per_s': round(0.5 + rng.random() * 1.5, 1)}]

    def sections(self, rows, g):
        rng = self.rng
        n = len(rows)
        worst_sim = max(r['sim_ms'] for r in rows)
        out = []

        def add(name, parent, ms, calls=1.0, maxk=None):
            maxk = maxk if maxk is not None else 1.4 + rng.random() * 0.8
            out.append({'name': name, 'parent': parent,
                        'depth': 0 if parent < 0 else out[parent]['depth'] + 1,
                        'ms': ms, 'self_ms': 0.0, 'max_ms': ms * maxk, 'calls_per_frame': calls})
            return len(out) - 1

        sim = g['sim']
        p = add('CGame::Process', -1, sim)
        out[p]['max_ms'] = max(out[p]['max_ms'], worst_sim)
        if sim > 1.0:
            add('CutsceneMgr::Update', p, sim * 0.004)
            hitchy = worst_sim - sim * 2.0
            st = add('Streaming::Update', p, sim * 0.10)
            la = add('Streaming::LoadAll', st, sim * 0.03)
            add('Streaming::Convert', st, sim * 0.045, calls=0.4)
            if hitchy > 50:
                extra = sum(max(0.0, r['sim_ms'] - sim * 2.0) for r in rows) / n
                for i in (p, st, la):
                    out[i]['ms'] += extra
                out[la]['max_ms'] = out[st]['max_ms'] = worst_sim - sim * 0.5
            sc = add('TheScripts::Process', p, sim * 0.06)
            add('Streaming::LoadAll', sc, sim * 0.004, calls=0.02, maxk=40.0)
            add('Population::Update', p, sim * 0.08)
            w = add('World::Process', p, sim * 0.55)
            add('World.Anim', w, sim * 0.12, calls=1.0)
            add('World.Control', w, sim * 0.28)
            add('World.Collision', w, sim * 0.11)
        add('DMAudio.Service', -1, g['audio'])
        add('CnstrRenderList', -1, g['renderlist'] * 0.64)
        add('PreRender', -1, g['renderlist'] * 0.36)
        add('EnvMapBeforeFrame', -1, g['envmap'] * 0.78)
        sof = min(0.25, g['scene'] * 0.03)
        add('StartOfFrame', -1, sof)
        rs = add('RenderScene', -1, g['scene'] - sof)
        for name, share in (('Scene.Water', .06), ('Scene.World0', .36), ('Scene.Reflections', .03),
                            ('Scene.World1Opaque', .33), ('Scene.Roads', .07), ('Scene.EverythingBarRoads', .04),
                            ('Scene.FadingIn', .02), ('Scene.Sky', .02), ('Scene.Boats', .01)):
            add(name, rs, (g['scene'] - sof) * share)
        add('EnvMapRender', -1, g['envmap'] * 0.22)
        fx = add('RenderEffects', -1, g['fx'] * 0.9)
        for name, share in (('FX.EnvMap', .08), ('FX.World2Transparent', .36), ('FX.Vehicles', .34),
                            ('FX.Shadows', .08), ('FX.Coronas', .03), ('FX.Particles', .05)):
            add(name, fx, g['fx'] * 0.9 * share, calls=1.0)
        add('RenderMotionBlur', -1, g['fx'] * 0.1)
        add('Render2dStuff', -1, g['2d'])
        add('EndOfFrame', -1, g['eof'], maxk=1.3 + rng.random())
        for i, s in enumerate(out):
            kids = sum(c['ms'] for c in out if c['parent'] == i)
            s['self_ms'] = max(0.0, s['ms'] - kids)
        for s in out:
            for k in ('ms', 'self_ms', 'max_ms'):
                s[k] = round(s[k], 4)
        return [s for s in out if s['ms'] > 0.0005 or s['depth'] == 0]

    def skip(self, test_id, group, name, kind, scene, reason, variant='', viewpoint=''):
        t = {'id': test_id, 'group': group, 'name': name, 'variant': variant, 'viewpoint': viewpoint,
             'kind': kind, 'skipped': True, 'skip_reason': reason, 'interrupted': False, 'scene': scene,
             'frames': 0, 'first_frame': len(self.rows), 'seconds': 0.0, 'fps': None,
             'frame_ms': None, 'work_ms': None, 'swap_ms': None, 'gpu_ms': None, 'gpu_sync_ms': None,
             'gpu_samples': 0, 'cpu_bound_pct': None, 'gpu_bound_pct': None,
             'groups_ms': {g: 0.0 for g in GROUPS}, 'hitches': 0, 'worst_hitch_ms': 0.0,
             'worst_hitch_group': '', 'per_frame': None, 'totals': None, 'threads': [],
             'cpu_load_pct': None, 'memory': None, 'load_ms': None, 'settle_ms': None, 'sections': []}
        self.tests.append(t)

    # ---- catalogue ------------------------------------------------------------
    def run(self, partial):
        rng = self.rng
        for (tid, group, name, kind, (h, mi, w), (pd, cd), m, n, path) in GAMEPLAY:
            scene = {'hour': h, 'minute': mi, 'weather': w, 'ped_density': pd, 'car_density': cd,
                     'fixed_step': kind != 'streaming', 'paused': False}
            hitches, burst = None, None
            if kind == 'streaming':
                c = int(n * 0.55)
                hitches = {c: 175.0, c + 1: 415.0, c + 3: 255.0, c + 9: 118.0, int(n * 0.2): 96.0}
                burst = lambda i, c=c: 900.0 if c - 6 <= i <= c + 12 else (60.0 if i % 23 == 0 else 0.0)
            elif kind == 'follow':
                burst = lambda i: 120.0 if i % 90 == 0 else 0.0
            rows = self.frames(tid, m, n, path, hitches=hitches, stream_burst=burst,
                               wobble=0.02 if kind == 'static' else 0.10)
            interrupted = tid == 'c3_mayhem' and self.variant == 0
            if interrupted:
                rows[n // 3]['frame_ms'] = 2350.0
                rows[n // 3]['swap_ms'] = 2350.0 - rows[n // 3]['work_ms']
                rows[n // 3]['flags'] |= BF_HITCH
            self.result(tid, group, name, kind, scene, rows, interrupted=interrupted)
            if partial and tid == 'c1_crowd_d1':
                return
        base_tp = GAMEPLAY[0][6]
        for tid, name, pos, load, settle in TELEPORTS:
            load = load * (1 + rng.uniform(-0.08, 0.08))
            settle = settle * (1 + rng.uniform(-0.08, 0.08))
            n = max(20, int(settle / 34.0))
            hitches = {0: 260.0 + rng.random() * 200, 1: 140.0, 3: 120.0 + rng.random() * 60}
            burst = lambda i, n=n: 1400.0 if i < n * 0.4 else 0.0
            rows = self.frames(tid, base_tp, n, [pos], hitches=hitches, stream_burst=burst, wobble=0.04)
            scene = {'hour': 12, 'minute': 0, 'weather': 0, 'ped_density': 1.0, 'car_density': 1.0,
                     'fixed_step': False, 'paused': False}
            self.result(tid, 'streaming', name, 'teleport', scene, rows,
                        load_ms=round(load, 1), settle_ms=round(settle, 1))
        for vp, cam, base in VIEWPOINTS:
            for var, mul, gmod in ISO_VARIANTS:
                tid = 'iso_%s_%s' % (vp, var)
                name = 'Isolation %s: %s' % (vp, var)
                paused = var not in UNPAUSED
                scene = {'hour': 12, 'minute': 0, 'weather': 0, 'ped_density': 1.0, 'car_density': 1.0,
                         'fixed_step': True, 'paused': paused}
                need = CAP_NEEDED.get(var)
                if need and need not in self.caps:
                    self.skip(tid, 'isolation', name, 'static', scene,
                              'capacite absente (%s)' % need, variant=var, viewpoint=vp)
                    continue
                m = dict(base)
                for k, f in mul.items():
                    m[k] *= f
                if gmod:
                    op, val = gmod
                    m['gpu'] = m['gpu'] + val if op == '+' else (m['gpu'] * val if op == 'x' else val)
                if var == 'baseline_end':
                    m['scene'] *= 1.004
                rows = self.frames(tid, m, 90, [cam], paused=paused, gpu_sync=var == 'gpu_sync',
                                   wobble=0.0 if paused else 0.02, noise=0.02)
                self.result(tid, 'isolation', name, 'static', scene, rows, variant=var, viewpoint=vp)


def geomean(vals):
    vals = [v for v in vals if v and v > 0]
    return math.exp(sum(math.log(v) for v in vals) / len(vals)) if vals else None


def scores(tests):
    ok = lambda t: not t['skipped'] and not t['interrupted'] and t['fps']
    gfx = [t['fps'] for t in tests if t['group'] == 'graphics' and ok(t)]
    cpu = [t['fps'] for t in tests if t['group'] == 'cpu' and ok(t)]
    s1 = [t['fps'] for t in tests if t['id'] == 's1_bridge_run' and ok(t)]
    g, c, o = geomean(gfx), geomean(cpu), geomean(gfx + cpu + s1)
    r = lambda v: round(100 * v, 1) if v else None
    return {'graphics': r(g), 'cpu': r(c), 'overall': r(o), 'valid': bool(gfx and cpu and s1)}


def deltas(tests):
    by = {(t['viewpoint'], t['variant']): t for t in tests if t['group'] == 'isolation' and not t['skipped']}
    out = []
    for (vp, var), t in by.items():
        if var == 'baseline':
            continue
        ref = 'sim_running' if var == 'no_audio' else 'baseline'
        r = by.get((vp, ref))
        if not r:
            continue
        bm, vm = r['frame_ms']['mean'], t['frame_ms']['mean']
        g = lambda x: x['gpu_ms']['mean'] if x['gpu_ms'] else None
        out.append({'viewpoint': vp, 'variant': var, 'reference': ref, 'baseline_ms': bm, 'variant_ms': vm,
                    'saved_ms': round(bm - vm, 3), 'saved_pct': round(100 * (bm - vm) / bm, 2),
                    'baseline_gpu_ms': g(r), 'variant_gpu_ms': g(t),
                    'baseline_work_ms': r['work_ms']['mean'], 'variant_work_ms': t['work_ms']['mean']})
    return out


SECTION_ADVICE = {
    'Scene.World0': 'Monde (LOD et bâtiments lointains) : fusionner les modèles, réduire la distance des LOD.',
    'Scene.World1Opaque': 'Géométrie opaque proche : moins de draw calls (batching, culling plus agressif).',
    'FX.World2Transparent': 'Objets transparents : trier et regrouper, réduire le surdessin (arbres, grilles).',
    'FX.Vehicles': 'Véhicules : matériaux et passes de reflets trop nombreux par voiture.',
    'World.Control': 'IA et physique des véhicules et piétons : limiter les mises à jour lointaines.',
    'EndOfFrame': 'Attente du GPU au swap : le GPU est le facteur limitant.',
}
FEATURE_ADVICE = {
    'no_world_opaque': 'Monde opaque : fusionner les modèles, culling plus agressif.',
    'no_world_transparent': 'Transparents : moins de surdessin, tri et regroupement.',
    'no_vehicles': 'Véhicules : matériaux, reflets et nombre de passes par voiture.',
    'no_envmap': 'Reflets : carte d’environnement une image sur deux ou plus petite.',
    'no_peds': 'Piétons : skinning et nombre de draws par piéton.',
}


def targets(tests, dl, rows, pc):
    """Same rules as BenchMetrics::Analyse: sections, isolation features, GPU, streaming."""
    ok = lambda t: not t['skipped'] and not t['interrupted'] and t['frames'] > 0
    gp = [t for t in tests if t['group'] in ('graphics', 'cpu') and ok(t)]
    if not gp:
        return []
    frame = sum(t['frame_ms']['mean'] for t in gp) / len(gp)
    out = []
    acc = {}
    for t in gp:
        for s in t['sections']:
            acc[s['name']] = acc.get(s['name'], 0.0) + s['self_ms'] / len(gp)
    for name, ms in sorted(acc.items(), key=lambda kv: -kv[1])[:6]:
        out.append({'what': name, 'kind': 'section', 'ms': round(ms, 3), 'pct': round(100 * ms / frame, 2),
                    'advice': SECTION_ADVICE.get(name, 'Section coûteuse : la profiler plus finement.')})
    by = {}
    for d in dl:
        by.setdefault(d['variant'], []).append(d)
    for var, ds in by.items():
        saved = sum(d['saved_ms'] for d in ds) / len(ds)
        pct = sum(d['saved_pct'] for d in ds) / len(ds)
        if var == 'sim_running':
            vm = sum(d['variant_ms'] for d in ds) / len(ds)
            if -saved >= 0.25:
                out.append({'what': 'simulation', 'kind': 'cpu', 'ms': round(-saved, 3), 'pct': round(100 * -saved / vm, 2),
                            'advice': 'Simulation (IA, physique, scripts, streaming) : voir World.Control et CGame::Process.'})
            continue
        if var not in FEATURE_ADVICE and not var.startswith('no_'):
            continue
        if var in ('no_render_3d', 'no_audio'):
            continue
        if saved >= 0.25:
            out.append({'what': var, 'kind': 'feature', 'ms': round(saved, 3), 'pct': round(pct, 2),
                        'advice': FEATURE_ADVICE.get(var, 'Fonction coûteuse : réduire sa fréquence ou sa résolution.')})
    gfx = [t for t in gp if t['group'] == 'graphics' and t['gpu_ms']]
    if gfx and sum(t['gpu_bound_pct'] for t in gfx) / len(gfx) > 50:
        ms = sum(t['gpu_ms']['mean'] for t in gfx) / len(gfx)
        fm = sum(t['frame_ms']['mean'] for t in gfx) / len(gfx)
        vp = [d['saved_pct'] for d in dl if d['variant'] == 'viewport_50']
        vpp = sum(vp) / len(vp) if vp else 0.0
        adv = ('GPU limité par le remplissage (viewport 50 %% : -%.0f %%) : moins de surdessin, de transparence et '
               'de passes plein écran.' % vpp) if vpp > 25 else (
               'GPU limité par la géométrie ou les états (viewport 50 %% : -%.0f %%) : moins de draws.' % vpp)
        out.append({'what': 'GPU', 'kind': 'gpu', 'ms': round(ms, 3), 'pct': round(100 * ms / fm, 2), 'advice': adv})
    s1 = [t for t in tests if t['id'] == 's1_bridge_run' and ok(t)]
    if s1 and s1[0]['hitches']:
        t = s1[0]
        hitch = sum(r['frame_ms'] for r in rows if r['test'] == t['id'] and r['flags'] & BF_HITCH)
        out.append({'what': t['id'], 'kind': 'streaming', 'ms': t['worst_hitch_ms'],
                    'pct': round(100 * hitch / (t['seconds'] * 1000), 2),
                    'advice': 'Pic de %.0f ms (groupe %s) : chargement bloquant (LoadAll, changement d’île) ; '
                              'précharger ou rendre asynchrone.' % (t['worst_hitch_ms'], t['worst_hitch_group'] or '?')})
    tps = [t for t in tests if t['kind'] == 'teleport' and not t['skipped'] and t['load_ms']]
    if tps:
        w = max(tps, key=lambda t: t['load_ms'])
        out.append({'what': 'teleportation', 'kind': 'streaming', 'ms': w['load_ms'], 'pct': 0,
                    'advice': 'LoadScene jusqu’à %.0f ms (%s) : moins de lectures disque, conversions hors du thread '
                              'principal.' % (w['load_ms'], w['id'])})
    out.sort(key=lambda t: -t['ms'])
    return out[:16]


def main():
    ap = argparse.ArgumentParser(description='Synthetic reLCS Benchmark results folder')
    ap.add_argument('out')
    ap.add_argument('--variant', type=int, default=0)
    ap.add_argument('--profile', choices=('vita', 'pc'), default='vita')
    ap.add_argument('--partial', action='store_true')
    a = ap.parse_args()
    gen = Gen(a.variant, a.profile)
    gen.run(a.partial)
    pc = a.profile == 'pc'
    stamp = '2026-09-30_14-%02d-00' % (5 + a.variant)
    if pc:
        device = {'platform': 'Windows', 'model': 'AMD Ryzen 7 5800X 8-Core', 'firmware': '10.0.26200',
                  'build': 'pc-2026-09-30', 'cpu_mhz': 0, 'bus_mhz': 0, 'gpu_mhz': 0, 'xbar_mhz': 0,
                  'gpu': 'Direct3D 9', 'screen': [1920, 1080], 'notes': ''}
        boot = {'engine_ms': 1450.0, 'game_ms': 3900.0, 'ready_ms': 5200.0}
    else:
        device = {'platform': 'PS Vita', 'model': 'PCH-2000 (0x0000)', 'firmware': '3.65',
                  'build': '01.15-%s-bench' % ('abc1234' if a.variant == 0 else 'def5678'),
                  'cpu_mhz': 444, 'bus_mhz': 222, 'gpu_mhz': 222, 'xbar_mhz': 166,
                  'gpu': 'SGX543MP4+ (vitaGL)', 'screen': [960, 544],
                  'notes': 'charge CPU: sceKernelGetCpuUsage; fence GPU: sceGxmNotification'}
        boot = {'engine_ms': 4210.0, 'game_ms': 11830.0, 'ready_ms': 16540.0}
    dl = [] if a.partial else deltas(gen.tests)
    sc = scores(gen.tests)
    if a.partial:
        sc['valid'] = False
    res = {'schema': 'relcs-bench/1', 'partial': a.partial, 'aborted': False, 'timestamp': stamp,
           'device': device, 'caps': gen.caps,
           'settings': {'Mode': 'full', 'Tests': '', 'Loops': '1', 'AutoExit': '0', 'Overlay': '0',
                        'FixedStep': '1', 'Seed': '1234'},
           'boot': boot, 'scores': sc, 'tests': gen.tests, 'deltas': dl,
           'targets': [] if a.partial else targets(gen.tests, dl, gen.rows, pc)}
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'results.json').write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding='utf-8')
    if not a.partial:
        with open(out / 'frames.csv', 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(CSV_HEADER)
            for r in gen.rows:
                vals = []
                for k in CSV_HEADER:
                    v = r[k]
                    vals.append('%.3f' % v if isinstance(v, float) else str(v))
                w.writerow(vals)
    print('bench_sample: %d tests, %d frames -> %s' % (len(gen.tests), len(gen.rows), out))


if __name__ == '__main__':
    main()
