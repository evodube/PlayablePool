"""The waiting NPC's fidgets (Blender 5.2 + PyNifly 30.2), run in pool_anims.blend with PoolWait.hkx imported onto
FO4Human (action "PoolWait").

Each fidget starts and ends on PoolWait's frame 1 pose, so it goes back into the waiting loop without a jump. Every
frame is that pose plus a few movements (hips, spine, shoulders), then the limbs are put back: the feet stay planted,
the right hand stays on the cue (or moves with it, for the tap) and the left hand stays on the hip (or goes where the
fidget takes it). The frames are worked out with no action on the rig, then keyed into a new action:

    build("PoolFidgetLean", 120, lean)        # leaning on the cue, weight over to it, and back
    build("PoolFidgetStretch", 150, stretch)  # two shoulder rolls, the neck stretched side to side
    build("PoolFidgetTap", 90, tap)           # lifting the cue and tapping its butt on the floor twice
    build("PoolFidgetTable", 150, over_table) # leaning over the table on the left hand, looking the balls over
    build("PoolFidgetChalk", 150, chalk)      # chalking the cue's tip with the chalk pinched in the left hand

Where they look is never set straight on the head. Each fidget says where it wants them looking (yaw, pitch, roll),
and that goes through springs: the head gets there first, the neck a moment later and the chest last, each easing in
and settling (so it isn't stiff), with a little drift on top while they hold a look.

The chalk is held between the left thumb and the index and middle fingers (CHALK_IN_HAND, in the hand bone's own
frame) all through the chalking clip, so it moves with the hand; the hand is placed so that the chalk sits on the
cue's tip, then rubs and twists it there.

Export each with export_scene.pynifly_hkx (FO4, 30 fps) inside a 3D-viewport temp_override, as for the other clips.
In game the cue's butt follows the grip up off the floor while the tap plays (see Game::PlaceStickUpright), and the
chalk is placed from the left hand node with CHALK_IN_HAND while the chalking plays.
"""

import bpy, math
from mathutils import Vector, Matrix, Quaternion

arm = bpy.data.objects["FO4Human"]
P = arm.pose.bones
B = arm.data.bones

FPS = 30.0


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
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


def rot_world(name, axis, deg, pivot=None):
    """turn a bone (and so everything below it) about a world axis through its head"""
    if abs(deg) < 1e-6:
        return
    pb = P[name]
    h = pivot if pivot is not None else pb.head.copy()
    q = Quaternion(Vector(axis).normalized(), math.radians(deg))
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix
    upd()


def rot_local(name, axis, deg):
    """turn a bone about one of its own axes"""
    pb = P[name]
    h = pb.head.copy()
    ax = (pb.matrix.to_3x3() @ Vector(axis)).normalized()
    q = Quaternion(ax, math.radians(deg))
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


def path(t, keys):
    """a value eased from key to key: keys = [(t, value), ...] in order, held before the first and after the last"""
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            return v0 + (v1 - v0) * smooth((t - t0) / (t1 - t0))
    return keys[-1][1]


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


# ------------------------------------------------------------------ the table and the cue while they wait
# The table in front of them (Opponent.cpp: the wait spot is kWaitBeyondRail 55 outside the felt, facing it): the rail
# about 40 ahead, its top at about 75.
RAIL_Y = 44.0
RAIL_Z = 75.5
CUE_LEN = 94.0
BUTT = Vector((14.5, 30.5, 0.6))  # where the cue's butt rests while they wait (Game::PlaceStickUpright)
CHALK = 1.8  # the chalk cube's size


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


# ------------------------------------------------------------------ the left hand holding the chalk
PINCH = {  # finger curls (degrees about each joint's own Z; negative closes) for holding the chalk
    2: (-5, -15, -10),   # index
    3: (-5, -15, -10),   # middle
    4: (-45, -55, -35),  # ring and little finger tucked in
    5: (-45, -55, -35),
}


