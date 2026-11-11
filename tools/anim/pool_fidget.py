"""The waiting NPC's fidgets (Blender 5.2 + PyNifly 30.2), run in pool_anims.blend with PoolWait.hkx imported onto
FO4Human (action "PoolWait").

Each fidget starts and ends on PoolWait's frame 1 pose, so it goes back into the waiting loop without a jump. Every
frame is that pose plus a few movements (hips, spine, shoulders, head), then the limbs are put back: the feet stay
planted, the right hand stays on the cue (or moves with it, for the tap) and the left hand stays on the hip. The
frames are worked out with no action on the rig, then keyed into a new action:

    build("PoolFidgetLean", 120, lean)       # leaning on the cue, weight over to it, and back
    build("PoolFidgetStretch", 150, stretch) # two shoulder rolls, the neck stretched side to side
    build("PoolFidgetTap", 90, tap)          # lifting the cue and tapping its butt on the floor twice
    build("PoolFidgetTable", 150, over_table) # leaning over the table on the left hand, looking the balls over
    build("PoolFidgetChalk", 150, chalk)     # chalking the cue's tip: the left hand brings the chalk up, twists it, back

Export each with export_scene.pynifly_hkx (FO4, 30 fps) inside a 3D-viewport temp_override, as for the other clips.
In game the cue's butt follows the grip up off the floor while the tap plays (see Game::PlaceStickUpright).
"""

import bpy, math
from mathutils import Vector, Matrix, Quaternion

arm = bpy.data.objects["FO4Human"]
P = arm.pose.bones
B = arm.data.bones


def upd():
    bpy.context.view_layer.update()


# ------------------------------------------------------------------ the base pose: PoolWait's first frame
def base_pose():
    arm.animation_data.action = bpy.data.actions["PoolWait"]
    bpy.context.scene.frame_set(1)
    basis = {pb.name: pb.matrix_basis.copy() for pb in P}
    arm.animation_data.action = None
    return basis


def apply(basis):
    for pb in P:
        pb.matrix_basis = basis[pb.name].copy()
    upd()


# ------------------------------------------------------------------ helpers (armature space = actor space, facing +Y)
def rot_world(name, axis, deg, pivot=None):
    """turn a bone (and so everything below it) about a world axis through its head"""
    pb = P[name]
    h = pivot if pivot is not None else pb.head.copy()
    q = Quaternion(Vector(axis).normalized(), math.radians(deg))
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix
    upd()


def move(name, offset):
    pb = P[name]
    pb.matrix = Matrix.Translation(Vector(offset)) @ pb.matrix
    upd()


def aim_bone(name, child, to_point):
    pb = P[name]
    h = pb.head.copy()
    q = (P[child].head - h).normalized().rotation_difference((to_point - h).normalized())
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix
    upd()


def limb(upper, lower, end, target, hint):
    """two-bone IK: put `end`'s head on target, the middle joint bending towards hint"""
    a = (B[lower].head_local - B[upper].head_local).length
    b = (B[end].head_local - B[lower].head_local).length
    S = P[upper].head.copy()
    dv = target - S
    d = min(dv.length, a + b - 0.05)
    u = dv.normalized()
    x = (a * a - b * b + d * d) / (2 * d)
    r = math.sqrt(max(0.0, a * a - x * x))
    n = hint - u * hint.dot(u)
    n = n.normalized() if n.length > 1e-6 else Vector((0, 0, 1))
    E = S + u * x + n * r
    aim_bone(upper, lower, E)
    aim_bone(lower, end, S + u * d)


def pin(end, upper, lower, world, rot, hint):
    """the limb re-solved so `end` is back at `world`, turned as `rot` (a 3x3 in armature space)"""
    limb(upper, lower, end, world, hint)
    P[end].matrix = Matrix.Translation(world) @ rot.to_4x4()
    upd()


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def env(t, a=0.18, b=0.22):
    """0 -> 1 over the first a of the clip, 1 -> 0 over the last b"""
    return smooth(t / a) * smooth((1 - t) / b)


def bump(t, t0, t1, rise=0.6):
    """0 -> 1 -> 0 between t0 and t1, peaking `rise` of the way through"""
    if t <= t0 or t >= t1:
        return 0.0
    s = (t - t0) / (t1 - t0)
    return smooth(s / rise) if s < rise else smooth((1 - s) / (1 - rise))


# ------------------------------------------------------------------ the fidgets: f(t in 0..1) poses on top of the base
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


def lean(t, keep):
    w = env(t, 0.25, 0.3)
    sway = math.sin(t * math.pi * 3) * 0.15 * w  # a little settling while leaning
    move("COM", (3.8 * w + sway, 0.4 * w, -1.2 * w))  # the hips over to the cue side (right), sinking a touch
    rot_world("COM", Z, -4 * w)  # turning a little towards the cue
    rot_world("Spine1", Y, 4.5 * w)  # the upper body leaning on to it
    rot_world("Chest", Y, 2.0 * w)
    rot_world("Neck", Y, -3.0 * w)  # the head kept nearly level
    rot_world("Head", X, -4.0 * w)  # looking down at the table a little
    return {"hand_r": Vector((0, 0, -0.8 * w))}  # their weight on the cue: the hand sinks a touch


