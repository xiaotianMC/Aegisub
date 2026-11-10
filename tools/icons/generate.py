#!/usr/bin/env python3
# Copyright (c) 2026, Aegisub contributors
# SPDX-License-Identifier: ISC
"""Writes src/libresrc/icons_modern.cpp, the monochrome icons (docs/icons.md).

    tools/icons/generate.py path/to/lucide-static/icons

The icons are on Lucide's 24x24 grid with a stroke of 2. Those in LUCIDE and
COMPOSITE come from Lucide's SVG files (the lucide-static npm package, 1.70.0 when written; ISC
licence: src/libresrc/LICENSE.lucide); CUSTOM are drawn here in the same style.
Paths are written with absolute commands, which the pixel hinting in
src/libresrc/libresrc.cpp reads; Hinter below does the same hinting, for
previews.
"""
import math, os, re, sys

# --- SVG bodies as data: parsed, made absolute, moved, hinted, written
NUM = r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?'
ARGS = dict(M=2, L=2, H=1, V=1, C=6, S=4, Q=4, T=2, A=7, Z=0)

def fmt(v):
    s = f'{v:.3f}'.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s

def parse_path(d):
    """[(cmd, [args])] absolute; H/V kept. Arc flags may be packed ("011.4")."""
    out = []; pos = 0; cmd = None; x = y = sx = sy = 0
    numre = re.compile(r'[\s,]*(' + NUM + ')')
    def skip():
        nonlocal pos
        while pos < len(d) and d[pos] in ' ,\t\n': pos += 1
    def number():
        nonlocal pos
        m = numre.match(d, pos); pos = m.end(); return float(m.group(1))
    def flag():
        nonlocal pos
        skip(); v = float(d[pos]); pos += 1; return v
    while True:
        skip()
        if pos >= len(d): break
        if d[pos].isalpha(): cmd = d[pos]; pos += 1
        elif cmd is None: raise ValueError(d)
        C = cmd.upper(); rel = cmd != C; n = ARGS[C]
        if C == 'Z':
            out.append(('Z', [])); x, y = sx, sy; continue
        a = [flag() if C == 'A' and k in (3, 4) else number() for k in range(n)]
        if C == 'H':
            nx = a[0] + (x if rel else 0); out.append(('H', [nx])); x = nx
        elif C == 'V':
            ny = a[0] + (y if rel else 0); out.append(('V', [ny])); y = ny
        elif C == 'A':
            ex, ey = a[5] + (x if rel else 0), a[6] + (y if rel else 0)
            out.append(('A', a[:5] + [ex, ey])); x, y = ex, ey
        else:
            pts = []
            for k in range(0, n, 2):
                pts += [a[k] + (x if rel else 0), a[k + 1] + (y if rel else 0)]
            out.append((C, pts)); x, y = pts[-2], pts[-1]
            if C == 'M': sx, sy = x, y; cmd = 'l' if rel else 'L'
    return out

def write_path(cmds):
    s = ''
    for c, a in cmds:
        s += c
        for k, v in enumerate(a):
            t = fmt(v)
            if k and not t.startswith('-'): s += ' '
            s += t
    return s

ELEM = re.compile(r'<(path|circle|ellipse|rect|line|polyline|polygon|g|/g)\b([^>]*?)(/?)>')
def attrs(s): return dict(re.findall(r'([\w-]+)\s*=\s*["\']([^"\']*)["\']', s))
def parse(body):
    """[(tag, attrs dict)] with line/polyline/polygon as paths."""
    out = []
    for m in ELEM.finditer(body):
        tag, a = m.group(1), attrs(m.group(2))
        if tag == 'line':
            a['d'] = f"M{a.pop('x1')} {a.pop('y1')}L{a.pop('x2')} {a.pop('y2')}"; tag = 'path'
        elif tag in ('polyline', 'polygon'):
            nums = re.findall(NUM, a.pop('points'))
            pts = [f'{nums[k]} {nums[k+1]}' for k in range(0, len(nums), 2)]
            a['d'] = 'M' + 'L'.join(pts) + ('Z' if tag == 'polygon' else ''); tag = 'path'
        out.append((tag, a))
    return out

