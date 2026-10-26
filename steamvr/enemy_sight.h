#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace framevr::enemysight {

inline constexpr size_t RowSize=508;
inline constexpr size_t MinProbeRows=243;
inline constexpr int DefaultPercent=100;
inline constexpr int MinPercent=100;
inline constexpr int MaxPercent=300;
inline constexpr int StepPercent=10;
// In D2R's MonStats table, aidist == 0 means "use the engine's default 35 sub-tile
// AI activation radius". Scaling writes an explicit sub-tile radius in [2, 250]
// and restores the exact baseline bytes (including 0) when returning to 100%.
inline constexpr int DefaultAiDist=35;
inline constexpr int MinAiDist=2;
inline constexpr int MaxAiDist=250;
// Engine target-acquisition and line-of-sight ceilings in D2Game AiUtil/AiThink:
// - sub_6FCF2CC0 (ranged LOS check used at the top of CorruptArcher, etc.) returns
//   nullptr when nDistance > 48; since sub_6FCF2110 checks nDistance < aidist,
//   capping CorruptArcher aidist at 49 wakes her at nDistance <= 48 so she never
//   enters the !pTarget idle/circle daze at 49..54 sub-tiles.
// - AITHINK_Fn059_BloodRaven idles immediately when nTargetDistance > 45 (aidist <= 46).
inline constexpr int MaxLosRange=48;
inline constexpr int MaxLosAiDist=49;
inline constexpr int MaxBloodRavenAiDist=46;

inline constexpr size_t HcIdxOffset=0x00;
inline constexpr size_t Flags1Offset=0x3d;   // 0x01 npc, 0x04 inTown, 0x80 killable
inline constexpr size_t Flags3Offset=0x3f;   // 0x02 enabled
inline constexpr size_t AiOffset=0x52;       // uint16_t index into MonAi
inline constexpr size_t AlignOffset=0x87;    // 0 hostile, 1 friendly/ally, 2 neutral
inline constexpr size_t ThreatOffset=0x89;
inline constexpr size_t AiDelOffset=0x8a;    // uint8_t[3] Normal / Nightmare / Hell
inline constexpr size_t AiDistOffset=0x8d;   // uint8_t[3] Normal / Nightmare / Hell
inline constexpr size_t AipOffset=0x90;      // int16_t[8][3] Normal / Nightmare / Hell

inline int NormalizePercent(float value) {
    if(!std::isfinite(value))return DefaultPercent;
    const float stepped=std::round(value/float(StepPercent))*float(StepPercent);
    return int(std::clamp(stepped,float(MinPercent),float(MaxPercent)));
}

