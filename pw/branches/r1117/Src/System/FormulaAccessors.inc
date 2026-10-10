// НЕ ПРАВИТЬ РУКАМИ — сгенерировано Tools/FormulaCheck/gen_accessors.py
// из Data/GameLogic/FormulaPars.h (порядок virtual = vtable-слоты).
// Перегенерировать: python3 Tools/FormulaCheck/gen_accessors.py > Src/System/FormulaAccessors.inc

enum EFormulaIface { FI_UNIT = 0, FI_MISC = 1, FI_CUSTOM = 2, FI_COUNT = 3 };

// --- сигнатуры аксессоров (thiscall: self первым аргументом) ---
// B__ -> bool()
typedef bool (__thiscall *Sig_0)(void const *self);
// B__E -> bool(E)
typedef bool (__thiscall *Sig_1)(void const *self, int a0);
// B__F -> bool(F)
typedef bool (__thiscall *Sig_2)(void const *self, float a0);
// B__FIIPP -> bool(F,I,I,P,P)
typedef bool (__thiscall *Sig_3)(void const *self, float a0, int a1, int a2, void const * a3, void const * a4);
// B__I -> bool(I)
typedef bool (__thiscall *Sig_4)(void const *self, int a0);
// B__S -> bool(S)
typedef bool (__thiscall *Sig_5)(void const *self, char const * a0);
// B__U -> bool(U)
typedef bool (__thiscall *Sig_6)(void const *self, unsigned int a0);
// F__ -> float()
typedef float (__thiscall *Sig_7)(void const *self);
// F__B -> float(B)
typedef float (__thiscall *Sig_8)(void const *self, bool a0);
// F__BFEFFB -> float(B,F,E,F,F,B)
typedef float (__thiscall *Sig_9)(void const *self, bool a0, float a1, int a2, float a3, float a4, bool a5);
// F__E -> float(E)
typedef float (__thiscall *Sig_10)(void const *self, int a0);
// F__F -> float(F)
typedef float (__thiscall *Sig_11)(void const *self, float a0);
// F__FF -> float(F,F)
typedef float (__thiscall *Sig_12)(void const *self, float a0, float a1);
// F__FUP -> float(F,U,P)
typedef float (__thiscall *Sig_13)(void const *self, float a0, unsigned int a1, void const * a2);
// F__I -> float(I)
typedef float (__thiscall *Sig_14)(void const *self, int a0);
// F__PB -> float(P,B)
typedef float (__thiscall *Sig_15)(void const *self, void const * a0, bool a1);
// F__S -> float(S)
typedef float (__thiscall *Sig_16)(void const *self, char const * a0);
// F__SPP -> float(S,P,P)
typedef float (__thiscall *Sig_17)(void const *self, char const * a0, void const * a1, void const * a2);
// I__ -> int()
typedef int (__thiscall *Sig_18)(void const *self);
// I__IFPP -> int(I,F,P,P)
typedef int (__thiscall *Sig_19)(void const *self, int a0, float a1, void const * a2, void const * a3);
// I__II -> int(I,I)
typedef int (__thiscall *Sig_20)(void const *self, int a0, int a1);
// I__P -> int(P)
typedef int (__thiscall *Sig_21)(void const *self, void const * a0);
// I__S -> int(S)
typedef int (__thiscall *Sig_22)(void const *self, char const * a0);
// I__SPE -> int(S,P,E)
typedef int (__thiscall *Sig_23)(void const *self, char const * a0, void const * a1, int a2);
// I__V -> int(V)
typedef int (__thiscall *Sig_24)(void const *self, CVec2 a0);
// P__ -> void const *()
typedef void const * (__thiscall *Sig_25)(void const *self);
// P__E -> void const *(E)
typedef void const * (__thiscall *Sig_26)(void const *self, int a0);
// P__S -> void const *(S)
typedef void const * (__thiscall *Sig_27)(void const *self, char const * a0);
// P__SPE -> void const *(S,P,E)
typedef void const * (__thiscall *Sig_28)(void const *self, char const * a0, void const * a1, int a2);
// V__ -> CVec2()
typedef CVec2 (__thiscall *Sig_29)(void const *self);
// V__S -> CVec2(S)
typedef CVec2 (__thiscall *Sig_30)(void const *self, char const * a0);

enum EFormulaAccSig {
  SIG_B__ = 0,
  SIG_B__E = 1,
  SIG_B__F = 2,
  SIG_B__FIIPP = 3,
  SIG_B__I = 4,
  SIG_B__S = 5,
  SIG_B__U = 6,
  SIG_F__ = 7,
  SIG_F__B = 8,
  SIG_F__BFEFFB = 9,
  SIG_F__E = 10,
  SIG_F__F = 11,
  SIG_F__FF = 12,
  SIG_F__FUP = 13,
  SIG_F__I = 14,
  SIG_F__PB = 15,
  SIG_F__S = 16,
  SIG_F__SPP = 17,
  SIG_I__ = 18,
  SIG_I__IFPP = 19,
  SIG_I__II = 20,
  SIG_I__P = 21,
  SIG_I__S = 22,
  SIG_I__SPE = 23,
  SIG_I__V = 24,
  SIG_P__ = 25,
  SIG_P__E = 26,
  SIG_P__S = 27,
  SIG_P__SPE = 28,
  SIG_V__ = 29,
  SIG_V__S = 30,
  SIG_COUNT = 31
};