def write(elems):
    s = ''
    for tag, a in elems:
        if tag == '/g': s += '</g>'; continue
        at = ''.join(f" {k}='{v}'" for k, v in a.items())
        s += f'<{tag}{at}>' if tag == 'g' else f'<{tag}{at}/>'
    return s

def transform(elems, k, dx=0, dy=0):
    """Uniform scale by k then move: x' = k x + dx."""
    X = lambda v: k * v + dx; Y = lambda v: k * v + dy
    out = []
    for tag, a in elems:
        a = dict(a)
        if tag == 'path':
            cmds = []
            for c, v in parse_path(a['d']):
                if c == 'H': v = [X(v[0])]
                elif c == 'V': v = [Y(v[0])]
                elif c == 'A': v = [v[0] * k, v[1] * k, v[2], v[3], v[4], X(v[5]), Y(v[6])]
                else: v = [X(t) if j % 2 == 0 else Y(t) for j, t in enumerate(v)]
                cmds.append((c, v))
            a['d'] = write_path(cmds)
        elif tag in ('circle', 'ellipse'):
            a['cx'] = fmt(X(float(a['cx']))); a['cy'] = fmt(Y(float(a['cy'])))
            for r in ('r', 'rx', 'ry'):
                if r in a: a[r] = fmt(float(a[r]) * k)
        elif tag == 'rect':
            a['x'] = fmt(X(float(a.get('x', 0)))); a['y'] = fmt(Y(float(a.get('y', 0))))
            for r in ('width', 'height', 'rx', 'ry'):
                if r in a: a[r] = fmt(float(a[r]) * k)
        elif tag == 'g' and 'transform' in a:
            a['transform'] = re.sub(r'translate\(([^)]*)\)', lambda m: 'translate(' + ' '.join(fmt(float(t) * k) for t in re.findall(NUM, m.group(1))) + ')', a['transform'])
        if 'stroke-width' in a: a['stroke-width'] = fmt(float(a['stroke-width']) * k)
        out.append((tag, a))
    return out

