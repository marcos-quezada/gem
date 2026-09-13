#!/usr/bin/env python3
"""Check desktop icons and details-view browsing with real Rasta mouse input.

MIT License (see LICENSE). Copyright (C) 2026 tomaz stih.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import Session


def crop(frame, x, y, w, h):
    return bytes((frame[row * 80 + col // 8] >> (7 - col % 8)) & 1
                 for row in range(y, y + h) for col in range(x, x + w))


def double_click(session, x, y):
    session.event(3, x, y)
    for _ in range(2):
        session.event(10, x, y)
        session.event(11, x, y)
    session.event(3, 639, 399)
    session.pause()


def check(session):
    import re
    assets = (Path(__file__).resolve().parents[3] /
              'src/apps/desktop/desktop_assets.c').read_text()
    def words(name):
        body = assets.split('static const UWORD ' + name + '[] = {')[1]
        return [int(v, 16) for v in re.findall(r'0x[0-9a-f]+', body.split('};')[0])]
    def matches(frame, name, x, y):
        mask, data = words(name + '_mask'), words(name + '_data')
        pixels = crop(frame, x, y, 32, 32)
        return all(pixels[row * 32 + col] == bool(data[row * 2 + col // 16] & (0x8000 >> (col % 16)))
                   for row in range(32) for col in range(32)
                   if mask[row * 2 + col // 16] & (0x8000 >> (col % 16)))
    initial = session.frame('desktop_icons')
    positions = [(36 + col * 78, 38 + row * 66)
                 for col in range(7) for row in range(5)]
    folders = [(x, y) for x, y in positions if matches(initial, 'folder', x, y)]
    assert len(folders) == 1, 'Workspace folder icon is missing'
    assert any(matches(initial, 'disk', x, y) for x, y in positions), 'Disk icons missing'
    assert matches(initial, 'trash', 576, 332), 'Trash icon missing'
    x, y = folders[0]
    session.click(x + 16, y + 16)
    session.pause(.85)
    session.click(x + 16, y + 16)
    delayed_repeat = session.frame('workspace_delayed_repeat')
    assert crop(delayed_repeat, 220, 40, 400, 245) == \
        crop(initial, 220, 40, 400, 245), \
        'A delayed click on a selected icon opened the file manager'
    session.click(639, 399)
    # The bare desktop shows only the Desk menu (File/Arrange are hidden).
    session.event(3, 20, 10)
    session.event(10, 20, 10)
    menu = session.frame('desk_empty')
    assert crop(menu, 4, 48, 165, 130) == crop(initial, 4, 48, 165, 130), 'Desk reserves blank rows for unused slots'
    session.event(3, 80, 32)
    session.event(11, 80, 32)
    info = session.frame('desktop_info')
    assert crop(info, 230, 150, 180, 90) != crop(initial, 230, 150, 180, 90), 'Desktop info did not open a dialog'
    session.key(40)
    dismissed = session.frame('desktop_info_closed')
    assert crop(dismissed, 230, 150, 180, 90) == crop(initial, 230, 150, 180, 90), 'Desktop info did not close'
    session.pause(.85)
    double_click(session, x + 16, y + 16)
    opened = session.frame('workspace_open')
    assert crop(opened, 240, 42, 280, 16) != crop(initial, 240, 42, 280, 16), 'No file manager title'
    header = crop(opened, 220, 62, 360, 20)
    assert sum(header) < 1500, 'List column header must not be highlighted'
    assert sum(header[-360:]) >= 300, 'List column separator is missing'
    assert all(sum(crop(opened, 220, 82 + row * 18, 360, 18)) < 2000
               for row in range(10)), 'List view preselected an entry'
    assert sum(crop(opened, 220, 262, 360, 1)) >= 300, 'Status separator is missing'
    assert sum(crop(opened, 220, 263, 360, 18)) > 0, 'File count status is missing'
    assert sum(crop(opened, 356, 99, 88, 18)) > 0, 'List date column is empty'
    assert sum(crop(opened, 516, 99, 64, 18)) > 0, 'List type column is empty'
    session.event(3, 20, 10)
    session.event(10, 20, 10)
    populated = session.frame('desk_one_window')
    assert crop(populated, 4, 90, 165, 80) == crop(opened, 4, 90, 165, 80), 'Desk reserves blank rows below its browser entry'
    session.event(3, 630, 380)
    session.event(11, 630, 380)
    session.pause()
    double_click(session, 300, 108)  # First child directory, below header and parent.
    child = session.frame('child_open')
    assert crop(child, 240, 42, 280, 16) != crop(opened, 240, 42, 280, 16), 'Folder navigation did not change title'
    assert crop(child, 240, 100, 280, 140) != crop(opened, 240, 100, 280, 140), 'Directory contents did not change'
    assert sum(crop(child, 356, 100, 88, 18)) > 0, 'File date is not visible in list view'
    assert sum(crop(child, 444, 100, 72, 18)) > 0, 'File size is not visible in list view'
    assert sum(crop(child, 516, 100, 64, 18)) > 0, 'File type is not visible in list view'
    assert sum(crop(child, 220, 82, 360, 2)) == 0, 'List text is not vertically centered'
    assert 40 < sum(crop(child, 220, 99, 360, 1)) < 180, 'Dotted row separator is missing'
    first_file = crop(child, 220, 100, 360, 18)
    session.click(590, 272)  # Scroll down one row.
    scrolled = session.frame('list_scrolled')
    assert crop(scrolled, 220, 62, 360, 20) == header, 'List header moved while scrolling'
    assert crop(scrolled, 220, 82, 360, 180) != crop(child, 220, 82, 360, 180), 'List rows did not scroll'
    session.click(470, 70)  # Size column header.
    size_sorted = session.frame('size_sorted')
    assert crop(size_sorted, 220, 100, 360, 18) != first_file, 'Size header did not sort the list'
    assert all(sum(crop(size_sorted, 220, 82 + row * 18, 360, 18)) < 2000
               for row in range(10)), 'Sorting preselected a list entry'
    double_click(session, 300, 90)  # Parent row below the list header.
    parent = session.frame('parent_open')
    assert crop(parent, 240, 42, 280, 16) == crop(opened, 240, 42, 280, 16), 'Parent navigation failed'
    # While a file manager is open, the File menu is present and drops a popup.
    session.event(3, 75, 10)
    session.event(10, 75, 10)
    file_menu = session.frame('file_menu')
    assert crop(file_menu, 62, 24, 120, 60) != crop(parent, 62, 24, 120, 60), 'File menu did not drop while a file manager is open'
    session.event(11, 75, 10)
    session.pause()
    # Closing the file manager removes File/Arrange: the bare desktop shows only Desk.
    session.click(229, 50)  # Close file manager.
    closed = session.frame('browser_closed')
    assert crop(closed, 240, 42, 280, 220) == crop(initial, 240, 42, 280, 220), 'Desktop was not restored after close'
    session.event(3, 75, 10)
    session.event(10, 75, 10)
    file_gone = session.frame('file_absent')
    assert crop(file_gone, 62, 24, 120, 60) == crop(initial, 62, 24, 120, 60), 'File menu must be absent once the file manager is closed'
    session.event(11, 75, 10)
    print('PASS desktop file manager shows four list columns, icons, navigation and close')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('direct', 'proxy'), required=True)
    for name in ('binary', 'gemd', 'artifacts', 'resources'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    args.demo, args.capture = 'desktop', True
    session = Session(args)
    session.path = Path(args.artifacts).resolve()/f'desktop_{args.mode}'
    session.path.mkdir(parents=True, exist_ok=True)
    # Session environment was prepared for the original generated path.
    session.env['GEM_RASTA_FRAMEBUFFER'] = str(session.path/'framebuffer')
    session.env['GEMD_SOCKET'] = str(session.path/'socket')
    (session.path/'uat_child').mkdir(exist_ok=True)
    (session.path/'uat_child'/'marker.txt').write_text('Desktop UAT fixture\n')
    (session.path/'uat_child'/'a_small.txt').write_text('x')
    (session.path/'uat_child'/'z_large.txt').write_bytes(b'x' * 2048)
    for index in range(16):
        (session.path/'uat_child'/f'scroll_{index:02d}.dat').write_text(str(index))
    result = {'passed': False}
    try:
        try:
            session.run()
            check(session)
        finally:
            session.close()
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        (session.path/'result.json').write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()