struct FormulaAccEntry {
  char const   *cpName;   // имя метода как в FormulaPars.h
  unsigned char nIface;   // EFormulaIface
  unsigned char nSig;     // EFormulaAccSig
  unsigned short nSlot;   // vtable-слот (порядок объявления virtual)
};

static const FormulaAccEntry kFormulaAcc[] = {
  {"GetLife", 0, 7, 0},
  {"GetEnergy", 0, 7, 1},
  {"IsMale", 0, 0, 2},
  {"GetMaxLife", 0, 7, 3},
  {"GetMaxEnergy", 0, 7, 4},
  {"GetRange", 0, 7, 5},
  {"GetMoveSpeed", 0, 7, 6},
  {"GetAttackSpeed", 0, 7, 7},
  {"GetCritMult", 0, 7, 8},
  {"GetLifeRegen", 0, 7, 9},
  {"GetEnergyRegen", 0, 7, 10},
  {"GetLifeRegenAbs", 0, 7, 11},
  {"GetEnergyRegenAbs", 0, 7, 12},
  {"GetEvasion", 0, 7, 13},
  {"GetLifeDrain", 0, 7, 14},
  {"GetEnergyDrain", 0, 7, 15},
  {"GetSummonCnt", 0, 7, 16},
  {"IsMoving", 0, 0, 17},
  {"GetTimeStanding", 0, 7, 18},
  {"GetTerrainType", 0, 18, 19},
  {"GetUnitKind", 0, 18, 20},
  {"IsHero", 0, 0, 21},
  {"UnitCheck", 0, 1, 22},
  {"GetNativeDefenceIndex", 0, 18, 23},
  {"GetNativeAttackIndex", 0, 18, 24},
  {"GetDevPoints", 0, 18, 25},
  {"GetFlag", 0, 6, 26},
  {"GetOnBase", 0, 0, 27},
  {"GetZZimaSex", 0, 18, 28},
  {"IsVisibleForEnemy", 0, 4, 29},
  {"GetObjectSize", 0, 7, 30},
  {"GetBaseLife", 0, 7, 31},
  {"GetBaseEnergy", 0, 7, 32},
  {"GetBaseRange", 0, 7, 33},
  {"GetBaseMoveSpeed", 0, 7, 34},
  {"GetBaseAttackSpeed", 0, 7, 35},
  {"GetBaseCriticalChance", 0, 7, 36},
  {"GetBaseCriticalMultiplier", 0, 7, 37},
  {"GetBaseLifeDrain", 0, 7, 38},
  {"GetBaseEnergyDrain", 0, 7, 39},
  {"GetBaseEvasion", 0, 7, 40},
  {"GetBaseLifeRegeneration", 0, 7, 41},
  {"GetBaseLifeRegenerationAbsolute", 0, 7, 42},
  {"GetBaseEnergyRegeneration", 0, 7, 43},
  {"GetBaseEnergyRegenerationAbsolute", 0, 7, 44},
  {"GetBaseVisibilityRange", 0, 7, 45},
  {"GetBaseStrength", 0, 7, 46},
  {"GetBaseIntellect", 0, 7, 47},
  {"GetBaseDexterity", 0, 7, 48},
  {"GetBaseBaseAttack", 0, 7, 49},
  {"GetBaseStamina", 0, 7, 50},
  {"GetBaseWill", 0, 7, 51},
  {"GetBaseNaftaMult", 0, 7, 52},
  {"GetBaseNaftaSpecForKill", 0, 7, 53},
  {"GetBaseNaftaForKill", 0, 7, 54},
  {"GetBaseNaftaForAssist", 0, 7, 55},
  {"GetBaseTeamNaftaForKill", 0, 7, 56},
  {"GetBaseTeamNaftaForPresence", 0, 7, 57},
  {"GetBaseCritChanceBonus", 0, 7, 58},
  {"GetBaseEnergyDamageDecrease", 0, 7, 59},
  {"GetBaseMaterialDamageDecrease", 0, 7, 60},
  {"GetBaseMissChance", 0, 7, 61},
  {"GetLifeRegenTotal", 0, 7, 62},
  {"GetEnergyRegenTotal", 0, 7, 63},
  {"GetDamageMin", 0, 7, 64},
  {"GetDamageMax", 0, 7, 65},
  {"GetDexterityDamageMult", 0, 7, 66},
  {"GetMoveSpeedMpS", 0, 7, 67},
  {"GetCritChance", 0, 7, 68},
  {"GetAttacksPerSecond", 0, 7, 69},
  {"GetBodyDefence", 0, 7, 70},
  {"GetSpiritDefence", 0, 7, 71},
  {"GetPenetrationFromAttackSpeed", 0, 7, 72},
  {"GetPenetrationFromDexterity", 0, 7, 73},
  {"GetPenetrationTotal", 0, 7, 74},
  {"IsRecommended", 0, 4, 75},
  {"GetStrength", 0, 7, 76},
  {"GetIntellect", 0, 7, 77},
  {"GetDexterity", 0, 7, 78},
  {"GetBaseAttack", 0, 7, 79},
  {"GetStamina", 0, 7, 80},
  {"GetWill", 0, 7, 81},
  {"GetNaftaMult", 0, 7, 82},
  {"GetNaftaSpecForKill", 0, 18, 83},
  {"GetNaftaForKill", 0, 18, 84},
  {"GetNaftaForAssist", 0, 18, 85},
  {"GetTeamNaftaForKill", 0, 18, 86},
  {"GetTeamNaftaForPresence", 0, 18, 87},
  {"GetNaftaLevel", 0, 18, 88},
  {"GetNafta", 0, 18, 89},
  {"GetTotalNaftaEarned", 0, 18, 90},
  {"GetCritChanceBonus", 0, 7, 91},
  {"GetEnergyDamageDecrease", 0, 7, 92},
  {"GetMaterialDamageDecrease", 0, 7, 93},
  {"GetMissChance", 0, 7, 94},
  {"GetObjectMaster", 0, 25, 95},
  {"GetObjectFavorite", 0, 25, 96},
  {"GetObjectTarget", 0, 25, 97},
  {"GetVariable", 0, 16, 98},
  {"GetPos", 0, 29, 99},
  {"GetFctn", 0, 18, 100},
  {"GetOriginalFctn", 0, 18, 101},
  {"GetWOID", 0, 18, 102},
  {"FindApplicator", 0, 28, 103},
  {"CountApplicators", 0, 23, 104},
  {"GetAblt", 0, 26, 105},
  {"GetTalent", 0, 27, 106},
  {"GetDamageTaken", 0, 11, 107},
  {"GetDamageDealt", 0, 11, 108},
  {"GetCoreEnergy", 0, 7, 109},
  {"HasTalentFromPack", 0, 5, 110},
  {"GetEffectiveClassMask", 0, 18, 111},
  {"CountStatusesForDispel", 0, 21, 112},
  {"GetForce", 0, 8, 113},
  {"GetHeroStatisticsValue", 0, 10, 114},
  {"GetHeroAchievementCount", 0, 22, 115},
  {"IsEvading", 0, 0, 116},
  {"GetKillStreak", 0, 18, 117},
  {"GetDeathStreak", 0, 18, 118},
  {"GetAssistersCount", 0, 18, 119},
  {"GetSpecCount", 0, 18, 120},
  {"GetPresentTeamActiveMembers", 0, 18, 121},
  {"IsSkinUsed", 0, 5, 122},
  {"GetDamageTakenEx", 0, 13, 123},
  {"GetDamageDealtEx", 0, 13, 124},
  {"IsAbilityOn", 1, 0, 0},
  {"IsTalentBought", 1, 0, 1},
  {"GetRank", 1, 18, 2},
  {"GetDist2Target", 1, 7, 3},
  {"GetScale", 1, 7, 4},
  {"GetParentScale", 1, 7, 5},
  {"GetManaCost", 1, 7, 6},
  {"GetPreparedness", 1, 7, 7},
  {"GetAbilityType", 1, 18, 8},
  {"Roll", 1, 2, 9},
  {"GetTerrainType", 1, 18, 10},
  {"GetRefineRate", 1, 18, 11},
  {"GetTalentLevel", 1, 18, 12},
  {"GetTalentDevPoints", 1, 18, 13},
  {"GetNaftaCost", 1, 18, 14},
  {"GetCooldown", 1, 7, 15},
  {"GetScrollLevel", 1, 18, 16},
  {"GetObjectOwner", 1, 25, 17},
  {"GetObjectParent", 1, 25, 18},
  {"GetObject", 1, 27, 19},
  {"GetConstant", 1, 17, 20},
  {"GetVariable", 1, 16, 21},
  {"GetTargetPos", 1, 29, 22},
  {"GetRandom", 1, 20, 23},
  {"GetAbilityScale", 1, 9, 24},
  {"GetSmartRoll", 1, 3, 25},
  {"GetSmartRandom", 1, 19, 26},
  {"GetRefineAbilityScale", 1, 12, 27},
  {"GetAlternativeTargetIndex", 1, 18, 28},
  {"GetTerrainPart", 1, 14, 29},
  {"GetActivatedWithinKit", 1, 18, 30},
  {"GetTalentsWithinKit", 1, 18, 31},
  {"GetStatusDispellPriority", 1, 15, 32},
  {"GetVectorVariable", 1, 30, 33},
  {"GetTerrianTypeUnderCursor", 1, 18, 34},
  {"GetNatureTypeInPos", 1, 24, 35},
  {"IsNight", 1, 0, 36},
  {"CheckUpgradePerCastPerTarget", 1, 0, 37},
  {"GetPos2D", 2, 29, 0},
};