class Hinter:
    """As libresrc.cpp: a whole number of pixels for the stroke, lines on the grid."""
    def __init__(self, px, grid=24):
        self.s = px / grid; self.w = max(1, math.floor(px / 16 * 1.25 + 0.5)); self.grid = grid; self.cur = self.w
    def on_grid(self, v, centres):
        # Rounded the same way on both sides of the icon's middle, so a
        # shape symmetric about it stays symmetric
        middle = 12 * self.s
        g = math.floor(middle + 1e-6) + 0.5 if centres else math.floor(middle + 0.5 + 1e-6)
        off = v * self.s - middle
        steps = -math.floor(-off + 0.5 + 1e-6) if off < 0 else math.floor(off + 0.5 + 1e-6)
        return (g + steps) / self.s
    def line(self, v): return self.on_grid(v, self.cur % 2)
    def whole(self, v): return self.on_grid(v, False)
    def radius(self, r): return r if r * self.s < 1 else round(r * self.s) / self.s  # a tiny one as drawn
    def stroke_units(self): return self.w / self.s
    def path(self, d):
        # Points on the grid; a point very close to the last one (a dot drawn
        # as a zero-length line, a tiny tick) moves with it instead, so it
        # neither vanishes nor grows.
        cmds = []; mx = my = 0; ox = oy = 0
        def near(x, y): return abs(x - ox) < 0.75 and abs(y - oy) < 0.75 and cmds
        def snap(x, y):
            nonlocal mx, my
            if near(x, y): return x + mx, y + my
            nx, ny = self.line(x), self.line(y); mx, my = nx - x, ny - y; return nx, ny
        for c, v in parse_path(d):
            if c in 'MLT':
                n = snap(v[0], v[1]); ox, oy = v[0], v[1]; v = list(n)
            elif c == 'H':
                n = snap(v[0], oy); ox = v[0]; v = [n[0]]
            elif c == 'V':
                n = snap(ox, v[0]); oy = v[0]; v = [n[1]]
            elif c == 'A':
                # Radii change as the distance between the ends did, so the arc keeps its shape
                fx, fy, wx, wy = ox + mx, oy + my, ox, oy
                n = snap(v[5], v[6]); before = math.hypot(v[5] - wx, v[6] - wy); after = math.hypot(n[0] - fx, n[1] - fy)
                k = after / before if before > 0.1 else 1
                ox, oy = v[5], v[6]; v = [v[0] * k, v[1] * k] + v[2:5] + list(n)
            elif c == 'C':
                pmx, pmy = mx, my
                n = snap(v[4], v[5]); ox, oy = v[4], v[5]
                v = [v[0] + pmx, v[1] + pmy, v[2] + mx, v[3] + my, n[0], n[1]]
            elif c in 'SQ':
                n = snap(v[2], v[3]); ox, oy = v[2], v[3]
                v = [v[0] + mx, v[1] + my, n[0], n[1]]
            cmds.append((c, v))
        return write_path(cmds)
    def elems(self, elems):
        out = []
        for tag, a in elems:
            a = dict(a)
            self.cur = self.w
            if 'stroke-width' in a:
                # Its own weight, in whole pixels too, a pixel more or less than the stroke
                own = float(a['stroke-width']) * self.s
                self.cur = max(1, math.floor(own + 0.5)) if own < self.w else max(self.w + 1, math.floor(own + 0.5)) if own > self.w * 1.2 else self.w
                a['stroke-width'] = fmt(self.cur / self.s)
            if tag != 'g' and tag != '/g':
                stroked = a.get('stroke') != 'none'
                if tag == 'path': a['d'] = self.path(a['d'])
                elif tag == 'circle' and not stroked:
                    # A dot: a whole number of pixels across, at least the
                    # stroke, centred on a pixel centre or edge to match
                    dia = max(self.w, round(2 * float(a['r']) * self.s))
                    c = lambda v: self.on_grid(v, dia % 2)
                    cx, cy, r = c(float(a['cx'])), c(float(a['cy'])), dia / 2 / self.s
                    if dia <= 2:  # Too small to antialias as a circle: a square of pixels
                        tag = 'rect'; a = {k: v for k, v in a.items() if k not in ('cx', 'cy', 'r')}
                        a.update(x=fmt(cx - r), y=fmt(cy - r), width=fmt(2 * r), height=fmt(2 * r))
                    else:
                        a['cx'] = fmt(cx); a['cy'] = fmt(cy); a['r'] = fmt(r)
                elif tag in ('circle', 'ellipse') and stroked:
                    a['cx'] = fmt(self.line(float(a['cx']))); a['cy'] = fmt(self.line(float(a['cy'])))
                    for r in ('r', 'rx', 'ry'):
                        if r in a: a[r] = fmt(self.radius(float(a[r])))
                elif tag == 'rect':
                    f = self.line if stroked else self.whole
                    x, y, w, h = (float(a.get(k, 0)) for k in ('x', 'y', 'width', 'height'))
                    x0, y0, x1, y1 = f(x), f(y), f(x + w), f(y + h)
                    a.update(x=fmt(x0), y=fmt(y0), width=fmt(x1 - x0), height=fmt(y1 - y0))
            out.append((tag, a))
        return out