def pinch_fingers(w):
    for f, degs in PINCH.items():
        for j, d in zip((1, 2, 3), degs):
            rot_local("Arm_Finger%d%d.L" % (f, j), Z, d * w)
    rot_local("Arm_Finger12.L", Z, -30 * w)  # the thumb closed on to the index and middle fingers


def finger_tip(f, side="L"):
    """a fingertip: the rig's bones have no real ends (their tails are just drawn 5 long), so the last joint is carried
    on along the one before it"""
    h2 = P["Arm_Finger%d2.%s" % (f, side)].head
    h3 = P["Arm_Finger%d3.%s" % (f, side)].head
    return h3 + (h3 - h2) * 0.85


def pinch_point():
    """between the thumb's tip and the index and middle finger tips: where the chalk is held"""
    return (finger_tip(1) * 2 + finger_tip(2) + finger_tip(3)) / 4


# ------------------------------------------------------------------ the fidgets: f(t in 0..1, keep) -> what they do
# Each returns a dict: "look": (yaw, pitch, roll) in degrees (yaw + is to their left, pitch + up, roll + their head
# tipped to the right); "hand_r": the right hand's offset; "hand_l_at": (position, rotation) for the left hand, or
# "chalk_on": (point, rotation, twist axis, twist degrees) to put the chalk on; "lift": the cue's butt off the floor;
# "pinch": 0..1 the left hand's fingers closed round the chalk.


def lean(t, keep):
    w = env(t, 0.25, 0.3)
    sway = math.sin(t * math.pi * 3) * 0.15 * w  # a little settling while leaning
    move("COM", (3.8 * w + sway, 0.4 * w, -1.2 * w))  # the hips over to the cue side (right), sinking a touch
    rot_world("COM", Z, -4 * w)  # turning a little towards the cue
    rot_world("Spine1", Y, 4.5 * w)  # the upper body leaning on to it
    rot_world("Chest", Y, 2.0 * w)
    look = path(t, [(0.0, 0), (0.25, -6), (0.55, -9), (0.7, 4), (0.8, 4), (1.0, 0)])
    return {"hand_r": Vector((0, 0, -0.8 * w)), "look": (look, -6 * w, -3.0 * w)}  # head kept nearly level as they lean


def stretch(t, keep):
    w = env(t, 0.12, 0.18)
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
    rot_world("Spine2", X, -2.0 * w)  # standing a bit taller through it
    # the head eased to one side, held, over to the other, held, back: a stretch, not a twitch
    roll = path(t, [(0.0, 0), (0.55, 0), (0.64, 13), (0.7, 14), (0.79, -13), (0.86, -14), (0.96, 0)])
    pitch = path(t, [(0.0, 0), (0.1, 3), (0.55, 3), (0.6, -2), (0.9, -2), (1.0, 0)])
    return {"look": (0.0, pitch, roll)}


def tap(t, keep):
    # the cue lifted and its butt tapped on the floor twice: up slowly, down quick
    up = 5.0 * bump(t, 0.12, 0.36, 0.75) + 4.0 * bump(t, 0.38, 0.6, 0.75)
    look = env(t, 0.15, 0.25)
    rot_world("Spine2", X, -1.0 * look)
    return {"hand_r": Vector((0, 0, up)), "lift": up, "look": (-12 * look, -16 * look, -2 * look)}  # watching the butt


def over_table(t, keep):
    """bending over the table, the left hand on the rail, looking the balls over"""
    w = env(t, 0.22, 0.24)
    move("COM", (0.0, -2.5 * w, -2.0 * w))  # the hips go back as they bend
    rot_world("Spine1", X, -14 * w)  # bending forward over the rail
    rot_world("Spine2", X, -12 * w)
    rot_world("Chest", X, -8 * w)
    hand = keep["hand_l"][0].lerp(Vector((-13.0, RAIL_Y, RAIL_Z + 2.6)), smooth((w - 0.15) / 0.85))
    flat = hand_rot("L", Vector((0.35, 1.0, -0.15)), Vector((1.0, -0.3, 0.0)))  # palm down on the rail
    k = smooth((w - 0.3) / 0.7)
    rot = keep["hand_l"][1].to_quaternion().slerp(flat.to_quaternion(), k).to_matrix()
    # reading the table: a look one way, a pause, across the other way, a pause, back
    yaw = path(t, [(0.0, 0), (0.24, 0), (0.33, 24), (0.44, 26), (0.58, -22), (0.68, -24), (0.8, 0)])
    pitch = path(t, [(0.0, 0), (0.2, 14), (0.4, 11), (0.6, 13), (0.8, 10), (1.0, 0)])  # up, to see across
    return {"hand_l_at": (hand, rot), "look": (yaw, pitch * w, yaw * 0.12)}


