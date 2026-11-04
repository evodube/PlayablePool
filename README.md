# fo4pool

Playable pool tables for Fallout 4, as an F4SE plugin. The balls are real objects on the table, moved by the
plugin's own physics (not Havok).

**Status: test build 0.1.** Proves the core loop only: balls appear on a table and roll believably. No camera,
HUD, rules or opponents yet.

## Requirements

- Fallout 4 1.10.163 ("Old Gen"). The plugin is also built for Next-Gen and Anniversary, but only OG is targeted.
- F4SE and Address Library for F4SE Plugins for your game version.
- A ball object: `fo4pool.esl` with one static (see below).

## Trying it

1. Install `Data/F4SE/Plugins/fo4pool.dll` and `fo4pool.ini`, plus `fo4pool.esl`.
2. Go to a pool table. Look at it (or click it in the console).
3. **Numpad 4**: calibration markers appear on the felt corners, side pockets, head and foot spots and centre.
   If they don't sit on the felt, edit `[Table]` in `fo4pool.ini` (length, width, height, offsets, axis), then
   press Numpad 4 twice to see the change. The INI is re-read every time.
4. **Numpad 7**: rack.
5. **Numpad 8**: hit the cue ball the way you're facing. Shift = soft, Ctrl = hard. Wait for the balls to stop
   before the next shot. A scratched cue ball goes back on the head spot.
6. **Numpad 9**: take the balls away.

Balls are removed automatically before the game saves.

`Data/F4SE/Plugins/fo4pool.log` records what happened (form IDs, the ball mesh's measured size, every shot).

## The ball object

The plugin spawns a static object for each ball and moves its 3D itself, so the object must not use Havok.
Any ball-shaped mesh works for testing; with `bAutoScale=1` the plugin measures the mesh and scales it to
`fRadius` (2.0 units, a real 57 mm ball). Numbered textures come later.

To make the ESL from a mesh path (relative to `Data\Meshes`):

```
python tools/make_ball_esl.py "Some\\Folder\\Ball.nif"
```

Or make it by hand in xEdit/the Creation Kit: a new light plugin `fo4pool.esl` with one STAT using the mesh, form
ID `xx000800`. Any other static works too: point `[Ball] sPlugin` and `iFormID` at it.

If the mesh has collision, that collision stays where the ball was spawned (only the visible ball moves), so a
mesh without collision is better.

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