def svg(elems, colour, px, hint=True, grid=24):
    h = Hinter(px, grid)
    body = write(h.elems(elems) if hint else elems).replace('@', colour)
    sw = h.stroke_units() if hint else 2 * grid / 24
    return (f"<svg xmlns='http://www.w3.org/2000/svg' width='{grid}' height='{grid}' viewBox='0 0 {grid} {grid}'>"
            f"<g fill='none' stroke='{colour}' stroke-width='{fmt(sw)}' stroke-linecap='round' stroke-linejoin='round'>{body}</g></svg>")


# --- Aegisub's own icons
def p(d, extra=''): return f"<path d='{d}'{extra}/>"
def fill(d): return f"<path d='{d}' fill='@'/>"
def dot(x, y, r=1.2): return f"<circle cx='{x}' cy='{y}' r='{r}' fill='@' stroke='none'/>"
def rect(x, y, w, h, rx=2, extra=''): return f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}'{extra}/>"
def bar(x, y, w, h): return rect(x, y, w, h, 1, " fill='@' stroke='none'")
def lb(x, w=3, y0=4, y1=20): return p(f'M{x+w} {y0}H{x}V{y1}H{x+w}')   # [
def rb(x, w=3, y0=4, y1=20): return p(f'M{x-w} {y0}H{x}V{y1}H{x-w}')   # ]
def tri_r(x0, x1, cy=12, h=5): return fill(f'M{x0} {cy-h}L{x1} {cy}L{x0} {cy+h}Z')
def arrow_r(x0, x1, y=12, h=4): return p(f'M{x0} {y}H{x1}') + p(f'M{x1-h} {y-h}L{x1} {y}L{x1-h} {y+h}')
def arrow_l(x0, x1, y=12, h=4): return p(f'M{x1} {y}H{x0}') + p(f'M{x0+h} {y-h}L{x0} {y}L{x0+h} {y+h}')
def arrow_d(x, y0, y1, h=3): return p(f'M{x} {y0}V{y1}') + p(f'M{x-h} {y1-h}L{x} {y1}L{x+h} {y1-h}')
def playhead(x, top=2): return p(f'M{x} {top+6}V21') + fill(f'M{x-3.5} {top}H{x+3.5}V{top+3}L{x} {top+6.5}L{x-3.5} {top+3}Z')
def node(cx, cy): return rect(cx - 2.5, cy - 2.5, 5, 5, 1)
pencil = p('M18.5 2.5A2.12 2.12 0 0 1 21.5 5.5L13 14L9 15L10 11Z')
lens = "<circle cx='9.5' cy='9.5' r='6.5'/>" + p('M14.2 14.2L16.5 16.5')
A_ = p('M2 15.5L6.5 3L11 15.5M3.7 11.5H9.3')
B_ = p('M13.5 3V15.5H18.6A3.15 3.15 0 0 0 18.6 9.2H13.5M13.5 3H18.1A3.1 3.1 0 0 1 18.1 9.2')
AB = A_ + B_
screen = rect(2, 4, 20, 15)
shape_rect = rect(2, 3.5, 17.5, 14.5)