def chalk(t, keep):
    """the left hand comes off the hip with the chalk, chalks the tip (rubbing it round and twisting), and goes back"""
    tip, d = keep["tip"]
    reach = smooth((t - 0.06) / 0.22) * smooth((0.94 - t) / 0.22)  # up to the tip and back down
    # palm down over the tip, fingers forward and a little in, the thumb towards the right
    over = hand_rot("L", Vector((0.3, 1.0, -0.35)), Vector((1.0, -0.25, 0.05)))
    on = smooth((t - 0.26) / 0.05) * smooth((0.72 - t) / 0.05)  # the chalk on the tip
    rub, twist = Vector(), 0.0
    if 0.0 < on:
        s = (t - 0.26) / 0.46
        a = s * 3 * 2 * math.pi  # three rubs round the tip
        side = d.cross(Vector(X)).normalized()
        fwd = d.cross(side).normalized()
        rub = (side * math.cos(a) + fwd * math.sin(a)) * 0.45 * on
        twist = math.sin(a) * 30.0 * on  # and the wrist twisting back and forth with them
    point = tip + d * (CHALK * 0.5 + 0.3 * (1 - on)) + rub  # the chalk's centre, just over the tip till it goes on
    look = smooth((t - 0.04) / 0.2) * smooth((0.96 - t) / 0.18)
    return {
        "chalk_on": (point, over, d, twist, reach),
        "pinch": 1.0,
        "look": (-16 * look, -12 * look, -3 * look),  # eyes on the tip
    }


# ------------------------------------------------------------------ where they look: springs, head first
def springs(targets, omegas):
    """critically damped springs following each channel of `targets` (lists of floats per frame)"""
    out = []
    for omega in omegas:
        x = [0.0] * len(targets[0])
        v = [0.0] * len(targets[0])
        seq = []
        for tgt in targets:
            for _ in range(4):  # sub-steps
                h = 1.0 / FPS / 4
                for c in range(len(x)):
                    a = omega * omega * (tgt[c] - x[c]) - 2 * omega * v[c]
                    v[c] += a * h
                    x[c] += v[c] * h
            seq.append(list(x))
        out.append(seq)
    return out


def drift(f, n, seed):
    """a little wandering on top of a held look (degrees), fading in and out with the clip"""
    t = f / FPS
    e = env(f / (n - 1), 0.2, 0.25)
    yaw = 0.9 * math.sin(t * 1.7 + seed) + 0.5 * math.sin(t * 3.1 + seed * 2.3)
    pitch = 0.6 * math.sin(t * 1.3 + seed * 1.7) + 0.3 * math.sin(t * 2.9 + seed)
    return yaw * e, pitch * e


def apply_look(look_head, look_neck, look_chest):
    """the look shared out: the chest a little of the turn, the neck some, the head the rest"""
    yc, pc, rc = look_chest
    yn, pn, rn = look_neck
    yh, ph, rh = look_head
    rot_world("Chest", Z, 0.15 * yc)
    rot_world("Neck", Z, 0.35 * yn)
    rot_world("Neck", X, 0.4 * pn)
    rot_world("Neck", Y, 0.35 * rn)
    rot_world("Head", Z, 0.5 * yh)
    rot_world("Head", X, 0.6 * ph)
    rot_world("Head", Y, 0.65 * rh)


