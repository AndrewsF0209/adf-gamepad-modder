# adf-gamepad-modder

**Gamepad → keyboard/mouse mapping that lives inside the game process. No daemon, no virtual driver, no background resident program.**

English below. 中文理念宣言见 [docs/manifesto.zh.md](docs/manifesto.zh.md)。

---

## Why "no resident program" matters

Mainstream gamepad mappers (DS4Windows, x360ce, reWASD, Steam Input) all follow
the same architecture: a **resident process** runs in the background, creates a
virtual input device, and every game sees that virtual device instead of your
real controller. This works, but it costs you:

- a tray icon that must be running 24/7, consuming memory and CPU forever;
- a virtual bus driver (ViGEm etc.) that needs kernel-level installation,
  triggers anti-cheat suspicion, and breaks when Windows updates;
- global device takeover — the mapper intercepts *all* gamepad traffic,
  even for apps that don't want it.

**adf-gamepad-modder takes the opposite approach.** The mapper is a tiny DLL
(`winmm.dll` / `version.dll` / `dinput8.dll` proxy) that is loaded *into the
game process itself* through a standard DLL-search-order deployment. It polls
the physical controller with SDL3 and injects synthesized keyboard/mouse input
via `SendInput`, entirely in user space. When the game exits, the mapper
exits with it. Nothing is installed, nothing runs in the background, nothing
else on the system is affected.

| | Resident mappers | adf-gamepad-modder |
|---|---|---|
| Background process | always running | none — dies with the game |
| Kernel / virtual driver | usually required | never |
| System-wide side effects | device takeover | zero (per-game opt-in) |
| Old-game compatibility (DirectInput polling, RawInput) | varies | designed for it |
| Deployment | install + autostart | drop 4 files next to the game exe |

## How it works

1. The WPF **configurator** (the only GUI) edits bindings and writes
   `adf_mapper.json` next to the game exe.
2. **Deployment** copies the proxy DLLs + launcher into the game folder.
   A generated `{game}_gamepad.exe` starts the real game with the proxies
   in scope (or the proxies are placed directly, depending on the mode).
3. Inside the game process, the proxy loads the core engine, which reads
   the JSON, polls the pad via SDL3 at ~1 kHz, and emits input through
   `SendInput` — scan codes, relative mouse, wheel, with full support for
   legacy DirectInput consumers.

The engine is pure C++20, statically linked, zero runtime dependencies.

## Features

- Per-button bindings: key (exact PS/2 scan code + extended flag), mouse
  button, wheel step
- Latch (toggle) with interrupt keys
- Chords (multi-key combos, strict press/release ordering)
- Gestures per button: tap / double-tap / long-press
- Radial menus: hold a button, aim with a stick, 8 directions, three
  trigger modes (on-release / on-centering / on-select with hold semantics)
- Operation layers: hold-to-activate layers and mouse-lock auto-switching,
  each layer overriding bindings *and* stick sensitivity profiles
- Stick modifier keys (d-pad / action buttons) for instant aim/look swaps
- Dead zones, 8-way digital conversion, response curves, edge acceleration
- Xbox and DualSense layouts, live pad diagram in the configurator
- Resident-mapping mode in the configurator for tuning without launching
  the game

## Building

Requirements: Windows 10+, Visual Studio 2022 (C++ toolset), CMake ≥ 3.20,
Ninja, .NET 8 SDK. SDL3 is fetched automatically by CMake (or point
`-DADF_SDL3_SOURCE_DIR` at a local copy).

```powershell
scripts\build.ps1 -Arch x64
scripts\build.ps1 -Arch x86
scripts\publish_configurator.ps1   # builds both, publishes GUI, stages dist/
```

## Testing tools

- `src/tools/input_probe` — a deliberately boring diagnostic window that
  reports injected input through the four channels classic games use
  (DirectInput keyboard polling, DI mouse buffer, RawInput, window
  messages + async state). It renders no game imagery on purpose.
- `src/tools/test_kbm_host` — minimal fake "old game" host used to verify
  proxy forwarding and injection.

## Anti-cheat & fair play

This is a personal input remapper for single-player and offline use, and for
games/launchers that officially permit input customization. Process-injected
input *can* be flagged by online anti-cheat systems; using it in competitive
online games is your responsibility, and we ask you not to. The project ships
no stealth features: the proxy DLLs are plainly visible in the game folder,
and there is exactly one deployment mode documented here.

## License

Apache License 2.0 — see [LICENSE](LICENSE). You may use, modify and
redistribute this code, **including in closed-source commercial products**,
as long as you keep the copyright notice and state changes. Third-party
components and their licenses are listed in [NOTICE](NOTICE).

The *idea* — in-process, daemonless gamepad mapping — is explicitly meant to
be copied, reimplemented, and shipped by anyone. That is the point of this
repository.