CUSTOM = {
 # audio toolbar: play ranges, brackets are the line's start and end
 'button_playsel': lb(3) + rb(21) + tri_r(9.5, 15.5, h=4.5),
 'button_playline': tri_r(7, 17, cy=10, h=6) + p('M4 20.5H20'),
 'button_playfivehbefore': dot(3, 12) + dot(7, 12) + tri_r(10.5, 16, h=4.5) + lb(19),
 'button_playfivehafter': rb(5) + tri_r(8, 13.5, h=4.5) + dot(17, 12) + dot(21, 12),
 'button_playfirstfiveh': lb(3) + tri_r(8, 13.5, h=4.5) + p('M16 12H21M21 8V16'),
 'button_playlastfiveh': p('M3 8V16M3 12H8') + tri_r(10.5, 16, h=4.5) + rb(21),
 'button_playtoend': tri_r(3, 9.5, h=5.5) + arrow_r(12.5, 21.5, h=3.5),
 'button_leadin': arrow_l(2, 14, h=3.5) + lb(17.5, 3.5),
 'button_leadout': rb(6.5, 3.5) + arrow_r(10, 22, h=3.5),
 'toggle_audio_autocommit': p('M21 12A9 9 0 1 1 12 3C14.52 3 16.93 4 18.74 5.74L21 8') + p('M21 3V8H16') + p('M8.5 12.5L11 15L16 10'),
 'toggle_audio_nextcommit': p('M2 12.5L6.5 17L13 8.5') + arrow_d(19, 4, 20, 3.5),
 'toggle_audio_autoscroll': lb(2.5) + rb(21.5) + arrow_r(7.5, 16.5, h=3.5),
 'toggle_audio_spectrum': p('M4 14V20M8 6V20M12 10.5V20M16 3V20M20 14.5V20'),
 # main toolbar
 'video_to_substart': arrow_r(2, 13.5, h=3.5) + lb(17.5, 3.5),
 'video_to_subend': arrow_r(2, 13.5, h=3.5) + rb(21.5, 3.5),
 'substart_to_video': playhead(6.5) + p('M6.5 12H14M6.5 21H14'),
 'subend_to_video': playhead(17.5) + p('M17.5 12H10M17.5 21H10'),
 'snap_subs_to_scene': p('M2.5 3V21M21.5 3V21') + rect(7, 8.5, 10, 7) + p('M5 12H2.5M19 12H21.5'),
 'shift_to_frame': playhead(5) + rect(9.5, 10, 12.5, 8) + p('M18.5 14H13.5M15.5 12L13.5 14L15.5 16'),
 'font_collector_button': p('M3 4H14M8.5 4V20') + arrow_d(18.5, 5, 16, 3) + p('M15 20.5H22'),
 'shift_times_toolbutton': rect(7, 3, 10, 7) + p('M2 17H22M5 14L2 17L5 20M19 14L22 17L19 20'),
 'kara_timing_copier': ''.join(rect(x, y, 5, 5, 1) for y in (2, 17) for x in (2, 9.5, 17)) + arrow_d(12, 9, 15, 2.5),
 # visual typesetting tools
 'visual_rotatexy': "<ellipse cx='12' cy='13.5' rx='9.5' ry='4'/>" + p('M12 2V22') + fill('M17.5 6.5L21.5 9.2L17 11Z'),
 'visual_scale': rect(2, 12, 10, 10) + p('M12 12L21 3M14.5 3H21V9.5'),
 # scroll effect: the area's two edges, the text going up between them
 'visual_scroll': p('M2 3H22M2 21H22') + p('M12 17.5V7') + p('M8 11L12 7L16 11'),
 'visual_vector_clip': p('M4.5 17C6 7 18 17 19.5 7') + node(4.5, 19.5) + node(19.5, 4.5),
 'visual_vector_clip_drag': node(12, 12) + p('M12 2V7M12 17V22M2 12H7M17 12H22')
    + p('M9.5 4.5L12 2L14.5 4.5M9.5 19.5L12 22L14.5 19.5M4.5 9.5L2 12L4.5 14.5M19.5 9.5L22 12L19.5 14.5'),
 'visual_vector_clip_line': node(4.5, 19.5) + node(19.5, 4.5) + p('M7 17L17 7'),
 'visual_vector_clip_bicubic': node(4.5, 19.5) + node(19.5, 4.5) + p('M7 19.5C17 19.5 7 4.5 17 4.5') + p('M7 19.5H13M17 4.5H11') + dot(13.5, 19.5, 1.5) + dot(10.5, 4.5, 1.5),
 'visual_vector_clip_convert': p('M4 3V21') + p('M19 3C15.5 9 22.5 15 19 21') + p('M8 12H15M10 10L8 12L10 14M13 10L15 12L13 14'),
 'visual_vector_clip_insert': p('M2 21.5C4.5 11.5 19.5 11.5 22 21.5') + node(12, 14) + p('M12 2V8M9 5H15'),
 'visual_vector_clip_remove': p('M2 21.5C4.5 11.5 19.5 11.5 22 21.5') + node(12, 14) + p('M9 5H15'),
 'visual_vector_clip_freehand': pencil + p('M2 21L5 16.5L8 21L11 17'),
 'visual_vector_clip_freehand_smooth': pencil + p('M2 20.5C3.5 15.5 6 15.5 7 18.5S10 22 11.5 17.5'),
 # help: a question mark alone, as large as the icon allows; inside
 # Lucide's circle it was too small to read at 16 px
 'visual_help': p('M7.5 8.5A4.5 4.5 0 1 1 14.25 12.4C12.9 13.2 12 14.2 12 15.5') + dot(12, 20, 1.5),
 'visual_move_conv_move': "<circle cx='5' cy='12' r='3'/>" + arrow_r(10.5, 22),
 'visual_move_conv_pos': p('M20 10C20 14.993 14.461 20.193 12.601 21.799A1 1 0 0 1 11.399 21.799C9.539 20.193 4 14.993 4 10A8 8 0 0 1 20 10') + "<circle cx='12' cy='10' r='3'/>",
 # shapes: room at the bottom right for the flyout mark
 'shape_rectangle': shape_rect,
 'shape_ellipse': "<ellipse cx='11' cy='11' rx='9' ry='7.5'/>",
 'shape_triangle': p('M11 3L20 18.5H2Z'),
 'shape_polygon': p('M2 3.5H20L13 11L20 18.5H2Z'),
 'visual_rectangle': shape_rect,
 # edit box
 'button_bold': p('M6 12H15A4 4 0 0 1 15 20H6V4H14A4 4 0 0 1 14 12', " stroke-width='3'"),
 'button_fontname': p('M12 4.5C11 3.5 8 3.2 8 6.5V20M5 10H11.5') + p('M15 10V20M15 14C15 11.5 16.5 10 18.5 10S22 11.5 22 14V20'),
 'button_color_one': AB + bar(2, 18, 9.5, 3.5),
 'button_color_two': AB + bar(12.5, 18, 9.5, 3.5),
 'button_color_three': AB + rect(2.5, 18, 19, 3, 1, " stroke-width='1.5'"),
 'button_color_four': f"<g opacity='.4' transform='translate(1.5 1.5)'>{AB}</g>" + AB + bar(2, 18.5, 20, 3.5),
 # menus
 'open_audio_from_video_menu': rect(2, 3, 15, 12) + fill('M7.5 6.5L12 9L7.5 11.5Z') + p('M18 16.5A3 3 0 0 1 18 21.5M20.5 14A6 6 0 0 1 20.5 22.5'),
 'use_dummy_video_menu': screen + p('M6 15.5L18 7.5'),
 'show_video_details_menu': screen + p('M12 11V15.5') + dot(12, 8, 1.2),
 'find_next_menu': lens + arrow_r(14.5, 22, y=20, h=3),
 'find_replace_menu': lens + p('M15 22.5V21A3 3 0 0 1 18 18H22M20 16L22 18L20 20'),
 'open_with_toolbutton': p('M12 17H4A2 2 0 0 1 2 15V5A2 2 0 0 1 4 3H8L10 5.5H17A2 2 0 0 1 19 7.5V11') + p('M14.5 22L18 13L21.5 22M15.7 19H20.3'),
 'save_as_toolbutton': p('M11 18H4A2 2 0 0 1 2 16V4A2 2 0 0 1 4 2H13L17 6V10') + p('M6 2V6H12') + p('M20 12.5A1.77 1.77 0 0 1 22.5 15L17 20.5L13.5 21.5L14.5 18Z'),
}

