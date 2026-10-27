"""Verify real saved ASS: only specified rows have their move endpoints swapped."""
from pathlib import Path
import re
import sys

def rows(path):
    return [line.split(',', 9) for line in Path(path).read_text(encoding='utf-8-sig').splitlines()
            if line.startswith(('Dialogue:', 'Comment:'))]

def reversed_text(text):
    def swap(match):
        args = match[1].split(',')
        assert len(args) in (4, 6)
        args[:4] = args[2:4] + args[:2]
        return '\\move(' + ','.join(args) + ')'
    return re.sub(r'\\move\(([^()]*)\)', swap, text)

if __name__ == '__main__':
    original, saved = rows(sys.argv[1]), rows(sys.argv[2])
    reverse = {int(n) for n in sys.argv[3:]}
    assert original and len(original) == len(saved), 'Row count changed'
    assert reverse <= set(range(1, len(original) + 1)), 'Invalid row number'
    for number, (before, after) in enumerate(zip(original, saved), 1):
        assert before[:9] == after[:9], f'Metadata changed on row {number}'
        expected = reversed_text(before[9]) if number in reverse else before[9]
        assert after[9] == expected, f'Unexpected text or path on row {number}'
    print('PASS: move paths reversed only on requested rows; times, metadata and other text preserved')