static const int kFormulaAccCount = 164;
static const unsigned char kFormulaAccKind[] = {
  0,  // IUnitFormulaPars.GetLife
  0,  // IUnitFormulaPars.GetEnergy
  0,  // IUnitFormulaPars.IsMale
  0,  // IUnitFormulaPars.GetMaxLife
  0,  // IUnitFormulaPars.GetMaxEnergy
  0,  // IUnitFormulaPars.GetRange
  0,  // IUnitFormulaPars.GetMoveSpeed
  0,  // IUnitFormulaPars.GetAttackSpeed
  0,  // IUnitFormulaPars.GetCritMult
  0,  // IUnitFormulaPars.GetLifeRegen
  0,  // IUnitFormulaPars.GetEnergyRegen
  0,  // IUnitFormulaPars.GetLifeRegenAbs
  0,  // IUnitFormulaPars.GetEnergyRegenAbs
  0,  // IUnitFormulaPars.GetEvasion
  0,  // IUnitFormulaPars.GetLifeDrain
  0,  // IUnitFormulaPars.GetEnergyDrain
  0,  // IUnitFormulaPars.GetSummonCnt
  0,  // IUnitFormulaPars.IsMoving
  0,  // IUnitFormulaPars.GetTimeStanding
  0,  // IUnitFormulaPars.GetTerrainType
  0,  // IUnitFormulaPars.GetUnitKind
  0,  // IUnitFormulaPars.IsHero
  0,  // IUnitFormulaPars.UnitCheck
  0,  // IUnitFormulaPars.GetNativeDefenceIndex
  0,  // IUnitFormulaPars.GetNativeAttackIndex
  0,  // IUnitFormulaPars.GetDevPoints
  0,  // IUnitFormulaPars.GetFlag
  0,  // IUnitFormulaPars.GetOnBase
  0,  // IUnitFormulaPars.GetZZimaSex
  0,  // IUnitFormulaPars.IsVisibleForEnemy
  0,  // IUnitFormulaPars.GetObjectSize
  0,  // IUnitFormulaPars.GetBaseLife
  0,  // IUnitFormulaPars.GetBaseEnergy
  0,  // IUnitFormulaPars.GetBaseRange
  0,  // IUnitFormulaPars.GetBaseMoveSpeed
  0,  // IUnitFormulaPars.GetBaseAttackSpeed
  0,  // IUnitFormulaPars.GetBaseCriticalChance
  0,  // IUnitFormulaPars.GetBaseCriticalMultiplier
  0,  // IUnitFormulaPars.GetBaseLifeDrain
  0,  // IUnitFormulaPars.GetBaseEnergyDrain
  0,  // IUnitFormulaPars.GetBaseEvasion
  0,  // IUnitFormulaPars.GetBaseLifeRegeneration
  0,  // IUnitFormulaPars.GetBaseLifeRegenerationAbsolute
  0,  // IUnitFormulaPars.GetBaseEnergyRegeneration
  0,  // IUnitFormulaPars.GetBaseEnergyRegenerationAbsolute
  0,  // IUnitFormulaPars.GetBaseVisibilityRange
  0,  // IUnitFormulaPars.GetBaseStrength
  0,  // IUnitFormulaPars.GetBaseIntellect
  0,  // IUnitFormulaPars.GetBaseDexterity
  0,  // IUnitFormulaPars.GetBaseBaseAttack
  0,  // IUnitFormulaPars.GetBaseStamina
  0,  // IUnitFormulaPars.GetBaseWill
  0,  // IUnitFormulaPars.GetBaseNaftaMult
  0,  // IUnitFormulaPars.GetBaseNaftaSpecForKill
  0,  // IUnitFormulaPars.GetBaseNaftaForKill
  0,  // IUnitFormulaPars.GetBaseNaftaForAssist
  0,  // IUnitFormulaPars.GetBaseTeamNaftaForKill
  0,  // IUnitFormulaPars.GetBaseTeamNaftaForPresence
  0,  // IUnitFormulaPars.GetBaseCritChanceBonus
  0,  // IUnitFormulaPars.GetBaseEnergyDamageDecrease
  0,  // IUnitFormulaPars.GetBaseMaterialDamageDecrease
  0,  // IUnitFormulaPars.GetBaseMissChance
  0,  // IUnitFormulaPars.GetLifeRegenTotal
  0,  // IUnitFormulaPars.GetEnergyRegenTotal
  0,  // IUnitFormulaPars.GetDamageMin
  0,  // IUnitFormulaPars.GetDamageMax
  0,  // IUnitFormulaPars.GetDexterityDamageMult
  0,  // IUnitFormulaPars.GetMoveSpeedMpS
  0,  // IUnitFormulaPars.GetCritChance
  0,  // IUnitFormulaPars.GetAttacksPerSecond
  0,  // IUnitFormulaPars.GetBodyDefence
  0,  // IUnitFormulaPars.GetSpiritDefence
  0,  // IUnitFormulaPars.GetPenetrationFromAttackSpeed
  0,  // IUnitFormulaPars.GetPenetrationFromDexterity
  0,  // IUnitFormulaPars.GetPenetrationTotal
  0,  // IUnitFormulaPars.IsRecommended
  0,  // IUnitFormulaPars.GetStrength
  0,  // IUnitFormulaPars.GetIntellect
  0,  // IUnitFormulaPars.GetDexterity
  0,  // IUnitFormulaPars.GetBaseAttack
  0,  // IUnitFormulaPars.GetStamina
  0,  // IUnitFormulaPars.GetWill
  0,  // IUnitFormulaPars.GetNaftaMult
  0,  // IUnitFormulaPars.GetNaftaSpecForKill
  0,  // IUnitFormulaPars.GetNaftaForKill
  0,  // IUnitFormulaPars.GetNaftaForAssist
  0,  // IUnitFormulaPars.GetTeamNaftaForKill
  0,  // IUnitFormulaPars.GetTeamNaftaForPresence
  0,  // IUnitFormulaPars.GetNaftaLevel
  0,  // IUnitFormulaPars.GetNafta
  0,  // IUnitFormulaPars.GetTotalNaftaEarned
  0,  // IUnitFormulaPars.GetCritChanceBonus
  0,  // IUnitFormulaPars.GetEnergyDamageDecrease
  0,  // IUnitFormulaPars.GetMaterialDamageDecrease
  0,  // IUnitFormulaPars.GetMissChance
  1,  // IUnitFormulaPars.GetObjectMaster
  1,  // IUnitFormulaPars.GetObjectFavorite
  1,  // IUnitFormulaPars.GetObjectTarget
  0,  // IUnitFormulaPars.GetVariable
  0,  // IUnitFormulaPars.GetPos
  0,  // IUnitFormulaPars.GetFctn
  0,  // IUnitFormulaPars.GetOriginalFctn
  0,  // IUnitFormulaPars.GetWOID
  2,  // IUnitFormulaPars.FindApplicator
  0,  // IUnitFormulaPars.CountApplicators
  2,  // IUnitFormulaPars.GetAblt
  2,  // IUnitFormulaPars.GetTalent
  0,  // IUnitFormulaPars.GetDamageTaken
  0,  // IUnitFormulaPars.GetDamageDealt
  0,  // IUnitFormulaPars.GetCoreEnergy
  0,  // IUnitFormulaPars.HasTalentFromPack
  0,  // IUnitFormulaPars.GetEffectiveClassMask
  0,  // IUnitFormulaPars.CountStatusesForDispel
  0,  // IUnitFormulaPars.GetForce
  0,  // IUnitFormulaPars.GetHeroStatisticsValue
  0,  // IUnitFormulaPars.GetHeroAchievementCount
  0,  // IUnitFormulaPars.IsEvading
  0,  // IUnitFormulaPars.GetKillStreak
  0,  // IUnitFormulaPars.GetDeathStreak
  0,  // IUnitFormulaPars.GetAssistersCount
  0,  // IUnitFormulaPars.GetSpecCount
  0,  // IUnitFormulaPars.GetPresentTeamActiveMembers
  0,  // IUnitFormulaPars.IsSkinUsed
  0,  // IUnitFormulaPars.GetDamageTakenEx
  0,  // IUnitFormulaPars.GetDamageDealtEx
  0,  // IMiscFormulaPars.IsAbilityOn
  0,  // IMiscFormulaPars.IsTalentBought
  0,  // IMiscFormulaPars.GetRank
  0,  // IMiscFormulaPars.GetDist2Target
  0,  // IMiscFormulaPars.GetScale
  0,  // IMiscFormulaPars.GetParentScale
  0,  // IMiscFormulaPars.GetManaCost
  0,  // IMiscFormulaPars.GetPreparedness
  0,  // IMiscFormulaPars.GetAbilityType
  0,  // IMiscFormulaPars.Roll
  0,  // IMiscFormulaPars.GetTerrainType
  0,  // IMiscFormulaPars.GetRefineRate
  0,  // IMiscFormulaPars.GetTalentLevel
  0,  // IMiscFormulaPars.GetTalentDevPoints
  0,  // IMiscFormulaPars.GetNaftaCost
  0,  // IMiscFormulaPars.GetCooldown
  0,  // IMiscFormulaPars.GetScrollLevel
  1,  // IMiscFormulaPars.GetObjectOwner
  2,  // IMiscFormulaPars.GetObjectParent
  1,  // IMiscFormulaPars.GetObject
  0,  // IMiscFormulaPars.GetConstant
  0,  // IMiscFormulaPars.GetVariable
  0,  // IMiscFormulaPars.GetTargetPos
  0,  // IMiscFormulaPars.GetRandom
  0,  // IMiscFormulaPars.GetAbilityScale
  0,  // IMiscFormulaPars.GetSmartRoll
  0,  // IMiscFormulaPars.GetSmartRandom
  0,  // IMiscFormulaPars.GetRefineAbilityScale
  0,  // IMiscFormulaPars.GetAlternativeTargetIndex
  0,  // IMiscFormulaPars.GetTerrainPart
  0,  // IMiscFormulaPars.GetActivatedWithinKit
  0,  // IMiscFormulaPars.GetTalentsWithinKit
  0,  // IMiscFormulaPars.GetStatusDispellPriority
  0,  // IMiscFormulaPars.GetVectorVariable
  0,  // IMiscFormulaPars.GetTerrianTypeUnderCursor
  0,  // IMiscFormulaPars.GetNatureTypeInPos
  0,  // IMiscFormulaPars.IsNight
  0,  // IMiscFormulaPars.CheckUpgradePerCastPerTarget
  0,  // ICustomFormulaPars.GetPos2D
};

