# fo4pool: project handoff

Paste this into a new chat to carry on where we left off.

## What it is

**fo4pool** is an F4SE plugin for Fallout 4 that makes vanilla pool tables playable. It spawns real balls, runs its
own physics, gives you a shot camera with a guide and a cue stick, and plays full 8-ball (two players, hotseat) or
Solo Play. It also has sounds, a terminal-style HUD and every setting in the F4SE Menu Framework menu. Next big goal:
an NPC opponent with custom pool animations.

## Where things live

- **Repo:** `evodube/fo4pool` (GitHub). `main` is built by CI. Work in progress lives on other branches.
  - `main` = **0.5.0** (last build delivered).
  - `wip/quiet-notifications` = 3 commits on top of main, **not built yet**:
    1. Calls only go to the HUD log; corner notifications are opt-in (`[HUD] bNotifications`).
    2. The player's view is held while aiming and restored on exit (fixes "looking at the sky" after aiming).
    3. Animation work in `tools/anim` (Blender scene, PoolAim.hkx, PoolStrike.hkx, README).
- **Build:** GitHub Actions. Linux job runs the physics and rules tests; Windows job builds with xmake +
  Dear-Modding CommonLibF4 (submodule pinned at ef995295), targeting Fallout 4 OG 1.10.163. Each push to `main`
  publishes the built `Data` folder to the `builds` branch (fetch with git; artifacts can't be downloaded from the
  cloud session). **Pushing to main = building.**
