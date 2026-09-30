#!/usr/bin/env python3
"""Host test of the reLCS Benchmark report tool (tools/vita/bench-report.py).

Generates synthetic result folders with tools/vita/test_support/bench_sample.py
(full Vita run, a second run for --compare, a PC run without GPU timing nor
sections, a partial run) plus hand-written minimal and malformed files, runs
the report on each and checks: exit codes, self-contained HTML (no http(s)://,
no external src/href), well-formed markup (html.parser tag balance, unique ids,
resolvable #anchors), key sections, every test id, escaping of hostile strings,
and the --text and --compare console summaries.
Usage: python tools/vita/check-bench-report.py (Python 3.10+, standard library)
"""
from html.parser import HTMLParser
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / 'tools/vita/bench-report.py'
SAMPLE = ROOT / 'tools/vita/test_support/bench_sample.py'
VOID = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'source', 'track', 'wbr'}
SECTIONS = ['Scores', 'À optimiser en priorité', 'Tests', 'Budget d’image', 'Temps d’image', 'Isolation',
            'Charge par image', 'Sections du profileur', 'Threads et mémoire', 'Méthode']


class Markup(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.errors, self.ids, self.anchors, self.external, self.text = [], [], set(), [], [], []

    def _attrs(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a:
            if a['id'] in self.ids:
                self.errors.append('duplicate id %r' % a['id'])
            self.ids.add(a['id'])
        for k in ('src', 'href', 'xlink:href'):
            v = a.get(k)
            if v is None:
                continue
            if v.startswith('#'):
                self.anchors.append(v[1:])
            else:
                self.external.append('<%s %s="%s">' % (tag, k, v))

    def handle_starttag(self, tag, attrs):
        self._attrs(tag, attrs)
        if tag not in VOID:
            self.stack.append((tag, self.getpos()))

    def handle_startendtag(self, tag, attrs):
        self._attrs(tag, attrs)

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack:
            self.errors.append('</%s> without an open tag at %s' % (tag, self.getpos()))
            return
        top, pos = self.stack.pop()
        if top != tag:
            self.errors.append('</%s> at %s closes <%s> opened at %s' % (tag, self.getpos(), top, pos))

    def handle_data(self, data):
        if not self.stack or self.stack[-1][0] not in ('script', 'style'):
            self.text.append(data)


def fail(msg):
    print('FAIL:', msg)
    sys.exit(1)


def run(args, env=None, expect=0):
    e = dict(os.environ)
    e['PYTHONIOENCODING'] = 'utf-8'
    if env:
        e.update(env)
    p = subprocess.run([sys.executable] + [str(a) for a in args], capture_output=True, env=e)
    out = p.stdout.decode('utf-8', 'replace')
    err = p.stderr.decode('utf-8', 'replace')
    if p.returncode != expect:
        fail('%s -> exit %d (expected %d)\n%s\n%s' % (' '.join(map(str, args)), p.returncode, expect, out[-3000:], err[-3000:]))
    return out, err


def check_html(path, must=(), ids=(), must_not=()):
    if not path.is_file():
        fail('%s not written' % path)
    raw = path.read_text(encoding='utf-8')
    low = raw.lower()
    if 'http://' in low or 'https://' in low:
        fail('%s references http(s)://' % path)
    for bad in ('<link', '@import', 'url('):
        if bad in low:
            fail('%s contains external-resource syntax %r' % (path, bad))
    m = Markup()
    m.feed(raw)
    m.close()
    if m.stack:
        m.errors.append('unclosed tags: %s' % ', '.join('<%s>@%s' % t for t in m.stack[-5:]))
    if m.external:
        m.errors.append('external references: %s' % ', '.join(m.external[:5]))
    missing = [a for a in m.anchors if a not in m.ids]
    if missing:
        m.errors.append('anchors without target: %s' % ', '.join(sorted(set(missing))[:5]))
    if m.errors:
        fail('%s markup:\n  %s' % (path, '\n  '.join(m.errors[:12])))
    # French typography puts narrow no-break spaces before : ; ! ?
    text = ''.join(m.text).replace(' ', ' ').replace('\xa0', ' ')
    for s in must:
        if s not in text:
            fail('%s: missing text %r' % (path, s))
    for i in ids:
        if i not in raw:
            fail('%s: test id %r missing' % (path, i))
    for s in must_not:
        if s in raw:
            fail('%s: must not contain %r' % (path, s))
    return raw, text


def test_ids(folder):
    data = json.loads((folder / 'results.json').read_text(encoding='utf-8'))
    return [t['id'] for t in data['tests']]


def main():
    tmp = Path(tempfile.mkdtemp(prefix='bench-report-'))
    try:
        a, b, pc, part = tmp / 'vita_a', tmp / 'vita_b', tmp / 'pc', tmp / 'partial'
        run([SAMPLE, a])
        run([SAMPLE, b, '--variant', '1'])
        run([SAMPLE, pc, '--profile', 'pc'])
        run([SAMPLE, part, '--partial'])
        ids = test_ids(a)
        if len(ids) < 80 or not (a / 'frames.csv').is_file():
            fail('sample generator: %d tests' % len(ids))
        print('PASS: sample generator (%d tests, frames.csv, PC, partial)' % len(ids))

        # full Vita report
        out, _ = run([REPORT, a])
        raw, text = check_html(a / 'report.html', SECTIONS + ['Montée en charge de la foule', 'Streaming',
                               'Limité par le remplissage', 'moindres carrés', 'Téléportations',
                               'Point de vue A · Chinatown', 'Point de vue B · Bedford Point', '⚠ interrompu',
                               'capacite absente (flat_tex)'], ids)
        charts = raw.count('class="lc"')
        if charts < 17:
            fail('only %d frame-time charts' % charts)
        if len(raw) > 4 * 1024 * 1024:
            fail('report too large: %d bytes' % len(raw))
        for tag in ('<polyline', '<svg', 'prefers-color-scheme:dark', 'class="dv"', 'class="bstack"', '<details'):
            if tag not in raw:
                fail('report lacks %r' % tag)
        print('PASS: Vita report, %d KiB, %d charts, all %d test ids, sections, interpretation' % (
            len(raw.encode('utf-8')) // 1024, charts, len(ids)))

        # -o and --text
        custom = tmp / 'out' / 'custom.html'
        run([REPORT, a / 'results.json', '-o', custom])
        check_html(custom, SECTIONS, ids)
        c = tmp / 'text_only'
        shutil.copytree(a, c)
        (c / 'report.html').unlink()
        out, _ = run([REPORT, c, '--text'])
        if (c / 'report.html').exists():
            fail('--text alone wrote report.html')
        for s in ['Scores', 'Graphismes', 'Processeur', 'Global', 'À optimiser en priorité', 'Isolation',
                  'interrompu'] + [i for i in ids if not i.startswith('iso_')]:
            if s not in out:
                fail('--text output lacks %r' % s)
        out, _ = run([REPORT, c, '--text'], env={'PYTHONIOENCODING': 'cp1252'})
        run([REPORT, c, '--text', '-o', c / 'both.html'])
        check_html(c / 'both.html', SECTIONS)
        print('PASS: -o path, --text summary (UTF-8 and cp1252 console), --text with -o')

        # compare
        out, _ = run([REPORT, '--compare', a, b])
        raw, _ = check_html(b / 'compare.html', ['Comparaison avant / après', 'Scores', 'Tests',
                                                  'À optimiser : évolution des cibles', 'Isolation : évolution des coûts',
                                                  '⚠ interrompu avant'], ids)
        if 'class="n better"' not in raw or 'class="n same"' not in raw:
            fail('compare lacks better/noise colouring')
        for s in ['Scores', 'mieux', '~', 'g1_portland_el', 'Cibles', 'interrompu avant']:
            if s not in out:
                fail('compare text lacks %r' % s)
        run([REPORT, '--compare', a / 'results.json', pc / 'results.json', '-o', tmp / 'cmp_pc.html'])
        check_html(tmp / 'cmp_pc.html', ['Plateformes différentes'])
        print('PASS: --compare HTML and text (better/worse/noise, targets, isolation deltas, cross-platform)')

        # PC run: no GPU, no sections, caps []
        run([REPORT, pc])
        check_html(pc / 'report.html', SECTIONS + ['Temps GPU non mesuré', 'Tests de viewport indisponibles',
                                                    'Aucune section enregistrée', 'non détaillé'], test_ids(pc))
        print('PASS: PC-like run (no GPU timing, no sections, no counters)')

        # partial run: results.json only
        run([REPORT, part])
        check_html(part / 'report.html', ['Résultats partiels', 'frames.csv absent', 'Scores non valides',
                                          'Cibles calculées par le rapport'], test_ids(part))
        print('PASS: partial run without frames.csv')

        # Loops=2: the same test id twice, frames.csv segments restart at frame 0
        lp = tmp / 'loops'
        shutil.copytree(a, lp)
        (lp / 'report.html').unlink()
        data = json.loads((lp / 'results.json').read_text(encoding='utf-8'))
        data['tests'].insert(1, json.loads(json.dumps(data['tests'][0])))
        (lp / 'results.json').write_text(json.dumps(data), encoding='utf-8')
        lines = (lp / 'frames.csv').read_text(encoding='utf-8').splitlines()
        g1 = [l for l in lines[1:] if l.startswith('g1_portland_el,')]
        (lp / 'frames.csv').write_text('\n'.join(lines + g1) + '\n', encoding='utf-8')
        run([REPORT, lp])
        raw, _ = check_html(lp / 'report.html', SECTIONS, ids)
        if raw.count('class="lc"') != charts + 1:
            fail('Loops=2: expected %d charts, got %d' % (charts + 1, raw.count('class="lc"')))
        print('PASS: repeated test id (Loops=2): one chart per run, unique anchors')

        # hand-written minimal / hostile file
        m = tmp / 'minimal'
        m.mkdir()
        nan = float('nan')
        data = {'schema': 'relcs-bench/1', 'partial': True, 'device': None, 'scores': None, 'settings': 'Mode=quick;Seed=1',
                'caps': [], 'tests': [
                    {'id': 'g1_portland_el', 'group': 'graphics', 'frames': 0, 'skipped': True, 'skip_reason': '<b>x</b>'},
                    {'id': 'c1_crowd_d1', 'group': 'cpu', 'fps': nan, 'frame_ms': {'mean': None}},
                    {'id': 'c1_crowd_d2', 'group': 'cpu', 'fps': 'abc', 'frames': 3, 'frame_ms': {'mean': 20, 'p95': '25'},
                     'per_frame': {'peds': 4, 'vehicles': 'x'}, 'sections': [
                         {'name': 'A', 'parent': 7, 'depth': 'x', 'self_ms': None}, {'name': '<i>B</i>', 'parent': 0}],
                     'threads': 'bad', 'memory': [], 'cpu_load_pct': [None, nan], 'groups_ms': {'sim': float('inf')}},
                    {'id': 'iso_a_chinatown_baseline', 'group': 'isolation', 'viewpoint': 'a_chinatown', 'variant': 'baseline',
                     'frames': 90, 'frame_ms': {'mean': 30}, 'work_ms': {'mean': 20}},
                    {'id': 'iso_a_chinatown_no_peds', 'group': 'isolation', 'viewpoint': 'a_chinatown', 'variant': 'no_peds',
                     'frames': 90, 'frame_ms': {'mean': 28}},
                    {'id': 's2_tp_bedford', 'group': 'streaming', 'kind': 'teleport', 'frames': 10, 'fps': 20,
                     'load_ms': None, 'settle_ms': 1200, 'totals': None},
                    'not a test'],
                'deltas': [{'viewpoint': 'a_chinatown', 'variant': 'no_peds', 'saved_ms': None}, 5],
                'targets': [{'what': '<script>alert(1)</script>', 'ms': '3.5', 'kind': 'section'}, {'ms': nan}]}
        (m / 'results.json').write_text(json.dumps(data), encoding='utf-8')
        (m / 'frames.csv').write_text('test,frame,frame_ms,flags\nc1_crowd_d2,0,20.5,0\nc1_crowd_d2,1,bad,\n'
                                      'c1_crowd_d2,2,19.0\n,,\nunknown,0,5,16\n', encoding='utf-8')
        run([REPORT, m])
        check_html(m / 'report.html', ['Scores', 'À optimiser en priorité', 'Résultats partiels'],
                   ['g1_portland_el', 'c1_crowd_d1', 'c1_crowd_d2'],
                   must_not=['<script>alert(1)', '<b>x</b>', '<i>B</i>'])
        run([REPORT, m, '--text'])
        e = tmp / 'empty'
        e.mkdir()
        (e / 'results.json').write_text('{"schema":"relcs-bench/1"}', encoding='utf-8')
        run([REPORT, e])
        check_html(e / 'report.html', ['Aucun test dans ce fichier'])
        run([REPORT, '--compare', e, m])
        check_html(m / 'compare.html', ['Comparaison avant / après'])
        print('PASS: minimal, malformed and hostile results (NaN, wrong types, bad CSV rows, escaping)')

        # errors
        run([REPORT, tmp / 'does-not-exist'], expect=2)
        bad = tmp / 'bad'
        bad.mkdir()
        (bad / 'results.json').write_text('{"schema": "relcs-bench/1", "tests": [', encoding='utf-8')
        run([REPORT, bad], expect=2)
        (bad / 'results.json').write_text('[1, 2]', encoding='utf-8')
        run([REPORT, bad], expect=2)
        print('PASS: missing or invalid results.json -> exit 2 with a message')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('PASS: bench-report.py')


if __name__ == '__main__':
    main()
