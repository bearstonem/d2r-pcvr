# D2R PCVR controls

Windows preview for **SteamVR and Virtual Desktop (VDXR)**. These controls
use two tracked VR controllers; an Xbox controller is not required.

**VR Settings > What's new** shows one line per feature from the last
published release. It is available to everyone and works offline.

In [developer mode](steamvr/DEVELOPMENT_SCENARIOS.md), the hand-wheel **DEV** tab
provides Character, Equipment and act-grouped travel tools. These test tools are
hidden in ordinary play; launch with `--frame-vr --frame-dev` for isolated offline tests.

## Button names

| Hand / button | Quest / Touch | Valve Index | Steam Frame |
| --- | --- | --- | --- |
| Right primary / secondary | A / B | A / B | A / B |
| Left primary / secondary | X / Y | A / B | D-pad Down / Up |
| Trigger | Index trigger | Index trigger | Index trigger |
| Grip | Grip button | Squeeze | Grip input |
| Stick click | Press the thumbstick | Press the thumbstick | Press the thumbstick |

**Left primary** below means **Quest X / Index A / Frame D-pad Down**.
**Left secondary** means **Quest Y / Index B / Frame D-pad Up**.

Wheel button badges, grip combinations, skill-picker headings and the Controls
help use the active controller layout. Open either wheel > **VR SETTINGS** >
**VR**, then use the **Headset / controllers** dropdown to choose **Automatic**
(default), **Quest / Touch**, **Valve Index**, **Steam Frame**, or **Generic**.
Select **Apply** to save; closing without Apply discards the choice.

Automatic reads each hand's active OpenXR interaction profile and refreshes
when controllers change. This also supports mixed controller layouts. Unknown
profiles display Primary / Secondary rather than guessed button letters. The
legacy WMR profile displays Unbound for face slots, which it does not bind.
The override changes labels only; it preserves skill assignments and controls.
Custom SteamVR remappings may need the Generic labels.

