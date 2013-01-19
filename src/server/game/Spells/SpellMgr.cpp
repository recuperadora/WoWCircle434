/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "SpellMgr.h"
#include "SpellInfo.h"
#include "ObjectMgr.h"
#include "SpellAuras.h"
#include "SpellAuraDefines.h"
#include "SharedDefines.h"
#include "DBCStores.h"
#include "World.h"
#include "Chat.h"
#include "Spell.h"
#include "BattlegroundMgr.h"
#include "CreatureAI.h"
#include "MapManager.h"
#include "BattlegroundIC.h"
#include "BattlefieldWG.h"
#include "BattlefieldMgr.h"

bool IsPrimaryProfessionSkill(uint32 skill)
{
    SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
    if (!pSkill)
        return false;

    if (pSkill->categoryId != SKILL_CATEGORY_PROFESSION)
        return false;

    return true;
}

bool IsPartOfSkillLine(uint32 skillId, uint32 spellId)
{
    SkillLineAbilityMapBounds skillBounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (SkillLineAbilityMap::const_iterator itr = skillBounds.first; itr != skillBounds.second; ++itr)
        if (itr->second->skillId == skillId)
            return true;

    return false;
}

DiminishingGroup GetDiminishingReturnsGroupForSpell(SpellInfo const* spellproto, bool triggered)
{
    if (spellproto->IsPositive())
        return DIMINISHING_NONE;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (spellproto->Effects[i].ApplyAuraName == SPELL_AURA_MOD_TAUNT)
            return DIMINISHING_TAUNT;
    }

    // Explicit Diminishing Groups
    switch (spellproto->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            // Pet charge effects (Infernal Awakening, Demon Charge)
            if (spellproto->SpellVisual[0] == 2816 && spellproto->SpellIconID == 15)
                return DIMINISHING_CONTROLLED_STUN;
            // Frost Tomb
            else if (spellproto->Id == 48400)
                return DIMINISHING_NONE;
            // Gnaw
            else if (spellproto->Id == 47481)
                return DIMINISHING_CONTROLLED_STUN;
            // Earthquake
            else if (spellproto->Id == 64697)
                return DIMINISHING_NONE;
            break;
        }
        // Event spells
        case SPELLFAMILY_UNK1:
            return DIMINISHING_NONE;
        case SPELLFAMILY_MAGE:
        {
            // Frostbite
            if (spellproto->SpellFamilyFlags[1] & 0x80000000)
                return DIMINISHING_ROOT;
            // Shattered Barrier
            else if (spellproto->SpellVisual[0] == 12297)
                return DIMINISHING_ROOT;
            // Deep Freeze
            else if (spellproto->SpellIconID == 2939 && spellproto->SpellVisual[0] == 9963)
                return DIMINISHING_CONTROLLED_STUN;
            // Frost Nova / Freeze (Water Elemental)
            else if (spellproto->SpellIconID == 193)
                return DIMINISHING_CONTROLLED_ROOT;
            // Dragon's Breath
            else if (spellproto->SpellFamilyFlags[0] & 0x800000)
                return DIMINISHING_DRAGONS_BREATH;
            // Ring of Frost
            else if (spellproto->Id == 82691)
                return DIMINISHING_DISORIENT;
            // Slow
            else if (spellproto->Id == 31589)
                return DIMINISHING_LIMITONLY;
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Hamstring - limit duration to 10s in PvP
            if (spellproto->SpellFamilyFlags[0] & 0x2)
                return DIMINISHING_LIMITONLY;
            // Charge Stun (own diminishing)
            else if (spellproto->SpellFamilyFlags[0] & 0x01000000)
                return DIMINISHING_CHARGE;
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Curses/etc
            if ((spellproto->SpellFamilyFlags[0] & 0x80000000) || (spellproto->SpellFamilyFlags[1] & 0x200))
                return DIMINISHING_LIMITONLY;
            // Seduction
            else if (spellproto->SpellFamilyFlags[1] & 0x10000000)
                return DIMINISHING_FEAR;
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            // Pounce
            if (spellproto->Id == 9005)
                return DIMINISHING_CONTROLLED_STUN;
            // Cyclone
            else if (spellproto->SpellFamilyFlags[1] & 0x20)
                return DIMINISHING_CYCLONE;
            // Entangling Roots
            // Nature's Grasp
            else if (spellproto->SpellFamilyFlags[0] & 0x00000200)
                return DIMINISHING_CONTROLLED_ROOT;
            // Faerie Fire
            else if (spellproto->SpellFamilyFlags[0] & 0x400)
                return DIMINISHING_LIMITONLY;
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            // Gouge
            if (spellproto->SpellFamilyFlags[0] & 0x8)
                return DIMINISHING_DISORIENT;
            // Blind
            else if (spellproto->SpellFamilyFlags[0] & 0x1000000)
                return DIMINISHING_FEAR;
            // Cheap Shot
            else if (spellproto->SpellFamilyFlags[0] & 0x400)
                return DIMINISHING_CONTROLLED_STUN;
            // Crippling poison - Limit to 10 seconds in PvP (No SpellFamilyFlags)
            else if (spellproto->SpellIconID == 163)
                return DIMINISHING_LIMITONLY;
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Hunter's Mark
            if ((spellproto->SpellFamilyFlags[0] & 0x400) && spellproto->SpellIconID == 538)
                return DIMINISHING_LIMITONLY;
            // Scatter Shot (own diminishing)
            else if ((spellproto->SpellFamilyFlags[0] & 0x40000) && spellproto->SpellIconID == 132)
                return DIMINISHING_SCATTER_SHOT;
            // Entrapment (own diminishing)
            else if (spellproto->SpellVisual[0] == 7484 && spellproto->SpellIconID == 20)
                return DIMINISHING_ENTRAPMENT;
            // Wyvern Sting mechanic is MECHANIC_SLEEP but the diminishing is DIMINISHING_DISORIENT
            else if ((spellproto->SpellFamilyFlags[1] & 0x1000) && spellproto->SpellIconID == 1721)
                return DIMINISHING_DISORIENT;
            // Freezing Arrow
            else if (spellproto->SpellFamilyFlags[0] & 0x8)
                return DIMINISHING_DISORIENT;
            // Bad Manner (Pet Monkey)
            else if (spellproto->Id == 90337)
                return DIMINISHING_STUN;
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Judgement of Justice - limit duration to 10s in PvP
            if (spellproto->SpellFamilyFlags[0] & 0x100000)
                return DIMINISHING_LIMITONLY;
            // Turn Evil
            else if ((spellproto->SpellFamilyFlags[1] & 0x804000) && spellproto->SpellIconID == 309)
                return DIMINISHING_FEAR;
            break;
        }
        case SPELLFAMILY_DEATHKNIGHT:
        {
            // Hungering Cold (no flags)
            if (spellproto->SpellIconID == 2797)
                return DIMINISHING_DISORIENT;
            // Mark of Blood
            else if ((spellproto->SpellFamilyFlags[0] & 0x10000000) && spellproto->SpellIconID == 2285)
                return DIMINISHING_LIMITONLY;
            break;
        }
        default:
            break;
    }

    // Lastly - Set diminishing depending on mechanic
    uint32 mechanic = spellproto->GetAllEffectsMechanicMask();
    if (mechanic & (1 << MECHANIC_CHARM))
        return DIMINISHING_MIND_CONTROL;
    if (mechanic & (1 << MECHANIC_SILENCE))
        return DIMINISHING_SILENCE;
    if (mechanic & (1 << MECHANIC_SLEEP))
        return DIMINISHING_SLEEP;
    if (mechanic & ((1 << MECHANIC_SAPPED) | (1 << MECHANIC_POLYMORPH) | (1 << MECHANIC_SHACKLE)))
        return DIMINISHING_DISORIENT;
    // Mechanic Knockout, except Blast Wave
    if (mechanic & (1 << MECHANIC_KNOCKOUT) && spellproto->SpellIconID != 292)
        return DIMINISHING_DISORIENT;
    if (mechanic & (1 << MECHANIC_DISARM))
        return DIMINISHING_DISARM;
    if (mechanic & (1 << MECHANIC_FEAR))
        return DIMINISHING_FEAR;
    if (mechanic & (1 << MECHANIC_STUN))
        return triggered ? DIMINISHING_STUN : DIMINISHING_CONTROLLED_STUN;
    if (mechanic & (1 << MECHANIC_BANISH))
        return DIMINISHING_BANISH;
    if (mechanic & (1 << MECHANIC_ROOT))
        return triggered ? DIMINISHING_ROOT : DIMINISHING_CONTROLLED_ROOT;
    if (mechanic & (1 << MECHANIC_HORROR))
        return DIMINISHING_HORROR;

    return DIMINISHING_NONE;
}

DiminishingReturnsType GetDiminishingReturnsGroupType(DiminishingGroup group)
{
    switch (group)
    {
        case DIMINISHING_TAUNT:
        case DIMINISHING_CONTROLLED_STUN:
        case DIMINISHING_STUN:
        case DIMINISHING_OPENING_STUN:
        case DIMINISHING_CYCLONE:
        case DIMINISHING_CHARGE:
            return DRTYPE_ALL;
        case DIMINISHING_LIMITONLY:
        case DIMINISHING_NONE:
            return DRTYPE_NONE;
        default:
            return DRTYPE_PLAYER;
    }
}

DiminishingLevels GetDiminishingReturnsMaxLevel(DiminishingGroup group)
{
    switch (group)
    {
        case DIMINISHING_TAUNT:
            return DIMINISHING_LEVEL_TAUNT_IMMUNE;
        default:
            return DIMINISHING_LEVEL_IMMUNE;
    }
}

int32 GetDiminishingReturnsLimitDuration(DiminishingGroup group, SpellInfo const* spellproto)
{
    if (!IsDiminishingReturnsGroupDurationLimited(group))
        return 0;

    // Explicit diminishing duration
    switch (spellproto->SpellFamilyName)
    {
        case SPELLFAMILY_DRUID:
        {
            // Faerie Fire - limit to 40 seconds in PvP (3.1)
            if (spellproto->SpellFamilyFlags[0] & 0x400)
                return 40 * IN_MILLISECONDS;
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Wyvern Sting
            if (spellproto->SpellFamilyFlags[1] & 0x1000)
                return 6 * IN_MILLISECONDS;
            // Hunter's Mark
            if (spellproto->SpellFamilyFlags[0] & 0x400)
                return 30 * IN_MILLISECONDS;
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Repentance - limit to 6 seconds in PvP
            if (spellproto->SpellFamilyFlags[0] & 0x4)
                return 6 * IN_MILLISECONDS;
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Banish - limit to 6 seconds in PvP
            if (spellproto->SpellFamilyFlags[1] & 0x8000000)
                return 6 * IN_MILLISECONDS;
            // Curse of Tongues - limit to 12 seconds in PvP
            else if (spellproto->SpellFamilyFlags[2] & 0x800)
                return 12 * IN_MILLISECONDS;
            // Curse of Elements - limit to 120 seconds in PvP
            else if (spellproto->SpellFamilyFlags[1] & 0x200)
               return 120 * IN_MILLISECONDS;
            break;
        }
        default:
            break;
    }

    return 8 * IN_MILLISECONDS;
}

bool IsDiminishingReturnsGroupDurationLimited(DiminishingGroup group)
{
    switch (group)
    {
        case DIMINISHING_BANISH:
        case DIMINISHING_CONTROLLED_STUN:
        case DIMINISHING_CONTROLLED_ROOT:
        case DIMINISHING_CYCLONE:
        case DIMINISHING_DISORIENT:
        case DIMINISHING_ENTRAPMENT:
        case DIMINISHING_FEAR:
        case DIMINISHING_HORROR:
        case DIMINISHING_MIND_CONTROL:
        case DIMINISHING_OPENING_STUN:
        case DIMINISHING_ROOT:
        case DIMINISHING_STUN:
        case DIMINISHING_SLEEP:
        case DIMINISHING_LIMITONLY:
            return true;
        default:
            return false;
    }
}

SpellMgr::SpellMgr()
{
}

SpellMgr::~SpellMgr()
{
    UnloadSpellInfoStore();
}

/// Some checks for spells, to prevent adding deprecated/broken spells for trainers, spell book, etc
bool SpellMgr::IsSpellValid(SpellInfo const* spellInfo, Player* player, bool msg)
{
    // not exist
    if (!spellInfo)
        return false;

    bool need_check_reagents = false;

    // check effects
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (spellInfo->Effects[i].Effect)
        {
            case 0:
                continue;

            // craft spell for crafting non-existed item (break client recipes list show)
            case SPELL_EFFECT_CREATE_ITEM:
            case SPELL_EFFECT_CREATE_ITEM_2:
            {
                if (spellInfo->Effects[i].ItemType == 0)
                {
                    // skip auto-loot crafting spells, its not need explicit item info (but have special fake items sometime)
                    if (!spellInfo->IsLootCrafting())
                    {
                        if (msg)
                        {
                            if (player)
                                ChatHandler(player).PSendSysMessage("Craft spell %u not have create item entry.", spellInfo->Id);
                            else
                                sLog->outError(LOG_FILTER_SQL, "Craft spell %u not have create item entry.", spellInfo->Id);
                        }
                        return false;
                    }

                }
                // also possible IsLootCrafting case but fake item must exist anyway
                else if (!sObjectMgr->GetItemTemplate(spellInfo->Effects[i].ItemType))
                {
                    if (msg)
                    {
                        if (player)
                            ChatHandler(player).PSendSysMessage("Craft spell %u create not-exist in DB item (Entry: %u) and then...", spellInfo->Id, spellInfo->Effects[i].ItemType);
                        else
                            sLog->outError(LOG_FILTER_SQL, "Craft spell %u create not-exist in DB item (Entry: %u) and then...", spellInfo->Id, spellInfo->Effects[i].ItemType);
                    }
                    return false;
                }

                need_check_reagents = true;
                break;
            }
            case SPELL_EFFECT_LEARN_SPELL:
            {
                SpellInfo const* spellInfo2 = sSpellMgr->GetSpellInfo(spellInfo->Effects[i].TriggerSpell);
                if (!IsSpellValid(spellInfo2, player, msg))
                {
                    if (msg)
                    {
                        if (player)
                            ChatHandler(player).PSendSysMessage("Spell %u learn to broken spell %u, and then...", spellInfo->Id, spellInfo->Effects[i].TriggerSpell);
                        else
                            sLog->outError(LOG_FILTER_SQL, "Spell %u learn to invalid spell %u, and then...", spellInfo->Id, spellInfo->Effects[i].TriggerSpell);
                    }
                    return false;
                }
                break;
            }
        }
    }

    if (need_check_reagents)
    {
        for (uint8 j = 0; j < MAX_SPELL_REAGENTS; ++j)
        {
            if (spellInfo->Reagent[j] > 0 && !sObjectMgr->GetItemTemplate(spellInfo->Reagent[j]))
            {
                if (msg)
                {
                    if (player)
                        ChatHandler(player).PSendSysMessage("Craft spell %u have not-exist reagent in DB item (Entry: %u) and then...", spellInfo->Id, spellInfo->Reagent[j]);
                    else
                        sLog->outError(LOG_FILTER_SQL, "Craft spell %u have not-exist reagent in DB item (Entry: %u) and then...", spellInfo->Id, spellInfo->Reagent[j]);
                }
                return false;
            }
        }
    }

    return true;
}

uint32 SpellMgr::GetSpellDifficultyId(uint32 spellId) const
{
    SpellDifficultySearcherMap::const_iterator i = mSpellDifficultySearcherMap.find(spellId);
    return i == mSpellDifficultySearcherMap.end() ? 0 : (*i).second;
}

void SpellMgr::SetSpellDifficultyId(uint32 spellId, uint32 id)
{
    mSpellDifficultySearcherMap[spellId] = id;
}

uint32 SpellMgr::GetSpellIdForDifficulty(uint32 spellId, Unit const* caster) const
{
    if (!GetSpellInfo(spellId))
        return spellId;

    if (!caster || !caster->GetMap() || !caster->GetMap()->IsDungeon())
        return spellId;

    uint32 mode = uint32(caster->GetMap()->GetSpawnMode());
    if (mode >= MAX_DIFFICULTY)
    {
        sLog->outError(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellIdForDifficulty: Incorrect Difficulty for spell %u.", spellId);
        return spellId; //return source spell
    }

    uint32 difficultyId = GetSpellDifficultyId(spellId);
    if (!difficultyId)
        return spellId; //return source spell, it has only REGULAR_DIFFICULTY

    SpellDifficultyEntry const* difficultyEntry = sSpellDifficultyStore.LookupEntry(difficultyId);
    if (!difficultyEntry)
    {
        sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellIdForDifficulty: SpellDifficultyEntry not found for spell %u. This should never happen.", spellId);
        return spellId; //return source spell
    }

    if (difficultyEntry->SpellID[mode] <= 0 && mode > DUNGEON_DIFFICULTY_HEROIC)
    {
        sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellIdForDifficulty: spell %u mode %u spell is NULL, using mode %u", spellId, mode, mode - 2);
        mode -= 2;
    }

    if (difficultyEntry->SpellID[mode] <= 0)
    {
        sLog->outError(LOG_FILTER_SQL, "SpellMgr::GetSpellIdForDifficulty: spell %u mode %u spell is 0. Check spelldifficulty_dbc!", spellId, mode);
        return spellId;
    }

    sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellIdForDifficulty: spellid for spell %u in mode %u is %d", spellId, mode, difficultyEntry->SpellID[mode]);
    return uint32(difficultyEntry->SpellID[mode]);
}

SpellInfo const* SpellMgr::GetSpellForDifficultyFromSpell(SpellInfo const* spell, Unit const* caster) const
{
    uint32 newSpellId = GetSpellIdForDifficulty(spell->Id, caster);
    SpellInfo const* newSpell = GetSpellInfo(newSpellId);
    if (!newSpell)
    {
        sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellForDifficultyFromSpell: spell %u not found. Check spelldifficulty_dbc!", newSpellId);
        return spell;
    }

    sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "SpellMgr::GetSpellForDifficultyFromSpell: Spell id for instance mode is %u (original %u)", newSpell->Id, spell->Id);
    return newSpell;
}

SpellChainNode const* SpellMgr::GetSpellChainNode(uint32 spell_id) const
{
    SpellChainMap::const_iterator itr = mSpellChains.find(spell_id);
    if (itr == mSpellChains.end())
        return NULL;

    return &itr->second;
}

uint32 SpellMgr::GetFirstSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->first->Id;

    return spell_id;
}

uint32 SpellMgr::GetLastSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->last->Id;

    return spell_id;
}

uint32 SpellMgr::GetNextSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        if (node->next)
            return node->next->Id;

    return 0;
}

uint32 SpellMgr::GetPrevSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        if (node->prev)
            return node->prev->Id;

    return 0;
}

uint8 SpellMgr::GetSpellRank(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->rank;

    return 0;
}

uint32 SpellMgr::GetSpellWithRank(uint32 spell_id, uint32 rank, bool strict) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
    {
        if (rank != node->rank)
            return GetSpellWithRank(node->rank < rank ? node->next->Id : node->prev->Id, rank, strict);
    }
    else if (strict && rank > 1)
        return 0;
    return spell_id;
}

SpellRequiredMapBounds SpellMgr::GetSpellsRequiredForSpellBounds(uint32 spell_id) const
{
    return mSpellReq.equal_range(spell_id);
}

SpellsRequiringSpellMapBounds SpellMgr::GetSpellsRequiringSpellBounds(uint32 spell_id) const
{
    return mSpellsReqSpell.equal_range(spell_id);
}

bool SpellMgr::IsSpellRequiringSpell(uint32 spellid, uint32 req_spellid) const
{
    SpellsRequiringSpellMapBounds spellsRequiringSpell = GetSpellsRequiringSpellBounds(req_spellid);
    for (SpellsRequiringSpellMap::const_iterator itr = spellsRequiringSpell.first; itr != spellsRequiringSpell.second; ++itr)
    {
        if (itr->second == spellid)
            return true;
    }
    return false;
}

const SpellsRequiringSpellMap SpellMgr::GetSpellsRequiringSpell()
{
    return this->mSpellsReqSpell;
}

uint32 SpellMgr::GetSpellRequired(uint32 spell_id) const
{
    SpellRequiredMap::const_iterator itr = mSpellReq.find(spell_id);

    if (itr == mSpellReq.end())
        return 0;

    return itr->second;
}

SpellLearnSkillNode const* SpellMgr::GetSpellLearnSkill(uint32 spell_id) const
{
    SpellLearnSkillMap::const_iterator itr = mSpellLearnSkills.find(spell_id);
    if (itr != mSpellLearnSkills.end())
        return &itr->second;
    else
        return NULL;
}

SpellLearnSpellMapBounds SpellMgr::GetSpellLearnSpellMapBounds(uint32 spell_id) const
{
    return mSpellLearnSpells.equal_range(spell_id);
}

bool SpellMgr::IsSpellLearnSpell(uint32 spell_id) const
{
    return mSpellLearnSpells.find(spell_id) != mSpellLearnSpells.end();
}

bool SpellMgr::IsSpellLearnToSpell(uint32 spell_id1, uint32 spell_id2) const
{
    SpellLearnSpellMapBounds bounds = GetSpellLearnSpellMapBounds(spell_id1);
    for (SpellLearnSpellMap::const_iterator i = bounds.first; i != bounds.second; ++i)
        if (i->second.spell == spell_id2)
            return true;
    return false;
}

SpellTargetPosition const* SpellMgr::GetSpellTargetPosition(uint32 spell_id) const
{
    SpellTargetPositionMap::const_iterator itr = mSpellTargetPositions.find(spell_id);
    if (itr != mSpellTargetPositions.end())
        return &itr->second;
    return NULL;
}

SpellSpellGroupMapBounds SpellMgr::GetSpellSpellGroupMapBounds(uint32 spell_id) const
{
    spell_id = GetFirstSpellInChain(spell_id);
    return mSpellSpellGroup.equal_range(spell_id);
}

bool SpellMgr::IsSpellMemberOfSpellGroup(uint32 spellid, SpellGroup groupid) const
{
    SpellSpellGroupMapBounds spellGroup = GetSpellSpellGroupMapBounds(spellid);
    for (SpellSpellGroupMap::const_iterator itr = spellGroup.first; itr != spellGroup.second; ++itr)
    {
        if (itr->second == groupid)
            return true;
    }
    return false;
}