// --- enum-константы, доступные формулам (из FormulaPars.h) ---
struct FormulaEnumEntry { char const *cpName; int nValue; };
static const FormulaEnumEntry kFormulaEnum[] = {
  {"ABILITYSCALEMODE_STAT", 0},
  {"ABILITYSCALEMODE_LIFE", 1},
  {"ABILITYSCALEMODE_ENERGY", 2},
  {"ZZSEX_UNDEFINED", 0},
  {"ZZSEX_MALE", 1},
  {"ZZSEX_FEMALE", 2},
  {"TERR_N", 0},
  {"TERR_A", 1},
  {"TERR_B", 2},
  {"UNITCHECKID_ISHERO", 0},
  {"UNITCHECKID_ISBUILDING", 1},
  {"FCTN_N", 0},
  {"FCTN_A", 1},
  {"FCTN_B", 2},
  {"GLOBAL", 0},
  {"UNIT", 1},
  {"ABILITY", 2},
  {"DISPATCH", 3},
  {"UNIT_AND_SUMMONS", 4},
  {"BASE_ATTACK", -1},
  {"ABILITY0", 0},
  {"ABILITY1", 1},
  {"ABILITY2", 2},
  {"ABILITY3", 3},
  {"ABILITY4", 4},
  {"PROTECTOR", 1},
  {"VANGUARD", 2},
  {"EXECUTIONER", 4},
  {"FIGHTER", 8},
  {"SUPPORT", 16},
  {"TOWERS_DESTROYED", 0},
  {"BARRACKS_DESTROYED", 1},
  {"HERO_KILLS", 2},
  {"ASSISTS", 3},
  {"DEATHS", 4},
  {"GLYPHS_PICKUPED", 5},
  {"ACHIEVEMENTS_SCORE", 6},
  {"TOTAL_SCORE", 7},
  {"DamageSource::UNIT", 0},
  {"UNIT", 0},
  {"DamageSource::APPLICATOR_ABILITY", 1},
  {"APPLICATOR_ABILITY", 1},
  {"DamageSource::ABILITY_ATTACK", 2},
  {"ABILITY_ATTACK", 2},
  {"DamageSource::ABILITY_SPECIAL", 3},
  {"ABILITY_SPECIAL", 3},
  {"DamageSource::ABILITY1", 4},
  {"ABILITY1", 4},
  {"DamageSource::ABILITY2", 5},
  {"ABILITY2", 5},
  {"DamageSource::ABILITY3", 6},
  {"ABILITY3", 6},
  {"DamageSource::ABILITY4", 7},
  {"ABILITY4", 7},
  {"DamageSource::ABILITY5", 8},
  {"ABILITY5", 8},
  {"DamageSource::TALENT", 9},
  {"TALENT", 9},
  {"DamageSource::TALENT_CLASS", 10},
  {"TALENT_CLASS", 10},
  {"DamageSource::TALENT_ORDINARY", 11},
  {"TALENT_ORDINARY", 11},
  {"DamageSource::TALENT_GOOD", 12},
  {"TALENT_GOOD", 12},
  {"DamageSource::TALENT_EXCELLENT", 13},
  {"TALENT_EXCELLENT", 13},
  {"DamageSource::TALENT_MAGNIFICENT", 14},
  {"TALENT_MAGNIFICENT", 14},
  {"DamageSource::TALENT_EXCLUSIVE", 15},
  {"TALENT_EXCLUSIVE", 15},
  {"DamageSource::TALENT_OUTSTANDING", 16},
  {"TALENT_OUTSTANDING", 16},
  {"DamageSource::TALENT_TIER1", 17},
  {"TALENT_TIER1", 17},
  {"DamageSource::TALENT_TIER2", 18},
  {"TALENT_TIER2", 18},
  {"DamageSource::TALENT_TIER3", 19},
  {"TALENT_TIER3", 19},
  {"DamageSource::TALENT_TIER4", 20},
  {"TALENT_TIER4", 20},
  {"DamageSource::TALENT_TIER5", 21},
  {"TALENT_TIER5", 21},
  {"DamageSource::TALENT_TIER6", 22},
  {"TALENT_TIER6", 22},
  {"DMG_UNIT", 1},
  {"DMG_APPLICATOR_ABILITY", 2},
  {"DMG_ABILITY_ATTACK", 4},
  {"DMG_ABILITY_SPECIAL", 8},
  {"DMG_ABILITY1", 16},
  {"DMG_ABILITY2", 32},
  {"DMG_ABILITY3", 64},
  {"DMG_ABILITY4", 128},
  {"DMG_ABILITY5", 256},
  {"DMG_TALENT", 512},
  {"DMG_TALENT_CLASS", 1024},
  {"DMG_TALENT_ORDINARY", 2048},
  {"DMG_TALENT_GOOD", 4096},
  {"DMG_TALENT_EXCELLENT", 8192},
  {"DMG_TALENT_MAGNIFICENT", 16384},
  {"DMG_TALENT_EXCLUSIVE", 32768},
  {"DMG_TALENT_OUTSTANDING", 65536},
  {"DMG_TALENT_TIER1", 131072},
  {"DMG_TALENT_TIER2", 262144},
  {"DMG_TALENT_TIER3", 524288},
  {"DMG_TALENT_TIER4", 1048576},
  {"DMG_TALENT_TIER5", 2097152},
  {"DMG_TALENT_TIER6", 4194304},
  {"DMG_APPL_ABLT", 2},
  {"DMG_ATTACK", 4},
  {"DMG_SPECIAL", 8},
  {"DMG_ABLT1", 16},
  {"DMG_ABLT2", 32},
  {"DMG_ABLT3", 64},
  {"DMG_ABLT4", 128},
  {"DMG_ABLT5", 256},
  {"DMG_TAL", 512},
  {"DMG_CLASS", 1024},
  {"DMG_ORDINARY", 2048},
  {"DMG_GOOD", 4096},
  {"DMG_EXCELLENT", 8192},
  {"DMG_MAGNIFICENT", 16384},
  {"DMG_EXCLUSIVE", 32768},
  {"DMG_OUTSTANDING", 65536},
  {"DMG_TAL1", 131072},
  {"DMG_TAL2", 262144},
  {"DMG_TAL3", 524288},
  {"DMG_TAL4", 1048576},
  {"DMG_TAL5", 2097152},
  {"DMG_TAL6", 4194304},
  {"ABILITYTYPE_SIMPLE", 0},
  {"ABILITYTYPE_ACTIVE", 1},
  {"ABILITYTYPE_MULTIACTIVE", 2},
  {"ABILITYTYPE_PASSIVE", 3},
  {"ABILITYTYPE_AUTOCASTABLE", 4},
  {"ABILITYTYPE_SWITCHABLE", 5},
  {"ABILITYTYPE_CHANNELLING", 6},
};
static const int kFormulaEnumCount = 136;