# The style panel's alignment buttons: text lines against the edges, the
# edge they keep to drawn solid, the other faint
CUSTOM.update({
 'align_top': p('M3 3H21') + p('M3 21H21', " opacity='.4'") + p('M7.5 8H16.5M7.5 12.5H16.5'),
 'align_middle': p('M3 3H21M3 21H21', " opacity='.4'") + p('M7.5 9.75H16.5M7.5 14.25H16.5'),
 'align_bottom': p('M3 3H21', " opacity='.4'") + p('M3 21H21') + p('M7.5 11.5H16.5M7.5 16H16.5'),
})


# --- From Lucide
LUCIDE = {
 # audio
 'button_prev': 'skip-back', 'button_next': 'skip-forward', 'button_stop': 'square',
 'button_audio_commit': 'check', 'button_audio_goto': 'locate-fixed', 'toggle_audio_medusa': 'keyboard', 'kara_mode': 'mic',
 # main
 'new_toolbutton': 'file', 'open_toolbutton': 'folder-open', 'save_toolbutton': 'save',
 'jumpto_button': 'arrow-right-to-line', 'zoom_in_button': 'zoom-in', 'zoom_out_button': 'zoom-out',
 'select_visible_button': 'eye', 'style_toolbutton': 'a-large-small', 'properties_toolbutton': 'sliders-horizontal',
 'attach_button': 'paperclip', 'automation_toolbutton': 'code-xml',
 'translation_toolbutton': 'messages-square', 'resample_toolbutton': 'scaling', 'timing_processor_toolbutton': 'timer',
 'spellcheck_toolbutton': 'spell-check', 'options_button': 'settings', 'toggle_tag_hiding': 'braces',
 # video
 'button_play': 'play', 'button_pause': 'pause', 'toggle_video_autoscroll': 'monitor-play',
 # visual
 'visual_standard': 'crosshair', 'visual_move': 'move', 'visual_rotatez': 'rotate-cw',
 'visual_clip': 'crop', 'visual_pan': 'hand',
 # rest
 'about_menu': 'info', 'arrow_up': 'arrow-up', 'arrow_down': 'arrow-down', 'arrow_up_stop': 'arrow-up-to-line',
 'arrow_down_stop': 'arrow-down-to-line', 'arrow_sort': 'arrow-down-wide-narrow', 'bugtracker_button': 'bug',
 'button_italics': 'italic', 'button_underline': 'underline', 'button_strikeout': 'strikethrough',
 'open_audio_menu': 'volume-2', 'close_audio_menu': 'volume-x', 'open_keyframes_menu': 'key-round',
 'open_timecodes_menu': 'clock', 'open_video_menu': 'tv-minimal-play',
 'detach_video_menu': 'external-link', 'new_window_menu': 'app-window', 'contents_button': 'book-open',
 'copy_button': 'copy', 'cut_button': 'scissors', 'paste_button': 'clipboard', 'delete_button': 'trash-2',
 'undo_button': 'undo-2', 'redo_button': 'redo-2', 'export_menu': 'share', 'eyedropper_tool': 'pipette',
 'find_button': 'search', 'irc_button': 'message-square-more', 'website_button': 'globe',
 'languages_menu': 'languages', 'kara_split_accept': 'check', 'kara_split_cancel': 'x', 'select_lines_button': 'funnel',
 'toggle_audio_link': 'link',
 'align_left': 'text-align-start', 'align_centre': 'text-align-center', 'align_right': 'text-align-end',
}
# Lucide's picture made small at the top left, with a badge at the bottom right
X_BADGE = "<path d='M16 16L21 21M21 16L16 21'/>"
SAVE_BADGE = "<path d='M18.5 14V20.5M15.5 17.5L18.5 20.5L21.5 17.5'/>"
COMPOSITE = {
 'close_keyframes_menu': ('key-round', X_BADGE), 'save_keyframes_menu': ('key-round', SAVE_BADGE),
 'close_timecodes_menu': ('clock', X_BADGE), 'save_timecodes_menu': ('clock', SAVE_BADGE),
 'close_video_menu': ('tv-minimal-play', X_BADGE),
}
# Play controls solid, like Aegisub's own audio ones
FILLED = {'button_prev', 'button_next', 'button_stop', 'button_play', 'button_pause'}

