#pragma once
#include "vr_settings.h"
#include <iterator>

namespace framevr::settings {
// Keep the existing panel's pixel and physical size. Help occupies an extra,
// transparent strip on its right in the same atlas/composition layer.
constexpr int HelpGap=24,HelpWidth=440,AtlasWidth=Width+HelpGap+HelpWidth;
constexpr Rect HelpCard{Width+HelpGap,242,HelpWidth-24,612};
constexpr int NoHelp=-1,HelpApply=Count,HelpClose=Count+1,HelpRecenter=Count+2,HelpCalibrate=Count+3,HelpMonoAudio=Count+4,
              HelpPerformance=Count+5,HelpGameVideo=Count+6,HelpFrameGen=Count+7,HelpLast=HelpFrameGen;
inline const char* HelpTitle(int id) {
    if(id>=0&&id<Count)return Definitions[id].label;
    switch(id){case HelpApply:return "Apply";case HelpClose:return "Close";
        case HelpRecenter:return "Reset eye height";case HelpCalibrate:return "Calibrate body";
        case HelpMonoAudio:return "Windows Mono audio";
        case HelpPerformance:return "Frame times now";case HelpGameVideo:return "The game's graphics options";
        case HelpFrameGen:return "Frame generation now";default:return "";}
}
inline const char* HelpText(int id) {
    switch(id) {
        case Motion:return "Enables tracked weapon swings, bow drawing, throwing and belt interactions. Keep this On to use physical combat. A selected trigger skill does not replace your free weapon swings.";
        case Seated:return "Raises the potion belt for easier reach while seated. Turn this Off to place the belt using Belt drop below eyes. Body calibration still uses your standing measurements.";
        case Dominant:return "Which hand's grip swaps your weapon set when you reach over either shoulder. This does not swap the game's weapon and shield hands.";
        case SmoothTurn:return "On turns continuously with the right stick at Smooth turn speed. Off turns in steps using Snap turn angle. Choose whichever feels more comfortable.";
        case Snap:return "How far each right-stick flick turns you when Smooth turning is Off. Smaller angles give finer steps. Release the stick between snap turns.";
        case TurnSpeed:return "How quickly the right stick rotates your view when Smooth turning is On, in degrees per second. Lower values turn more slowly. This does not affect snap turns.";
        case Waist:return "Distance from your eyes down to the potion belt in standing mode. Lower this value to raise the belt. Seated belt position uses its own raised placement.";
        case BeltForward:return "Moves the potion belt forward from your estimated torso. Increase it if the belt is too close to your body; decrease it if you have to reach too far.";
        case Foveation:return "Shades the edges of the view at reduced quality to save GPU time. Foveation strength sets how much.";
        case FoveationStrength:return "How much detail foveated rendering removes at the edges. Stronger levels save more GPU time. Available when Foveated rendering is Fixed or Eye tracked.";
        case Sky:return "Shows the mod's added sky outside. This changes the background above the level; it does not increase terrain or object draw distance.";
        case Fog:return "Adds distance fog to outdoor scenery. Turning it Off can make distant scenery clearer. Indoor added fog is disabled by default to avoid artifacts through transparent effects.";
        case Ceiling:return "Shows the mod's added overhead surfaces in supported dungeon areas. These are separate from the game's building roofs.";
        case DrawDistance:return "Controls how many surrounding map tiles remain drawn. Higher values reveal more terrain but use more rendering resources. Lower this first if large outdoor areas run poorly.";
        case ObjectDistance:return "How far away scenery objects remain visible. Higher values retain more distant buildings and props, with a performance and memory cost. Lower values can help crowded areas such as Act III.";
        case RenderResolution:return "The image size drawn for each eye, independent of your monitor. Higher is sharper but costs GPU time. Apply rebuilds the VR view; DLSS is set in the game's own Options.";
        case StereoRendering:return "How the two eye images of each headset frame are drawn. Menus, and views other than first person, always draw both eyes.";
        case DeferGpuWait:return "Lets the CPU begin the next frame while the graphics card finishes the last one, instead of waiting for it at the end of every frame. Without it each frame takes the CPU's time plus the GPU's, and every stereo rendering choice runs at about half its frame rate. Shown while it is off; Apply saves it as [render] defer_gpu_wait.";
        case EyeHeight:return "Your standing eye height above the tracked floor. Body calibration measures this to set the world's scale. Use Calibrate body to measure it again.";
        case ArmSpan:return "The distance between your controller grips with your arms extended in a T-pose. Body calibration uses it to fit the character's arms to yours.";
        case BodyCalibration:return "Stores your measured standing height and controller span. Use Calibrate body to repeat the guided measurement; updating the mod preserves it.";
        case SkillGestures:return "Lets compatible assigned trigger skills power physical actions: Smite with a shield bash, arrow skills with bow release, and melee skills such as Bash, Charged Strike or Maul with a landed strike. The skill must be learned and available. Off, every swing is a basic attack.";
        case WristMap:return "Shows the minimap on your left hand. Minimap & Compass controls its placement. The wheel's MAP tab remains available.";
        case WristCompass:return "Shows compass bearings on your left hand. North follows the top of the minimap. Minimap & Compass controls its placement.";
        case ControllerLabels:return "Which controller's button names appear in help and menus. This changes labels, not button bindings.";
        case WeaponSkillIndicators:return "Shows the assigned skill's icon and name beside each weapon. Turn this Off for a clearer view. This describes the assigned skill, not buffs currently affecting your character.";
        case PlanarSpellAim:return "Off lets supported straight hand spells aim up or down. On keeps those shots level with the ground. Ground-based spells such as Charged Bolt keep their native path.";
        case ProjectileAimAssist:return "Help for aiming supported hand spells. Bows and crossbows are always aimed by you alone.";
        case VitalsLocation:return "Where health and mana appear on your left hand.";
        case VitalsTilt:return "Rotates the health and mana display relative to your hand. Positive values tilt its top toward you; negative values tilt it away. The minimap and compass keep their own angle.";
        case CriticalAlerts:return "Shows a brief warning and vibrates when health or mana is low. Adjust its vibration under Haptics > Resource alerts. Turning this Off disables the warning, including its vibration.";
        case NavigationLocation:return "Where the minimap and compass appear on your left hand. Independent of health and mana.";
        case MapTopDown:return "The wrist minimap's view. The gold arrow shows your facing direction. Full map and Full top-down show the game's full map instead of its corner map: look at it and push the right stick up to zoom in a lot, down to zoom back out. Apply saves your choice.";
        case MapRotate:return "In Top-down and Full top-down views, Off keeps north at the top and turns the arrow with you. On rotates the map so your facing direction stays at the top. This has no effect in the isometric views; your choice is kept for the next time you use Top-down. Apply saves it.";
        case OrdinaryMobBars:return "Shows names and health bars for ordinary enemies. Turn it Off to reduce clutter. Boss and elite bars remain visible.";
        case MenuDistance:return "How far away eye-level game panels, such as inventory and merchant menus, appear. You can also point at a game menu, hold Right Grip and push or pull it; menus reopen at that distance until you change this setting or restart. This does not move this VR Settings panel; grab it with Right Grip to move, push, pull or tilt it.";
        case TooltipTiming:return "How soon item details appear when you point the laser at an item in a game panel.";
        case CompanionCounts:return "Shows companion counts and effects currently affecting your character below the left-hand minimap. This includes received buffs, auras and harmful effects, not just assigned skills. It follows Minimap & Compass placement.";
        case OfflinePlayers:return "Uses native offline Players 1-8 scaling for tougher monsters and increased experience and loot. Apply affects newly spawned monsters; existing monsters keep their current stats. For all monsters to use the new value, Save and Exit, then start a new game with the same character. Game default follows the game's own setting. Works with every class and either melee mode. Does not change Normal, Nightmare or Hell, or your desktop preference.";
        case StrainVignette:return "A black shadow at the edges of your view as repeated blocks strain your guard in Unleashed; rarer attackers strain it faster. It clears when hits stop for a few seconds or you lower your shield. Off hides only the shadow: strain, guard breaks and chip damage still apply.";
        case TargetHighlight:return "A thin, steady outline round the monsters you aim at. Off by default; the percentage sets how strongly it shows. A hand held out picks the monster it points at; with no hand on one, the monster you look at is outlined. Monster highlights and the hand colors below choose which show and how; the one you look at takes the color of the hand that would cast at it. Only monsters in sight count. A guide only: it does not change how skills aim.";
        case AimHighlights:return "Which aimed monsters are outlined. A button you hold keeps showing the monster its cast took until you let go. Greyed out while the Monster outline is Off.";
        case HighlightRight:return "The outline color of the monster your right hand points at, and of the monster you look at when the right hand would cast at it (Dominant hand Right, no bow equipped). By range: red near, orange mid-range, yellow far. Otherwise one color around the color wheel, 15 degrees at a time; gold by default. A color unlike the monster's own reads best. Greyed out while the Monster outline is Off.";
        case HighlightLeft:return "The outline color of the monster your left hand points at, and of the monster you look at when the left hand would cast at it (a bow equipped, or Dominant hand Left). By range: red near, orange mid-range, yellow far. Otherwise one color around the color wheel, 15 degrees at a time; blue by default. A color unlike the monster's own reads best. Greyed out while the Monster outline is Off.";
        case SpellLock:return "Skills take the monster your casting hand points at (arm held out a tenth of your Controller span), else the other hand's, else the one you look at, and keep it until you let go or it walks out of the game's reach. Bolts fly at it; curses and Corpse Explosion land on it. With no monster, where that arm points, else where you look.";
        case AimFollowsView:return "On, the aimed monster follows your head: your eyes catch one within 17 degrees of where you face and keep it to 30 until another comes within 17; a hand picks only within 30 degrees. A casting hand held out and pointing aims alone: its own monster, else where it points. The one you look at loses its outline while your main hand points. Charge, Leap Attack, Dragon Flight and Whirlwind also take the other hand's outlined monster. Off by default.";
        case StaffPoint:return "A staff held upright would cast along its shaft, straight up. With this On, an upright staff casts along your arm, from your shoulder through your hand, so a bent wrist, knuckles to the side or a hand overhead still cast where you reach. Leveled out like a wand (within about 35 degrees of flat), it casts from its top as before. The Warlock's skills keep the staff's own aim.";
        case LandingCircles:return "Shows where a ground skill will hit once the game accepts the cast, in its element's color: fire orange, cold blue, lightning yellow. The circle fills from the centre outward until the skill lands: a Fire Blast bomb over its flight, Meteor over its fall, Blizzard while the ice falls. Fire Wall shows as a strip across your aim. Floor only, never monsters' legs or walls. Greyed out while Ground skills is Game.";
        case GroundAim:return "Where ground skills land: Fire Blast and traps, Blizzard, Meteor, Fire Wall, Hydra, Fissure, Volcano, Bone Wall and Bone Prison. Aimed at the horizon or above, as far as the game lets a skill be cast.";
        case BowLaser:return "At full draw a faint line runs from the arrow's tip along the path a release would fly, ending in a brighter dot at the floor or fading out after 15 m. It doesn't stop at walls or monsters. Shown for basic arrows and Magic, Fire, Cold, Ice, Exploding, Immolation and Freezing Arrow; not Multiple Shot, Guided Arrow, Strafe or crossbows. Off by default, else its color in 15-degree steps from red 0; past 345 comes Off, both ways.";
        case ThrowLift:return "A thrown javelin, knife or axe leaves along your hand's motion, which tends to dip as you let go. This raises every throw above that path by the angle shown, 10 degrees by default, keeping its direction left or right. Minus values lower it; Off follows the hand exactly. Both hands, every throw and javelin skill, the Assassin's thrown traps and gas and oil potions.";
        case HeartbeatStart:return "The life percentage where the low-life heartbeat begins. Below it the heartbeat steps up every 10% of life you lose, louder and faster, until it is at full strength below 10%. 60% is the default; Off silences it. Healing winds it back down beat by beat; wiping the visor clean fades it until the next hit. Works in Unleashed and Classic.";
        case ShieldGrip:return "Slides a held shield along your forearm: + toward the elbow so your fingers reach its grip strap, - toward the hand. Shield follows sets how much it turns with your wrist. 0 by default (the original position when Shield follows is 100%); about 3 cm suits a buckler, large shields need more. The shield blocks where it is shown, so this moves its blocking surface too.";
        case ShieldOut:return "Moves a held shield away from your forearm (+) or into it (-), up to 30 cm either way. Use it with Grip offset and Up to seat the shield on your arm; it blocks where it is shown. 0 by default.";
        case ShieldUp:return "Moves a held shield across your forearm, square to Out, up to 30 cm either way. Use it with Grip offset and Out to seat the shield on your arm; it blocks where it is shown. 0 by default.";
        case ShieldFollow:return "How much a held shield turns with your wrist. 0% straps it to your forearm, so bending the wrist leaves it still; 100% turns it with your hand. Twisting the forearm turns it at every value. 50% by default, in 10% steps.";
        case ShieldRoll:return "Turns the shield around the forearm; + toward the little finger. 0 = the attach's own facing. -180 to +180 degrees in 5-degree steps; +90 suits a controller held palm down.";
        case EnemySight:return "Scales how far away hostile monsters notice and approach you in offline VR, from 100% (game default) up to 300% (three times as far). 100% keeps the game's original sight distance and restores the unmodified table values. Friendly NPCs, mercenaries and summons are unchanged.";
        case RunSpeed:return "Walk walks; every step above runs. Base run is your class's run with no speed bonus (gear, Faster Run/Walk or skills), then 10% steps of the way up to Full run (default), the full speed your gear and skills give. Never faster than the game. Movement skills keep their own speed. Stick toggle, beside it, switches with the right stick.";
        case RunWalkToggle:return "On: push the right stick up to switch between walking and Movement speed's run - from Walk, the full run. Not while a wheel is open or while you look at the wrist map. A new character, or a change of Movement speed, starts at Movement speed again. Off by default; right stick up then does nothing.";
        case LeftHandStrikes:return "Druid only. A melee skill on a trigger (Maul, Feral Rage, Fire Claws, Rabies, Hunger, Fury) casts when that hand's claw lands a full-cycle stroke; faster strokes stay basic attacks. Toggle it in the skill wheel's lower-left box. Weapon skill gestures Off keeps every strike basic.";
        case CompanionAi:return "Enhanced VR Mercenary overhaul: dynamic detection range matching enemy sight, elastic 40 sub-tile combat leash, and an anti-stuck watchdog that safely relocates the ally to your rear flank if trapped. Legacy restores vanilla AI constants and behavior.";
        case MeleeHitSpeed:return "Minimum weapon movement speed for a swing to count as a strike. Lower it for easier swings; raise it to require faster movement. Default: 12 m/s. Both melee modes use this value. Shield bash has its own threshold.";
        case ShieldBashSpeed:return "Minimum tracked hand speed for a physical shield bash, including assigned Smite. Default: 2 m/s. Move the hand at least 12 cm in one deliberate stroke; wrist twisting alone does not attack. The speed must still be met at contact. Applies in both melee modes; blocking, parries and trigger attacks are unchanged.";
        case MeleeThrustSpeed:return "Minimum forward speed for a stab along your weapon's tip. Default: 2 m/s. Move forward at least 12 cm, then pull back before stabbing again. This is separate from swing speed and applies in both melee modes.";
        case LootLabelSize:return "Scales dropped-item names. 45% restores their original size; higher values make them larger. The darker background stays. Hold Right Grip, then Right Trigger, to reveal nearby loot names; release either to restore normal labels.";
        case HapticStrength:return "Overall vibration strength for both controllers. 0% disables vibration; 100% is normal. This multiplies each category's strength, up to the controller's maximum. Different headsets can feel different at the same percentage.";
        case HapticMelee:return "Strength of physical weapon and shield impacts. Weapon tests vibrate the right controller; Shield tests vibrate the left. Tests use the values shown here before you Apply.";
        case HapticShortMelee:return "Off uses longer, more noticeable melee taps. On uses very brief 10 ms pulses at 180 Hz. Some controllers barely reproduce these short pulses, so leave this Off if impacts feel weak or disappear.";
        case HapticBowDraw:return "Strength of nocking, drawing and full-draw vibration cues. Its test previews the controllers used during a bow draw. Overall strength also affects the result.";
        case HapticBowRelease:return "Strength of feedback when releasing a physical arrow or throwing a javelin. Its test uses the release action's controllers. Overall strength also affects the result.";
        case HapticSpell:return "Strength of the brief vibration after a supported hand spell successfully casts. The casting hand vibrates once per cast or volley. Left and Right test each controller using the displayed values.";
        case HapticInteraction:return "Strength of feedback for interactions such as loot, potion handling and menu actions. Left and Right test each controller. Tests use your displayed values before Apply.";
        case HapticAlert:return "Strength of low-health and low-mana warning vibration on the left controller. Actual warnings also require Low health / mana alerts under HUD to be On. Test previews the vibration even when warnings are disabled.";
        case BloodVisor:return "Shows peripheral blood splatters as life falls below 60%, 40% and 20%. Healing back above each threshold fades that layer. Face wipe gesture lets you clear splatters by hand.";
        case BloodWipe:return "With blood on the visor, move either hand side to side in front of your face with that hand's trigger released. Successful wipes remove blood with vibration. This does not restore health.";
        case ElementalHands:return "A Sorceress's hands each show a small copy of their trigger skill's look, made from the spell's own effects in your game: flames, lightning flashes or ice, switching while you hold a spell shortcut. Supported hand spells flare it as they cast; the spells you cast are unchanged. Experimental: it is drawn over the picture, so it can show through your weapon or a wall. Other classes see nothing.";
        case BottlePhysics:return "A bottle you have drunk from falls from your open hand, bounces and rolls, and weapons, fists and the shield knock it about. A potion thrown at your mercenary flies as a real bottle and breaks where it first hits; a thrown trap, bomb, gas or oil potion lands where its arc first hits, stopping at walls and tables. Off: emptied bottles vanish and throws use a simple arc to the floor. Healing the mercenary works either way.";
        case TorchFlame:return "A one-handed club with fire damage is a torch: it lights the dark round you from its head. This shows or hides the flame drawn on it; the light stays either way.";
        case PropPhysics:return "The level's small things near you (cups, plates, vases, books, skulls, stools, barrels and more) stay put until a weapon, hand, shield or bottle reaches them. Then they fall and roll, and stay where they land until the area loads again. Purely visual. Shelves and tables are approximate: something pushed off a shelf's back can fall through it.";
        case HelpPerformance:return "The last second of headset frames. Drawing: the game preparing one eye (By turns) or both; it grows with what is in view, draw distances and shadows, hardly with resolution. Game: its update between frames. VR: the mod's own work. Headset wait: the runtime holding the frame for the next refresh. Past the headset's frame (11.1 ms at 90 Hz), late frames wait a whole refresh: up to half the frame rate.";
        case FrameGeneration:return "OFXR Bridge, an experimental OpenXR layer by tig3rmast3r, has the game draw every other frame and makes the rest from the motion between them. It needs Stereo rendering: Both eyes, and pauses by turns. Turning it on restarts the VR session, a moment of black. With Virtual Desktop, set its SSW to Disabled.";
        case FrameGenPreset:return "How carefully NVIDIA's optical flow looks for the motion between two frames. Medium is the suggested start; OFXR marks Fast as a test setting. Changes apply while you play.";
        case FrameGenScale:return "The size NVIDIA's optical flow works at, of each eye's image. 50% is the suggested start; 100% usually costs more than it gains. Changes apply while you play.";
        case HelpFrameGen:return "What OFXR Bridge is doing now: off, loading, active with its backend, paused by turns, bypassed or failed. While it runs the game draws half the headset's frames (the frame rate above) and it makes the rest. By turns each eye would get a quarter, so it waits for Both eyes. It needs the Microsoft Visual C++ x64 runtime.";
        case HelpGameVideo:return "The game's own graphics options, as it saved them (Options > Video, then close the menu). Shadows, character detail and lights add drawing work for the CPU; ambient occlusion (AO), atmospherics and scale cost the graphics card. Change them in the game's Options > Video; the numbers are the game's own, 0 its lowest.";
        case HelpApply:return "Saves your pending changes and applies them. Your other saved settings and calibration are preserved. Changes stay as a draft until you press Apply.";
        case HelpClose:return "Returns to the game and discards changes you have not applied. Right B also closes this panel. The world continues running while VR Settings is open.";
        case HelpRecenter:return "Resets the current eye-height reference for your play position. This is separate from measuring your body and does not replace your saved body calibration.";
        case HelpCalibrate:return "Guides you through measuring standing eye height and controller span in a T-pose. Apply or discard pending settings first. Hold both triggers when prompted; Right B cancels.";
        case HelpMonoAudio:return "Windows is set to Mono audio, which mixes every sound to the centre before it reaches the headset, so sounds cannot follow your head and stick turns. Turn it off in Windows Settings > Accessibility > Audio > Mono audio; the change applies at once.";
        default:break;
    }
    // Future settings retain at least their inline explanation until richer
    // help is added alongside them.
    if(id>=0&&id<Count)return Definitions[id].detail;
    return "";
}
// Rows with named choices: the one selected now and what it does, under the
// row's general help, so stepping through the choices explains each one.
struct ChoiceHelp {const char* name=nullptr;const char* text=nullptr;};
constexpr int HelpChoiceGap=18,HelpChoiceNameHeight=40;   // on the card: above the name, the name's line
inline int HelpChoices(int id) {
    switch(id) {
        case Foveation:case MenuDistance:case TooltipTiming:case ProjectileAimAssist:return 3;
        case FoveationStrength:case VitalsLocation:case NavigationLocation:return 4;
        case StereoRendering:return int(std::size(StereoRenderingNames));
        case FrameGeneration:case FrameGenPreset:case FrameGenScale:return 3;
        case RenderResolution:return int(std::size(resolution::Choices));
        case ControllerLabels:return int(controllers::Choice::Count);
        case MapTopDown:return 4;
        case Dominant:case AimHighlights:case CompanionAi:return 2;
        case GroundAim:return int(std::size(GroundAimNames));
        case LeftHandStrikes:return int(std::size(LeftHandStrikesNames));
        case SpellLock:return int(std::size(SpellLockNames));
        default:return 0;
    }
}
inline ChoiceHelp HelpChoice(int id,float value) {
    const int count=HelpChoices(id);
    if(!count||!std::isfinite(value))return {};
    const int c=std::clamp(int(std::lround(value)),0,count-1);
    switch(id) {
        case Foveation:{
            static constexpr const char* names[]={"Off","Fixed","Eye tracked"};
            static constexpr const char* text[]={
                "Every pixel is shaded at full quality. Choose this if the edges look coarse or your GPU has time to spare.",
                "Shading is reduced toward the edges of the lenses, where you see least sharply. The full-quality centre stays straight ahead.",
                "The full-quality area follows your gaze, on headsets that share eye tracking such as the Steam Frame. While no gaze samples arrive, the whole view uses full quality."};
            return {names[c],text[c]};}
        case FoveationStrength:{
            static constexpr const char* text[]={
                "A large full-quality centre; the edges are only lightly reduced. The smallest saving.",
                "The default balance between edge detail and GPU time.",
                "The same edge area as Medium, shaded more coarsely.",
                "The coarsest edges and a smaller full-quality centre. The largest saving, and the most visible."};
            return {FoveationStrengthNames[c],text[c]};}
        case StereoRendering:{
            static constexpr const char* text[]={
                "The game draws each frame twice, once per eye, from the same moment. Accurate 3D, and the most CPU and GPU work per frame.",
                "One eye per frame, alternating; the other eye's last image is turned to your head and moved with the scene's depth. About half the drawing work, but moving monsters can look doubled and the edges of nearby things may smear."};
            return {StereoRenderingNames[c],text[c]};}
        case FrameGeneration:{
            static constexpr const char* text[]={
                "No layer: the headset shows the frames the game draws. The default.",
                "AMD FidelityFX optical flow on any graphics card, the lightest. Dark areas can smear as you turn your head.",
                "The optical flow hardware of RTX and GTX 1660 cards (not GTX 1650 or GTX 10). Quality and scale: the row below."};
            return {framegen::ModeNames[c],text[c]};}
        case FrameGenPreset:{
            static constexpr const char* text[]={
                "The most careful search for motion, and the most GPU time.",
                "OFXR's suggested balance of quality and GPU time.",
                "The least GPU time; OFXR still calls it a test."};
            return {framegen::PresetNames[c],text[c]};}
        case FrameGenScale:{
            static constexpr const char* text[]={
                "Full size: the finest motion, but it usually costs more than it gains.",
                "Between full and half size.",
                "Half size: OFXR's suggested balance."};
            return {framegen::ScaleNames[c],text[c]};}
        case RenderResolution:{
            static constexpr const char* text[]={
                "Each eye uses the game window's size, so the monitor resolution sets the headset image.",
                "Each eye uses the size the VR runtime recommends for your headset, capped at 4096 pixels per side.",
                "Each eye is drawn at this fixed size, whatever your monitor or headset. Larger sizes are sharper and cost more GPU time."};
            return {resolution::Choices[c],text[std::min(c,2)]};}
        case ControllerLabels:{
            static constexpr const char* text[]={
                "Detects the active controllers and uses their button names.",
                "Uses Meta Quest Touch button names, whatever is detected.",
                "Uses Valve Index button names, whatever is detected.",
                "Uses Steam Frame button names, whatever is detected.",
                "Uses neutral names that fit any controller."};
            return {controllers::Choices[c],text[c]};}
        case ProjectileAimAssist:{
            static constexpr const char* text[]={
                "Spells fly exactly where your hand aims them.",
                "A launch aimed within 4 degrees of a monster is turned toward it.",
                "A launch within 12 degrees is turned to the monster's centre, and the spell gently homes in flight. Charged Bolt gets the launch correction only."};
            return {AimAssistNames[c],text[c]};}
        case VitalsLocation:{
            static constexpr const char* text[]={
                "Shown when you turn your left wrist toward your face, like checking a watch.",
                "Shown when you raise your left palm toward you with grip and trigger released.",
                "Either gesture shows it.",
                "Health and mana are not shown on your hand."};
            return {vitals::LocationNames[c],text[c]};}
        case NavigationLocation:{
            static constexpr const char* text[]={
                "Shown when you turn your left wrist toward your face, like checking a watch.",
                "Shown when you raise your left palm toward you with grip and trigger released.",
                "Either gesture shows it.",
                "The minimap and compass are not shown on your hand."};
            return {vitals::LocationNames[c],text[c]};}
        case MapTopDown:{
            static constexpr const char* text[]={
                "The game's own slanted map, with NPC and exit names.",
                "Rooms straightened into a floor plan. NPC and exit names are hidden so their text is not turned with the floor. Rotate map can keep your facing at the top.",
                "The game's full map, isometric, from its widest view. Look at it and push the right stick up to zoom in, up to 16x, staying sharp; down zooms back out.",
                "The game's full map as a floor plan, from its widest view, zoomed like Full map. Rotate map can keep your facing at the top."};
            return {MapViewNames[c],text[c]};}
        case MenuDistance:{
            static constexpr const char* text[]={
                "Panels appear 55 cm in front of you, the main menu 70 cm.",
                "Panels appear 85 cm in front of you, the main menu 1 m.",
                "Panels appear 1.15 m in front of you, the main menu 1.3 m."};
            return {MenuDistanceNames[c],text[c]};}
        case TooltipTiming:{
            static constexpr const char* text[]={
                "Item details appear as soon as the laser reaches an item.",
                "Item details appear after the laser rests on an item for 300 ms.",
                "Item details use the game's own timing."};
            return {TooltipTimingNames[c],text[c]};}
        case SpellLock:{
            static constexpr const char* text[]={
                "Spells and bow skills aim along the casting hand, as before.",
                "Spells go to the aimed monster; bow skills aim as before.",
                "Bow skills cast from a button go to the aimed monster, the bow hand's first; spells aim as before.",
                "Spells and bow skills go to the aimed monster."};
            return {SpellLockNames[c],text[c]};}
        case GroundAim:{
            static constexpr const char* text[]={
                "The game aims ground skills its own way, as before: Bone Wall and Prison where your hand points at the floor.",
                "On the floor where your casting hand points, held out or not, with a landing circle.",
                "On the monster your hands or eyes aim at (Bone Wall in front of it), else where you point, with a landing circle."};
            return {GroundAimNames[c],text[c]};}
        case AimHighlights:{
            static constexpr const char* text[]={
                "One outline: the monster your dominant hand (VR > Dominant hand) points at, else the one you look at.",
                "Each hand's monster has its own outline in its own color; with neither hand on one, the monster you look at."};
            return {HighlightsNames[c],text[c]};}
        case CompanionAi:{
            static constexpr const char* names[]={"Legacy","Enabled"};
            static constexpr const char* text[]={
                "Restores vanilla mercenary AI constants and original pathfinding behavior.",
                "Enables dynamic combat detection range scaling, elastic combat tether (40 sub-tiles), and anti-stuck watchdog repositioning to your rear flank."};
            return {names[c],text[c]};}
        case Dominant:{
            static constexpr const char* names[]={"Right","Left"};
            static constexpr const char* text[]={
                "Reach your right hand over either shoulder and grip to swap weapon sets.",
                "Reach your left hand over either shoulder and grip to swap weapon sets."};
            return {names[c],text[c]};}
        case LeftHandStrikes:{
            static constexpr const char* text[]={
                "The left claw casts the skill on the left trigger; a skill strikes cannot cast gives basic attacks.",
                "Both claws cast the right trigger's skill (a skill can sit on only one trigger). The claws share one attack cycle, so this is never more casts."};
            return {LeftHandStrikesNames[c],text[c]};}
        default:return {};
    }
}
inline int HelpAt(Page page,float u,float v,bool controllerMenu=false,const Values* values=nullptr,bool monoAudio=false,const Context* context=nullptr) {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<0||v>=1)return NoHelp;
    if(monoAudio&&MonoAudioBox.Contains(u*Width,v*Height))return HelpMonoAudio;
    // Do not explain controls hidden behind an open dropdown.
    if(page==Page::VR&&controllerMenu)return NoHelp;
    const float x=u*Width,y=v*Height;
    if(Apply.Contains(x,y))return HelpApply;
    if(Close.Contains(x,y))return HelpClose;
    if(Recenter.Contains(x,y))return HelpRecenter;
    if(Calibrate.Contains(x,y))return HelpCalibrate;
    if(page==Page::VR&&ControllerBox.Contains(x,y))return ControllerLabels;
    if(page==Page::Performance){
        if(DeferBox.Contains(x,y)&&(!values||DeferOffered(*values)))return DeferGpuWait;
        if(PerformanceBox.Contains(x,y))return HelpPerformance;
        if(GameVideoBox.Contains(x,y))return HelpGameVideo;
        if(FrameGenBox.Contains(x,y)&&(!values||(*values)[StereoRendering]<.5f))return HelpFrameGen;   // not drawn by turns
    }
    if(page==Page::HUD){
        if(Rect{40,181,570,52}.Contains(x,y))return MapTopDown;
        if(Rect{650,181,590,52}.Contains(x,y))return MapRotate;
    }
    if(page==Page::Controls) {
        if(Rect{40,666,1200,52}.Contains(x,y))return WeaponSkillIndicators;
        if(Rect{40,728,1200,52}.Contains(x,y))return PlanarSpellAim;
        if(Rect{40,790,1200,52}.Contains(x,y))return ProjectileAimAssist;
    }
    const RowList rows=Rows(page,values?*values:Defaults(),context?*context:Context{});
    for(int row=0;row<rows.count;++row)   // a paired row: its right half explains the right setting
        if(Rect{40,RowY+row*RowStep-4,1200,RowStep}.Contains(x,y))return Paired(rows[row])&&x>=PairX(1)-PairGap/2?PairOf(rows[row]):rows[row];
    return NoHelp;
}
class HoverHelp {
    int target=NoHelp;
    uint64_t since=0;
public:
    static constexpr uint64_t DelayMs=450;
    void Clear(){target=NoHelp;since=0;}
    int Step(int next,uint64_t now) {
        if(next==NoHelp){Clear();return NoHelp;}
        if(next!=target||now<since){target=next;since=now;}
        return now-since>=DelayMs?target:NoHelp;
    }
};
}
