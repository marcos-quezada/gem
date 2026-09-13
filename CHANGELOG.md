# Change log

Significant user-facing changes to GEM for Linux are recorded here. The project
does not yet assign release version numbers, so current work remains under
Unreleased.

## Unreleased

_Updated 2026-09-12._

![What's new: GEM File Manager and Grok running in the VT100 Terminal](docs/images/screenshots/gem_desktop_2026_09_12.png)

### Added

- Implemented a Desktop Trash Can following the freedesktop Linux Trash
  convention. Home-volume items use `$XDG_DATA_HOME/Trash` or
  `$HOME/.local/share/Trash`; other volumes use `.Trash-$UID`. Each item has a
  percent-encoded `.trashinfo` record containing its original path and deletion
  time.
- Added moving files and non-empty directories to Trash, restoring them to
  their original locations, collision-safe restore names, permanent deletion
  and confirmed Empty Trash. Cross-filesystem moves use the portable OS layer
  and preserve symbolic-link containment.
- Implemented a DEC VT100-compatible Terminal parser and screen model. It
  supports cursor addressing, erase and insertion operations, scrolling
  regions, origin and autowrap modes, tabs, saved cursor state, primary and
  alternate screens, DEC line drawing, device/status reports, and bold,
  underline and reverse-video attributes.
- Added terminal input sequences for cursor, editing and function keys, plus
  application cursor-key mode. The PTY is resized with the GEM window and
  advertises `TERM=vt100` to child programs.
- Added safe handling for modern terminal control strings and UTF-8 box-drawing
  fallbacks. Host VTE, Kitty and color-terminal variables are removed from the
  child environment so applications do not enable capabilities GEM Terminal
  did not advertise.

### Changed

- Expanded File Manager list view with Name, Date, Size and Type columns, a
  fixed header, dotted row separators and a file/folder-count status bar. Its
  list text uses the GEM system font; icon captions retain the small icon font.
- Normal F5 startup now launches only Desktop and Terminal. The all-samples
  session remains an explicit integration test.
- Promoted Desktop, Terminal, Clock and Calculator from independent samples to
  bundled applications under `src/apps/`; their integrated binaries now live
  in `bin/apps/`.
- Popup items use balanced left and right margins with a dedicated checkmark
  gutter.

### Fixed

- File Manager drag/drop now distinguishes stationary double-clicks from
  drags, draws the classic icon/title union outline and inverts the Trash title
  while a valid drop remains held over it.
- Successful Trash moves and restores refresh every open File Manager viewing
  an affected directory. Empty Trash remains enabled whenever Trash contains
  items, regardless of which File Manager window is active.