def lucide(directory, name):
    t = open(os.path.join(directory, name + '.svg')).read()
    inner = t[t.index('>', t.index('<svg')) + 1:t.rindex('</svg>')]
    return [dotify(e) for e in parse(inner)]

def dotify(e):
    # Lucide draws dots as a zero-length line ("M12 8h.01"), which nanosvg
    # leaves out: a filled circle as wide as the stroke instead.
    t, a = e
    if t != 'path': return e
    cmds = parse_path(a['d'])
    if len(cmds) == 2 and cmds[0][0] == 'M' and cmds[1][0] in 'LHV':
        x, y = cmds[0][1]; c, v = cmds[1]
        ex, ey = (v[0], v[1]) if c == 'L' else ((v[0], y) if c == 'H' else (x, v[0]))
        if abs(ex - x) + abs(ey - y) < 0.1:
            return ('circle', {'cx': fmt(x), 'cy': fmt(y), 'r': '1', 'fill': '@', 'stroke': 'none'})
    return e

def filled(elems): return [(t, dict(a, fill='@') if t in ('path', 'rect') else a) for t, a in elems]

def build(directory):
    """name -> (source comment, elements)"""
    out = {}
    for n, src in LUCIDE.items():
        el = lucide(directory, src)
        out[n] = (f'Lucide: {src}', filled(el) if n in FILLED else el)
    for n, (src, badge) in COMPOSITE.items():
        out[n] = (f'Lucide: {src}, with a badge', transform(lucide(directory, src), 0.66) + parse(badge))
    for n, body in CUSTOM.items():
        out[n] = ('', parse(body))
    return out

