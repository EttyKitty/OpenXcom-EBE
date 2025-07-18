/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "RuleCraft.h"
#include "RuleTerrain.h"
#include "../Engine/Exception.h"
#include "../Engine/RNG.h"
#include "../Engine/ScriptBind.h"
#include "Mod.h"

namespace OpenXcom
{

const std::string RuleCraft::DEFAULT_CRAFT_DEPLOYMENT_PREVIEW = "STR_CRAFT_DEPLOYMENT_PREVIEW";

/**
 * Creates a blank ruleset for a certain
 * type of craft.
 * @param type String defining the type.
 */
RuleCraft::RuleCraft(const std::string &type, int listOrder) :
	_type(type), _sprite(-1), _marker(-1), _hangarType(-1), _weapons(0), _maxUnitsLimit(-1), _pilots(0), _maxVehiclesAndLargeSoldiersLimit(-1),
	_maxSmallSoldiers(-1), _maxLargeSoldiers(-1), _maxSmallVehicles(-1), _maxLargeVehicles(-1),
	_maxSmallUnits(-1), _maxLargeUnits(-1), _maxSoldiers(-1), _maxVehicles(-1),
	_monthlyBuyLimit(0), _costBuy(0), _costRent(0), _costSell(0), _repairRate(1), _refuelRate(1),
	_transferTime(24), _score(0), _battlescapeTerrainData(0), _maxSkinIndex(0),
	_keepCraftAfterFailedMission(false), _allowLanding(true), _spacecraft(false), _notifyWhenRefueled(false), _autoPatrol(false), _undetectable(false),
	_missilePower(0),
	_listOrder(listOrder), _maxAltitude(-1), _defaultAltitude("STR_VERY_LOW"), _onlyOneSoldierGroupAllowed(false), _stats(),
	_shieldRechargeAtBase(1000),
	_mapVisible(true), _forceShowInMonthlyCosts(false), _useAllStartTiles(false)
{
	for (int i = 0; i < WeaponMax; ++ i)
	{
		for (int j = 0; j < WeaponTypeMax; ++j)
			_weaponTypes[i][j] = 0;
	}
	_stats.radarRange = 672;
	_stats.radarChance = 100;
	_stats.sightRange = 1696;
	_stats.maxItems = 999999;
	_stats.maxStorageSpace = 99999.0;
	_weaponStrings[0] = "STR_WEAPON_ONE";
	_weaponStrings[1] = "STR_WEAPON_TWO";
}

/**
 *
 */
RuleCraft::~RuleCraft()
{
	delete _battlescapeTerrainData;
}

/**
 * Loads the craft from a YAML file.
 * @param node YAML node.
 * @param mod Mod for the craft.
 * @param modIndex A value that offsets the sounds and sprite values to avoid conflicts.
 * @param listOrder The list weight for this craft.
 */
void RuleCraft::load(const YAML::YamlNodeReader& node, Mod *mod, const ModScript &parsers)
{
	const auto& reader = node.useIndex();
	if (const auto& parent = reader["refNode"])
	{
		load(parent, mod, parsers);
	}

	//requires
	mod->loadUnorderedNames(_type, _requires, reader["requires"]);
	mod->loadBaseFunction(_type, _requiresBuyBaseFunc, reader["requiresBuyBaseFunc"]);
	reader.tryRead("requiresBuyCountry", _requiresBuyCountry);

	if (reader["sprite"])
	{
		// used in
		// Surface set (baseOffset):
		//   BASEBITS.PCK (33)
		//   INTICON.PCK (11)
		//   INTICON.PCK (0)
		//
		// Final index in surfaceset is `baseOffset + sprite + (sprite > 4 ? modOffset : 0)`
		_sprite = mod->getOffset(reader["sprite"].readVal(_sprite), 4);
	}
	if (const auto& skinSprites = reader["skinSprites"])
	{
		_skinSprites.clear();
		for (const auto& skinSprite : skinSprites.children())
		{
			int tmp = mod->getOffset(skinSprite.readVal<int>(), 4);
			_skinSprites.push_back(tmp);
		}
	}
	_stats.load(reader);
	if (reader["marker"])
	{
		_marker = mod->getOffset(reader["marker"].readVal(_marker), 8);
	}
	reader.tryRead("hangarType", _hangarType);  // Added from HEAD
	reader.tryRead("weapons", _weapons);
	reader.tryRead("maxUnitsLimit", _maxUnitsLimit);
	reader.tryRead("pilots", _pilots);
	reader.tryRead("maxHWPUnitsLimit", _maxVehiclesAndLargeSoldiersLimit);
	reader.tryRead("maxSmallSoldiers", _maxSmallSoldiers);
	reader.tryRead("maxLargeSoldiers", _maxLargeSoldiers);
	reader.tryRead("maxSmallVehicles", _maxSmallVehicles);
	reader.tryRead("maxLargeVehicles", _maxLargeVehicles);
	reader.tryRead("maxSmallUnits", _maxSmallUnits);
	reader.tryRead("maxLargeUnits", _maxLargeUnits);
	reader.tryRead("maxSoldiers", _maxSoldiers);
	reader.tryRead("maxVehicles", _maxVehicles);
	reader.tryRead("monthlyBuyLimit", _monthlyBuyLimit);
	reader.tryRead("costBuy", _costBuy);
	reader.tryRead("costRent", _costRent);
	reader.tryRead("costSell", _costSell);
	mod->loadName(_type, _refuelItemName, reader["refuelItem"]);  // Adjusted to use `reader`
	reader.tryRead("repairRate", _repairRate);
	reader.tryRead("refuelRate", _refuelRate);
	reader.tryRead("transferTime", _transferTime);
	reader.tryRead("score", _score);

	if (const auto& terrain = reader["battlescapeTerrainData"])  // Used `reader` consistently
	{
		RuleTerrain *rule = new RuleTerrain(terrain["name"].readVal<std::string>());
		rule->load(terrain, mod);
		_battlescapeTerrainData = rule;
	}
	reader.tryRead("craftInventoryTile", _craftInventoryTile);
	mod->loadUnorderedInts(_type, _groups, reader["groups"]);
	mod->loadUnorderedInts(_type, _allowedSoldierGroups, reader["allowedSoldierGroups"]);
	mod->loadUnorderedInts(_type, _allowedArmorGroups, reader["allowedArmorGroups"]);
	reader.tryRead("onlyOneSoldierGroupAllowed", _onlyOneSoldierGroupAllowed);
	reader.tryRead("maxSkinIndex", _maxSkinIndex);
	reader.tryRead("deployment", _deployment);
	reader.tryRead("keepCraftAfterFailedMission", _keepCraftAfterFailedMission);
	reader.tryRead("allowLanding", _allowLanding);
	reader.tryRead("spacecraft", _spacecraft);
	reader.tryRead("notifyWhenRefueled", _notifyWhenRefueled);
	reader.tryRead("autoPatrol", _autoPatrol);
	reader.tryRead("undetectable", _undetectable);
	reader.tryRead("missilePower", _missilePower);
	reader.tryRead("listOrder", _listOrder);
	reader.tryRead("maxAltitude", _maxAltitude);
	reader.tryRead("defaultAltitude", _defaultAltitude);

	if (const auto& types = reader["weaponTypes"])
	{
		size_t max = std::min(types.childrenCount(), (size_t)WeaponMax);
		for (size_t i = 0; i < max; ++i)
		{
			const auto& type = types[i];
			if (type.hasVal())
			{
				for (int j = 0; j < WeaponTypeMax; ++j)
					_weaponTypes[i][j] = type.readVal<int>();
			}
			else if (type.isSeq())
			{
				for (int j = 0; (size_t)j < type.childrenCount() && j < WeaponTypeMax; ++j)
					_weaponTypes[i][j] = type[j].readVal<int>();
				for (int j = type.childrenCount(); j < WeaponTypeMax; ++j)
					_weaponTypes[i][j] = _weaponTypes[i][0];
			}
			else
			{
				throw Exception("Invalid weapon type in craft " + _type + ".");
			}
		}
	}
	if (const auto& str = reader["weaponStrings"])
	{
		for (int i = 0; (size_t)i < str.childrenCount() &&  i < WeaponMax; ++i)
			_weaponStrings[i] = str[i].readVal<std::string>();
	}
	if (const auto& str = reader["fixedWeapons"])
	{
		for (int i = 0; (size_t)i < str.childrenCount() && i < WeaponMax; ++i)
			_fixedWeaponNames[i] = str[i].readVal<std::string>();
	}
	reader.tryRead("shieldRechargedAtBase", _shieldRechargeAtBase);
	reader.tryRead("mapVisible", _mapVisible);
	reader.tryRead("forceShowInMonthlyCosts", _forceShowInMonthlyCosts);
	reader.tryRead("useAllStartTiles", _useAllStartTiles);
	reader.tryRead("customPreview", _customPreview);

	mod->loadSoundOffset(_type, _selectSound, reader["selectSound"], "GEO.CAT");
	mod->loadSoundOffset(_type, _takeoffSound, reader["takeoffSound"], "GEO.CAT");

	reader.tryRead("pilotMinStatsRequired", _pilotMinStatsRequired);
	mod->loadNames(_type, _pilotSoldierBonusesRequiredNames, reader["pilotSoldierBonusesRequired"]);

	_craftScripts.load(_type, reader, parsers.craftScripts);
	_scriptValues.load(reader, parsers.getShared());
}

/**
 * Cross link with other rules.
 */
void RuleCraft::afterLoad(const Mod* mod)
{
	mod->linkRule(_refuelItem, _refuelItemName);
	mod->linkRule(_pilotSoldierBonusesRequired, _pilotSoldierBonusesRequiredNames);

	// No turning soldiers into antimatter
	mod->checkForSoftError(_stats.soldiers < 0, _type, "Default unit capacity cannot be negative.", LOG_ERROR);
	mod->checkForSoftError(_stats.vehicles < 0, _type, "Default HWP capacity cannot be negative.", LOG_ERROR);

	// Backwards-compatibility
	if (_maxUnitsLimit < 0)
	{
		_maxUnitsLimit = _stats.soldiers;
	}
	if (_maxVehiclesAndLargeSoldiersLimit < 0)
	{
		_maxVehiclesAndLargeSoldiersLimit = _stats.vehicles;
	}

	// Sanity checks
	mod->checkForSoftError(_maxUnitsLimit < _stats.soldiers, _type, "Maximum unit capacity is smaller than the default unit capacity.", LOG_ERROR);
	mod->checkForSoftError(_maxVehiclesAndLargeSoldiersLimit < _stats.vehicles, _type, "Maximum HWP capacity is smaller than the default HWP capacity.", LOG_ERROR);
}

/**
 * Gets the language string that names
 * this craft. Each craft type has a unique name.
 * @return The craft's name.
 */
const std::string &RuleCraft::getType() const
{
	return _type;
}

/**
 * Gets the list of research required to
 * acquire this craft.
 * @return The list of research IDs.
 */
const std::vector<std::string> &RuleCraft::getRequirements() const
{
	return _requires;
}

/**
 * Gets the ID of the sprite used to draw the craft
 * in the Basescape and Equip Craft screens.
 * @return The Sprite ID.
 */
int RuleCraft::getSprite(int skinIndex) const
{
	if (skinIndex > 0)
	{
		size_t vectorIndex = skinIndex - 1;
		if (vectorIndex < _skinSprites.size())
		{
			return _skinSprites[vectorIndex];
		}
	}
	return _sprite;
}

/**
 * Returns the globe marker for the craft type.
 * @return Marker sprite, -1 if none.
 */
int RuleCraft::getMarker() const
{
	return _marker;
}

/**
 * Returns the hangar type id in which
 * craft can be allocated.
 * @return hangar type ID, -1 if none.
 */
int RuleCraft::getHangarType() const	
{
	return _hangarType;
}

/**
 * Gets the maximum fuel the craft can contain.
 * @return The fuel amount.
 */
int RuleCraft::getMaxFuel() const
{
	return _stats.fuelMax;
}

/**
 * Gets the maximum damage (damage the craft can take)
 * of the craft.
 * @return The maximum damage.
 */
int RuleCraft::getMaxDamage() const
{
	return _stats.damageMax;
}

/**
 * Gets the maximum speed of the craft flying
 * around the Geoscape.
 * @return The speed in knots.
 */
int RuleCraft::getMaxSpeed() const
{
	return _stats.speedMax;
}

/**
 * Gets the acceleration of the craft for
 * taking off / stopping.
 * @return The acceleration.
 */
int RuleCraft::getAcceleration() const
{
	return _stats.accel;
}

/**
 * Gets the maximum number of weapons that
 * can be equipped onto the craft.
 * @return The weapon capacity.
 */
int RuleCraft::getWeapons() const
{
	return _weapons;
}

/**
 * Checks if this craft is supported in the New Battle mode (and Preview mode).
 */
bool RuleCraft::isForNewBattle() const
{
	return getBattlescapeTerrainData() && getMaxUnitsLimit() > 0 && getAllowLanding();
}

/**
 * Gets the maximum number of units (soldiers and vehicles, small and large) that
 * the craft can carry.
 * @return The maximum unit capacity.
 */
int RuleCraft::getMaxUnits() const
{
	return _stats.soldiers;
}

/**
* Gets the number of pilots that the craft requires in order to take off.
* @return The number of pilots.
*/
int RuleCraft::getPilots() const
{
	return _pilots;
}

/**
 * Gets the maximum number of vehicles (and 2x2 soldiers) that
 * the craft can carry.
 * @return The maximum vehicle capacity (incl. 2x2 soldiers).
 */
int RuleCraft::getMaxVehiclesAndLargeSoldiers() const
{
	return _stats.vehicles;
}

/**
 * Gets the cost of this craft for
 * purchase/rent (0 if not purchasable).
 * @return The cost.
 */
int RuleCraft::getBuyCost() const
{
	return _costBuy;
}

/**
 * Gets the cost of rent for a month.
 * @return The cost.
 */
int RuleCraft::getRentCost() const
{
	return _costRent;
}

/**
 * Gets the sell value of this craft
 * Rented craft should use 0.
 * @return The sell value.
 */
int RuleCraft::getSellCost() const
{
	return _costSell;
}

/**
 * Gets what item is required while
 * the craft is refuelling.
 * @return The item ID or "" if none.
 */
const RuleItem* RuleCraft::getRefuelItem() const
{
	return _refuelItem;
}

/**
 * Gets how much damage is removed from the
 * craft while repairing.
 * @return The amount of damage.
 */
int RuleCraft::getRepairRate() const
{
	return _repairRate;
}

/**
 * Gets how much fuel is added to the
 * craft while refuelling.
 * @return The amount of fuel.
 */
int RuleCraft::getRefuelRate() const
{
	return _refuelRate;
}

/**
 * Gets the craft's radar range
 * for detecting UFOs.
 * @return The range in nautical miles.
 */
int RuleCraft::getRadarRange() const
{
	return _stats.radarRange;
}

/**
 * Gets the craft's radar chance
 * for detecting UFOs.
 * @return The chance in percentage.
 */
int RuleCraft::getRadarChance() const
{
	return _stats.radarChance;
}

/**
 * Gets the craft's sight range
 * for detecting bases.
 * @return The range in nautical miles.
 */
int RuleCraft::getSightRange() const
{
	return _stats.sightRange;
}

/**
 * Gets the amount of time this item
 * takes to arrive at a base.
 * @return The time in hours.
 */
int RuleCraft::getTransferTime() const
{
	return _transferTime;
}

/**
 * Gets the number of points you lose
 * when this craft is destroyed.
 * @return The score in points.
 */
int RuleCraft::getScore() const
{
	return _score;
}

/**
 * Gets the terrain data needed to draw the Craft in the battlescape.
 * @return The terrain data.
 */
RuleTerrain *RuleCraft::getBattlescapeTerrainData() const
{
	return _battlescapeTerrainData;
}

/**
 * Checks if this craft is lost after a failed mission or not.
 * @return True if this craft is NOT lost (e.g. paratroopers).
 */
bool RuleCraft::keepCraftAfterFailedMission() const
{
	return _keepCraftAfterFailedMission;
}

/**
 * Checks if this craft is capable of landing (on missions).
 * @return True if this ship is capable of landing (on missions).
 */
bool RuleCraft::getAllowLanding() const
{
	return _allowLanding;
}

/**
 * Checks if this ship is capable of going to mars.
 * @return True if this ship is capable of going to mars.
 */
bool RuleCraft::getSpacecraft() const
{
	return _spacecraft;
}

/**
 * Checks if a notification should be displayed when the craft is refueled.
 * @return True if notification should appear.
 */
bool RuleCraft::notifyWhenRefueled() const
{
	return _notifyWhenRefueled;
}

/**
* Checks if the craft supports auto patrol feature.
* @return True if auto patrol is supported.
*/
bool RuleCraft::canAutoPatrol() const
{
	return _autoPatrol;
}

/**
 * Gets the list weight for this research item.
 * @return The list weight.
 */
int RuleCraft::getListOrder() const
{
	return _listOrder;
}

/**
 * Gets the deployment layout for this craft.
 * @return The deployment layout.
 */
const RuleCraftDeployment &RuleCraft::getDeployment() const
{
	return _deployment;
}

/**
* Gets the craft inventory tile position.
* @return The tile position.
*/
const std::vector<int> &RuleCraft::getCraftInventoryTile() const
{
	return _craftInventoryTile;
}

/**
 * Gets the maximum amount of items this craft can store.
 * @return number of items.
 */
int RuleCraft::getMaxItems() const
{
	return _stats.maxItems;
}

/**
 * Gets the maximum storage space for items in this craft.
 * @return storage space limit.
 */
double RuleCraft::getMaxStorageSpace() const
{
	return _stats.maxStorageSpace;
}

/**
 * Test for possibility of usage of weapon type in weapon slot.
 * @param slot value less than WeaponMax.
 * @param weaponType weapon type of weapon that we try insert.
 * @return True if can use.
 */
bool RuleCraft::isValidWeaponSlot(int slot, int weaponType) const
{
	for (int j = 0; j < WeaponTypeMax; ++j)
	{
		if (_weaponTypes[slot][j] == weaponType)
			return true;
	}
	return false;
}

int RuleCraft::getWeaponTypesRaw(int slot, int subslot) const
{
	return _weaponTypes[slot][subslot];
}

/**
 * Return string ID of weapon slot name for geoscape craft state.
 * @param slot value less than WeaponMax.
 * @return String ID for translation.
 */
const std::string &RuleCraft::getWeaponSlotString(int slot) const
{
	return _weaponStrings[slot];
}

/**
 * Gets the string ID of a fixed weapon in a given slot.
 * @param slot value less than WeaponMax.
 * @return String ID.
 */
const std::string &RuleCraft::getFixedWeaponInSlot(int slot) const
{
	return _fixedWeaponNames[slot];
}

/**
 * Gets basic statistic of craft.
 * @return Basic stats of craft.
 */
const RuleCraftStats& RuleCraft::getStats() const
{
	return _stats;
}

/**
 * Gets the maximum altitude this craft can dogfight to.
 * @return max altitude (0-4).
 */
int RuleCraft::getMaxAltitude() const
{
	return _maxAltitude;
}

/**
 * Gets the craft's default display altitude.
 * @return String ID of an altitude for display purposes.
 */
const std::string& RuleCraft::getDefaultDisplayAltitude() const
{
	return _defaultAltitude;
}

/**
 * If the craft is underwater, it can only dogfight over polygons.
 * TODO: Replace this with its own flag.
 * @return underwater or not
 */
bool RuleCraft::isWaterOnly() const
{
	return _maxAltitude > -1;
}

/**
 * Gets how many shield points are recharged when landed at base per hour
 * @return shield recharged per hour
 */
int RuleCraft::getShieldRechargeAtBase() const
{
	return _shieldRechargeAtBase;
}

/**
 * Gets whether or not the craft map should be visible at the beginning of the battlescape
 * @return visible or not?
 */
bool RuleCraft::isMapVisible() const
{
	return _mapVisible;
}

/**
 * Gets whether or not the craft type should be displayed in Monthly Costs even if not present in the base.
 * @return visible or not?
 */
bool RuleCraft::forceShowInMonthlyCosts() const
{
	return _forceShowInMonthlyCosts;
}

/**
 * Can the player utilize all start tiles on a craft or only the ones specified in the '_deployment' list?
 * @return can use all or not?
 */
bool RuleCraft::useAllStartTiles() const
{
	return _useAllStartTiles;
}

/**
 * Gets the craft's custom preview type.
 * @return String ID of an alienDeployment.
 */
const std::string& RuleCraft::getCustomPreviewType() const
{
	return _customPreview.empty() ? DEFAULT_CRAFT_DEPLOYMENT_PREVIEW : _customPreview;
}

/**
 * Gets the craft's custom preview type.
 * @return String ID of an alienDeployment or an empty string.
 */
const std::string& RuleCraft::getCustomPreviewTypeRaw() const
{
	return _customPreview;
}

/**
 * Calculates the theoretical range of the craft
 * This depends on when you launch the craft as fuel is consumed only on exact 10 minute increments
 * @param type Which calculation should we do? 0 = maximum, 1 = minimum, 2 = average of the two
 * @return The calculated range
 */
int RuleCraft::calculateRange(int type)
{
	// If the craft uses an item to refuel, the tick rate is one fuel unit per 10 minutes
	int totalFuelTicks = _stats.fuelMax;

	// If no item is used to refuel, the tick rate depends on speed
	if (_refuelItem == nullptr)
	{
		// Craft with less than 100 speed don't consume fuel and therefore have infinite range
		if (_stats.speedMax < 100)
		{
			return -1;
		}

		totalFuelTicks = _stats.fuelMax / (_stats.speedMax / 100);
	}

	// Six ticks per hour, factor return trip and speed for total range
	int range;
	switch (type)
	{
		// Min range happens when the craft is sent at xx:x9:59, as a unit of fuel is immediately consumed, so we subtract an extra 'tick' of fuel in this case
		case 1:
			range = (totalFuelTicks - 1) * _stats.speedMax / 12;
			break;
		case 2:
			range = (2 * totalFuelTicks - 1) * _stats.speedMax / 12 / 2;
			break;
		default :
			range = totalFuelTicks * _stats.speedMax / 12;
			break;
	}

	return range;
}

/**
 * Gets a random sound id from a given sound vector.
 * @param vector The source vector.
 * @param defaultValue Default value (in case nothing is specified = vector is empty).
 * @return The sound id.
 */
int RuleCraft::getRandomSound(const std::vector<int>& vector, int defaultValue) const
{
	if (!vector.empty())
	{
		return vector[RNG::generate(0, vector.size() - 1)];
	}
	return defaultValue;
}

/**
 * Gets the sound played when the player directly selects a craft on the globe.
 * @return The select sound id.
 */
int RuleCraft::getSelectSound() const
{
	return getRandomSound(_selectSound);
}

/**
 * Gets the sound played when a craft takes off from a base.
 * @return The takeoff sound id.
 */
int RuleCraft::getTakeoffSound() const
{
	return getRandomSound(_takeoffSound);
}


////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

void getTypeScript(const RuleCraft* r, ScriptText& txt)
{
	if (r)
	{
		txt = { r->getType().c_str() };
		return;
	}
	else
	{
		txt = ScriptText::empty;
	}
}

std::string debugDisplayScript(const RuleCraft* rc)
{
	if (rc)
	{
		std::string s;
		s += RuleCraft::ScriptName;
		s += "(type: \"";
		s += rc->getType();
		s += "\"";
		s += ")";
		return s;
	}
	else
	{
		return "null";
	}
}

} // namespace

/**
 * Register Type in script parser.
 * @param parser Script parser.
 */
void RuleCraft::ScriptRegister(ScriptParserBase* parser)
{
	Bind<RuleCraft> b = { parser };

	b.add<&getTypeScript>("getType");

	b.add<&RuleCraft::getWeapons>("getWeaponsMax");
	b.add<&RuleCraft::getMaxUnits>("getSoldiersMax");
	b.add<&RuleCraft::getMaxVehiclesAndLargeSoldiers>("getVehiclesMax");
	b.add<&RuleCraft::getPilots>("getPilotsMax");

	RuleCraftStats::addGetStatsScript<&RuleCraft::_stats>(b, "Stats.");

	b.addScriptValue<&RuleCraft::_scriptValues>();
	b.addDebugDisplay<&debugDisplayScript>();
}

}

