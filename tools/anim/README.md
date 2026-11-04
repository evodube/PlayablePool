# Pool animations

`pool_anims.blend` (Blender 5.2 + PyNifly 30.2) holds Fallout 4's human rig, imported from the vanilla
`skeleton.hkx`, a game-scale table, cue ball and cue, and two clips. Hand-animated, rotations driven by a
small two-bone IK script (the `pool_ik` text block), no constraints left to bake.

| Clip | File | Frames (30 fps) | What |
|---|---|---|---|
| PoolAim | PoolAim.hkx | 90 (3 s), loops | lined up, two practice strokes of 7 units, one breath |
| PoolStrike | PoolStrike.hkx | 60 (2 s) | settle, backswing 11 units, pause, accelerating stroke, follow-through 7 units |

**Cue ball contact: PoolStrike frame 35** (about 1.13 s in). The game should strike the ball then.

Shot geometry, in the actor's own space (root at the origin, facing +Y; game units):

- cue line: x = 8.5, along +Y, rising about 2 degrees towards the butt
- cue tip at rest: (8.5, 60.6, 73.9); cue ball centre: (8.5, 66.1, 73.9) (felt 70.4 + radius 3.51)
- so the actor stands with its root 66.1 units behind the cue ball along the shot line and 8.5 units to the
  left of it (the line runs under the chin)
- bridge hand: left, flat on the felt at y = 40..48; grip hand: right, wrist at about (9.5, -8.5, 80.9)

Exports are checked by re-importing them: within 0.05 units and 0.1 degrees of the Blender clip.

**In game the cue is placed from the hands, not from the reference cue above**: its butt end through the right palm
(between the wrist and the middle finger's base), resting 3.0 above the left hand's thumb-index V (clear of the knuckles). Measured that way at
contact (PoolStrike frame 35), the tip is at (5.17, 74.87) in the actor's space and the cue points 2.77 degrees to
their left; `src/Opponent.cpp` stands them from those numbers (ball 79.5 ahead along the shot, root 8.8 left of the
line, turned 2.8 degrees towards it). The 66.1 / 8.5 above were for the reference cue and left the hand-placed cue
about 13 units through the ball.

An edit of both clips (PoolAim_edit / PoolStrike_edit in `pool_anims_v2.blend`) moves the right hand 8.8 out and 4
up from the shoulder (collarbone +4 degrees, arm re-solved; the hand keeps its orientation) and rolls the left hand
22 degrees flatter; nothing above the torso changes. Its contact numbers: tip (-0.47, 72.72), yaw -13.01, cue sloping
4.2 degrees down to the tip, its centre line 2.5 above the bridge hand's knuckle joints.

PoolAim and PoolStrike are the hand-keyed originals above. A scripted re-pose of both (an open stance, PoolAim2 and
PoolStrike2 in `pool_anims_v2.blend`) was tried and dropped in favour of these.

## PoolWait

| Clip | File | Frames (30 fps) | What |
|---|---|---|---|
| PoolWait | PoolWait.hkx | 180 (6 s), loops | standing at the table between shots: cue upright in the right hand, butt on the floor by the right foot, left hand on the hip; weight shift, two breaths, a glance left and right |

Posed by script: `pose_wait` / `wait_params` / `bake_wait` in `pool_pose.py` (the `pool_pose` text block in
`pool_anims_v2.blend`). Root midway between the feet, facing the table; the cue's butt rests at (11, 7, 0.6).

## In the mod

The clips ship at `Data\Meshes\Actors\Character\Animations\fo4pool\` and `fo4pool.esl` has two dynamic idles that
point at them (`tools/make_esl.py`): `fo4poolAim` (0x811) and `fo4poolStrike` (0x812), both on
`RaiderRootBehavior.hkx` with the `dyn_ActivationLoop` event, so no behaviour file is touched.

They're played by the NPC opponent (`src/Opponent.cpp`, `src/Anim.cpp`): on their turn the NPC walks
(Papyrus `PathToReference`) to an XMarkerHeading placed at the stance above, is snapped onto it (`MoveTo`), plays
PoolAim for `[Opponent] fAimSeconds`, then PoolStrike, and the cue ball is struck 35 frames into it. When the cue
ball is far from the rail the stance would be over the table, so they stand back at the rail instead and the clip's
reach falls short of the ball.
