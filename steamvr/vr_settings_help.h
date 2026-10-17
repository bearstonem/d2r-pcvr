#pragma once
#include "vr_settings.h"

namespace framevr::settings {
// Keep the existing panel's pixel and physical size. Help occupies an extra,
// transparent strip on its right in the same atlas/composition layer.
constexpr int HelpGap=24,HelpWidth=440,AtlasWidth=Width+HelpGap+HelpWidth;
constexpr Rect HelpCard{Width+HelpGap,242,HelpWidth-24,612};
constexpr int NoHelp=-1,HelpApply=Count,HelpClose=Count+1,HelpRecenter=Count+2,HelpCalibrate=Count+3;
inline const char* HelpTitle(int id) {
    if(id>=0&&id<Count)return Definitions[id].label;
    switch(id){case HelpApply:return "Apply";case HelpClose:return "Close";
        case HelpRecenter:return "Reset eye height";case HelpCalibrate:return "Calibrate body";default:return "";}
}
inline const char* HelpText(int id) {
    switch(id) {
        case Motion:return "Enables tracked weapon swings, bow drawing, throwing and belt interactions. Keep this On to use physical combat. A selected trigger skill does not replace your free weapon swings.";
        case Seated:return "Raises the potion belt for easier reach while seated. Turn this Off to place the belt using Belt drop below eyes. Body calibration still uses your standing measurements.";
        case Dominant:return "Chooses which controller's grip activates the over-shoulder weapon swap. Reach that hand over either shoulder. This does not swap the game's weapon and shield hands.";
        case SmoothTurn:return "On turns continuously with the right stick at Smooth turn speed. Off turns in steps using Snap turn angle. Choose whichever feels more comfortable.";
        case Snap:return "How far each right-stick flick turns you when Smooth turning is Off. Smaller angles give finer steps. Release the stick between snap turns.";
        case TurnSpeed:return "How quickly the right stick rotates your view when Smooth turning is On, in degrees per second. Lower values turn more slowly. This does not affect snap turns.";
        case Waist:return "Distance from your eyes down to the potion belt in standing mode. Lower this value to raise the belt. Seated belt position uses its own raised placement.";
        case BeltForward:return "Moves the potion belt forward from your estimated torso. Increase it if the belt is too close to your body; decrease it if you have to reach too far.";
        case Foveation:return "Off shades the full view at normal quality. Fixed reduces shading toward the edges. Eye-tracked follows your gaze when supported; missing gaze samples use full quality. It can improve performance at the cost of peripheral detail.";
        case Sky:return "Shows the mod's added sky outside. This changes the background above the level; it does not increase terrain or object draw distance.";
        case Fog:return "Adds distance fog to outdoor scenery. Turning it Off can make distant scenery clearer. Indoor added fog is disabled by default to avoid artifacts through transparent effects.";
        case Ceiling:return "Shows the mod's added overhead surfaces in supported dungeon areas. These are separate from the game's building roofs.";
        case DrawDistance:return "Controls how many surrounding map tiles remain drawn. Higher values reveal more terrain but use more rendering resources. Lower this first if large outdoor areas run poorly.";
        case ObjectDistance:return "How far away scenery objects remain visible. Higher values retain more distant buildings and props, with a performance and memory cost. Lower values can help crowded areas such as Act III.";
        case RenderResolution:return "Sets the image size rendered for each eye independently of your monitor. Higher resolution is sharper but costs GPU time. Headset uses the runtime recommendation, capped at 4096 per side. Apply rebuilds the VR view.";
        case EyeHeight:return "Your standing eye height above the tracked floor. Body calibration measures this to set the world's scale. Use Calibrate body to measure it again.";
        case ArmSpan:return "The distance between your controller grips with your arms extended in a T-pose. Body calibration uses it to fit the character's arms to yours.";
        case BodyCalibration:return "Stores your measured standing height and controller span. Use Calibrate body to repeat the guided measurement; updating the mod preserves it.";
        case SkillGestures:return "Lets compatible assigned trigger skills power physical actions, such as Smite with a shield bash and arrow skills with bow release. The skill must be learned and available. Ordinary physical weapon swings remain separate.";
        case WristMap:return "Shows the minimap on your left hand. Minimap & Compass controls its placement. The wheel's MAP tab remains available.";
        case WristCompass:return "Shows compass bearings on your left hand. North follows the top of the minimap. Minimap & Compass controls its placement.";
        case ControllerLabels:return "Automatic detects the active controllers and uses their button names in help and menus. Choose an override if the names are wrong. This changes labels, not button bindings.";
        case WeaponSkillIndicators:return "Shows the assigned skill's icon and name beside each weapon. Turn this Off for a clearer view. This describes the assigned skill, not buffs currently affecting your character.";
        case PlanarSpellAim:return "Off lets supported straight hand spells aim up or down. On keeps those shots level with the ground. Ground-based spells such as Charged Bolt keep their native path.";
        case ProjectileAimAssist:return "For supported hand spells: Off uses your aim, Light assists the launch, and Strong also adds gentle homing. Charged Bolt receives launch assistance only. This setting does not change physical bow or crossbow aiming.";
        case VitalsLocation:return "Chooses where health and mana appear on your left hand. Palm appears when you raise your palm toward you with grip and trigger released. Either allows both gestures. Off hides this display.";
        case VitalsTilt:return "Rotates the health and mana display relative to your hand. Positive values tilt its top toward you; negative values tilt it away. The minimap and compass keep their own angle.";
        case CriticalAlerts:return "Shows a brief warning and vibrates when health or mana is low. Adjust its vibration under Haptics > Resource alerts. Turning this Off disables the warning, including its vibration.";
        case NavigationLocation:return "Chooses Wrist, Palm, Either or Off for the left-hand minimap and compass. Palm requires your palm facing you with grip and trigger released. This placement is independent of health and mana.";
        case OrdinaryMobBars:return "Shows names and health bars for ordinary enemies. Turn it Off to reduce clutter. Boss and elite bars remain visible.";
        case MenuDistance:return "Moves eye-level game panels, such as inventory and merchant menus, nearer or farther away. Choose the distance that is easiest to read and point at. This does not move this VR Settings panel.";
        case TooltipTiming:return "Sets how soon item details appear when you point the laser at an item in a game panel. Instant has no added delay; Short waits 300 ms; Original uses the game's timing. VR Settings help has its own short hover delay.";
        case MeleeRules:return "Unleashed: each deliberate stroke hits every enemy it reaches once. Default swing damage = normal damage x hit chance x recovery x cleave. 70% hit chance means 70% damage, not a miss. Recovery uses attack speed: half a cycle since your last landed stroke gives 25%; a full cycle gives 100%. Cleave per target: 100%, 75%, 50%, then 25%. Ripostes skip these reductions. Adds physical shield blocks and parries. Classic uses hit rolls, attack-speed limits and one target per stroke.";
        case CompanionCounts:return "Shows companion counts and effects currently affecting your character below the left-hand minimap. This includes received buffs, auras and harmful effects, not just assigned skills. It follows Minimap & Compass placement.";
        case ParryWindow:return "In Unleashed, swing your shield diagonally from lower right to upper left just before a melee hit. This sets the timing window in milliseconds. A successful parry staggers the attacker and opens a riposte. 0 disables perfect parries.";
        case StrainDark:return "Chooses a pale or dark edge effect as repeated blocks strain your guard in Unleashed. Lower your shield to let strain clear. This changes the indicator's color, not blocking strength.";
        case MeleeHitSpeed:return "Minimum weapon movement speed for a swing to count as a strike. Lower it for easier swings; raise it to require faster movement. Default: 12 m/s. Both melee modes use this value. Shield bash has its own threshold.";
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
        default:break;
    }
    // Future settings retain at least their inline explanation until richer
    // help is added alongside them.
    if(id>=0&&id<Count)return Definitions[id].detail;
    return "";
}
inline int HelpAt(Page page,float u,float v,bool controllerMenu=false) {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<0||v>=1)return NoHelp;
    // Do not explain controls hidden behind an open dropdown.
    if(page==Page::VR&&controllerMenu)return NoHelp;
    const float x=u*Width,y=v*Height;
    if(Apply.Contains(x,y))return HelpApply;
    if(Close.Contains(x,y))return HelpClose;
    if(Recenter.Contains(x,y))return HelpRecenter;
    if(Calibrate.Contains(x,y))return HelpCalibrate;
    if(page==Page::VR&&ControllerBox.Contains(x,y))return ControllerLabels;
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