// --- вызов аксессора: аргументы снимаются со стека VM, результат кладётся ---
static const unsigned char kFormulaSigArgType[SIG_COUNT * 6] = {
  FT_U, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__E
  FT_F, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__F
  FT_F, FT_I, FT_I, FT_P, FT_P, FT_U,  // B__FIIPP
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__I
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__S
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // B__U
  FT_U, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__
  FT_B, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__B
  FT_B, FT_F, FT_I, FT_F, FT_F, FT_B,  // F__BFEFFB
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__E
  FT_F, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__F
  FT_F, FT_F, FT_U, FT_U, FT_U, FT_U,  // F__FF
  FT_F, FT_I, FT_P, FT_U, FT_U, FT_U,  // F__FUP
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__I
  FT_P, FT_B, FT_U, FT_U, FT_U, FT_U,  // F__PB
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // F__S
  FT_P, FT_P, FT_P, FT_U, FT_U, FT_U,  // F__SPP
  FT_U, FT_U, FT_U, FT_U, FT_U, FT_U,  // I__
  FT_I, FT_F, FT_P, FT_P, FT_U, FT_U,  // I__IFPP
  FT_I, FT_I, FT_U, FT_U, FT_U, FT_U,  // I__II
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // I__P
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // I__S
  FT_P, FT_P, FT_I, FT_U, FT_U, FT_U,  // I__SPE
  FT_V, FT_U, FT_U, FT_U, FT_U, FT_U,  // I__V
  FT_U, FT_U, FT_U, FT_U, FT_U, FT_U,  // P__
  FT_I, FT_U, FT_U, FT_U, FT_U, FT_U,  // P__E
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // P__S
  FT_P, FT_P, FT_I, FT_U, FT_U, FT_U,  // P__SPE
  FT_U, FT_U, FT_U, FT_U, FT_U, FT_U,  // V__
  FT_P, FT_U, FT_U, FT_U, FT_U, FT_U,  // V__S
};
static const unsigned char kFormulaSigArgs[SIG_COUNT] = {
  0,  // B__
  1,  // B__E
  1,  // B__F
  5,  // B__FIIPP
  1,  // B__I
  1,  // B__S
  1,  // B__U
  0,  // F__
  1,  // F__B
  6,  // F__BFEFFB
  1,  // F__E
  1,  // F__F
  2,  // F__FF
  3,  // F__FUP
  1,  // F__I
  2,  // F__PB
  1,  // F__S
  3,  // F__SPP
  0,  // I__
  4,  // I__IFPP
  2,  // I__II
  1,  // I__P
  1,  // I__S
  3,  // I__SPE
  1,  // I__V
  0,  // P__
  1,  // P__E
  1,  // P__S
  3,  // P__SPE
  0,  // V__
  1,  // V__S
};
static const unsigned char kFormulaSigRet[SIG_COUNT] = {
  FT_B,  // B__
  FT_B,  // B__E
  FT_B,  // B__F
  FT_B,  // B__FIIPP
  FT_B,  // B__I
  FT_B,  // B__S
  FT_B,  // B__U
  FT_F,  // F__
  FT_F,  // F__B
  FT_F,  // F__BFEFFB
  FT_F,  // F__E
  FT_F,  // F__F
  FT_F,  // F__FF
  FT_F,  // F__FUP
  FT_F,  // F__I
  FT_F,  // F__PB
  FT_F,  // F__S
  FT_F,  // F__SPP
  FT_I,  // I__
  FT_I,  // I__IFPP
  FT_I,  // I__II
  FT_I,  // I__P
  FT_I,  // I__S
  FT_I,  // I__SPE
  FT_I,  // I__V
  FT_P,  // P__
  FT_P,  // P__E
  FT_P,  // P__S
  FT_P,  // P__SPE
  FT_V,  // V__
  FT_V,  // V__S
};

