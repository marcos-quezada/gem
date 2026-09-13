# Desktop icons and file manager regression

The desktop already contained disk, Workspace and Trash bitmap assets and a
file manager. The proxy session lost their visible and interactive entry
points for two reasons:

- gemd clipped copied object trees to owned windows and explicit dialogs,
  excluding the desktop owner's exposed background.
- The desktop menu used several LASTOB flags and disconnected unused Desk
  window entries. Transport validation rejected this malformed graph, so
  installing the menu did not establish desktop ownership.

Object-tree rendering now includes the desktop owner's visible background,
using the same window occlusion rules as desktop VDI drawing. The menu has
one final LASTOB, keeps every entry linked, hides unused entries and republishes
browser window labels when they change.

`tests/uat/samples/desktop.py` runs the shipped direct and proxy binaries with
real Rasta and scripted HID events. It checks source bitmap pixels for disk,
Workspace and Trash icons, double-clicks Workspace, enters a fixture folder,
returns to its parent and closes the browser. It checks title/content changes
and restored background pixels. In list view it also checks the Name, Date,
Size and Type header and verifies that file metadata is rendered in the last
three columns. Rows, header and footer use the system font, and directory labels
use the compact `/name` form. The header uses
Gemscape's metric-derived band height, an unhighlighted background and a
horizontal separator; it remains fixed while vertically centered file rows and
their dotted separators scroll. A matching bottom status band reports
file/folder counts and total byte size.
No AI or manual input is required.

Evidence from `make tests` is recorded in [the latest report](LATEST.md).
Screenshots and machine-readable results are under
`build/uat/desktop_direct/` and `build/uat/desktop_proxy/`, including
`desktop_icons.pbm`, `workspace_delayed_repeat.pbm`, `workspace_open.pbm`,
`child_open.pbm`,
`list_scrolled.pbm`, `size_sorted.pbm`, `parent_open.pbm` and
`browser_closed.pbm`.

The fixture directory contains three text files with distinct names and sizes;
the captured child window shows their metadata and confirms that clicking Size
puts the largest first. Header clicks apply the same four sort modes as the
Arrange menu. These checks cover browsing and closing, not file deletion or
launching every file type. Restart an existing F5 session to load the rebuilt
desktop and server.

## Menu cleanup and Workspace follow-up

AES menu layout and saved-region bounds now ignore HIDETREE children, so
unused Desk slots do not reserve blank rows. The separator is hidden when
there are no file-manager windows. Desktop info opens a real information
alert. Placeholder file operations, the entire Options menu and unsupported
sort modes have been removed.

Direct/proxy UAT checks Desk geometry with zero and one browser window,
Desktop info opening/dismissal and File → Open, in addition to browsing.
The full F5 session test clicks the exposed Workspace label, waits beyond the
double-click interval, and clicks it again to verify that selection alone does
not activate an icon. It then performs a real double-click, verifies a new
browser window and closes it. Terminal can cover the icon bitmap while part of
the label remains exposed; input must go to the desktop only in the exposed
region. Restart F5 after rebuilding to use the current server and desktop.

## Desktop info removing windows

A deterministic reproduction opens a witness window on a second connection,
sends part of a valid RPC frame, and leaves Desk open for three seconds before
selecting Desktop info. Before the fix gemd disconnected that client, removed
its window and the next write failed with `BrokenPipeError`. Evidence is in
`build/desktop-info-timeout-repro.log`.

Native menu and window-drag tracking wait synchronously for physical input.
The server previously counted that interval against the two-second transport
deadline even though it could not service any sockets. Queued replies and
partially received requests could therefore expire when tracking returned.
gemd now advances session deadline timestamps by the time spent dispatching
physical input, preserving the remaining client time budget. Requests outside
these server-imposed pauses still expire under the existing limits.

The F5 integration test keeps the menu open three seconds, selects Desktop
info over Stout's closer, completes the interrupted request, and keeps the
alert open another seven seconds. It checks every existing window's geometry,
the witness connection, all sample processes, and dismissal of the alert.
`desktop_info_before.pbm`, `desktop_info_selected.pbm`,
`desktop_info_open.pbm` and `desktop_info_closed.pbm` in
`build/sample_session_test/` capture this path. The standalone direct/proxy
checks continue to verify the alert's visible appearance and dismissal.

The longer alert check also reproduced a second disconnect: Clock's window
(handle 2) disappeared while the alert was open. Diagnostic tracing recorded
`modal transport close app=3 opcode=4 age=4294967295`. The modal service loop
sampled `now` before receiving a request, while receiving its header could
set `io.started` one tick later. Unsigned subtraction then treated that new
request as expired. Timeout checks now sample the clock after socket I/O.
The failure is recorded in `build/desktop-info-traced.log`; the trace value
above records the observed underflow, not a real elapsed wait.

## Container and background regression

The September audit found that adding Workspace before probing mounts suppressed
the root-disk fallback when every container mount was filtered out. The fallback
now counts disk entries separately and allows Workspace and a disk to refer to
the same path. Direct and proxy UAT run in the Docker mount namespace as well
as on the host.

The all-samples alert check also exposed an inverted checker phase after a
background repaint: the desktop used the opposite pattern ink to AES. Its fill
now matches AES, so opening and dismissing Desktop info restores identical
background pixels. The existing assertion remains unchanged.

## Trash backend

The desktop Trash icon now opens a File Manager window bound to `trash://`.
The backend writes percent-encoded freedesktop `.trashinfo` records, chooses a
same-filesystem home or `.Trash-$UID` store, falls back to home Trash with an
OS-layer cross-filesystem move when a volume store is unavailable, generates
collision names, and supports restore, permanent purge and empty. File Manager Delete and drops on
the icon or Trash window call the same backend. Permanent actions use
cancel-default GEM alerts, and navigation below a trashed directory is checked
against that item's store root.

`test_gem_trash` uses an isolated `XDG_DATA_HOME` and covers file and directory
moves, metadata enumeration, restore collisions and generated rename, symlink
purge containment, percent encoding, and emptying. The desktop sources are also
checked for direct POSIX filesystem and mount calls; those operations live in
the platform implementation behind `gem_os_*`. `uat_trash_direct` and
`uat_trash_proxy` cover drag-to-Trash and cancel-default/confirmed destructive
alerts through real HID input. List rows draw a dotted XOR rectangle and icon
view draws a dotted XOR upside-down-T union contour from the actual icon and
measured title rectangles while the Desktop polls the drag position. The UAT captures this
contour at its source and destination, checks that the Trash title is inverted
before button-up and remains inverted behind the confirmation, verifies that a
stationary icon-view double-click opens a folder, and moves both a file and a
non-empty directory. The directory-first sequence
also protects browser refreshes from corrupting the current absolute path. The
unit test verifies `.Trash-$UID` placement and moves a nested `tools/` fixture
across `/tmp` and `/dev/shm`. UAT also forces a move failure, dismisses the
error alert and immediately double-clicks another folder; this verifies that
the alert consumes its button release and later folder activation still works.
Because classic AES has no filesystem-change broadcast, the Desktop dispatches
successful move and restore notifications to all of its File Manager windows.
Every filesystem window showing the affected parent and every Trash window is
reloaded immediately. Empty Trash follows the cached Trash contents rather than
the active-window backend, so it remains enabled after a source-window drag
changes focus away from an open Trash browser.

Popup menu layout measures rendered labels and shortcuts, strips conventional
leading checkmark blanks from the visible label, and gives the content equal
left and right padding plus a distinct checkmark gutter. Menu UAT and the Trash
Arrange-menu capture exercise this geometry in direct and proxied modes.
