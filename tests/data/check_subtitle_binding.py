"""Verify a real saved graphic/text binding: saved.ass duration-seconds bound|unbound."""
from pathlib import Path
import re
import sys

def centiseconds(value):
    hours, minutes, seconds = value.split(':')
    return round((int(hours) * 3600 + int(minutes) * 60 + float(seconds)) * 100)

if __name__ == '__main__':
    text = Path(sys.argv[1]).read_text(encoding='utf-8-sig')
    duration = round(float(sys.argv[2]) * 100)
    bound = sys.argv[3] == 'bound'
    rows = [line.split(',', 9) for line in text.splitlines() if line.startswith('Dialogue:')]
    assert len(rows) == 3, 'Expected graphic, original text and unrelated subtitle'
    assert rows[0][3] == '图形', 'Graphic inherited a text style'
    assert sum(line.startswith('Style: 图形,') for line in text.splitlines()) == 1
    ids = []
    for row in rows:
        match = re.match(r'^\{((?:=\d+)+)\}', row[9])
        ids.append(set(map(int, re.findall(r'\d+', match[1]))) if match else set())
        if match:
            row[9] = row[9][match.end():]
    for row in rows[:2]:
        assert centiseconds(row[1]) == 0
        assert centiseconds(row[2]) - centiseconds(row[1]) == duration, 'Linked duration differs'
        assert row[0] == 'Dialogue: 0', 'Actual layer changed'
    assert rows[1][9] == '测试字幕：绘制矩形后，原文和时间必须保持不变。'
    assert rows[2][1:4] == ['0:00:01.00', '0:00:04.00', 'Default']
    assert rows[2][9] == '未绑定的其他字幕，时间必须保持不变。'
    assert 7 in ids[1] and 'Data: 7,test.metadata,euntouched' in text, 'Unrelated metadata lost'
    data = {}
    for line in text.splitlines():
        if line.startswith('Data: '):
            number, key, value = line[6:].split(',', 2)
            if key == 'aegisub.enhanced.subtitle-binding':
                data[int(number)] = value
    if bound:
        group_ids = ids[0] & ids[1] & data.keys()
        assert len(group_ids) == 1 and not (ids[2] & data.keys()), 'Wrong binding membership'
        assert re.fullmatch(r'e[0-7]:[0-9a-f]{32}', data[next(iter(group_ids))]), 'Missing stable color/group ID'
    else:
        assert not any(group & data.keys() for group in ids), 'Binding survived unlink'
    print('PASS: graphics style, group persistence, synchronized duration, untouched subtitle and metadata')
