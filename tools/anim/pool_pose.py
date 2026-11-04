"""The pool clips' posing script (Blender 5.2 + PyNifly 30.2), the `pool_pose` text block in pool_anims_v2.blend.

Every frame is posed from the cue and a few body parameters (stroke, sway, breath, head nod, rise), then keyed on
every bone: bake_clip("PoolAim2", 120, aim_params) and bake_clip("PoolStrike2", 60, strike_params). Export each with
export_scene.pynifly_hkx (game FO4, 30 fps, reference_skel = the vanilla skeleton.hkx) inside a 3D-viewport
temp_override.
"""

import bpy, math
from mathutils import Vector, Matrix, Quaternion, Euler

arm = bpy.data.objects["FO4Human"]; P = arm.pose.bones; B = arm.data.bones
def upd(): bpy.context.view_layer.update()

# ---------------------------------------------------------------- stance geometry (actor space, facing +Y)
LINE_X = 13.0          # the cue line runs along x = 13 (beside the right hip)
BALL = Vector((LINE_X, 89.0, 73.9))   # cue ball centre: felt 70.4 + radius 3.51; the root is midway between the feet
FELT_Z = 70.4
CUE_LEN = 94.0; GRIP_FROM_BUTT = 12.0; GAP = 1.5; RADIUS = 3.51
ELEV = math.radians(3.0)               # the butt rises 3 degrees
BRIDGE_BACK = 19.0                     # bridge hand this far behind the cue ball
GRIP_R = 2.3                           # the cue's radius at the grip plus half a finger

def rest_rot(name):
    return B[name].matrix_local.to_3x3()

def frame_from(up, right):
    up = up.normalized(); fwd = up.cross(right).normalized(); right = fwd.cross(up).normalized()
    m = Matrix((right, fwd, up)).transposed()   # columns right, fwd, up
    return m

def orient(name, up, right, head=None):
    """Bone's world orientation = rest orientation turned so world Z -> up and world X -> right."""
    pb = P[name]
    h = head if head is not None else pb.head.copy()
    q = frame_from(up, right)
    pb.matrix = Matrix.Translation(h) @ (q @ rest_rot(name)).to_4x4()
    upd()

def turn(name, q, head=None):
    pb = P[name]; h = head if head is not None else pb.head.copy()
    pb.matrix = Matrix.Translation(h) @ (q.to_matrix() @ rest_rot(name)).to_4x4(); upd()

def aim_bone(name, child, to_point):
    pb = P[name]; h = pb.head.copy()
    cur = P[child].head - h; want = to_point - h
    q = cur.normalized().rotation_difference(want.normalized())
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix; upd()

def limb(upper, lower, end, target, hint):
    a = (B[lower].head_local - B[upper].head_local).length
    b = (B[end].head_local - B[lower].head_local).length
    S = P[upper].head.copy(); dv = target - S; d = min(dv.length, a + b - 0.05); u = dv.normalized()
    x = (a*a - b*b + d*d) / (2*d); r = math.sqrt(max(0.0, a*a - x*x))
    n = hint - u * hint.dot(u); n = n.normalized() if n.length > 1e-6 else Vector((0,0,1))
    E = S + u*x + n*r
    aim_bone(upper, lower, E); aim_bone(lower, end, S + u*d)
    return E

def rot_local(name, axis, deg):
    """extra rotation about the bone's own axis (in its current world frame)"""
    pb = P[name]; h = pb.head.copy()
    m3 = pb.matrix.to_3x3()
    ax = (m3 @ Vector(axis)).normalized()
    q = Quaternion(ax, math.radians(deg))
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix; upd()

def yaw(deg): return Quaternion((0,0,1), math.radians(deg))

def clear():
    for pb in P:
        pb.matrix_basis = Matrix.Identity(4)
    upd()

def frame3(a, b):
    a = a.normalized(); c = a.cross(b).normalized(); b = c.cross(a).normalized()
    return Matrix((a, b, c)).transposed()