// 1-based aip index (1..8) for monster AI archetypes whose state machine in
// AiThink.cpp gates waking, approaching or attacking behind an explicit distance
// parameter. Note that monai.txt mislabels Fallen's aip2 as wander %, whereas
// AITHINK_Fn006_Fallen uses aip2 as FALLEN_AI_PARAM_APPROACH_DISTANCE.
inline constexpr int AwarenessAipIndex(uint16_t ai) {
    switch(ai) {
        case 3:   return 2; // Zombie: ZOMBIE_AI_PARAM_AWARE_DISTANCE (aip2)
        case 6:   return 2; // Fallen: FALLEN_AI_PARAM_APPROACH_DISTANCE (aip2)
        case 14:  return 1; // QuillRat: QUILLRAT_AI_PARAM_ACTIVATE_DISTANCE (aip1)
        case 21:  return 1; // Mummy: MUMMY_AI_PARAM_AWAKE_DISTANCE (aip1)
        case 28:  return 3; // Vampire: VAMPIRE_AI_PARAM_ACTIVE_DISTANCE (aip3)
        case 46:  return 2; // ElementalBeast: ELEMENTALBEAST_AI_PARAM_ACTIVATION_DISTANCE (aip2)
        case 52:  return 8; // FrogDemon: FROGDEMON_AI_PARAM_EMERGE_DISTANCE (aip8)
        case 56:  return 6; // Tentacle: TENTACLE_AI_PARAM_ACTIVE_DISTANCE (aip6)
        case 57:  return 6; // TentacleHead: TENTACLE_AI_PARAM_ACTIVE_DISTANCE (aip6)
        case 63:  return 1; // GargoyleTrap: GORGOYLETRAP_AI_PARAM_SHOOT_DISTANCE (aip1)
        case 70:  return 7; // FingerMage: FINGERMAGE_AI_PARAM_OUT_OF_RANGE (aip7)
        case 73:  return 8; // AbyssKnight: ABYSSKNIGHT_AI_PARAM_ACTIVE_DISTANCE (aip8)
        case 77:  return 1; // Trap-Missile: TRAPMISSILE_AI_PARAM_DISTANCE (aip1)
        case 80:  return 1; // Trap-Poison: TRAPNOVA_AI_PARAM_DISTANCE (aip1)
        case 82:  return 2; // InvisoSpawner: SPAWNER_AI_PARAM_ACTIVATION_DISTANCE (aip2)
        case 83:  return 2; // MosquitoNest: SPAWNER_AI_PARAM_ACTIVATION_DISTANCE (aip2)
        case 85:  return 8; // HighPriest: HIGHPRIEST_AI_PARAM_RANGE (aip8)
        case 92:  return 1; // Trap-Nova: TRAPNOVA_AI_PARAM_DISTANCE (aip1)
        case 94:  return 4; // DesertTurret: DESERTTURRET_AI_PARAM_RANGE (aip4)
        case 95:  return 6; // PantherJavelin: PANTHERJAVELIN_AI_PARAM_THROW_DISTANCE (aip6)
        case 96:  return 1; // FetishBlowgun: FETISHBLOWGUN_AI_PARAM_STAY_IN_RANGE (aip1)
        case 118: return 4; // Succubus: SUCCUBUS_AI_PARAM_CURSE_RANGE (aip4)
        case 121: return 4; // MinionSpawner: MINIONSPAWNER_AI_PARAM_ACTIVATION_RANGE (aip4)
        case 130: return 3; // DeathMauler: DEATHMAULER_AI_PARAM_SHOOT_RANGE (aip3)
        case 142: return 7; // ClawViperEx: CLAWVIPEREX_AI_PARAM_SHOOT_DISTANCE (aip7)
        default:  return 0;
    }
}

// True when the archetype's awareness aip acts as an approach/wake gate that in
// vanilla MonStats is smaller than aidist (e.g. Fallen aip2=10, Zombie aip2=10,
// Mummy aip1=5 vs aidist=35). When sight distance is scaled above 100%, the gate
// must cover at least the scaled aidist (+1 for strict '<' checks) so awakened
// monsters actively pursue the player instead of wandering in place.
inline constexpr bool GateCoversAiDist(uint16_t ai) {
    switch(ai) {
        case 3:   // Zombie
        case 6:   // Fallen
        case 14:  // QuillRat
        case 21:  // Mummy
        case 28:  // Vampire
        case 46:  // ElementalBeast
        case 52:  // FrogDemon
        case 56:  // Tentacle
        case 57:  // TentacleHead
        case 70:  // FingerMage
        case 73:  // AbyssKnight
        case 82:  // InvisoSpawner
        case 83:  // MosquitoNest
        case 85:  // HighPriest
        case 94:  // DesertTurret
        case 121: // MinionSpawner
            return true;
        default:
            return false;
    }
}

// True for archetypes that call sub_6FCF2CC0 (or sub_6FCF1B30 / sub_6FCF1BD0),
// which strictly rejects targets when nDistance > 48 and returns nullptr. Capping
// aidist at 49 (nDistance < 49 -> nDistance <= 48) ensures awakened monsters always
// acquire a valid target and never fall into dead-zone idle or broken walk states.
inline constexpr bool RequiresLosAiDist(uint16_t ai) {
    switch(ai) {
        case 4:   // Bighead
        case 13:  // FallenShaman
        case 15:  // SandMaggot
        case 22:  // GreaterMummy
        case 28:  // Vampire
        case 35:  // CorruptArcher
        case 37:  // SkeletonBow
        case 53:  // Summoner
        case 57:  // TentacleHead
        case 64:  // SkeletonMage
        case 74:  // OblivionKnight
        case 95:  // PantherJavelin
        case 96:  // FetishBlowgun
        case 118: // Succubus
        case 119: // SuccubusWitch
        case 122: // Imp
            return true;
        default:
            return false;
    }
}