struct FmVec2Raw { float x, y; };
static void FormulaAccCallVec(int nAcc, void const *self, void const *const *ap,
                            float &rf, int &ri, bool &rb, CVec2 &rv, void const *&rp)
{
  switch (nAcc)
  {
  case 99:  // IUnitFormulaPars.GetPos
    rv = ((IUnitFormulaPars const *)self)->GetPos(); break;
  case 147:  // IMiscFormulaPars.GetTargetPos
    rv = ((IMiscFormulaPars const *)self)->GetTargetPos(); break;
  case 158:  // IMiscFormulaPars.GetVectorVariable
    rv = ((IMiscFormulaPars const *)self)->GetVectorVariable(*reinterpret_cast<char const * const *>(ap[0])); break;
  case 160:  // IMiscFormulaPars.GetNatureTypeInPos
    ri = (int)((IMiscFormulaPars const *)self)->GetNatureTypeInPos(CVec2(((FmVec2Raw const *)ap[0])->x, ((FmVec2Raw const *)ap[0])->y)); break;
  case 163:  // ICustomFormulaPars.GetPos2D
    rv = ((ICustomFormulaPars const *)self)->GetPos2D(); break;
  default: NI_ASSERT(false, "прямой вызов только для сигнатур с CVec2"); break;
  }
}