Steam Frame uses its native profile when the runtime advertises it. With an
older runtime's Touch compatibility binding, Automatic can report Touch;
select Steam Frame manually to show its D-pad labels. The compatibility binding
also aliases the upper face/D-pad buttons to the secondary action; the wheel
shows B and D-pad Up as the representative controls. See
[Valve's Frame input documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
and the [OpenXR controller profiles](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#interaction-profiles).

The saved override is `[interactions] controller_labels`: 0 Automatic, 1 Quest /
Touch, 2 Index, 3 Frame, 4 Generic. Missing or invalid values use Automatic.

## Left-handed play (experimental)

**VR Settings > Controls > Hands > Left-handed (experimental)** (Off by default; `[interactions] left_handed`)
mirrors everything: you play as a right-handed player would, with the other hand. It is the one switch for
your hands: the hand that swaps weapon sets, points at loot and has the first monster outline is your main
hand, the left one with Left-handed on. (The separate Dominant hand setting is gone.)

**How to read this guide left-handed.** Every page is written for a right-handed player. With Left-handed On,
read every "right" as your **left** (hand, arm, fist, shoulder, wrist, controller, stick, trigger, grip) and
every "left" as your right. "Right A" and "Right B" are then your left controller's two buttons (X and Y on
Touch controllers) and "Left X" and "Left Y" your right controller's (A and B). In the headset nothing needs
translating: VR Settings, the Controller guide, the wheels, the MOTIONS page and the prompts name the hands
and buttons as you use them.

| What | Left-handed |
| --- | --- |
| Main weapon | In your left hand; the left trigger attacks and holds the main trigger skill |
| Shield or second weapon | On your right arm: guard, parry, Smite and the second weapon's swings and throws |
| Face buttons | The left controller's two buttons act as A and B, the right controller's as X and Y |
| Grips | Each hand's grip layer follows the hand |
| Sticks | Right stick walks, left stick turns (its up / down still zooms the map and shows loot names) |
| Saved skills and glyphs | Follow the hand's role: what was on the right hand is on the left |
| Vibration | Goes to the hand that acted |
| Menu button | Where the controller has it |
| Weapon swap, monster outline's first hand, loot pick's pointing hand | The left hand |
| Menu laser | Your left controller points at game menus and VR Settings; its trigger clicks, its grip grabs and moves a panel |
| Inventory on the hide, quest log | Held in your right hand, by the menu's right edge; reach your right hand over your right shoulder and grip to take it out or put it away |
| Wrist HUD, vitals, map and compass | On your right wrist and right palm; the low life and mana buzz is on the right controller |
| Floating map | Pulled off the right wrist with the left controller's grip; the left stick pushes, pulls and zooms it; the right grip resizes it |
| Class motions | Each motion is its mirror image: one described with the right fist, arm or shoulder is done with the left, toward the other side, and a two-hand motion with the hands exchanged. The MOTIONS page shows them that way round |
| Words and button names | VR Settings rows and help, the Controller guide's two columns, the wheels and the on-screen prompts name the hand that does each thing now |

| Bow | Held in your right hand; your left hand nocks, draws and releases the string |
| Crossbow, staff, spear, polearm, two-handed sword, axe or maul | Led by your left hand, the right hand supporting it |

Every weapon kind is mirrored, so equipping or putting away a weapon never moves a stick, a button or a
trigger. The log names each kind as it is equipped (`Frame left-handed: ...`); should a kind ever be switched
back to right-handed play, the whole layout would go right-handed with it while it is held, and the log and
the in-headset words would say so.

Not mirrored:

- Third person and other players see a right-handed hero.
- Two things always name your real hands: the **Left-handed** row itself, and the detected controllers.

A weapon or shield is turned around in the other hand rather than mirrored, so a one-sided blade or a painted
shield can look different. The shield's wrist bend stop is not on the right arm yet.

## Gameplay

With **Motion combat** enabled, each hand has its own saved five-button layout.
Quest left X/Y correspond to Index left A/B; right A/B labels are the same.

| Action | VR control |
| --- | --- |
| Walk / turn | Left stick / right stick sideways, including with a wheel open; snap by default, with optional smooth turning |
| Open or close a hand wheel | Click that hand's thumbstick; it stays open without holding, and reopens on that hand's last page (Map or Equipment; **SPELLS** goes back), and its skill editor on the last tree you picked, until a new game |
| Switch to the other wheel | Click the other thumbstick |
| Assign a button | Opposite hand's laser + trigger: select a wedge, then a skill |
| Cast a buff by drawing a glyph | Hold your **Draw glyph** button, draw a shape in the air and let go: the skill that hand has on that shape is cast. See [Glyph casting](#glyph-casting) |
| Use an inventory item | Either wheel > **QUICK USE**: point with the opposite hand and briefly click its trigger; use happens on release |
| Assign / change quick-use item | Hold the trigger over **QUICK USE** for 0.65 seconds, then select a carried scroll, tome or other usable item. An empty slot also opens the picker on a short click. **CLEAR SLOT** removes the assignment |
| Walk / run speed | VR Settings > **Comfort > Moving** > **Movement speed**, then Apply: **Walk**; **Base run** (your class's run with no speed bonus); **Run 10%…90%** of the way up; **Full run** (the default, the full speed your gear and skills give). Saved to `[gameplay] run_speed_pct`; the game's own run / walk toggle is kept in line with it. Movement skills keep their own speed, and it never runs faster than the game. While fighting you walk: being hit, landing a strike, or a spell, arrow or throw leaving your hand switches to walking until half a second after the last one (`[gameplay] combat_walk_ms`, 0 = off). With **Stick toggle** (**Controls > Hands**) on (Off by default; `[gameplay] run_walk_toggle`), push the **right stick up** to switch between walking and that run (from Walk, the full run) |
| Finish editing | Click the wheel hand's stick again, or select CLOSE in its center |
| Inventory / character / skills / journal | Reach the **left hand over the left shoulder** and grip; or Left Grip + left stick click; or either wheel > INVENTORY. However it opens, the tabbed menu comes up on a leather sheet strapped to your left hand, with the world still around you; the right laser works on it as before, the left stick still walks and the right stick turns, an item picked up and clicked outside the menu drops to the ground, life / mana show above it, and closing the menu (Right B, its close button, or reaching back over the left shoulder and gripping) puts the leather away. Native tabs retain their controls. **VR Settings > Interface > Menus > Hold menu** sets it: **Leather**, **Menu only** (held by its left edge, no hide) or **Off** (in front of you instead, without the shoulder reach). |
| VR settings / body calibration | Either wheel → VR SETTINGS |
| Recalibrate body and eye height, no menu | Stand in a **T cross**, nothing pressed and no potion in hand, for 2 seconds: both controllers buzz lightly while you hold it (the buzz stopping means you left the pose and the 2 seconds start over), then the prompt opens; hold both triggers to save, Right B cancels. It applies as soon as it says Saved. Not in Seated play, with a menu open or a monster close (in a game you joined, monsters do not block it). With vibration off the prompt opening is the only cue. Turn it off: VR Settings > Comfort > Body > **T cross to calibrate** |
| Explain a VR setting | Point the right-hand laser at its label or controls for about half a second. Help appears beside the panel and disappears when you point away; no click needed |
| Minimap | Either wheel → MAP; movement remains available |
| Zoom the minimap | Look at the wrist / palm map or the wheel's MAP tab and push the right stick up to zoom in, down to zoom out; down until it stops shows the whole map. While you look at it, right stick up / down does nothing else (no run / walk toggle, no loot labels); left / right still turns, except during a zoom push |
| Floating map | Point the right controller at the raised wrist map (or roughly at it) and squeeze **Right Grip**: a copy of the map comes off the wrist in your hand. Move and turn it any way, push the right stick forward / back to send it out (up to 3 m) or pull it in, then let go: it stays there, facing you. While you hold it, the right stick does not turn you. Grab it again any time by pointing at it. Squeeze both grips (either order) while pointing at it and spread or close your hands to make it bigger or smaller. Let go of it next to the raised wrist map to put it away. It shows while you look toward it; Save and Exit or a new game puts it back on the wrist |
| Zoom a floating map | Point the right controller anywhere on the floating map and push the right stick up to zoom in, down to zoom out; the area grows and shrinks with the map. The wrist map zooms as before, by looking at it; both show the same picture, so they zoom together |
| Reset height | VR Settings > Comfort > Body > Height and arm span > **Set eye height** |
| Adjust controller vibration | **VR Settings > Controls > Vibration**: overall vibration plus separate melee, bow draw, bow/throw release, spell, loot/potion/menu and low life/mana alert strengths. **Feel it** on a row's help card tests it on that action's hands; **Apply** saves |
| Hold to show all nearby loot labels (Alt) | Hold the **right stick down** during gameplay, outside wheels, while not looking at the wrist map, and while not pointing at a floating map. Labels stay shown while it is held; let go to restore normal drop/hover labels. Uses the native Alt range, independently of where you aim. A mostly vertical push never turns you. Also listed in **VR Settings > Guide** |
| Interact / pick up / talk | Right A by default; requires an aimed interactable. One press acts on one target: press again for each item, including loot a chest just dropped. With **VR Settings > Interface > Labels > Loot pick** on, point at an item (arm out) to choose which one |
| Free melee swing / thrust | Move the equipped weapon through an enemy |
| Physical dual wield | Equip two supported melee weapons on a Barbarian or two claws on an Assassin; release both triggers and swing or thrust with either hand. Active skills retain their button bindings |
| Break pots / barrels | Swing a held melee weapon through them with its trigger released. Either Barbarian weapon or Assassin claw works; ordinary button interaction/kicking remains available |
| Choose motion thresholds | **VR Settings > Gameplay > Combat**: Swing speed to hit and Stab speed to hit |
| Bone Spear | Assign either hand's trigger or shortcut; aim with that controller and hold to repeat while moving. **Gameplay > Targeting > Spell aim assist** applies to Bone Spear too; Strong adds gentle homing |
| Bone Wall / Bone Prison | Aim with the casting controller: the wall or prison lands where its ray meets the floor (six cells ahead when level). With **VR Settings > Gameplay > Targeting > Ground skill aim** on Monster, Bone Prison lands on the aimed monster and Bone Wall rises 3 cells in front of it. Native placement rules still apply |
| Holy Bolt | Aim with either casting controller, including up/down with Gameplay > Targeting > Keep spell shots level Off. Light assists at launch; Strong also adds gentle homing toward undead/demons. Directly aim at your mercenary to heal; native damage/healing rules apply |
| See which monsters you are aiming at | **VR Settings > Interface > Outlines > Monster outline** (Off by default; 10-100% sets its brightness) draws a thin, steady outline round each hand's monster: a hand held out picks the monster it points at; with no hand on one, the monster you look at is outlined. **Outlines shown**: Both hands (default) outlines each hand's monster, Main hand only that hand's (the right; the left with **Controls > Hands > Left-handed** on; the bow hand while a bow is out, whose color also wins when both hands are on one monster). **Left hand outline color** and **Right hand outline color** each choose By range (red near, orange mid-range, yellow far) or one hue (press the color to see all 24 and By range at once; one press picks; < and > still step through them); gold and blue by default. The monster you look at takes the color of the hand that would cast at it: the left (bow) hand with a bow equipped, else your dominant hand. A button you hold keeps showing the monster its cast took. Walls in between rule a monster out. A guide only: it does not change how skills aim |
| Send skills to that monster | **VR Settings > Gameplay > Targeting > Skills go to the aimed monster** (Off by default; Spells, Bow skills or Both. A class without bow skills sees it as **Spells go to the aimed monster**, On or Off): a spell takes the monster of the hand whose button casts it (arm held out in front of you, along where it points, at least a tenth of your Controller span: 15 cm on a 1.5 m span; a hand pointing within 45 degrees of straight down, or hanging at your side, is resting), else the other hand's, else the one you look at, and keeps it until you let go or it walks out of the game's reach. Bolts and Lightning fly at it; curses and Corpse Explosion land on it; Guided Arrow, Bone Spirit, Charge, Leap Attack, Dragon Flight, Attract and Taunt are handed the monster itself; Teleport, Leap, golems and raises go where you point. With no monster, a spell goes where its hand's arm points, else the other arm, else where you look. Only monsters within the game's own cast reach count (50 cells on either axis, less a margin). Off, spells aim along the casting hand as before. With Bow skills or Both, bow skills take the bow hand's monster first, aimed from the bow hand. The Warlock's skills are left out for now: they keep the hand aim they had before these settings |
| Keep the aim on what you face | **VR Settings > Gameplay > Targeting > Aim with your view** (Off by default; shown once Monster outline, Skills go to the aimed monster or Ground skill aim on Monster is on): the monster you look at must come within 17 degrees of where your head faces, and stays yours while it is within 30, until another comes within 17 (with **Aim with your eyes** on a headset with eye tracking, where your eyes look picks it instead, below); a hand picks only monsters within 30 degrees of where your head faces. A casting hand (the bow hand for a bow skill) held out and pointing aims alone: its cast takes its own monster, else goes where it points, never the other hand's or the one you look at, and while the hand whose color the one you look at takes points, that monster is not outlined. A lowered hand still takes the other hand's, then the one you look at. The melee attack skills (Charge, Leap Attack, Dragon Flight, Whirlwind) from a raised hand take its own monster, else the other hand's while that one is outlined (Outlines shown on Both hands, or the other hand is the dominant one), never the one you look at; lowered, they try all three like any cast |
| Aim with your eyes | **VR Settings > Gameplay > Targeting > Aim with your eyes** (**Eyes**, beside Aim with your view; On by default; shown only on a headset that shares eye tracking, such as a Steam Frame through SteamVR, once Monster outline, Skills go to the aimed monster or Ground skill aim on Monster is on): "the one you look at" is the monster your eyes are on, not the one your head faces. It is outlined while no hand points, and skills take it after your hands' monsters; with no monster and no hand held out, a cast goes where your eyes look. A monster within about 6 degrees of where you look is taken (look at its middle when one stands behind another), and it stays yours while your eyes are within about 10 degrees of it, until you look at another for a tenth of a second; a quick glance across others never takes them. A blink keeps it; with tracking lost for a third of a second, your head's view picks again (17 / 30 degrees with Aim with your view on) until your eyes are seen again. Your hands pick as before. Off: your head's view, as before. Eye-tracked foveation does not need to be on |
| Cast with a staff | **VR Settings > Gameplay > Targeting > Aim staff along arm** (Off by default): an upright staff casts along your arm, shoulder through hand; leveled out like a wand, it casts from its top. The Warlock's skills keep the staff's own aim |
| Aim ground skills | **VR Settings > Gameplay > Targeting > Ground skill aim** sets where Fire Blast, Shock Web and the other traps (Blade Sentinel, Mind Blast...), Blizzard, Meteor, Fire Wall, Hydra, Fissure, Volcano, Bone Wall and Bone Prison land. **Game's aim** (default): the game's own aim, as before. **Pointed**: on the floor where the casting hand points, held out or not (else the other arm, else where you look); aimed at the horizon or above, as far as the game lets a skill be cast (45 cells along an axis, more diagonally). **Monster**: on the monster the casting hand, the other hand or your eyes aim at, else where you point |
| See where a ground skill will hit | **VR Settings > Gameplay > Targeting > Landing circles** (default On; with Ground skill aim on Pointed or Monster): once the game accepts the cast, a circle in the element's color fills from the center outward until the skill lands (a bomb over its flight, timed by the game's own missile, Meteor over its fall, Blizzard while the ice falls); Fire Wall shows as a strip across your aim. It paints the floor only, not monsters' legs or walls |
| Seat a held shield on your arm | **VR Settings > Gameplay > Shield**: Shield along forearm slides the shield up the forearm so your fingers reach its grip strap; Shield out from arm and Shield across forearm move it away from and across the forearm. In centimeters (along -10 to 40, out and across -30 to 30), all 0 by default (the original position when Shield wrist turn is 100%); about 3 / 2 / 2 suits a buckler, large shields need more. The shield blocks where it is shown. Shield wrist turn sets how much it turns with your wrist (0% strapped to the forearm; 100% with the hand; default 50%, 10% steps), and Shield roll turns it about the forearm (default 0, -180 to +180 in 5° steps, + toward the little finger) |
| Increase offline difficulty | **VR Settings > Gameplay > Monsters > Offline difficulty**: Game's setting or Players 1-8; higher counts make monsters tougher and increase rewards |
| Bow draw | Right trigger near the left-hand bow string; draw and release |
| See where a drawn arrow will fly | At full draw a faint line runs from the arrow's tip along its straight flight, with a brighter dot where it would reach the floor; otherwise it fades out at 15 m. It does not stop at walls or monsters. Basic arrows and Magic, Fire, Cold, Ice, Exploding, Immolation and Freezing Arrow; not Multiple Shot, Guided Arrow, Strafe or crossbows. **VR Settings > Gameplay > Targeting > Arrow path line** chooses Off or its color, 15 degrees at a time around the color wheel (Off by default; the first step is red 0°); stepping past 345° goes to Off and on to 0°, in either direction |
| Bash with the bow | With **VR Settings > Gameplay > Combat > Bow bash** On (Off by default), swing the bow in your left hand into a monster, the string not drawn and both triggers released: it staggers, so you can step back and draw. It deals no damage and spends no arrow; your bow hand feels each contact. One bash every 1.5 s, the same monster every 3 s; act bosses never stagger. Any class with a bow; not crossbows yet |
| Fire a crossbow | Equip bolts and pull Right Trigger; aim and fire while moving. With a crossbow in the hands the right trigger is **Attack** whatever skill its slot holds, and the wrist cue says **Attack, Crossbow: pull trigger**. The slot keeps its skill: take the crossbow off and the trigger casts it again. A bow skill there (Multiple Shot, Strafe, Guided Arrow, the elemental arrows) stays, and the game fires it from the crossbow. Other skills cast from the face buttons as usual |
| Support a crossbow | Hold Left Grip near the stock; release to free the hand. After a shoulder swap, release the swap grip before grabbing the stock again |
| Javelin, throwing knife or axe throw | Right trigger on Attack, Throw or nothing; hold the trigger, throw, release |
| Cancel a physical attack / draw | Open either wheel, then release the held controls |
| Swap weapon sets I / II | Main hand's grip over either shoulder (right hand; left with Left-handed on); or wheel → EQUIPMENT |
| Stow / draw weapons | Wheel → EQUIPMENT → Stow / Draw |
| Grab / drink potion | Grip a belt bottle, bring its opening to the mouth and tip it; release to return unused |
| Drop the empty bottle | Open your hand after drinking; it falls, bounces and rolls on the floor (visual only). Swing a weapon or shield into it to knock it away |
| Knock over cups, plates, vases, stools | Swing a weapon, punch or block into the level's small props; they fall and roll where they land until the area reloads (visual only) |
| Heal your mercenary | Toss a held Health, Rejuvenation, Antidote or Thawing bottle so it lands near its feet; with bottle physics it flies as a real body and breaks where it first hits. A miss keeps the potion |
| Command your mercenary (offline) | Point at it (with the controller, or your hand in line with it) with **Grip** held and pull **Trigger**: grip first or both at once, either way; the trigger casts nothing while you do. Aim, and let go: at the ground it walks there and holds; at a monster it focuses it; still on the mercenary it holds its place, or regroups if it was holding. Grip alone does nothing to it. **VR Settings > Gameplay > Monsters > Tactical commands** turns this off |
| Quick potion | Wheel → EQUIPMENT → potion, using opposite laser + trigger |
| Grab wheel potion | Wheel → EQUIPMENT → potion, using the pointing hand's grip |
| Wrist health / mana / XP | Look at the back of the left wrist |
| Wipe blood off the visor | Scrub either hand side to side in front of your face, trigger released. About six strokes clear a full visor. **Interface > Warnings > Wipe blood by hand** turns this off |
| Recenter / stereo capture | PC F11 / F8 |
| Walk where the controller points | **VR Settings > Controls > Hands > Walk direction**: **Look** (default) walks where you look; **Controller** walks where the left controller points |
| Room-scale | **VR Settings > Comfort > Body > Room follows you** (beside Seated play; On by default): as you move in your room your hero's spot follows you, so monsters attack where you stand. Grips, triggers, swings, casting, the left stick and menus pause it |
| Recenter position, keep view direction | PC F13 (native OpenXR, gameplay only) |

F13 resets the tracked position, including height, without resetting view direction.
It skips open menus, calibration, item dragging and held grips. Release both grips
before recentering so a two-handed weapon's combat state is not interrupted.
This is a recenter shortcut,
not room-scale movement of the game actor; the world can still shift in position.

Editing consumes combat inputs but never pauses the game or zeros locomotion.
Selection clicks do not cast. Release buttons and triggers after closing before
using them in combat. Menu, tracking, equipment and assignment changes also
require a fresh press. Only one wheel is open at a time.

Interact latches the target present when you press A. Pressing at empty space
does not wait for an item to come into range. Release and press again for each
new target. A release during an inventory/NPC menu rearms interaction; holding
the button through a menu or game-session change cannot reuse the old target.

**VR Settings > Interface > Labels > Loot pick** (On by default): with several items on
the floor, Interact takes the one you point at, not the nearest. Hold your
main hand (the right; the left with **Controls > Hands > Left-handed** on) out and point at the item: its name
shows, even far away, with a thin pulsing rim. Within the pick-up reach (4 cells)
the rim is in the item's rarity color and the name grows; farther, the rim is red
and A takes nothing, not the nearest item either, and does not walk there. With
no item pointed at, doors, chests, waypoints and NPCs work as before; a pointed
item comes first. `[interactions] loot_pick_reach_cells` in `d2r_pcvr.ini` sets
the reach (0 = no limit, up to 40 cells). Details are in [GAMEPLAY.md](steamvr/GAMEPLAY.md).

On the **VR** settings tab, **Smooth turning** defaults to **Off** (30-degree
snap turns). Turn it **On**, then **Apply**, for continuous right-stick turning.
**Snap turn angle** and **Smooth turn speed** keep their values when switching
modes. Existing smooth-turn profiles retain their selected mode.

On the **Vibration** page, pointing at a row shows a **Feel it** button on its
help card: it plays that action on its hands at the strength shown, before
Apply.
**Short melee taps** defaults Off: weapon impacts last 90 ms and shield impacts
45 ms at the controller's preferred frequency. On selects the earlier 10 ms /
180 Hz pulse. **Interface > Warnings > Low life and mana alert** must also be enabled for
automatic resource warnings.

Melee is **Unleashed**: physical strokes can hit each creature they reach
once, without hit rolls or an attack-speed cooldown, and need deliberate
motion. The Classic rules are gone; `[gameplay] melee_rules` is ignored.
**Gameplay > Combat > Stab speed to hit** is separate from swing speed: 2 m/s by default, adjustable
from 0.75 to 10 m/s in 0.25 m/s steps. Push along the blade and pull back between
stabs. The old `attack_speed` INI key does not select Classic; choose it here.
**Gameplay > Combat > Melee reach** (0 to 50 cm, 0 by default) lets a weapon or fist that
passes that close beside a monster still hit it. Walls still block, and shield
bash is unchanged.
**Gameplay > Combat > Weapon heft** (Off by default) gives weapons weight: a heavy
weapon needs a long, committed stroke of your hand before it meets the monster to
hit fully, and a shorter one lands lighter, never below 30%. A great maul needs
about 40 cm of hand travel, a two-handed sword 32, a crystal sword 16 and a long
sword 12; faster weapons need less. Daggers, claws, wands, orbs, thrown weapons and
fists hit fully with a flick. Turning the wrist or drifting the hand slowly does
not count. Each hit is still one game attack, and the weapon never lags your hand.
**Gameplay > Combat > Weapons collide** (On by default; the right half of Melee
reach's row) keeps your own items from passing through each other: the weapon
stops at the shield on your other arm and slides along its face, and two weapons,
one in each hand, stop at each other. Your real hand cannot be stopped, so the
hero's hand gives way instead, at most 12 cm, and eases back to your controller in
about 0.1 s once they part; push further and they pass through. A strike clanks in
both controllers (Melee hits strength), and a fast one plays the game's block
clank. Bows, two-handed grips, throws, Druid forms and menus are left alone; an
Assassin's crossed claws rest against each other and still block.
**Gameplay > Physics > Struck monsters** (both On by default) makes monsters answer
your strokes. **Monsters stagger:** a long, committed stroke that lands on the beat
makes the monster flinch, whatever the damage. **Heavy hits knock back:** a full
stroke of a two-handed weapon, spear, polearm or staff shoves it back, away from
you. Neither adds damage. A monster staggers at most every 1.2 seconds and is
knocked back at most every 2.5; act bosses never react, and uniques and champions
only stagger.

**Unleashed swing damage:** every stroke that connects lands, but how hard it
hits depends on your character. Your attack rating against the monster's
defense scales the damage (the game's own hit chance, 5-95%), and so does your
rhythm: your attack speed sets a fixed beat. A stroke on the beat (a full cycle
after your last full hit) hits fully; strokes in between hit much weaker but never
delay the next beat, so measured swings beat flailing. Faster attack speed (IAS)
shortens the beat.
One stroke can hit several monsters, each further one a little weaker.
On-hit effects (life and mana leech, critical and deadly strike, crushing blow,
open wounds, chance to cast and the like) come only from a stroke made after
your attack speed has fully recovered, and only on the first monster it hits.

**Unleashed incoming damage:** monsters' attacks and projectiles always reach
you; your shield is what stops them. Your Defense decides how hard they land:
the chance a monster would have had to hit you (5-95%) scales the whole
attack, elemental damage included. Spells never roll to hit, so Defense
doesn't reduce them; resistances still matter.

**Unleashed hit effects:** contact always lands both ways, but everything an
attack does besides its damage (leech, chill, freeze, stun, procs, chance to
cast when struck, thorns, item wear...) only happens when the game's own hit
roll would have landed it. A monster with a 30% chance to hit you chills or
drains you on about 3 hits in 10, as in the original game, and your own swings'
effects follow your attack rating the same way, arrows and throws included:
your attack rating scales an arrow's damage as it does a swing's. A trigger attack
(a basic attack or melee skill on the trigger, or Smite) is a stroke too: attack
rating scales it the same way (not Smite, which always hits), and it shares your
swings' recovery, so mixing trigger attacks with swings is never faster than
swinging alone; trigger attacks in a row are paced only by their animation. Poison and burn
count as damage: they are scaled like the hit itself. Energy Shield, Bone Armor
and the other absorb skills soak and drain at the original game's average rate.

### Shield: block, parry and strain (Unleashed)

- **Block:** raise the shield and put its broad face between you and the
  attacker. You can look elsewhere while holding that position, including
  while walking or running. A valid guard replaces the random block-chance
  roll; the game's other block eligibility checks still apply. Coverage uses
  the rendered shield's orientation and size with a small allowance around the
  incoming attack line. Unsupported meshes use an attachment-based fallback.
  Lowering it, holding it away from the attack line or turning it edge-on opens
  the guard.
  Blows that meet the shield wear it, at the rate it wears as one piece of
  your armor when you are struck (about 2% a block), but only the shield
  wears; the rest of your armor wears only from hits that land. A parry
  spares it.
- **Parry:** as a melee attack comes in, swing the shield diagonally across
  your view (lower right to upper left with a left-hand shield) as the blow
  comes down at you, from late in its wind-up until just after the blow meets
  your shield (0.2 s). A swing while the
  enemy is still raising its arms is too early and is no parry. The swing
  only needs to finish with the shield raised on the attacker's side, even if
  it ends edge-on. The blow meets your shield and the attacker staggers.
  A monster with no stagger animation (Griswold, for one) is
  **deflected**: the parry sound and the riposte, but it keeps its footing.
  **Act bosses** (Andariel, Duriel, Mephisto, Diablo, Baal) cannot be parried:
  the swing meets them as a plain block.
  Every enemy attack makes contact: without a parry, where your shield is at
  that moment decides whether it blocks or hits you.
- **Riposte:** your next weapon hit on the parried monster within 1.5 s is a
  clean hit: full damage, whatever your attack rating or timing.
- **Guard strain:** every block (not a parry) strains your guard, and the
  rarer the attacker the more: a minion strains it a little more than a
  normal monster, a champion twice as much, a unique or super unique two and
  a half times and an act boss four times. A black shadow
  creeps in from the edge of your view; at full strain
  the guard breaks and the shadow swells over most of your view. The shield
  keeps blocking; the cost is sight, then chip damage (below). It fades once
  no hit has reached your shield for a moment, even held up, and is gone about
  3 seconds after the last hit; lowering the shield starts the fade at once.
  About 10 normal-monster blocks reach full strain (an act boss: about 3).
  **Gameplay > Shield > Guard strain shadow** turns the shadow Off (strain, guard breaks
  and chip damage still apply).
- **Guard break:** the guard breaks with a bone snap, and while it is broken
  blocked hits leak a **chip**: part of their physical damage gets through,
  less the higher your chance to block (more while running). Elemental damage
  and its effects land in full, so resistances still matter. It recovers as
  the strain fades: let hits stop or lower the shield.

**Interface > Labels > Loot name size** adjusts dropped-item text and its darker backing from
20% to 150% in five-point steps. The original size is 45%; Apply saves the choice
to `[hud] labels_size`. Existing explicit sizes are preserved on update.

**Interface > Wrist > Buffs and companions** (default On) adds summon icons and live counts below
the left-hand minimap. Necromancers see skeletons, skeletal mages, golem and
revives as those skills become available; other classes use their own summon
groups. Item-granted summons also qualify. A hired mercenary has its own count.
Counts come from your summon roster, including companions outside your view.
Used/available groups stay visible at zero, with the zero highlighted. The row
follows **Map and compass** wrist/palm placement and hides with navigation;
turning off Buffs and companions preserves the map and compass. Unknown or stale
rosters are hidden during loading rather than presented as a lost army.

The same display shows **effects currently on your character**: active buffs,
auras (including received auras), armor spells, warcries, curses, poison/chill,
potion effects and shrine bonuses. These come from native active states, not
learned skills or button assignments. Indicators disappear when the state ends.
Harmful effects appear first in red, beneficial effects in green, and other
statuses in gold. Names remain visible when an effect has no cached skill icon.
Permanent passive skills and internal animation flags are omitted. No duration
or charge count is inferred; this first version indicates presence only.

The display is bounded to three effect rows. If more than 21 effects overlap,
the final cell shows how many additional effects are active. Existing
`[interactions] companion_counts` preferences also control this expanded row.

**Interface > Wrist > Skill cooldowns** (default On, an idea from Vin) shows up to three
dimmed skill icons between the life and mana orbs while those skills recharge after a
cast: Frozen Orb 1 s, Blizzard 1.8 s, Meteor 1.2 s, Fist of the Heavens 0.4 s, Volcano
4 s and every other skill with a delay. Each icon's color sweeps back clockwise from the
top as its delay runs out, and the icon goes when the skill is ready. Only skills you can
cast in VR show: on a hand button, a glyph or a motion. A glyph or motion cast of a skill
that is still recharging is refused with the refusal buzz and a grey trail instead of the
cast's, and a light tick on that hand follows once the skill is ready. The times are the
game's own, read from your offline game's server; a game hosted by another player shows
none. Saved as `[interactions] skill_cooldowns`.

**Interface > Warnings > Blood on visor** (default On) splashes blood around the edge of the
view as life drops below 60%, 40% and 20%; large splats drip slowly, then dry.
Healing back above a band, from any source, fades that band's blood out over
about a second. Changing maximum life alone (Battle Orders, Oak Sage, +life gear)
neither splashes nor fades. With **Interface > Warnings > Wipe blood by hand** On (the default),
scrubbing a hand side to side in front of the face wipes blood
at any time: each stroke ticks the controller and smears some splats away. While
you are still hurt, the visor then stays clean until the next hit splashes it
again. The centre of the view always stays clear. While a hand is scrubbing it
does not attack; a held potion, squeezed trigger or bow draw never counts as
wiping.

**Low-life heartbeat:** **Interface > Warnings > Heartbeat starts at** (default 60%, or Off)
picks the life where a quiet heartbeat begins. Every 10% of life you lose below
that, it gets louder and faster, at full strength below 10%. Healing winds it
down beat by beat. Wiping the visor clean fades it out too, until the next hit
brings it back. It plays in Classic and Unleashed; `[gameplay]
low_life_heartbeat` sets its loudness (200%, 0 = off). **Felt** in the same row (On by
default) beats it in both controllers too, a lub-dub that quickens and strengthens as
life drops and stops in menus and when you die; **Controls > Vibration > Low life and
mana alerts** sets its strength.

**Hit feel:** with **Controls > Vibration > By target** On (the default, in the Short
melee taps row), a hit feels like what you struck: one thud for flesh, three clicks for
skeletons, zombies and mummies, a soft shiver for ghosts, a crack and crumble for pots
and barrels, two ringing taps for a parry. A bow's string ticks harder and faster the
further you draw it, trembles faintly when held, and snaps on release.

## Hand skill layouts

| Slot | Quest left | Quest right | Index, either hand |
| --- | --- | --- | --- |
| Primary skill | Trigger | Trigger | Trigger |
| Second action | X | A (Interact by default) | A |
| Third action | Y | B | B |
| Alternate second | Left Grip + X | Right Grip + A | Same-hand Grip + A |
| Alternate third | Left Grip + Y | Right Grip + B | Same-hand Grip + B |

There is no left-trigger modifier layer. Each hand aims its own spells. Face
shortcuts do not change either trigger assignment. On a Sorceress, Fire Bolt,
Fireball, Ice Bolt, Ice Blast, Lightning, Charged Bolt and Chain Lightning can fire from both hands simultaneously,
each with independent aim and its own Faster Cast Rate timing. Hold the input to repeat. Native
mana costs apply to each cast, and walking remains available without entering
the original casting animation. See [hand projectile spells](docs/hand_projectile_spells.md)
for the experimental adapter's scope and offline testing checklist.

**VR Settings > Gameplay > Targeting > Keep spell shots level** defaults to Off for new or
missing settings, allowing up/down aiming of straight projectile spells. Turn
it On and Apply to keep those spells level; Charged Bolt keeps its native planar
spread. Existing saved preferences are retained, and changes apply to new casts.

**Spell aim assist** on the same page defaults to Strong for new or missing
settings; existing saved choices are retained. Off disables assistance. Light corrects nearby
misses at launch; Strong adds gentle homing toward the enemy selected at launch
for straight hand spells. Charged Bolt and Chain Lightning remain launch-only. This does not affect
bows, thrown weapons or other native spells, and does not guarantee a hit.

Other skills retain native repeat behavior, delays and animation restrictions.
A native action has one owner: the first held input wins, with right-hand
priority for simultaneous presses. Native and physical casts do not start
together. A competing input must be released and pressed again; it is never
queued to fire unexpectedly when the first hand stops.

Select a wheel wedge to edit its binding directly. **CLEAR SLOT** removes the
skill; clearing right A restores Interact, and clearing right trigger restores
basic weapon handling. Interact can also be chosen explicitly from the picker.
Assigning a spell to right A deliberately replaces its interaction action.
The skill picker has **All**, the character's three skill-tree categories, and
**General / Items** tabs. For example, a Sorceress has Fire, Lightning and Cold;
a Paladin has Combat, Offensive Auras and Defensive Auras. General / Items holds
Attack, Interact, Town Portal and skills from equipment or another class. Each
tab shows its available count; empty categories are dimmed. Choose a category,
then a skill, using **PREVIOUS / NEXT** when needed. Only available active skills
appear; passive skills cannot be assigned. Existing bindings are preserved.
Grip changes cannot switch a held face button to another spell: release the
face button first. Held potions and holsters take priority over spell shortcuts.

Bindings use the game's controller assignments, saved by the game per character
and weapon-set bank. Existing assignments are retained in these native slots:

| Hand | Trigger | Primary face | Secondary face | Grip + primary | Grip + secondary |
| --- | --- | --- | --- | --- | --- |
| Right | B | A | X | Y | RB |
| Left | LT + B | LT + X | LT + Y | LT + RB | LT + A |

These are underlying **native gamepad slots**, not physical VR button labels.
Native duplicate-assignment rules still apply: assigning a skill may clear its
previous native slot. Native menus show their usual gamepad prompts. The mod
does not directly edit saves or invent extra game-owned skill records.

### Glyph casting

Draw a shape in the air to cast a buff, an aura or another skill that needs no
target: armors, warcries, auras, Energy Shield, summons and the like.

1. Choose the button that draws: open a hand's wheel, select one of its
   wedges and pick **Draw glyph** (last under **General / Items**). That button
   now draws and casts nothing else; its game skill is cleared. Putting a skill
   back on it, or **CLEAR SLOT**, stops it drawing. Each hand can have its own
   Draw glyph button: put one on the other hand's wheel too to draw with both.
2. Give the shapes their skills: the wheel's **GLYPHS** box (upper right, on the
   spells and equipment pages) opens a page of eight shapes: circle, triangle,
   square, lightning bolt, spiral, V, star and wave. Select a shape, then the
   skill it casts; the list holds your hero's own skills that need no target.
   **CLEAR GLYPH** takes its skill off; **BACK** keeps it. Each hand has its own
   eight, so the same shape can cast one skill drawn with the right hand and
   another drawn with the left: the page opens on the hand whose wheel you
   opened, and the box at its upper left (**SHAPES OF THE** right or left hand)
   shows the other hand's when clicked. A
   left-hand shape without a skill of its own casts the right hand's (shown
   dimmed, under RIGHT HAND'S); **CLEAR GLYPH** on a left shape gives it back
   the right hand's.
3. In play, hold the Draw glyph button and draw the shape in front of you, like
   writing on a window: one stroke, at least about 12 cm across, in under four
   seconds. Where you start and which way round you go don't matter; the
   triangle points up and the V opens up, and the lightning bolt runs from the
   top down. Let go to cast.

The line glows pale while you draw. A recognised glyph flashes, then takes on
its skill's element for about two seconds before it goes:

- **Fire**: flames lick up off the line and embers rise; it burns away along a
  glowing edge.
- **Cold**: ice shards grow out of it; it cracks, and the pieces fall away.
- **Lightning**: arcs crackle and jump along it, throwing sparks; it flickers out.
- **Poison**: green mist sinks from it and drops fall; bubbles rise and pop.
- **Holy** (most Paladin auras): golden rays rise from it among glints.
- **Magic** (Energy Shield, Teleport, Inner Sight, Valkyrie, Meditation,
  Conviction...): motes run along it.
- **Warcries** (and Might, Thorns): it beats like a shout and rings out in echoes.
- **Nature** (the Druid's spirits, vines, beasts and forms, Cleansing): spores
  drift off it in echoes.
- **Bone** (bone spells, skeletons, Clay Golem, Revive): it smokes and sheds chips.
- **Curse** (Necromancer curses, Blood Golem, Corpse Explosion, Death Sentry):
  blood-red mist sinks and a ring closes in on it.
- **Shadow** (the Assassin's shadow disciplines): it smokes away.
- **Steel** (blade skills, Iron Golem): blades spin along it striking sparks.

The Warlock's skills follow their tree: Demon a curse's, Eldritch shadow's and
Chaos fire's. A glyph that isn't recognised, or has no skill, greys and crumbles
to ash with a short buzz. Opening a wheel or losing tracking mid-stroke cancels it.

The skill is cast by a native button press, so mana, cast rate, the buff and its
sound stay the game's: the skill's own hand button when it is on one, else the
game's **right-trigger** skill slot (native slot RT, which no hand button
reaches in VR). A shape's skill is put on that slot when you first draw it, the
way the game assigns any skill; like every assignment the game saves it with
the character and weapon set, and a skill you had on RT in a plain controller
layout is replaced. The shapes are shared by all your characters: a shape whose
skill your hero lacks shows **Another hero's** and casts nothing. Settings:
`[glyphs]` in the profile INI (`draw_slot` and `second_draw_slot`, one a hand;
one skill id per shape for the right hand, and `left_circle` ... `left_wave`
for the left, where -1 casts the right hand's), edited from the wheel only.
Shapes given skills before each hand had its own set are the right hand's, and
the left hand's until a left shape gets its own.

## Weapon skill gestures

Active skill icons and name panels beside the weapons are **off by default**.
Review assignments by opening either hand's wheel. To restore the weapon
indicators, open **VR Settings > Gameplay > Combat > Skill icons on weapons**, turn it
**On**, and press **Apply**. This changes both hands' visual indicators only;
assigned spells and weapon skill gestures keep working. The saved preference
is `[interactions] weapon_skill_indicators=0` (hidden) or `1` (shown).

**VR Settings > Gameplay > Shield > Bash speed to hit** adjusts physical bash/Smite
independently of weapon swings: default 2 m/s, range 0.75-10 m/s in 0.25 m/s
steps. Move the hand at least 12 cm in a deliberate stroke. Wrist rotation alone
does not attack, and contact still requires the selected speed. Blocking, parries
and button-triggered attacks use their existing rules.

The trigger skill is the persistent gesture skill for its hand. Optional
hand-side icons show the assignments. Left-trigger Smite supports physical shield swings
and thrusts when a shield and its native hook are available. Supported arrow
skills assigned to left trigger fire on bow draw/release using the right hand
to pull the string. The left trigger remains a button fallback; right trigger
away from the string uses the right hand's own assignment.

A trigger skill that works as a gesture with what the hand holds says
**GESTURE** instead of PRIMARY in that hand's wheel, and the hand's skill card
says how: **Shield swing / thrust** (Smite with a shield), **Draw & release**
(arrow skills with a bow), **Swing to strike** (melee skill strikes, below) or
**Throw to place** (the Assassin's traps and bombs). Other trigger skills cast
where you point.
Pointing at a skill in the skill picker (**EDIT SKILLS**), or at a slot in a
hand wheel, shows a line under it saying how that skill works from that slot:
the gesture on a trigger, or what its button does. Skills a landed swing or shield
bash casts (melee strike skills, Fury and Zeal, Double Swing and Frenzy, Smite with a shield) are
tagged **CAST ON STRIKE**; body gestures (Holy Shield's sign of
the cross, the shapeshifts, Blessed Hammer, Charge, Fist of the Heavens), bow draws
and throws keep **GESTURE**. Both need a trigger slot, except the body gestures,
which work from any slot. The body gestures are the Paladin's and the Druid's
motions: do them with both grips held, as every class's motions. Both
controllers hum once a motion's first part is done, stronger as it nears its
cast, and pulse together when it casts; each motion has its card on the class's
**MOTIONS** page (the wheel), where it can be turned off.

**Bow quiver.** With a bow, a face button assigned to Magic Arrow, Fire Arrow,
Cold Arrow, Exploding Arrow, Ice Arrow, Immolation Arrow or Freezing Arrow loads
that skill instead of casting it. Every following arrow you nock shows the
skill on the string and flies the basic arrow's physical path when released;
press the same button again for basic arrows, or another arrow skill's button
to swap. The skill's own mana cost, damage and impact effects apply to each
shot. If the game refuses it (usually not enough mana), the drawn arrow flies as
a basic arrow and the character's mana warning plays. With nothing loaded, a
supported arrow skill on left trigger uses the same physical release.
A face button assigned to Multiple Shot, Guided Arrow or Strafe loads it the
same way: every following draw and release fires it, through that button, as
the game does, so Multiple Shot keeps its fan, Guided Arrow its homing and
Strafe its volley. Press the button again for basic arrows. Set
`[bow] quiver=0` to restore native buttons for these skills.
The selected skill also changes the nocked arrow's model and particles. Set
`[bow] quiver_preview=0` to keep the basic nocked model while still firing skill
arrows. Model swaps wait for the native client update to retire the previous
arrow; a brief basic fallback can appear while the new model loads.

**Javelin skills.** With a javelin in hand, the hand throw carries Poison
Javelin, Lightning Bolt, Plague Javelin or Lightning Fury: bind one to the right
trigger (it arms the throw), or press a face button bound to one to load it
into every following throw, like the bow quiver (press again for basic throws).
The skill's own mana cost, damage, clouds and lightning apply, and each throw
uses up a javelin. If the game refuses it, a basic javelin flies instead.
Poison and Plague Javelin fly the javelin model; Lightning Bolt and Fury fly as
their lightning. `[bow] quiver=0` turns this off too.

**Double Throw.** A Barbarian with a throwing weapon in each hand can bind
Double Throw to the right trigger, or load it from a face button the same way.
Each hand then throws its own weapon: the right with the right trigger, the
left with the left trigger, together or one after the other. While it is armed
the left trigger throws instead of casting its own skill. Each throw is one
native Double Throw of that hand's weapon, with its attack bonus, mana and
quantity. The game's own Double Throw throws both weapons for one mana cost;
here each hand's throw pays it. If the game refuses it, the hand throws its
weapon as a basic throw.

**Throwing traps and bombs.** With an Assassin's Fire Blast, Shock Web, Blade
Sentinel, Charged Bolt, Lightning or Death Sentry, Wake of Fire or Wake of
Inferno on the right trigger, press the trigger: the bomb or trap appears in
your hand. Throw it and let go, and the game casts the skill where its arc
lands, within the skill's own reach. A quick press still casts where you point
(with **Ground skills** on Where you point; on Game, where the game aims), and
holding the trigger still for a moment casts natively as before, repeating
while held. Start the throw right after pressing; a slow reach casts nothing.
The models are the game's own, cached from your installed game by the
installer. **Gameplay > Combat > Skills on your moves** Off turns this off.

**Throwing potions.** Equip a stack of gas or oil potions (Rancid, Choking or
Strangling Gas; Oil, Exploding or Fulminating Potion) in either hand, with
Attack or Throw on that hand's trigger. Press the trigger, throw with the hand
that holds the potion and let go. While the trigger is held, a sigil on the
floor shows where the potion will land: hold the hand out in front to move it,
then wind up and throw. With a potion ready in each hand, each has its own
sigil. If the potion is your only weapon and only the other hand's trigger has
Attack or Throw, that trigger arms the throw and the potion's hand still makes
it. An off hand potion beside a weapon or a second potion in the main hand
needs Left Hand Throw on the off trigger. With
**Bottle physics** on, the potion leaves your hand as a real bottle and flies
until it first touches the floor, a wall, a table or a prop; it shatters there,
and the game's own cloud or blast starts at that spot, within its reach. Off,
the game's own Throw lands the potion where your arc meets the floor. Running
carries the throw either way, and **Gameplay > Combat > Thrown weapon lift** raises it like any throw. A quick press or
a still hold throws as it did before. A potion in hand is never swung as a
melee weapon. **Gameplay > Combat > Skills on your moves** Off turns this off.

Face-button skills never replace gesture skills. Ordinary melee swings remain
basic attacks for skills without a supported physical adapter; assigning a
spell does not cast it on every swing. Physical skill gestures can be disabled
under **VR Settings > Gameplay > Combat > Skills on your moves** (on by default), retaining
button casting and basic physical attacks. This implementation supports the
existing Smite and bow adapters, and melee skill strikes (below).

**Melee skill strikes.** A supported melee skill on a hand's trigger is that
hand's strike skill: a weapon or claw strike that lands casts it on the
creature it struck, with the skill's own mana, damage and effects, and no
animation. So far:

- the Druid's single-hit form skills: **Maul** and **Feral Rage** (their
  stacks build per landed cast), **Rabies**, **Fire Claws** and **Hunger**;
- the Barbarian's **Bash**, **Stun**, **Concentrate** and **Berserk**;
- the Paladin's **Sacrifice**, **Vengeance** and **Conversion**, and **Zeal**
  as its native sequence (below);
- the Amazon's **Power Strike**, **Impale**, **Charged Strike** and
  **Lightning Strike**;
- the Necromancer's **Poison Dagger**.

A weapon's left-trigger skill rides the left weapon of a dual-wield pair.
Skills that hit several times a cast with their own native sequence (Jab,
Fend, Frenzy, Double Swing, Whirlwind) and Charge stay trigger buttons. The skill wheel's lower-left box, **Left claw strikes** (Druid
only), picks the left claw's skill with one click: its own trigger's
(default) or the right trigger's (a skill sits on one trigger only, so this
puts it on both claws). For plain left-claw strikes, leave the left trigger
without a strike skill. In
Unleashed only a stroke that waited a full attack cycle casts, on the first
creature it lands on; faster strokes and the rest of a cleave stay basic
attacks. In Classic every paced strike casts. Without mana, outside the form
or with the wrong weapon, the strike lands as a basic attack (no mana also
plays the character's mana warning). In Werewolf and Werebear form each
strike that is one native attack (a full-cycle stroke in Unleashed, every
paced strike in Classic) plays the form's growl as the native attack would
(85% / 80% of the time), and Rabies and Hunger their bite. Holding the trigger still casts the
skill natively. **Gameplay > Combat > Skills on your moves** Off keeps strikes basic.

**Fury and Zeal: one swing, all the hits.** The full-cycle swing that lands
with werewolf **Fury** or the Paladin's **Zeal** (the rule above) casts it on
the monster you hit: its mana once and hit 1. The rest of its hits (two to five
in all) then land by themselves at the skill's native speed (Fury about 4/9
of your attack cycle apart, Zeal half a cycle), each on the nearest other monster within
reach, as natively; you don't act them out, and you keep moving freely. If no
monster is left in reach, the remaining hits are lost. Swings while it runs,
and quicker swings, stay basic attacks; the next full-cycle swing once it ends
casts again. Only the casting swing gives the impact pulse.

**Zeal's light.** Every Zeal hit flashes one ray of the Den of Evil's light
over the monster it lands on, then it fades before the next hit, whether a
swing casts Zeal or you hold it on the trigger. While Zeal is on a hand's
trigger, a few rays are kept lit out of sight so each one shows at the moment
of the hit. Purely visual. `[gameplay] zeal_light=0` turns it off;
`zeal_light_frames` (2-25, default 6, at 25 a second) sets how long each ray
holds; `zeal_light_pool` (0-10, default 6) how many are kept lit;
`zeal_light_exit` how each ray leaves (1 rises into the sky, the default; 2
sinks into the ground; 3 shrinks; 0 just goes) over `zeal_light_exit_ms`
(default 250). The ray stands behind the monster as you see it, at a
different angle each time, its light slanting in from that side, never from
behind you: `zeal_light_behind` (cells behind it, default 1.5),
`zeal_light_spread` (degrees either side of straight behind, default 60),
`zeal_light_turn` (degrees, default 0) trims the slant.

Turning gestures off or changing equipment/assignments cancels an in-progress
skill draw. Shield contact and its trigger fallback cannot duplicate a cast.
Projectile targets follow the casting hand rather than locomotion direction.
The five supported Sorceress projectile spells bypass the native casting
animation; other skills can still stop movement through their native animation.
The new physical casting path still requires offline headset validation.

## Menus: laser and native buttons

Both methods work in the main menu and native game panels, including inventory,
stash and merchants. Keep the D2R window focused on the PC.

Use a **stick or face button** to start native button navigation and follow the
game's displayed prompts. The idle laser will not move your native selection.
To return to the laser, release the native controls and deliberately move your
aim across the panel. A small hand tremor will not change methods.

### Laser controls

| Action | VR control |
| --- | --- |
| Hover / point | Aim the right controller's laser |
| Select / drag an item | Right trigger; hold while moving the item and release to place it |
| Context / alternate click | Left Grip + Right Trigger (hold grip first) |
| Native Ctrl-click shortcut | Hold Left Trigger, then click with Right Trigger; the game chooses drop, sell or transfer for the current panel |
| Native Shift modifier | Hold Right Grip while using the laser |
| Push / pull the panel | Point at the panel, hold Right Grip and move that hand toward or away from you; release to leave it there. The VR Settings panel can also be moved and tilted this way |
| Back / close | Right B, or point and click the panel's close/back button |
| Tabs / page controls | Point and click the native on-screen controls |

Dragging keeps the item held if the ray briefly leaves the panel. Releasing
the trigger ends the drag at the last valid pointer position. Tracking or
desktop focus loss releases the injected mouse button; release the trigger
before starting again. Pressing or releasing Left Grip during a drag will
not convert it into a context-click.

### VR keyboard (character names)

D2R shows no on-screen keyboard for the character name, so the mod adds one.
When you pick a hero on the character creation screen, a keyboard appears
under the menu, tilted toward you. Point the laser at a key and pull the right
trigger to type it into the name box.

| Key | Effect |
| --- | --- |
| Letters, `-`, `_` | Type the character (letters are small) |
| Shift | The next letter is a capital (the keys show capitals while it waits) |
| Back | Delete the last character |
| Enter | Confirm, as the keyboard's Enter does |
| Close | Hide the keyboard until you pick another hero or reopen character creation |

The keys follow your Windows keyboard layout, so a letter types the same
character on any layout. A physical keyboard keeps working alongside it.

### Native menu prompts

| Game's displayed input | Quest control | Steam Frame control |
| --- | --- | --- |
| A | Right A | Right A |
| B / back | Right B | Right B |
| X | Left X | Left A |
| Y | Left Y | Left B |
| Left stick / right stick | Left stick / right stick | Left stick / right stick |
| LS / RS click | Left stick click / right stick click | Left stick click / right stick click |
| Inventory: X / drop selected item | Left X; hold if the native prompt requires it | Left A; hold if the native prompt requires it |
| Inventory: hold RS / sort | Press and hold right stick click | Press and hold right stick click |
| LB / RB | Left Grip / Right Grip | Left Grip / Right Grip |
| LT / RT | Left Trigger / Right Trigger | Left Trigger / Right Trigger |
| Menu / Start, where available to the game | Controller menu button | Controller menu button |
| Advanced Stats (the game's View in the tabbed menu) | Laser + trigger on the stone stats button below the menu's hint row | Laser + trigger on the stone stats button below the menu's hint row |
| Reset Skills & Stats (offline) | Laser + trigger twice on the Character page's **Reset Skills & Stats** button | Laser + trigger twice on the Character page's **Reset Skills & Stats** button |

Quest and most VR controllers have no View button (the menu buttons belong to
the headset), so the
tabbed menu (on the leather or the room panel alike) carries its own Advanced
Stats button: a small stone badge with three gold bars just below the menu's
hint row. Point the laser at it (it lights) and pull the trigger: the game gets
one View press, as from an Xbox pad, and opens or closes Advanced Stats. It is not shown with the cube, the stash, merchants or other panels.

The Character page also carries a stone **Reset Skills & Stats** button in its
lower right, in offline games. Pull the trigger on it once and it asks
**Confirm?** (with no time limit; leaving the page or closing the menu takes the
question back); pull again and the game resets every stat and skill point, as a
Token of Absolution does. It is free, works as often as you like and leaves
Akara's own reset untouched. It needs one free inventory square for a moment
(the button says so if there is none) and does nothing for Classic characters
or in TCP/IP games.

Native menu actions use the same buttons in the main menu, tabbed menu, stash,
inventory and merchant panels. **Inside a native menu, left primary performs the displayed X action.**
During motion gameplay, Inventory and VR Settings are available in either wheel.
VR Settings reopens on the page you closed it on, Right B included; a new game
starts it over on the Guide.
Right-stick click toggles the right wheel during gameplay. Use the shoulder
gesture or **EQUIPMENT / BELT** wheel for weapon configurations.

Quest controllers do not need a separate D-pad or Xbox controller: use sticks
for native navigation and the laser for the on-screen controls. When using
the laser, Right Trigger is a mouse click and the grips/triggers above provide
its modifiers; once native navigation is selected, they perform LB/RB/LT/RT.
Release buttons, triggers and sticks when opening or closing menus so a held
input cannot also act in the next screen or in gameplay.

The mod's own settings and calibration panels use their VR laser controls:
right laser + trigger to select, Right B to close/cancel. Native gamepad input
does not pass through those panels.

### Quick-use inventory slot

Both wheels share one assignment, remembered by item type across restocks and
sessions. The displayed count includes carried inventory items or tome charges;
belt items keep their existing potion controls. Stash and cube contents are not
included. Use follows the native game's restrictions. Identify opens inventory
for target selection. A long press edits without spending an item. Move the
pointer away before releasing to cancel a short press. If the item changed,
ran out, or tracking was lost, the use is discarded rather than retried.

The minimap has a narrow bronze bevel and inset corner details. The full native
map is fitted inside the frame so edge markers remain visible.

A gold arrow sits on the game's blue hero marker and points the way you face.
Change its view under **VR Settings > Interface > Wrist**, then press **Apply**:

- **Map view: Isometric / Top-down / Full map / Full top-down.** Top-down flattens the
  game's slanted map into a plan, so rooms are square and the arrow matches your real
  turns. Map names are left out in top-down, since they would turn 45 degrees with the
  floor. **Full map** and **Full top-down** show the game's full (Tab) map instead of its
  corner map, in the same two views, and zoom much further (below).
- **Turn map with you: Off / On** (top-down views only). On turns the map so the way you face
  is up; Off keeps north up.

Both are saved like any setting. Isometric with north up is the default.

To zoom, look at the wrist or palm map (its center within about 22 degrees
of where your head points) and push the right stick up to zoom in about your
hero, up to 4x, or down to zoom out; 1x, the whole native map, is the lowest.
Only a mostly vertical push zooms, and during it left / right does not turn
until the stick is back near the center. A light tick marks each quarter step
(faster as it zooms in) and a firmer one either end. The wheel's MAP tab shows
the same zoom, and the stick zooms it the same way while you look at it. The
zoom is not saved: every new game starts at 1x. Looking away gives the right
stick back from the next frame; a push still held then must return to the
center before it toggles running or shows loot labels. A push already held
when you look at the map zooms it straight away.

With **Full map** or **Full top-down**, 1x is the game's full map at its widest
(the game's own zoom 0.125, about four times wider than its Tab map), and the
right stick zooms in to 16x. The game redraws its map closer as you zoom in, so it
stays sharp; past 4x a tick marks each whole step. The game's full map is a window
around your hero the size of its screen: the widest view shows that window, not
the whole level. While the wrist or palm map shows it, or a floating map is out and you look toward it, the full map is not drawn in
your view; press Tab as usual for the game's own map.

**Floating map:** point the right controller at the raised wrist map (or within about 15 degrees of its edge) and squeeze
**Right Grip** to pull a copy off the wrist; the wrist keeps its own map, and the copy always shows what
**Map view** shows. Held, it follows your hand in every direction; the right stick forward / back sends it out (up
to 3 m) or pulls it in, and does not turn you while you hold it. Let go and it stays there in the room, turned to
face you. Point at it and squeeze Right Grip to move it again. Both grips within a quarter second, either order,
with either hand pointing at it, size it: spread your hands to grow it, bring them together to shrink it (10 to
150 cm); it moves with your hands, keeping its place between them. Bring it back to the raised wrist map (a light
buzz) and let go to put it away. It shows while you look within about 30 degrees of it. While a copy is out, a
light buzz tells you when your aim is on a map you can take, and pointing anywhere on the floating map and pushing
the right stick up / down zooms it (the wrist map zooms as before, by looking at it; both show the same picture).
Every place you can point at on the floating map grows and shrinks with it: the zoom area is the map itself, and
a grab takes it a little past its edges: a share of its size, up to about 15 degrees past them. A load
screen in the same game keeps it where it is; Save and Exit or a new game puts it back on the wrist. With no copy out, the wrist map and the
stick work as before; a Left Grip pressed while you point at the raised wrist map reaches the game up to a
quarter second late, in case Right Grip follows.