def orient_hand(side, fingers, thumbward, wrist=None):
    """turn the hand so the middle finger points along `fingers` and the pinky->thumb line along `thumbward`"""
    name = "Arm_Hand." + side
    f_rest = (B["Arm_Finger31." + side].head_local - B[name].head_local)
    t_rest = (B["Arm_Finger11." + side].head_local - B["Arm_Finger51." + side].head_local)
    q = frame3(fingers, thumbward) @ frame3(f_rest, t_rest).inverted()
    pb = P[name]; h = wrist if wrist is not None else pb.head.copy()
    pb.matrix = Matrix.Translation(h) @ (q @ rest_rot(name)).to_4x4(); upd()

def aim_tail(name, to_point):
    pb = P[name]; h = pb.head.copy()
    q = (pb.tail - h).normalized().rotation_difference((to_point - h).normalized())
    pb.matrix = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix; upd()

def finger_to(side, finger, target, bend=0.0, bend_axis=None):
    """lay a finger out straight (or with a little bend) from its base towards target"""
    n = "Arm_Finger%d%%d.%s" % (finger, side)
    base = P[n % 1].head.copy()
    u = (target - base).normalized()
    l1 = (B[n % 2].head_local - B[n % 1].head_local).length
    l2 = (B[n % 3].head_local - B[n % 2].head_local).length
    off = (bend_axis or Vector((0, 0, 1))) * bend
    aim_bone(n % 1, n % 2, base + u * l1 + off * 0.5)
    aim_bone(n % 2, n % 3, base + u * (l1 + l2) + off)
    aim_tail(n % 3, target)

def wrap_finger(side, finger, cz, angles):
    """lay a finger round the cue (axis along Y at x = LINE_X, height cz): each joint aimed at the point at the
    next angle (degrees from +X towards +Z) on a circle of GRIP_R"""
    n = "Arm_Finger%d%%d.%s" % (finger, side)
    def at(a, y):
        a = math.radians(a); return Vector((LINE_X + GRIP_R * math.cos(a), y, cz + GRIP_R * math.sin(a)))
    y = P[n % 1].head.y
    aim_bone(n % 1, n % 2, at(angles[1], y))
    aim_bone(n % 2, n % 3, at(angles[2], y))
    aim_tail(n % 3, at(angles[3], y))

def curl(side, finger, degs):
    """finger 1 = thumb .. 5 = pinky; degs per joint, positive straightens (local Z), negative curls"""
    for j, dgr in zip((1, 2, 3), degs):
        if dgr:
            rot_local("Arm_Finger%d%d.%s" % (finger, j, side), (0, 0, 1), dgr)

# ---------------------------------------------------------------- the cue
def cue_points(stroke):
    """stroke: how far the tip is back from its rest gap (positive = drawn back, negative = through the ball)"""
    d = Vector((0, math.cos(ELEV), -math.sin(ELEV)))        # butt -> tip
    tip = BALL - Vector((0, RADIUS + GAP + stroke, 0))
    tip.z = BALL.z
    butt = tip - d * CUE_LEN
    grip = butt + d * GRIP_FROM_BUTT
    return tip, butt, grip, d

