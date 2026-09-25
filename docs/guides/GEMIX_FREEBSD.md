# Native Gemix FreeBSD Backend

## Build

The native build requires FreeBSD's own `libdrm` (DRM/KMS dumb-buffer
API), `libseat` (`sysutils/seatd`, a single package that provides both
the `seatd` daemon and `libseat` itself -- no separate `libseat` package
exists), and `devctl` (part of the base system) in addition to the base
`cc`/GNU Make/CMake/Python build tools. FreeBSD's base `clang` is used
directly -- confirmed pure, portable C11 code with no GCC-specific
builtins anywhere in this codebase, so there is no need for `gcc`/`g++`
from ports.

```sh
cmake -S . -B build \
    -DCMAKE_C_COMPILER=cc \
    -DCMAKE_BUILD_TYPE=Release \
    -DGEM_PLATFORM=freebsd
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

`bin/apps/` contains `desktop`, `terminal`, `calc`, and `clock`;
`bin/core/gemd` is the display server. Unlike the Linux native backend,
there is no relocatable `gemix_package`/SDK target for FreeBSD yet --
this is confirmed intentional (`gemix_package`/`gem_sdk` are guarded to
`CMAKE_SYSTEM_NAME STREQUAL "Linux"` in the root `CMakeLists.txt`),
not an oversight.

## Prerequisites: seatd

`gemd` acquires the DRM device and evdev input devices through
`libseat`, so it can run as a normal, non-root user -- the same way
Sway or any other Wayland compositor does on this machine. Confirm
`seatd` is installed, enabled, and running before starting `gemd`:

```sh
pkg install seatd
sysrc seatd_enable=YES
service seatd start
```

If `gemd` fails to start with `gem_freebsd_seat_init failed`, this is
the first thing to check.

## Devices

The backend opens the DRM device directly (`/dev/drm/N`) via
`libseat_open_device()`, and discovers keyboard, mouse, and touchpad
evdev devices under `/dev/input` the same way.

**Safety note**: this backend never auto-probes multiple `/dev/drm/N`
nodes to "find the right one" -- on a machine with more than one GPU
(e.g. an Intel + NVIDIA laptop), opening the wrong node with any
DRM-aware client can be genuinely unsafe on some hardware/driver
combinations. The device path is always explicit, matching
`GEM_FREEBSD_DRM` below or a conservative default.

Configuration variables:

- `GEM_FREEBSD_DRM=/dev/drm/1` selects another DRM device (default:
  `/dev/drm/0`).
- `GEM_FREEBSD_DRM_DEVCTL=drmn1` overrides the newbus device name used
  for the power-cycle workaround below, if it differs from `drmn0` on a
  given machine.
- `GEM_FREEBSD_DRM_POWERCYCLES=N` overrides the number of `devctl`
  power-cycle attempts (default: 2; see "Known hardware quirk" below).
- `GEM_FREEBSD_INPUT=/dev/input/event2,/dev/input/event5` disables
  discovery and selects exact input devices.
- `GEM_FREEBSD_GRAB=1` exclusively grabs selected evdev devices
  (`EVIOCGRAB`). Recommended for a dedicated session; inconvenient
  during development.
- `GEM_VDI_WIDTH` / `GEM_VDI_HEIGHT` request a desktop size. When unset
  (or `0`), the desktop fills the visible display. Requests larger than
  the display are clamped.
- `GEM_FREEBSD_MOUSE_SCALE=8` multiplies relative (`EV_REL`) mouse
  deltas, and also scales `GEM_FREEBSD_TOUCHPAD_RELATIVE`'s
  touchpad-derived deltas (one shared knob for both). Absolute devices
  are preferred automatically when present. Confirmed on real
  clickpad hardware: the default (8) can feel far too fast for a
  touchpad's raw coordinate density -- `GEM_FREEBSD_MOUSE_SCALE=1` was
  the confirmed-comfortable value on the hardware this backend was
  developed against; tune per-device.
- `GEM_FREEBSD_KEEP_REL_MOUSE=1` keeps relative mice even when an
  absolute-position device is open (default is to drop them so they do
  not fight absolute input).
- `GEM_FREEBSD_TOUCHPAD_RELATIVE=1` tracks an absolute-capable touchpad
  via relative (delta-based) motion instead of always mapping to an
  absolute screen position. Real, confirmed use case: a touchpad whose
  sensing surface has a physically-unreachable region (e.g. an
  embedded fingerprint reader) can't otherwise reach part of the
  screen. Also suppresses tap-to-click (`BTN_TOUCH`-as-button)
  specifically in this mode, since a single cursor movement often needs
  multiple lift+reposition cycles and treating every touch-down as a
  click would cause unintended clicks/drags mid-gesture. Physical
  buttons (a separate evdev code from `BTN_TOUCH`) are unaffected.
  Includes real MT protocol type B "slot" tracking, so a clickpad's
  physical button press (confirmed on real hardware to register as a
  second, simultaneous touch) doesn't corrupt cursor position -- only
  the originally-primary touch drives the cursor. Known, narrow edge
  case: deliberately unrealistic rapid multi-finger tapping (not a
  normal click+move gesture) can still produce some erratic movement.
- `GEM_RESOURCE_DIR=/path/to/bin/resources` overrides resource
  discovery (required for a from-scratch build; points at the raw
  build's `bin/resources`, not a packaged layout).

## Known hardware quirk: `devctl` power-cycle + page-flip

On at least one real machine this backend was developed against, the
eDP display link would not actually display content on a cold-start
`gemd` launch, despite every DRM/KMS API call succeeding and reporting
correct state (`drmModeSetCrtc` success, correct `buffer_id`/mode/
connector). The confirmed, working fix -- not a guess -- is a real PCI
power-state cycle of the GPU device via the documented `devctl(3)` C API
(`devctl_suspend()`/`devctl_resume()`, the same mechanism `devctl(8)`
itself uses), deferred until real drawing activity has actually
happened (an empirically-confirmed threshold, with a time-based
fallback so it doesn't depend on a specific client app existing), plus
an explicit `drmModePageFlip()` afterward to keep the display
continuously rescanning (without it, the display shows one correct
frame and then never rescans again, even though the underlying memory
keeps updating correctly).

This is real, hardware-specific behavior, not something every FreeBSD/
DRM machine needs -- if a different machine's display works without a
power-cycle, `GEM_FREEBSD_DRM_POWERCYCLES=0` disables it (or simply
never triggers, since it depends on `GEM_FREEBSD_DRM_DEVCTL` naming a
real newbus device).

## Known limitations

- **Keyboard layout is fixed to US, regardless of the OS's configured
  layout.** `lib/platform/{linux,freebsd}/keymap.c`'s `ascii_for_key()`
  maps evdev `KEY_*` codes (physical key positions, a standard
  layout-agnostic encoding) to a hardcoded US-QWERTY ASCII table, with
  no awareness of the console's own `kbdcontrol` keymap, X11/
  libxkbcommon, or any other OS-level layout translation. This affects
  every platform equally, not a FreeBSD-specific gap.
- **Non-root operation requires `seatd`** (see above) -- running as
  root with a raw, non-seat-brokered `open()` also works (confirmed
  during initial bring-up) but is not the intended, documented path.
- After stopping `gemd` (confirmed not specific to `SIGKILL` -- a
  graceful `SIGTERM` shows the same behavior), the physical console's
  keyboard has been observed stuck in a raw/scancode mode (every
  keystroke prefixed with an ESC character) until switching to a
  different VT and back. Root cause not yet confirmed.
- **`terminal`'s VDI text rendering only supports GEM's classic
  bitmap/Atari charset**, confirmed via `vst_font(vdi_handle, ATARI)` in
  `src/apps/terminal/main.c` -- not a full Unicode-capable renderer.
  Nerd Font/powerline glyphs (private-use-area Unicode code points) in
  a normal shell prompt will not render; use a plain-ASCII prompt
  fallback inside GEM's terminal rather than expecting a Nerd-Font-based
  prompt to work there.

## Launching applications from within the desktop

There is no separate launcher or program menu -- the file browser's
double-click-to-run (`shel_write()`, the classic GEM "Application
Program Start" mechanism) is the only in-desktop launch mechanism.
Any executable file counts as launchable (checked via
`desktop_browser_name_is_app()`: not a `.so`/`.a`/`.o`, not literally
named `gemd`, doesn't contain `_hosted` in its name), no special
extension required. Navigate the file browser to `bin/apps/` and
double-click `clock`, `calc`, `terminal`, or `desktop` directly.

## Launching: `gem-launch`

There is no bundled launch script equivalent to Linux's manual
three-terminal sequence built specifically for FreeBSD's non-root/
`libseat` workflow yet in this repository -- `tools/scripts/
start-gemd.sh` exists but is a thin `exec` wrapper with no socket-wait
or client-sequencing logic. A FreeBSD-specific wrapper
(`gem-launch`, POSIX `sh`) lives in the dotfiles repo that manages
this: starts `gemd`, polls for its socket (rather than a fixed sleep),
launches one or more client apps (`--all` for the full bundled set:
`desktop`, `calc`, `clock`, `terminal`), and stops `gemd` gracefully on
exit.

Manual equivalent, matching the same sequencing:

```sh
export GEM_RESOURCE_DIR=/path/to/bin/resources
export GEM_FREEBSD_TOUCHPAD_RELATIVE=1   # if applicable to your hardware
export GEM_FREEBSD_MOUSE_SCALE=1         # if applicable to your hardware
./bin/core/gemd &
while [ ! -S /tmp/gemd.sock ]; do sleep 0.1; done
exec ./bin/apps/desktop
```

## VT-switching

Under `libseat`, the kernel/`seatd` do **not** intercept a VT-switch
hotkey (e.g. Ctrl+Alt+Fn) automatically the way old-style `vt(4)`
console switching did -- the compositor is expected to recognize the
combo itself and explicitly request the switch. `hid.c` does this: it
recognizes Ctrl+Alt+F1 through F12 and calls
`libseat_switch_session()`. Confirmed on real hardware: the switch
completes cleanly without needing to kill `gemd` first, and switching
back redraws correctly with cursor still responsive.

## Keyboard shortcuts

Menu items use classic Atari GEM accelerator syntax, defined per-app in
that app's own menu resource: `^X` (Ctrl+X), a bare letter (plain
keypress, no modifier), `F1`-`F19`, or `ESC`. The menu itself displays
the shortcut text next to the label when opened -- there is no single
master keybinding list beyond what each app's own menus show. Super+Q
is a confirmed, working AES quit accelerator. The file browser
additionally supports Return/arrow keys/Delete for navigating the
active selection.

## Application SDK

Same as the [Linux native backend](GEMIX_LINUX.md#application-sdk) --
applications include `<gem.h>`/`<gem/gem.h>` and link against
`libgem.so`, connecting to `gemd` through `GEMD_SOCKET` (default
`/tmp/gemd.sock`). See [API transport](../architecture/API_TRANSPORT.md).
