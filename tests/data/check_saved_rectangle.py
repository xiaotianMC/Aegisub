"""Check the result of drawing and saving a rectangle in the actual app.

Usage: python check_saved_rectangle.py saved-file.ass
"""
from pathlib import Path
import re
import sys

rows = [line.split(',', 9) for line in Path(sys.argv[1]).read_text(encoding='utf-8-sig').splitlines()
        if line.startswith('Dialogue: ')]
assert len(rows) == 2, 'Expected the original subtitle plus exactly one rectangle'
rectangle, original = rows
assert original[9] == '测试字幕：绘制矩形后，原文和时间必须保持不变。', 'Original subtitle was overwritten'
assert original[1:4] == ['0:00:00.00', '0:00:05.00', 'Default'], 'Original timing/style changed'
assert rectangle[:3] == original[:3], 'Rectangle timing/layer must match its source'
assert rectangle[3] == '图形', 'Rectangle must use its dedicated graphics style'
text = rectangle[9]
for tag in (r'\an7', r'\fscx100', r'\fscy100', r'\bord0', r'\shad0', r'\p1', r'\p0'):
    assert tag in text, f'Missing drawing tag: {tag}'
position = re.search(r'\\pos\((\d+),(\d+)\)', text)
assert position, 'Rectangle needs a valid top-left position'
shape = re.search(r'm 0 0 l (\d+) 0 (\d+) (\d+) 0 (\d+)', text)
assert shape, 'Expected a closed rectangular drawing path'
w1, w2, h1, h2 = map(int, shape.groups())
assert w1 == w2 and h1 == h2 and w1 > 0 and h1 > 0, 'Invalid or zero-area rectangle'
x, y = map(int, position.groups())
assert x + w1 <= 640 and y + h1 <= 360, 'Drawing coordinates exceeded the video area'
print(f'PASS: original subtitle preserved; {w1}x{h1} rectangle at ({x},{y}) saved as standard ASS')