def stretch(t, keep):
    w = env(t, 0.12, 0.18)
    out = {}
    # two shoulder rolls in the first 60%: up, back, down
    if 0.08 < t < 0.62:
        th = (t - 0.08) / 0.54 * 4 * math.pi
        lift = 15.0 * math.sin(th) if math.sin(th) > 0 else 4.0 * math.sin(th)
        back = 11.0 * (1 - math.cos(th)) / 2
        rot_world("Arm_Collarbone.L", Y, lift)
        rot_world("Arm_Collarbone.R", Y, -lift)
        rot_world("Arm_Collarbone.L", Z, back)
        rot_world("Arm_Collarbone.R", Z, -back)
        rot_world("Chest", X, -3.0 * (1 - math.cos(th)) / 2)  # the chest opening up as the shoulders go back
    # then the neck stretched to one side, the other, and back
    n = bump(t, 0.5, 0.74, 0.5) - bump(t, 0.7, 0.95, 0.5)
    rot_world("Neck", Y, 9.0 * n)
    rot_world("Head", Y, 6.0 * n)
    rot_world("Spine2", X, -2.0 * w)  # standing a bit taller through it
    return out


def tap(t, keep):
    # the cue lifted and its butt tapped on the floor twice: up slowly, down quick
    up = 5.0 * bump(t, 0.12, 0.36, 0.75) + 4.0 * bump(t, 0.38, 0.6, 0.75)
    look = env(t, 0.15, 0.25)
    rot_world("Head", X, -7.0 * look)  # watching the butt
    rot_world("Neck", X, -3.0 * look)
    rot_world("Spine2", X, -1.0 * look)
    return {"hand_r": Vector((0, 0, up)), "lift": up}


# The table in front of them while they wait (Opponent.cpp: the wait spot is kWaitBeyondRail 55 outside the felt,
# facing it): the rail's outer edge about 40 ahead, its top at about 75.
RAIL_Y = 44.0
RAIL_Z = 75.5
CUE_LEN = 94.0
BUTT = Vector((14.5, 30.5, 0.6))  # where the cue's butt rests while they wait (Game::PlaceStickUpright)


def grip_point():
    """the grip in the right hand (the same point the game holds the cue at): wrist frame, see tools/anim/README.md"""
    w = P["Arm_Hand.R"].head
    fb = (P["Arm_Finger21.R"].head + P["Arm_Finger31.R"].head + P["Arm_Finger41.R"].head + P["Arm_Finger51.R"].head) / 4
    e1 = (fb - w).normalized()
    e2 = P["Arm_Finger21.R"].head - P["Arm_Finger51.R"].head
    e2 = (e2 - e1 * e2.dot(e1)).normalized()
    e3 = e1.cross(e2)
    return w + 6.478 * e1 + 0.133 * e2 - 1.488 * e3


def cue_tip():
    d = (grip_point() - BUTT).normalized()
    return BUTT + d * CUE_LEN, d


def frame3(a, b):
    a = a.normalized()
    c = a.cross(b).normalized()
    b = c.cross(a).normalized()
    return Matrix((a, b, c)).transposed()


def hand_rot(side, fingers, thumbward):
    """the hand's world rotation with the middle finger along `fingers` and the little finger -> thumb line along
    `thumbward`"""
    name = "Arm_Hand." + side
    f_rest = B["Arm_Finger31." + side].head_local - B[name].head_local
    t_rest = B["Arm_Finger11." + side].head_local - B["Arm_Finger51." + side].head_local
    q = frame3(fingers, thumbward) @ frame3(f_rest, t_rest).inverted()
    return q @ B[name].matrix_local.to_3x3()


def over_table(t, keep):
    """bending over the table, the left hand on the rail, looking the balls over left and right"""
    w = env(t, 0.22, 0.24)
    move("COM", (0.0, -2.5 * w, -2.0 * w))  # the hips go back as they bend
    rot_world("Spine1", X, -14 * w)  # bending forward over the rail
    rot_world("Spine2", X, -12 * w)
    rot_world("Chest", X, -8 * w)
    rot_world("Neck", X, 12 * w)  # the head up, to see across the table
    look = math.sin((t - 0.25) / 0.5 * 2 * math.pi) if 0.25 < t < 0.75 else 0.0
    rot_world("Head", Z, 22 * look * w)  # looking along the table one way and the other
    rot_world("Neck", Z, 8 * look * w)
    hand = keep["hand_l"][0].lerp(Vector((-13.0, RAIL_Y, RAIL_Z + 2.6)), smooth((w - 0.15) / 0.85))
    rot = keep["hand_l"][1]
    flat = hand_rot("L", Vector((0.35, 1.0, -0.15)), Vector((1.0, -0.3, 0.0)))  # palm down on the rail, fingers forward
    k = smooth((w - 0.3) / 0.7)
    rot = rot.to_quaternion().slerp(flat.to_quaternion(), k).to_matrix()
    return {"hand_l_at": (hand, rot)}


