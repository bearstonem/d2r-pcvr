#pragma once
#include "vr_settings.h"
#include <iterator>

namespace framevr::settings {
// Keep the existing panel's pixel and physical size. Help occupies an extra,
// transparent strip on its right in the same atlas/composition layer.
constexpr int HelpGap=24,HelpWidth=440,AtlasWidth=Width+HelpGap+HelpWidth;
constexpr Rect HelpCard{Width+HelpGap,242,HelpWidth-24,612};
constexpr int NoHelp=-1,HelpApply=Count,HelpClose=Count+1,HelpRecenter=Count+2,HelpCalibrate=Count+3,HelpMonoAudio=Count+4;
inline const char* HelpTitle(int id) {
    if(id>=0&&id<Count)return Definitions[id].label;
    switch(id){case HelpApply:return "Apply";case HelpClose:return "Close";
        case HelpRecenter:return "Reset eye height";case HelpCalibrate:return "Calibrate body";
        case HelpMonoAudio:return "Windows Mono audio";default:return "";}
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
        case DesktopOneEye:return "In stereo the game window alternates between both eyes, which looks doubled on the monitor. On copies the left eye back over each right frame after the headset has it, so the monitor shows one steady image. The headset view is the same either way. Off saves one full-frame GPU copy per frame.";
        case EyeHeight:return "Your standing eye height above the tracked floor. Body calibration measures this to set the world's scale. Use Calibrate body to measure it again.";
        case ArmSpan:return "The distance between your controller grips with your arms extended in a T-pose. Body calibration uses it to fit the character's arms to yours.";
        case BodyCalibration:return "Stores your measured standing height and controller span. Use Calibrate body to repeat the guided measurement; updating the mod preserves it.";
        case SkillGestures:return "Lets compatible assigned trigger skills power physical actions, such as Smite with a shield bash and arrow skills with bow release. The skill must be learned and available. Ordinary physical weapon swings remain separate.";
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
        case MapTopDown:return "The wrist minimap's view. The gold arrow shows your facing direction. Apply saves your choice.";
        case MapRotate:return "In Top-down view, Off keeps north at the top and turns the arrow with you. On rotates the map so your facing direction stays at the top. This has no effect in Isometric view; your choice is kept for the next time you use Top-down. Apply saves it.";
        case OrdinaryMobBars:return "Shows names and health bars for ordinary enemies. Turn it Off to reduce clutter. Boss and elite bars remain visible.";
        case MenuDistance:return "How far away eye-level game panels, such as inventory and merchant menus, appear. This does not move this VR Settings panel.";
        case TooltipTiming:return "How soon item details appear when you point the laser at an item in a game panel.";
        case MeleeRules:return "How your physical melee strokes deal damage.";
        case CompanionCounts:return "Shows companion counts and effects currently affecting your character below the left-hand minimap. This includes received buffs, auras and harmful effects, not just assigned skills. It follows Minimap & Compass placement.";
        case OfflinePlayers:return "Uses native offline Players 1-8 scaling for tougher monsters and increased experience and loot. Apply affects newly spawned monsters; existing monsters keep their current stats. For all monsters to use the new value, Save and Exit, then start a new game with the same character. Game default follows the game's own setting. Works with every class and either melee mode. Does not change Normal, Nightmare or Hell, or your desktop preference.";
        case StrainSpeed:return "How quickly blocked hits build the shield strain vignette in Unleashed. Strain starts fading a moment after hits stop, or as soon as you lower the shield; full strain is gone about 3 seconds after the last hit.";
        case StrainDark:return "The color of the edge effect as repeated blocks strain your guard in Unleashed. It clears when hits stop for a few seconds or you lower your shield.";
        case TargetOutlineColour:return "Static: every outline in the Static color below. By range: red near, orange mid-range, yellow far, so you can tell at a glance how close the monster you aim at is. Select the value to switch.";
        case TargetOutlineHue:return "The Static color, around the color wheel 15 degrees at a time; gold by default. A color unlike the monster's own reads best: red is hard to see on red monsters.";
        case TargetHighlight:return "A thin, steady outline round the monster you are aiming at. Off by default; the percentage sets how strongly it shows. Monster outline color chooses gold or a color by distance. While your arm is held out, the monster your hand points at is chosen; when your hand points at none, the monster you look at is. Only monsters in your line of sight count. The outline is a guide only: it does not change how attacks or spells aim.";
        case HeartbeatStart:return "The life percentage where the low-life heartbeat begins. Below it the heartbeat steps up every 10% of life you lose, louder and faster, until it is at full strength below 10%. 60% is the default; Off silences it. Healing winds it back down beat by beat; wiping the visor clean fades it until the next hit. Works in Unleashed and Classic.";
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
        case Foveation:case MenuDistance:case TooltipTiming:case StrainSpeed:case ProjectileAimAssist:return 3;
        case FoveationStrength:case VitalsLocation:case NavigationLocation:return 4;
        case StereoRendering:return int(std::size(StereoRenderingNames));
        case RenderResolution:return int(std::size(resolution::Choices));
        case ControllerLabels:return int(controllers::Choice::Count);
        case Dominant:case MeleeRules:case StrainDark:case MapTopDown:return 2;
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
                "One eye per frame, alternating; the other eye's last image is turned to your head and moved with the scene's depth. About half the drawing work, but moving monsters can look doubled and the edges of nearby things may smear.",
                "The game draws each frame once, for the left eye, and the graphics card draws it again for the right eye: both eyes from one moment for about one eye's CPU work. Starts a few seconds into an area and pauses while an area loads."};
            return {StereoRenderingNames[c],text[c]};}
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
                "Rooms straightened into a floor plan. NPC and exit names are hidden so their text is not turned with the floor. Rotate map can keep your facing at the top."};
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
        case Dominant:{
            static constexpr const char* names[]={"Right","Left"};
            static constexpr const char* text[]={
                "Reach your right hand over either shoulder and grip to swap weapon sets.",
                "Reach your left hand over either shoulder and grip to swap weapon sets."};
            return {names[c],text[c]};}
        case MeleeRules:{
            static constexpr const char* text[]={
                "Each deliberate stroke hits every enemy it reaches, once. Damage = normal damage x hit chance (70% means 70% damage, never a miss) x recovery (25% at half an attack cycle since your last landed stroke, 100% at a full one) x cleave (100%, 75%, 50%, then 25% per target). Ripostes skip these. Adds shield blocks and parries.",
                "The game's own rules: each stroke rolls to hit, attack speed limits how often you can hit, and each stroke hits one target."};
            return {MeleeRulesNames[c],text[c]};}
        case StrainSpeed:{
            static constexpr const char* text[]={
                "Half the strain per block: about 20 blocks to full strain without lowering your shield.",
                "The original rate: about 10 blocks to full strain.",
                "Twice the strain per block: about 5 blocks to full strain."};
            return {StrainSpeedNames[c],text[c]};}
        case StrainDark:{
            static constexpr const char* text[]={
                "A pale glow at the edges of your view.",
                "A dark vignette at the edges of your view."};
            return {StrainColorNames[c],text[c]};}
        default:return {};
    }
}
inline int HelpAt(Page page,float u,float v,bool controllerMenu=false,const Values* values=nullptr,bool monoAudio=false) {
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
    if(page==Page::Video&&VideoDeferBox.Contains(x,y)&&(!values||DeferOffered(*values)))return DeferGpuWait;
    if(page==Page::HUD){
        if(Rect{40,181,570,52}.Contains(x,y))return MapTopDown;
        if(Rect{650,181,590,52}.Contains(x,y))return MapRotate;
    }
    if(page==Page::Controls) {
        if(Rect{40,666,1200,52}.Contains(x,y))return WeaponSkillIndicators;
        if(Rect{40,728,1200,52}.Contains(x,y))return PlanarSpellAim;
        if(Rect{40,790,1200,52}.Contains(x,y))return ProjectileAimAssist;
    }
    for(int row=0;row<RowCount(page);++row)
        if(Rect{40,RowY+row*RowStep-4,1200,RowStep}.Contains(x,y))return RowKey(page,row);
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