SpellGroupSpellMapBounds SpellMgr::GetSpellGroupSpellMapBounds(SpellGroup group_id) const
{
    return mSpellGroupSpell.equal_range(group_id);
}

void SpellMgr::GetSetOfSpellsInSpellGroup(SpellGroup group_id, std::set<uint32>& foundSpells) const
{
    std::set<SpellGroup> usedGroups;
    GetSetOfSpellsInSpellGroup(group_id, foundSpells, usedGroups);
}

void SpellMgr::GetSetOfSpellsInSpellGroup(SpellGroup group_id, std::set<uint32>& foundSpells, std::set<SpellGroup>& usedGroups) const
{
    if (usedGroups.find(group_id) != usedGroups.end())
        return;
    usedGroups.insert(group_id);

    SpellGroupSpellMapBounds groupSpell = GetSpellGroupSpellMapBounds(group_id);
    for (SpellGroupSpellMap::const_iterator itr = groupSpell.first; itr != groupSpell.second; ++itr)
    {
        if (itr->second < 0)
        {
            SpellGroup currGroup = (SpellGroup)abs(itr->second);
            GetSetOfSpellsInSpellGroup(currGroup, foundSpells, usedGroups);
        }
        else
        {
            foundSpells.insert(itr->second);
        }
    }
}

bool SpellMgr::AddSameEffectStackRuleSpellGroups(SpellInfo const* spellInfo, int32 amount, std::map<SpellGroup, int32>& groups) const
{
    uint32 spellId = spellInfo->GetFirstRankSpell()->Id;
    SpellSpellGroupMapBounds spellGroup = GetSpellSpellGroupMapBounds(spellId);
    // Find group with SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT if it belongs to one
    for (SpellSpellGroupMap::const_iterator itr = spellGroup.first; itr != spellGroup.second; ++itr)
    {
        SpellGroup group = itr->second;
        SpellGroupStackMap::const_iterator found = mSpellGroupStack.find(group);
        if (found != mSpellGroupStack.end())
        {
            if (found->second == SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT)
            {
                // Put the highest amount in the map
                if (groups.find(group) == groups.end())
                    groups[group] = amount;
                else
                {
                    int32 curr_amount = groups[group];
                    // Take absolute value because this also counts for the highest negative aura
                    if (abs(curr_amount) < abs(amount))
                        groups[group] = amount;
                }
                // return because a spell should be in only one SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT group
                return true;
            }
        }
    }
    // Not in a SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT group, so return false
    return false;
}

SpellGroupStackRule SpellMgr::CheckSpellGroupStackRules(SpellInfo const* spellInfo1, SpellInfo const* spellInfo2) const
{
    uint32 spellid_1 = spellInfo1->GetFirstRankSpell()->Id;
    uint32 spellid_2 = spellInfo2->GetFirstRankSpell()->Id;
    if (spellid_1 == spellid_2)
        return SPELL_GROUP_STACK_RULE_DEFAULT;
    // find SpellGroups which are common for both spells
    SpellSpellGroupMapBounds spellGroup1 = GetSpellSpellGroupMapBounds(spellid_1);
    std::set<SpellGroup> groups;
    for (SpellSpellGroupMap::const_iterator itr = spellGroup1.first; itr != spellGroup1.second; ++itr)
    {
        if (IsSpellMemberOfSpellGroup(spellid_2, itr->second))
        {
            bool add = true;
            SpellGroupSpellMapBounds groupSpell = GetSpellGroupSpellMapBounds(itr->second);
            for (SpellGroupSpellMap::const_iterator itr2 = groupSpell.first; itr2 != groupSpell.second; ++itr2)
            {
                if (itr2->second < 0)
                {
                    SpellGroup currGroup = (SpellGroup)abs(itr2->second);
                    if (IsSpellMemberOfSpellGroup(spellid_1, currGroup) && IsSpellMemberOfSpellGroup(spellid_2, currGroup))
                    {
                        add = false;
                        break;
                    }
                }
            }
            if (add)
                groups.insert(itr->second);
        }
    }

    SpellGroupStackRule rule = SPELL_GROUP_STACK_RULE_DEFAULT;

    for (std::set<SpellGroup>::iterator itr = groups.begin(); itr!= groups.end(); ++itr)
    {
        SpellGroupStackMap::const_iterator found = mSpellGroupStack.find(*itr);
        if (found != mSpellGroupStack.end())
            rule = found->second;
        if (rule)
            break;
    }
    return rule;
}

SpellProcEventEntry const* SpellMgr::GetSpellProcEvent(uint32 spellId) const
{
    SpellProcEventMap::const_iterator itr = mSpellProcEventMap.find(spellId);
    if (itr != mSpellProcEventMap.end())
        return &itr->second;
    return NULL;
}

bool SpellMgr::IsSpellProcEventCanTriggeredBy(SpellProcEventEntry const* spellProcEvent, uint32 EventProcFlag, SpellInfo const* procSpell, uint32 procFlags, uint32 procExtra, bool active)
{
    // No extra req need
    uint32 procEvent_procEx = PROC_EX_NONE;

    // check prockFlags for condition
    if ((procFlags & EventProcFlag) == 0)
        return false;

    bool hasFamilyMask = false;

    /* Check Periodic Auras

    *Dots can trigger if spell has no PROC_FLAG_SUCCESSFUL_NEGATIVE_MAGIC_SPELL
        nor PROC_FLAG_TAKEN_POSITIVE_MAGIC_SPELL

    *Only Hots can trigger if spell has PROC_FLAG_TAKEN_POSITIVE_MAGIC_SPELL

    *Only dots can trigger if spell has both positivity flags or PROC_FLAG_SUCCESSFUL_NEGATIVE_MAGIC_SPELL

    *Aura has to have PROC_FLAG_TAKEN_POSITIVE_MAGIC_SPELL or spellfamily specified to trigger from Hot

    */

    if (procFlags & PROC_FLAG_DONE_PERIODIC)
    {
        if (EventProcFlag & PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_NEG)
        {
            if (!(procExtra & PROC_EX_INTERNAL_DOT))
                return false;
        }
        else if (procExtra & PROC_EX_INTERNAL_HOT)
            procExtra |= PROC_EX_INTERNAL_REQ_FAMILY;
        else if (EventProcFlag & PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_POS)
            return false;
    }

    if (procFlags & PROC_FLAG_TAKEN_PERIODIC)
    {
        if (EventProcFlag & PROC_FLAG_TAKEN_SPELL_MAGIC_DMG_CLASS_POS)
        {
            if (!(procExtra & PROC_EX_INTERNAL_DOT))
                return false;
        }
        else if (procExtra & PROC_EX_INTERNAL_HOT)
            procExtra |= PROC_EX_INTERNAL_REQ_FAMILY;
        else if (EventProcFlag & PROC_FLAG_TAKEN_SPELL_NONE_DMG_CLASS_POS)
            return false;
    }
    // Trap casts are active by default
    if (procFlags & PROC_FLAG_DONE_TRAP_ACTIVATION)
        active = true;

    // Always trigger for this
    if (procFlags & (PROC_FLAG_KILLED | PROC_FLAG_KILL | PROC_FLAG_DEATH))
        return true;

    if (spellProcEvent)     // Exist event data
    {
        // Store extra req
        procEvent_procEx = spellProcEvent->procEx;

        // For melee triggers
        if (procSpell == NULL)
        {
            // Check (if set) for school (melee attack have Normal school)
            if (spellProcEvent->schoolMask && (spellProcEvent->schoolMask & SPELL_SCHOOL_MASK_NORMAL) == 0)
                return false;
        }
        else // For spells need check school/spell family/family mask
        {
            // Check (if set) for school
            if (spellProcEvent->schoolMask && (spellProcEvent->schoolMask & procSpell->SchoolMask) == 0)
                return false;

            // Check (if set) for spellFamilyName
            if (spellProcEvent->spellFamilyName && (spellProcEvent->spellFamilyName != procSpell->SpellFamilyName))
                return false;

            // spellFamilyName is Ok need check for spellFamilyMask if present
            if (spellProcEvent->spellFamilyMask)
            {
                if (!(spellProcEvent->spellFamilyMask & procSpell->SpellFamilyFlags))
                    return false;
                hasFamilyMask = true;
                // Some spells are not considered as active even with have spellfamilyflags
                if (!(procEvent_procEx & PROC_EX_ONLY_ACTIVE_SPELL))
                    active = true;
            }
        }
    }

    if (procExtra & (PROC_EX_INTERNAL_REQ_FAMILY))
    {
        if (!hasFamilyMask)
            return false;
    }

    // Check for extra req (if none) and hit/crit
    if (procEvent_procEx == PROC_EX_NONE)
    {
        // No extra req, so can trigger only for hit/crit - spell has to be active
        if ((procExtra & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) && active)
            return true;
    }
    else // Passive spells hits here only if resist/reflect/immune/evade
    {
        if (procExtra & AURA_SPELL_PROC_EX_MASK)
        {
            // if spell marked as procing only from not active spells
            if (active && procEvent_procEx & PROC_EX_NOT_ACTIVE_SPELL)
                return false;
            // if spell marked as procing only from active spells
            if (!active && procEvent_procEx & PROC_EX_ONLY_ACTIVE_SPELL)
                return false;
            // Exist req for PROC_EX_EX_TRIGGER_ALWAYS
            if (procEvent_procEx & PROC_EX_EX_TRIGGER_ALWAYS)
                return true;
            // PROC_EX_NOT_ACTIVE_SPELL and PROC_EX_ONLY_ACTIVE_SPELL flags handle: if passed checks before
            if ((procExtra & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) && ((procEvent_procEx & (AURA_SPELL_PROC_EX_MASK)) == 0))
                return true;
        }
        // Check Extra Requirement like (hit/crit/miss/resist/parry/dodge/block/immune/reflect/absorb and other)
        if (procEvent_procEx & procExtra)
            return true;
    }
    return false;
}

SpellProcEntry const* SpellMgr::GetSpellProcEntry(uint32 spellId) const
{
    SpellProcMap::const_iterator itr = mSpellProcMap.find(spellId);
    if (itr != mSpellProcMap.end())
        return &itr->second;
    return NULL;
}

bool SpellMgr::CanSpellTriggerProcOnEvent(SpellProcEntry const& procEntry, ProcEventInfo& eventInfo)
{
    // proc type doesn't match
    if (!(eventInfo.GetTypeMask() & procEntry.typeMask))
        return false;

    // check XP or honor target requirement
    if (procEntry.attributesMask & PROC_ATTR_REQ_EXP_OR_HONOR)
        if (Player* actor = eventInfo.GetActor()->ToPlayer())
            if (eventInfo.GetActionTarget() && !actor->isHonorOrXPTarget(eventInfo.GetActionTarget()))
                return false;

    // always trigger for these types
    if (eventInfo.GetTypeMask() & (PROC_FLAG_KILLED | PROC_FLAG_KILL | PROC_FLAG_DEATH))
        return true;

    // check school mask (if set) for other trigger types
    if (procEntry.schoolMask && !(eventInfo.GetSchoolMask() & procEntry.schoolMask))
        return false;

    // check spell family name/flags (if set) for spells
    if (eventInfo.GetTypeMask() & (PERIODIC_PROC_FLAG_MASK | SPELL_PROC_FLAG_MASK | PROC_FLAG_DONE_TRAP_ACTIVATION))
    {
        if (procEntry.spellFamilyName && (procEntry.spellFamilyName != eventInfo.GetSpellInfo()->SpellFamilyName))
            return false;

        if (procEntry.spellFamilyMask && !(procEntry.spellFamilyMask & eventInfo.GetSpellInfo()->SpellFamilyFlags))
            return false;
    }

    // check spell type mask (if set)
    if (eventInfo.GetTypeMask() & (SPELL_PROC_FLAG_MASK | PERIODIC_PROC_FLAG_MASK))
    {
        if (procEntry.spellTypeMask && !(eventInfo.GetSpellTypeMask() & procEntry.spellTypeMask))
            return false;
    }

    // check spell phase mask
    if (eventInfo.GetTypeMask() & REQ_SPELL_PHASE_PROC_FLAG_MASK)
    {
        if (!(eventInfo.GetSpellPhaseMask() & procEntry.spellPhaseMask))
            return false;
    }

    // check hit mask (on taken hit or on done hit, but not on spell cast phase)
    if ((eventInfo.GetTypeMask() & TAKEN_HIT_PROC_FLAG_MASK) || ((eventInfo.GetTypeMask() & DONE_HIT_PROC_FLAG_MASK) && !(eventInfo.GetSpellPhaseMask() & PROC_SPELL_PHASE_CAST)))
    {
        uint32 hitMask = procEntry.hitMask;
        // get default values if hit mask not set
        if (!hitMask)
        {
            // for taken procs allow normal + critical hits by default
            if (eventInfo.GetTypeMask() & TAKEN_HIT_PROC_FLAG_MASK)
                hitMask |= PROC_HIT_NORMAL | PROC_HIT_CRITICAL;
            // for done procs allow normal + critical + absorbs by default
            else
                hitMask |= PROC_HIT_NORMAL | PROC_HIT_CRITICAL | PROC_HIT_ABSORB;
        }
        if (!(eventInfo.GetHitMask() & hitMask))
            return false;
    }

    return true;
}

SpellBonusEntry const* SpellMgr::GetSpellBonusData(uint32 spellId) const
{
    // Lookup data
    SpellBonusMap::const_iterator itr = mSpellBonusMap.find(spellId);
    if (itr != mSpellBonusMap.end())
        return &itr->second;
    // Not found, try lookup for 1 spell rank if exist
    if (uint32 rank_1 = GetFirstSpellInChain(spellId))
    {
        SpellBonusMap::const_iterator itr2 = mSpellBonusMap.find(rank_1);
        if (itr2 != mSpellBonusMap.end())
            return &itr2->second;
    }
    return NULL;
}

SpellThreatEntry const* SpellMgr::GetSpellThreatEntry(uint32 spellID) const
{
    SpellThreatMap::const_iterator itr = mSpellThreatMap.find(spellID);
    if (itr != mSpellThreatMap.end())
        return &itr->second;
    else
    {
        uint32 firstSpell = GetFirstSpellInChain(spellID);
        itr = mSpellThreatMap.find(firstSpell);
        if (itr != mSpellThreatMap.end())
            return &itr->second;
    }
    return NULL;
}

SkillLineAbilityMapBounds SpellMgr::GetSkillLineAbilityMapBounds(uint32 spell_id) const
{
    return mSkillLineAbilityMap.equal_range(spell_id);
}

PetAura const* SpellMgr::GetPetAura(uint32 spell_id, uint8 eff)
{
    SpellPetAuraMap::const_iterator itr = mSpellPetAuraMap.find((spell_id<<8) + eff);
    if (itr != mSpellPetAuraMap.end())
        return &itr->second;
    else
        return NULL;
}

SpellEnchantProcEntry const* SpellMgr::GetSpellEnchantProcEvent(uint32 enchId) const
{
    SpellEnchantProcEventMap::const_iterator itr = mSpellEnchantProcEventMap.find(enchId);
    if (itr != mSpellEnchantProcEventMap.end())
        return &itr->second;
    return NULL;
}

bool SpellMgr::IsArenaAllowedEnchancment(uint32 ench_id) const
{
    return mEnchantCustomAttr[ench_id];
}

const std::vector<int32>* SpellMgr::GetSpellLinked(int32 spell_id) const
{
    SpellLinkedMap::const_iterator itr = mSpellLinkedMap.find(spell_id);
    return itr != mSpellLinkedMap.end() ? &(itr->second) : NULL;
}

PetLevelupSpellSet const* SpellMgr::GetPetLevelupSpellList(uint32 petFamily) const
{
    PetLevelupSpellMap::const_iterator itr = mPetLevelupSpellMap.find(petFamily);
    if (itr != mPetLevelupSpellMap.end())
        return &itr->second;
    else
        return NULL;
}

PetDefaultSpellsEntry const* SpellMgr::GetPetDefaultSpellsEntry(int32 id) const
{
    PetDefaultSpellsMap::const_iterator itr = mPetDefaultSpellsMap.find(id);
    if (itr != mPetDefaultSpellsMap.end())
        return &itr->second;
    return NULL;
}

SpellAreaMapBounds SpellMgr::GetSpellAreaMapBounds(uint32 spell_id) const
{
    return mSpellAreaMap.equal_range(spell_id);
}

SpellAreaForQuestMapBounds SpellMgr::GetSpellAreaForQuestMapBounds(uint32 quest_id) const
{
    return mSpellAreaForQuestMap.equal_range(quest_id);
}

SpellAreaForQuestMapBounds SpellMgr::GetSpellAreaForQuestEndMapBounds(uint32 quest_id) const
{
    return mSpellAreaForQuestEndMap.equal_range(quest_id);
}

SpellAreaForAuraMapBounds SpellMgr::GetSpellAreaForAuraMapBounds(uint32 spell_id) const
{
    return mSpellAreaForAuraMap.equal_range(spell_id);
}

SpellAreaForAreaMapBounds SpellMgr::GetSpellAreaForAreaMapBounds(uint32 area_id) const
{
    return mSpellAreaForAreaMap.equal_range(area_id);
}

bool SpellArea::IsFitToRequirements(Player const* player, uint32 newZone, uint32 newArea) const
{
    if (gender != GENDER_NONE)                   // not in expected gender
        if (!player || gender != player->getGender())
            return false;

    if (raceMask)                                // not in expected race
        if (!player || !(raceMask & player->getRaceMask()))
            return false;

    if (areaId)                                  // not in expected zone
        if (newZone != areaId && newArea != areaId)
            return false;

    if (questStart)                              // not in expected required quest state
        if (!player || (((1 << player->GetQuestStatus(questStart)) & questStartStatus) == 0))
            return false;

    if (questEnd)                                // not in expected forbidden quest state
        if (!player || (((1 << player->GetQuestStatus(questEnd)) & questEndStatus) == 0))
            return false;

    if (auraSpell)                               // not have expected aura
        if (!player || (auraSpell > 0 && !player->HasAura(auraSpell)) || (auraSpell < 0 && player->HasAura(-auraSpell)))
            return false;

    // Extra conditions -- leaving the possibility add extra conditions...
    switch (spellId)
    {
        case 91604: // No fly Zone - Wintergrasp
        {
            if (!player)
                return false;

            Battlefield* Bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetZoneId());
            if (!Bf || Bf->CanFlyIn() || (!player->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !player->HasAuraType(SPELL_AURA_FLY)))
                return false;
            break;
        }
        case 68719: // Oil Refinery - Isle of Conquest.
        case 68720: // Quarry - Isle of Conquest.
        {
            if (!player || player->GetBattlegroundTypeId() != BATTLEGROUND_IC || !player->GetBattleground())
                return false;

            uint8 nodeType = spellId == 68719 ? NODE_TYPE_REFINERY : NODE_TYPE_QUARRY;
            uint8 nodeState = player->GetTeamId() == TEAM_ALLIANCE ? NODE_STATE_CONTROLLED_A : NODE_STATE_CONTROLLED_H;

            BattlegroundIC* pIC = static_cast<BattlegroundIC*>(player->GetBattleground());
            if (pIC->GetNodeState(nodeType) == nodeState)
                return true;

            return false;
        }
        case 56618: // Horde Controls Factory Phase Shift
        case 56617: // Alliance Controls Factory Phase Shift
            {
                if (!player)
                    return false;

                Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetZoneId());

                if (!bf || bf->GetTypeId() != BATTLEFIELD_WG)
                    return false;

                // team that controls the workshop in the specified area
                uint32 team = bf->GetData(newArea);

                if (team == TEAM_HORDE)
                    return spellId == 56618;
                else if (team == TEAM_ALLIANCE)
                    return spellId == 56617;
            }
            break;
        case 57940: // Essence of Wintergrasp - Northrend
        case 58045: // Essence of Wintergrasp - Wintergrasp
        {
            if (!player)
                return false;

            if (Battlefield* battlefieldWG = sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG))
                return battlefieldWG->IsEnabled() && (player->GetTeamId() == battlefieldWG->GetDefenderTeam()) && !battlefieldWG->IsWarTime();
            break;
        }
        case 74411: // Battleground - Dampening
        {
            if (!player)
                return false;

            if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetZoneId()))
                return bf->IsWarTime();
            break;
        }

    }

    return true;
}

void SpellMgr::LoadSpellRanks()
{
    uint32 oldMSTime = getMSTime();

    // cleanup core data before reload - remove reference to ChainNode from SpellInfo
    for (SpellChainMap::iterator itr = mSpellChains.begin(); itr != mSpellChains.end(); ++itr)
    {
        mSpellInfoMap[itr->first]->ChainEntry = NULL;
    }
    mSpellChains.clear();
    //                                                     0             1      2
    QueryResult result = WorldDatabase.Query("SELECT first_spell_id, spell_id, rank from spell_ranks ORDER BY first_spell_id, rank");

    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell rank records. DB table `spell_ranks` is empty.");

        return;
    }

    uint32 count = 0;
    bool finished = false;

    do
    {
                        // spellid, rank
        std::list < std::pair < int32, int32 > > rankChain;
        int32 currentSpell = -1;
        int32 lastSpell = -1;

        // fill one chain
        while (currentSpell == lastSpell && !finished)
        {
            Field* fields = result->Fetch();

            currentSpell = fields[0].GetUInt32();
            if (lastSpell == -1)
                lastSpell = currentSpell;
            uint32 spell_id = fields[1].GetUInt32();
            uint32 rank = fields[2].GetUInt8();

            // don't drop the row if we're moving to the next rank
            if (currentSpell == lastSpell)
            {
                rankChain.push_back(std::make_pair(spell_id, rank));
                if (!result->NextRow())
                    finished = true;
            }
            else
                break;
        }
        // check if chain is made with valid first spell
        SpellInfo const* first = GetSpellInfo(lastSpell);
        if (!first)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell rank identifier(first_spell_id) %u listed in `spell_ranks` does not exist!", lastSpell);
            continue;
        }
        // check if chain is long enough
        if (rankChain.size() < 2)
        {
            sLog->outError(LOG_FILTER_SQL, "There is only 1 spell rank for identifier(first_spell_id) %u in `spell_ranks`, entry is not needed!", lastSpell);
            continue;
        }
        int32 curRank = 0;
        bool valid = true;
        // check spells in chain
        for (std::list<std::pair<int32, int32> >::iterator itr = rankChain.begin(); itr!= rankChain.end(); ++itr)
        {
            SpellInfo const* spell = GetSpellInfo(itr->first);
            if (!spell)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u (rank %u) listed in `spell_ranks` for chain %u does not exist!", itr->first, itr->second, lastSpell);
                valid = false;
                break;
            }
            ++curRank;
            if (itr->second != curRank)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u (rank %u) listed in `spell_ranks` for chain %u does not have proper rank value(should be %u)!", itr->first, itr->second, lastSpell, curRank);
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;
        int32 prevRank = 0;
        // insert the chain
        std::list<std::pair<int32, int32> >::iterator itr = rankChain.begin();
        do
        {
            ++count;
            int32 addedSpell = itr->first;
            mSpellChains[addedSpell].first = GetSpellInfo(lastSpell);
            mSpellChains[addedSpell].last = GetSpellInfo(rankChain.back().first);
            mSpellChains[addedSpell].rank = itr->second;
            mSpellChains[addedSpell].prev = GetSpellInfo(prevRank);
            mSpellInfoMap[addedSpell]->ChainEntry = &mSpellChains[addedSpell];
            prevRank = addedSpell;
            ++itr;
            if (itr == rankChain.end())
            {
                mSpellChains[addedSpell].next = NULL;
                break;
            }
            else
                mSpellChains[addedSpell].next = GetSpellInfo(itr->first);
        }
        while (true);
    } while (!finished);

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell rank records in %u ms", count, GetMSTimeDiffToNow(oldMSTime));

}