def chalk(t, keep):
    """the left hand leaves the hip with the chalk, chalks the tip with three twists, and goes back"""
    tip, d = keep["tip"]
    reach = smooth((t - 0.08) / 0.22) * smooth((0.92 - t) / 0.22)  # to the tip and back
    # palm down over the tip: fingers pointing forward and in, the thumb towards the right
    over = hand_rot("L", Vector((0.25, 1.0, -0.2)), Vector((1.0, -0.2, 0.0)))
    on = tip + Vector((-3.2, -4.6, 3.4))  # the wrist, so the chalk under the fingers sits on the tip
    hand = keep["hand_l"][0].lerp(on, reach)
    rot = keep["hand_l"][1].to_quaternion().slerp(over.to_quaternion(), reach).to_matrix()
    # three twists back and forth round the cue while it's on
    if 0.32 < t < 0.7:
        tw = math.sin((t - 0.32) / 0.38 * 3 * 2 * math.pi) * 28.0 * smooth((t - 0.32) / 0.05) * smooth((0.7 - t) / 0.05)
        q = Quaternion(d, math.radians(tw))
        hand = tip + q @ (hand - tip)
        rot = q.to_matrix() @ rot
    look = env(t, 0.15, 0.2)
    rot_world("Head", X, 10 * look)  # chin up a little, eyes on the tip
    rot_world("Head", Z, -10 * look)
    rot_world("Chest", Z, -4 * look)  # turned a touch towards the cue
    return {"hand_l_at": (hand, rot)}


# ------------------------------------------------------------------ build: frames worked out, then keyed
def frames(n, fn):
    basis = base_pose()
    apply(basis)
    keep = {
        "hand_r": (P["Arm_Hand.R"].head.copy(), P["Arm_Hand.R"].matrix.to_3x3().copy()),
        "hand_l": (P["Arm_Hand.L"].head.copy(), P["Arm_Hand.L"].matrix.to_3x3().copy()),
        "foot_r": (P["Leg_Foot.R"].head.copy(), P["Leg_Foot.R"].matrix.to_3x3().copy()),
        "foot_l": (P["Leg_Foot.L"].head.copy(), P["Leg_Foot.L"].matrix.to_3x3().copy()),
        "elbow_r": P["Arm_ForeArm1.R"].head.copy(),
        "elbow_l": P["Arm_ForeArm1.L"].head.copy(),
        "knee_r": P["Leg_Calf.R"].head.copy(),
        "knee_l": P["Leg_Calf.L"].head.copy(),
        "com": P["COM"].head.copy(),
    }
    keep["tip"] = cue_tip()
    out = []
    lifts = []
    for f in range(n):
        t = f / (n - 1)
        apply(basis)
        com0 = P["COM"].head.copy()
        extra = fn(t, keep) or {}
        hip = P["COM"].head - com0
        # the feet back on the floor where they were
        for side, s in (("foot_l", "L"), ("foot_r", "R")):
            pos, rot = keep[side]
            hint = (keep["knee_" + s.lower()] - pos) + Vector((0, 8, 0))
            pin("Leg_Foot." + s, "Leg_Thigh." + s, "Leg_Calf." + s, pos, rot, hint)
        # the right hand on the cue, the left on the hip (moving with the hips)
        pos, rot = keep["hand_r"]
        pin("Arm_Hand.R", "Arm_UpperArm.R", "Arm_ForeArm1.R", pos + extra.get("hand_r", Vector()), rot,
            keep["elbow_r"] - pos + Vector((4, -4, 0)))
        pos, rot = keep["hand_l"]
        if "hand_l_at" in extra:
            at, rot = extra["hand_l_at"]
            pin("Arm_Hand.L", "Arm_UpperArm.L", "Arm_ForeArm1.L", at, rot, Vector((-1.0, -0.6, -0.4)))
        else:
            pin("Arm_Hand.L", "Arm_UpperArm.L", "Arm_ForeArm1.L", pos + hip, rot, keep["elbow_l"] - pos + Vector((-6, -4, 0)))
        out.append({pb.name: pb.matrix_basis.copy() for pb in P})
        lifts.append(extra.get("lift", 0.0))
    return out, lifts


def build(name, n, fn):
    poses, lifts = frames(n, fn)
    old = bpy.data.actions.get(name)
    if old:
        bpy.data.actions.remove(old)
    arm.animation_data.action = None
    for pb in P:
        pb.rotation_mode = "QUATERNION"
    for f, pose in enumerate(poses, start=1):
        for pb in P:
            pb.matrix_basis = pose[pb.name]
            pb.keyframe_insert("location", frame=f, group=pb.name)
            pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
    act = arm.animation_data.action
    act.name = name
    act.use_fake_user = True
    act["lift"] = lifts  # how far the cue's butt comes off the floor each frame (the tap)
    return act