def absolute(elems):
    out = []
    for t, a in elems:
        a = dict(a)
        if t == 'path': a['d'] = write_path(parse_path(a['d']))
        out.append((t, a))
    return write(out)

HEAD = """// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Monochrome icons (docs/icons.md): SVG bodies on Lucide's 24x24 grid,
// stroked 2 wide with round ends and joins; '@' is replaced by the icon
// colour, and parts drawn solid say fill='@' themselves. Paths use absolute
// commands only, as the pixel hinting in libresrc.cpp reads them.
//
// Icons marked "Lucide" are from Lucide (https://lucide.dev), ISC licence,
// some derived from Feather, MIT licence: see LICENSE.lucide. They are
// written with absolute commands and dots as small circles, and the play
// controls are filled. The rest are drawn for Aegisub in the same style.
// Generated by tools/icons/generate.py; edit that, not this.
#include "libresrc.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace {
struct ModernIcon { std::string_view name; const char *body; };

// Sorted by name for the binary search below
const ModernIcon icons[] = {
"""
TAIL = """};
}

const char *libresrc_modern_icon(std::string_view name) {
\tauto it = std::lower_bound(std::begin(icons), std::end(icons), name,
\t\t[](ModernIcon const& icon, std::string_view n) { return icon.name < n; });
\treturn it != std::end(icons) && it->name == name ? it->body : nullptr;
}
"""

def main():
    if len(sys.argv) != 2: sys.exit(__doc__)
    data = build(sys.argv[1])
    lines = []
    for n in sorted(data):
        src, el = data[n]
        body = absolute(el)
        assert '"' not in body and not re.search(r"d='[^']*[a-z]", body), n
        lines.append(f'\t{{"{n}", "{body}"}},' + (f' // {src}' if src else ''))
    target = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../src/libresrc/icons_modern.cpp')
    with open(target, 'w') as f:
        f.write(HEAD + '\n'.join(lines) + '\n' + TAIL)
    print(f'{len(lines)} icons')

if __name__ == '__main__':
    main()
