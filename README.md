# fo4pool

Playable pool tables for Fallout 4, as an F4SE plugin. The balls are real objects on the table, moved by the
plugin's own physics (not Havok).

**Status: test build 0.1.** Proves the core loop only: balls appear on a table and roll believably. No camera,
HUD, rules or opponents yet.

## Requirements

- Fallout 4 1.10.163 ("Old Gen"). The plugin is also built for Next-Gen and Anniversary, but only OG is targeted.
- F4SE and Address Library for F4SE Plugins for your game version.
- `fo4pool.esl` and the collision-free ball meshes (see below).

## Trying it

1. Install `Data/F4SE/Plugins/fo4pool.dll` and `fo4pool.ini`, plus `fo4pool.esl` and `Meshes/fo4pool/`.
2. Go to a pool table. Look at it (or click it in the console).
3. **Numpad 4**: calibration markers appear on the felt corners, side pockets, head and foot spots and centre.
   If they don't sit on the felt, edit `[Table]` in `fo4pool.ini` (length, width, height, offsets, axis), then
   press Numpad 4 twice to see the change. The INI is re-read every time.
4. **Numpad 7**: rack.
5. **Numpad 8**: hit the cue ball the way you're facing. Shift = soft, Ctrl = hard. Wait for the balls to stop
   before the next shot. A scratched cue ball goes back on the head spot.
6. **Numpad 9**: take the balls away.

Balls are removed automatically before the game saves.

`Documents\My Games\Fallout4\F4SE\fo4pool.log` records what happened (form IDs, the ball mesh's measured size, every shot).

## The balls

The plugin spawns a static object for each ball and moves its 3D itself, so the balls must not use Havok. The
vanilla pool balls (`Meshes\Props\Poolballs\Poolball_Cue.nif`, `Poolball_01.nif` to `Poolball_15.nif`) are physics
objects, so fo4pool uses collision-free copies of them:

```
python tools/strip_collision.py Poolball_Cue.nif Data/Meshes/fo4pool/Ball_Cue.nif
python tools/strip_collision.py Poolball_01.nif  Data/Meshes/fo4pool/Ball_01.nif   (and so on to 15)
python tools/make_esl.py Data/fo4pool.esl
```

`fo4pool.esl` holds 16 statics: the cue ball at `xx000800` and ball n at `xx000800 + n`. Bethesda's meshes aren't
in this repository; run the tools on the ones from your own game.

Any other static works too: point `[Ball]` in the INI at it. If an object with Havok is used anyway, the plugin
switches its collision to keyframed so the physics engine leaves it alone.

## Tables

Racking prefers whatever the crosshair points at whose mesh path contains "PoolTable". The INI defaults are measured
from the vanilla `SetDressing\PoolTable\PoolTable01.nif`: felt at height 70.4, 224 × 112 between the cushion noses,
length along the mesh's Y axis.

## Building

Built with [xmake](https://xmake.io) and [Dear-Modding CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4),
the same setup as Real Videos:

```
git submodule update --init --recursive
xmake f -m releasedbg
xmake build fo4pool
```

GitHub Actions builds the DLL on every push and uploads it as an artifact, ready to install.

### Physics tests

The simulation (`src/sim`) has no game dependencies and is tested on its own:

```
g++ -std=c++20 -O2 -Isrc tests/sim_test.cpp src/sim/Sim.cpp -o sim_test && ./sim_test
```

## How it works

- `src/sim`: the table simulation in table space (x along the length, y across, z up, game units). Ball-ball and
  cushion collisions with restitution, rolling resistance, pocket openings with jaws, rolling rotation. Fixed small
  sub-steps, deterministic.
- `src/Game.cpp`: picks the table, spawns the balls with `TESDataHandler::CreateReferenceAtLocation`, and every
  frame converts each ball's table position and rotation into a world transform on its 3D node.
- `src/Hook.cpp`: a Present hook gives a call every frame; the work itself is queued onto the game's main thread.

## License

GPL-3.0
