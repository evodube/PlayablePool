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

**The shipped PoolAim / PoolStrike are an edit of these originals** (PoolAim_edit / PoolStrike_edit in
`pool_anims_v2.blend`; nothing above the torso changed): the right hand 8.8 out and 9 up from the shoulder
(collarbone +4 degrees, arm re-solved, the hand keeping its orientation), the right hand's fingers 2-5 wrapped round
the cue (radius 2 from its centre line; the thumb as in the original), and the left hand rolled 22 degrees flatter.

**In game the cue is placed from the hands, not from the reference cue above**:
- grip: a fixed point in the right hand's frame (origin the wrist, e1 = wrist -> the fingers' bases' average,
  e2 = little finger base -> index base across it, e3 = e1 x e2): 6.478 e1 + 0.133 e2 - 1.488 e3, inside the
  wrapped fingers;
- bridge: 3.0 above the midpoint of the left thumb's and index finger's bases (on top of the knuckles).

Measured that way at contact (PoolStrike frame 35): the tip at (0.59, 73.37) in the actor's space, the cue pointing
10.85 degrees to their left and sloping 7.3 degrees down to the tip, which meets the ball 0.4 below its centre.
`src/Opponent.cpp` stands them from those numbers (ball 77.0 ahead along the shot, root 14.4 left of the line,
turned 10.9 degrees towards it).

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

## PoolAim2 / PoolStrike2 / PoolWait2

From the user's two posed clips (aim2, poseidle2), animated in `pool_anims_v3.blend` (a copy of v2): each frame is the
pose plus small rotations about the bones' heads in the armature's space.

| Clip | File | Frames (30 fps) | What |
|---|---|---|---|
| PoolAim2 | PoolAim2.hkx | 120 (4 s), loops | the aim pose; two soft practice strokes (the right forearm swings from the elbow, the hand ~2.7 back), a breath, a small nod; the bridge hand moves < 0.15 |
| PoolStrike2 | PoolStrike2.hkx | 60 (2 s) | from PoolAim2's frame 1: settle, backswing ~7, pause, accelerating stroke, **contact on frame 35**, follow-through ~6, beginning to rise |
| PoolWait2 | PoolWait2.hkx | 180 (6 s), loops | the lean on the table; two breaths, a slow side-to-side sway, the head looking about; the hands move < 0.6 |

Exported with PyNifly (FO4, 30 fps, the vanilla skeleton.hkx); re-imported within 0.02 units. The originals' "WeaponDraw"
annotation isn't in these. Each NPC shot picks PoolAim or PoolAim2 at random and then plays the matching strike.

In game (`src/Opponent.cpp`): PoolAim2 / PoolStrike2 measured like PoolStrike at contact (frame 35): tip at
(-3.53, 85.53), the cue 14.8 degrees to their left, so the stance stands them further back. Each shot's aim is picked
when the shot is planned: PoolAim2 when PoolAim can't reach it, otherwise either. PoolWait2 is a side-on lean whose
body faces the clip's -X, so the root is turned a quarter right to have them face the table; the cue's butt goes
under the right hand at (-4, 32).

## PoolAim3 / PoolStrike3

From the user's aim3 pose, in `anim_tests\anim3\pool_anims_v4.blend` (PoolAim3Anim / PoolStrike3Anim):

| Clip | File | Frames (30 fps) | What |
|---|---|---|---|
| PoolAim3 | PoolAim3.hkx | 120 (4 s), loops | the pose with PoolAim2's breath and nod; two practice strokes (~3.6) with the grip sliding along the cue's line (shoulder and elbow solved together, the hand keeping its orientation), so the cue keeps its angle |
| PoolStrike3 | PoolStrike3.hkx | 60 (2 s) | PoolStrike2's stroke profile along the same straight line, **contact on frame 35**; nothing but the right arm moves |

Contact (frame 35): the tip at (8.06, 92.02), the cue 3.6 degrees to their left, sloping 9 degrees down. The
export wrote 1 fps again; patched to 30 like PoolAim2. Each shot picks at random among the aims that reach it.