inline uint16_t ReadU16(const uint8_t* p) {
    uint16_t v=0;
    std::memcpy(&v,p,sizeof v);
    return v;
}

inline int16_t ReadI16(const uint8_t* p) {
    int16_t v=0;
    std::memcpy(&v,p,sizeof v);
    return v;
}

inline void WriteI16(uint8_t* p,int16_t v) {
    std::memcpy(p,&v,sizeof v);
}

inline bool HostileMonsterRow(std::span<const uint8_t> row) {
    if(row.size()!=RowSize)return false;
    const bool enabled=(row[Flags3Offset]&0x02u)!=0;
    const bool hostile=row[AlignOffset]==0;
    const bool killableAndWild=(row[Flags1Offset]&0x85u)==0x80u; // killable && !npc && !inTown
    return enabled&&hostile&&killableAndWild;
}

// Verify compiled MonStats row stride and immutable fields on three canonical
// rows (skeleton1, zombie1, mephisto) shared across Classic, LoD and RotW.
inline bool ValidTableLayout(std::span<const uint8_t> table,size_t rowCount) {
    if(rowCount<MinProbeRows||table.size()!=rowCount*RowSize)return false;
    struct Probe {
        uint16_t row;
        uint16_t ai;
        uint8_t align;
        uint8_t threat;
        uint8_t aidel[3];
    };
    static constexpr Probe probes[]={
        {0,   2,  0, 10, {15,14,13}}, // skeleton1
        {5,   3,  0, 10, {15,14,13}}, // zombie1
        {242, 50, 0, 14, {15,10,6}},  // mephisto
    };
    for(const auto& p:probes) {
        const uint8_t* r=table.data()+size_t(p.row)*RowSize;
        if(ReadU16(r+HcIdxOffset)!=p.row)return false;
        if(ReadU16(r+AiOffset)!=p.ai)return false;
        if(r[AlignOffset]!=p.align||r[ThreatOffset]!=p.threat)return false;
        if(std::memcmp(r+AiDelOffset,p.aidel,3)!=0)return false;
    }
    return true;
}

inline uint8_t ScaleAiDistForArchetype(uint8_t baseDist,int percent,uint16_t ai) {
    const int pct=NormalizePercent(float(percent));
    if(pct==DefaultPercent)return baseDist;
    const int effective=baseDist>0?int(baseDist):DefaultAiDist;
    const int scaled=int(std::lround(float(effective)*(float(pct)/100.0f)));
    int maxDist=MaxAiDist;
    if(ai==59)maxDist=MaxBloodRavenAiDist;              // BloodRaven: idles when nTargetDistance > 45
    else if(RequiresLosAiDist(ai))maxDist=MaxLosAiDist; // sub_6FCF2CC0 requires nDistance <= 48
    return uint8_t(std::clamp(scaled,MinAiDist,maxDist));
}

inline uint8_t ScaleAiDist(uint8_t baseDist,int percent) {
    return ScaleAiDistForArchetype(baseDist,percent,0);
}

inline int16_t ScaleAwarenessAip(int16_t baseAip,int percent,int maxVal=MaxAiDist) {
    const int pct=NormalizePercent(float(percent));
    if(pct==DefaultPercent||baseAip<=0)return baseAip;
    const int scaled=int(std::lround(float(baseAip)*(float(pct)/100.0f)));
    return int16_t(std::clamp(scaled,MinAiDist,maxVal));
}

inline int16_t ScaleGateAip(int16_t baseAip,int percent,uint8_t scaledAiDist) {
    const int pct=NormalizePercent(float(percent));
    if(pct==DefaultPercent||baseAip<=0)return baseAip;
    const int prop=int(ScaleAwarenessAip(baseAip,pct));
    const int floorDist=int(scaledAiDist)+1;
    return int16_t(std::clamp(std::max(prop,floorDist),MinAiDist,MaxAiDist));
}