# ------------------------------------------------------------------ build: frames worked out, then keyed
CHALK_IN_HAND = None  # the chalk's matrix in the left hand bone's frame (worked out by the chalking clip)


def frames(n, fn):
    global CHALK_IN_HAND
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
    }
    keep["tip"] = cue_tip()
    # the chalk in the hand: the pinch point, in the hand's own frame, with the fingers closed on it
    apply(basis)
    pinch_fingers(1.0)
    hm = P["Arm_Hand.L"].matrix.copy()
    chalk_world = Matrix.Translation(pinch_point()) @ hm.to_3x3().to_4x4()
    in_hand = hm.inverted() @ chalk_world

    # pass 1: where they want to look, frame by frame, then the springs (the head quickest, the chest slowest)
    wants = []
    for f in range(n):
        apply(basis)
        wants.append(list((fn(f / (n - 1), keep) or {}).get("look", (0.0, 0.0, 0.0))))
    head, neck, chest = springs(wants, (11.0, 7.0, 4.0))

    out, lifts, chalks = [], [], []
    for f in range(n):
        t = f / (n - 1)
        apply(basis)
        com0 = P["COM"].head.copy()
        extra = fn(t, keep) or {}
        hip = P["COM"].head - com0
        dy, dp = drift(f, n, seed=sum(map(ord, fn.__name__)) % 7)
        apply_look((head[f][0] + dy, head[f][1] + dp, head[f][2]), neck[f], chest[f])
        # the feet back on the floor where they were
        for side, s in (("foot_l", "L"), ("foot_r", "R")):
            pos, rot = keep[side]
            hint = (keep["knee_" + s.lower()] - pos) + Vector((0, 8, 0))
            pin("Leg_Foot." + s, "Leg_Thigh." + s, "Leg_Calf." + s, pos, rot, hint)
        # the right hand on the cue
        pos, rot = keep["hand_r"]
        pin("Arm_Hand.R", "Arm_UpperArm.R", "Arm_ForeArm1.R", pos + extra.get("hand_r", Vector()), rot,
            keep["elbow_r"] - pos + Vector((4, -4, 0)))
        # the left hand: on the hip, on the rail, or chalking
        pos, rot = keep["hand_l"]
        if "chalk_on" in extra:
            point, over, axis, twist, reach = extra["chalk_on"]
            q = Quaternion(axis, math.radians(twist))
            want_rot = q.to_matrix() @ over
            # where the wrist goes so that the chalk (in the hand) is on `point`
            wrist_on = point - want_rot @ (in_hand.translation)
            hand_pos = (pos + hip).lerp(wrist_on, reach)
            hand_rot_ = rot.to_quaternion().slerp(want_rot.to_quaternion(), reach).to_matrix()
            pin("Arm_Hand.L", "Arm_UpperArm.L", "Arm_ForeArm1.L", hand_pos, hand_rot_, Vector((-1.0, -0.8, -0.6)))
        elif "hand_l_at" in extra:
            at, r = extra["hand_l_at"]
            pin("Arm_Hand.L", "Arm_UpperArm.L", "Arm_ForeArm1.L", at, r, Vector((-1.0, -0.6, -0.4)))
        else:
            pin("Arm_Hand.L", "Arm_UpperArm.L", "Arm_ForeArm1.L", pos + hip, rot, keep["elbow_l"] - pos + Vector((-6, -4, 0)))
        if extra.get("pinch"):
            pinch_fingers(extra["pinch"])
        out.append({pb.name: pb.matrix_basis.copy() for pb in P})
        lifts.append(extra.get("lift", 0.0))
        chalks.append(1.0 if "chalk_on" in extra else 0.0)
    if any(chalks):
        CHALK_IN_HAND = in_hand
    return out, lifts, chalks, in_hand


def build(name, n, fn):
    poses, lifts, chalks, in_hand = frames(n, fn)
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
    if any(chalks):
        act["chalk_in_hand"] = [v for row in in_hand for v in row]  # the chalk in the left hand bone's frame
    return act
