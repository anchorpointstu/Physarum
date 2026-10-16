# Physarum Plugin

A GPU Physarum (slime mould) simulation effect for Adobe After Effects 2026 on Windows.
It appears as **Effect > Simulation > Physarum**.

Scaffolded 2026-10-16 by the deploy app. Lives at `C:\dev\Physarum`.

## What it does

Hundreds of thousands to millions of agents sense, follow and deposit trails, which
self-organise into networks, foams, veins and flows. Highlights:

- **GPU.** The simulation runs in DirectX 12 compute shaders on the plugin's own GPU
  device. The finished frame is copied into After Effects' buffer.
- **Deterministic.** Frame N depends only on the seed and the parameters over frames
  0 to N. Scrubbing, RAM preview and the render queue all produce the same image.
  Parameters can be keyframed.
- **Fast scrubbing.** GPU snapshots are kept every few frames, so jumping back restores
  the nearest snapshot instead of starting over.
- **Up to four species**, each with its own behaviour and colour. They can follow or
  avoid each other's trails.
- **Colour mapping** by species colours, a three-colour gradient, or built-in palettes
  (Fire, Ice, Neon, Mono, Bio). It handles 8, 16 and 32 bpc projects.
- **Spawn patterns** (random, center, ring, box, edge) with inward or outward headings.
- **Presets**: Nets, Foam, Worms, Flow, Blobs, Coral, Sponge and Veins.
- **Optional agent dots** drawn over the trails.

It is a generator: apply it to a solid. The layer's own pixels are ignored.

## Layout

- `src/sim/` is the host-independent simulation core: the DX12 wrapper, `SimInstance`
  and presets.
- `src/shaders/` holds the HLSL compute shaders. They are compiled at build time and
  embedded in the binaries.
- `src/ae/` is the After Effects layer: parameters, sequence data, render and PiPL.
- `src/cli/` is `physarum_cli.exe`. It runs the simulation without After Effects,
  writes PNGs, and runs the self-checks.
- `cmake/` and `scripts/` hold the build and install helpers.
- `build/` is build output and is gitignored.

Commit source, not build output. A built `.exe`, `.aex` or installer belongs in a
GitHub Release rather than in the repo history.

## Building

Requirements:

- Visual Studio 2022 Build Tools with the C++ workload and a Windows 10/11 SDK. These
  provide `fxc.exe`, `d3d12.lib` and `rc.exe`.
- The After Effects SDK, downloaded from Adobe's developer site. It is never committed.
  The build looks for it in this order:
  1. `-SdkRoot`
  2. the `AE_SDK_ROOT` environment variable
  3. `C:\dev\prereq\AfterEffectsSDK_26.5_win\AfterEffectsSDK_26.5_win`

```powershell
scripts\build.ps1                 # Release -> build\Release\Physarum.aex and physarum_cli.exe
scripts\build.ps1 -Config All     # Release and Debug
scripts\build.ps1 -Clean -Test    # clean build, then the determinism and snapshot self-checks
```

The script uses CMake's Visual Studio generator. It avoids Ninja because a Command
Processor AutoRun registry entry (such as `cd /d C:\xampp\htdocs`) breaks Ninja's link
steps.

Without the SDK, the script builds only the CLI.

## Installing

Copy `Physarum.aex` and `scripts\install.ps1` into one folder on the machine with
After Effects. Close After Effects, open PowerShell as administrator in that folder, and
run:

```powershell
powershell -ExecutionPolicy Bypass -File install.ps1
```

The `-ExecutionPolicy Bypass` part is needed because Windows blocks scripts by default.
It applies to this one run only.

The script reads Adobe's documented `CommonPluginInstallPath` registry value to find the
plug-in folder. That is normally
`C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore`, and the script installs into a
`Physarum` subfolder there.

- `-Source <path>` installs a plugin from elsewhere.
- `-Uninstall` removes it again.

Alternatively, copy `Physarum.aex` by hand into that folder or into After Effects'
`Support Files\Plug-ins` folder.

The plugin needs Windows 10 or 11 and a DirectX 12 GPU. It has no Visual C++ runtime
dependency.

## Using it

1. Create a solid and apply **Effect > Simulation > Physarum**.
2. Pick a **Preset**, or tune the controls. Changing a preset-controlled slider switches
   Preset back to Custom.
3. Animate any control in the Trail and Species groups. The change affects that frame
   and every frame after it.

### Controls

| Group | Control | Notes |
|---|---|---|
| Top | Preset, Species (1-4) | Setting Species shows or hides the species groups. |
| Top | Seed, Agent Count, Simulation Scale, Pre-roll Steps | These set up the simulation and cannot be animated. Simulation Scale sets the simulation size as a percentage of the layer size. |
| Top | Steps Per Frame, Boundary | Animatable. Boundary is Wrap, Bounce or Respawn. |
| Trail | Decay, Diffusion Radius, Diffusion Strength, Trail Ceiling | Decay applies per step. Trail Ceiling is both the white point and the maximum trail value. |
| Species N | Share, Color, Sensor Angle, Sensor Distance, Turn Angle, Step Size, Deposit, Wander, Other Species | Distances are in simulation pixels. Other Species runs from -100 % (avoid) to +100 % (follow). |
| Species N | Spawn Pattern, Spawn Center, Spawn Size, Heading | These set the starting layout and cannot be animated. |
| Color | Palette, Gradient Low/Mid/High, Exposure, Gamma, Alpha From Brightness, Background, Background Color | The gradient colours show only when Palette is Gradient. |
| Agents | Draw Agents, Agent Size, Agent Opacity | Dots are drawn at the agents' positions. |
| Cache | Snapshot Every, Cache Limit (MB) | These tune scrubbing speed against GPU memory. Changing them never changes the image. |

### Good to know

- **Long jumps take time.** The first render of frame 1000 simulates 1000 frames, with a
  progress bar. After that, scrubbing uses snapshots.
- **4K is heavy.** Each trail buffer takes 16 bytes per simulation pixel. Simulation Scale
  50 % is a good starting point at 4K.
- **Warning icon.** After Effects shows a warning icon next to the effect because it
  doesn't support multi-frame rendering. This is intended: the simulation state is
  sequential. Other effects still render in parallel.
- **Premiere Pro is not supported.**
- **Non-square pixels.** The simulation runs in the layer's pixel grid.

## Testing without After Effects

```powershell
build\Release\physarum_cli.exe --help
build\Release\physarum_cli.exe --frames 200 --set preset=veins --set palette=fire
build\Release\physarum_cli.exe --selfcheck --frames 40           # determinism
build\Release\physarum_cli.exe --snapshot-check 12 --frames 50   # snapshot restore
```

On a GTX 1650 Super, 1024 x 1024 with 500k agents takes about 3.4 ms per frame.
1920 x 1080 with 2M agents takes about 12 ms per frame.

## Deploying

Through the deploy app at <http://127.0.0.1:4400>: "Connect to GitHub" once, then
"Preview & push". It pushes to the Anchor Point Studio account.

## Secrets

Nothing secret belongs in this folder. Keep keys in environment variables or a
local file listed in `.gitignore`.
