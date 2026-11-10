"""Verify actual GUI-saved ASS visibility without changing any event fields."""
from pathlib import Path
import sys

def events(path):
    result = []
    for line in Path(path).read_text(encoding='utf-8-sig').splitlines():
        kind, sep, fields = line.partition(':')
        if sep and kind in ('Dialogue', 'Comment'):
            result.append((kind[0], fields.strip().split(',', 9)))
    return result

if __name__ == '__main__':
    original, saved = events(sys.argv[1]), events(sys.argv[2])
    expected = sys.argv[3]
    assert original and len(original) == len(saved) == len(expected), 'Event count changed'
    assert set(expected) <= {'C', 'D'}, 'Use C for hidden and D for shown'
    assert ''.join(kind for kind, _ in saved) == expected, 'Visibility differs'
    assert [fields for _, fields in original] == [fields for _, fields in saved], 'Event data changed'
    print('PASS: visibility = ' + expected + '; all event data and order preserved')
