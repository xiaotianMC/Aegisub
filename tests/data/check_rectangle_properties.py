"""Check an actual UI-saved edit: baseline, saved, width, height, x, y, RGB, transparency%."""
from pathlib import Path
import re
import sys

def events(path):
    return [line.partition(':')[0:3:2] for line in Path(path).read_text(encoding='utf-8-sig').splitlines()
            if line.startswith(('Dialogue:', 'Comment:'))]

def check(original, saved, w, h, x, y, rgb, transparency):
    before, after = events(original), events(saved)
    assert len(before) == len(after) == 2
    assert before[1] == after[1], 'Original subtitle changed'
    assert before[0][0] == after[0][0], 'Visibility changed'
    b, a = before[0][1].strip().split(',', 9), after[0][1].strip().split(',', 9)
    assert b[:9] == a[:9], 'Rectangle metadata changed'
    text = a[9]
    shape = re.search(r'm 0 0 l ([\d.]+) 0 ([\d.]+) ([\d.]+) 0 ([\d.]+)', text)
    assert shape, 'Rectangle path missing'
    w1, w2, h1, h2 = map(float, shape.groups())
    scale_x = re.findall(r'\\fscx([-\d.]+)', text)
    scale_y = re.findall(r'\\fscy([-\d.]+)', text)
    sx, sy = float(scale_x[-1]) / 100 if scale_x else 1, float(scale_y[-1]) / 100 if scale_y else 1
    assert abs(w1 * sx - w) < .002 and w1 == w2 and abs(h1 * sy - h) < .002 and h1 == h2
    original_shape = re.search(r'm 0 0 l ([\d.]+) 0 ([\d.]+) ([\d.]+) 0 ([\d.]+)', b[9])
    assert original_shape and shape.group() == original_shape.group(), 'Drawing path was rewritten'
    position = re.findall(r'\\pos\(([-\d.]+),([-\d.]+)\)', text)
    assert len(position) == 1 and tuple(map(float, position[0])) == (x, y)
    colors = re.findall(r'\\1c&H([0-9A-Fa-f]{6})&', text)
    assert colors and colors[-1].upper() == rgb[4:6] + rgb[2:4] + rgb[0:2]
    alpha = re.findall(r'\\1a&H([0-9A-Fa-f]{2})&', text)
    assert alpha and abs(int(alpha[-1], 16) - transparency * 255 / 100) <= .5
    for tag in (r'\an7', r'\frz0', r'\bord0', r'\shad0', r'\3a&HFF&', r'\4a&HFF&'):
        assert tag in text, tag
    print('PASS: rectangle properties persisted; metadata, other tags and source subtitle preserved')

if __name__ == '__main__':
    check(sys.argv[1], sys.argv[2], *map(float, sys.argv[3:7]), sys.argv[7].upper(), float(sys.argv[8]))