# ---------------------------------------------------------------- the pose
# Everything relative to the cue ball R (its distance ahead of the root) and the cue line x = LINE_X.
def pose(stroke=0.0, sway=0.0, breath=0.0, head_nod=0.0, rise=0.0, settle=0.0):
    """stroke: tip drawn back (+) / through (-); sway: body moving with the stroke (-1..1); breath: 0..1;
    head_nod: units the head moves along the line; rise: 0 down on the shot .. 1 standing back up; settle: 1 = still
    bending down into the shot (0 = down)"""
    clear()
    R = BALL.y
    tip, butt, grip, d = cue_points(stroke)
    up = rise + settle

    # hips: turned 40 degrees towards the cue line, left of it, just clear of the table
    hips = Vector((LINE_X - 12.0 + sway * 0.4, R - 76.0 - sway * 1.2 - up * 4.0, 64.0 + up * 5.0))
    turn("COM", yaw(-40 + sway * 2.0), head=hips)
    rot_local("Pelvis", (1, 0, 0), 10 - up * 8)

    # shoulders: the left one forward over the bridge arm, the right one back over the cue
    lift = breath * 0.5 + up * 16.0
    sh_l = Vector((LINE_X - 13.0, R - 44.0 - up * 10.0 - sway * 0.6, 95.0 + lift))
    sh_r = Vector((LINE_X + 3.5, R - 58.0 - up * 8.0 - sway * 0.8, 96.5 + lift))
    neck_base = (sh_l + sh_r) * 0.5 + Vector((0, 2.0, 2.0))
    aim_bone("Spine1", "Spine2", P["Spine1"].head + (neck_base - P["Spine1"].head) * 0.30 + Vector((0, 0, 3)))
    aim_bone("Spine2", "Chest", P["Spine2"].head + (neck_base - P["Spine2"].head) * 0.45)
    orient("Chest", neck_base - P["Chest"].head, (sh_r - sh_l))
    # head: chin over the cue, eyes down the line
    head_at = Vector((LINE_X - 2.0, R - 40.0 + head_nod - up * 10.0, 92.0 + breath * 0.3 + up * 18.0))
    aim_bone("Neck", "Head", head_at)
    look = (BALL - P["Head"].head).normalized()
    orient("Head", Vector((0, 0.35, 1)) - look * 0.55 * (1.0 - up), Vector((1, 0, 0)))
    aim_bone("Arm_Collarbone.L", "Arm_UpperArm.L", sh_l)
    aim_bone("Arm_Collarbone.R", "Arm_UpperArm.R", sh_r)

    # right arm: the hand on the outside of the cue, forearm hanging, elbow up and back; the fingers wrap
    # round from the outside (palm against the outer side, fingertips under and up its inner side)
    wrist_off = Vector((4.6, 0.0, 6.3))
    for _ in range(2):
        limb("Arm_UpperArm.R", "Arm_ForeArm1.R", "Arm_Hand.R", grip + wrist_off, Vector((0.6, -0.2, 1)))
        orient_hand("R", Vector((-0.45, 0.12, -1)), Vector((0, 1, 0.1)))
        mb = P["Arm_Finger31.R"].head
        want = Vector((LINE_X + GRIP_R * math.cos(math.radians(40)), mb.y, grip.z + GRIP_R * math.sin(math.radians(40))))
        wrist_off += Vector((want.x - mb.x, 0.0, want.z - mb.z))
    for f in (2, 3, 4, 5):
        wrap_finger("R", f, grip.z, (40, -45, -130, -165))
    tb = P["Arm_Finger11.R"].head
    finger_to("R", 1, Vector((LINE_X - 0.4, tb.y + 4.5, grip.z + 1.6)))

    # left arm: bridge hand flat on the felt, the cue in the V of thumb and index
    bridge_y = R - BRIDGE_BACK - up * 30.0
    wrist = Vector((LINE_X - 1.5, bridge_y - 7.0, FELT_Z + 3.2 + up * 6.0))
    limb("Arm_UpperArm.L", "Arm_ForeArm1.L", "Arm_Hand.L", wrist, Vector((-1, 0, -0.4)))
    orient_hand("L", Vector((-0.1, 1, -0.22)), Vector((1, 0, 0.2)))
    for f, ang in ((2, 8), (3, -4), (4, -16), (5, -28)):
        base = P["Arm_Finger%d1.L" % f].head
        a = math.radians(ang)
        tipp = base + Vector((math.sin(a) * 9.0, math.cos(a) * 9.0, 0)); tipp.z = FELT_Z + 0.35
        finger_to("L", f, tipp, bend=0.0)
    tb = P["Arm_Finger11.L"].head
    finger_to("L", 1, Vector((LINE_X + 0.8, tb.y + 6.0, tb.z + 4.0)))

    # legs: left foot forward under the rail, right foot back and turned out; knees soft
    foot_l = Vector((LINE_X - 24.0, R - 78.0, 6.1)); foot_r = Vector((LINE_X - 2.0, R - 100.0, 6.1))
    limb("Leg_Thigh.L", "Leg_Calf.L", "Leg_Foot.L", foot_l, Vector((-0.2, 1, 0)))
    limb("Leg_Thigh.R", "Leg_Calf.R", "Leg_Foot.R", foot_r, Vector((0.5, 1, 0)))
    fl = math.radians(10); fr = math.radians(-45)
    orient("Leg_Foot.L", Vector((0, 0, 1)), Vector((math.cos(fl), math.sin(fl), 0)))
    orient("Leg_Foot.R", Vector((0, 0, 1)), Vector((math.cos(fr), math.sin(fr), 0)))
    return tip, d