void SpellMgr::LoadSpellRequired()
{
    uint32 oldMSTime = getMSTime();

    mSpellsReqSpell.clear();                                   // need for reload case
    mSpellReq.clear();                                         // need for reload case

    //                                                   0        1
    QueryResult result = WorldDatabase.Query("SELECT spell_id, req_spell from spell_required");

    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell required records. DB table `spell_required` is empty.");

        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell_id = fields[0].GetUInt32();
        uint32 spell_req = fields[1].GetUInt32();

        // check if chain is made with valid first spell
        SpellInfo const* spell = GetSpellInfo(spell_id);
        if (!spell)
        {
            sLog->outError(LOG_FILTER_SQL, "spell_id %u in `spell_required` table is not found in dbcs, skipped", spell_id);
            continue;
        }

        SpellInfo const* req_spell = GetSpellInfo(spell_req);
        if (!req_spell)
        {
            sLog->outError(LOG_FILTER_SQL, "req_spell %u in `spell_required` table is not found in dbcs, skipped", spell_req);
            continue;
        }

        if (GetFirstSpellInChain(spell_id) == GetFirstSpellInChain(spell_req))
        {
            sLog->outError(LOG_FILTER_SQL, "req_spell %u and spell_id %u in `spell_required` table are ranks of the same spell, entry not needed, skipped", spell_req, spell_id);
            continue;
        }

        if (IsSpellRequiringSpell(spell_id, spell_req))
        {
            sLog->outError(LOG_FILTER_SQL, "duplicated entry of req_spell %u and spell_id %u in `spell_required`, skipped", spell_req, spell_id);
            continue;
        }

        mSpellReq.insert (std::pair<uint32, uint32>(spell_id, spell_req));
        mSpellsReqSpell.insert (std::pair<uint32, uint32>(spell_req, spell_id));
        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell required records in %u ms", count, GetMSTimeDiffToNow(oldMSTime));

}

