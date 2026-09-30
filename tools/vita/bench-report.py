#!/usr/bin/env python3
"""HTML report of a reLCS Benchmark run (schema "relcs-bench/1", docs/BENCHMARK.md).

  python tools/vita/bench-report.py RESULTS [-o report.html]    HTML report
  python tools/vita/bench-report.py RESULTS --text              French console summary
  python tools/vita/bench-report.py --compare OLD NEW [-o F]    before/after (HTML + text)

RESULTS is a result folder (ux0:data/reLCS/benchmark/<date>/ copied to the PC,
or benchmark/<date>/ next to the Windows exe) or its results.json. frames.csv
next to it is optional: without it there are no frame-time charts nor hitch
lists. Partial runs ("partial": true), skipped tests, zero-frame tests and PC
runs without GPU timing or profiler sections are handled.

The HTML is one self-contained file: inline CSS, inline SVG, a few lines of
inline JavaScript for chart tooltips, no external resource. With --text alone
no HTML is written unless -o is given. Standard library only (Python 3.10+).
"""
import argparse
import csv
import html
import json
import math
import re
import sys
from pathlib import Path

SCHEMA = 'relcs-bench/1'
GROUPS = ['sim', 'audio', 'renderlist', 'envmap', 'scene', 'fx', '2d', 'eof']
GROUP_LABEL = {'sim': 'simulation', 'audio': 'audio', 'renderlist': 'liste de rendu', 'envmap': 'envmap',
               'scene': 'scène', 'fx': 'effets', '2d': '2D / HUD', 'eof': 'fin d’image'}
GROUP_HINT = {'sim': 'CGame::Process', 'audio': 'DMAudio.Service', 'renderlist': 'CnstrRenderList + PreRender',
              'envmap': 'EnvMapBeforeFrame + EnvMapRender', 'scene': 'StartOfFrame + RenderScene',
              'fx': 'RenderEffects + RenderMotionBlur', '2d': 'Render2dStuff, menus, fondu',
              'eof': 'EndOfFrame, swap compris'}
TEST_GROUPS = ['graphics', 'cpu', 'streaming', 'isolation', 'boot']
TEST_GROUP_LABEL = {'graphics': 'Graphismes', 'cpu': 'Processeur', 'streaming': 'Streaming',
                    'isolation': 'Isolation', 'boot': 'Démarrage'}
KIND_LABEL = {'flythrough': 'survol', 'static': 'fixe', 'follow': 'poursuite', 'stress': 'stress',
              'streaming': 'streaming', 'teleport': 'téléportation'}
WEATHER = {0: 'soleil', 1: 'nuageux', 2: 'pluie', 3: 'brouillard', 4: 'grand soleil', 5: 'tempête'}
CAPS = [('gpu_fence', 'fence GPU'), ('gpu_sync', 'synchro GPU'), ('viewport', 'viewport'),
        ('draw_mode', 'modes de draw'), ('flat_tex', 'textures unies'), ('sections', 'sections'),
        ('gl_counters', 'compteurs GL'), ('disk', 'disque'), ('threads', 'threads'), ('cpu_load', 'charge CPU')]
VARIANT_LABEL = {
    'baseline': 'Référence', 'no_peds': 'Sans piétons', 'no_vehicles': 'Sans véhicules',
    'no_objects': 'Sans objets', 'no_world_lod': 'Sans LOD du monde', 'no_world_opaque': 'Sans monde opaque',
    'no_world_transparent': 'Sans monde transparent', 'no_water': 'Sans eau', 'no_sky': 'Sans ciel',
    'no_shadows': 'Sans ombres', 'no_coronas': 'Sans halos (coronas)', 'no_particles_fx': 'Sans particules ni effets',
    'no_envmap': 'Sans reflets (envmap)', 'no_postfx': 'Sans post-traitement', 'hud_on': 'Avec HUD',
    'ps2_alpha_off': 'Alpha PS2 désactivé', 'backface_cull': 'Élimination des faces arrière',
    'lod_low': 'LOD bas (0,925)', 'lod_high': 'LOD haut (1,8)', 'farclip_300': 'Distance max 300 m',
    'viewport_75': 'Viewport 75 %', 'viewport_50': 'Viewport 50 %', 'viewport_25': 'Viewport 25 %',
    'viewport_min': 'Viewport minimal', 'flat_textures': 'Textures unies', 'draw_tiny': 'Draw calls minuscules',
    'draw_null': 'Draw calls nuls', 'gpu_sync': 'Synchro GPU', 'no_render_3d': 'Sans rendu 3D',
    'baseline_end': 'Référence (fin)', 'sim_running': 'Simulation active', 'no_audio': 'Sans audio'}
DIAG_VARIANTS = {'viewport_75', 'viewport_50', 'viewport_25', 'viewport_min', 'flat_textures', 'draw_tiny',
                 'draw_null', 'gpu_sync', 'no_render_3d', 'baseline_end', 'sim_running', 'no_audio'}
SCALE_EXCLUDED = {'no_render_3d', 'draw_null', 'gpu_sync', 'draw_tiny'}
VIEWPOINT_LABEL = {'a_chinatown': 'A · Chinatown', 'b_bedford': 'B · Bedford Point'}
TARGET_WHAT = {'simulation': 'Simulation', 'teleportation': 'Téléportation (LoadScene)',
               'GPU (attente swap)': 'GPU (attente au swap)'}
TARGET_KIND = {'section': 'section', 'feature': 'fonction', 'gpu': 'GPU', 'streaming': 'streaming', 'cpu': 'CPU'}
STAT_KEYS = ['mean', 'median', 'p90', 'p95', 'p99', 'min', 'max', 'low1_fps']
BF_GPU_SYNC, BF_PAUSED, BF_HITCH = 4, 8, 16
NOISE_PCT = 2.0
MAX_POINTS = 1500
NNBSP = ' '
MINUS = '−'
DASH = '–'


class ReportError(Exception):
    pass


# ---- values ------------------------------------------------------------------

def num(v):
    if isinstance(v, bool) or v is None:
        return None
    if isinstance(v, (int, float)):
        f = float(v)
        return f if math.isfinite(f) else None
    if isinstance(v, str):
        try:
            f = float(v.strip())
        except ValueError:
            return None
        return f if math.isfinite(f) else None
    return None


def pos(v):
    f = num(v)
    return f if f is not None and f > 0 else None


def obj(v):
    return v if isinstance(v, dict) else {}


def arr(v):
    return v if isinstance(v, list) else []


def txt(v):
    return '' if v is None else str(v)


def esc(v):
    return html.escape(txt(v), quote=True)


def fmt(v, dec=1):
    f = num(v)
    if f is None:
        return DASH
    s = f'{f:,.{dec}f}'
    if s.startswith('-') and not any(c in '123456789' for c in s):
        s = s[1:]
    return s.replace(',', ' ').replace('.', ',').replace(' ', NNBSP).replace('-', MINUS)


def fmt_signed(v, dec=1):
    f = num(v)
    if f is None:
        return DASH
    s = fmt(abs(f), dec)
    if not any(c in '123456789' for c in s):
        return '±' + s
    return ('+' if f > 0 else MINUS) + s


def fmt_pct(v, dec=0, signed=False):
    f = num(v)
    if f is None:
        return DASH
    return (fmt_signed(f, dec) if signed else fmt(f, dec)) + NNBSP + '%'


def fmt_int(v):
    return fmt(v, 0)


def fmt_big(v):
    f = num(v)
    if f is None:
        return DASH
    if abs(f) >= 1e6:
        return fmt(f / 1e6, 2) + NNBSP + 'M'
    if abs(f) >= 1e4:
        return fmt(f / 1e3, 1) + NNBSP + 'k'
    return fmt(f, 0)


def plain(s):
    s = s.replace(NNBSP, ' ').replace(MINUS, '-').replace('’', "'")
    return '\n'.join(line.rstrip() for line in s.split('\n'))


def mean_of(vals):
    vals = [v for v in vals if v is not None]
    return sum(vals) / len(vals) if vals else None


def percentile(sorted_vals, p):
    n = len(sorted_vals)
    return sorted_vals[max(0, min(n - 1, math.ceil(p / 100.0 * n) - 1))]


