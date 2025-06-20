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
#include "Vehicle.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

/**
 * Initializes a vehicle of the specified type.
 * @param rules Pointer to ruleset.
 * @param ammo Initial ammo.
 * @param size Size in tiles.
 * @param spaceOccupied Space occupied in a craft.
 */
Vehicle::Vehicle(const RuleItem* rules, int ammo, int size, int spaceOccupied) : _rules(rules), _ammo(ammo), _size(size), _spaceOccupied(spaceOccupied)
{
}

/**
 *
 */
Vehicle::~Vehicle()
{
}

/**
 * Loads the vehicle from a YAML file.
 * @param node YAML node.
 */
void Vehicle::load(const YAML::YamlNodeReader& reader)
{
	reader.tryRead("ammo", _ammo);
	reader.tryRead("size", _size);
	reader.tryRead("spaceOccupied", _spaceOccupied);
}

/**
 * Saves the base to a YAML file.
 * @return YAML node.
 */
void Vehicle::save(YAML::YamlNodeWriter writer) const
{
	writer.setAsMap();
	writer.write("type", _rules->getType());
	writer.write("ammo", _ammo);
	writer.write("size", _size);
	writer.write("spaceOccupied", _spaceOccupied);
}

/**
 * Returns the ruleset for the vehicle's type.
 * @return Pointer to ruleset.
 */
const RuleItem *Vehicle::getRules() const
{
	return _rules;
}

/**
 * Returns the ammo contained in this vehicle.
 * @return Weapon ammo.
 */
int Vehicle::getAmmo() const
{
	if (_ammo == -1)
	{
		return 255;
	}
	return _ammo;
}

/**
 * Changes the ammo contained in this vehicle.
 * @param ammo Weapon ammo.
 */
void Vehicle::setAmmo(int ammo)
{
	if (_ammo != -1)
	{
		_ammo = ammo;
	}
}

/**
 * Returns the size occupied by this vehicle
 * in a transport craft.
 * @return Size in tiles.
 */
int Vehicle::getTotalSize() const
{
	return _size;
}

/**
 * Gets how much space the armor occupies in a craft. If not specified, uses armor size.
 * @return Space occupied by the vehicle.
 */
int Vehicle::getSpaceOccupied() const
{
	return (_spaceOccupied > -1 ? _spaceOccupied : getTotalSize());
}

}