struct RowBaseline {
    uint8_t aidist[3]{};
    int16_t aip[8][3]{};
    uint16_t ai=0;
    bool hostile=false;
};

inline bool CaptureBaseline(std::span<const uint8_t> table,size_t rowCount,std::vector<RowBaseline>& out) {
    if(!ValidTableLayout(table,rowCount)){out.clear();return false;}
    out.resize(rowCount);
    for(size_t i=0;i<rowCount;++i) {
        const uint8_t* r=table.data()+i*RowSize;
        RowBaseline b{};
        std::memcpy(b.aidist,r+AiDistOffset,3);
        b.ai=ReadU16(r+AiOffset);
        b.hostile=HostileMonsterRow({r,RowSize});
        for(int k=0;k<8;++k) {
            const size_t off=AipOffset+size_t(k)*6;
            for(int d=0;d<3;++d)b.aip[k][d]=ReadI16(r+off+size_t(d)*2);
        }
        out[i]=b;
    }
    return true;
}

inline void WriteAipTriplet(uint8_t* row,int aipIndex1Based,const int16_t vals[3]) {
    const size_t off=AipOffset+size_t(aipIndex1Based-1)*6;
    for(int d=0;d<3;++d)WriteI16(row+off+size_t(d)*2,vals[d]);
}