def place_cue(tip, d):
    """the CueStick mesh (thin end at local z -47, 94 long) with its tip on `tip`, pointing along d"""
    st = bpy.data.objects["CueStick"]
    if st.parent:
        mw = st.matrix_world.copy(); st.parent = None; st.matrix_world = mw
    z = (-d).normalized(); x = Vector((1, 0, 0)); x = (x - z * x.dot(z)).normalized(); y = z.cross(x)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = tip - d.normalized() * 47.0
    st.matrix_world = m


def look_from(eye, target):
    eye = Vector(eye); target = Vector(target)
    for area in bpy.context.window.screen.areas:
        if area.type == 'VIEW_3D':
            r3 = area.spaces.active.region_3d
            r3.view_perspective = 'PERSP'
            r3.view_location = target
            r3.view_distance = (eye - target).length
            r3.view_rotation = (eye - target).to_track_quat('Z', 'Y')
            area.tag_redraw()


def ease(x):
    x = max(0.0, min(1.0, x)); return x * x * (3 - 2 * x)

def seg(f, keys):
    """piecewise eased curve through (frame, value) keys"""
    if f <= keys[0][0]: return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if f <= f1:
            return v0 + (v1 - v0) * ease((f - f0) / (f1 - f0))
    return keys[-1][1]

def aim_params(f, n=120):
    # three practice strokes, each a little different, ending where it started so it loops
    stroke = seg(f, [(1, 0.0), (8, 0.0), (24, 6.5), (36, -0.6), (42, -0.6), (58, 7.5), (70, -0.4), (76, -0.4),
                     (92, 6.0), (104, 0.0), (120, 0.0)])
    breath = 0.5 - 0.5 * math.cos(2 * math.pi * (f - 1) / (n - 1))
    sway = -stroke / 7.5 * 0.5
    nod = 0.6 * math.sin(2 * math.pi * (f - 1) / (n - 1) * 2)
    return dict(stroke=stroke, sway=sway, breath=breath, head_nod=nod)

def strike_params(f, n=60):
    # settle, slow backswing, pause, accelerating stroke (contact on frame 35), follow through, begin to rise
    if f <= 28:
        stroke = seg(f, [(1, 0.0), (6, 0.0), (24, 12.0), (28, 12.0)])
    elif f <= 35:
        x = (f - 28) / 7.0
        stroke = 12.0 + (-1.5 - 12.0) * (x ** 2.2)     # speeding up all the way to the ball
    else:
        stroke = seg(f, [(35, -1.5), (41, -9.0), (60, -9.0)])
    sway = -max(-1.0, min(1.0, stroke / 12.0)) * 0.6
    rise = seg(f, [(1, 0.0), (46, 0.0), (60, 0.55)])
    breath = seg(f, [(1, 0.5), (24, 1.0), (35, 0.2), (60, 0.6)])
    nod = seg(f, [(1, 0.0), (35, 0.0), (44, 2.0), (60, 2.0)])
    return dict(stroke=stroke, sway=sway, breath=breath, head_nod=nod, rise=rise)

def bake_clip(name, n, params):
    arm.animation_data_create()
    act = bpy.data.actions.get(name)
    if act: bpy.data.actions.remove(act)
    act = bpy.data.actions.new(name); act.use_fake_user = True
    arm.animation_data.action = act
    st = bpy.data.objects["CueStick"]; st.animation_data_create()
    cact = bpy.data.actions.get("Cue_" + name)
    if cact: bpy.data.actions.remove(cact)
    cact = bpy.data.actions.new("Cue_" + name); cact.use_fake_user = True
    st.animation_data.action = cact
    for pb in P:
        pb.rotation_mode = 'QUATERNION'
    for f in range(1, n + 1):
        tip, d = pose(**params(f))
        place_cue(tip, d)
        for pb in P:
            pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
            pb.keyframe_insert("location", frame=f, group=pb.name)
        st.keyframe_insert("location", frame=f); st.keyframe_insert("rotation_euler", frame=f)
    return act