void SpellMgr::LoadSpellLearnSkills()
{
    uint32 oldMSTime = getMSTime();

    mSpellLearnSkills.clear();                              // need for reload case

    // search auto-learned skills and add its to map also for use in unlearn spells/talents
    uint32 dbc_count = 0;
    for (uint32 spell = 0; spell < GetSpellInfoStoreSize(); ++spell)
    {
        SpellInfo const* entry = GetSpellInfo(spell);

        if (!entry)
            continue;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (entry->Effects[i].Effect == SPELL_EFFECT_SKILL)
            {
                SpellLearnSkillNode dbc_node;
                dbc_node.skill = entry->Effects[i].MiscValue;
                dbc_node.step  = entry->Effects[i].CalcValue();
                if (dbc_node.skill != SKILL_RIDING)
                    dbc_node.value = 1;
                else
                    dbc_node.value = dbc_node.step * 75;
                dbc_node.maxvalue = dbc_node.step * 75;
                mSpellLearnSkills[spell] = dbc_node;
                ++dbc_count;
                break;
            }
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u Spell Learn Skills from DBC in %u ms", dbc_count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellLearnSpells()
{
    uint32 oldMSTime = getMSTime();

    mSpellLearnSpells.clear();                              // need for reload case

    //                                                  0      1        2
    QueryResult result = WorldDatabase.Query("SELECT entry, SpellID, Active FROM spell_learn_spell");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell learn spells. DB table `spell_learn_spell` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell_id = fields[0].GetUInt32();

        SpellLearnSpellNode node;
        node.spell       = fields[1].GetUInt32();
        node.active      = fields[2].GetBool();
        node.autoLearned = false;

        if (!GetSpellInfo(spell_id))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_learn_spell` does not exist", spell_id);
            continue;
        }

        if (!GetSpellInfo(node.spell))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_learn_spell` learning not existed spell %u", spell_id, node.spell);
            continue;
        }

        if (GetTalentSpellCost(node.spell))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_learn_spell` attempt learning talent spell %u, skipped", spell_id, node.spell);
            continue;
        }

        mSpellLearnSpells.insert(SpellLearnSpellMap::value_type(spell_id, node));

        ++count;
    } while (result->NextRow());

    // search auto-learned spells and add its to map also for use in unlearn spells/talents
    uint32 dbc_count = 0;
    for (uint32 spell = 0; spell < GetSpellInfoStoreSize(); ++spell)
    {
        SpellInfo const* entry = GetSpellInfo(spell);

        if (!entry)
            continue;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (entry->Effects[i].Effect == SPELL_EFFECT_LEARN_SPELL)
            {
                SpellLearnSpellNode dbc_node;
                dbc_node.spell = entry->Effects[i].TriggerSpell;
                dbc_node.active = true;                     // all dbc based learned spells is active (show in spell book or hide by client itself)

                // ignore learning not existed spells (broken/outdated/or generic learnig spell 483
                if (!GetSpellInfo(dbc_node.spell))
                    continue;

                // talent or passive spells or skill-step spells auto-casted and not need dependent learning,
                // pet teaching spells must not be dependent learning (casted)
                // other required explicit dependent learning
                dbc_node.autoLearned = entry->Effects[i].TargetA.GetTarget() == TARGET_UNIT_PET || GetTalentSpellCost(spell) > 0 || entry->IsPassive() || entry->HasEffect(SPELL_EFFECT_SKILL_STEP);

                SpellLearnSpellMapBounds db_node_bounds = GetSpellLearnSpellMapBounds(spell);

                bool found = false;
                for (SpellLearnSpellMap::const_iterator itr = db_node_bounds.first; itr != db_node_bounds.second; ++itr)
                {
                    if (itr->second.spell == dbc_node.spell)
                    {
                        sLog->outError(LOG_FILTER_SQL, "Spell %u auto-learn spell %u in spell.dbc then the record in `spell_learn_spell` is redundant, please fix DB.",
                            spell, dbc_node.spell);
                        found = true;
                        break;
                    }
                }

                if (!found)                                  // add new spell-spell pair if not found
                {
                    mSpellLearnSpells.insert(SpellLearnSpellMap::value_type(spell, dbc_node));
                    ++dbc_count;
                }
            }
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell learn spells + %u found in DBC in %u ms", count, dbc_count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellTargetPositions()
{
    uint32 oldMSTime = getMSTime();

    mSpellTargetPositions.clear();                                // need for reload case

    //                                                0      1              2                  3                  4                  5
    QueryResult result = WorldDatabase.Query("SELECT id, target_map, target_position_x, target_position_y, target_position_z, target_orientation FROM spell_target_position");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell target coordinates. DB table `spell_target_position` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 Spell_ID = fields[0].GetUInt32();

        SpellTargetPosition st;

        st.target_mapId       = fields[1].GetUInt16();
        st.target_X           = fields[2].GetFloat();
        st.target_Y           = fields[3].GetFloat();
        st.target_Z           = fields[4].GetFloat();
        st.target_Orientation = fields[5].GetFloat();

        MapEntry const* mapEntry = sMapStore.LookupEntry(st.target_mapId);
        if (!mapEntry)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell (ID:%u) target map (ID: %u) does not exist in `Map.dbc`.", Spell_ID, st.target_mapId);
            continue;
        }

        if (st.target_X==0 && st.target_Y==0 && st.target_Z==0)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell (ID:%u) target coordinates not provided.", Spell_ID);
            continue;
        }

        SpellInfo const* spellInfo = GetSpellInfo(Spell_ID);
        if (!spellInfo)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell (ID:%u) listed in `spell_target_position` does not exist.", Spell_ID);
            continue;
        }

        bool found = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].TargetA.GetTarget() == TARGET_DEST_DB || spellInfo->Effects[i].TargetB.GetTarget() == TARGET_DEST_DB)
            {
                // additional requirements
                if (spellInfo->Effects[i].Effect == SPELL_EFFECT_BIND && spellInfo->Effects[i].MiscValue)
                {
                    uint32 area_id = sMapMgr->GetAreaId(st.target_mapId, st.target_X, st.target_Y, st.target_Z);
                    if (area_id != uint32(spellInfo->Effects[i].MiscValue))
                    {
                        sLog->outError(LOG_FILTER_SQL, "Spell (Id: %u) listed in `spell_target_position` expected point to zone %u bit point to zone %u.", Spell_ID, spellInfo->Effects[i].MiscValue, area_id);
                        break;
                    }
                }

                found = true;
                break;
            }
        }
        if (!found)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell (Id: %u) listed in `spell_target_position` does not have target TARGET_DEST_DB (17).", Spell_ID);
            continue;
        }

        mSpellTargetPositions[Spell_ID] = st;
        ++count;

    } while (result->NextRow());

    /*
    // Check all spells
    for (uint32 i = 1; i < GetSpellInfoStoreSize; ++i)
    {
        SpellInfo const* spellInfo = GetSpellInfo(i);
        if (!spellInfo)
            continue;

        bool found = false;
        for (int j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            switch (spellInfo->Effects[j].TargetA)
            {
                case TARGET_DEST_DB:
                    found = true;
                    break;
            }
            if (found)
                break;
            switch (spellInfo->Effects[j].TargetB)
            {
                case TARGET_DEST_DB:
                    found = true;
                    break;
            }
            if (found)
                break;
        }
        if (found)
        {
            if (!sSpellMgr->GetSpellTargetPosition(i))
                sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "Spell (ID: %u) does not have record in `spell_target_position`", i);
        }
    }*/

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell teleport coordinates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellGroups()
{
    uint32 oldMSTime = getMSTime();

    mSpellSpellGroup.clear();                                  // need for reload case
    mSpellGroupSpell.clear();

    //                                                0     1
    QueryResult result = WorldDatabase.Query("SELECT id, spell_id FROM spell_group");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell group definitions. DB table `spell_group` is empty.");
        return;
    }

    std::set<uint32> groups;
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 group_id = fields[0].GetUInt32();
        if (group_id <= SPELL_GROUP_DB_RANGE_MIN && group_id >= SPELL_GROUP_CORE_RANGE_MAX)
        {
            sLog->outError(LOG_FILTER_SQL, "SpellGroup id %u listed in `spell_group` is in core range, but is not defined in core!", group_id);
            continue;
        }
        int32 spell_id = fields[1].GetInt32();

        groups.insert(std::set<uint32>::value_type(group_id));
        mSpellGroupSpell.insert(SpellGroupSpellMap::value_type((SpellGroup)group_id, spell_id));

    } while (result->NextRow());

    for (SpellGroupSpellMap::iterator itr = mSpellGroupSpell.begin(); itr!= mSpellGroupSpell.end();)
    {
        if (itr->second < 0)
        {
            if (groups.find(abs(itr->second)) == groups.end())
            {
                sLog->outError(LOG_FILTER_SQL, "SpellGroup id %u listed in `spell_group` does not exist", abs(itr->second));
                mSpellGroupSpell.erase(itr++);
            }
            else
                ++itr;
        }
        else
        {
            SpellInfo const* spellInfo = GetSpellInfo(itr->second);

            if (!spellInfo)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_group` does not exist", itr->second);
                mSpellGroupSpell.erase(itr++);
            }
            else if (spellInfo->GetRank() > 1)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_group` is not first rank of spell", itr->second);
                mSpellGroupSpell.erase(itr++);
            }
            else
                ++itr;
        }
    }

    for (std::set<uint32>::iterator groupItr = groups.begin(); groupItr != groups.end(); ++groupItr)
    {
        std::set<uint32> spells;
        GetSetOfSpellsInSpellGroup(SpellGroup(*groupItr), spells);

        for (std::set<uint32>::iterator spellItr = spells.begin(); spellItr != spells.end(); ++spellItr)
        {
            ++count;
            mSpellSpellGroup.insert(SpellSpellGroupMap::value_type(*spellItr, SpellGroup(*groupItr)));
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell group definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellGroupStackRules()
{
    uint32 oldMSTime = getMSTime();

    mSpellGroupStack.clear();                                  // need for reload case

    //                                                       0         1
    QueryResult result = WorldDatabase.Query("SELECT group_id, stack_rule FROM spell_group_stack_rules");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell group stack rules. DB table `spell_group_stack_rules` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 group_id = fields[0].GetUInt32();
        uint8 stack_rule = fields[1].GetInt8();
        if (stack_rule >= SPELL_GROUP_STACK_RULE_MAX)
        {
            sLog->outError(LOG_FILTER_SQL, "SpellGroupStackRule %u listed in `spell_group_stack_rules` does not exist", stack_rule);
            continue;
        }

        SpellGroupSpellMapBounds spellGroup = GetSpellGroupSpellMapBounds((SpellGroup)group_id);

        if (spellGroup.first == spellGroup.second)
        {
            sLog->outError(LOG_FILTER_SQL, "SpellGroup id %u listed in `spell_group_stack_rules` does not exist", group_id);
            continue;
        }

        mSpellGroupStack[(SpellGroup)group_id] = (SpellGroupStackRule)stack_rule;

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell group stack rules in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellProcEvents()
{
    uint32 oldMSTime = getMSTime();

    mSpellProcEventMap.clear();                             // need for reload case

    //                                                0      1           2                3                 4                 5                 6          7       8        9             10
    QueryResult result = WorldDatabase.Query("SELECT entry, SchoolMask, SpellFamilyName, SpellFamilyMask0, SpellFamilyMask1, SpellFamilyMask2, procFlags, procEx, ppmRate, CustomChance, Cooldown FROM spell_proc_event");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell proc event conditions. DB table `spell_proc_event` is empty.");
        return;
    }

    uint32 count = 0;
    uint32 customProc = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        SpellInfo const* spell = GetSpellInfo(entry);
        if (!spell)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_proc_event` does not exist", entry);
            continue;
        }

        SpellProcEventEntry spe;

        spe.schoolMask      = fields[1].GetInt8();
        spe.spellFamilyName = fields[2].GetUInt16();
        spe.spellFamilyMask[0] = fields[3].GetUInt32();
        spe.spellFamilyMask[1] = fields[4].GetUInt32();
        spe.spellFamilyMask[2] = fields[5].GetUInt32();
        spe.procFlags       = fields[6].GetUInt32();
        spe.procEx          = fields[7].GetUInt32();
        spe.ppmRate         = fields[8].GetFloat();
        spe.customChance    = fields[9].GetFloat();
        spe.cooldown        = fields[10].GetUInt32();

        mSpellProcEventMap[entry] = spe;

        if (spell->ProcFlags == 0)
        {
            if (spe.procFlags == 0)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_proc_event` probally not triggered spell", entry);
                continue;
            }
            customProc++;
        }
        ++count;
    } while (result->NextRow());

    if (customProc)
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u extra and %u custom spell proc event conditions in %u ms",  count, customProc, GetMSTimeDiffToNow(oldMSTime));
    else
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u extra spell proc event conditions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));

}

void SpellMgr::LoadSpellProcs()
{
    uint32 oldMSTime = getMSTime();

    mSpellProcMap.clear();                             // need for reload case

    //                                                 0        1           2                3                 4                 5                 6         7              8               9        10              11             12      13        14
    QueryResult result = WorldDatabase.Query("SELECT spellId, schoolMask, spellFamilyName, spellFamilyMask0, spellFamilyMask1, spellFamilyMask2, typeMask, spellTypeMask, spellPhaseMask, hitMask, attributesMask, ratePerMinute, chance, cooldown, charges FROM spell_proc");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell proc conditions and data. DB table `spell_proc` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        int32 spellId = fields[0].GetInt32();

        bool allRanks = false;
        if (spellId <=0)
        {
            allRanks = true;
            spellId = -spellId;
        }

        SpellInfo const* spellEntry = GetSpellInfo(spellId);
        if (!spellEntry)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_proc` does not exist", spellId);
            continue;
        }

        if (allRanks)
        {
            if (GetFirstSpellInChain(spellId) != uint32(spellId))
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_proc` is not first rank of spell.", fields[0].GetInt32());
                continue;
            }
        }

        SpellProcEntry baseProcEntry;

        baseProcEntry.schoolMask      = fields[1].GetInt8();
        baseProcEntry.spellFamilyName = fields[2].GetUInt16();
        baseProcEntry.spellFamilyMask[0] = fields[3].GetUInt32();
        baseProcEntry.spellFamilyMask[1] = fields[4].GetUInt32();
        baseProcEntry.spellFamilyMask[2] = fields[5].GetUInt32();
        baseProcEntry.typeMask        = fields[6].GetUInt32();
        baseProcEntry.spellTypeMask   = fields[7].GetUInt32();
        baseProcEntry.spellPhaseMask  = fields[8].GetUInt32();
        baseProcEntry.hitMask         = fields[9].GetUInt32();
        baseProcEntry.attributesMask  = fields[10].GetUInt32();
        baseProcEntry.ratePerMinute   = fields[11].GetFloat();
        baseProcEntry.chance          = fields[12].GetFloat();
        float cooldown                = fields[13].GetFloat();
        baseProcEntry.cooldown        = uint32(cooldown);
        baseProcEntry.charges         = fields[14].GetUInt32();

        while (true)
        {
            if (mSpellProcMap.find(spellId) != mSpellProcMap.end())
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_proc` has duplicate entry in the table", spellId);
                break;
            }
            SpellProcEntry procEntry = SpellProcEntry(baseProcEntry);

            // take defaults from dbcs
            if (!procEntry.typeMask)
                procEntry.typeMask = spellEntry->ProcFlags;
            if (!procEntry.charges)
                procEntry.charges = spellEntry->ProcCharges;
            if (!procEntry.chance && !procEntry.ratePerMinute)
                procEntry.chance = float(spellEntry->ProcChance);

            // validate data
            if (procEntry.schoolMask & ~SPELL_SCHOOL_MASK_ALL)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has wrong `schoolMask` set: %u", spellId, procEntry.schoolMask);
            if (procEntry.spellFamilyName && (procEntry.spellFamilyName < 3 || procEntry.spellFamilyName > 17 || procEntry.spellFamilyName == 14 || procEntry.spellFamilyName == 16))
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has wrong `spellFamilyName` set: %u", spellId, procEntry.spellFamilyName);
            if (procEntry.chance < 0)
            {
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has negative value in `chance` field", spellId);
                procEntry.chance = 0;
            }
            if (procEntry.ratePerMinute < 0)
            {
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has negative value in `ratePerMinute` field", spellId);
                procEntry.ratePerMinute = 0;
            }
            if (cooldown < 0)
            {
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has negative value in `cooldown` field", spellId);
                procEntry.cooldown = 0;
            }
            if (procEntry.chance == 0 && procEntry.ratePerMinute == 0)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u doesn't have `chance` and `ratePerMinute` values defined, proc will not be triggered", spellId);
            if (procEntry.charges > 99)
            {
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has too big value in `charges` field", spellId);
                procEntry.charges = 99;
            }
            if (!procEntry.typeMask)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u doesn't have `typeMask` value defined, proc will not be triggered", spellId);
            if (procEntry.spellTypeMask & ~PROC_SPELL_PHASE_MASK_ALL)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has wrong `spellTypeMask` set: %u", spellId, procEntry.spellTypeMask);
            if (procEntry.spellTypeMask && !(procEntry.typeMask & (SPELL_PROC_FLAG_MASK | PERIODIC_PROC_FLAG_MASK)))
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has `spellTypeMask` value defined, but it won't be used for defined `typeMask` value", spellId);
            if (!procEntry.spellPhaseMask && procEntry.typeMask & REQ_SPELL_PHASE_PROC_FLAG_MASK)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u doesn't have `spellPhaseMask` value defined, but it's required for defined `typeMask` value, proc will not be triggered", spellId);
            if (procEntry.spellPhaseMask & ~PROC_SPELL_PHASE_MASK_ALL)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has wrong `spellPhaseMask` set: %u", spellId, procEntry.spellPhaseMask);
            if (procEntry.spellPhaseMask && !(procEntry.typeMask & REQ_SPELL_PHASE_PROC_FLAG_MASK))
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has `spellPhaseMask` value defined, but it won't be used for defined `typeMask` value", spellId);
            if (procEntry.hitMask & ~PROC_HIT_MASK_ALL)
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has wrong `hitMask` set: %u", spellId, procEntry.hitMask);
            if (procEntry.hitMask && !(procEntry.typeMask & TAKEN_HIT_PROC_FLAG_MASK || (procEntry.typeMask & DONE_HIT_PROC_FLAG_MASK && (!procEntry.spellPhaseMask || procEntry.spellPhaseMask & (PROC_SPELL_PHASE_HIT | PROC_SPELL_PHASE_FINISH)))))
                sLog->outError(LOG_FILTER_SQL, "`spell_proc` table entry for spellId %u has `hitMask` value defined, but it won't be used for defined `typeMask` and `spellPhaseMask` values", spellId);

            mSpellProcMap[spellId] = procEntry;

            if (allRanks)
            {
                spellId = GetNextSpellInChain(spellId);
                spellEntry = GetSpellInfo(spellId);
            }
            else
                break;
        }
        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell proc conditions and data in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellBonusess()
{
    uint32 oldMSTime = getMSTime();

    mSpellBonusMap.clear();                             // need for reload case

    //                                                0      1             2          3         4
    QueryResult result = WorldDatabase.Query("SELECT entry, direct_bonus, dot_bonus, ap_bonus, ap_dot_bonus FROM spell_bonus_data");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell bonus data. DB table `spell_bonus_data` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();

        SpellInfo const* spell = GetSpellInfo(entry);
        if (!spell)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_bonus_data` does not exist", entry);
            continue;
        }

        SpellBonusEntry& sbe = mSpellBonusMap[entry];
        sbe.direct_damage = fields[1].GetFloat();
        sbe.dot_damage    = fields[2].GetFloat();
        sbe.ap_bonus      = fields[3].GetFloat();
        sbe.ap_dot_bonus   = fields[4].GetFloat();

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u extra spell bonus data in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellThreats()
{
    uint32 oldMSTime = getMSTime();

    mSpellThreatMap.clear();                                // need for reload case

    //                                                0      1        2       3
    QueryResult result = WorldDatabase.Query("SELECT entry, flatMod, pctMod, apPctMod FROM spell_threat");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 aggro generating spells. DB table `spell_threat` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!GetSpellInfo(entry))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_threat` does not exist", entry);
            continue;
        }

        SpellThreatEntry ste;
        ste.flatMod  = fields[1].GetInt32();
        ste.pctMod   = fields[2].GetFloat();
        ste.apPctMod = fields[3].GetFloat();

        mSpellThreatMap[entry] = ste;
        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u SpellThreatEntries in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSkillLineAbilityMap()
{
    uint32 oldMSTime = getMSTime();

    mSkillLineAbilityMap.clear();

    uint32 count = 0;

    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* SkillInfo = sSkillLineAbilityStore.LookupEntry(i);
        if (!SkillInfo)
            continue;

        mSkillLineAbilityMap.insert(SkillLineAbilityMap::value_type(SkillInfo->spellId, SkillInfo));
        ++count;
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u SkillLineAbility MultiMap Data in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellPetAuras()
{
    uint32 oldMSTime = getMSTime();

    mSpellPetAuraMap.clear();                                  // need for reload case

    //                                                  0       1       2    3
    QueryResult result = WorldDatabase.Query("SELECT spell, effectId, pet, aura FROM spell_pet_auras");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell pet auras. DB table `spell_pet_auras` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell = fields[0].GetUInt32();
        uint8 eff = fields[1].GetUInt8();
        uint32 pet = fields[2].GetUInt32();
        uint32 aura = fields[3].GetUInt32();

        SpellPetAuraMap::iterator itr = mSpellPetAuraMap.find((spell<<8) + eff);
        if (itr != mSpellPetAuraMap.end())
            itr->second.AddAura(pet, aura);
        else
        {
            SpellInfo const* spellInfo = GetSpellInfo(spell);
            if (!spellInfo)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_pet_auras` does not exist", spell);
                continue;
            }
            if (spellInfo->Effects[eff].Effect != SPELL_EFFECT_DUMMY &&
               (spellInfo->Effects[eff].Effect != SPELL_EFFECT_APPLY_AURA ||
                spellInfo->Effects[eff].ApplyAuraName != SPELL_AURA_DUMMY))
            {
                sLog->outError(LOG_FILTER_SPELLS_AURAS, "Spell %u listed in `spell_pet_auras` does not have dummy aura or dummy effect", spell);
                continue;
            }

            SpellInfo const* spellInfo2 = GetSpellInfo(aura);
            if (!spellInfo2)
            {
                sLog->outError(LOG_FILTER_SQL, "Aura %u listed in `spell_pet_auras` does not exist", aura);
                continue;
            }

            PetAura pa(pet, aura, spellInfo->Effects[eff].TargetA.GetTarget() == TARGET_UNIT_PET, spellInfo->Effects[eff].CalcValue());
            mSpellPetAuraMap[(spell<<8) + eff] = pa;
        }

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell pet auras in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

// Fill custom data about enchancments
void SpellMgr::LoadEnchantCustomAttr()
{
    uint32 oldMSTime = getMSTime();

    uint32 size = sSpellItemEnchantmentStore.GetNumRows();
    mEnchantCustomAttr.resize(size);

    for (uint32 i = 0; i < size; ++i)
       mEnchantCustomAttr[i] = 0;

    uint32 count = 0;
    for (uint32 i = 0; i < GetSpellInfoStoreSize(); ++i)
    {
        SpellInfo const* spellInfo = GetSpellInfo(i);
        if (!spellInfo)
            continue;

        // TODO: find a better check
        if (!(spellInfo->AttributesEx2 & SPELL_ATTR2_PRESERVE_ENCHANT_IN_ARENA) || !(spellInfo->Attributes & SPELL_ATTR0_NOT_SHAPESHIFT))
            continue;

        for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            if (spellInfo->Effects[j].Effect == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY)
            {
                uint32 enchId = spellInfo->Effects[j].MiscValue;
                SpellItemEnchantmentEntry const* ench = sSpellItemEnchantmentStore.LookupEntry(enchId);
                if (!ench)
                    continue;
                mEnchantCustomAttr[enchId] = true;
                ++count;
                break;
            }
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u custom enchant attributes in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellEnchantProcData()
{
    uint32 oldMSTime = getMSTime();

    mSpellEnchantProcEventMap.clear();                             // need for reload case

    //                                                  0         1           2         3
    QueryResult result = WorldDatabase.Query("SELECT entry, customChance, PPMChance, procEx FROM spell_enchant_proc_data");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell enchant proc event conditions. DB table `spell_enchant_proc_data` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 enchantId = fields[0].GetUInt32();

        SpellItemEnchantmentEntry const* ench = sSpellItemEnchantmentStore.LookupEntry(enchantId);
        if (!ench)
        {
            sLog->outError(LOG_FILTER_SQL, "Enchancment %u listed in `spell_enchant_proc_data` does not exist", enchantId);
            continue;
        }

        SpellEnchantProcEntry spe;

        spe.customChance = fields[1].GetUInt32();
        spe.PPMChance = fields[2].GetFloat();
        spe.procEx = fields[3].GetUInt32();

        mSpellEnchantProcEventMap[enchantId] = spe;

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u enchant proc data definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellLinked()
{
    uint32 oldMSTime = getMSTime();

    mSpellLinkedMap.clear();    // need for reload case

    //                                                0              1             2
    QueryResult result = WorldDatabase.Query("SELECT spell_trigger, spell_effect, type FROM spell_linked_spell");
    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 linked spells. DB table `spell_linked_spell` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        int32 trigger = fields[0].GetInt32();
        int32 effect = fields[1].GetInt32();
        int32 type = fields[2].GetUInt8();

        SpellInfo const* spellInfo = GetSpellInfo(abs(trigger));
        if (!spellInfo)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_linked_spell` does not exist", abs(trigger));
            continue;
        }
        spellInfo = GetSpellInfo(abs(effect));
        if (!spellInfo)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_linked_spell` does not exist", abs(effect));
            continue;
        }

        if (type) //we will find a better way when more types are needed
        {
            if (trigger > 0)
                trigger += SPELL_LINKED_MAX_SPELLS * type;
            else
                trigger -= SPELL_LINKED_MAX_SPELLS * type;
        }
        mSpellLinkedMap[trigger].push_back(effect);

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u linked spells in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadPetLevelupSpellMap()
{
    uint32 oldMSTime = getMSTime();

    mPetLevelupSpellMap.clear();                                   // need for reload case

    uint32 count = 0;
    uint32 family_count = 0;

    for (uint32 i = 0; i < sCreatureFamilyStore.GetNumRows(); ++i)
    {
        CreatureFamilyEntry const* creatureFamily = sCreatureFamilyStore.LookupEntry(i);
        if (!creatureFamily)                                     // not exist
            continue;

        for (uint8 j = 0; j < 2; ++j)
        {
            if (!creatureFamily->skillLine[j])
                continue;

            for (uint32 k = 0; k < sSkillLineAbilityStore.GetNumRows(); ++k)
            {
                SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(k);
                if (!skillLine)
                    continue;

                //if (skillLine->skillId != creatureFamily->skillLine[0] &&
                //    (!creatureFamily->skillLine[1] || skillLine->skillId != creatureFamily->skillLine[1]))
                //    continue;

                if (skillLine->skillId != creatureFamily->skillLine[j])
                    continue;

                if (skillLine->learnOnGetSkill != ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL)
                    continue;

                SpellInfo const* spell = GetSpellInfo(skillLine->spellId);
                if (!spell) // not exist or triggered or talent
                    continue;

                if (!spell->SpellLevel)
                    continue;

                PetLevelupSpellSet& spellSet = mPetLevelupSpellMap[creatureFamily->ID];
                if (spellSet.empty())
                    ++family_count;

                spellSet.insert(PetLevelupSpellSet::value_type(spell->SpellLevel, spell->Id));
                ++count;
            }
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u pet levelup and default spells for %u families in %u ms", count, family_count, GetMSTimeDiffToNow(oldMSTime));
}

bool LoadPetDefaultSpells_helper(CreatureTemplate const* cInfo, PetDefaultSpellsEntry& petDefSpells)
{
    // skip empty list;
    bool have_spell = false;
    for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
    {
        if (petDefSpells.spellid[j])
        {
            have_spell = true;
            break;
        }
    }
    if (!have_spell)
        return false;

    // remove duplicates with levelupSpells if any
    if (PetLevelupSpellSet const* levelupSpells = cInfo->family ? sSpellMgr->GetPetLevelupSpellList(cInfo->family) : NULL)
    {
        for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
        {
            if (!petDefSpells.spellid[j])
                continue;

            for (PetLevelupSpellSet::const_iterator itr = levelupSpells->begin(); itr != levelupSpells->end(); ++itr)
            {
                if (itr->second == petDefSpells.spellid[j])
                {
                    petDefSpells.spellid[j] = 0;
                    break;
                }
            }
        }
    }

    // skip empty list;
    have_spell = false;
    for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
    {
        if (petDefSpells.spellid[j])
        {
            have_spell = true;
            break;
        }
    }

    return have_spell;
}

void SpellMgr::LoadPetDefaultSpells()
{
    uint32 oldMSTime = getMSTime();

    mPetDefaultSpellsMap.clear();

    uint32 countCreature = 0;
    uint32 countData = 0;

    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {

        if (!itr->second.PetSpellDataId)
            continue;

        // for creature with PetSpellDataId get default pet spells from dbc
        CreatureSpellDataEntry const* spellDataEntry = sCreatureSpellDataStore.LookupEntry(itr->second.PetSpellDataId);
        if (!spellDataEntry)
            continue;

        int32 petSpellsId = -int32(itr->second.PetSpellDataId);
        PetDefaultSpellsEntry petDefSpells;
        for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
            petDefSpells.spellid[j] = spellDataEntry->spellId[j];

        if (LoadPetDefaultSpells_helper(&itr->second, petDefSpells))
        {
            mPetDefaultSpellsMap[petSpellsId] = petDefSpells;
            ++countData;
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded addition spells for %u pet spell data entries in %u ms", countData, GetMSTimeDiffToNow(oldMSTime));

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, "Loading summonable creature templates...");
    oldMSTime = getMSTime();

    // different summon spells
    for (uint32 i = 0; i < GetSpellInfoStoreSize(); ++i)
    {
        SpellInfo const* spellEntry = GetSpellInfo(i);
        if (!spellEntry)
            continue;

        for (uint8 k = 0; k < MAX_SPELL_EFFECTS; ++k)
        {
            if (spellEntry->Effects[k].Effect == SPELL_EFFECT_SUMMON || spellEntry->Effects[k].Effect == SPELL_EFFECT_SUMMON_PET)
            {
                uint32 creature_id = spellEntry->Effects[k].MiscValue;
                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(creature_id);
                if (!cInfo)
                    continue;

                // already loaded
                if (cInfo->PetSpellDataId)
                    continue;

                // for creature without PetSpellDataId get default pet spells from creature_template
                int32 petSpellsId = cInfo->Entry;
                if (mPetDefaultSpellsMap.find(cInfo->Entry) != mPetDefaultSpellsMap.end())
                    continue;

                PetDefaultSpellsEntry petDefSpells;
                for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
                    petDefSpells.spellid[j] = cInfo->spells[j];

                if (LoadPetDefaultSpells_helper(cInfo, petDefSpells))
                {
                    mPetDefaultSpellsMap[petSpellsId] = petDefSpells;
                    ++countCreature;
                }
            }
        }
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u summonable creature templates in %u ms", countCreature, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellAreas()
{
    uint32 oldMSTime = getMSTime();

    mSpellAreaMap.clear();                                  // need for reload case
    mSpellAreaForQuestMap.clear();
    mSpellAreaForQuestEndMap.clear();
    mSpellAreaForAuraMap.clear();

    //                                                  0     1         2              3               4                 5          6          7       8         9
    QueryResult result = WorldDatabase.Query("SELECT spell, area, quest_start, quest_start_status, quest_end_status, quest_end, aura_spell, racemask, gender, autocast FROM spell_area");

    if (!result)
    {
        sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded 0 spell area requirements. DB table `spell_area` is empty.");

        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell = fields[0].GetUInt32();
        SpellArea spellArea;
        spellArea.spellId             = spell;
        spellArea.areaId              = fields[1].GetUInt32();
        spellArea.questStart          = fields[2].GetUInt32();
        spellArea.questStartStatus    = fields[3].GetUInt32();
        spellArea.questEndStatus      = fields[4].GetUInt32();
        spellArea.questEnd            = fields[5].GetUInt32();
        spellArea.auraSpell           = fields[6].GetInt32();
        spellArea.raceMask            = fields[7].GetUInt32();
        spellArea.gender              = Gender(fields[8].GetUInt8());
        spellArea.autocast            = fields[9].GetBool();

        if (SpellInfo const* spellInfo = GetSpellInfo(spell))
        {
            if (spellArea.autocast)
                const_cast<SpellInfo*>(spellInfo)->Attributes |= SPELL_ATTR0_CANT_CANCEL;
        }
        else
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` does not exist", spell);
            continue;
        }

        {
            bool ok = true;
            SpellAreaMapBounds sa_bounds = GetSpellAreaMapBounds(spellArea.spellId);
            for (SpellAreaMap::const_iterator itr = sa_bounds.first; itr != sa_bounds.second; ++itr)
            {
                if (spellArea.spellId != itr->second.spellId)
                    continue;
                if (spellArea.areaId != itr->second.areaId)
                    continue;
                if (spellArea.questStart != itr->second.questStart)
                    continue;
                if (spellArea.auraSpell != itr->second.auraSpell)
                    continue;
                if ((spellArea.raceMask & itr->second.raceMask) == 0)
                    continue;
                if (spellArea.gender != itr->second.gender)
                    continue;

                // duplicate by requirements
                ok = false;
                break;
            }

            if (!ok)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` already listed with similar requirements.", spell);
                continue;
            }
        }

        if (spellArea.areaId && !GetAreaEntryByAreaID(spellArea.areaId))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong area (%u) requirement", spell, spellArea.areaId);
            continue;
        }

        if (spellArea.questStart && !sObjectMgr->GetQuestTemplate(spellArea.questStart))
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong start quest (%u) requirement", spell, spellArea.questStart);
            continue;
        }

        if (spellArea.questEnd)
        {
            if (!sObjectMgr->GetQuestTemplate(spellArea.questEnd))
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong end quest (%u) requirement", spell, spellArea.questEnd);
                continue;
            }
        }

        if (spellArea.auraSpell)
        {
            SpellInfo const* spellInfo = GetSpellInfo(abs(spellArea.auraSpell));
            if (!spellInfo)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong aura spell (%u) requirement", spell, abs(spellArea.auraSpell));
                continue;
            }

            if (uint32(abs(spellArea.auraSpell)) == spellArea.spellId)
            {
                sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have aura spell (%u) requirement for itself", spell, abs(spellArea.auraSpell));
                continue;
            }

            // not allow autocast chains by auraSpell field (but allow use as alternative if not present)
            if (spellArea.autocast && spellArea.auraSpell > 0)
            {
                bool chain = false;
                SpellAreaForAuraMapBounds saBound = GetSpellAreaForAuraMapBounds(spellArea.spellId);
                for (SpellAreaForAuraMap::const_iterator itr = saBound.first; itr != saBound.second; ++itr)
                {
                    if (itr->second->autocast && itr->second->auraSpell > 0)
                    {
                        chain = true;
                        break;
                    }
                }

                if (chain)
                {
                    sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have aura spell (%u) requirement that itself autocast from aura", spell, spellArea.auraSpell);
                    continue;
                }

                SpellAreaMapBounds saBound2 = GetSpellAreaMapBounds(spellArea.auraSpell);
                for (SpellAreaMap::const_iterator itr2 = saBound2.first; itr2 != saBound2.second; ++itr2)
                {
                    if (itr2->second.autocast && itr2->second.auraSpell > 0)
                    {
                        chain = true;
                        break;
                    }
                }

                if (chain)
                {
                    sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have aura spell (%u) requirement that itself autocast from aura", spell, spellArea.auraSpell);
                    continue;
                }
            }
        }

        if (spellArea.raceMask && (spellArea.raceMask & RACEMASK_ALL_PLAYABLE) == 0)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong race mask (%u) requirement", spell, spellArea.raceMask);
            continue;
        }

        if (spellArea.gender != GENDER_NONE && spellArea.gender != GENDER_FEMALE && spellArea.gender != GENDER_MALE)
        {
            sLog->outError(LOG_FILTER_SQL, "Spell %u listed in `spell_area` have wrong gender (%u) requirement", spell, spellArea.gender);
            continue;
        }

        SpellArea const* sa = &mSpellAreaMap.insert(SpellAreaMap::value_type(spell, spellArea))->second;

        // for search by current zone/subzone at zone/subzone change
        if (spellArea.areaId)
            mSpellAreaForAreaMap.insert(SpellAreaForAreaMap::value_type(spellArea.areaId, sa));

        // for search at quest start/reward
        if (spellArea.questStart)
            mSpellAreaForQuestMap.insert(SpellAreaForQuestMap::value_type(spellArea.questStart, sa));

        // for search at quest start/reward
        if (spellArea.questEnd)
            mSpellAreaForQuestEndMap.insert(SpellAreaForQuestMap::value_type(spellArea.questEnd, sa));

        // for search at aura apply
        if (spellArea.auraSpell)
            mSpellAreaForAuraMap.insert(SpellAreaForAuraMap::value_type(abs(spellArea.auraSpell), sa));

        ++count;
    } while (result->NextRow());

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded %u spell area requirements in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

// Temporary structure to hold spell effect entries for faster loading
struct SpellEffectArray
{
    SpellEffectArray()
    {
        effects[0] = NULL;
        effects[1] = NULL;
        effects[2] = NULL;
    }

    SpellEffectEntry const* effects[MAX_SPELL_EFFECTS];
};

void SpellMgr::LoadSpellInfoStore()
{
    uint32 oldMSTime = getMSTime();

    UnloadSpellInfoStore();
    mSpellInfoMap.resize(sSpellStore.GetNumRows(), NULL);

    std::map<uint32, SpellEffectArray> effectsBySpell;

    for (uint32 i = 0; i < sSpellEffectStore.GetNumRows(); ++i)
    {
        SpellEffectEntry const* effect = sSpellEffectStore.LookupEntry(i);
        if (!effect)
            continue;

        effectsBySpell[effect->EffectSpellId].effects[effect->EffectIndex] = effect;
    }

    for (uint32 i = 0; i < sSpellStore.GetNumRows(); ++i)
        if (SpellEntry const* spellEntry = sSpellStore.LookupEntry(i))
            mSpellInfoMap[i] = new SpellInfo(spellEntry, effectsBySpell[i].effects);

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded spell info store in %u ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::UnloadSpellInfoStore()
{
    for (uint32 i = 0; i < mSpellInfoMap.size(); ++i)
    {
        if (mSpellInfoMap[i])
            delete mSpellInfoMap[i];
    }
    mSpellInfoMap.clear();
}

void SpellMgr::UnloadSpellInfoImplicitTargetConditionLists()
{
    for (uint32 i = 0; i < mSpellInfoMap.size(); ++i)
    {
        if (mSpellInfoMap[i])
            mSpellInfoMap[i]->_UnloadImplicitTargetConditionLists();
    }
}

void SpellMgr::LoadSpellCustomAttr()
{
    uint32 oldMSTime = getMSTime();

    SpellInfo* spellInfo = NULL;
    for (uint32 i = 0; i < GetSpellInfoStoreSize(); ++i)
    {
        spellInfo = mSpellInfoMap[i];
        if (!spellInfo)
            continue;

        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            switch (spellInfo->Effects[j].ApplyAuraName)
            {
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_CONFUSE:
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_AOE_CHARM:
                case SPELL_AURA_MOD_FEAR:
                case SPELL_AURA_MOD_STUN:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
                    break;
                case SPELL_AURA_PERIODIC_HEAL:
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
                case SPELL_AURA_PERIODIC_LEECH:
                case SPELL_AURA_PERIODIC_MANA_LEECH:
                case SPELL_AURA_PERIODIC_HEALTH_FUNNEL:
                case SPELL_AURA_PERIODIC_ENERGIZE:
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_OBS_MOD_POWER:
                case SPELL_AURA_POWER_BURN:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_NO_INITIAL_THREAT;
                    break;
            }

            switch (spellInfo->Effects[j].Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                case SPELL_EFFECT_HEAL:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_DIRECT_DAMAGE;
                    break;
                case SPELL_EFFECT_POWER_DRAIN:
                case SPELL_EFFECT_POWER_BURN:
                case SPELL_EFFECT_HEAL_MAX_HEALTH:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_HEAL_PCT:
                case SPELL_EFFECT_ENERGIZE_PCT:
                case SPELL_EFFECT_ENERGIZE:
                case SPELL_EFFECT_HEAL_MECHANICAL:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_NO_INITIAL_THREAT;
                    break;
                case SPELL_EFFECT_CHARGE:
                case SPELL_EFFECT_CHARGE_DEST:
                case SPELL_EFFECT_JUMP:
                case SPELL_EFFECT_JUMP_DEST:
                case SPELL_EFFECT_LEAP_BACK:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_CHARGE;
                    break;
                case SPELL_EFFECT_PICKPOCKET:
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_PICKPOCKET;
                    break;
                case SPELL_EFFECT_ENCHANT_ITEM:
                case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
                case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
                case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                {
                    // only enchanting profession enchantments procs can stack
                    if (IsPartOfSkillLine(SKILL_ENCHANTING, i))
                    {
                        uint32 enchantId = spellInfo->Effects[j].MiscValue;
                        SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
                        for (uint8 s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
                        {
                            if (enchant->type[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
                                continue;

                            SpellInfo* procInfo = (SpellInfo*)GetSpellInfo(enchant->spellid[s]);
                            if (!procInfo)
                                continue;

                            // if proced directly from enchantment, not via proc aura
                            // NOTE: Enchant Weapon - Blade Ward also has proc aura spell and is proced directly
                            // however its not expected to stack so this check is good
                            if (procInfo->HasAura(SPELL_AURA_PROC_TRIGGER_SPELL))
                                continue;

                            procInfo->AttributesCu |= SPELL_ATTR0_CU_ENCHANT_PROC;
                        }
                    }
                    break;
                }
            }
        }

        if (!spellInfo->_IsPositiveEffect(EFFECT_0, false))
            spellInfo->AttributesCu |= SPELL_ATTR0_CU_NEGATIVE_EFF0;

        if (!spellInfo->_IsPositiveEffect(EFFECT_1, false))
            spellInfo->AttributesCu |= SPELL_ATTR0_CU_NEGATIVE_EFF1;

        if (!spellInfo->_IsPositiveEffect(EFFECT_2, false))
            spellInfo->AttributesCu |= SPELL_ATTR0_CU_NEGATIVE_EFF2;

        if (spellInfo->SpellVisual[0] == 3879)
            spellInfo->AttributesCu |= SPELL_ATTR0_CU_CONE_BACK;

        switch (spellInfo->Id)
        {
            case 60256:
                //Crashes client on pressing ESC (Maybe because of ReqSpellFocus and GameObject)
                spellInfo->AttributesEx4 &= ~SPELL_ATTR4_TRIGGERED;
                break;
            case 1776: // Gouge
            case 1777:
            case 8629:
            case 11285:
            case 11286:
            case 12540:
            case 13579:
            case 24698:
            case 28456:
            case 29425:
            case 34940:
            case 36862:
            case 38764:
            case 38863:
            case 52743: // Head Smack
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_REQ_TARGET_FACING_CASTER;
                break;
            case 53: // Backstab
            case 2589:
            case 2590:
            case 2591:
            case 8721:
            case 11279:
            case 11280:
            case 11281:
            case 25300:
            case 26863:
            case 48656:
            case 48657:
            case 703: // Garrote
            case 8631:
            case 8632:
            case 8633:
            case 11289:
            case 11290:
            case 26839:
            case 26884:
            case 48675:
            case 48676:
            case 5221: // Shred
            case 6800:
            case 8992:
            case 9829:
            case 9830:
            case 27001:
            case 27002:
            case 48571:
            case 48572:
            case 8676: // Ambush
            case 8724:
            case 8725:
            case 11267:
            case 11268:
            case 11269:
            case 27441:
            case 48689:
            case 48690:
            case 48691:
            case 6785: // Ravage
            case 6787:
            case 9866:
            case 9867:
            case 27005:
            case 48578:
            case 48579:
            case 21987: // Lash of Pain
            case 23959: // Test Stab R50
            case 24825: // Test Backstab
            case 58563: // Assassinate Restless Lookout
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_REQ_CASTER_BEHIND_TARGET;
                break;
            case 26029: // Dark Glare
            case 37433: // Spout
            case 43140: // Flame Breath
            case 43215: // Flame Breath
            case 70461: // Coldflame Trap
            case 72133: // Pain and Suffering
            case 73788: // Pain and Suffering
            case 73789: // Pain and Suffering
            case 73790: // Pain and Suffering
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_CONE_LINE;
                break;
            case 24340: // Meteor
            case 26558: // Meteor
            case 28884: // Meteor
            case 36837: // Meteor
            case 38903: // Meteor
            case 41276: // Meteor
            case 57467: // Meteor
            case 26789: // Shard of the Fallen Star
            case 31436: // Malevolent Cleave
            case 35181: // Dive Bomb
            case 40810: // Saber Lash
            case 43267: // Saber Lash
            case 43268: // Saber Lash
            case 42384: // Brutal Swipe
            case 45150: // Meteor Slash
            case 64688: // Sonic Screech
            case 72373: // Shared Suffering
            case 71904: // Chaos Bane
            case 70492: // Ooze Eruption
            case 72505: // Ooze Eruption
            case 72624: // Ooze Eruption
            case 72625: // Ooze Eruption
            case 88942: // Meteor Slash
            case 95172: // Meteor Slash
            case 82935: // Caustic Slime
            case 88915: // Caustic Slime
            case 88916: // Caustic Slime
            case 88917: // Caustic Slime
            case 86825: // Blackout
            case 92879: // Blackout
            case 92880: // Blackout
            case 92881: // Blackout
                // ONLY SPELLS WITH SPELLFAMILY_GENERIC and EFFECT_SCHOOL_DAMAGE
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_SHARE_DAMAGE;
                break;
            case 18500: // Wing Buffet
            case 33086: // Wild Bite
            case 49749: // Piercing Blow
            case 52890: // Penetrating Strike
            case 53454: // Impale
            case 59446: // Impale
            case 62383: // Shatter
            case 64777: // Machine Gun
            case 65239: // Machine Gun
            case 65919: // Impale
            case 67858: // Impale
            case 67859: // Impale
            case 67860: // Impale
            case 69293: // Wing Buffet
            case 74439: // Machine Gun
            case 63278: // Mark of the Faceless (General Vezax)
            case 62544: // Thrust (Argent Tournament)
            case 64588: // Thrust (Argent Tournament)
            case 66479: // Thrust (Argent Tournament)
            case 68505: // Thrust (Argent Tournament)
            case 62709: // Counterattack! (Argent Tournament)
            case 62626: // Break-Shield (Argent Tournament, Player)
            case 64590: // Break-Shield (Argent Tournament, Player)
            case 64342: // Break-Shield (Argent Tournament, NPC)
            case 64686: // Break-Shield (Argent Tournament, NPC)
            case 65147: // Break-Shield (Argent Tournament, NPC)
            case 68504: // Break-Shield (Argent Tournament, NPC)
            case 62874: // Charge (Argent Tournament, Player)
            case 68498: // Charge (Argent Tournament, Player)
            case 64591: // Charge (Argent Tournament, Player)
            case 63003: // Charge (Argent Tournament, NPC)
            case 63010: // Charge (Argent Tournament, NPC)
            case 68321: // Charge (Argent Tournament, NPC)
            case 72255: // Mark of the Fallen Champion (Deathbringer Saurfang)
            case 72444: // Mark of the Fallen Champion (Deathbringer Saurfang)
            case 72445: // Mark of the Fallen Champion (Deathbringer Saurfang)
            case 72446: // Mark of the Fallen Champion (Deathbringer Saurfang)
            case 62775: // Tympanic Tantrum (XT-002 encounter)
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_IGNORE_ARMOR;
                break;
            case 64422: // Sonic Screech (Auriaya)
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_SHARE_DAMAGE;
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_IGNORE_ARMOR;
                break;
            case 72293: // Mark of the Fallen Champion (Deathbringer Saurfang)
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_NEGATIVE_EFF0;
                break;
            case 22482: // Blade Flurry dmg
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_IGNORE_ARMOR;
                break;
            case 48707: // Anti-Magic Shell
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_SEND_BASE_AMOUNT;
                break;
            default:
                break;
        }

        switch (spellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_WARRIOR:
                // Shout
                if (spellInfo->SpellFamilyFlags[0] & 0x20000 || spellInfo->SpellFamilyFlags[1] & 0x20)
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
                break;
            case SPELLFAMILY_DRUID:
                // Roar
                if (spellInfo->SpellFamilyFlags[0] & 0x8)
                    spellInfo->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
                break;
            default:
                break;
        }
    }

    CreatureAI::FillAISpellInfo();

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded spell custom attributes in %u ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadDbcDataCorrections()
{
    uint32 oldMSTime = getMSTime();

    SpellInfo* spellInfo = NULL;
    for (uint32 i = 0; i < GetSpellInfoStoreSize(); ++i)
    {
        spellInfo = mSpellInfoMap[i];
        if (!spellInfo)
            continue;

        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            switch (spellInfo->Effects[j].Effect)
            {
                case SPELL_EFFECT_CHARGE:
                case SPELL_EFFECT_CHARGE_DEST:
                case SPELL_EFFECT_JUMP:
                case SPELL_EFFECT_JUMP_DEST:
                case SPELL_EFFECT_LEAP_BACK:
                    if (!spellInfo->Speed && !spellInfo->SpellFamilyName)
                        spellInfo->Speed = SPEED_CHARGE;
                    break;
            }
        }

        if (spellInfo->ActiveIconID == 2158)  // flight
            spellInfo->Attributes |= SPELL_ATTR0_PASSIVE;

        switch (spellInfo->Id)
        {
            case 87934: // Serpent Spread
            case 87935:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_PROC_TRIGGER_SPELL;
                break;
            case 88466:
            case 88453:
                spellInfo->Speed = 0;
                break;
            // Master Marksman Aim Shot proc
            case 82928:
                spellInfo->CastTimeMax = 0;
                spellInfo->CastTimeMin = 0;
                spellInfo->Effects[1].BasePoints = 117;
                break;
            // Chains Of Ice
            case 45524:
                spellInfo->Effects[1].TargetA = TARGET_UNIT_TARGET_ENEMY;
                break;
            // Eclipse
            case 48517:
            case 48518:
                spellInfo->Attributes |= SPELL_ATTR0_CANT_CANCEL;
                break;
            // Glyph of Totemic Recall
            case 55438:
                spellInfo->Effects[0].MiscValue= SPELLMOD_EFFECT1;
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_ADD_PCT_MODIFIER;
                spellInfo->Effects[0].BasePoints = 200;
                break;
            case 97463: // Rallying Cry
            case 54443: // Demonic Empowerment
            case 79437: // Soulburn - Healthstone
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT;
                break;
            case 69176:
                spellInfo->AuraInterruptFlags |= AURA_INTERRUPT_FLAG_NOT_SEATED;
                break;
            // Nature's Blessing
            case 30869:
            case 30868:
            case 30867:
                spellInfo->Effects[0].SpellClassMask = flag96(0x000001C0, 0, 0x00010010); // Greater Healing Wave & Riptide
                break;
            case 34130: // Create Healthstone
                spellInfo->Effects[0].BasePoints = 1;
                break;
            case 99:    // Demoralizing Roar
            case 5857:  // Hellfire Effect
            case 49203: // Hungering Cold
//             case 52212: // Death and Decay
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 6343: // Thunder Clap (Battle, Defensive Stance)
            case 77758: // Thrash (Bear Form)
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                break;
            case 44203: // Tranquility
                spellInfo->Effects[1].SetRadiusIndex(10);
                break;
            case 42730:
                spellInfo->Effects[EFFECT_1].TriggerSpell = 42739;
                break;
            case 59735:
                spellInfo->Effects[EFFECT_1].TriggerSpell = 59736;
                break;
            // Wild Mushroom: Detonate dmg
            case 78777:
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            // Fungal Growth
            case 81289:
            case 81282:
                spellInfo->Effects[0].BasePoints = 100;
                break;
            // Avenger's Shield
            case 31935:
                spellInfo->DmgClass = SPELL_DAMAGE_CLASS_MAGIC;
                break;
            // Kill Command
            case 34026:
                spellInfo->SetRangeIndex(2);
                break;
            case 85673: // Word of Glory
            case 89023: // Blessed life (spell, not talent)
            case 85222: // Light of Dawn
            case 89024: // Pursuit of Justice
                spellInfo->Effects[1].Effect = 0;
                break;
            // wrong dbc, 7 days cooldown
            case 75141: // Dream of Skywall
            case 75142: // Dream of Deepholm
            case 75144: // Dream of Hyjal
            case 75145: // Dream of Ragnaros
            case 75146: // Dream of Azshara
                spellInfo->RecoveryTime = 0;
                spellInfo->CategoryRecoveryTime = 604800000; 
                break;
            // wrong dbc, 1 day cooldown 
            case 80243: // Transmute: Truegold
            case 61288: // Minor Inscription Research
            case 61177: // Northrend Inscription Research
                spellInfo->RecoveryTime = 0;
                spellInfo->CategoryRecoveryTime = 86400000; 
                break;
            // Blood Burst
            case 81280:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            // Transmute: Living Elements
            case 78866:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_SCRIPT_EFFECT;
                break;
            // Aura of Foreboding
            case 93974:
            case 93975:
            case 93986:
            case 93987:
                spellInfo->Effects[0].SetRadiusIndex(26);
                break;
            // Strikes of Opportunity
            case 76838:
                spellInfo->SpellFamilyName = SPELLFAMILY_WARRIOR;
                break;
            // Have Group, Will Travel
            case 83967:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Effects[0].TriggerSpell = 0;
                spellInfo->Effects[0].TargetA = TARGET_UNIT_CASTER;
                break;
            // Heart's Judgment, Heart of Ignacious trinket
            case 91041:
                spellInfo->CasterAuraSpell = 91027;
                break;
            // Heart's Judgment, Heart of Ignacious trinket (heroic)
            case 92328:
                spellInfo->CasterAuraSpell = 92325;
                break;
            // Shadow Apparition
            case 87426:
                spellInfo->Effects[0].TargetA = TARGET_UNIT_CASTER;
                break;
            case 52611: // Summon Skeletons
            case 52612: // Summon Skeletons
                spellInfo->Effects[EFFECT_0].MiscValueB = 64;
                break;
            case 40244: // Simon Game Visual
            case 40245: // Simon Game Visual
            case 40246: // Simon Game Visual
            case 40247: // Simon Game Visual
            case 42835: // Spout, remove damage effect, only anim is needed
                spellInfo->Effects[EFFECT_0].Effect = 0;
                break;
            case 41955:
                spellInfo->SetDurationIndex(28);
                break;
            case 30657: // Quake
                spellInfo->Effects[EFFECT_0].TriggerSpell = 30571;
                break;
            case 30541: // Blaze (needs conditions entry)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                break;
            case 63665: // Charge (Argent Tournament emote on riders)
            case 31298: // Sleep (needs target selection script)
            case 51904: // Summon Ghouls On Scarlet Crusade (this should use conditions table, script for this spell needs to be fixed)
            case 2895:  // Wrath of Air Totem rank 1 (Aura)
            case 68933: // Wrath of Air Totem rank 2 (Aura)
            case 29200: // Purify Helboar Meat
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                break;
            case 31344: // Howl of Azgalor
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_100_YARDS); // 100yards instead of 50000?!
                break;
            case 12540:
                spellInfo->SetDurationIndex(35);
                spellInfo->SpellIconID = 4423;
                break;
            case 42818: // Headless Horseman - Wisp Flight Port
            case 42821: // Headless Horseman - Wisp Flight Missile
                spellInfo->SetRangeIndex(6); // 100 yards
                break;
            case 36350: //They Must Burn Bomb Aura (self)
                spellInfo->Effects[EFFECT_0].TriggerSpell = 36325; // They Must Burn Bomb Drop (DND)
                break;
            case 49838: // Stop Time
                spellInfo->AttributesEx3 |= SPELL_ATTR3_NO_INITIAL_AGGRO;
                break;
            case 61407: // Energize Cores
            case 62136: // Energize Cores
            case 54069: // Energize Cores
            case 56251: // Energize Cores
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_SRC_AREA_ENTRY;
                break;
            case 50785: // Energize Cores
            case 59372: // Energize Cores
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_SRC_AREA_ENEMY;
                break;
            case 8494: // Mana Shield (rank 2)
                // because of bug in dbc
                spellInfo->ProcChance = 0;
                break;
            case 20335: // Heart of the Crusader
            case 20336:
            case 20337:
            case 63320: // Glyph of Life Tap
            // Entries were not updated after spell effect change, we have to do that manually :/
                spellInfo->AttributesEx3 |= SPELL_ATTR3_CAN_PROC_WITH_TRIGGERED;
                break;
            case 59725: // Improved Spell Reflection - aoe aura
                // Target entry seems to be wrong for this spell :/
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER_AREA_PARTY;
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_10_YARDS_2);
                break;
            case 44978: // Wild Magic
            case 45001:
            case 45002:
            case 45004:
            case 45006:
            case 45010:
            case 31347: // Doom
            case 41635: // Prayer of Mending
            case 44869: // Spectral Blast
            case 45027: // Revitalize
            case 45976: // Muru Portal Channel
            case 39365: // Thundering Storm
            case 41071: // Raise Dead (HACK)
            case 52124: // Sky Darkener Assault
            case 42442: // Vengeance Landing Cannonfire
            case 45863: // Cosmetic - Incinerate to Random Target
            case 25425: // Shoot
            case 45761: // Shoot
            case 42611: // Shoot
            case 61588: // Blazing Harpoon
            case 52479: // Gift of the Harvester
            case 48246: // Ball of Flame
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 36384: // Skartax Purple Beam
                spellInfo->MaxAffectedTargets = 2;
                break;
            case 41376: // Spite
            case 39992: // Needle Spine
            case 29576: // Multi-Shot
            case 40816: // Saber Lash
            case 37790: // Spread Shot
            case 46771: // Flame Sear
            case 45248: // Shadow Blades
            case 41303: // Soul Drain
            case 29213: // Curse of the Plaguebringer - Noth
            case 28542: // Life Drain - Sapphiron
            case 66588: // Flaming Spear
            case 54171: // Divine Storm
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 38310: // Multi-Shot
                spellInfo->MaxAffectedTargets = 4;
                break;
            case 42005: // Bloodboil
            case 38296: // Spitfire Totem
            case 37676: // Insidious Whisper
            case 46008: // Negative Energy
            case 45641: // Fire Bloom
            case 55665: // Life Drain - Sapphiron (H)
            case 28796: // Poison Bolt Volly - Faerlina
                spellInfo->MaxAffectedTargets = 5;
                break;
            case 40827: // Sinful Beam
            case 40859: // Sinister Beam
            case 40860: // Vile Beam
            case 40861: // Wicked Beam
            case 54835: // Curse of the Plaguebringer - Noth (H)
            case 54098: // Poison Bolt Volly - Faerlina (H)
                spellInfo->MaxAffectedTargets = 10;
                break;
            case 50312: // Unholy Frenzy
                spellInfo->MaxAffectedTargets = 15;
                break;
            case 33711: // Murmur's Touch
            case 38794:
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Effects[EFFECT_0].TriggerSpell = 33760;
                break;
            case 86211: // Soul Swap
            case 17941: // Shadow Trance
            case 22008: // Netherwind Focus
            case 31834: // Light's Grace
            case 34754: // Clearcasting
            case 34936: // Backlash
            case 48108: // Hot Streak
            case 51124: // Killing Machine
            case 54741: // Firestarter
            case 57761: // Fireball!
            case 39805: // Lightning Overload
            case 64823: // Item - Druid T8 Balance 4P Bonus
            case 34477: // Misdirection
            case 44401: // Missile Barrage
            case 47283: // Empowered Imp
            case 88688: // Surge of Light
                spellInfo->ProcCharges = 1;
                break;
            case 74396: // Fingers of Frost visual buff
                spellInfo->ProcCharges = 2;
                spellInfo->StackAmount = 0;
                break;
            case 28200: // Ascendance (Talisman of Ascendance trinket)
                spellInfo->ProcCharges = 6;
                break;
            case 51852: // The Eye of Acherus (no spawn in phase 2 in db)
                spellInfo->Effects[EFFECT_0].MiscValue |= 1;
                break;
            case 51912: // Crafty's Ultra-Advanced Proto-Typical Shortening Blaster
                spellInfo->Effects[EFFECT_0].Amplitude = 3000;
                break;
            case 29809: // Desecration Arm - 36 instead of 37 - typo? :/
                spellInfo->Effects[0].SetRadiusIndex(EFFECT_RADIUS_7_YARDS);
                break;
            // Master Shapeshifter: missing stance data for forms other than bear - bear version has correct data
            // To prevent aura staying on target after talent unlearned
            case 48420:
                spellInfo->Stances = 1 << (FORM_CAT - 1);
                break;
            case 48421:
                spellInfo->Stances = 1 << (FORM_MOONKIN - 1);
                break;
            case 48422:
                spellInfo->Stances = 1 << (FORM_TREE - 1);
                break;
            case 51466: // Elemental Oath (Rank 1)
            case 51470: // Elemental Oath (Rank 2)
                spellInfo->Effects[EFFECT_1].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[EFFECT_1].ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
                spellInfo->Effects[EFFECT_1].MiscValue = SPELLMOD_EFFECT2;
                spellInfo->Effects[EFFECT_1].SpellClassMask = flag96(0x00000000, 0x00004000, 0x00000000);
                break;
            case 47569: // Improved Shadowform (Rank 1)
                // with this spell atrribute aura can be stacked several times
                spellInfo->Attributes &= ~SPELL_ATTR0_NOT_SHAPESHIFT;
                break;
            case 64904: // Hymn of Hope
                spellInfo->Effects[EFFECT_1].ApplyAuraName = SPELL_AURA_MOD_INCREASE_ENERGY_PERCENT;
                break;
            case 19465: // Improved Stings (Rank 2)
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_CASTER;
                break;
            case 30421: // Nether Portal - Perseverence
                spellInfo->Effects[2].BasePoints += 30000;
                break;
            case 16834: // Natural shapeshifter
            case 16835:
                spellInfo->SetDurationIndex(21);
                break;
            case 65142: // Ebon Plague
                spellInfo->Effects[1].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->Effects[1].MiscValue = SPELL_SCHOOL_MASK_MAGIC;
                break;
            case 41913: // Parasitic Shadowfiend Passive
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY; // proc debuff, and summon infinite fiends
                break;
            case 27892: // To Anchor 1
            case 27928: // To Anchor 1
            case 27935: // To Anchor 1
            case 27915: // Anchor to Skulls
            case 27931: // Anchor to Skulls
            case 27937: // Anchor to Skulls
                spellInfo->SetRangeIndex(13);
                break;
            // target allys instead of enemies, target A is src_caster, spells with effect like that have ally target
            // this is the only known exception, probably just wrong data
            case 29214: // Wrath of the Plaguebringer
            case 54836: // Wrath of the Plaguebringer
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ALLY;
                spellInfo->Effects[EFFECT_1].TargetB = TARGET_UNIT_SRC_AREA_ALLY;
                break;
            case 57994: // Wind Shear - improper data for EFFECT_1 in 3.3.5 DBC, but is correct in 4.x
                spellInfo->Effects[EFFECT_1].Effect = SPELL_EFFECT_MODIFY_THREAT_PERCENT;
                spellInfo->Effects[EFFECT_1].BasePoints = -6; // -5%
                break;
            case 63675: // Improved Devouring Plague
                spellInfo->AttributesEx3 |= SPELL_ATTR3_NO_DONE_BONUS;
                break;
            case 8145: // Tremor Totem (instant pulse)
            case 6474: // Earthbind Totem (instant pulse)
                spellInfo->AttributesEx5 |= SPELL_ATTR5_START_PERIODIC_AT_APPLY;
                break;
            case 52109: // Flametongue Totem rank 1 (Aura)
            case 52110: // Flametongue Totem rank 2 (Aura)
            case 52111: // Flametongue Totem rank 3 (Aura)
            case 52112: // Flametongue Totem rank 4 (Aura)
            case 52113: // Flametongue Totem rank 5 (Aura)
            case 58651: // Flametongue Totem rank 6 (Aura)
            case 58654: // Flametongue Totem rank 7 (Aura)
            case 58655: // Flametongue Totem rank 8 (Aura)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                break;
            case 5176:  // Wrath
            case 2912:  // Starfire
            case 78674: // Starsurge
                spellInfo->Effects[EFFECT_1].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ENEMY;
                break;
            case 70728: // Exploit Weakness (needs target selection script)
            case 70840: // Devious Minds (needs target selection script)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_PET;
                break;
            case 70893: // Culling The Herd (needs target selection script)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_MASTER;
                break;
            case 54800: // Sigil of the Frozen Conscience - change class mask to custom extended flags of Icy Touch
                        // this is done because another spell also uses the same SpellFamilyFlags as Icy Touch
                        // SpellFamilyFlags[0] & 0x00000040 in SPELLFAMILY_DEATHKNIGHT is currently unused (3.3.5a)
                        // this needs research on modifier applying rules, does not seem to be in Attributes fields
                spellInfo->Effects[EFFECT_0].SpellClassMask = flag96(0x00000040, 0x00000000, 0x00000000);
                break;
            case 64949: // Idol of the Flourishing Life
                spellInfo->Effects[EFFECT_0].SpellClassMask = flag96(0x00000000, 0x02000000, 0x00000000);
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
                break;
            case 34231: // Libram of the Lightbringer
            case 60792: // Libram of Tolerance
            case 64956: // Libram of the Resolute
                spellInfo->Effects[EFFECT_0].SpellClassMask = flag96(0x80000000, 0x00000000, 0x00000000);
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
                break;
            case 28851: // Libram of Light
            case 28853: // Libram of Divinity
            case 32403: // Blessed Book of Nagrand
                spellInfo->Effects[EFFECT_0].SpellClassMask = flag96(0x40000000, 0x00000000, 0x00000000);
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
                break;
            case 45602: // Ride Carpet
                spellInfo->Effects[EFFECT_0].BasePoints = 0; // force seat 0, vehicle doesn't have the required seat flags for "no seat specified (-1)"
                break;
            case 64745: // Item - Death Knight T8 Tank 4P Bonus
            case 64936: // Item - Warrior T8 Protection 4P Bonus
                spellInfo->Effects[0].BasePoints = 100; // 100% chance of procc'ing, not -10% (chance calculated in PrepareTriggersExecutedOnHit)
                break;
            case 19970: // Entangling Roots (Rank 6) -- Nature's Grasp Proc
            case 19971: // Entangling Roots (Rank 5) -- Nature's Grasp Proc
            case 19972: // Entangling Roots (Rank 4) -- Nature's Grasp Proc
            case 19973: // Entangling Roots (Rank 3) -- Nature's Grasp Proc
            case 19974: // Entangling Roots (Rank 2) -- Nature's Grasp Proc
            case 19975: // Entangling Roots (Rank 1) -- Nature's Grasp Proc
            case 27010: // Entangling Roots (Rank 7) -- Nature's Grasp Proc
            case 53313: // Entangling Roots (Rank 8) -- Nature's Grasp Proc
                spellInfo->SetCastTimeIndex(1);
                break;
            case 59414: // Pulsing Shockwave Aura (Loken)
                // this flag breaks movement, remove it
                spellInfo->AttributesEx &= ~SPELL_ATTR1_CHANNELED_1;
                break;
            case 61719: // Easter Lay Noblegarden Egg Aura - Interrupt flags copied from aura which this aura is linked with
                spellInfo->AuraInterruptFlags = AURA_INTERRUPT_FLAG_HITBYSPELL | AURA_INTERRUPT_FLAG_TAKE_DAMAGE;
                break;
            case 70650: // Death Knight T10 Tank 2P Bonus
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_ADD_PCT_MODIFIER;
                break;
            case 68872: // Soulstorm (Bronjahm Encounter)
                spellInfo->InterruptFlags = 0;
                break;
            // ULDUAR SPELLS
            //
            case 64014: // Expedition Base Camp Teleport
            case 64032: // Formation Grounds Teleport
            case 64028: // Colossal Forge Teleport
            case 64031: // Scrapyard Teleport
            case 64030: // Antechamber Teleport
            case 64029: // Shattered Walkway Teleport
            case 64024: // Conservatory Teleport
            case 64025: // Halls of Invention Teleport
            case 64027: // Descent into Madness Teleport
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DB;
                break;
            case 62374: // Pursued (Flame Leviathan)
                spellInfo->Effects[0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS);   // 50000yd
                break;
            case 62383: // Shatter (Ignis)
                spellInfo->SpellVisual[0] = 12639;
                break;
            case 63342: // Focused Eyebeam Summon Trigger (Kologarn)
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 62716: // Growth of Nature (Freya)
            case 65584: // Growth of Nature (Freya)
            case 64381: // Strength of the Pack (Auriaya)
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 63018: // Searing Light (XT-002)
            case 65121: // Searing Light (25m) (XT-002)
            case 63024: // Gravity Bomb (XT-002)
            case 64234: // Gravity Bomb (25m) (XT-002)
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 62834: // Boom (XT-002)
            // This hack is here because we suspect our implementation of spell effect execution on targets
            // is done in the wrong order. We suspect that EFFECT_0 needs to be applied on all targets,
            // then EFFECT_1, etc - instead of applying each effect on target1, then target2, etc.
            // The above situation causes the visual for this spell to be bugged, so we remove the instakill
            // effect and implement a script hack for that.
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            case 64386: // Terrifying Screech (Auriaya)
            case 64389: // Sentinel Blast (Auriaya)
            case 64678: // Sentinel Blast (Auriaya)
                spellInfo->SetDurationIndex(28); // 5 seconds, wrong DBC data?
                break;
            case 64321: // Potent Pheromones (Freya)
                // spell should dispel area aura, but doesn't have the attribute
                // may be db data bug, or blizz may keep reapplying area auras every update with checking immunity
                // that will be clear if we get more spells with problem like this
                spellInfo->AttributesEx |= SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY;
                break;
            case 62301: // Cosmic Smash (Algalon the Observer)
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 64598: // Cosmic Smash (Algalon the Observer)
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 62293: // Cosmic Smash (Algalon the Observer)
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_DEST_CASTER;
                break;
            case 62311: // Cosmic Smash (Algalon the Observer)
            case 64596: // Cosmic Smash (Algalon the Observer)
                spellInfo->SetRangeIndex(6);  // 100yd
                break;
            // ENDOF ULDUAR SPELLS
            //
            // TRIAL OF THE CRUSADER SPELLS
            //
            case 66258: // Infernal Eruption (10N)
            case 67901: // Infernal Eruption (25N)
                // increase duration from 15 to 18 seconds because caster is already
                // unsummoned when spell missile hits the ground so nothing happen in result
                spellInfo->SetDurationIndex(85);
                break;
            // ENDOF TRIAL OF THE CRUSADER SPELLS
            //
            // ICECROWN CITADEL SPELLS
            //
            // THESE SPELLS ARE WORKING CORRECTLY EVEN WITHOUT THIS HACK
            // THE ONLY REASON ITS HERE IS THAT CURRENT GRID SYSTEM
            // DOES NOT ALLOW FAR OBJECT SELECTION (dist > 333)
            case 70781: // Light's Hammer Teleport
            case 70856: // Oratory of the Damned Teleport
            case 70857: // Rampart of Skulls Teleport
            case 70858: // Deathbringer's Rise Teleport
            case 70859: // Upper Spire Teleport
            case 70860: // Frozen Throne Teleport
            case 70861: // Sindragosa's Lair Teleport
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DB;
                break;
            case 69055: // Saber Lash (Lord Marrowgar)
            case 70814: // Saber Lash (Lord Marrowgar)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_5_YARDS); // 5yd
                break;
            case 69075: // Bone Storm (Lord Marrowgar)
            case 70834: // Bone Storm (Lord Marrowgar)
            case 70835: // Bone Storm (Lord Marrowgar)
            case 70836: // Bone Storm (Lord Marrowgar)
            case 72864: // Death Plague (Rotting Frost Giant)
            case 71160: // Plague Stench (Stinky)
            case 71161: // Plague Stench (Stinky)
            case 71123: // Decimate (Stinky & Precious)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_100_YARDS); // 100yd
                break;
            case 72378: // Blood Nova (Deathbringer Saurfang)
            case 73058: // Blood Nova (Deathbringer Saurfang)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_200_YARDS);
                break;
            case 72769: // Scent of Blood (Deathbringer Saurfang)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS);
                // no break
            case 72771: // Scent of Blood (Deathbringer Saurfang)
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_200_YARDS);
                break;
            case 72723: // Resistant Skin (Deathbringer Saurfang adds)
                // this spell initially granted Shadow damage immunity, however it was removed but the data was left in client
                spellInfo->Effects[EFFECT_2].Effect = 0;
                break;
            case 70460: // Coldflame Jets (Traps after Saurfang)
                spellInfo->SetDurationIndex(1); // 10 seconds
                break;
            case 71412: // Green Ooze Summon (Professor Putricide)
            case 71415: // Orange Ooze Summon (Professor Putricide)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            case 71159: // Awaken Plagued Zombies
                spellInfo->SetDurationIndex(21);
                break;
            case 70530: // Volatile Ooze Beam Protection (Professor Putricide)
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_APPLY_AURA; // for an unknown reason this was SPELL_EFFECT_APPLY_AREA_AURA_RAID
                break;
            // THIS IS HERE BECAUSE COOLDOWN ON CREATURE PROCS IS NOT IMPLEMENTED
            case 71604: // Mutated Strength (Professor Putricide)
            case 72673: // Mutated Strength (Professor Putricide)
            case 72674: // Mutated Strength (Professor Putricide)
            case 72675: // Mutated Strength (Professor Putricide)
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            case 72454: // Mutated Plague (Professor Putricide)
            case 72464: // Mutated Plague (Professor Putricide)
            case 72506: // Mutated Plague (Professor Putricide)
            case 72507: // Mutated Plague (Professor Putricide)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                break;
            case 70911: // Unbound Plague (Professor Putricide) (needs target selection script)
            case 72854: // Unbound Plague (Professor Putricide) (needs target selection script)
            case 72855: // Unbound Plague (Professor Putricide) (needs target selection script)
            case 72856: // Unbound Plague (Professor Putricide) (needs target selection script)
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_TARGET_ENEMY;
                break;
            case 71518: // Unholy Infusion Quest Credit (Professor Putricide)
            case 72934: // Blood Infusion Quest Credit (Blood-Queen Lana'thel)
            case 72289: // Frost Infusion Quest Credit (Sindragosa)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // another missing radius
                break;
            case 71708: // Empowered Flare (Blood Prince Council)
            case 72785: // Empowered Flare (Blood Prince Council)
            case 72786: // Empowered Flare (Blood Prince Council)
            case 72787: // Empowered Flare (Blood Prince Council)
                spellInfo->AttributesEx3 |= SPELL_ATTR3_NO_DONE_BONUS;
                break;
            case 71266: // Swarming Shadows
            case 72890: // Swarming Shadows
                spellInfo->AreaGroupId = 0; // originally, these require area 4522, which is... outside of Icecrown Citadel
                break;
            case 70602: // Corruption
            case 48278: // Paralyze
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 70715: // Column of Frost (visual marker)
                spellInfo->SetDurationIndex(32); // 6 seconds (missing)
                break;
            case 71085: // Mana Void (periodic aura)
                spellInfo->SetDurationIndex(9); // 30 seconds (missing)
                break;
            case 72015: // Frostbolt Volley (only heroic)
            case 72016: // Frostbolt Volley (only heroic)
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(EFFECT_RADIUS_40_YARDS);
                break;
            case 70936: // Summon Suppressor (needs target selection script)
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                break;
            case 72706: // Achievement Check (Valithria Dreamwalker)
            case 71357: // Order Whelp
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS);   // 200yd
                break;
            case 70598: // Sindragosa's Fury
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DEST;
                break;
            case 69846: // Frost Bomb
                spellInfo->Speed = 0.0f;    // This spell's summon happens instantly
                break;
            case 71614: // Ice Lock
                spellInfo->Mechanic = MECHANIC_STUN;
                break;
            case 72762: // Defile
                spellInfo->SetDurationIndex(559); // 53 seconds
                break;
            case 72743: // Defile
                spellInfo->SetDurationIndex(22); // 45 seconds
                break;
            case 72754: // Defile
            case 73708: // Defile
            case 73709: // Defile
            case 73710: // Defile
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                break;
            case 69030: // Val'kyr Target Search
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                break;
            case 69198: // Raging Spirit Visual
                spellInfo->SetRangeIndex(13); // 50000yd
                break;
            case 73654: // Harvest Souls
            case 74295: // Harvest Souls
            case 74296: // Harvest Souls
            case 74297: // Harvest Souls
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                break;
            case 73655: // Harvest Soul
                spellInfo->AttributesEx3 |= SPELL_ATTR3_NO_DONE_BONUS;
                break;
            case 73540: // Summon Shadow Trap
                spellInfo->SetDurationIndex(23); // 90 seconds
                break;
            case 73530: // Shadow Trap (visual)
                spellInfo->SetDurationIndex(28); // 5 seconds
                break;
            case 73529: // Shadow Trap
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_10_YARDS); // 10yd
                break;
            case 74282: // Shadow Trap (searcher)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_3_YARDS); // 3yd
                break;
            case 72595: // Restore Soul
            case 73650: // Restore Soul
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                break;
            case 74086: // Destroy Soul
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                break;
            case 74302: // Summon Spirit Bomb
            case 74342: // Summon Spirit Bomb
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 74341: // Summon Spirit Bomb
            case 74343: // Summon Spirit Bomb
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 73579: // Summon Spirit Bomb
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_25_YARDS); // 25yd
                break;
            case 72350: // Fury of Frostmourne
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                break;
            case 75127: // Kill Frostmourne Players
            case 72351: // Fury of Frostmourne
            case 72431: // Jump (removes Fury of Frostmourne debuff)
            case 72429: // Mass Resurrection
            case 73159: // Play Movie
            case 73582: // Trigger Vile Spirit (Inside, Heroic)
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                break;
            case 72376: // Raise Dead
                spellInfo->MaxAffectedTargets = 3;
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_50000_YARDS); // 50000yd
                break;
            case 71809: // Jump
                spellInfo->SetRangeIndex(3); // 20yd
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_25_YARDS); // 25yd
                break;
            case 72405: // Broken Frostmourne
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(EFFECT_RADIUS_200_YARDS); // 200yd
                break;
            // ENDOF ICECROWN CITADEL SPELLS
            //
            // RUBY SANCTUM SPELLS
            //
            case 74769: // Twilight Cutter
            case 77844: // Twilight Cutter
            case 77845: // Twilight Cutter
            case 77846: // Twilight Cutter
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(EFFECT_RADIUS_100_YARDS); // 100yd
                break;
            case 75509: // Twilight Mending
                spellInfo->AttributesEx6 |= SPELL_ATTR6_CAN_TARGET_INVISIBLE;
                spellInfo->AttributesEx2 |= SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS;
                break;
            case 75888: // Awaken Flames
            case 75889: // Awaken Flames
                spellInfo->AttributesEx |= SPELL_ATTR1_CANT_TARGET_SELF;
                break;
            // ENDOF RUBY SANCTUM SPELLS
            //
            case 40055: // Introspection
            case 40165: // Introspection
            case 40166: // Introspection
            case 40167: // Introspection
                spellInfo->Attributes |= SPELL_ATTR0_NEGATIVE_1;
                break;
            case 2378: // Minor Fortitude
                spellInfo->ManaCost = 0;
                spellInfo->ManaPerSecond = 0;
                break;
            // OCULUS SPELLS
            // The spells below are here, because their effect 1 is giving warning, because the triggered spell is not found in dbc and is missing from encounter sniff.
            case 49462: // Call Ruby Drake
            case 49461: // Call Amber Drake
            case 49345: // Call Emerald Drake
                spellInfo->Effects[EFFECT_0].Effect = 0;
                break;
            // ENDOF OCULUS SPELLS
            //
            // THRONE OF THE TIDES SPELLS
            //
            // Lady Nazjar
            case 90479: // Waterspout dmg
                spellInfo->Effects[0].Effect = 0;
                break;
            case 75690: // Waterspout knock
                spellInfo->Effects[0].SetRadiusIndex(7);
                spellInfo->Effects[1].SetRadiusIndex(7);
                break;
            case 75700: // Geyser Errupt
            case 91469:
                spellInfo->Effects[0].SetRadiusIndex(8);
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            case 94046: // Geyser Errupt knock
            case 94047:
                spellInfo->Effects[0].SetRadiusIndex(8);
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            case 75813: // Chain Lightning
            case 91450:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 75993: // Lightning Surge dmg
            case 91451:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 80564: // Fungal Spores dmg
            case 91470:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            // Commander Ulthok
            case 76085: // Dark Fissure dmg
            case 91375:
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            case 76047:
                spellInfo->Effects[0].TargetB = 0;
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            // Erunak Stonespeaker
            case 84945: // Earth Shards dmg
            case 91491:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            // Mindbender Ghursha
            case 76341: // Unrelenting Agony dmg
            case 91493:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            // Ozumat
            case 83463: // Entangling Grasp
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 83915: // Brain Spike
            case 91497:
                spellInfo->Effects[0].SetRadiusIndex(23);
                spellInfo->Effects[1].SetRadiusIndex(23);
                break;
            case 83561: // Blight of Ozumat dmg
            case 91495:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            // ENDOF THRONE OF THE TIDES SPELLS
            //
            // BLACKROCK CAVERNS SPELLS
            //
            // Rom'ogg Bonecrusher 
            case 75272: // Quake
                spellInfo->Effects[0].SetRadiusIndex(23);
                spellInfo->Effects[1].SetRadiusIndex(23);
                break;
            case 82189: // Chains of Woe
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            // Ascendant Lord Obsidius
            case 76186: // Thinderclap
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            case 76164: // Shadow of Obsidius
                spellInfo->Effects[0].BasePoints = 10000000;
                break;
            // ENDOF BLACKROCK CAVERNS SPELLS
            //
            // THE VORTEX PINNACLE SPELLS
            //
            // Grand Vizier Ertan
            case 86292: // Cyclone Shield dmg
            case 93991:
                spellInfo->Effects[0].SetRadiusIndex(26);
                spellInfo->Effects[1].SetRadiusIndex(26);
                spellInfo->Effects[2].SetRadiusIndex(26);
                break;
            // Asaad
            case 87551: // Supremacy of the Storm dmg
            case 93994:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 87622: // Chain Lightning
            case 93993:
                spellInfo->Effects[0].SetRadiusIndex(12);
                break;
            case 87618: // Static Cling
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            // Altairus
            case 88276: // Call of Wind Dummy
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 88282: // Upwind of Altairus
                spellInfo->Effects[1].SetRadiusIndex(9);
                break;
            // Trash
            case 87854: // Arcane Barrage
            case 92756:
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 88073: // Starfall
            case 92783:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 87761: // Rally
                spellInfo->Effects[0].SetRadiusIndex(10);
                break;
            case 87933: // Air Nova
            case 92753:
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            case 85159: // Howling Gale dmg
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 85084: // Howling Gale
                spellInfo->Effects[0].TriggerSpell = 85159;
                break;
            case 87765: // Lightning Lash Dummy
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 87768: // Lightning Nova
            case 92780:
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            case 88170: // Cloudburst
            case 92760:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            // ENDOF THE VORTEX PINNACLE SPELLS
            //
            // BARADIN HOLD SPELLS
            //
            // Occu'thar
            case 96872: // Focused Fire
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 96883: // Focused Fire dmg
            case 101004:
                spellInfo->Effects[0].SetRadiusIndex(17);
                break;
            case 96920: // Eye of Occu'thar
            case 101006:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 96931: // Eye of Occu'thar script
                spellInfo->Effects[0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[0].TargetB = 0;
                break;
            case 96968: // Occu'thar's Destruction
            case 101008:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 96946: // Eye of Occu'thar dmg
                spellInfo->Effects[0].TargetA = TARGET_UNIT_CASTER;
                break;
            // Alizabal
            case 105065: // Seething Hate Dummy
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 105069: // Seething Hate dmg
            case 108094:
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            case 106248: // Blade Dance Dummy
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 104994: // Blade Dance dmg
                spellInfo->Effects[0].SetRadiusIndex(17);
                break;
            // ENDOF BARADIN HOLD SPELLS
            //
            // THE STONECORE SPELLS
            //
            // Corborus
            case 82415: // Dumpening Wave
            case 92650:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            case 92122: // Crystal Shard dmg
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            // Slabhide
            case 80800: // Eruption
            case 92657:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 80801: // Eruption aura
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            // Ozruk
            case 78807: // Shatter
            case 92662:
                spellInfo->Effects[0].SetRadiusIndex(18);
                spellInfo->Effects[1].SetRadiusIndex(18);
                break;
            case 92426: // Paralyze
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            // High Priestess Azil
            case 79251: // Gratity Well aura
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 79249: // Gravity Well aura dmg
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[0].BasePoints = 3000;
                break;
            case 79050: // Arcane Shield dmg
            case 92667:
                spellInfo->Effects[2].SetRadiusIndex(8);
                break;
            // ENDOF STONECORE SPELLS
            //
            // LOST CITY OF THE TOL'VIR
            case 83644: // Mystic Trap N
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 91252: // Mystic Trap H
                spellInfo->MaxAffectedTargets = 5;
                break;
            case 83112: // Land Mine Player Search Effect
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                spellInfo->AttributesEx3 = SPELL_ATTR3_ONLY_TARGET_PLAYERS;
                break;
            case 91263: // Detonate Traps
                spellInfo->Attributes |= SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY;
                break;
            case 84799: // Paralytic Blow Dart N
            case 89989: // Paralytic Blow Dart H
                spellInfo->Attributes = 0;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                spellInfo->Effects[EFFECT_2].TargetB = 0;
                spellInfo->Targets = TARGET_FLAG_UNIT;
                break;
            case 83131: // Summon Shockwave Target N
            case 83132: // Summon Shockwave Target S
            case 83133: // Summon Shockwave Target W
            case 83134: // Summon Shockwave Target E
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(15);
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DEST;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Targets = TARGET_FLAG_DEST_LOCATION;
                break;
            case 83454: // Shockwave N
            case 90029: // Shockwave H
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(15);
                break;
            case 81690: // Scent of Blood N
            case 89998: // Scent of Blood H
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 82263: // Merged Souls
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                spellInfo->Effects[EFFECT_2].TargetB = 0;
                spellInfo->Targets = TARGET_FLAG_UNIT;
                break;
            case 82430: // Repentance
                spellInfo->Effects[EFFECT_0].MiscValue = 250;
                break;
            case 91196: // Blaze of the Heavens
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(7);
                break;
            case 88814: // Hallowed Ground N
            case 90010: // Hallowed Ground H
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(32);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(32);
                break;
            case 81942: // Heaven's Fury N
            case 90040: // Heaven's Fury H
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(15);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(15);
                break;
            case 82622: // Plague of Ages N
            case 89997: // Plague of Ages H
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->AttributesEx = SPELL_ATTR1_CANT_TARGET_SELF;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS | SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 82637: // Plague of Ages N
                spellInfo->ExcludeTargetAuraSpell = 82622;
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Attributes &= ~SPELL_ATTR0_NEGATIVE_1;
                spellInfo->AttributesEx = SPELL_ATTR1_CANT_TARGET_SELF;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS | SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 89996: // Plague of Ages H
                spellInfo->ExcludeTargetAuraSpell = 89997;
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Attributes &= ~SPELL_ATTR0_NEGATIVE_1;
                spellInfo->AttributesEx = SPELL_ATTR1_CANT_TARGET_SELF;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS | SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 82640: // Plague of Ages N
                spellInfo->ExcludeTargetAuraSpell = 82622;
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Attributes &= ~SPELL_ATTR0_NEGATIVE_1;
                spellInfo->AttributesEx = SPELL_ATTR1_CANT_TARGET_SELF;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS | SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 89995: // Plague of Ages H
                spellInfo->ExcludeTargetAuraSpell = 89997;
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Attributes &= ~SPELL_ATTR0_NEGATIVE_1;
                spellInfo->AttributesEx = SPELL_ATTR1_CANT_TARGET_SELF;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS | SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 82139: // Repentance
                spellInfo->AttributesEx3 &= ~SPELL_ATTR3_DEATH_PERSISTENT;
                break;
            case 82425: // Wail of Darkness N
            case 90039: // Wail of Darkness H
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(32);
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_SRC_CASTER;
                spellInfo->Effects[EFFECT_1].TargetB = TARGET_UNIT_SRC_AREA_ENEMY;
                break;
            case 84546: // Static Shock
            case 84555: // Static Shock
            case 84556: // Static Shock
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DEST;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Targets = TARGET_FLAG_DEST_LOCATION;
                break;
            case 84956: // Call of Sky
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DEST;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_DEST_DEST;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                spellInfo->Targets = TARGET_FLAG_DEST_LOCATION;
                break;
            case 83455: // Chain Lightning N
            case 90027: // Chain Lightning H
                spellInfo->InterruptFlags |= SPELL_INTERRUPT_FLAG_INTERRUPT;
                break;
            case 83790: // Cloud Burst
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_DEST;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_DEST_DEST_RANDOM;
                spellInfo->Targets = TARGET_FLAG_DEST_LOCATION;
                break;
            case 83051: // Cloud Burst
            case 90032: // Cloud Burst
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(7);
                break;
            case 83566: // Wailing Winds
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                spellInfo->Effects[EFFECT_2].TargetB = 0;
                break;
            case 83094: // Wailing Winds N
            case 90031: // Wailing Winds H
                spellInfo->AttributesEx4 |= SPELL_ATTR4_TRIGGERED;
                break;
            case 83151: // Absorb Storms
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_NEARBY_ENTRY;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_CASTER;
                break;
            case 84126: // Call Scorpid
            case 82791: // Call Crocolisk
                spellInfo->Attributes = 0;
                break;
            // ENDOF LOST CITY OF THE TOL'VIR
            // GRIM BATOL SPELLS
            //
            // Trash
            case 76517: // Eruption Fire
            case 90693:
                spellInfo->Effects[0].SetRadiusIndex(15);
                break;
            case 76782: // Rock Smash dmg
            case 90862:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 76786: // Fissure dmg
            case 90863:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 76101: // Lightning Strike dmg
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 76392: // Arcane Slash
            case 90660:
                spellInfo->Effects[0].SetRadiusIndex(15);
                break;
            case 76370: // Warped Twilight
            case 90300:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 76620: // Azure Blast
            case 90697:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 76404: // Crimson Charge
                spellInfo->EquippedItemClass = 0;
                spellInfo->EquippedItemSubClassMask = 0;
                break;
            case 76409: // Crimson Shockwave
            case 90312:
                spellInfo->Effects[0].SetRadiusIndex(8);
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            case 76327: // Blaze
            case 90307:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 76693: // Empowering Twilight dmg
            case 90707:
                spellInfo->Effects[0].SetRadiusIndex(8);
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            case 76627: // Mortal Strike
                spellInfo->EquippedItemClass = 0;
                spellInfo->EquippedItemSubClassMask = 0;
                break;
            case 76603: // Earth Spike
            case 90487:
                spellInfo->Effects[0].SetRadiusIndex(15);
                break;
            case 76411: // Meat Grinder
            case 90665:
                spellInfo->EquippedItemClass = 0;
                spellInfo->EquippedItemSubClassMask = 0;
                break;
            case 76413: // Meat Grinder dmg
            case 90664:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 76668: // Flame Conduit
            case 90850:
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            case 76578: // Chain Lightning
            case 90856:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            // General Umbriss
            case 74675: // Blitz dmg
            case 90251:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                break;
            case 74837: // Modgud Malady
            case 90179:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                break;
            case 90170: // Modgud Malice aura
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                spellInfo->Effects[2].Effect = 0;
                break;
            // Forgemaster Throngus
            case 74976: // Disorenting Roar
            case 90737:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 90754: // Lava Patch dmg
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 74986: // Cave In dmg
            case 90722:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 74984: // Mighty Stomp
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            // Drahga Shadowburner
            case 75238: // Supernova
            case 90972:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[2].SetRadiusIndex(14);
                break;
            case 75245: // Burning Shadowbolt
            case 90915:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75317: // Seeping Twilight dmg
            case 90964:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            // Erudax
            case 75861: // Binding Shadows aura
            case 91079:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                spellInfo->Effects[2].SetRadiusIndex(14);
                break;
            case 75520: // Twilight Corruption
            case 91049:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75694: // Shadow Gale Speed
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75664: // Shadow Gale
            case 91086:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75692: // Shadow Gale dmg
            case 91087:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            case 76194: // Twilight Blast dmg
            case 91042:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75809: // Shield of Nightmare
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 75763: // Umbral Mending
            case 91040:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            // ENDOF GRIM BATOL SPELLS
            //
            // BLACKWING DESCENT SPELLS
            //
            // Trash
            case 79604: // Thunderclap
            case 91905:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(13);
                break;
            case 80035: // Vengeful Rage
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(12);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(12);
                break;
            case 79589: // Constricting Chains
            case 91911:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(27);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(27);
                break;
            case 80336: // Frost Burn
            case 91896:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(9);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(9);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(9);
                break;
            case 80878: // Bestowal of Angerforge
            case 80871: // Bestowal of Thaurissan
            case 80875: // Bestowal of Ironstar
            case 80872: // Bestowal of Burningeye
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 80638: // Stormbolt
            case 91890:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 80646: // Chain Lightning
            case 91891:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                break;
            case 91849: // Grip of Death
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_SRC_CASTER; 
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENEMY;
                break;
            case 92048: // Shadow Infusion
                spellInfo->TargetAuraSpell = 0;
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            case 92051: // Shadow Conductor
            case 92135:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            case 92023: // Encasing Shadows
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                spellInfo->TargetAuraSpell = 0;
                break;
            case 92153: // Blazing Inferno missile
                spellInfo->Speed = 6;
                break;
            case 92154: // Blazing Inferno dmg
            case 92190:
            case 92191:
            case 92192:
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 92173: // Shadow Breath
            case 92193:
            case 92194:
            case 92195:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                break;
            case 89798: // Master Adventurer Award
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            // Magmaw
            case 89773: // Mangle
            case 91912:
            case 94616:
            case 94617:
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            case 78359: // Magma Split 1
            case 91916:
            case 91925:
            case 91926:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENEMY;
                break;
            case 78068: // Magma Split 2
            case 91917:
            case 91927:
            case 91928:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENEMY;
                break;
            // Omnotron Defence System
            case 78697: // Recharging blue
            case 95019:
            case 95020:
            case 95021:
            case 78698: // Recharging orange
            case 95025:
            case 95026:
            case 95027:
            case 78699: // Recharging purple
            case 95028:
            case 95029:
            case 95030:
            case 78700: // Recharging green
            case 95022:
            case 95023:
            case 95024:
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            case 91540: // Arcane Annihilator
            case 91542:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 79629: // Power Generator aoe
            case 91555:
            case 91556:
            case 91557:
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_SRC_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENTRY;
                break;
            case 91858: // Overcharged Power Generator aoe
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 91879: // Arcane Blowback
            case 91880:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            case 79912: // Static Shock dmg
            case 91456:
            case 91457:
            case 91458:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                break;
            case 79035: // Inseneration Security Missure
            case 91523:
            case 91524:
            case 91525:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            case 79501: // Acquiring Target
            case 92035:
            case 92036:
            case 92037:
                spellInfo->AttributesEx5 |= SPELL_ATTR5_USABLE_WHILE_STUNNED;
                break;
            case 79504: // Flamethrower
            case 91535:
            case 91536:
            case 91537:
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CONE_ENEMY_24;
                spellInfo->AttributesEx5 |= SPELL_ATTR5_USABLE_WHILE_STUNNED;
                break;
            case 79617: // Backdraft
            case 91528:
            case 91529:
            case 91530:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            case 80092: // Poison Bomb
            case 91498:
            case 91499:
            case 91500:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            case 80164: // Chemical Cloud aoe a
            case 91478:
            case 91479:
            case 91480:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(18);
                break;
            case 80161: // Chemocal Cloud aoe b
            case 91471:
            case 91472:
            case 91473:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(18);
                break;
            case 80097: // Poison Pubble aoe
            case 91488:
            case 91489:
            case 91490:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            // Maloriak
            case 77699: // Flash Freeze dmg
            case 92978:
            case 92979:
            case 92980:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 77763: // Biting Chill dmg
            case 92975:
            case 92976:
            case 92977:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(29);
                break;
            case 77615: // Debilitating Slime
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(12);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(12);
                break;
            case 77908: // Arcane Storm dmg
            case 92961:
            case 92962:
            case 92963:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 78095: // Magma Jets dmg
            case 93014:
            case 93015:
            case 93016:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(15);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(15);
                break;
            case 78225: // Acid Nova
            case 93011:
            case 93012:
            case 93013:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 77987: // Grown Catalyst
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(13);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(13);
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 78208: // Absolute Zero
            case 93041:
            case 93042:
            case 93043:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 77715: // Shatter
            case 95655:
            case 95656:
            case 95657:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            // Chimaeron
            case 82705: // Finkle's Mixture
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            case 82848: // Massacre
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            // Atramedes
            case 77612: // Modulation
            case 92451:
            case 92452:
            case 92453:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 77675: // Sonar Pulse dmg
            case 92417:
            case 92418:
            case 92419:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                break;
            case 77982: // Searing Flame dmg
            case 92421:
            case 92422:
            case 92423:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(28);
                break;
            case 78115: // Sonar Fireball
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;
            case 92553: // Sonar Bomb dmg
            case 92554:
            case 92555:
            case 92556:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 77966: // Searing Flame Missile
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(23);
                break;
            case 78353: // Roaring Flame dmg
            case 92445:
            case 92446:
            case 92447:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 78023: // Roaring Flame aura dmg
            case 92483:
            case 92484:
            case 92485:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(8);
                break;
            case 78875: // Devastation
                spellInfo->TargetAuraSpell = 0;
                break;
            case 78868: // Devastation dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(28);
                break;

            // ENDOF BLACKWING DESCENT SPELLS
            //
            // FIRELANDS SPELLS
            //
            // Trash
            case 99692: // Terrifying Roar
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(33);
                break;
            case 97552: // Lava Shower dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(26);
                break;
            case 99993: // Fiery Blood
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(18);
                spellInfo->Effects[EFFECT_2].SetRadiusIndex(18);
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 100273: // Shell Spin dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(8);
                break;
            case 100799: // Fire Torment dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 99530: // Flame Stomp
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            case 99555: // Summon Lava Jest
                spellInfo->MaxAffectedTargets = 4;
                break;
            // Shannox
            case 100002: // Hurl Spear dmg
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(15);
                break;
            case 100031: // Hurl Spear aoe
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 100495: // Magma Flare
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(27);
                break;
            case 99945: // Face Rage jump
                spellInfo->Effects[EFFECT_0].TriggerSpell = 0;
                spellInfo->Attributes |= SPELL_ATTR0_IMPOSSIBLE_DODGE_PARRY_BLOCK;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 99947: // Face Rage aura
                spellInfo->Attributes |= SPELL_ATTR0_IMPOSSIBLE_DODGE_PARRY_BLOCK;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 100415: // Rage
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            case 99937: // Jagged Tear
            case 101218:
            case 101219:
            case 101220:
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            // ENDOF FIRELANDS
            // BASTION OF TWILIGHT SPELLS
            //
            // Trash
            case 85647: // Mind Sear dmg
                spellInfo->Effects[2].SetRadiusIndex(13);
                break;
            case 85697:
                spellInfo->Effects[2].SetRadiusIndex(29);
                break;

            // Halfus Wyrmbreaker
            //
            case 83710: // Furious Roar
            case 86169:
            case 86170:
            case 86171:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                spellInfo->Effects[2].SetRadiusIndex(28);
                break;
            case 83719: // Fireball Barrage T
                spellInfo->Effects[0].TargetA = 1;
                break;
            case 83855: // Scorching Breath dmg
            case 86163:
            case 86164:
            case 86165:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 83734: // Fireball Barrage dmg0
            case 86154:
            case 86155:
            case 86156:
            case 83721: // Fireball Barrage dmg1
            case 86151:
            case 86152:
            case 86153:
                spellInfo->Effects[0].SetRadiusIndex(26);
                break;
            //
            // - Theralion & Valiona
            //
            case 86380: // Dazzling Destruction Summon Missile
            case 92923:
            case 92924:
            case 92925:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 86369: // Twilight Blast
            case 92898:
            case 92899:
            case 92900:
                spellInfo->Speed = 15;
                break;
            case 86371: // Twilight Blast dmg
            case 92903:
            case 92904:
            case 92905:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 86386: // Dazzling Destruction Missile
            case 92920:
            case 92921:
            case 92922:
                spellInfo->Speed = 15;
                break;
            case 86406: // Dazzling Destruction dmg
            case 92926:
            case 92927:
            case 92928:
                spellInfo->Effects[0].SetRadiusIndex(32);
                spellInfo->Effects[1].SetRadiusIndex(32);
                spellInfo->Effects[1].TargetB = 15;
                break;
            case 86505: // Fabolous Flames dmg
            case 92907:
            case 92908:
            case 92909:
                spellInfo->Effects[0].SetRadiusIndex(32);
                break;
            case 86607: // Engulfing Magic
            case 92912:
            case 92913:
            case 92914:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 86825: // Blackout dmg
            case 92879:
            case 92880:
            case 92881:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 86013: // Twilight Meteorite
            case 92859:
            case 92860:
            case 92861:
                spellInfo->Speed = 15;
                break;
            case 86014: // Twilight Meteorite dmg
            case 92863:
            case 92864:
            case 92865:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 88518: // Twilight Meteorite Mark
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 86199: // Twilight Flame dmg1
            case 92868:
            case 92869:
            case 92870:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].Effect = 0;
                spellInfo->Effects[2].SetRadiusIndex(14);
                break;
            case 86228: // Twilight Flame dmg2
            case 92867:
                spellInfo->TargetAuraSpell = 0;
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 86305: // Unstable Twilight dmg
            case 92882:
            case 92883:
            case 92884:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            case 86202: // Twilight Shift Aura 1
            case 92889:
            case 92890:
            case 92891:
            case 88436: // Twilight Shift Aura 2
            case 92892:
            case 92893:
            case 92894:
                spellInfo->Effects[0].MiscValue = 16;
                break;
            case 86210: // Twilight Zone
                spellInfo->Effects[0].Effect = 0;
                break;
            case 86214: // Twilight Zone dmg
            case 92885:
            case 92886:
            case 92887:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            case 93019: // Rift Blast
            case 93020:
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 93055: // Shifting Reality
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            // Cho'gall
            case 91303: // Conversion
            case 93203:
            case 93204:
            case 93205:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 81571: // Unleashed Shadows dmg
            case 93221:
            case 93222:
            case 93223:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 81538: // Blaze dmg
            case 93212:
            case 93213:
            case 93214:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 81713: // Depravity
            case 93175:
            case 93176:
            case 93177:
                spellInfo->Effects[0].SetRadiusIndex(9);
                break;
            case 81761: // Spilled Blood of the Old God dmg
            case 93172:
            case 93173:
            case 93174:
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            case 82919: // Sprayed Corruption
            case 93108:
            case 93109:
            case 93110:
                spellInfo->Effects[0].SetRadiusIndex(9);
                spellInfo->Effects[1].SetRadiusIndex(9);
                break;
            case 82299: // Fester Blood
                spellInfo->Effects[0].TargetA = 1;
                spellInfo->Effects[0].TargetB = 0;
                spellInfo->Effects[0].Effect = 3;
                spellInfo->Effects[1].Effect = 0;
                break;
            case 82630: // Comsume Blood of the Old God
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                spellInfo->Effects[2].SetRadiusIndex(28);
                break;
            case 82414: // Darkened Creations
            case 93160:
            case 93161:
            case 93162:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 82433: // Darkened Creations sum
                spellInfo->Effects[0].TargetA = 1;
                break;
            case 82361: // Corruption of the Old God
                spellInfo->Effects[1].Effect = 0;
                break;
            case 82363: // Corruption of the Old God dmg
            case 93169:
            case 93170:
            case 93171:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            case 82151: // Shadow Bolt
            case 93193:
            case 93194:
            case 93195:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 91317: // Worshipping
            case 93365:
            case 93366:
            case 93367:
                spellInfo->Effects[2].Effect = 0;
            case 91331: // Twisted Devotion
            case 93206:
            case 93207:
            case 93208:
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 93103: // Corrupted Blood aura
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->ExcludeTargetAuraSpell = 93103;
                break;
            // Sinestra
            case 95822: // Twilight Flame dmg
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 95855: // Call of Flames
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 89435: // Wrack aoe
            case 92956:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 89421: // Wrack dmg
            case 92955:
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                spellInfo->AttributesEx6 |= SPELL_ATTR6_NO_DONE_PCT_DAMAGE_MODS;
                break;
            case 90028: // Unleash Essence
            case 92947:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 90045: // Indomitable dmg
            case 92946:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                break;
            case 87655: // Twilight Infusion dummy
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 87231: // Fiery Barrier aura
                spellInfo->Effects[0].SetRadiusIndex(10);
                break;
            case 88146: // Twilight Essence dmg
                spellInfo->Effects[0].SetRadiusIndex(22);
                spellInfo->Effects[1].SetRadiusIndex(22);
                break;
            case 92950: // Twilight Essence dmg
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                break;
            case 92852: // Twilight Slicer dmg
            case 92954:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 95564: // Twilight Infusion missile
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 89299: // Twilight Spit dmg
            case 92953:
                spellInfo->AttributesEx3  |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 92958: // Twilight Pulse dmg
            case 92959:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            // Council of Ascendents
            case 82746: // Glaciate
            case 92506:
            case 92507:
            case 92508:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                break;
            case 92548: // Glaciate 2
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 82699: // Water Bomb
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 82762: // Waterlogged
                spellInfo->Effects[0].SetRadiusIndex(29);
                break;
            case 77347: // Water Bomb Summon
                spellInfo->Effects[0].SetRadiusIndex(12);
                spellInfo->Effects[1].SetRadiusIndex(12);
                break;
            case 82666: // Frost Imbued
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 82859: // Inferno Rush
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 82856: // Inferno Jump
                spellInfo->Effects[1].Effect = 0;
                break;
            case 82663: // Flame Imbued
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            case 83067: // Thundershock
            case 92469:
            case 92470:
            case 92471:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 83099: // Lightning Rod
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 83300: // Chain Lightning dummy
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 83282: // Chain Lightning
            case 92448:
            case 92449:
            case 92450:
                spellInfo->Effects[0].SetRadiusIndex(8);
                break;
            case 83070: // Lightning Blast
            case 92454:
            case 92455:
            case 92456:
                spellInfo->Effects[0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[0].TargetB = 0;
                spellInfo->Effects[1].Effect = 0;
                break;
            case 83078: // Disperse 1
                spellInfo->Effects[0].Effect = 0;
                break;
            case 83565: // Quake
            case 92544:
            case 92545:
            case 92546:
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            case 84915: // Liquid Ice dmg
            case 92497:
            case 92498:
            case 92499:
                spellInfo->Effects[0].SetRadiusIndex(8);
                spellInfo->Effects[1].SetRadiusIndex(8);
                break;
            case 84948: // Gravity Crush
            case 92486:
            case 92487:
            case 92488:
                spellInfo->Effects[0].SetRadiusIndex(28);
                spellInfo->Effects[1].SetRadiusIndex(28);
                spellInfo->Effects[2].SetRadiusIndex(28);
                break;
            case 84913: // Lava Seed
                spellInfo->Effects[0].SetRadiusIndex(28);
                break;
            // ENDOF BASTION OF TWILIGHT SPELLS
            //
            // ZUL'GURUB SPELLS
            //
            // Venoxis
            case 96489: // Toxic Explosion
            case 97093:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(14);
                break;
            case 96560: // Word of Hethiss
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            case 96842: // Bloodvenom
                spellInfo->MaxAffectedTargets = 3;
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(41);
                break;
            case 96638: // Bloodvenom dmg
            case 97104:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(26);
                break;
            case 96685: // Venomous Infusion dmg
            case 97338:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                break;
            case 96475: // Toxic Link
                spellInfo->MaxAffectedTargets = 2;
                break;
            case 96476: // Toxic Link dummy
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->TargetAuraSpell = 96477;
                break;
            // Zanzil
            case 96319:
            case 96316:
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Effects[EFFECT_0].ApplyAuraName = 0;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            case 96342: // Pursuit
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_CASTER;
                break;
            case 96914: // Zanzil Fire
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_STUN;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                break;
            // Kilnara
            case 96457: // Wall of Agony
                spellInfo->AttributesEx &= ~SPELL_ATTR1_CHANNELED_1;
                break;
            case 96909: // Wail of Sorrow
            case 96948:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 96422: // Tears of Blood dmg
            case 96957:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(32);
                break;
            case 97355: // Gaping Wound jump
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                break;
            case 97357: // Gaping Wound dmg
            case 97358:
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_1].TargetB = 0;
                break;
            case 98238: // Rat Lure
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(10);
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENTRY;
                break;
            // Jindo The Godbreaker
            case 96689: // Spirit World aura
                spellInfo->Effects[EFFECT_1].MiscValue = 2;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ENEMY;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_HIT_RESULT;
                break;
            case 97022: // Hakkar's Chains
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                spellInfo->ChannelInterruptFlags = 0;
                break;
            case 97172: // Shadows of Hakkar
            case 97198: // Body Slam
                spellInfo->InterruptFlags &= ~SPELL_INTERRUPT_FLAG_INTERRUPT;
                break;
            case 97597: // Spirit Warrior's Gaze
            case 97158:
                spellInfo->AttributesEx3 |= SPELL_ATTR3_ONLY_TARGET_PLAYERS;
                spellInfo->MaxAffectedTargets = 1;
                break;
            case 97152: // Summon Spirit target
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(33);
                break;
            case 96964: // Sunder Rift
                spellInfo->SetDurationIndex(18);
                break;
            case 96970: // Sunder Rift:
                spellInfo->ProcChance = 100;
                break;
            case 97320: // Sunder Rift
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_SRC_CASTER;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_SRC_AREA_ALLY;
                spellInfo->InterruptFlags = 0;
                break;
            // ENDOF ZUL'GURUB SPELLS
            //
            // ZUL'AMAN SPELLS
            //
            // Akil'zon
            case 43648: // Electrical Storm
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_STUN;
                break;
            // Janalai
           case 42471: // Hatch Eggs
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(10);
                spellInfo->MaxAffectedTargets = 3;
                break;
            case 42630: // Fire Bomb dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(15);
                break;
            case 43140: // Flame Breath
            case 97855:
                spellInfo->InterruptFlags &= ~SPELL_INTERRUPT_FLAG_INTERRUPT;
                spellInfo->ChannelInterruptFlags &= ~SPELL_INTERRUPT_FLAG_INTERRUPT;
                break;
            // Halazzi
            case 97505: // Refreshing Stream heal
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(8);
                break;
            // Hex Lord Malacrass
            case 44132: // Drain Power
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            case 43095: // Creeping Paralysis
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            case 97682: // Pillar of Flame dmg
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                break;
            case 43121: // Feather Storm
            case 97645:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(7);
                break;
            // ENDOF ZUL'AMAN SPELLS
            //
            // Camouflage
            case 80325:
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            // Steady Shot
            case 56641:
                spellInfo->Effects[EFFECT_2].TargetA = TARGET_UNIT_CASTER;
                break;
            // Fortune Cookie
            case 87604:
                spellInfo->Effects[EFFECT_2].Effect = SPELL_EFFECT_SCRIPT_EFFECT;
                break;
            // Atonement
            case 81751:
                spellInfo->MaxAffectedTargets = 1;
                spellInfo->Effects[0].SetRadiusIndex(18);
                spellInfo->Effects[0].RealPointsPerLevel = 0.0f;
                break;
            // Whirlwind (MH+OH)
            case 1680:
            case 44949:
            // Whirlwind (MH+OH) - Bladestorm
            case 50622:
            case 95738:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(14);
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(14);
                break;
            // Second Wind
            case 29841:
            case 29842:
                spellInfo->Effects[EFFECT_1].ApplyAuraName = SPELL_AURA_OBS_MOD_HEALTH;
                break;
            // Improved Frost Presence (Rank 1/2)
            case 50384:
            case 50385:
                spellInfo->Effects[EFFECT_1].SpellClassMask = 0;
                break;
            // Death Knight Pet Scaling 02
            // unused value but we can use it for gargoyle penetration
            case 51996:
                spellInfo->Effects[EFFECT_2].ApplyAuraName = SPELL_AURA_MOD_TARGET_RESISTANCE;
                spellInfo->Effects[EFFECT_2].MiscValue = 126;
                break;
            // Mastery DK Frost
            case 77514:
            // Mastery DK Unholy
            case 77515:
                spellInfo->Effects[EFFECT_0].SpellClassMask = 0;
                break;
            // Frost Nova
            case 122:
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            // Guardian of Ancient Kings
            case 86150:
                spellInfo->Effects[0].TargetA = 1;
                break;
            case 1160:  // Demoralizing Shout
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->AttributesEx |= SPELL_ATTR1_NOT_BREAK_STEALTH;
                break;
            case 12323: // Piercing Howl
                spellInfo->AttributesEx |= SPELL_ATTR1_NOT_BREAK_STEALTH;
                break;
            // Fire Power
            case 54734:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            // Speed of Light
            case 85495:
            case 85498:
            case 85499:
                spellInfo->Effects[1].TargetA = 1;
                break;
            // Forbearance
            case 642:
                spellInfo->ExcludeCasterAuraSpell = 25771;
            case 633:
            case 1022:
                spellInfo->ExcludeTargetAuraSpell = 25771;
                break;
             // Consecration
            case 36946:
                spellInfo->SetDurationIndex(1);
                break;
            // Tower of Radiance rank 3
            case 85512:
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_PROC_TRIGGER_SPELL;
                break;
            // War Stomp
            case 20549:
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            // Blood Frenzy
            case 30069:
            case 30070:
                spellInfo->SetDurationIndex(3);
                break;
            case 118:   // Polymorph
            case 61305: // Polymorph (other animal)
            case 28272: // polymorph (other animal)
            case 61721: // Polymorph (other animal)
            case 61780: // Polymorph (other animal)
            case 28271: // Polymorph (other animal)
                spellInfo->AuraInterruptFlags = AURA_INTERRUPT_FLAG_TAKE_DAMAGE;
                break;
            // Fingers of Frost
            case 44544:
                spellInfo->Effects[0].SpellClassMask = flag96(0x00020000, 0x00100000, 0x00000008);
                break;
            // Living Bomb
            case 44457:
                spellInfo->MaxAffectedTargets = 3;
                break;
            // Seals of Command
            case 85126:
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
                spellInfo->Effects[1].BasePoints = 2;
                break;
            // Seal of Righteousness
            case 25742:
                spellInfo->Effects[0].ChainTarget = 1;
                break;
            // Holy Radiance
            case 86452:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            // Ancient Healer
            case 86674:
                spellInfo->ProcCharges = 5;
                break;
            // Ancient Guardian
            case 86657:
                spellInfo->Effects[0].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            // Divine Storm
            case 53385:
                spellInfo->Effects[0].BasePoints = 0;
                break;
            // Soul Harvest
            case 79268:
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_PERIODIC_ENERGIZE;
                spellInfo->Effects[0].BasePoints = 1;
                spellInfo->Effects[0].MiscValue = 7;
                spellInfo->Effects[0].Amplitude = 3000;
                spellInfo->Effects[1].BasePoints = 5;
                spellInfo->Effects[1].Amplitude = 1000;
                break;
            // Improved Mind Blast
            case 15273:
            case 15312:
            case 15313:
                spellInfo->Effects[1].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Inner Focus
            case 89485:
                spellInfo->ProcChance = 100;
                spellInfo->ProcCharges = 1;
                spellInfo->ProcFlags = PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_POS;
                break;
            // Strength of Soul
            case 89488:
            case 89489:
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE;
                break;
            // Circle of Healing
            case 34861:
                spellInfo->Effects[0].SetRadiusIndex(10);
                spellInfo->Effects[0].TargetB = TARGET_UNIT_CASTER_AREA_RAID;
                break;
            // Sin and Punishment
            case 87099:
            case 87100:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Chakra
            case 14751:
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->Effects[1].Effect = 0;
                spellInfo->Effects[2].Effect = 0;
                break;
            // Holy Nova dmg
            case 15237:
                spellInfo->Effects[0].SetRadiusIndex(13);
                break;
            // Holy Nova heal
            case 23455: 
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->MaxAffectedTargets = 5;
                break;
            // Mental Scream
            case 8122:
                spellInfo->Effects[0].SetRadiusIndex(14);
                spellInfo->Effects[1].SetRadiusIndex(14);
                spellInfo->Effects[2].SetRadiusIndex(14);
                break;
            // Shadow Orbs
            case 77486:
                spellInfo->ProcChance = 0;
                spellInfo->ProcCharges = 0;
                spellInfo->ProcFlags = 0;
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->Effects[0].TriggerSpell = 0;
                break;
            case 687:   // Demonic Armor
            case 28176: // Fel Armor
                spellInfo->Effects[2].BasePoints = 91711;
                break;
            // Dark Intent
            case 80398:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Effects[0].TriggerSpell = 0;
                spellInfo->ProcChance = 0;
                spellInfo->ProcFlags = 0;
                break;
            // Dark Intent proc
            case 85768:
                spellInfo->SpellFamilyName = SPELLFAMILY_WARLOCK;
                break;
            // Howl of Terror
            case 5484:
                spellInfo->Effects[0].SetRadiusIndex(13);
                spellInfo->Effects[1].SetRadiusIndex(13);
                break;
            // Impact
            case 12355:
                spellInfo->AttributesEx |= SPELL_ATTR1_NO_THREAT;
                spellInfo->Attributes |= SPELL_ATTR0_NEGATIVE_1;
                spellInfo->Effects[0].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[1].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            // Mirror Image - Clone Me!
            case 45204:
                spellInfo->AttributesEx6 |= SPELL_ATTR6_CAN_TARGET_INVISIBLE;
                spellInfo->AttributesEx2 |= SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS;
                break;
            // Copy Weapon Spells
            case 41055:
            case 45206:
            case 63416:
            case 69891:
            case 69892:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Mechanic = 0;
                break;
            // Jinx: Curse of Elements
            case 86105:
            case 86717:
                spellInfo->Effects[0].SetRadiusIndex(23);
                spellInfo->Effects[1].SetRadiusIndex(23);
                break;
            // Soul Burn
            case 74434:
                spellInfo->ProcCharges = 1;
                break;
            // Soul Burn: Seed of Corruption
            case 86664:
                spellInfo->Effects[0].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->SetDurationIndex(85);
                break;
            // Demonic Leap
            case 54785:
                spellInfo->Effects[0].SetRadiusIndex(43);
                break;
            // Frenzied Regeneration
            case 22842:
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT;
                break;
            case 779:   // Swipe (Bear)
            case 62078: // Swipe (Cat)
                spellInfo->Effects[0].SetRadiusIndex(14);
                break;
            // Critical Mass (proc from pyroblast)
            case 11095:
            case 12872:
            case 12873:
                spellInfo->Effects[0].SpellClassMask[0] |= 0x00400000;
                break;
            // Sunfire
            case 94338:
                spellInfo->Effects[0].BasePoints = 93402;
                break;
            case 53353: // Chimera Shot heal effect
                spellInfo->DmgClass = SPELL_DAMAGE_CLASS_MAGIC;
                break;
            // Gift of the Earthmother
            case 51179:
            case 51180:
            case 51181:
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_ADD_PCT_MODIFIER;
                spellInfo->Effects[1].MiscValue = SPELLMOD_DOT;
                break;
            // Tree of Life
            case 65139:
                spellInfo->Effects[1].Effect = 0;
                spellInfo->Effects[1].ApplyAuraName = 0;
                break;
            // Blood in the Water rank 2
            case 80319:
                spellInfo->Effects[0].ApplyAuraName = SPELL_AURA_PROC_TRIGGER_SPELL;
                break;
            // Solar Beam
            case 78675:
                spellInfo->Effects[1].ApplyAuraName = SPELL_AURA_PERIODIC_DUMMY;
                spellInfo->Effects[1].Amplitude = 500;
                spellInfo->Effects[1].SetRadiusIndex(8);
                spellInfo->Effects[2].ApplyAuraName = 0;
                spellInfo->Effects[2].Effect = 0;
                break;
            // Wild Growth
            case 48438:
                spellInfo->Effects[0].SetRadiusIndex(10);
                spellInfo->Effects[1].SetRadiusIndex(10);
                break;
            // Echo of Light
            case 77489:
                spellInfo->StackAmount = 100; // should be inf
                spellInfo->AttributesEx8 |= SPELL_ATTR8_DONT_RESET_PERIODIC_TIMER;
                break;
            // Efflorescence
            // Healing Rain
            // Holy Word: Sanctuary
            case 81262:
            case 88685:
            case 73920:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_PERIODIC_DUMMY;
                spellInfo->Effects[EFFECT_1].ApplyAuraName = 0;
                spellInfo->Effects[EFFECT_1].Effect = 0;
                spellInfo->Effects[EFFECT_0].Amplitude = spellInfo->Id == 81262 ? 1000 : 2000;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ALLY;
                spellInfo->Effects[EFFECT_0].TargetB = TARGET_UNIT_DEST_AREA_ALLY;
                spellInfo->AttributesEx5 &= ~SPELL_ATTR5_START_PERIODIC_AT_APPLY;
                break;
            // Healing Rain
            // Holy Word: Sanctuary
            case 73921:
            case 88686:
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ALLY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                break;
            // Relentless Strikes
            case 14179:
            case 58422:
            case 58423:
                spellInfo->Effects[EFFECT_0].SpellClassMask[1] |= 0x00000008;
                break;
            // Relentless Strikes effect
            case 14181:
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_ENERGIZE;
                spellInfo->Effects[EFFECT_0].MiscValue = 3;
                break;
            // Smoke Bomb
            case 76577:
                spellInfo->Effects[EFFECT_1].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Fan of Knives
            case 51723:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(14);
                break;
            // Honor Among Thieves rank 1
            case 51698:
                spellInfo->SpellFamilyName = SPELLFAMILY_ROGUE;
                break;
            // Stampeding Roar
            case 77761: // Bear
            case 77764: // Cat
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(13);
                break;
            // Cheat Death
            case 45182:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN;
                break;
            // Runic Corruption
            case 51460:
            // Improved Blood Presence
            case 63611:
                spellInfo->Effects[EFFECT_1].Effect = 0;
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_POWER_REGEN_PERCENT;
                spellInfo->Effects[EFFECT_0].MiscValue = 5;
                spellInfo->Effects[EFFECT_0].MiscValueB = NUM_RUNE_TYPES;
                break;
            // Fire Nova
            case 1535:
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(12);
                break;
            // Fulmination damage
            case 88767:
                spellInfo->AttributesEx6 |= SPELL_ATTR6_NO_DONE_PCT_DAMAGE_MODS;
                break;
            // Searing Flame
            case 77661:
                spellInfo->Effects[EFFECT_0].ScalingMultiplier = 0.0f;
                spellInfo->AttributesEx6 |= SPELL_ATTR6_NO_DONE_PCT_DAMAGE_MODS;
                break;
            // Summon Gargoyle
            case 49206:
                spellInfo->SetDurationIndex(587);
                spellInfo->AttributesEx5 |= SPELL_ATTR5_SINGLE_TARGET_SPELL;
                break;
            // Focused Insight
            case 77794: // rank 1
            case 77795: // rank 2
                spellInfo->Effects[EFFECT_1].Effect = SPELL_EFFECT_APPLY_AURA;
                spellInfo->Effects[EFFECT_1].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_CASTER;
                break;
            // Detterence
            case 19263:
                spellInfo->Effects[EFFECT_2].ApplyAuraName = SPELL_AURA_MOD_PACIFY;
                break;
            // Multishot
            case 2643: 
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_DEST_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_1].SetRadiusIndex(14);
                break;
            // Paralysis
            case 87193:
            case 87194:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_MOD_ROOT;
                spellInfo->Effects[EFFECT_0].BasePoints = 0;
                break;
            // Detonate Mana, Tyrande's Favorite Doll
            case 92601:
                spellInfo->CasterAuraSpell = 92596;
                break;
            // Eye for an Eye
            case 25988:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Hercular's Rod
            case 89821:
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            // Magic Suppression
            case 49224:
            case 49610:
            case 49611:
                spellInfo->ProcCharges = 0;
                break;
            // Weakening, Flameseer's Staff
            case 75192:
                spellInfo->StackAmount = 20;
                break;
            // Improved Blood Presence
            case 61261:
                spellInfo->Attributes |= SPELL_ATTR0_PASSIVE;
                break;
            // Raise Ally
            case 61999:
                spellInfo->Effects[EFFECT_1].Effect = 0;
                break;
            // Snowball
            case 25677:
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_DUMMY;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ANY;
                spellInfo->Effects[EFFECT_1].TargetA = TARGET_UNIT_TARGET_ANY;
                break;
            // Shadow Fiend
            case 34433:
                spellInfo->Effects[EFFECT_0].MiscValueB = 1561;
                break;
            // Inflate Air Balloon, Undersea Inflation quest
            case 75346:
                spellInfo->Effects[EFFECT_0].Effect = SPELL_EFFECT_KILL_CREDIT;
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_CASTER;
                spellInfo->Effects[EFFECT_0].MiscValue = 40399;
                spellInfo->Effects[EFFECT_1].Effect = 0;
                spellInfo->Effects[EFFECT_1].ApplyAuraName = 0;
                break;
            // Flamebreaker, Flameseer's Staff, Flamebreaker quest
            case 75206:
                spellInfo->Effects[EFFECT_0].Effect = 0;
                spellInfo->Effects[EFFECT_0].ApplyAuraName = 0;
                spellInfo->Effects[EFFECT_0].TriggerSpell = 0;
                break;
            // Summon Unbound Flamesparks, Flameseer's Staff, Flamebreaker quest
            case 74723:
                spellInfo->Effects[EFFECT_0].MiscValue = 40065;
                break;
            // alot of aoe spells
            case 42208: // Blizzard
            case 42223: // Rain of Fire
            case 42231: // Hurricane
            case 52212: // Death and Decay
            case 81269: // Efflorescence
                spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ENEMY;
                spellInfo->Effects[EFFECT_0].TargetB = 0;
                spellInfo->Effects[EFFECT_0].SetRadiusIndex(0);

                if (spellInfo->Id == 81269) // Death and Decay
                    spellInfo->Effects[EFFECT_0].TargetA = TARGET_UNIT_TARGET_ALLY;

                if (spellInfo->Id == 52212) // Efflorescence
                    spellInfo->Effects[0].SetRadiusIndex(13);

                break;
            case 54424: // Fel Intelligence
                spellInfo->Effects[1].SetRadiusIndex(12);
                break;
            // Unstable Affliction dispel dmg
            case 31117:
                spellInfo->AttributesEx6 &= ~SPELL_ATTR6_NO_DONE_PCT_DAMAGE_MODS;
                break;
            case 82691: // Ring of Frost
                spellInfo->AttributesEx |= SPELL_ATTR1_CANT_BE_REFLECTED;
                break;
            // Item - Druid T12 Feral 4P Bonus
            case 99009:
                spellInfo->ProcChance = 100;
                spellInfo->ProcFlags = 16;
                break;
            // Item - Shaman T12 Enhancement 2P Bonus
            case 99209:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Item - Shaman T12 Enhancement 4P Bonus
            case 99213:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
                spellInfo->ProcChance = 100;
                spellInfo->ProcFlags = 16;
                break;
            // Stormfire, Item - Shaman T12 Enhancement 4P Bonus
            case 99212:
                spellInfo->AttributesEx3 |= SPELL_ATTR3_STACK_FOR_DIFF_CASTERS;
                break;
            // Item - Shaman T12 Elemental 4P Bonus
            case 99206:
                spellInfo->Effects[EFFECT_0].ApplyAuraName = SPELL_AURA_DUMMY;
                break;
            // Item - Rogue T12 4P Bonus
            case 99175:
                spellInfo->ProcChance = 0;
                spellInfo->ProcFlags = 0;
                break;
            // Glyph of Grounding Totem
            case 89523:
                spellInfo->SpellFamilyName = SPELLFAMILY_SHAMAN;
                break;
            // General Hunter Passives (x2ccrit bonus has already calculated) 
            case 87816:
                spellInfo->Effects[EFFECT_0].SpellClassMask = 0;
                break;
            default:
                break;
        }

        switch (spellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_PALADIN:
                // Seals of the Pure should affect Seal of Righteousness
                if (spellInfo->SpellIconID == 25 && spellInfo->HasAttribute(SPELL_ATTR0_PASSIVE))
                    spellInfo->Effects[0].SpellClassMask[1] |= 0x20000000;
                break;
            case SPELLFAMILY_DEATHKNIGHT:
                // Icy Touch - extend FamilyFlags (unused value) for Sigil of the Frozen Conscience to use
                if (spellInfo->SpellIconID == 2721 && spellInfo->SpellFamilyFlags[0] & 0x2)
                    spellInfo->SpellFamilyFlags[0] |= 0x40;
                break;
        }

        // Need to regroup target mask after spellInfo map editing
        spellInfo->LoadSpellTargetMask();
    }

    SummonPropertiesEntry* properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(121));
    properties->Type = SUMMON_TYPE_TOTEM;
    properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(647)); // 52893
    properties->Type = SUMMON_TYPE_TOTEM;
    properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(3002)); // 46157
    properties->Type = SUMMON_TYPE_TOTEM;
    properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(3001)); // Wild Mushrooms
    properties->Type = SUMMON_TYPE_TOTEM;
    properties->Flags &= ~512;

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Loaded spell dbc data corrections in %u ms", GetMSTimeDiffToNow(oldMSTime));
}
