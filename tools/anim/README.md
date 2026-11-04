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
