# HyprToroid

A toroidal workspace grid and configurable desktop session for native Hyprland on Arch Linux/CachyOS.

The C++ plugin is derived from [Hyprtasking](https://github.com/raybbian/hyprtasking). Its upstream Git history and BSD-3-Clause notices are retained.

Targets the installed Hyprland 0.57.1 APIs. The compositor/plugin ABI check remains enabled. Rebuild and verify the plugin after Hyprland updates; loading arbitrary incompatible native code can crash the compositor.

## Installation

The Arch package installs these runtime paths, with no dependency on where the source checkout lives:

- `/usr/bin/hyprtoroid`: session/development CLI.
- `/usr/lib/hyprtoroid/libhyprtoroid.so`: packaged plugin.
- `/usr/lib/hyprtoroid/hyprsession`: session restoration helper.
- `/usr/share/hyprtoroid/`: default Lua profile, keybinding help and Noctalia minimap.
- `/usr/share/wayland-sessions/hyprtoroid*.desktop`: generic standalone and UWSM session entries. GPU-specific login entries are not shipped.

Clone the public source, then build from a **separate working directory** outside the checkout. The package downloads the independent Hyprsession source at a pinned commit and verifies its checksum; a sibling checkout is not required.

```sh
git clone https://github.com/Nie1iX/hyprtoroid.git "$HOME/hyprtoroid"
package_dir=$(mktemp -d /tmp/hyprtoroid-package.XXXXXXXX)
cd "$package_dir"
cp "$HOME/hyprtoroid/packaging/PKGBUILD" .
HYPRTOROID_SOURCE_DIR="$HOME/hyprtoroid" makepkg -s
sudo pacman -U ./hyprtoroid-*.pkg.tar.zst
```

The checkout can live elsewhere: set `HYPRTOROID_SOURCE_DIR` to its path. For local Hyprsession development, set `HYPRSESSION_SOURCE_DIR` explicitly to that checkout; this replaces the pinned download. The makepkg working directory must be outside both local checkouts and must not contain either of them. Build dependencies include the system Hyprland development headers, Meson/Ninja, GCC and Cargo. `CARGO`/`RUSTC` can select an existing Rust toolchain. Package installation needs administrator authorization; ordinary development/reload does not. Nothing is published automatically.

Shared resources retain their file modes, not the source checkout's ownership, when staged by `fakeroot`.

Choose **HyprToroid** or **HyprToroid (UWSM)** at the login screen. The launcher refuses to start another real session inside an existing graphical session. `hyprtoroid --session` is also available from a logged-in TTY.

## Personal settings

Keep personal preferences outside the package, in `${XDG_CONFIG_HOME:-$HOME/.config}/hyprtoroid/config.lua`. This optional Lua file is loaded last. For example:

```lua
hl.config({
    plugin = { hyprtasking = { gestures = {
        move_rails = true,
        move_commit_threshold = 0.05,
    } } },
})
```

Apply changes with **Super+Shift+C** or `hyprctl reload`. Do not edit `/usr/share` for personal settings; package upgrades can replace it. Monitor scaling and keyboard layout belong in this user file. User overrides can distinguish the real session from a preview using `HYPRTOROID_SESSION`.

Application-specific window rules also belong in this file, not the reference session. The packaged profile does not impose Telegram or specially titled terminal rules.

Shortcut help (**Super+/**) is English by default. An optional `${XDG_CONFIG_HOME:-$HOME/.config}/hyprtoroid/keybindings.txt` replaces the packaged help, without changing bindings. To start with the Russian translation:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/hyprtoroid"
cp /usr/share/hyprtoroid/keybindings.ru.txt "${XDG_CONFIG_HOME:-$HOME/.config}/hyprtoroid/keybindings.txt"
```

Edit that user-owned copy to reflect personal shortcuts. Preview sessions copy both personal `config.lua` and help, while keeping their runtime state isolated.

Optional `~/.config/hyprtoroid/session.env` contains trusted shell settings for session startup:

```sh
HYPRTOROID_GPU=nvidia
```

GPU choices are `auto`, `intel` and `nvidia`; the launcher discovers card nodes by vendor instead of hardcoding PCI addresses. An explicit `--gpu` overrides the user preference. The generic default is `auto`.

Private login templates can be kept outside the checkout, for example in `~/.config/hyprtoroid/session-entries/`. This is storage, not a session-manager discovery directory: installing such an entry system-wide is an explicit administrator action. A separate NVIDIA entry is unnecessary when the generic launcher already reads a personal GPU preference.

## Gesture settings

The inherited sole configuration/Lua namespace remains `plugin.hyprtasking`; it is not an external upstream plugin loaded underneath HyprToroid.

- `gestures.move_distance = 300`: finger travel corresponding to a workspace width.
- `gestures.move_rails = true`: lock a three-finger gesture to the initial nearest horizontal, vertical or diagonal rail after the neutral zone. Reversal on the same rail is allowed; perpendicular input is ignored. Set false for free 2D motion.
- `gestures.move_commit_threshold = 0.5`: default release threshold, a fraction of one workspace step after the neutral zone. **0 means any nonzero net travel**, not an off/legacy mode. The range is `0..1`. A zero-motion gesture, a return to the start or a cancelled gesture does not switch workspaces. Smaller values allow shorter swipes. Quick regrabs measure the new gesture only, without undoing a previous committed transition.

The continuous neutral zone is 15% of `move_distance` per projected axis. Finger tracking has no extra smoothing delay. Release completes to a whole workspace. Looping transitions and diagonal rails are supported; a diagonal can still reveal four workspace fragments. Four-finger gestures open/close overview. Overview click selects a workspace; dragging requires more than 5 logical pixels; Escape is swallowed while closing overview.

## Live development

Editable source and installed runtime are separate.

The CLI resolves the source checkout from `HYPRTOROID_SOURCE`, then `~/.config/hyprtoroid/dev-source`, falling back to `~/hyprtoroid`. The checkout can live anywhere.

```sh
hyprtoroid dev build
hyprtoroid dev preview
hyprtoroid dev reload
hyprtoroid status
```

`dev build` compiles the C++ plugin against system headers, without private Fedora/runtime include paths. `dev preview` starts an isolated nested compositor: its DBus, runtime, configuration and state are separate and physical DRM card nodes are masked with bwrap. It does not start Noctalia or session restore for the preview. Reloading a plugin inside the preview does not change the real session's selected library.

`dev reload` builds a candidate, copies it to an immutable per-user `.so`, checks for existing configuration errors, then asks the current plugin to prepare synchronously. It refuses while overview, gestures or dragging are active, or a consumed key press still awaits its release. On candidate/configuration failure it attempts to restore the previous selected library. Native plugin failures cannot be made crash-proof; use preview first for risky changes. The compositor and user applications are not deliberately restarted.

The optional `~/.config/hyprtoroid/plugin-path` records the selected development candidate. With no selector, a new session uses the packaged library. Do not manually edit or delete the selector while the compositor is running; use `dev reload` to switch safely. Candidates are stored in `~/.local/state/hyprtoroid/candidates/`. A one-time `dev reload --active-plugin /absolute/current/library.so` supports migration from a manually loaded library.

Personal Lua settings can be edited/reloaded immediately. C++ plugin features use the build/preview/reload loop above without switching desktop sessions. Changes to Hyprland itself, GPU drivers or session startup can still require a restart. The CLI never overwrites root-owned package libraries for development.

## Session restore and Noctalia

The [independent Hyprsession Rust fork](https://github.com/Nie1iX/hyprsession) is built into the combined HyprToroid package at the revision pinned in `packaging/PKGBUILD`. It relaunches applications and restores placement; it does not serialize process memory. Existing unrelated windows are preserved during startup merge. Incomplete restore protects the previous snapshot from autosave. State remains in `~/.local/state/hyprtoroid/hyprsession`, outside the package and source checkouts. Do not clear protection to force autosave after a failed restore.

`hyprtoroid logout` uses the protected final-save/freeze/logout sequence in a HyprToroid session, including proper UWSM shutdown. It falls back to Noctalia's normal logout outside this compositor.

Noctalia's minimap source can be registered as:

```sh
noctalia msg plugins source add hyprtoroid path /usr/share/hyprtoroid/noctalia-plugins
noctalia msg plugins enable hyprtoroid/minimap
```

The bar minimap is display-only; the desktop widget supports workspace clicks. Fuzzel is the separate Wayland menu used for keybinding help, not the desktop shell.

## Tests

From the checkout:

```sh
meson setup build
meson test -C build --print-errorlogs
```

The tests cover toroidal grid mapping, fixed overview projection, gesture neutral zones, eight-direction locking, completion boundaries and consumed-key ownership used by the reload guard. Assertions remain enabled with `NDEBUG`. These tests do not replace real-compositor rendering, hardware-gesture, session-restoration or multi-monitor checks.

## License

The plugin and its inherited code retain **BSD-3-Clause**, in `LICENSE`. Distribution must retain the copyright notice, conditions and warranty disclaimer; source disclosure is not required by BSD-3-Clause. The original authors' names may not be used to imply endorsement without permission.

The [fork baseline](https://github.com/raybbian/hyprtasking/commit/41b88141702b33c39f315f3aa9113b06226727c8) is retained in Git history.

The independent hyprsession fork retains its separate **GPL-3.0** license in its checkout's `LICENSE`. Its redistribution obligations are not replaced by the plugin's BSD license. Both notices are installed by the Arch package.