- **The user's PC** (Windows, "desktop-3h92q0m", connected through the desktop app):
  - `C:\Users\Dan\Desktop\pool fallout 4\` is the working folder: test zips, `Sounds\`, `anim_tests\` (Blender scene,
    exported clips, previews).
  - `C:\Users\Dan\Desktop\fallout 4 idles and animations\` holds the vanilla `skeleton.hkx` / `skeleton.nif` and a set of
    vanilla idles (2HM, workbench, chemistry, newspaper, wall lean).
  - `C:\Users\Dan\Documents\My Games\Fallout4\F4SE\` holds `fo4pool.log`.
  - Blender 5.2.3 is on the PC, reachable through its MCP connection, with **PyNifly 30.2** installed (native FO4 HKX
    import and export). Run its operators inside a `temp_override` of the 3D viewport, or they return without doing
    anything.
- **Delivering a build:** clone the `builds` branch, copy its `Data` over the assets folder (meshes and ESL live only
  in the cloud session's `/mnt/user-data/outputs/fo4pool_assets/Data`; if that's gone, the meshes and ESL are made
  by `tools/strip_collision.py` and `tools/make_esl.py` from the user's vanilla meshes), zip as
  `fo4pool-<version>-test.zip`, send it, and write it to the pool folder if it's connected.

## How the user likes to work

- "**dont build**" = make the changes but don't push to `main`. Commit to a branch instead.
- "**build**" = push to main, wait for CI, deliver the **full mod zip** (not just the DLL).
- Don't say a build hasn't been tested in game; they know.
- They've rebound keys to **F7 (rack)** and **F8 (aim)**.
- They like being shown previews and design options before big visual work.
- Don't use other games' or modders' assets without permission.

## Controls (defaults)

Numpad 7 rack (again at the same table, even from the shot camera = re-rack in place), Numpad 8 aim/stop,
Numpad 9 clear, Numpad 4 calibration markers, Numpad 5 change the called pocket. While aiming: hold left mouse and
pull back for power, release to shoot; right mouse toggles overhead; ball in hand = the mouse moves the cue ball,
left click sets it.

## Features and how they work

- **Physics** (`src/sim/Sim`, plain C++, tested in `tests/sim_test.cpp`):
  - fixed substeps, ball-ball collisions rewound to the time of impact, cushions as segments;
  - pockets with angled jaws and a capture depth (`fPocketDepth` 0.7, `fJawLength` 1.3, corner jaw angle 15°, side
    mouth 17), so balls can rattle out;
  - `fBallRestitution` 0.75, `fMaxSpeed` 365 (full power = what 50% used to be).
- **Rules** (`src/sim/Rules`, tested in `tests/rules_test.cpp`):
  - 8-Ball: the break, open table, groups, legal shot, fouls → ball in hand, the 8 (early / foul / wrong pocket
    loses), the 8 on the break is respotted.
  - **Solo Play** (`iGameMode=1`): all 14, then the 8 into the called pocket. Lost if the 8 goes in on the break,
    before the 14, or in the wrong pocket, or if you scratch while on the 8. Other scratches give ball in hand.
    Misses are fine. No scoring.
  - The called pocket is auto-picked from the guide; Numpad 5 cycles it. Marked on the table with a big dot.
- **Shot camera** (`src/Aim.cpp`, `src/CameraHook.cpp`):
  - uses the game's free camera; hooks FreeCameraState Update / GetTranslation / OnMouseMove and **LookHandler**
    OnMouseMove (vtable index 6);
  - while charging power, mouse input is eaten at the LookHandler and the look spring is zeroed, so the view stays
    perfectly still;
  - power is learned per degree of view pull;
  - the stick and guide are placed in the camera update.
- **HUD** (`src/Hud.cpp`, the green terminal look):
  - pre-rendered PNG art in `Data/F4SE/Plugins/fo4pool/hud` (`tools/hud/panels.html` + `render_hud.py`, Playwright
    at 3x, Chakra Petch and Share Tech Mono under the OFL, with baked glow atlases; positions in `src/HudAtlas.h`),
    loaded with Menu Framework `LoadTexture`;
  - laid out on a 1280x720 design screen; scale = screen height / 720 × `[HUD] fScale`;
  - shows the scoreboard, a terminal log, the YOUR SHOT / THEIR SHOT tag (SOLO PLAY in solo), the 16-segment power
    meter, a foul banner that blinks 3 times, ball in hand, the called-pocket map, and the win/lose banner;
  - only drawn while aiming.
- **Sounds** (`src/Audio.cpp`, its own XAudio2 engine):
  - CC0 "Pool and Billiards" pack, mono WAVs in `Data/Sound/fo4pool`: cue, hit 1-6, cushion 1-3, pocket, return
    (roll length depends on the pocket's distance from the foot end), turn (`player_turn`), start (`player_found`);
  - positional by camera, loudness from impact speed.
- **Settings menu** (`src/SettingsMenu.cpp`): every INI key, table-driven, on fo4pool pages in F4SE Menu Framework.
  Writes the INI as you change things; key rebinding by pressing the key.
- **Ball size:** auto-scale is measured once per rack from a real ball after it's placed (bug fix in 0.5.0). Still
  to check: the log line `ball mesh radius ... -> scale ...`.

## Useful technical facts

- Game rotation convention: `NiMatrix3` rows are the object's axes. A world point = `T + s * (R^T * p)`.
- Vanilla `PoolTable01.nif`: felt at z 70.4, playing area 224 × 112 (length along mesh Y), ball radius 3.51. The cue
  mesh is `PoolCue.nif`, tip at +83 on Y and butt at -11.
- `fo4pool.esl`: cue ball 0x800, balls 0x801-0x80F, cue stick 0x810.
- FO4 human rig: about 125 units tall, facing +Y. **The spine hangs off COM, not Pelvis.** The forearm has 3 twist
  segments (pose ForeArm1 and keep 2 and 3 rigid). The rest hand is already half-curled. Finger 1 = thumb,
  5 = pinky; each finger has 3 bones, and the last bone's head is the second knuckle.

## Animations (in progress)

- **Pipeline:** Blender + PyNifly 30.2.
  1. Import `skeleton.hkx` with `rotate_bones_pretty`.
  2. Animate with the two-bone IK script in the `pool_ik` text block (no constraints to bake).
  3. Export with `export_scene.pynifly_hkx`, game FO4, 30 fps, `reference_skel` = `skeleton.hkx`.
  4. Re-import to verify.
- **Clips so far** (right-handed, relaxed bar style):
  - **PoolAim**: 90 frames, loops, two practice strokes and a breath.
  - **PoolStrike**: 60 frames; **the cue reaches the ball on frame 35**.
- **Shot geometry**, in the actor's space: cue line at x = 8.5 along +Y, about 2° rising to the butt; cue ball centre
  at (8.5, 66.1, 73.9). So the actor's root stands 66.1 behind the ball and 8.5 to the left of the line. Details in
  `tools/anim/README.md`.
- **In-game test:** `fo4pool-anim-test.zip` swaps the chemistry station's `PoseA_Idle1` (aim) and `PoseA_IdleFlavor1`
  (strike). It's temporary; the user should remove it after testing.
- **Waiting on:** the user's verdict on the preview video and the in-game test.

## Next steps

1. Get feedback on the animations; adjust lean, timing and stroke length.
2. Build the `wip/quiet-notifications` branch into the next release when asked (merge to main = build).
3. More clips: stand up / walk round the table / chalk.
4. **NPC opponent:**
   - talk to a settler or companion → "Play pool" → they walk to the table;
   - their shots are chosen with `Sim::Predict` and a skill-based aim error;
   - they play PoolAim / PoolStrike, with the cue stick placed every frame from the actor's two hands;
   - the ball is struck on frame 35.
5. Smaller ideas: a stick-forward strike for the player, a shot follow-cam, a rack triangle, spin (english).

## Known open questions

- Check the ball-size line in `fo4pool.log`.
- Making NPCs play custom idles needs either a behaviour/idle form or piggybacking a vanilla furniture idle; not
  decided yet.
- Animations from other games (e.g. a Sims billiards mod) are off-limits without the author's permission.