static const unsigned char kFormulaSigHasV[SIG_COUNT] = {
  0,  // B__
  0,  // B__E
  0,  // B__F
  0,  // B__FIIPP
  0,  // B__I
  0,  // B__S
  0,  // B__U
  0,  // F__
  0,  // F__B
  0,  // F__BFEFFB
  0,  // F__E
  0,  // F__F
  0,  // F__FF
  0,  // F__FUP
  0,  // F__I
  0,  // F__PB
  0,  // F__S
  0,  // F__SPP
  0,  // I__
  0,  // I__IFPP
  0,  // I__II
  0,  // I__P
  0,  // I__S
  0,  // I__SPE
  1,  // I__V
  0,  // P__
  0,  // P__E
  0,  // P__S
  0,  // P__SPE
  1,  // V__
  1,  // V__S
};

static void FormulaAccCall(int nSig, void *fp, void const *self, void const *const *ap,
                         float &rf, int &ri, bool &rb, CVec2 &rv, void const *&rp)
{
  switch (nSig)
  {
  case SIG_B__:
    rb = ((Sig_0)fp)(self); break;
  case SIG_B__E:
    rb = ((Sig_1)fp)(self, *reinterpret_cast<int const *>(ap[0])); break;
  case SIG_B__F:
    rb = ((Sig_2)fp)(self, *reinterpret_cast<float const *>(ap[0])); break;
  case SIG_B__FIIPP:
    rb = ((Sig_3)fp)(self, *reinterpret_cast<float const *>(ap[0]), *reinterpret_cast<int const *>(ap[1]), *reinterpret_cast<int const *>(ap[2]), *reinterpret_cast<void const * const *>(ap[3]), *reinterpret_cast<void const * const *>(ap[4])); break;
  case SIG_B__I:
    rb = ((Sig_4)fp)(self, *reinterpret_cast<int const *>(ap[0])); break;
  case SIG_B__S:
    rb = ((Sig_5)fp)(self, *reinterpret_cast<char const * const *>(ap[0])); break;
  case SIG_B__U:
    rb = ((Sig_6)fp)(self, *reinterpret_cast<unsigned int const *>(ap[0])); break;
  case SIG_F__:
    rf = ((Sig_7)fp)(self); break;
  case SIG_F__B:
    rf = ((Sig_8)fp)(self, *reinterpret_cast<bool const *>(ap[0])); break;
  case SIG_F__BFEFFB:
    rf = ((Sig_9)fp)(self, *reinterpret_cast<bool const *>(ap[0]), *reinterpret_cast<float const *>(ap[1]), *reinterpret_cast<int const *>(ap[2]), *reinterpret_cast<float const *>(ap[3]), *reinterpret_cast<float const *>(ap[4]), *reinterpret_cast<bool const *>(ap[5])); break;
  case SIG_F__E:
    rf = ((Sig_10)fp)(self, *reinterpret_cast<int const *>(ap[0])); break;
  case SIG_F__F:
    rf = ((Sig_11)fp)(self, *reinterpret_cast<float const *>(ap[0])); break;
  case SIG_F__FF:
    rf = ((Sig_12)fp)(self, *reinterpret_cast<float const *>(ap[0]), *reinterpret_cast<float const *>(ap[1])); break;
  case SIG_F__FUP:
    rf = ((Sig_13)fp)(self, *reinterpret_cast<float const *>(ap[0]), *reinterpret_cast<unsigned int const *>(ap[1]), *reinterpret_cast<void const * const *>(ap[2])); break;
  case SIG_F__I:
    rf = ((Sig_14)fp)(self, *reinterpret_cast<int const *>(ap[0])); break;
  case SIG_F__PB:
    rf = ((Sig_15)fp)(self, *reinterpret_cast<void const * const *>(ap[0]), *reinterpret_cast<bool const *>(ap[1])); break;
  case SIG_F__S:
    rf = ((Sig_16)fp)(self, *reinterpret_cast<char const * const *>(ap[0])); break;
  case SIG_F__SPP:
    rf = ((Sig_17)fp)(self, *reinterpret_cast<char const * const *>(ap[0]), *reinterpret_cast<void const * const *>(ap[1]), *reinterpret_cast<void const * const *>(ap[2])); break;
  case SIG_I__:
    ri = (int)((Sig_18)fp)(self); break;
  case SIG_I__IFPP:
    ri = (int)((Sig_19)fp)(self, *reinterpret_cast<int const *>(ap[0]), *reinterpret_cast<float const *>(ap[1]), *reinterpret_cast<void const * const *>(ap[2]), *reinterpret_cast<void const * const *>(ap[3])); break;
  case SIG_I__II:
    ri = (int)((Sig_20)fp)(self, *reinterpret_cast<int const *>(ap[0]), *reinterpret_cast<int const *>(ap[1])); break;
  case SIG_I__P:
    ri = (int)((Sig_21)fp)(self, *reinterpret_cast<void const * const *>(ap[0])); break;
  case SIG_I__S:
    ri = (int)((Sig_22)fp)(self, *reinterpret_cast<char const * const *>(ap[0])); break;
  case SIG_I__SPE:
    ri = (int)((Sig_23)fp)(self, *reinterpret_cast<char const * const *>(ap[0]), *reinterpret_cast<void const * const *>(ap[1]), *reinterpret_cast<int const *>(ap[2])); break;
  case SIG_I__V:
    ri = (int)((Sig_24)fp)(self, *reinterpret_cast<CVec2 const *>(ap[0])); break;
  case SIG_P__:
    rp = ((Sig_25)fp)(self); break;
  case SIG_P__E:
    rp = ((Sig_26)fp)(self, *reinterpret_cast<int const *>(ap[0])); break;
  case SIG_P__S:
    rp = ((Sig_27)fp)(self, *reinterpret_cast<char const * const *>(ap[0])); break;
  case SIG_P__SPE:
    rp = ((Sig_28)fp)(self, *reinterpret_cast<char const * const *>(ap[0]), *reinterpret_cast<void const * const *>(ap[1]), *reinterpret_cast<int const *>(ap[2])); break;
  case SIG_V__:
    rv = ((Sig_29)fp)(self); break;
  case SIG_V__S:
    rv = ((Sig_30)fp)(self, *reinterpret_cast<char const * const *>(ap[0])); break;
  default: NI_ASSERT(false, "unknown formula accessor signature"); break;
  }
}

