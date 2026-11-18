#pragma once
#include <array>
#include <string_view>
namespace aurarecipient {
inline bool ShoutAsset(std::string_view path){
    constexpr std::array paths={
        std::string_view{"data/hd/vfx/particles/overlays/common/shout/vfx_shout_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/shoutstart/vfx_shout_start.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/battleorders/vfx_battle_orders.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/battleorderscast/vfx_battle_orders_start.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/battlecommand/vfx_battle_command_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/battlecommandcast/vfx_battle_command_start.particles"},
    };
    for(auto candidate:paths){
        if(path.size()!=candidate.size())continue;
        bool same=true;
        for(size_t i=0;i<path.size();++i){char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';if(c!=candidate[i]){same=false;break;}}
        if(same)return true;
    }
    return false;
}
// Persistent aura dependencies from native OverlayDefinition metadata.
// Excludes charge, cast/hit/target overlays, curses, and unrelated hand/weapon FX.
inline bool AuraAsset(std::string_view path){
    constexpr std::array paths={
        std::string_view{"data/hd/vfx/particles/overlays/common/heart_of_wolverine/vfx_heartofwolverine_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/oak_sage/vfx_oaksage_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/common/spirit_of_barbs/vfx_spiritofbarbs_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_defiance_front/fx_defiance_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_fanatic/aura_fanatic.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_holyfire_front/fx_holyfire_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_might_back/fx_might_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_prayer_back/fx_prayer.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_resistall_front/fx_resistall_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_resistcold/fx_resistcold_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_resistfire/fx_resistfire_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_resistlight/fx_resistlight.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/aura_thorns_front/fx_thorns_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/blessedaimback/fx_blessedaimoverlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/cleansingback/fx_cleansing_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/concentrationback/fx_concentration_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/convictionfront/fx_conviction_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/fanaticismfront/fx_fanatacism_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/holyfreeze/fx_holyfreeze_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/holyshockfront/holyshock_frontoverlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/meditationfront/fx_meditation_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/redemptionback/fx_redemption_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/sanctuaryback/fx_sanctuary_overlay.particles"},
        std::string_view{"data/hd/vfx/particles/overlays/paladin/staminafront/fx_stamina_overlay.particles"},
    };
    for(auto candidate:paths){
        if(path.size()!=candidate.size())continue;
        bool same=true;
        for(size_t i=0;i<path.size();++i){
            char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
            if(c!=candidate[i]){same=false;break;}
        }
        if(same)return true;
    }
    return false;
}
}
