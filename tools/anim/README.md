# Pool animations

`pool_anims_v2.blend` (on the PC, in `anim_tests`) holds Fallout 4's human rig, imported from the vanilla
`skeleton.hkx`, a game-scale table, cue ball and cue. The clips are posed by script, `pool_pose.py` here (the
`pool_pose` text block in the scene): every frame is built from where the cue is and a handful of body parameters,
with two-bone IK for the arms and legs, and keyed on every bone. No constraints to bake.

| Clip | File | Frames (30 fps) | What |
|---|---|---|---|
| PoolAim2 | PoolAim.hkx | 120 (4 s), loops | settled; three practice strokes of 6 to 7.5 units; sway against the stroke, breath, small head moves |
| PoolStrike2 | PoolStrike.hkx | 60 (2 s) | backswing 12 units, pause, stroke speeding up to the ball, follow-through 9, starts to stand up |

**Cue ball contact: PoolStrike frame 35** (about 1.13 s in). The game strikes the ball then.

Stance: open, right-handed. Left foot forward, right foot back and turned out, hips turned 40 degrees towards the
cue line; the cue runs beside the right hip, so the grip hand hangs outside the body (palm on the outside of the cue,
fingers wrapped under it); the bridge hand lies flat on the felt with the cue in the V of thumb and index.

Shot geometry, in the actor's own space (root midway between the feet, facing +Y; game units):

- cue line: x = 13, along +Y, rising 3 degrees towards the butt
- cue ball centre: (13, 89, 73.9) (felt 70.4 + radius 3.51)
- so the actor stands with its root 89 units behind the cue ball along the shot line and 13 to the left of it
- the table front can be 64 behind the cue ball (the left foot's toes and the hips stay clear of it)

Exports are checked by re-importing them: within 0.1 units of the Blender clip on every bone checked.

## In the mod

The clips ship at `Data\Meshes\Actors\Character\Animations\fo4pool\` and `fo4pool.esl` has two dynamic idles that
point at them (`tools/make_esl.py`): `fo4poolAim` (0x811) and `fo4poolStrike` (0x812), both on
`RaiderRootBehavior.hkx` with the `dyn_ActivationLoop` event, so no behaviour file is touched.

They're played by the NPC opponent (`src/Opponent.cpp`, `src/Anim.cpp`): on their turn the NPC walks
(Papyrus `PathToReference`) to an XMarkerHeading placed at the stance above, is snapped onto it (`MoveTo`), plays
PoolAim for `[Opponent] fAimSeconds`, then PoolStrike, and the cue ball is struck 35 frames into it. When the cue
ball is far from the rail the stance would be over the table, so they stand back at the rail instead and the clip's
reach falls short of the ball.