inline void ComputeRowAiFields(const RowBaseline& b,int percent,uint8_t outAiDist[3],int16_t outAip[8][3]) {
    const int pct=NormalizePercent(float(percent));
    for(int d=0;d<3;++d)outAiDist[d]=ScaleAiDistForArchetype(b.aidist[d],pct,b.ai);
    std::memcpy(outAip,b.aip,sizeof(b.aip));
    if(pct==DefaultPercent)return;
    if(const int aipIdx=AwarenessAipIndex(b.ai);aipIdx>0) {
        const bool gateCovers=GateCoversAiDist(b.ai);
        const int maxRange=(b.ai==74||b.ai==95||b.ai==96||b.ai==118)?MaxLosRange:MaxAiDist;
        for(int d=0;d<3;++d) {
            outAip[aipIdx-1][d]=gateCovers?ScaleGateAip(b.aip[aipIdx-1][d],pct,outAiDist[d])
                                          :ScaleAwarenessAip(b.aip[aipIdx-1][d],pct,maxRange);
        }
    }
    if(b.ai==13) {
        // FallenShaman: scale both resurrect range (aip4) and fireball shoot range (aip5).
        for(int d=0;d<3;++d) {
            outAip[3][d]=ScaleAwarenessAip(b.aip[3][d],pct);
            outAip[4][d]=ScaleAwarenessAip(b.aip[4][d],pct,MaxLosRange);
        }
    } else if(b.ai==35) {
        // CorruptArcher (Rogue archers):
        // - aip8 (WALK_TOW_DISTANCE, base 12..16) triggers BEFORE aip5 (ALWAYS_RUN_DISTANCE)
        //   in AITHINK_Fn035_CorruptArcher and locks archers into a +10% velocity slow walk
        //   all the way from 48 down to 11 sub-tiles without shooting. Setting aip8 = 0
        //   disables the slow-walk hijack.
        // - aip5 (ALWAYS_RUN_DISTANCE, base 20..24) is passed to PATH_SetStepNum, which
        //   rejects values >= PATH_MAX_STEPNUM (20) and resets nStepNum = 0. Setting
        //   aip5 = 17 (< 20 -> nStepNum = 16, within cr_arrow1..5 range) makes Rogue archers
        //   run at +100% velocity to 16 sub-tiles, stop, and immediately fire arrows.
        for(int d=0;d<3;++d) {
            outAip[4][d]=17;
            outAip[7][d]=0;
        }
    } else if(b.ai==37) {
        // SkeletonBow (Skeleton archers / Arquero Retornado):
        // - AITHINK_Fn037_SkeletonBow only shoots at nDistance < 20 and otherwise rolls
        //   aip3 (APPROACH_CHANCE_PCT, base 50% -> 50% chance to freeze in neutral for 20 frames)
        //   and walks aip4 steps toward aip5.
        // - Setting aip1 = 100 (SHOOT_CHANCE_PCT) fires immediately when in range without stalling.
        // - Setting aip2 = 4 (STALL_DURATION) reduces recovery freeze after shooting.
        // - Setting aip3 = 100 eliminates the 50% 20-frame neutral freeze so archers advance continuously.
        // - Setting aip4 = 6 ensures step vector length never exceeds nDistMax (14 in Path.cpp),
        //   preventing D2GAME_ModeChange_6FC65220 pathfinding rejections that freeze the monster.
        // - Setting aip5 = 16 guides archers smoothly into their < 20 bow firing range.
        for(int d=0;d<3;++d) {
            outAip[0][d]=100;
            outAip[1][d]=4;
            outAip[2][d]=100;
            outAip[3][d]=6;
            outAip[4][d]=16;
        }
    } else if(b.ai==64) {
        // SkeletonMage (Skeleton mages / Mago Retornado):
        // - In AITHINK_Fn064_SkeletonMage, if nDistance > aip2 (APPROACH_DISTANCE, base 9)
        //   and Roll < aip3 (APPROACH_CHANCE_PCT, base 30%), the mage enters a +10% velocity
        //   slow walk that blocks shooting until distance <= 9.
        // - Setting aip3 = 0 disables this slow-walk hijack.
        // - aip6 (FIRE_DIST, base 18) is scaled up to MaxLosRange (48) so mages can fire spells
        //   across the entire sight range. Since aidist is capped at MaxLosAiDist (49, dist <= 48),
        //   awakened mages are immediately in firing range.
        // - Setting aip1 = 100 (SHOOT_CHANCE_PCT) casts elemental bolts with 100% priority.
        // - Setting aip2 = 14 (APPROACH_DISTANCE), aip7 = 0 (no circle), aip8 = 3 (low stall).
        for(int d=0;d<3;++d) {
            outAip[0][d]=100;
            outAip[1][d]=14;
            outAip[2][d]=0;
            outAip[5][d]=ScaleAwarenessAip(b.aip[5][d],pct,MaxLosRange);
            outAip[6][d]=0;
            outAip[7][d]=3;
        }
    } else if(b.ai==70) {
        // FingerMage: scale cast range (aip5) alongside out-of-range (aip7).
        for(int d=0;d<3;++d) {
            outAip[4][d]=ScaleAwarenessAip(b.aip[4][d],pct,MaxLosRange);
        }
    } else if(b.ai==74) {
        // OblivionKnight: scale engage range (aip2) up to MaxLosRange so curses/Bone Spirit engage from distance.
        for(int d=0;d<3;++d) {
            outAip[1][d]=ScaleAwarenessAip(b.aip[1][d],pct,MaxLosRange);
        }
    } else if(b.ai==95) {
        // PantherJavelin: set throw chance to 100% so slingers attack eagerly once in throw range.
        for(int d=0;d<3;++d) {
            outAip[0][d]=100;
        }
    }
}

inline size_t ApplySightPercent(std::span<uint8_t> table,std::span<const RowBaseline> baseline,int percent) {
    if(baseline.size()<MinProbeRows||table.size()!=baseline.size()*RowSize)return 0;
    const int pct=NormalizePercent(float(percent));
    size_t updated=0;
    for(size_t i=0;i<baseline.size();++i) {
        const auto& b=baseline[i];
        if(!b.hostile)continue;
        uint8_t* r=table.data()+i*RowSize;
        uint8_t scaledDist[3]{};
        int16_t scaledAip[8][3]{};
        ComputeRowAiFields(b,pct,scaledDist,scaledAip);
        std::memcpy(r+AiDistOffset,scaledDist,sizeof scaledDist);
        for(int k=1;k<=8;++k)WriteAipTriplet(r,k,scaledAip[k-1]);
        ++updated;
    }
    return updated;
}

} // namespace framevr::enemysight