def stats_from(vals):
    vals = [v for v in vals if v is not None]
    if not vals:
        return {k: None for k in STAT_KEYS}
    s = sorted(vals)
    n = len(s)
    worst = s[-max(1, n // 100):]
    wm = sum(worst) / len(worst)
    return {'mean': sum(s) / n, 'median': percentile(s, 50), 'p90': percentile(s, 90), 'p95': percentile(s, 95),
            'p99': percentile(s, 99), 'min': s[0], 'max': s[-1], 'low1_fps': 1000.0 / wm if wm > 0 else None}


def statsd(v):
    d = obj(v)
    return {k: num(d.get(k)) for k in STAT_KEYS}


def geomean(vals):
    vals = [v for v in vals if v is not None and v > 0]
    return math.exp(sum(math.log(v) for v in vals) / len(vals)) if vals else None


def nice_ceil(v):
    if v is None or v <= 0:
        return 1.0
    e = 10 ** math.floor(math.log10(v))
    for m in (1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10):
        if m * e >= v - 1e-9:
            return m * e
    return 10 * e


def nice_step(span, target=4):
    if span <= 0:
        return 1.0
    raw = span / target
    e = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if m * e >= raw - 1e-12:
            return m * e
    return 10 * e


def ticks(vmax, target=4):
    step = nice_step(vmax, target)
    out, v = [], 0.0
    while v <= vmax + 1e-9:
        out.append(v)
        v += step
    return out, step


def tick_label(v, step):
    for dec in (0, 1, 2):
        if abs(step * 10 ** dec - round(step * 10 ** dec)) < 1e-6:
            return fmt(v, dec)
    return fmt(v, 3)


def pct_of(v, scale):
    if v is None or not scale:
        return 0.0
    return max(0.0, min(100.0, 100.0 * v / scale))


# ---- loading -----------------------------------------------------------------

def read_frames(path):
    out = {}
    with open(path, newline='', encoding='utf-8-sig', errors='replace') as f:
        rd = csv.reader(f)
        header = next(rd, None)
        if not header:
            return out
        header = [h.strip() for h in header]
        if 'test' not in header:
            return out
        ti = header.index('test')
        cols = [(i, h) for i, h in enumerate(header) if i != ti]
        for row in rd:
            if len(row) <= ti:
                continue
            rec = {}
            for i, h in cols:
                rec[h] = num(row[i]) if i < len(row) else None
            out.setdefault(row[ti].strip(), []).append(rec)
    # the same id twice (Loops > 1): one segment per run, split where the frame index restarts
    segs = {}
    for tid, rows in out.items():
        cur, prev = [], None
        segs[tid] = [cur]
        for r in rows:
            f = r.get('frame')
            if f is not None and prev is not None and f <= prev and cur:
                cur = []
                segs[tid].append(cur)
            cur.append(r)
            prev = f if f is not None else prev
    return segs


def safe_id(s):
    return ''.join(c if c.isascii() and (c.isalnum() or c in '-_') else '_' for c in s)


def row_gpu(r):
    return pos(r.get('gpu_ms'))


class Test:
    def __init__(self, d, frames):
        self.id = txt(d.get('id')) or '?'
        self.group = txt(d.get('group')) or 'autre'
        self.name = txt(d.get('name')) or self.id
        self.variant = txt(d.get('variant'))
        self.viewpoint = txt(d.get('viewpoint'))
        self.kind = txt(d.get('kind'))
        self.skipped = bool(d.get('skipped'))
        self.skip_reason = txt(d.get('skip_reason'))
        self.interrupted = bool(d.get('interrupted'))
        self.scene = obj(d.get('scene'))
        seg = frames.get(self.id, [])
        self.rows = seg.pop(0) if seg else []
        n = num(d.get('frames'))
        self.frames = int(n) if n is not None and n > 0 else len(self.rows)
        self.seconds = num(d.get('seconds'))
        self.frame = statsd(d.get('frame_ms'))
        self.work = statsd(d.get('work_ms'))
        self.swap = statsd(d.get('swap_ms'))
        self.gpu = statsd(d.get('gpu_ms'))
        self.gpu_sync = statsd(d.get('gpu_sync_ms'))
        if self.rows:
            for attr, key in (('frame', 'frame_ms'), ('work', 'work_ms'), ('swap', 'swap_ms')):
                if getattr(self, attr)['mean'] is None:
                    setattr(self, attr, stats_from([r.get(key) for r in self.rows]))
            if self.gpu['mean'] is None:
                self.gpu = stats_from([row_gpu(r) for r in self.rows])
        self.fps = pos(d.get('fps'))
        if self.fps is None and pos(self.frame['mean']):
            self.fps = 1000.0 / self.frame['mean']
        self.gpu_samples = num(d.get('gpu_samples'))
        self.cpu_bound = num(d.get('cpu_bound_pct'))
        self.gpu_bound = num(d.get('gpu_bound_pct'))
        gm = obj(d.get('groups_ms'))
        self.groups = {g: num(gm.get(g)) for g in GROUPS}
        if self.rows and all(v is None for v in self.groups.values()):
            self.groups = {g: mean_of([r.get(g + '_ms') for r in self.rows]) for g in GROUPS}
        h = num(d.get('hitches'))
        self.hitches = int(h) if h is not None else len(self.hitch_rows())
        self.worst_hitch = num(d.get('worst_hitch_ms'))
        self.worst_hitch_group = txt(d.get('worst_hitch_group'))
        self.per_frame = obj(d.get('per_frame'))
        self.totals = obj(d.get('totals'))
        self.threads = [x for x in arr(d.get('threads')) if isinstance(x, dict)]
        self.cpu_load = [num(x) for x in arr(d.get('cpu_load_pct'))]
        self.memory = obj(d.get('memory'))
        self.load_ms = num(d.get('load_ms'))
        self.settle_ms = num(d.get('settle_ms'))
        self.sections = [x for x in arr(d.get('sections')) if isinstance(x, dict)]

    @property
    def ok(self):
        return not self.skipped and self.frames > 0 and self.frame['mean'] is not None

    @property
    def scored(self):
        return self.ok and not self.interrupted and self.fps is not None

    @property
    def gameplay(self):
        return self.group in ('graphics', 'cpu', 'streaming') and self.kind != 'teleport'

    def pf(self, key):
        v = num(self.per_frame.get(key))
        if v is None and self.rows:
            v = mean_of([r.get(key) for r in self.rows])
        return v

    def hitch_rows(self):
        if not self.rows:
            return []
        flagged = [r for r in self.rows if r.get('flags') is not None and int(r['flags']) & BF_HITCH]
        if flagged or any(r.get('flags') is not None for r in self.rows):
            return flagged
        fr = sorted(r['frame_ms'] for r in self.rows if r.get('frame_ms') is not None)
        if not fr:
            return []
        lim = max(100.0, 3 * percentile(fr, 50))
        return [r for r in self.rows if (r.get('frame_ms') or 0) > lim]

    def label(self):
        if self.group == 'isolation' and self.variant:
            return '%s · %s' % (VIEWPOINT_LABEL.get(self.viewpoint, self.viewpoint),
                                VARIANT_LABEL.get(self.variant, self.variant))
        return self.name


class Run:
    def __init__(self, path):
        p = Path(path)
        jp = p / 'results.json' if p.is_dir() else p
        if not jp.is_file():
            raise ReportError('results.json introuvable : %s' % jp)
        try:
            data = json.loads(jp.read_text(encoding='utf-8-sig'))
        except (OSError, UnicodeDecodeError, ValueError) as e:
            raise ReportError('lecture impossible de %s : %s' % (jp, e))
        if not isinstance(data, dict):
            raise ReportError('%s : objet JSON attendu' % jp)
        self.path = jp
        self.folder = jp.parent
        self.data = data
        self.schema = txt(data.get('schema'))
        self.partial = bool(data.get('partial'))
        self.aborted = bool(data.get('aborted'))
        self.timestamp = txt(data.get('timestamp'))
        self.device = obj(data.get('device'))
        self.caps = [txt(c) for c in arr(data.get('caps'))]
        st = data.get('settings')
        if isinstance(st, str):
            st = dict(kv.split('=', 1) for kv in st.split(';') if '=' in kv)
        self.settings = {txt(k): txt(v) for k, v in obj(st).items()}
        self.boot = obj(data.get('boot'))
        self.csv_path = self.folder / 'frames.csv'
        self.frames, self.csv_note = {}, ''
        if self.csv_path.is_file():
            try:
                self.frames = read_frames(self.csv_path)
            except (OSError, csv.Error) as e:
                self.csv_note = 'frames.csv illisible (%s)' % e
        else:
            self.csv_note = 'frames.csv absent'
        pool = {k: list(v) for k, v in self.frames.items()}
        self.tests = [Test(t, pool) for t in arr(data.get('tests')) if isinstance(t, dict)]
        self.by_id = {t.id: t for t in self.tests}
        # HTML anchors: frame-time chart of a test, isolation card of a viewpoint
        used, self.anchors, self.vp_anchor = set(), {}, {}

        def unique(a):
            while a in used:
                a += '_'
            used.add(a)
            return a
        for t in self.tests:
            if t.group == 'isolation':
                if t.viewpoint not in self.vp_anchor:
                    self.vp_anchor[t.viewpoint] = unique(safe_id('iso-' + t.viewpoint))
            elif t.ok and t.rows and (t.gameplay or t.kind == 'teleport'):
                self.anchors[id(t)] = unique(safe_id('t-' + t.id))
        self.deltas = [self._delta(d) for d in arr(data.get('deltas')) if isinstance(d, dict)]
        self.deltas_computed = False
        if not self.deltas and any(t.group == 'isolation' for t in self.tests):
            self.deltas = self._compute_deltas()
            self.deltas_computed = bool(self.deltas)
        self.targets = [t for t in arr(data.get('targets')) if isinstance(t, dict)]
        self.targets_computed = False
        if not self.targets:
            self.targets = self._compute_targets()
            self.targets_computed = bool(self.targets)
        sc = obj(data.get('scores'))
        self.scores = {k: num(sc.get(k)) for k in ('graphics', 'cpu', 'overall')}
        self.scores_valid = sc.get('valid') if isinstance(sc.get('valid'), bool) else None
        self.scores_computed = False
        if all(v is None for v in self.scores.values()) and self.tests:
            calc = self.computed_scores()
            if any(v is not None for v in calc.values()):
                self.scores = calc
                self.scores_computed = True

    @staticmethod
    def _delta(d):
        out = {k: num(d.get(k)) for k in ('baseline_ms', 'variant_ms', 'saved_ms', 'saved_pct', 'baseline_gpu_ms',
                                            'variant_gpu_ms', 'baseline_work_ms', 'variant_work_ms')}
        for k in ('baseline_gpu_ms', 'variant_gpu_ms'):
            if out[k] is not None and out[k] < 0:
                out[k] = None
        if out['saved_ms'] is None and out['baseline_ms'] is not None and out['variant_ms'] is not None:
            out['saved_ms'] = out['baseline_ms'] - out['variant_ms']
        if out['saved_pct'] is None and out['saved_ms'] is not None and pos(out['baseline_ms']):
            out['saved_pct'] = 100.0 * out['saved_ms'] / out['baseline_ms']
        out['viewpoint'] = txt(d.get('viewpoint'))
        out['variant'] = txt(d.get('variant'))
        out['reference'] = txt(d.get('reference')) or ('sim_running' if out['variant'] == 'no_audio' else 'baseline')
        return out

    def _compute_deltas(self):
        iso = {(t.viewpoint, t.variant): t for t in self.tests if t.group == 'isolation' and t.ok}
        out = []
        for (vp, var), t in iso.items():
            if var == 'baseline':
                continue
            ref = 'sim_running' if var == 'no_audio' else 'baseline'
            r = iso.get((vp, ref))
            if r is None:
                continue
            out.append(self._delta({'viewpoint': vp, 'variant': var, 'reference': ref,
                                    'baseline_ms': r.frame['mean'], 'variant_ms': t.frame['mean'],
                                    'baseline_gpu_ms': r.gpu['mean'], 'variant_gpu_ms': t.gpu['mean'],
                                    'baseline_work_ms': r.work['mean'], 'variant_work_ms': t.work['mean']}))
        return out

    def _compute_targets(self):
        gp = [t for t in self.tests if t.gameplay and t.scored and t.group != 'streaming']
        acc = {}
        for t in gp:
            for s in t.sections:
                v = num(s.get('self_ms'))
                if v is not None:
                    acc[txt(s.get('name'))] = acc.get(txt(s.get('name')), 0.0) + v / len(gp)
        frame = mean_of([t.frame['mean'] for t in gp])
        out = []
        for name, ms in sorted(acc.items(), key=lambda kv: -kv[1])[:8]:
            out.append({'what': name, 'kind': 'section', 'ms': ms,
                        'pct': 100.0 * ms / frame if frame else None, 'advice': ''})
        return out

    def computed_scores(self):
        ok = [t for t in self.tests if t.scored]
        g = geomean([t.fps for t in ok if t.group == 'graphics'])
        c = geomean([t.fps for t in ok if t.group == 'cpu'])
        o = geomean([t.fps for t in ok if t.group in ('graphics', 'cpu') or t.id == 's1_bridge_run'])
        return {'graphics': 100 * g if g else None, 'cpu': 100 * c if c else None, 'overall': 100 * o if o else None}

    def group_fps(self, groups, extra=()):
        vals = [t.fps for t in self.tests if t.scored and (t.group in groups or t.id in extra)]
        return geomean(vals), len(vals)

    def ordered_groups(self):
        seen = [g for g in TEST_GROUPS if any(t.group == g for t in self.tests)]
        return seen + sorted({t.group for t in self.tests} - set(seen))

    def date_label(self):
        s = self.timestamp
        try:
            d, t = s.split('_', 1)
            y, m, dd = d.split('-')
            return '%s/%s/%s à %s' % (dd, m, y, t.replace('-', ':'))
        except ValueError:
            return s or DASH

    def mode_label(self):
        low = {k.lower(): v for k, v in self.settings.items()}
        tests = low.get('tests', '').strip()
        mode = low.get('mode', '').strip()
        if tests:
            return 'tests : ' + tests
        return {'full': 'complet', 'quick': 'rapide'}.get(mode, mode or DASH)


# ---- HTML pieces ---------------------------------------------------------------

CSS = r'''
:root{color-scheme:light dark;
--page:#f9f9f7;--surface:#fcfcfb;--surface2:#f1f0ec;--ink:#0b0b0b;--ink2:#52514e;--muted:#898781;
--grid:#e1e0d9;--axis:#c3c2b7;--ring:rgba(11,11,11,.10);
--s1:#2a78d6;--s2:#eb6834;--s3:#1baf7a;--s4:#eda100;--s5:#e87ba4;--s6:#008300;--s7:#4a3aa7;--s8:#e34948;
--good:#0ca30c;--good-ink:#006300;--warn:#fab219;--crit:#d03b3b;--crit-ink:#c02f2f;--other:#d4d3cc;
--cpu:var(--s2);--gpu:var(--s1);--frame:#52514e;--pos:var(--s1);--neg:var(--s8)}
@media (prefers-color-scheme:dark){:root{
--page:#0d0d0d;--surface:#1a1a19;--surface2:#242423;--ink:#ffffff;--ink2:#c3c2b7;--muted:#898781;
--grid:#2c2c2a;--axis:#383835;--ring:rgba(255,255,255,.10);
--s1:#3987e5;--s2:#d95926;--s3:#199e70;--s4:#c98500;--s5:#d55181;--s6:#008300;--s7:#9085e9;--s8:#e66767;
--good-ink:#0ca30c;--crit-ink:#e66767;--other:#444441;--frame:#c3c2b7}}
.g-sim{--c:var(--s1)}.g-audio{--c:var(--s2)}.g-renderlist{--c:var(--s3)}.g-envmap{--c:var(--s4)}
.g-scene{--c:var(--s5)}.g-fx{--c:var(--s6)}.g-2d{--c:var(--s7)}.g-eof{--c:var(--s8)}.g-other{--c:var(--other)}
.tg-graphics{--c:var(--s1)}.tg-cpu{--c:var(--s2)}.tg-streaming{--c:var(--s3)}.tg-isolation{--c:var(--s7)}
.tg-boot,.tg-autre{--c:var(--muted)}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--page);color:var(--ink);font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:1200px;margin:0 auto;padding:28px 16px 72px}
h1{font-size:24px;line-height:1.2;margin:0;font-weight:650;letter-spacing:-.01em}
h2{font-size:18px;margin:44px 0 6px;font-weight:650;scroll-margin-top:12px}
h3{font-size:14px;margin:22px 0 8px;font-weight:620}
p{margin:6px 0}
.lead{color:var(--ink2);margin:0 0 14px;max-width:80ch}
.muted{color:var(--muted)}.sec{color:var(--ink2)}
small,.small{font-size:12px}
code,.mono{font-family:ui-monospace,SFMono-Regular,Consolas,Menlo,monospace;font-size:12px}
a{color:var(--s1)}
.card{background:var(--surface);border:1px solid var(--ring);border-radius:12px;padding:16px 18px}
.grid2{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,520px),1fr));gap:14px}
.head{display:flex;flex-wrap:wrap;gap:8px 28px;align-items:flex-end;justify-content:space-between;margin-bottom:14px}
.kv{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,210px),1fr));gap:10px 22px;margin:14px 0 0}
.kv div{min-width:0}.kv dt{font-size:12px;color:var(--muted);margin:0}.kv dd{margin:0;overflow-wrap:anywhere}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}
.chip{display:inline-flex;align-items:center;gap:5px;font-size:12px;padding:1px 9px;border-radius:999px;border:1px solid var(--ring);background:var(--surface2);color:var(--ink2);white-space:nowrap}
.chip.off{background:transparent;color:var(--muted);text-decoration:line-through}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--c);flex:none}
.sw{display:inline-block;width:12px;height:10px;border-radius:2px;background:var(--c);flex:none}
.lk{display:inline-block;width:16px;height:2px;border-radius:1px;background:var(--c);flex:none;vertical-align:middle}
nav.toc{display:flex;flex-wrap:wrap;gap:4px 16px;margin:18px 0 0;font-size:13px}
nav.toc a{color:var(--ink2);text-decoration:none;border-bottom:1px solid var(--grid)}
nav.toc a:hover{color:var(--ink);border-color:var(--ink2)}
.banner{border-radius:10px;padding:10px 14px;margin:12px 0 0;border:1px solid var(--ring);border-left:4px solid var(--c);background:var(--surface)}
.banner.crit{--c:var(--crit)}.banner.warn{--c:var(--warn)}.banner.info{--c:var(--muted)}
.banner b{font-weight:650}
.scores{display:grid;grid-template-columns:1.35fr 1fr 1fr;gap:14px}
@media (max-width:760px){.scores{grid-template-columns:1fr}}
.score .lab{display:flex;align-items:center;gap:7px;font-size:13px;color:var(--ink2);font-weight:560}
.score .val{font-size:34px;font-weight:650;line-height:1.15;margin:6px 0 2px;letter-spacing:-.02em}
.score.hero .val{font-size:52px}
.score .sub{font-size:12px;color:var(--muted)}
.tw{overflow-x:auto;-webkit-overflow-scrolling:touch;border:1px solid var(--ring);border-radius:12px;background:var(--surface)}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{padding:6px 8px;border-bottom:1px solid var(--grid);text-align:left;white-space:nowrap;vertical-align:middle}
thead th{font-size:12px;font-weight:600;color:var(--ink2);background:var(--surface);position:sticky;top:0}
tbody tr:last-child td{border-bottom:0}
tbody tr:hover td{background:var(--surface2)}
td.n,th.n{text-align:right}
td.w{white-space:normal;min-width:220px}
td.kc{white-space:normal;min-width:150px}td.kc .why{max-width:none}
th.first,td.first{position:sticky;left:0;background:var(--surface);z-index:1}
tbody tr:hover td.first{background:var(--surface2)}
tr.grp th{background:var(--surface2);font-size:13px;color:var(--ink);padding-top:9px;padding-bottom:9px}
tr.grp th span.g{display:inline-flex;align-items:center;gap:7px}
.tid{display:block;font-size:11px;color:var(--muted);font-family:ui-monospace,SFMono-Regular,Consolas,Menlo,monospace}
.st{display:inline-flex;align-items:center;gap:4px;font-size:12px;padding:0 8px;border-radius:999px;border:1px solid var(--ring);white-space:nowrap}
.st.ok{color:var(--muted);border-color:transparent;padding:0}
.st.skip{color:var(--ink2);background:var(--surface2)}
.st.warn{color:var(--ink);background:rgba(250,178,25,.20);border-color:rgba(250,178,25,.55)}
.why{display:block;font-size:11px;color:var(--muted);white-space:normal;max-width:260px}
.nw,.why.nw{white-space:nowrap}
.split{display:inline-flex;width:54px;height:6px;border-radius:3px;overflow:hidden;background:var(--grid);vertical-align:middle;margin-right:6px}
.split i{display:block;height:100%}
.better{color:var(--good-ink);font-weight:600}.worse{color:var(--crit-ink);font-weight:600}.same{color:var(--muted)}
/* ranked targets */
.targets{list-style:none;margin:0;padding:0}
.targets li{display:grid;grid-template-columns:30px minmax(150px,1.1fr) minmax(140px,2fr) 120px;gap:2px 14px;align-items:center;padding:8px 0;border-bottom:1px solid var(--grid)}
.targets li:last-child{border-bottom:0}
.targets .rk{font-size:18px;font-weight:650;color:var(--muted);text-align:right}
.targets .what{font-weight:600;overflow-wrap:anywhere}
.targets .adv{grid-column:2 / -1;color:var(--ink2);font-size:13px}
.targets .v{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
.targets .v b{font-size:15px}
@media (max-width:640px){.targets li{grid-template-columns:26px 1fr 96px}.targets .bar{grid-column:2 / -1;grid-row:2}.targets .adv{grid-row:3}}
.bar{position:relative;height:12px;background:var(--surface2);border-radius:0 4px 4px 0}
.bar i{position:absolute;left:0;top:0;bottom:0;background:var(--s1);border-radius:0 4px 4px 0}
.bar i.over{background:repeating-linear-gradient(135deg,var(--s1) 0 6px,transparent 6px 9px)}
/* budget */
.budget{display:grid;grid-template-columns:minmax(120px,210px) 1fr 64px;gap:7px 12px;align-items:center}
.budget .bl{font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.budget .bv{text-align:right;font-variant-numeric:tabular-nums;font-size:13px}
.btrack{position:relative;height:20px}
.bstack{display:flex;height:100%;overflow:hidden;border-radius:0 4px 4px 0}
.bstack span{display:block;height:100%;background:var(--c);border-right:2px solid var(--surface);flex:none}
.bstack span:last-child{border-right:0}
.bstack span:hover{filter:brightness(1.12)}
.ref{position:absolute;top:-3px;bottom:-3px;width:0;border-left:1px solid var(--ink2);opacity:.55;pointer-events:none}
.baxis{position:relative;height:18px;font-size:11px;color:var(--muted)}
.baxis span{position:absolute;transform:translateX(-50%);white-space:nowrap}
.legend{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--ink2);margin:0 0 12px}
.legend span{display:inline-flex;align-items:center;gap:6px}
@media (max-width:640px){.budget{grid-template-columns:1fr 56px}.budget .bl{grid-column:1 / -1;margin-bottom:-5px}.budget .bspace{display:none}}
/* line charts */
.lc{display:grid;grid-template-columns:40px 1fr;grid-template-rows:170px 20px}
.lc .ya{position:relative;font-size:11px;color:var(--muted)}
.lc .ya span{position:absolute;right:6px;transform:translateY(50%);font-variant-numeric:tabular-nums}
.lc .xa{grid-column:2;position:relative;font-size:11px;color:var(--muted)}
.lc .xa span{position:absolute;top:3px;transform:translateX(-50%);font-variant-numeric:tabular-nums;white-space:nowrap}
.plot{position:relative;border-left:1px solid var(--axis);border-bottom:1px solid var(--axis);outline:none;touch-action:pan-y}
.plot:focus-visible{box-shadow:0 0 0 2px var(--s1)}
.plot svg{position:absolute;inset:0;width:100%;height:100%;overflow:hidden;display:block}
.plot .gl{stroke:var(--grid);stroke-width:1}
.plot .rl{stroke:var(--ink2);stroke-width:1;opacity:.5}
.plot .hl{stroke:var(--crit);stroke-width:1;opacity:.45}
.plot polyline{fill:none;stroke-width:1.5;stroke-linejoin:round;stroke-linecap:round}
.plot polyline.main{stroke-width:1.25}
.plot .area{fill:var(--frame);opacity:.07;stroke:none}
.plot .rlab{position:absolute;right:4px;transform:translateY(-100%);font-size:10px;color:var(--muted);background:var(--surface);padding:0 3px;border-radius:3px;pointer-events:none}
.cross{position:absolute;top:0;bottom:0;width:0;border-left:1px solid var(--ink2);display:none;pointer-events:none}
.tip{position:absolute;top:6px;display:none;background:var(--surface);border:1px solid var(--ring);box-shadow:0 4px 14px rgba(0,0,0,.12);border-radius:8px;padding:6px 9px;font-size:12px;pointer-events:none;z-index:3;white-space:nowrap}
.tip .th{color:var(--muted);margin-bottom:2px}
.tip .tr{display:flex;align-items:center;gap:6px}
.tip .tr i{display:inline-block;width:12px;height:2px;border-radius:1px}
.tip .tr b{font-variant-numeric:tabular-nums}
.tip .tr span{color:var(--ink2)}
.chart-card h3{margin:0 0 2px;display:flex;align-items:baseline;gap:8px;flex-wrap:wrap}
.chart-card .stats{font-size:12px;color:var(--ink2);margin-bottom:8px;font-variant-numeric:tabular-nums}
.chart-card .legend{margin:4px 0 8px}
/* isolation */
.dv{position:relative;width:220px;height:12px}
.dv .z{position:absolute;left:50%;top:-3px;bottom:-3px;border-left:1px solid var(--axis)}
.dv i{position:absolute;top:0;bottom:0}
.dv i.p{left:50%;background:var(--pos);border-radius:0 4px 4px 0}
.dv i.m{right:50%;background:var(--neg);border-radius:4px 0 0 4px}
.dv i.cap{background-image:repeating-linear-gradient(135deg,rgba(255,255,255,.55) 0 3px,transparent 3px 7px)}
.interp{margin:0;padding-left:18px}
.interp li{margin:4px 0;max-width:95ch}
.interp li.warn::marker{content:"⚠  ";color:var(--warn)}
.pill{font-size:11px;padding:0 6px;border-radius:999px;background:var(--surface2);color:var(--ink2);border:1px solid var(--ring)}
/* crowd */
.xy{grid-template-rows:200px 20px auto;max-width:620px}
.xy .xt{grid-column:2;text-align:center;font-size:11px;color:var(--muted);margin-top:2px}
.pt{position:absolute;width:10px;height:10px;margin:-5px 0 0 -5px;border-radius:50%;background:var(--c);box-shadow:0 0 0 2px var(--surface)}
details{border-top:1px solid var(--grid)}
details:first-of-type{border-top:0}
details summary{cursor:pointer;padding:9px 2px;list-style-position:outside}
details summary:hover{color:var(--s1)}
details[open] summary{font-weight:600}
details .tw{margin:4px 0 14px}
.indent{display:inline-block}
.meth dt{font-weight:620;margin-top:10px}.meth dd{margin:2px 0 0;color:var(--ink2);max-width:95ch}
footer{margin-top:48px;font-size:12px;color:var(--muted)}
'''

JS = r'''
(function(){
function f1(v){return v.toLocaleString('fr-FR',{minimumFractionDigits:1,maximumFractionDigits:1});}
function near(xs,x){var lo=0,hi=xs.length-1;if(hi<0)return -1;while(hi-lo>1){var m=(lo+hi)>>1;if(xs[m]<x)lo=m;else hi=m;}return Math.abs(xs[lo]-x)<=Math.abs(xs[hi]-x)?lo:hi;}
var charts=document.querySelectorAll('.lc');
for(var ci=0;ci<charts.length;ci++)(function(c){
 var plot=c.querySelector('.plot');if(!plot)return;
 var xmax=parseFloat(c.getAttribute('data-xmax'))||1,unit=c.getAttribute('data-xunit'),ser=[],byName={};
 var pl=plot.querySelectorAll('polyline[data-name]');
 for(var i=0;i<pl.length;i++){var p=pl[i],n=p.getAttribute('data-name'),s=byName[n];
  if(!s){s=byName[n]={n:n,r:+(p.getAttribute('data-rank')||0),c:getComputedStyle(p).stroke,xs:[],ys:[]};ser.push(s);}
  var a=p.getAttribute('points').trim().split(/\s+/);
  for(var j=0;j<a.length;j++){var q=a[j].split(',');s.xs.push(+q[0]);s.ys.push(-q[1]);}}
 if(!ser.length)return;
 ser.sort(function(a,b){return a.r-b.r;});
 var cross=document.createElement('div');cross.className='cross';plot.appendChild(cross);
 var tip=document.createElement('div');tip.className='tip';plot.appendChild(tip);
 var kx=0.5;
 function hide(){cross.style.display='none';tip.style.display='none';}
 function show(fx){fx=Math.max(0,Math.min(1,fx));var s0=ser[0],i=near(s0.xs,fx*xmax);if(i<0)return;
  var X=s0.xs[i],px=X/xmax*100;cross.style.left=px+'%';cross.style.display='block';
  tip.textContent='';var h=document.createElement('div');h.className='th';
  h.textContent=unit==='s'?('t = '+f1(X)+' s'):('image '+Math.round(X));tip.appendChild(h);
  for(var k=0;k<ser.length;k++){var s=ser[k],j=near(s.xs,X);if(j<0)continue;
   if(k>0&&Math.abs(s.xs[j]-X)>xmax/150)continue;
   var r=document.createElement('div');r.className='tr';var key=document.createElement('i');key.style.background=s.c;r.appendChild(key);
   var b=document.createElement('b');b.textContent=f1(s.ys[j])+' ms';r.appendChild(b);
   var sp=document.createElement('span');sp.textContent=s.n;r.appendChild(sp);tip.appendChild(r);}
  tip.style.display='block';
  if(px>55){tip.style.left='auto';tip.style.right='calc('+(100-px)+'% + 10px)';}
  else{tip.style.right='auto';tip.style.left='calc('+px+'% + 10px)';}}
 plot.addEventListener('pointermove',function(e){var r=plot.getBoundingClientRect();kx=(e.clientX-r.left)/r.width;show(kx);});
 plot.addEventListener('pointerleave',hide);
 plot.addEventListener('focus',function(){show(kx);});
 plot.addEventListener('blur',hide);
 plot.addEventListener('keydown',function(e){
  if(e.key==='ArrowRight'){kx=Math.min(1,kx+0.01);show(kx);e.preventDefault();}
  else if(e.key==='ArrowLeft'){kx=Math.max(0,kx-0.01);show(kx);e.preventDefault();}
  else if(e.key==='Escape'){hide();}});
})(charts[ci]);
})();
'''


FR_SPACE = re.compile(r' ([:;!?»])')


def page(title, body):
    # French typography: narrow no-break space before : ; ! ? » and after «
    body = FR_SPACE.sub(NNBSP + r'\1', body).replace('« ', '«' + NNBSP)
    return ('<!DOCTYPE html>\n<html lang="fr">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            '<meta name="color-scheme" content="light dark">\n'
            '<title>%s</title>\n<style>%s</style>\n</head>\n<body>\n<div class="wrap">\n%s\n</div>\n'
            '<script>%s</script>\n</body>\n</html>\n') % (esc(title), CSS, body, JS)


def status_html(t):
    if t.skipped:
        return '<span class="st skip">– ignoré</span>' + (
            '<span class="why">%s</span>' % esc(t.skip_reason) if t.skip_reason else '')
    if t.interrupted:
        return '<span class="st warn">⚠ interrompu</span><span class="why">exclu des scores</span>'
    if t.frames <= 0:
        return '<span class="st skip">aucune image</span>'
    return '<span class="st ok">✓ ok</span>'


def split_html(t):
    c, g = t.cpu_bound, t.gpu_bound
    if (c is None and g is None) or t.gpu['mean'] is None:
        return '<span class="muted">%s</span>' % DASH
    if c is None:
        c = 100.0 - g
    if g is None:
        g = 100.0 - c
    tot = max(1e-9, c + g)
    cw, gw = 100.0 * c / tot, 100.0 * g / tot
    dom = ('CPU %s' % fmt_pct(c)) if c >= g else ('GPU %s' % fmt_pct(g))
    return ('<span class="split" title="limité CPU %s · limité GPU %s"><i style="width:%.1f%%;background:var(--cpu)"></i>'
            '<i style="width:%.1f%%;background:var(--gpu)"></i></span>%s') % (fmt_pct(c), fmt_pct(g), cw, gw, dom)


def scene_label(t):
    s = t.scene
    parts = []
    h, m = num(s.get('hour')), num(s.get('minute'))
    if h is not None:
        parts.append('%02d:%02d' % (int(h), int(m or 0)))
    w = num(s.get('weather'))
    if w is not None:
        parts.append(WEATHER.get(int(w), 'météo %d' % int(w)))
    pd, cd = num(s.get('ped_density')), num(s.get('car_density'))
    if pd is not None and (pd != 1.0 or (cd is not None and cd != 1.0)):
        parts.append('densité ×%s/×%s' % (fmt(pd, 0 if pd == int(pd) else 1), fmt(cd, 0 if cd is not None and cd == int(cd) else 1)))
    if s.get('fixed_step') is False:
        parts.append('temps réel')
    if s.get('paused') is True:
        parts.append('gelé')
    return parts


def sec_header(run):
    d = run.device
    out = ['<header>']
    out.append('<div class="head"><div><div class="muted small">reLCS Benchmark · rapport</div>'
               '<h1>%s</h1></div><div class="sec small">%s · mode %s</div></div>' % (
                   esc(' · '.join(x for x in (txt(d.get('platform')), txt(d.get('model'))) if x) or 'Appareil inconnu'),
                   esc(run.date_label()), esc(run.mode_label())))
    clocks = []
    for k, lab in (('cpu_mhz', 'CPU'), ('bus_mhz', 'bus'), ('gpu_mhz', 'GPU'), ('xbar_mhz', 'xbar')):
        v = pos(d.get(k))
        if v:
            clocks.append('%s %s' % (lab, fmt(v, 0)))
    scr = arr(d.get('screen'))
    screen = '%s × %s' % (fmt_int(scr[0]), fmt_int(scr[1])) if len(scr) >= 2 and num(scr[0]) else DASH
    boot = []
    for k, lab in (('engine_ms', 'moteur'), ('game_ms', 'jeu'), ('ready_ms', 'prêt')):
        v = pos(run.boot.get(k))
        if v:
            boot.append('%s %s s' % (lab, fmt(v / 1000.0, 1)))
    items = [('Firmware', txt(d.get('firmware')) or DASH), ('Build', txt(d.get('build')) or DASH),
             ('Fréquences (MHz)', ' · '.join(clocks) or DASH), ('GPU', txt(d.get('gpu')) or DASH),
             ('Écran', screen), ('Démarrage', ' · '.join(boot) or DASH)]
    if txt(d.get('notes')):
        items.append(('Notes', txt(d.get('notes'))))
    out.append('<dl class="kv">%s</dl>' % ''.join(
        '<div><dt>%s</dt><dd>%s</dd></div>' % (esc(k), esc(v)) for k, v in items))
    caps = set(run.caps)
    chips = ''.join('<span class="chip%s" title="%s">%s</span>' % ('' if c in caps else ' off', esc(c), esc(lab))
                    for c, lab in CAPS)
    extra = [c for c in run.caps if c not in dict(CAPS)]
    chips += ''.join('<span class="chip">%s</span>' % esc(c) for c in extra)
    out.append('<div class="chips" aria-label="capacités">%s</div>' % chips)
    if run.settings:
        out.append('<div class="chips">%s</div>' % ''.join(
            '<span class="chip mono">%s=%s</span>' % (esc(k), esc(v)) for k, v in run.settings.items()))
    if run.aborted:
        out.append('<div class="banner crit"><b>⚠ Série abandonnée.</b> Les tests restants n’ont pas été exécutés ; '
                   'les scores ne couvrent que les tests terminés.</div>')
    if run.partial:
        out.append('<div class="banner warn"><b>⚠ Résultats partiels.</b> Fichier écrit pendant la série '
                   '(série en cours, plantage ou arrêt) : analyses, deltas et frames.csv peuvent manquer.</div>')
    if run.scores_valid is False:
        out.append('<div class="banner warn"><b>Scores non valides.</b> Un groupe de tests est vide, ignoré '
                   'ou interrompu : comparer ces scores avec prudence.</div>')
    if run.schema and run.schema != SCHEMA:
        out.append('<div class="banner warn">Schéma <code>%s</code> inattendu (attendu <code>%s</code>) : '
                   'lecture au mieux.</div>' % (esc(run.schema), SCHEMA))
    if run.csv_note:
        out.append('<div class="banner info">%s : pas de courbes par image ni de liste des hitches.</div>' % esc(run.csv_note))
    out.append('</header>')
    return '\n'.join(out)


def sec_toc(run, sections):
    return '<nav class="toc">%s</nav>' % ''.join('<a href="#%s">%s</a>' % (a, esc(l)) for a, l in sections)


def sec_scores(run):
    g_fps, g_n = run.group_fps(('graphics',))
    c_fps, c_n = run.group_fps(('cpu',))
    o_fps, o_n = run.group_fps(('graphics', 'cpu'), ('s1_bridge_run',))
    cards = [('overall', 'Global', 'tg-isolation', o_fps, o_n, 'graphismes + processeur + s1', True),
             ('graphics', 'Graphismes', 'tg-graphics', g_fps, g_n, 'survols de la carte', False),
             ('cpu', 'Processeur', 'tg-cpu', c_fps, c_n, 'foules, IA, chaos', False)]
    out = ['<h2 id="scores">Scores</h2>',
           '<p class="lead">100 × moyenne géométrique des FPS. Plus c’est haut, mieux c’est ; '
           'les tests ignorés ou interrompus sont exclus.%s</p>' % (
               ' <b>Scores recalculés par le rapport</b> (absents du fichier).' if run.scores_computed else ''),
           '<div class="scores">']
    for key, lab, cls, fps, n, what, hero in cards:
        v = run.scores.get(key)
        sub = ('≈ %s fps de moyenne géométrique · %d test%s · %s' % (fmt(fps, 1), n, 's' if n > 1 else '', what)
               if fps else 'aucun test valide')
        out.append('<div class="card score%s"><div class="lab"><i class="dot %s"></i>%s</div>'
                   '<div class="val">%s</div><div class="sub">%s</div></div>' % (
                       ' hero' if hero else '', cls, lab, fmt(v, 0), esc(sub)))
    out.append('</div>')
    return '\n'.join(out)


def sec_targets(run):
    out = ['<h2 id="optimiser">À optimiser en priorité</h2>']
    if not run.targets:
        out.append('<p class="lead">Aucune cible calculée (pas de sections ni de deltas d’isolation dans ce fichier).</p>')
        return '\n'.join(out)
    out.append('<p class="lead">Classement issu des tests de jeu et des tests d’isolation : coût moyen par image et part '
               'de l’image ; pour le streaming, durée du pire pic ou du chargement.%s</p>' % (
                   ' <b>Cibles calculées par le rapport</b> à partir des sections (absentes du fichier).'
                   if run.targets_computed else ''))
    rows = []
    for t in run.targets:
        rows.append((txt(t.get('what')) or '?', txt(t.get('kind')), num(t.get('ms')), num(t.get('pct')),
                     txt(t.get('advice'))))
    # per-frame costs share one scale; streaming times (hitch, LoadScene) are hatched when larger
    scale = max([ms for _, k, ms, _, _ in rows if ms is not None and k != 'streaming'] or [1.0])
    out.append('<div class="card"><ol class="targets">')
    for i, (what, kind, ms, pc, adv) in enumerate(rows, 1):
        over = ms is not None and ms > scale * 1.001
        w = 100.0 if over else pct_of(ms, scale)
        label, ident = target_name(run, what)
        if kind == 'streaming':
            unit = 'ms'
            pct_txt = ('%s du test en pics' % fmt_pct(pc, 1)) if pc else ''
        else:
            unit = 'ms/image'
            pct_txt = ('%s de l’image' % fmt_pct(pc)) if pc is not None else ''
        out.append('<li><div class="rk">%d</div><div class="what">%s%s <span class="pill">%s</span></div>'
                   '<div class="bar" title="%s ms"><i class="%s" style="width:%.1f%%"></i></div>'
                   '<div class="v"><b>%s</b> %s<br><span class="muted small">%s</span></div>%s</li>' % (
                       i, esc(label), (' <span class="tid" style="display:inline">%s</span>' % esc(ident)) if ident else '',
                       esc(TARGET_KIND.get(kind, kind or DASH)), esc(fmt(ms, 2)), 'over' if over else '', w,
                       fmt(ms, 1), unit, esc(pct_txt), '<div class="adv">%s</div>' % esc(adv) if adv else ''))
    out.append('</ol></div>')
    return '\n'.join(out)


def target_name(run, what):
    # (display label, identifier shown in small print or '')
    if what in VARIANT_LABEL:
        lab = VARIANT_LABEL[what]
        if lab.startswith('Sans '):
            lab = lab[5:6].upper() + lab[6:]
        return lab, what
    if what in run.by_id:
        return run.by_id[what].label(), what
    return TARGET_WHAT.get(what, what), ''


def test_row(run, t):
    fr, wk, gp = t.frame, t.work, t.gpu
    target = run.vp_anchor.get(t.viewpoint) if t.group == 'isolation' else run.anchors.get(id(t))
    name = esc(t.label())
    if target:
        name = '<a href="#%s" style="color:inherit;text-decoration:none">%s</a>' % (target, name)
    hit = fmt_int(t.hitches) if t.ok else DASH
    if t.ok and t.hitches and t.worst_hitch:
        hit += '<span class="why nw">pire %s ms%s</span>' % (
            fmt(t.worst_hitch, 0), (' · ' + esc(GROUP_LABEL.get(t.worst_hitch_group, t.worst_hitch_group)))
            if t.worst_hitch_group else '')
    cells = ['<td class="first">%s<span class="tid">%s</span></td>' % (name, esc(t.id)),
             '<td class="small sec kc">%s<span class="why">%s</span></td>' % (
                 esc(KIND_LABEL.get(t.kind, t.kind)), ' · '.join('<span class="nw">%s</span>' % esc(p) for p in scene_label(t))),
             '<td class="n">%s</td>' % (fmt_int(t.frames) if t.frames else DASH),
             '<td class="n"><b>%s</b></td>' % fmt(t.fps, 1),
             '<td class="n">%s</td>' % fmt(fr['low1_fps'], 1),
             '<td class="n">%s</td>' % fmt(fr['mean'], 1),
             '<td class="n">%s</td>' % fmt(fr['p95'], 1),
             '<td class="n">%s</td>' % fmt(wk['mean'], 1),
             '<td class="n">%s</td>' % fmt(gp['mean'], 1),
             '<td>%s</td>' % (split_html(t) if t.ok else DASH),
             '<td class="n">%s</td>' % hit,
             '<td>%s</td>' % status_html(t)]
    return '<tr>%s</tr>' % ''.join(cells)


TEST_HEAD = ('<thead><tr><th class="first">Test</th><th>Type · scène</th><th class="n">Images</th>'
             '<th class="n">FPS moy.</th><th class="n">1 % bas</th><th class="n">Image (ms)</th>'
             '<th class="n">p95 (ms)</th><th class="n">CPU work</th><th class="n">GPU</th>'
             '<th>Limité par</th><th class="n">Hitches</th><th>Statut</th></tr></thead>')


def sec_tests(run):
    out = ['<h2 id="tests">Tests</h2>',
           '<p class="lead">Temps en millisecondes par image. « 1 % bas » : FPS des 1 % d’images les plus lentes. '
           '« Limité par » : part des images où le travail <span class="sw" style="--c:var(--cpu)"></span> CPU '
           '(resp. le temps <span class="sw" style="--c:var(--gpu)"></span> GPU) est le plus long ; vide sans mesure GPU.</p>']
    if not run.tests:
        out.append('<div class="banner warn">Aucun test dans ce fichier.</div>')
        return '\n'.join(out)
    main = [g for g in run.ordered_groups() if g != 'isolation']
    out.append('<div class="tw"><table>%s<tbody>' % TEST_HEAD)
    for g in main:
        tests = [t for t in run.tests if t.group == g]
        out.append('<tr class="grp"><th colspan="12"><span class="g"><i class="dot tg-%s"></i>%s '
                   '<span class="muted small">%d test%s</span></span></th></tr>' % (
                       esc(g), esc(TEST_GROUP_LABEL.get(g, g)), len(tests), 's' if len(tests) > 1 else ''))
        out.extend(test_row(run, t) for t in tests)
    out.append('</tbody></table></div>')
    iso = [t for t in run.tests if t.group == 'isolation']
    if iso:
        n_skip = sum(1 for t in iso if t.skipped)
        out.append('<details><summary>Tests d’isolation : %d tests%s (détail par variante dans la section Isolation)'
                   '</summary><div class="tw"><table>%s<tbody>' % (
                       len(iso), (', %d ignorés' % n_skip) if n_skip else '', TEST_HEAD))
        out.extend(test_row(run, t) for t in iso)
        out.append('</tbody></table></div></details>')
    return '\n'.join(out)


def sec_budget(run):
    rows = [t for t in run.tests if t.gameplay and t.ok]
    rows += [t for t in run.tests if t.group == 'isolation' and t.variant in ('baseline', 'sim_running') and t.ok]
    out = ['<h2 id="budget">Budget d’image</h2>',
           '<p class="lead">Temps moyen par image réparti par groupe du profileur ; traits verticaux : '
           'budgets 60, 45 et 30 FPS (16,7 / 22,2 / 33,3 ms). « fin d’image » contient le swap, donc l’attente du GPU.</p>']
    if not rows:
        out.append('<p class="muted">Aucun test de jeu mesuré.</p>')
        return '\n'.join(out)
    has_groups = any(any((t.groups.get(g) or 0) > 0 for g in GROUPS) for t in rows)
    biggest = max(max(sum((t.groups.get(g) or 0) for g in GROUPS), t.frame['mean'] or 0) for t in rows)
    scale = nice_ceil(max(biggest * 1.04, 36.0 if biggest > 12 else 0.0))
    legend = ''.join('<span title="%s"><i class="sw g-%s"></i>%s</span>' % (esc(GROUP_HINT[g]), g, esc(GROUP_LABEL[g]))
                     for g in GROUPS)
    legend += '<span><i class="sw g-other"></i>%s</span>' % ('hors sections' if has_groups else 'non détaillé')
    out.append('<div class="card"><div class="legend">%s</div>' % legend)
    if not has_groups:
        out.append('<p class="muted small">Sections du profileur indisponibles sur cette plateforme : '
                   'seul le temps total est montré.</p>')
    refs = [(1000 / 60.0, '60'), (1000 / 45.0, '45'), (1000 / 30.0, '30 fps')]
    axis = ''.join('<span style="left:%.2f%%">%s</span>' % (pct_of(v, scale), l) for v, l in refs if v <= scale)
    out.append('<div class="budget"><div class="bspace"></div><div class="baxis">%s</div><div class="bspace"></div>' % axis)
    for t in rows:
        segs = []
        total = 0.0
        for g in GROUPS:
            v = t.groups.get(g) or 0.0
            if v <= 0:
                continue
            total += v
            segs.append('<span class="g-%s" style="width:%.3f%%" title="%s : %s ms (%s)"></span>' % (
                g, pct_of(v, scale), esc(GROUP_LABEL[g]), fmt(v, 2), esc(GROUP_HINT[g])))
        fm = t.frame['mean'] or 0.0
        other = fm - total
        if other > 0.15 or not segs:
            segs.append('<span class="g-other" style="width:%.3f%%" title="%s : %s ms"></span>' % (
                pct_of(max(other, 0.0), scale), 'hors sections' if segs else 'image', fmt(other, 2)))
        lines = ''.join('<i class="ref" style="left:%.2f%%"></i>' % pct_of(v, scale) for v, _ in refs if v <= scale)
        out.append('<div class="bl" title="%s">%s</div><div class="btrack"><div class="bstack">%s</div>%s</div>'
                   '<div class="bv">%s ms</div>' % (esc(t.id), esc(t.label()), ''.join(segs), lines, fmt(fm, 1)))
    out.append('</div></div>')
    # table twin
    out.append('<details><summary>Valeurs du budget (ms par image)</summary><div class="tw"><table><thead><tr>'
               '<th class="first">Test</th>%s<th class="n">Image</th></tr></thead><tbody>' % ''.join(
                   '<th class="n">%s</th>' % esc(GROUP_LABEL[g]) for g in GROUPS))
    for t in rows:
        out.append('<tr><td class="first">%s</td>%s<td class="n"><b>%s</b></td></tr>' % (
            esc(t.label()), ''.join('<td class="n">%s</td>' % fmt(t.groups.get(g), 2) for g in GROUPS),
            fmt(t.frame['mean'], 2)))
    out.append('</tbody></table></div></details>')
    return '\n'.join(out)


def line_chart(t):
    rows = [r for r in t.rows if r.get('frame_ms') is not None]
    n = len(rows)
    if n < 2:
        return ''
    xs, acc = [], 0.0
    use_t = all(r.get('t_ms') is not None for r in rows) and all(
        rows[i]['t_ms'] <= rows[i + 1]['t_ms'] for i in range(n - 1))
    for r in rows:
        if use_t:
            xs.append(r['t_ms'] / 1000.0)
        else:
            xs.append(acc / 1000.0)
            acc += r['frame_ms']
    xmax = max(xs[-1] + rows[-1]['frame_ms'] / 1000.0, 1e-3)
    if n > MAX_POINTS:
        idx = []
        step = n / float(MAX_POINTS)
        for b in range(MAX_POINTS):
            lo, hi = int(b * step), min(n, int((b + 1) * step))
            if hi > lo:
                idx.append(max(range(lo, hi), key=lambda i: rows[i]['frame_ms']))
    else:
        idx = list(range(n))
    fr = sorted(r['frame_ms'] for r in rows)
    med, p90, vmax = percentile(fr, 50), percentile(fr, 90), fr[-1]
    cap = max(p90 * 1.5, med * 2.0, 5.0)
    clipped = vmax > cap * 1.15
    top = cap if clipped else vmax
    if med > 12:
        top = max(top, 36.0)
    top *= 1.04
    ystep = nice_step(top, 5)
    ymax = math.ceil(top / ystep - 1e-9) * ystep
    yt = [i * ystep for i in range(int(round(ymax / ystep)) + 1)]
    # drawn in this order (frame on top); the tooltip lists them by rank
    series = [('CPU work', 'work_ms', 'var(--cpu)', 1)]
    if any(row_gpu(r) for r in rows):
        series.append(('GPU', 'gpu_ms', 'var(--gpu)', 2))
    series.append(('image', 'frame_ms', 'var(--frame)', 0))
    xt, xstep = ticks(xmax, 5)
    svg = ['<svg viewBox="0 %s %s %s" preserveAspectRatio="none" aria-hidden="true">' % (
        fmt_svg(-ymax), fmt_svg(xmax), fmt_svg(ymax))]
    for v in yt[1:]:
        svg.append('<line class="gl" x1="0" x2="%s" y1="%s" y2="%s" vector-effect="non-scaling-stroke"/>' % (
            fmt_svg(xmax), fmt_svg(-v), fmt_svg(-v)))
    refs = [(1000 / 30.0, '30 fps')]
    if ymax <= 100:
        refs.append((1000 / 60.0, '60 fps'))
    refs = [(v, l) for v, l in refs if v < ymax]
    for v, _ in refs:
        svg.append('<line class="rl" x1="0" x2="%s" y1="%s" y2="%s" vector-effect="non-scaling-stroke"/>' % (
            fmt_svg(xmax), fmt_svg(-v), fmt_svg(-v)))
    hitches = [i for i, r in enumerate(rows) if r.get('flags') is not None and int(r['flags']) & BF_HITCH]
    if not hitches and all(r.get('flags') is None for r in rows):
        lim = max(100.0, 3 * med)
        hitches = [i for i, r in enumerate(rows) if r['frame_ms'] > lim]
    for i in hitches[:200]:
        svg.append('<line class="hl" x1="%s" x2="%s" y1="0" y2="%s" vector-effect="non-scaling-stroke"/>' % (
            fmt_svg(xs[i]), fmt_svg(xs[i]), fmt_svg(-ymax)))
    area = ['%s,%s' % (fmt_svg(xs[i]), fmt_svg(-rows[i]['frame_ms'])) for i in idx]
    svg.append('<polygon class="area" points="%s,0 %s %s,0"/>' % (fmt_svg(xs[idx[0]]), ' '.join(area), fmt_svg(xs[idx[-1]])))
    for name, key, color, rank in series:
        pts = []
        for i in idx:
            v = row_gpu(rows[i]) if key == 'gpu_ms' else rows[i].get(key)
            if v is not None:
                pts.append('%s,%s' % (fmt_svg(xs[i]), fmt_svg(-v)))
        if pts:
            svg.append('<polyline data-name="%s" data-rank="%d" class="%s" style="stroke:%s" points="%s" '
                       'vector-effect="non-scaling-stroke"/>' % (esc(name), rank, 'main' if rank == 0 else 'sub',
                                                                  color, ' '.join(pts)))
    svg.append('</svg>')
    rlabs = ''.join('<span class="rlab" style="top:%.2f%%">%s</span>' % (100 - pct_of(v, ymax), l) for v, l in refs)
    ya = ''.join('<span style="bottom:%.2f%%">%s</span>' % (pct_of(v, ymax), tick_label(v, ystep)) for v in yt)
    xa = ''.join('<span style="left:%.2f%%">%s</span>' % (pct_of(v, xmax), tick_label(v, xstep) + (' s' if v == xt[-1] else ''))
                 for v in xt if v <= xmax * 0.97 or v == 0)
    legend = ''.join('<span><i class="lk" style="--c:%s"></i>%s</span>' % (c, esc(nm))
                     for nm, _, c, _ in sorted(series, key=lambda s: s[3]))
    if hitches:
        legend += '<span><i class="lk" style="--c:var(--crit);width:2px;height:10px"></i>hitch (%d)</span>' % len(hitches)
    note = ''
    if clipped:
        note = '<span class="muted small">échelle écrêtée à %s ms · pic %s ms</span>' % (fmt(ymax, 0), fmt(vmax, 0))
    if n > MAX_POINTS:
        note += '<span class="muted small"> · %s images, pic par tranche (%d points)</span>' % (fmt_int(n), len(idx))
    return ('<div class="legend">%s%s</div><div class="lc" data-xmax="%s" data-xunit="s">'
            '<div class="ya">%s</div><div class="plot" tabindex="0" aria-label="temps d’image de %s">%s%s</div>'
            '<div class="xa">%s</div></div>') % (legend, note, fmt_svg(xmax), ya, esc(t.id), ''.join(svg), rlabs, xa)


def fmt_svg(v):
    s = '%.3f' % v
    s = s.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def sec_timelines(run):
    tests = [t for t in run.tests if id(t) in run.anchors]
    out = ['<h2 id="courbes">Temps d’image</h2>']
    if not tests:
        out.append('<p class="lead">%s.</p>' % esc(run.csv_note or 'Aucune image enregistrée pour les tests de jeu'))
        return '\n'.join(out)
    out.append('<p class="lead">Une ligne par image (frames.csv) : image complète, travail CPU du thread principal '
               'et temps GPU (fence) quand il est connu. Survoler ou utiliser les flèches pour lire les valeurs.</p>')
    out.append('<div class="grid2">')
    for t in tests:
        fr = t.frame
        out.append('<div class="card chart-card" id="%s"><h3>%s <span class="tid">%s</span></h3>'
                   '<div class="stats">%s fps · moy. %s ms · p95 %s · p99 %s · max %s%s</div>%s</div>' % (
                       run.anchors[id(t)], esc(t.label()), esc(t.id), fmt(t.fps, 1), fmt(fr['mean'], 1), fmt(fr['p95'], 1),
                       fmt(fr['p99'], 1), fmt(fr['max'], 1),
                       (' · <span class="st warn">⚠ interrompu</span>' if t.interrupted else ''), line_chart(t)))
    out.append('</div>')
    return '\n'.join(out)


def dv_bar(saved, scale):
    if saved is None or not scale:
        return '<div class="dv"><i class="z"></i></div>'
    w = min(50.0, 50.0 * abs(saved) / scale)
    cap = abs(saved) > scale * 1.001
    return '<div class="dv"><i class="z"></i><i class="%s%s" style="width:%.2f%%"></i></div>' % (
        'p' if saved >= 0 else 'm', ' cap' if cap else '', w)


def interpretation(tests, deltas):
    out = []
    base = tests.get('baseline')
    if base is None or not base.ok:
        return [('', 'Référence absente ou ignorée : pas d’interprétation possible.')]
    bm, bw, bg = base.frame['mean'], base.work['mean'], base.gpu['mean']
    if bg is not None and bw is not None:
        if bg > bw * 1.05:
            out.append(('', 'Régime : <b>limité par le GPU</b> (GPU %s ms contre %s ms de travail CPU par image).' % (
                fmt(bg, 1), fmt(bw, 1))))
        elif bw > bg * 1.05:
            out.append(('', 'Régime : <b>limité par le CPU</b> (travail CPU %s ms contre %s ms de GPU par image).' % (
                fmt(bw, 1), fmt(bg, 1))))
        else:
            out.append(('', 'Régime : <b>équilibré</b> (CPU %s ms, GPU %s ms) : il faut alléger les deux.' % (
                fmt(bw, 1), fmt(bg, 1))))
    else:
        out.append(('', 'Temps GPU non mesuré sur cette plateforme : image %s ms, travail CPU %s ms.' % (
            fmt(bm, 1), fmt(bw, 1))))
    d = deltas.get('viewport_50')
    v50 = tests.get('viewport_50')
    if d and d['saved_pct'] is not None:
        if d['saved_pct'] > 25:
            out.append(('warn', '<b>Limité par le remplissage (fill-rate)</b> : le viewport à 50 %% (4 fois moins de pixels) '
                        'fait gagner %s ms (%s). Résolution interne, overdraw des transparents et coût des shaders de '
                        'fragments sont les leviers principaux.' % (fmt(d['saved_ms'], 1), fmt_pct(d['saved_pct']))))
        elif d['saved_pct'] > 8:
            out.append(('', 'Sensibilité moyenne au remplissage : viewport 50 %% → %s ms (%s).' % (
                fmt_signed(d['saved_ms'], 1), fmt_pct(d['saved_pct']))))
        else:
            out.append(('', 'Peu sensible au remplissage : viewport 50 %% ne gagne que %s ms (%s) ; le coût est dans '
                        'la géométrie, la soumission ou le CPU.' % (fmt(d['saved_ms'], 1), fmt_pct(d['saved_pct']))))
    elif v50 is not None and v50.skipped:
        out.append(('', 'Tests de viewport indisponibles : %s.' % esc(v50.skip_reason or 'capacité absente')))
    tiny, null = tests.get('draw_tiny'), tests.get('draw_null')
    if tiny is not None and tiny.ok and bm is not None:
        gain = bm - tiny.frame['mean']
        out.append(('', 'Draw calls minuscules : l’image passe de %s à %s ms ; le travail GPU réel (sommets et pixels) '
                    'pèse environ %s ms au-delà de ce que le CPU impose.' % (
                        fmt(bm, 1), fmt(tiny.frame['mean'], 1), fmt(max(gain, 0.0), 1))))
    if tiny is not None and null is not None and tiny.ok and null.ok:
        sub = (tiny.work['mean'] or 0) - (null.work['mean'] or 0)
        draws = pos(base.pf('draws'))
        per = ' (%s µs par draw, %s draws par image)' % (fmt(sub * 1000.0 / draws, 1), fmt(draws, 0)) if draws else ''
        out.append(('', 'Soumission des draw calls (CPU, pilote) : ≈ <b>%s ms</b> par image entre draw_tiny et draw_null%s.' % (
            fmt(sub, 1), per)))
    elif tiny is not None and tiny.skipped:
        out.append(('', 'Modes de draw indisponibles : %s.' % esc(tiny.skip_reason or 'capacité absente')))
    gs = tests.get('gpu_sync')
    if gs is not None and gs.ok and gs.gpu_sync['mean'] is not None:
        out.append(('', 'Synchro GPU : attente résiduelle après soumission %s ms par image (fence sans blocage : %s ms).' % (
            fmt(gs.gpu_sync['mean'], 1), fmt(bg, 1))))
    sr = deltas.get('sim_running')
    if sr and sr['saved_ms'] is not None:
        wd = None
        if sr['variant_work_ms'] is not None and sr['baseline_work_ms'] is not None:
            wd = sr['variant_work_ms'] - sr['baseline_work_ms']
        txt_ = 'Simulation (IA, physique, scripts, population) : l’image s’allonge de %s ms' % fmt(-sr['saved_ms'], 1)
        if wd is not None:
            txt_ += ' pour %s ms de travail CPU en plus' % fmt(wd, 1)
            if wd > 1.0 and -sr['saved_ms'] < wd * 0.5:
                txt_ += ' (en grande partie masqué par le GPU : il réapparaîtra dès que le GPU sera allégé)'
        out.append(('', txt_ + '.'))
    na = deltas.get('no_audio')
    if na and na['saved_ms'] is not None:
        wd = None
        if na['variant_work_ms'] is not None and na['baseline_work_ms'] is not None:
            wd = na['baseline_work_ms'] - na['variant_work_ms']
        out.append(('', 'Audio (DMAudio.Service) : %s ms d’image%s (no_audio contre sim_running).' % (
            fmt(na['saved_ms'], 1), (', %s ms de travail CPU' % fmt(wd, 1)) if wd is not None else '')))
    be = deltas.get('baseline_end')
    if be and be['saved_pct'] is not None:
        drift = -be['saved_pct']
        if abs(drift) > 3:
            out.append(('warn', 'Dérive de %s entre la première et la dernière référence : les deltas de ce point de vue '
                        'sont moins fiables (température, streaming inachevé).' % fmt_pct(drift, 1, True)))
        else:
            out.append(('', 'Dérive début/fin négligeable (%s) : les deltas sont fiables.' % fmt_pct(drift, 1, True)))
    feats = sorted([d for v, d in deltas.items() if v not in DIAG_VARIANTS and d['saved_ms'] is not None and d['saved_ms'] > 0],
                   key=lambda d: -d['saved_ms'])[:3]
    if feats:
        out.append(('', 'Plus gros postes : %s.' % ', '.join(
            '%s %s ms' % (esc(VARIANT_LABEL.get(d['variant'], d['variant']).lower()), fmt(d['saved_ms'], 1)) for d in feats)))
    return out


def iso_table(rows, tests, scale, title):
    out = ['<h3>%s</h3><div class="tw"><table><thead><tr><th class="first">Variante</th><th class="n">Image (ms)</th>'
           '<th class="n">Économie</th><th>← coût en plus · gain →</th><th class="n">%%</th>'
           '<th class="n">GPU réf. → var.</th><th class="n">CPU réf. → var.</th><th>Réf.</th></tr></thead><tbody>' % esc(title)]
    for d in rows:
        t = tests.get(d['variant'])
        out.append('<tr><td class="first" title="%s">%s <span class="tid" style="display:inline;margin-left:4px">%s</span>'
                   '</td><td class="n">%s</td>'
                   '<td class="n"><b>%s</b></td><td>%s</td><td class="n">%s</td><td class="n">%s → %s</td>'
                   '<td class="n">%s → %s</td><td class="small muted">%s</td></tr>' % (
                       esc(t.id if t else d['variant']), esc(VARIANT_LABEL.get(d['variant'], d['variant'])),
                       esc(d['variant']),
                       fmt(d['variant_ms'], 1), fmt_signed(d['saved_ms'], 2), dv_bar(d['saved_ms'], scale),
                       fmt_pct(d['saved_pct'], 1, True), fmt(d['baseline_gpu_ms'], 1), fmt(d['variant_gpu_ms'], 1),
                       fmt(d['baseline_work_ms'], 1), fmt(d['variant_work_ms'], 1), esc(d['reference'])))
    out.append('</tbody></table></div>')
    return ''.join(out)


def sec_isolation(run):
    iso = [t for t in run.tests if t.group == 'isolation']
    out = ['<h2 id="isolation">Isolation</h2>']
    if not iso:
        out.append('<p class="lead">Aucun test d’isolation dans cette série.</p>')
        return '\n'.join(out)
    out.append('<p class="lead">Même image figée, une fonction désactivée à la fois. Économie = référence − variante : '
               'positive (<span class="sw" style="--c:var(--pos)"></span>) la fonction coûte ce temps ; négative '
               '(<span class="sw" style="--c:var(--neg)"></span>) la variante ajoute du travail. Barres hachurées : '
               'hors échelle.%s</p>' % (' <b>Deltas recalculés par le rapport.</b>' if run.deltas_computed else ''))
    vps = []
    for t in iso:
        if t.viewpoint not in vps:
            vps.append(t.viewpoint)
    for vp in vps:
        tests = {t.variant: t for t in iso if t.viewpoint == vp}
        deltas = {d['variant']: d for d in run.deltas if d['viewpoint'] == vp}
        base = tests.get('baseline')
        head = VIEWPOINT_LABEL.get(vp, vp)
        sub = ''
        if base is not None and base.ok:
            sub = 'référence %s ms · %s fps' % (fmt(base.frame['mean'], 1), fmt(base.fps, 1))
            if pos(base.pf('draws')):
                sub += ' · %s draws' % fmt(base.pf('draws'), 0)
        out.append('<div class="card" id="%s" style="margin-bottom:14px;scroll-margin-top:12px"><h3 style="margin-top:0">'
                   'Point de vue %s <span class="muted small">%s</span></h3>' % (run.vp_anchor[vp], esc(head), esc(sub)))
        out.append('<ul class="interp">%s</ul>' % ''.join(
            '<li%s>%s</li>' % (' class="warn"' if lvl == 'warn' else '', h) for lvl, h in interpretation(tests, deltas)))
        vals = [abs(d['saved_ms']) for v, d in deltas.items() if d['saved_ms'] is not None and v not in SCALE_EXCLUDED]
        scale = max(vals) if vals else max([abs(d['saved_ms']) for d in deltas.values() if d['saved_ms'] is not None] or [1.0])
        order = lambda d: -(d['saved_ms'] if d['saved_ms'] is not None else -1e9)
        feats = sorted([d for v, d in deltas.items() if v not in DIAG_VARIANTS], key=order)
        diags = sorted([d for v, d in deltas.items() if v in DIAG_VARIANTS], key=order)
        if feats:
            out.append(iso_table(feats, tests, scale, 'Fonctions de rendu et réglages'))
        if diags:
            out.append(iso_table(diags, tests, scale, 'Diagnostics : résolution, draw calls, GPU, simulation'))
        skipped = [t for t in tests.values() if t.skipped]
        if skipped:
            out.append('<p class="small muted" style="margin-top:10px">Ignorés : %s.</p>' % '; '.join(
                '%s (%s)' % (esc(VARIANT_LABEL.get(t.variant, t.variant)), esc(t.skip_reason or 'sans raison'))
                for t in skipped))
        interrupted = [t for t in tests.values() if t.interrupted]
        if interrupted:
            out.append('<p class="small">⚠ Interrompus : %s.</p>' % ', '.join(esc(t.variant) for t in interrupted))
        out.append('</div>')
    return '\n'.join(out)


def lsq(points):
    n = len(points)
    if n < 2:
        return None, None
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    vx = sum((p[0] - mx) ** 2 for p in points)
    if vx <= 1e-12:
        return None, None
    slope = sum((p[0] - mx) * (p[1] - my) for p in points) / vx
    return slope, my - slope * mx


def sec_crowd(run):
    tests = sorted([t for t in run.tests if t.id.startswith('c1_crowd_')], key=lambda t: t.id)
    if not tests:
        return ''
    out = ['<h2 id="foule">Montée en charge de la foule</h2>']
    pts = []
    for t in tests:
        if not t.ok:
            continue
        p, v = t.pf('peds'), t.pf('vehicles')
        if p is None and v is None:
            continue
        pts.append((t, (p or 0.0) + (v or 0.0)))
    slope, icpt = lsq([(e, t.frame['mean']) for t, e in pts])
    wslope, wicpt = lsq([(e, t.work['mean']) for t, e in pts if t.work['mean'] is not None])
    if slope is not None:
        out.append('<p class="lead">Chaque entité (piéton ou véhicule) ajoute environ <b>%s ms</b> à l’image '
                   'et <b>%s ms</b> de travail CPU (moindres carrés sur %d densités).</p>' % (
                       fmt(slope, 3), fmt(wslope, 3), len(pts)))
    else:
        out.append('<p class="lead">Pas assez de densités mesurées pour une pente (%d point%s).</p>' % (
            len(pts), 's' if len(pts) > 1 else ''))
    out.append('<div class="grid2"><div class="card">')
    if pts:
        xmax = nice_ceil(max(e for _, e in pts) * 1.08 or 1.0)
        ytop = max(max(t.frame['mean'] for t, _ in pts), max((t.work['mean'] or 0) for t, _ in pts))
        ymax = nice_ceil(ytop * 1.1)
        yt, ystep = ticks(ymax, 4)
        xt, xstep = ticks(xmax, 5)
        svg = ['<svg viewBox="0 %s %s %s" preserveAspectRatio="none" aria-hidden="true">' % (
            fmt_svg(-ymax), fmt_svg(xmax), fmt_svg(ymax))]
        for v in yt[1:]:
            svg.append('<line class="gl" x1="0" x2="%s" y1="%s" y2="%s" vector-effect="non-scaling-stroke"/>' % (
                fmt_svg(xmax), fmt_svg(-v), fmt_svg(-v)))
        for s_, i_, col in ((slope, icpt, 'var(--frame)'), (wslope, wicpt, 'var(--cpu)')):
            if s_ is not None:
                svg.append('<line x1="0" x2="%s" y1="%s" y2="%s" style="stroke:%s;stroke-width:1.5;opacity:.6" '
                           'vector-effect="non-scaling-stroke"/>' % (
                               fmt_svg(xmax), fmt_svg(-i_), fmt_svg(-(i_ + s_ * xmax)), col))
        svg.append('</svg>')
        dots = []
        for t, e in pts:
            for val, col, nm in ((t.frame['mean'], 'var(--frame)', 'image'), (t.work['mean'], 'var(--cpu)', 'CPU work')):
                if val is None:
                    continue
                dots.append('<i class="pt" style="--c:%s;left:%.2f%%;top:%.2f%%" title="%s · %s entités · %s %s ms"></i>' % (
                    col, pct_of(e, xmax), 100 - pct_of(val, ymax), esc(t.name), fmt(e, 0), nm, fmt(val, 1)))
        ya = ''.join('<span style="bottom:%.2f%%">%s</span>' % (pct_of(v, ymax), tick_label(v, ystep)) for v in yt)
        xa = ''.join('<span style="left:%.2f%%">%s</span>' % (pct_of(v, xmax), tick_label(v, xstep)) for v in xt)
        out.append('<div class="legend"><span><i class="sw" style="--c:var(--frame)"></i>image (ms)</span>'
                   '<span><i class="sw" style="--c:var(--cpu)"></i>CPU work (ms)</span>'
                   '<span class="muted">droites : moindres carrés</span></div>')
        out.append('<div class="lc xy"><div class="ya">%s</div><div class="plot">%s%s</div><div class="xa">%s</div>'
                   '<div class="xt">piétons + véhicules (moyenne par image)</div></div>' % (ya, ''.join(svg), ''.join(dots), xa))
    else:
        out.append('<p class="muted">Aucune densité mesurée.</p>')
    out.append('</div><div class="tw"><table><thead><tr><th class="first">Test</th><th class="n">Piétons</th>'
               '<th class="n">Véhicules</th><th class="n">FPS</th><th class="n">Image</th><th class="n">CPU work</th>'
               '<th class="n">GPU</th><th class="n">simulation</th><th>Statut</th></tr></thead><tbody>')
    for t in tests:
        out.append('<tr><td class="first">%s<span class="tid">%s</span></td><td class="n">%s</td><td class="n">%s</td>'
                   '<td class="n"><b>%s</b></td><td class="n">%s</td><td class="n">%s</td><td class="n">%s</td>'
                   '<td class="n">%s</td><td>%s</td></tr>' % (
                       esc(t.name), esc(t.id), fmt(t.pf('peds'), 1), fmt(t.pf('vehicles'), 1), fmt(t.fps, 1),
                       fmt(t.frame['mean'], 1), fmt(t.work['mean'], 1), fmt(t.gpu['mean'], 1),
                       fmt(t.groups.get('sim'), 1), status_html(t)))
    out.append('</tbody></table></div></div>')
    return '\n'.join(out)


def dominant_group(r):
    best, bv = '', 0.0
    for g in GROUPS:
        v = r.get(g + '_ms')
        if v is not None and v > bv:
            best, bv = g, v
    return best


def sec_streaming(run):
    runs = [t for t in run.tests if t.kind == 'streaming']
    tps = [t for t in run.tests if t.kind == 'teleport']
    if not runs and not tps:
        return ''
    out = ['<h2 id="streaming">Streaming</h2>']
    for t in runs:
        out.append('<h3>%s <span class="tid">%s</span></h3>' % (esc(t.name), esc(t.id)))
        if not t.ok:
            out.append('<p>%s</p>' % status_html(t))
            continue
        out.append('<p class="lead">%s fps · %s hitch%s · pire %s ms%s · lecture disque %s Mio en %s ms · attentes '
                   'synchrones %s (%s ms).</p>' % (
                       fmt(t.fps, 1), fmt_int(t.hitches), 'es' if t.hitches > 1 else '', fmt(t.worst_hitch, 0),
                       (' (' + esc(GROUP_LABEL.get(t.worst_hitch_group, t.worst_hitch_group)) + ')') if t.worst_hitch_group else '',
                       fmt((num(t.totals.get('disk_bytes')) or 0) / 1048576.0, 1) if num(t.totals.get('disk_bytes')) is not None else DASH,
                       fmt(t.totals.get('disk_read_ms'), 0), fmt_int(t.totals.get('disk_sync_waits')),
                       fmt(t.totals.get('disk_sync_ms'), 0)))
        hr = t.hitch_rows()
        if hr:
            hr = sorted(sorted(hr, key=lambda r: -(r.get('frame_ms') or 0))[:15], key=lambda r: r.get('t_ms') or 0)
            out.append('<div class="tw"><table><thead><tr><th class="first n">t (s)</th><th class="n">Image</th>'
                       '<th class="n">CPU work</th><th>Groupe dominant</th><th class="n">simulation</th>'
                       '<th class="n">Requêtes</th><th class="n">Disque (Kio)</th><th class="n">Position caméra</th>'
                       '</tr></thead><tbody>')
            for r in hr:
                g = dominant_group(r)
                cam = DASH
                if r.get('cam_x') is not None:
                    cam = '%s ; %s ; %s' % (fmt(r['cam_x'], 0), fmt(r.get('cam_y'), 0), fmt(r.get('cam_z'), 0))
                out.append('<tr><td class="first n">%s</td><td class="n"><b>%s</b></td><td class="n">%s</td><td>%s</td>'
                           '<td class="n">%s</td><td class="n">%s</td><td class="n">%s</td><td class="n">%s</td></tr>' % (
                               fmt((r.get('t_ms') or 0) / 1000.0, 2), fmt(r.get('frame_ms'), 0), fmt(r.get('work_ms'), 1),
                               ('<i class="sw g-%s"></i> %s' % (g, esc(GROUP_LABEL[g]))) if g else DASH,
                               fmt(r.get('sim_ms'), 1), fmt_int(r.get('stream_req')), fmt(r.get('disk_kib'), 0), cam))
            out.append('</tbody></table></div>')
        elif t.hitches:
            out.append('<p class="muted small">Détail des hitches indisponible (frames.csv absent).</p>')
    if tps:
        out.append('<h3>Téléportations</h3>')
        out.append('<p class="lead">Chargement : durée de LoadScene. Stabilisation : temps jusqu’à ce que le streaming '
                   'se calme, pendant lequel les images sont mesurées.</p>')
        out.append('<div class="tw"><table><thead><tr><th class="first">Lieu</th><th class="n">Chargement (ms)</th>'
                   '<th class="n">Stabilisation (ms)</th><th class="n">Images</th><th class="n">FPS</th>'
                   '<th class="n">Hitches</th><th class="n">Pire (ms)</th><th class="n">Disque (Mio)</th>'
                   '<th>Statut</th></tr></thead><tbody>')
        for t in tps:
            db = num(t.totals.get('disk_bytes'))
            out.append('<tr><td class="first">%s<span class="tid">%s</span></td><td class="n"><b>%s</b></td>'
                       '<td class="n">%s</td><td class="n">%s</td><td class="n">%s</td><td class="n">%s</td>'
                       '<td class="n">%s</td><td class="n">%s</td><td>%s</td></tr>' % (
                           esc(t.name.replace('Teleportation: ', '')), esc(t.id), fmt(t.load_ms, 0), fmt(t.settle_ms, 0),
                           fmt_int(t.frames) if t.frames else DASH, fmt(t.fps, 1),
                           fmt_int(t.hitches) if t.ok else DASH, fmt(t.worst_hitch, 0) if t.hitches else DASH,
                           fmt(db / 1048576.0, 1) if db is not None else DASH, status_html(t)))
        lm = mean_of([t.load_ms for t in tps if t.ok])
        sm = mean_of([t.settle_ms for t in tps if t.ok])
        out.append('</tbody></table></div><p class="small sec">Moyenne : chargement %s ms, stabilisation %s ms.</p>' % (
            fmt(lm, 0), fmt(sm, 0)))
    return '\n'.join(out)


def sec_load(run):
    tests = [t for t in run.tests if t.ok and t.group != 'isolation']
    tests += [t for t in run.tests if t.ok and t.group == 'isolation' and t.variant == 'baseline']
    if not tests:
        return ''
    cols = [('draws', 'Draws', 0), ('indices', 'Indices', -1), ('uniforms', 'Uniforms', 0), ('tex_binds', 'Textures liées', 0),
            ('upload_kib', 'Envoi (Kio)', 1), ('disk_kib', 'Disque (Kio)', 1), ('peds', 'Piétons', 1),
            ('vehicles', 'Véhicules', 1), ('objects', 'Objets', 1)]
    out = ['<h2 id="charge">Charge par image</h2>',
           '<p class="lead">Moyennes par image mesurée (compteurs GL, disque, entités) et quelques totaux du test.</p>',
           '<div class="tw"><table><thead><tr><th class="first">Test</th>%s<th class="n">Compil. shaders</th>'
           '<th class="n">Envoi textures (ms)</th><th class="n">Attentes disque</th></tr></thead><tbody>' % ''.join(
               '<th class="n">%s</th>' % esc(l) for _, l, _ in cols)]
    caps = set(run.caps)
    need = {'draws': 'gl_counters', 'indices': 'gl_counters', 'uniforms': 'gl_counters', 'tex_binds': 'gl_counters',
            'upload_kib': 'gl_counters', 'disk_kib': 'disk', 'shader_compiles': 'gl_counters',
            'texture_upload_ms': 'gl_counters', 'disk_sync_waits': 'disk'}

    def known(k, v):
        # a zero from a platform without the counter means "unknown"
        return None if v == 0 and need.get(k) and need[k] not in caps else v

    for t in tests:
        cells = []
        for k, _, dec in cols:
            v = known(k, t.pf(k))
            cells.append('<td class="n">%s</td>' % (fmt_big(v) if dec < 0 else fmt(v, dec)))
        sw = known('disk_sync_waits', num(t.totals.get('disk_sync_waits')))
        sms = num(t.totals.get('disk_sync_ms'))
        out.append('<tr><td class="first">%s<span class="tid">%s</span></td>%s<td class="n">%s</td><td class="n">%s</td>'
                   '<td class="n">%s</td></tr>' % (
                       esc(t.label()), esc(t.id), ''.join(cells),
                       fmt_int(known('shader_compiles', num(t.totals.get('shader_compiles')))),
                       fmt(known('texture_upload_ms', num(t.totals.get('texture_upload_ms'))), 1),
                       ('%s (%s ms)' % (fmt_int(sw), fmt(sms, 0))) if sw else fmt_int(sw)))
    out.append('</tbody></table></div>')
    return '\n'.join(out)


def sec_sections(run):
    tests = [t for t in run.tests if t.ok and t.sections and (
        t.group != 'isolation' or t.variant in ('baseline', 'sim_running'))]
    out = ['<h2 id="sections">Sections du profileur</h2>']
    if not tests:
        out.append('<p class="lead">Aucune section enregistrée (profileur indisponible sur cette plateforme).</p>')
        return '\n'.join(out)
    out.append('<p class="lead">Moyennes par image mesurée, triées par temps propre (temps de la section moins ses '
               'sous-sections). Les temps sont du temps réel du thread principal : ils incluent les attentes du pilote.</p>')
    out.append('<div class="card">')
    for t in tests:
        secs = t.sections
        fm = t.frame['mean'] or 0.0
        order = sorted(range(len(secs)), key=lambda i: -(num(secs[i].get('self_ms')) or 0.0))
        top = secs[order[0]] if order else {}
        out.append('<details><summary>%s <span class="tid" style="display:inline">%s</span> '
                   '<span class="muted small">· %d sections · plus coûteuse : %s %s ms</span></summary>' % (
                       esc(t.label()), esc(t.id), len(secs), esc(txt(top.get('name'))), fmt(top.get('self_ms'), 2)))
        out.append('<div class="tw"><table><thead><tr><th class="first">Section</th><th class="n">Propre (ms)</th>'
                   '<th>Part de l’image</th><th class="n">Incl. (ms)</th><th class="n">Max (ms)</th>'
                   '<th class="n">Appels/image</th></tr></thead><tbody>')
        for i in order:
            s = secs[i]
            depth = int(num(s.get('depth')) or 0)
            par = num(s.get('parent'))
            pname = ''
            if par is not None and 0 <= int(par) < len(secs) and int(par) != i:
                pname = txt(secs[int(par)].get('name'))
            sm = num(s.get('self_ms'))
            out.append('<tr><td class="first"><span class="indent" style="padding-left:%dpx">%s</span>%s</td>'
                       '<td class="n"><b>%s</b></td><td><div class="bar" style="width:160px"><i style="width:%.1f%%">'
                       '</i></div></td><td class="n">%s</td><td class="n">%s</td><td class="n">%s</td></tr>' % (
                           min(depth, 8) * 14, esc(txt(s.get('name'))),
                           (' <span class="muted small">← %s</span>' % esc(pname)) if pname else '',
                           fmt(sm, 2), pct_of(sm, fm), fmt(s.get('ms'), 2), fmt(s.get('max_ms'), 1),
                           fmt(s.get('calls_per_frame'), 2)))
        out.append('</tbody></table></div></details>')
    out.append('</div>')
    return '\n'.join(out)


def sec_system(run):
    tests = [t for t in run.tests if t.ok and t.group != 'isolation' and (t.threads or any(
        v is not None for v in t.cpu_load) or t.memory)]
    out = ['<h2 id="systeme">Threads et mémoire</h2>']
    if not tests:
        out.append('<p class="lead">Temps par thread, charge des cœurs et mémoire non disponibles sur cette plateforme.</p>')
        return '\n'.join(out)
    names = []
    for t in tests:
        for th in t.threads:
            n = txt(th.get('name'))
            if n and n not in names:
                names.append(n)
    cores = max([len(t.cpu_load) for t in tests] or [0])
    mem = [('heap_used_mib', 'Tas utilisé'), ('heap_total_mib', 'Tas total'), ('gpu_ram_free_mib', 'RAM GPU libre'),
           ('cdram_free_mib', 'CDRAM libre'), ('phycont_free_mib', 'Phycont libre')]
    out.append('<p class="lead">Threads : millisecondes de CPU par seconde (1000 = un cœur plein). Mémoire en Mio, '
               'relevée en fin de test.</p>')
    out.append('<div class="tw"><table><thead><tr><th class="first">Test</th>%s%s%s</tr></thead><tbody>' % (
        ''.join('<th class="n">%s</th>' % esc(n) for n in names),
        ''.join('<th class="n">Cœur %d</th>' % i for i in range(cores)),
        ''.join('<th class="n">%s</th>' % esc(l) for _, l in mem)))
    for t in tests:
        th = {txt(x.get('name')): num(x.get('ms_per_s')) for x in t.threads}
        out.append('<tr><td class="first">%s<span class="tid">%s</span></td>%s%s%s</tr>' % (
            esc(t.label()), esc(t.id), ''.join('<td class="n">%s</td>' % fmt(th.get(n), 0) for n in names),
            ''.join('<td class="n">%s</td>' % fmt_pct(t.cpu_load[i] if i < len(t.cpu_load) else None) for i in range(cores)),
            ''.join('<td class="n">%s</td>' % fmt(t.memory.get(k), 1) for k, _ in mem)))
    out.append('</tbody></table></div>')
    return '\n'.join(out)


def sec_method():
    items = [
        ('Pas fixe', 'Les tests marqués « pas fixe » avancent le jeu de 1/30 s par image quelle que soit la durée réelle '
         'de l’image : chaque passage rend exactement les mêmes scènes et le FPS mesure la vitesse de rendu de ces images. '
         'Le streaming (pont Callahan, téléportations) tourne en temps réel pour rester réaliste.'),
        ('Images gelées', 'Dans les tests d’isolation, la simulation est suspendue après une phase de stabilisation : '
         'chaque variante rend la même image et seule la fonction désactivée change. « Simulation active » et « Sans audio » '
         'relancent la simulation.'),
        ('Temps mesurés', 'Image : d’un début d’image au suivant. CPU work : du début d’image au premier swap (thread '
         'principal), y compris les attentes qui ont lieu pendant ce travail. Swap : durée de l’échange, qui peut '
         'inclure le GPU, la file d’affichage, le pilote et le nettoyage des ressources.'),
        ('GPU : fence ou synchro', 'Le temps GPU « fence » est relevé sans bloquer : il se chevauche avec le travail CPU de '
         'l’image suivante. La variante « Synchro GPU » attend le GPU après chaque swap : l’image devient plus lente mais '
         'l’attente résiduelle est mesurée. Ce n’est pas un compteur d’occupation matérielle du GPU. La fence '
         'donne une borne basse quand le GPU commence à travailler avant le swap.'),
        ('Limité CPU / GPU', 'Comparaison des durées CPU et GPU sur les images ayant une mesure GPU disponible. '
         'Sans mesure GPU, le partage reste inconnu ; un swap long ne suffit pas à attribuer le coût au GPU.'),
        ('Deltas', 'Économie = temps de référence − temps de la variante. Positive : la fonction désactivée coûte ce temps. '
         'Négative : la variante ajoute du travail. La référence est « baseline » du même point de vue, sauf « Sans audio » '
         '(comparé à « Simulation active ») ; l’économie négative de « Simulation active » est le coût de la simulation.'),
        ('Hitch et 1 % bas', 'Hitch : image plus longue que max(100 ms, 3 × la médiane du test). 1 % bas : 1000 / moyenne '
         'des 1 % d’images les plus lentes.'),
        ('Scores', '100 × moyenne géométrique des FPS : graphismes (g1–g4), processeur (c1–c3), global (graphismes, '
         'processeur et s1). Les tests ignorés ou interrompus sont exclus.'),
    ]
    return '<h2 id="methode">Méthode</h2><div class="card"><dl class="meth">%s</dl></div>' % ''.join(
        '<dt>%s</dt><dd>%s</dd>' % (esc(a), esc(b)) for a, b in items)


def build_report(run):
    toc = [('scores', 'Scores'), ('optimiser', 'À optimiser'), ('tests', 'Tests'), ('budget', 'Budget d’image'),
           ('courbes', 'Temps d’image'), ('isolation', 'Isolation')]
    crowd = sec_crowd(run)
    streaming = sec_streaming(run)
    if crowd:
        toc.append(('foule', 'Foule'))
    if streaming:
        toc.append(('streaming', 'Streaming'))
    toc += [('charge', 'Charge'), ('sections', 'Sections'), ('systeme', 'Threads et mémoire'), ('methode', 'Méthode')]
    body = [sec_header(run), sec_toc(run, toc), sec_scores(run), sec_targets(run), sec_tests(run), sec_budget(run),
            sec_timelines(run), sec_isolation(run), crowd, streaming, sec_load(run) or '<h2 id="charge">Charge par image</h2>'
            '<p class="lead">Aucun test mesuré.</p>', sec_sections(run), sec_system(run), sec_method(),
            '<footer>Généré par tools/vita/bench-report.py depuis <code>%s</code> · schéma %s · %d tests · %s images '
            'dans frames.csv.</footer>' % (esc(run.path.name if run.path.parent == run.folder else run.path),
                                            esc(run.schema or '?'), len(run.tests),
                                            fmt_int(sum(len(s) for v in run.frames.values() for s in v)))]
    title = 'reLCS Benchmark · %s · %s' % (txt(run.device.get('platform')) or 'rapport', run.timestamp or '')
    return page(title, '\n'.join(b for b in body if b))


# ---- text summary ----------------------------------------------------------------

def text_summary(run, top=8):
    d = run.device
    L = []
    L.append('reLCS Benchmark — %s' % ' · '.join(x for x in (
        txt(d.get('platform')), txt(d.get('model')), ('firmware ' + txt(d.get('firmware'))) if d.get('firmware') else '',
        ('build ' + txt(d.get('build'))) if d.get('build') else '') if x))
    L.append('%s · mode %s · %d tests%s' % (run.date_label(), run.mode_label(), len(run.tests),
                                           '' if run.frames else ' · ' + run.csv_note))
    if run.aborted:
        L.append('ATTENTION : série abandonnée, tests restants non exécutés.')
    if run.partial:
        L.append('ATTENTION : résultats partiels (écrits pendant la série).')
    if run.scores_valid is False:
        L.append('ATTENTION : scores non valides (groupe vide ou interrompu).')
    L.append('')
    L.append('Scores%s' % (' (recalculés)' if run.scores_computed else ''))
    for key, lab, groups, extra in (('graphics', 'Graphismes', ('graphics',), ()), ('cpu', 'Processeur', ('cpu',), ()),
                                    ('overall', 'Global', ('graphics', 'cpu'), ('s1_bridge_run',))):
        g, n = run.group_fps(groups, extra)
        L.append('  %-11s %8s   (%s fps moy. géom., %d tests)' % (lab, fmt(run.scores.get(key), 0), fmt(g, 1), n))
    L.append('')
    L.append('Tests                                  FPS   1 % bas  image   p95   CPU   GPU  hitches  statut')
    for t in run.tests:
        if t.group == 'isolation':
            continue
        st = 'ignoré: ' + t.skip_reason if t.skipped else ('interrompu' if t.interrupted else '')
        L.append('  %-34s %6s %7s %6s %5s %5s %5s %8s  %s' % (
            t.id[:34], fmt(t.fps, 1), fmt(t.frame['low1_fps'], 1), fmt(t.frame['mean'], 1), fmt(t.frame['p95'], 1),
            fmt(t.work['mean'], 1), fmt(t.gpu['mean'], 1), fmt_int(t.hitches) if t.ok else DASH, st))
    iso = [t for t in run.tests if t.group == 'isolation']
    if iso:
        L.append('')
        L.append('Isolation (%d tests, %d ignorés) : plus fortes économies' % (len(iso), sum(1 for t in iso if t.skipped)))
        vps = []
        for t in iso:
            if t.viewpoint not in vps:
                vps.append(t.viewpoint)
        for vp in vps:
            mine = {x['variant']: x for x in run.deltas if x['viewpoint'] == vp and x['saved_ms'] is not None}
            ds = sorted([x for v, x in mine.items() if v not in DIAG_VARIANTS], key=lambda x: -x['saved_ms'])[:5]
            L.append('  %s : %s' % (VIEWPOINT_LABEL.get(vp, vp), ', '.join(
                '%s %s ms' % (x['variant'], fmt_signed(x['saved_ms'], 1)) for x in ds) or DASH))
            diag = [mine[v] for v in ('viewport_50', 'draw_tiny', 'sim_running', 'no_audio') if v in mine]
            if diag:
                L.append('    diagnostics : %s' % ', '.join('%s %s ms (%s)' % (
                    x['variant'], fmt_signed(x['saved_ms'], 1), fmt_pct(x['saved_pct'], 0, True)) for x in diag))
    L.append('')
    L.append('À optimiser en priorité%s' % (' (calculé depuis les sections)' if run.targets_computed else ''))
    if not run.targets:
        L.append('  (aucune cible)')
    for i, t in enumerate(run.targets[:top], 1):
        kind = txt(t.get('kind'))
        label = target_name(run, txt(t.get('what')))[0]
        pc = num(t.get('pct'))
        L.append('  %2d. %-30s %-9s %8s ms  %s%s' % (
            i, label[:30], TARGET_KIND.get(kind, kind), fmt(t.get('ms'), 1),
            fmt_pct(pc) if kind != 'streaming' else (fmt_pct(pc, 1) + ' en pics' if pc else ''),
            ('  — ' + txt(t.get('advice'))) if t.get('advice') else ''))
    return plain('\n'.join(L))


# ---- comparison ------------------------------------------------------------------

def change_class(old, new, higher_better):
    if old is None or new is None or old == 0:
        return 'same', None
    pct = 100.0 * (new - old) / abs(old)
    if abs(pct) < NOISE_PCT:
        return 'same', pct
    good = pct > 0 if higher_better else pct < 0
    return ('better' if good else 'worse'), pct


def change_html(old, new, higher_better):
    cls, pct = change_class(old, new, higher_better)
    if pct is None:
        return '<td class="n same">%s</td>' % DASH
    arrow = {'better': '▲ ', 'worse': '▼ ', 'same': '≈ '}[cls]
    return '<td class="n %s">%s%s</td>' % (cls, arrow, fmt_pct(pct, 1, True))


def change_text(old, new, higher_better):
    cls, pct = change_class(old, new, higher_better)
    if pct is None:
        return '-'
    return '%s %s' % (plain(fmt_pct(pct, 1, True)), {'better': 'mieux', 'worse': 'PIRE', 'same': '~'}[cls])


def compare_pairs(old, new):
    ids = [t.id for t in new.tests] + [t.id for t in old.tests if t.id not in new.by_id]
    return [(i, old.by_id.get(i), new.by_id.get(i)) for i in ids]


def run_caption(run):
    d = run.device
    return '%s · %s · build %s · %s' % (txt(d.get('platform')) or '?', txt(d.get('model')) or '?',
                                       txt(d.get('build')) or '?', run.date_label())


def compare_table(pairs):
    out = ['<div class="tw"><table><thead><tr><th class="first">Test</th>'
           '<th class="n">FPS avant</th><th class="n">FPS après</th><th class="n">Écart</th>'
           '<th class="n">Image avant</th><th class="n">Image après</th><th class="n">Écart</th>'
           '<th>Statut</th><th class="n">p95 avant → après</th><th class="n">CPU avant → après</th>'
           '<th class="n">GPU avant → après</th></tr></thead><tbody>']
    g = lambda r, a, k: getattr(r, a)[k] if r is not None and r.ok else None
    for tid, o, n in pairs:
        of = o.fps if o and o.ok else None
        nf = n.fps if n and n.ok else None
        om, nm = g(o, 'frame', 'mean'), g(n, 'frame', 'mean')
        st = []
        if o is None:
            st.append('nouveau')
        if n is None:
            st.append('absent après')
        for r, lab in ((o, 'avant'), (n, 'après')):
            if r is not None and r.skipped:
                st.append('ignoré ' + lab)
            if r is not None and r.interrupted:
                st.append('⚠ interrompu ' + lab)
        out.append('<tr><td class="first">%s<span class="tid">%s</span></td><td class="n">%s</td><td class="n"><b>%s</b></td>%s'
                   '<td class="n">%s</td><td class="n"><b>%s</b></td>%s<td class="small">%s</td><td class="n">%s → %s</td>'
                   '<td class="n">%s → %s</td><td class="n">%s → %s</td></tr>' % (
                       esc((n or o).label()), esc(tid), fmt(of, 1), fmt(nf, 1), change_html(of, nf, True), fmt(om, 1),
                       fmt(nm, 1), change_html(om, nm, False), esc(', '.join(st)), fmt(g(o, 'frame', 'p95'), 1),
                       fmt(g(n, 'frame', 'p95'), 1), fmt(g(o, 'work', 'mean'), 1), fmt(g(n, 'work', 'mean'), 1),
                       fmt(g(o, 'gpu', 'mean'), 1), fmt(g(n, 'gpu', 'mean'), 1)))
    out.append('</tbody></table></div>')
    return ''.join(out)


def build_compare(old, new):
    out = ['<header><div class="head"><div><div class="muted small">reLCS Benchmark · comparaison</div>'
           '<h1>Comparaison avant / après</h1></div></div>'
           '<dl class="kv"><div><dt>Avant</dt><dd>%s</dd></div><div><dt>Après</dt><dd>%s</dd></div></dl>' % (
               esc(run_caption(old)), esc(run_caption(new)))]
    if txt(old.device.get('platform')) != txt(new.device.get('platform')):
        out.append('<div class="banner warn"><b>Plateformes différentes</b> : la comparaison n’a de sens que sur le même appareil.</div>')
    for r, lab in ((old, 'avant'), (new, 'après')):
        if r.partial or r.aborted:
            out.append('<div class="banner warn">Série « %s » %s.</div>' % (lab, 'partielle' if r.partial else 'abandonnée'))
    out.append('<p class="lead">Écarts de moins de %s considérés comme du bruit (≈). ▲ mieux, ▼ moins bien.</p></header>' %
               fmt_pct(NOISE_PCT))
    out.append('<h2 id="scores">Scores</h2><div class="tw"><table><thead><tr><th class="first">Score</th>'
               '<th class="n">Avant</th><th class="n">Après</th><th class="n">Écart</th></tr></thead><tbody>')
    for k, lab in (('overall', 'Global'), ('graphics', 'Graphismes'), ('cpu', 'Processeur')):
        o, n = old.scores.get(k), new.scores.get(k)
        out.append('<tr><td class="first">%s</td><td class="n">%s</td><td class="n"><b>%s</b></td>%s</tr>' % (
            lab, fmt(o, 0), fmt(n, 0), change_html(o, n, True)))
    out.append('</tbody></table></div>')
    pairs = compare_pairs(old, new)
    main = [p for p in pairs if (p[2] or p[1]).group != 'isolation']
    iso = [p for p in pairs if (p[2] or p[1]).group == 'isolation']
    out.append('<h2 id="tests">Tests</h2>' + compare_table(main))
    if iso:
        out.append('<details><summary>Tests d’isolation : %d tests</summary>%s</details>' % (len(iso), compare_table(iso)))
    # targets
    ot = {txt(t.get('what')): t for t in old.targets}
    nt = {txt(t.get('what')): t for t in new.targets}
    names = list(nt) + [k for k in ot if k not in nt]
    out.append('<h2 id="optimiser">À optimiser : évolution des cibles</h2>')
    if names:
        out.append('<div class="tw"><table><thead><tr><th class="first">Cible</th><th>Type</th><th class="n">Avant (ms)</th>'
                   '<th class="n">Après (ms)</th><th class="n">Écart (ms)</th><th class="n">Rang avant → après</th>'
                   '</tr></thead><tbody>')
        orank = {k: i + 1 for i, k in enumerate(ot)}
        nrank = {k: i + 1 for i, k in enumerate(nt)}
        for k in names:
            o, n = ot.get(k), nt.get(k)
            om, nm = num((o or {}).get('ms')), num((n or {}).get('ms'))
            cls, _ = change_class(om, nm, False)
            dl = (nm - om) if om is not None and nm is not None else None
            out.append('<tr><td class="first">%s</td><td class="small sec">%s</td><td class="n">%s</td><td class="n">%s</td>'
                       '<td class="n %s">%s</td><td class="n">%s → %s</td></tr>' % (
                           esc(target_name(new, k)[0]), esc(TARGET_KIND.get(txt((n or o).get('kind')), txt((n or o).get('kind')))),
                           fmt(om, 2) if o else 'absent', fmt(nm, 2) if n else 'disparu', cls, fmt_signed(dl, 2),
                           orank.get(k, DASH), nrank.get(k, DASH)))
        out.append('</tbody></table></div>')
    else:
        out.append('<p class="lead">Aucune cible dans les deux séries.</p>')
    # isolation deltas
    od = {(d['viewpoint'], d['variant']): d for d in old.deltas}
    nd = {(d['viewpoint'], d['variant']): d for d in new.deltas}
    keys = list(nd) + [k for k in od if k not in nd]
    out.append('<h2 id="isolation">Isolation : évolution des coûts</h2>')
    if keys:
        out.append('<p class="lead">Économie de chaque variante avant et après : un coût qui baisse indique que la fonction '
                   'est devenue moins chère.</p><div class="tw"><table><thead><tr><th class="first">Point de vue · variante</th>'
                   '<th class="n">Image avant → après</th><th class="n">Économie avant</th><th class="n">Économie après</th>'
                   '<th class="n">Écart (ms)</th></tr></thead><tbody>')
        for k in keys:
            o, n = od.get(k), nd.get(k)
            os_, ns_ = (o or {}).get('saved_ms'), (n or {}).get('saved_ms')
            dl = (ns_ - os_) if os_ is not None and ns_ is not None else None
            out.append('<tr><td class="first">%s · %s<span class="tid">%s</span></td><td class="n">%s → %s</td>'
                       '<td class="n">%s</td><td class="n"><b>%s</b></td><td class="n">%s</td></tr>' % (
                           esc(VIEWPOINT_LABEL.get(k[0], k[0])), esc(VARIANT_LABEL.get(k[1], k[1])), esc(k[1]),
                           fmt((o or {}).get('variant_ms'), 1), fmt((n or {}).get('variant_ms'), 1),
                           fmt_signed(os_, 2), fmt_signed(ns_, 2), fmt_signed(dl, 2)))
        out.append('</tbody></table></div>')
    else:
        out.append('<p class="lead">Aucun delta d’isolation dans les deux séries.</p>')
    out.append('<footer>Généré par tools/vita/bench-report.py --compare · avant : <code>%s</code> · après : <code>%s</code>'
               '</footer>' % (esc(old.path), esc(new.path)))
    return page('reLCS Benchmark · comparaison', '\n'.join(out))


def compare_text(old, new):
    L = ['reLCS Benchmark — comparaison', 'avant : ' + run_caption(old), 'après : ' + run_caption(new),
         '(écart de moins de %s = bruit, noté ~)' % plain(fmt_pct(NOISE_PCT)), '', 'Scores']
    for k, lab in (('overall', 'Global'), ('graphics', 'Graphismes'), ('cpu', 'Processeur')):
        o, n = old.scores.get(k), new.scores.get(k)
        L.append('  %-11s %8s -> %8s  %s' % (lab, fmt(o, 0), fmt(n, 0), change_text(o, n, True)))
    L.append('')
    L.append('Tests                                 FPS avant -> après              image avant -> après')
    for tid, o, n in compare_pairs(old, new):
        if (n or o).group == 'isolation':
            continue
        of = o.fps if o and o.ok else None
        nf = n.fps if n and n.ok else None
        om = o.frame['mean'] if o and o.ok else None
        nm = n.frame['mean'] if n and n.ok else None
        note = []
        for r, lab in ((o, 'avant'), (n, 'après')):
            if r is None:
                note.append('absent ' + lab)
            elif r.skipped:
                note.append('ignoré ' + lab)
            elif r.interrupted:
                note.append('interrompu ' + lab)
        L.append('  %-34s %6s -> %6s %-14s %6s -> %6s %-14s %s' % (
            tid[:34], fmt(of, 1), fmt(nf, 1), change_text(of, nf, True), fmt(om, 1), fmt(nm, 1),
            change_text(om, nm, False), ', '.join(note)))
    ot = {txt(t.get('what')): num(t.get('ms')) for t in old.targets}
    nt = {txt(t.get('what')): num(t.get('ms')) for t in new.targets}
    if ot or nt:
        L.append('')
        L.append('Cibles (ms)')
        for k in list(nt)[:8] + [k for k in ot if k not in nt][:4]:
            L.append('  %-30s %8s -> %8s' % (target_name(new, k)[0][:30], fmt(ot.get(k), 2) if k in ot else 'absent',
                                            fmt(nt.get(k), 2) if k in nt else 'disparu'))
    return plain('\n'.join(L))


# ---- main --------------------------------------------------------------------------

def write_out(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')
    return path


def out_default(run, name):
    return run.folder / name


def main(argv=None):
    ap = argparse.ArgumentParser(description='Rapport HTML d’une série reLCS Benchmark (results.json + frames.csv).')
    ap.add_argument('results', nargs='?', help='dossier de résultats ou results.json')
    ap.add_argument('-o', '--output', help='fichier HTML (défaut : <dossier>/report.html ou <NOUVEAU>/compare.html)')
    ap.add_argument('--compare', nargs=2, metavar=('ANCIEN', 'NOUVEAU'), help='comparer deux séries')
    ap.add_argument('--text', action='store_true', help='résumé texte (sans HTML, sauf si -o est donné)')
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(errors='replace')
    except (AttributeError, ValueError):
        pass
    try:
        if a.compare:
            old, new = Run(a.compare[0]), Run(a.compare[1])
            path = write_out(a.output or out_default(new, 'compare.html'), build_compare(old, new))
            print(compare_text(old, new))
            print('\nComparaison HTML : %s' % path)
            return 0
        if not a.results:
            ap.error('dossier de résultats manquant (ou --compare ANCIEN NOUVEAU)')
        run = Run(a.results)
        if a.text:
            print(text_summary(run))
        if not a.text or a.output:
            path = write_out(a.output or out_default(run, 'report.html'), build_report(run))
            print('Rapport HTML : %s' % path)
        return 0
    except ReportError as e:
        print('bench-report : %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
