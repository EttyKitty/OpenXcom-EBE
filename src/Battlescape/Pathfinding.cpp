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
#include <list>
#include <algorithm>
#include "Pathfinding.h"
#include "PathfindingOpenSet.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/Armor.h"
#include "../Mod/Mod.h"
#include "../Savegame/BattleUnit.h"
#include "../Engine/Options.h"
#include "../fmath.h"
#include "BattlescapeGame.h"

namespace OpenXcom
{

constexpr int Pathfinding::dir_x[Pathfinding::dir_max];
constexpr int Pathfinding::dir_y[Pathfinding::dir_max];
constexpr int Pathfinding::dir_z[Pathfinding::dir_max];

int Pathfinding::red = 3;
int Pathfinding::yellow = 10;
int Pathfinding::brown = 11;
int Pathfinding::green = 4;
int Pathfinding::white = 6;


/**
 * Sets up a Pathfinding.
 * @param save pointer to SavedBattleGame object.
 */
Pathfinding::Pathfinding(SavedBattleGame *save) : _save(save), _unit(0), _pathPreviewed(false), _strafeMove(false)
{
	_size = _save->getMapSizeXYZ();
	// Initialize one node per tile
	_nodes.reserve(_size);
	_altNodes.reserve(_size);
	for (int i = 0; i < _size; ++i)
	{
		_nodes.push_back(PathfindingNode(_save->getTileCoords(i)));
		_altNodes.push_back(PathfindingNode(_save->getTileCoords(i)));
	}
}

/**
 * Deletes the Pathfinding.
 * @internal This is required to be here because it requires the PathfindingNode class definition.
 */
Pathfinding::~Pathfinding()
{
	// Nothing more to do here.
}

/**
 * Gets the Node on a given position on the map.
 * @param pos Position.
 * @return Pointer to node.
 */
PathfindingNode *Pathfinding::getNode(Position pos, bool alt)
{
	if (alt)
		return &_altNodes[_save->getTileIndex(pos)];
	return &_nodes[_save->getTileIndex(pos)];
}

/**
 * Calculates the shortest path.
 * @param unit Unit taking the path.
 * @param endPosition The position we want to reach.
 * @param missileTarget Target of the path.
 * @param maxTUCost Maximum time units the path can cost.
 */
void Pathfinding::calculate(BattleUnit *unit, Position endPosition, BattleActionMove bam, const BattleUnit *missileTarget, int maxTUCost)
{
	calculate(unit, unit->getPosition(), endPosition, bam, missileTarget, maxTUCost);
}

/**
 * Calculates the shortest path.
 * @param unit Unit taking the path.
 * @param startPosition The position we want to start from.
 * @param endPosition The position we want to reach.
 * @param missileTarget Target of the path.
 * @param maxTUCost Maximum time units the path can cost.
 */
void Pathfinding::calculate(BattleUnit *unit, Position startPosition, Position endPosition, BattleActionMove bam, const BattleUnit *missileTarget, int maxTUCost)
{
	_totalTUCost = {};
	_path.clear();

	const int size = bam != BAM_MISSILE ? unit->getArmor()->getSize() : 1;

	// i'm DONE with these out of bounds errors.
	if (endPosition.x > _save->getMapSizeX() - size || endPosition.y > _save->getMapSizeY() - size || endPosition.x < 0 || endPosition.y < 0) return;

	bool sneak = Options::sneakyAI && unit->getFaction() == FACTION_HOSTILE && !unit->isBrutal();

	auto movementType = getMovementType(unit, missileTarget, bam);
	if (missileTarget != 0 && maxTUCost == -1 && bam == BAM_MISSILE) // pathfinding for missile
	{
		maxTUCost = 10000;
	}
	_unit = unit;

	const Tile* destinationTile = _save->getTile(endPosition);

	// check if destination is not blocked
	if (isBlocked(_unit, destinationTile, O_FLOOR, bam, missileTarget) || isBlocked(_unit, destinationTile, O_OBJECT, bam, missileTarget))
		return;

	// the following check avoids that the unit walks behind the stairs if we click behind the stairs to make it go up the stairs.
	// it only works if the unit is on one of the 2 tiles on the stairs, or on the tile right in front of the stairs.
	if (isOnStairs(startPosition, endPosition) && unit->getFaction() == FACTION_PLAYER) //Xilmi: Yeah... but not if the AI actually wants to go behind the stairs!
	{
		endPosition.z++;
		destinationTile = _save->getTile(endPosition);
	}
	while (endPosition.z != _save->getMapSizeZ() && destinationTile->getTerrainLevel() == -24)
	{
		endPosition.z++;
		destinationTile = _save->getTile(endPosition);
	}
	// make sure we didn't just try to adjust the Z of our destination outside the map
	// this occurs in rare circumstances where an object has terrainLevel -24 on the top floor
	// and is considered passable terrain for whatever reason (usually bigwall type objects)
	if (endPosition.z == _save->getMapSizeZ())
	{
		return; // Icarus is a bad role model for XCom soldiers.
	}
	if (movementType != MT_FLY && bam != BAM_MISSILE)
	{
		// check if we have floor, else lower destination (for non flying units only, because otherwise they never reached this place)
		while (canFallDown(destinationTile, size) && !(size == 1 && destinationTile->hasLadder()))
		{
			endPosition.z--;
			destinationTile = _save->getTile(endPosition);
		}
	}

	// Strafing move allowed only to adjacent squares on same z. "Same z" rule mainly to simplify walking render.
	_strafeMove = bam == BAM_STRAFE && (startPosition.z == endPosition.z) &&
				  (abs(startPosition.x - endPosition.x) <= 1) && (abs(startPosition.y - endPosition.y) <= 1);

	if (_strafeMove)
	{
		auto direction = -1;
		vectorToDirection(endPosition - startPosition, direction);
		if (direction == -1 || std::min(abs(8 + direction - _unit->getDirection()), std::min(abs(_unit->getDirection() - direction), abs(8 + _unit->getDirection() - direction))) > 2)
		{
			// Strafing backwards-ish currently unsupported, turn it off and continue.
			_strafeMove = false;
		}
		else if (getTUCost(startPosition, direction, _unit, 0, bam).cost.time == INVALID_MOVE_COST)
		{
			// we can't reach in one step
			_strafeMove = false;
		}
	}

	// look for a possible fast and accurate bresenham path and skip A*
	if (!unit->isBrutal() && bresenhamPath(startPosition, endPosition, bam, missileTarget, sneak))
	{
		std::reverse(_path.begin(), _path.end()); // paths are stored in reverse order
		return;
	}
	else
	{
		abortPath(); // if bresenham failed, we shouldn't keep the path it was attempting, in case A* fails too.
	}
	// Now try through A*.
	if (!aStarPath(startPosition, endPosition, bam, missileTarget, sneak, maxTUCost))
	{
		abortPath();
	}
}

/**
 * Calculates the shortest path using a simple A-Star algorithm.
 * The unit information and movement type must have already been set.
 * The path information is set only if a valid path is found.
 * @param startPosition The position to start from.
 * @param endPosition The position we want to reach.
 * @param missileTarget Target of the path.
 * @param sneak Is the unit sneaking?
 * @param maxTUCost Maximum time units the path can cost.
 * @return True if a path exists, false otherwise.
 */
bool Pathfinding::aStarPath(Position startPosition, Position endPosition, BattleActionMove bam, const BattleUnit *missileTarget, bool sneak, int maxTUCost)
{
	// reset every node, so we have to check them all
	for (auto& pn : _nodes)
	{
		pn.reset();
	}

	// start position is the first one in our "open" list
	PathfindingNode *start = getNode(startPosition);
	start->connect({}, 0, 0, endPosition);
	PathfindingOpenSet openList;
	openList.push(start);
	bool missile = (bam == BAM_MISSILE);
	// if the open list is empty, we've reached the end
	while (!openList.empty())
	{
		PathfindingNode *currentNode = openList.pop();
		Position const &currentPos = currentNode->getPosition();
		currentNode->setChecked();
		if (currentPos == endPosition) // We found our target.
		{
			_path.clear();
			PathfindingNode *pf = currentNode;
			while (pf->getPrevNode())
			{
				_path.push_back(pf->getPrevDir());
				pf = pf->getPrevNode();
			}
			return true;
		}

		// Try all reachable neighbours.
		for (int direction = 0; direction < 10; direction++)
		{
			auto r = getTUCost(currentPos, direction, _unit, missileTarget, bam);
			if (r.cost.time == INVALID_MOVE_COST) // Skip unreachable / blocked
				continue;

			Position nextPos = r.pos;
			if (sneak && _save->getTile(nextPos)->getVisible()) r.cost.time *= 2; // avoid being seen
			PathfindingNode *nextNode = getNode(nextPos);
			if (nextNode->isChecked()) // Our algorithm means this node is already at minimum cost.
				continue;
			_totalTUCost = currentNode->getTUCost(missile) + r.cost + r.penalty;
			// If this node is unvisited or has only been visited from inferior paths...
			if ((!nextNode->inOpenSet() || nextNode->getTUCost(missile).time > _totalTUCost.time) && _totalTUCost.time <= maxTUCost)
			{
				nextNode->connect(_totalTUCost, currentNode, direction, endPosition);
				openList.push(nextNode);
			}
		}
	}
	// Unable to reach the target
	return false;
}

/**
 * Gets the TU cost to move from 1 tile to the other (ONE STEP ONLY).
 * But also updates the endPosition, because it is possible
 * the unit goes upstairs or falls down while walking.
 * @param startPosition The position to start from.
 * @param direction The direction we are facing.
 * @param endPosition The position we want to reach.
 * @param unit The unit moving.
 * @param missileTarget The target unit used for BAM_MISSILE.
 * @param bam What move type is required (one special case is BAM_MISSILE)?
 * @return TU cost or 255 if movement is impossible.
 */
PathfindingStep Pathfinding::getTUCost(Position startPosition, int direction, const BattleUnit *unit, const BattleUnit *missileTarget, BattleActionMove bam) const
{
	Position pos;
	directionToVector(direction, &pos);
	pos += startPosition;

	const auto movementType = getMovementType(unit, missileTarget, bam);
	const Armor* armor =  unit->getArmor();
	const int size = armor->getSize() - 1;
	const int numberOfParts = armor->getTotalSize();
	int maskOfPartsGoingUp = 0x0;
	int maskOfPartsHoleUp = 0x0;
	int maskOfPartsGoingDown = 0x0;
	int maskOfPartsFalling = 0x0;
	int maskOfPartsFlying = 0x0;
	int maskOfPartsGround = 0x0;
	int maskOfPartsClimb = 0x0;
	int maskArmor = size ? 0xF : 0x1;

	Position offsets[4] =
	{
		{ 0, 0, 0 },
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 1, 1, 0 },
	};
	const Tile* startTile[4] = { };
	const Tile* aboveStart[4] = { };
	const Tile* belowStart[4] = { };
	const Tile* destinationTile[4] = { };
	const Tile* aboveDestination[4] = { };
	const Tile* belowDestination[4] = { };

	// init variables
	for (int i = 0; i < numberOfParts; ++i)
	{
		const Tile* st = _save->getTile(startPosition + offsets[i]);
		const Tile* dt = _save->getTile(pos + offsets[i]);
		if (!st || !dt)
		{
			return {{INVALID_MOVE_COST, 0}};
		}
		startTile[i] = st;
		aboveStart[i] = _save->getAboveTile(st);
		belowStart[i] = _save->getBelowTile(st);
		destinationTile[i] = dt;
		aboveDestination[i] = _save->getAboveTile(dt);
		belowDestination[i] = _save->getBelowTile(dt);
	}

	// check move up or down
	for (int i = 0; i < numberOfParts; ++i)
	{
		const int maskCurrentPart = 1 << i;
		const bool checkClimbing = (i == 0) && (numberOfParts == 1) &&  movementType != MT_FLY;

		if (direction < DIR_UP && startTile[i]->getTerrainLevel() > - 16)
		{
			// check if we can go this way
			if (isBlockedDirection(unit, startTile[i], direction, bam, missileTarget))
				return {{INVALID_MOVE_COST, 0}};
			if (startTile[i]->getTerrainLevel() - destinationTile[i]->getTerrainLevel() > 8)
				return {{INVALID_MOVE_COST, 0}};
		}

		// if we are on a stairs try to go up a level
		if (direction < DIR_UP && startTile[i]->getTerrainLevel() <= -16 && aboveDestination[i] && !aboveDestination[i]->hasNoFloor(_save))
		{
			maskOfPartsGoingUp |= maskCurrentPart;
		}
		else if (direction < DIR_UP && movementType != MT_FLY && belowDestination[i] && canFallDown(destinationTile[i]) && belowDestination[i]->getTerrainLevel() <= -12)
		{
			maskOfPartsGoingDown |= maskCurrentPart;
		}
		else if (bam != BAM_MISSILE && movementType == MT_FLY)
		{
			// 2 or more voxels poking into this tile = no go
			auto overlaping = destinationTile[i]->getOverlappingUnit(_save, TUO_IGNORE_SMALL);
			bool knowsOfOverlapping = false;
			if (overlaping)
			{
				if (unit->getFaction() == FACTION_PLAYER && overlaping->getVisible())
					knowsOfOverlapping = true; // player know all visible units
				if (unit->getFaction() == overlaping->getFaction() && !_ignoreFriends)
					knowsOfOverlapping = true;
				if (unit->getFaction() != FACTION_PLAYER &&
					std::find(unit->getUnitsSpottedThisTurn().begin(), unit->getUnitsSpottedThisTurn().end(), overlaping) != unit->getUnitsSpottedThisTurn().end())
					knowsOfOverlapping = true;
				if (overlaping != unit && overlaping != missileTarget && knowsOfOverlapping)
				{
					return {{INVALID_MOVE_COST, 0}};
				}
			}
		}

		if (aboveStart[i] && aboveStart[i]->hasNoFloor(_save))
		{
			maskOfPartsHoleUp |= maskCurrentPart;
		}
		// check if we have floor, else fall down
		if (movementType != MT_FLY && canFallDown(startTile[i]))
		{
			if (!checkClimbing || (!startTile[i]->hasLadder() && !(direction < DIR_UP && belowStart[i] && belowStart[i]->hasLadder() && !canFallDown(destinationTile[i]))))
			{
				maskOfPartsFalling |= maskCurrentPart;
			}
		}
		if (movementType != MT_FLY && checkClimbing && direction >= DIR_UP && startTile[i]->hasLadder())
		{
			//TODO: need check interaction with GravLift and other corner cases
			maskOfPartsClimb |= maskCurrentPart;
		}
		if (movementType == MT_FLY && (canFallDown(startTile[i]) || canFallDown(destinationTile[i])))
		{
			maskOfPartsFlying |= maskCurrentPart;
		}
		if (direction != DIR_DOWN && !canFallDown(destinationTile[i]))
		{
			maskOfPartsGround |= maskCurrentPart;
		}
	}

	const bool triedStairs = (maskOfPartsGoingUp != 0 && ((maskOfPartsGoingUp | maskOfPartsHoleUp) == maskArmor));
	const bool triedStairsDown = (maskOfPartsGround == 0 && ((maskOfPartsGoingDown | maskOfPartsFalling) == maskArmor));
	const bool fallingDown = (maskOfPartsFalling == maskArmor);
	const bool flying =  (maskOfPartsFlying == maskArmor);
	const bool climb = (maskOfPartsClimb == maskArmor) && (bam != BAM_MISSILE);

	if (movementType != MT_FLY && fallingDown)
	{
		if (direction != DIR_DOWN)
		{
			return {{INVALID_MOVE_COST, 0}}; //cannot walk on air
		}
	}

	for (int i = 0; i < numberOfParts; ++i)
	{
		if (triedStairs)
		{
			destinationTile[i] = aboveDestination[i];
		}
		else if (direction != DIR_DOWN && triedStairsDown)
		{
			destinationTile[i] = belowDestination[i];
		}

		// check if the destination tile can be walked over
		if (isBlocked(unit, destinationTile[i], O_FLOOR, bam, missileTarget) || isBlocked(unit, destinationTile[i], O_OBJECT, bam, missileTarget))
		{
			return {{INVALID_MOVE_COST, 0}};
		}
	}

	// don't let tanks phase through doors.
	if (size)
	{
		const Tile* t = destinationTile[3];
		if ((t->isDoor(O_NORTHWALL)) ||
			(t->isDoor(O_WESTWALL)))
		{
			return {{INVALID_MOVE_COST, 0}};
		}
	}

	// pre-calculate fire penalty (to make it consistent for 2x2 units)
	auto firePenaltyCost = 0;
	if (unit->getFaction() != FACTION_PLAYER &&
		unit->avoidsFire())
	{
		for (int i = 0; i < numberOfParts; ++i)
		{
			if (destinationTile[i]->getFire() > 0)
			{
				firePenaltyCost = FIRE_PREVIEW_MOVE_COST; // try to find a better path, but don't exclude this path entirely.
			}
		}
	}


	// calculate cost and some final checks
	auto totalCost = 0;

	for (int i = 0; i < numberOfParts; ++i)
	{
		auto cost = 0;
		auto upperLevel = destinationTile[i]->getPosition().z > startTile[i]->getPosition().z;
		auto sameLevel = destinationTile[i]->getPosition().z == startTile[i]->getPosition().z;
		if (direction < DIR_UP && sameLevel)
		{
			// check if we can go this way
			if (isBlockedDirection(unit, startTile[i], direction, bam, missileTarget))
				return {{INVALID_MOVE_COST, 0}};
			if (startTile[i]->getTerrainLevel() - destinationTile[i]->getTerrainLevel() > 8)
				return {{INVALID_MOVE_COST, 0}};
		}
		else if (direction >= DIR_UP && !triedStairsDown)
		{
			// check if we can go up or down through gravlift or fly
			if (validateUpDown(unit, startTile[i]->getPosition(), direction, missileTarget))
			{
				if (climb)
				{
					int minCost = INVALID_MOVE_COST;
					auto calcMin = [&](bool ladder, TilePart p)
					{
						if (ladder)
						{
							int value = startTile[i]->getTUCost(p, movementType);
							minCost = std::min(minCost, value ? value : DEFAULT_MOVE_FLY_COST);
						}
					};

					calcMin(startTile[i]->hasLadderOnObject(), O_OBJECT);
					calcMin(startTile[i]->hasLadderOnNorthWall(), O_NORTHWALL);
					calcMin(startTile[i]->hasLadderOnWestWall(), O_WESTWALL);

					if (minCost >= INVALID_MOVE_COST)
					{
						return {{INVALID_MOVE_COST, 0}};
					}
					cost = minCost;
				}
				else
				{
					cost = DEFAULT_MOVE_FLY_COST; // vertical movement by flying suit or grav lift
				}
			}
			else
			{
				return {{INVALID_MOVE_COST, 0}};
			}
		}
		if (upperLevel)
		{
			startTile[i] = _save->getAboveTile(startTile[i]);

			if (direction < DIR_UP)
			{
				// check if we can go this way
				if (isBlockedDirection(unit, startTile[i], direction, bam, missileTarget))
					return {{INVALID_MOVE_COST, 0}};
				if (startTile[i]->getTerrainLevel() - destinationTile[i]->getTerrainLevel() > 8)
					return {{INVALID_MOVE_COST, 0}};
			}
		}

		int wallCounter = 0;
		int wallTmp = 0;
		int wallcost = 0; // walking through rubble walls, but don't charge for walking diagonally through doors (which is impossible),
						// they're a special case unto themselves, if we can walk past them diagonally, it means we can go around,
						// as there is no wall blocking us.
		if ((direction == 0 || direction == 7 || direction == 1) && !startTile[i]->hasLadderOnNorthWall())
		{
			wallTmp = startTile[i]->getTUCost(O_NORTHWALL, movementType);
			if (wallTmp > 0)
			{
				wallcost += wallTmp;
				wallCounter += 1;
			}
		}
		if (!triedStairsDown && (direction == 2 || direction == 1 || direction == 3) && !destinationTile[i]->hasLadderOnWestWall())
		{
			wallTmp = destinationTile[i]->getTUCost(O_WESTWALL, movementType);
			if (wallTmp > 0)
			{
				wallcost += wallTmp;
				wallCounter += 1;
			}
		}
		if (!triedStairsDown && (direction == 4 || direction == 3 || direction == 5) && !destinationTile[i]->hasLadderOnNorthWall())
		{
			wallTmp = destinationTile[i]->getTUCost(O_NORTHWALL, movementType);
			if (wallTmp > 0)
			{
				wallcost += wallTmp;
				wallCounter += 1;
			}
		}
		if ((direction == 6 || direction == 5 || direction == 7) && !startTile[i]->hasLadderOnWestWall())
		{
			wallTmp = startTile[i]->getTUCost(O_WESTWALL, movementType);
			if (wallTmp > 0)
			{
				wallcost += wallTmp;
				wallCounter += 1;
			}
		}

		// "average" cost: https://openxcom.org/forum/index.php?topic=12589.0
		if (wallCounter > 0)
		{
			wallcost /= wallCounter;
		}

		// for backward compatiblity (100 + 100 + 100 > 255) or for (255 + 10 > 255)
		if (wallcost >= INVALID_MOVE_COST)
		{
			return {{INVALID_MOVE_COST, 0}};
		}

		// if we don't want to fall down and there is no floor, we can't know the TUs so it's default to 4
		if (direction < DIR_UP && !triedStairsDown && destinationTile[i]->hasNoFloor(0))
		{
			cost = DEFAULT_MOVE_COST;
		}

		// calculate the cost by adding floor walk cost and object walk cost
		if (direction < DIR_UP)
		{
			cost += destinationTile[i]->getTUCost(O_FLOOR, movementType);
			if (!triedStairsDown && !triedStairs && destinationTile[i]->getMapData(O_OBJECT) && !destinationTile[i]->hasLadderOnObject())
			{
				cost += destinationTile[i]->getTUCost(O_OBJECT, movementType);
			}
			if (cost == 0)
			{
				// some mods have broken tiles with 0 move cost, as it could be problematic for the engine, we override it to default cost.
				cost = DEFAULT_MOVE_COST;
			}
			// climbing up a level costs one extra
			if (upperLevel)
			{
				cost++;
			}
		}

		// diagonal walking (uneven directions) costs 50% more tu's
		if (direction < DIR_UP && direction & 1)
		{
			cost = (int)((double)cost * 1.5);
		}

		cost += wallcost;

		// TFTD thing: underwater tiles on fire or filled with smoke cost 2 TUs more for whatever reason.
		if (_save->getDepth() > 0 && (destinationTile[i]->getFire() > 0 || destinationTile[i]->getSmoke() > 0))
		{
			cost += 2;
		}

		if (missileTarget && destinationTile[i]->getUnit())
		{
			BattleUnit *unitHere = destinationTile[i]->getUnit();
			if (unitHere != missileTarget && !unitHere->isOut() && unitHere != unit)
			{
				if (unitHere->getFaction() == unit->getFaction())
				{
					return {{INVALID_MOVE_COST, 0}}; // consider any tile occupied by a friendly as being blocked
				}
				else if (unit->getUnitRules() && unitHere->getTurnsSinceSpottedByFaction(unit->getFaction()) <= unit->getUnitRules()->getIntelligence())
				{
					return {{INVALID_MOVE_COST, 0}}; // consider any tile occupied by a known unit that isn't our target as being blocked
				}
			}
		}

		// Strafing costs +1 for forwards-ish or sidewards, propose +2 for backwards-ish directions
		// Maybe if flying then it makes no difference?
		if (_strafeMove && bam == BAM_STRAFE)
		{
			if (unit->getDirection() != direction)
			{
				cost += 1;
			}
		}

		// cap move cost to given limit
		cost = std::min(cost, +MAX_MOVE_COST);

		totalCost += cost;
	}

	// because unit move up or down we adjust final position
	if (triedStairs)
	{
		pos.z++;
	}
	else if (direction != DIR_DOWN && triedStairsDown)
	{
		pos.z--;
	}

	// for bigger sized units, check the path between parts in an X shape at the end position
	if (size)
	{
		totalCost /= numberOfParts;
		const Tile *originTile = _save->getTile(pos + Position(1,1,0));
		const Tile *finalTile = _save->getTile(pos);
		int tmpDirection = 7;
		if (isBlockedDirection(unit, originTile, tmpDirection, bam, missileTarget))
			return {{INVALID_MOVE_COST, 0}};
		if (!triedStairsDown && abs(originTile->getTerrainLevel() - finalTile->getTerrainLevel()) > 10)
			return {{INVALID_MOVE_COST, 0}};
		originTile = _save->getTile(pos + Position(1,0,0));
		finalTile = _save->getTile(pos + Position(0,1,0));
		tmpDirection = 5;
		if (isBlockedDirection(unit, originTile, tmpDirection, bam, missileTarget))
			return {{INVALID_MOVE_COST, 0}};
		if (!triedStairsDown && abs(originTile->getTerrainLevel() - finalTile->getTerrainLevel()) > 10)
			return {{INVALID_MOVE_COST, 0}};
	}


	if (bam == BAM_MISSILE)
	{
		return { { }, { }, pos };
	}

	if (direction == DIR_DOWN && fallingDown)
	{
		return { { }, { firePenaltyCost, 0 }, pos };
	}

	const auto costDiv = 100 * 100 * 100;
	ArmorMoveCost cost = { totalCost, totalCost };

	cost *= unit->getMoveCostBase();

	if (climb)
	{
		cost *= unit->getMoveCostBaseClimb();
	}
	else if (flying)
	{
		cost *= unit->getMoveCostBaseFly();
	}
	else
	{
		cost *= unit->getMoveCostBaseNormal();
	}

	if (direction >= Pathfinding::DIR_UP)
	{
		if (climb)
		{
			if (direction == Pathfinding::DIR_UP)
			{
				cost *= armor->getMoveCostClimbUp();
			}
			else
			{
				cost *= armor->getMoveCostClimbDown();
			}
		}
		else if (flying)
		{
			if (direction == Pathfinding::DIR_UP)
			{
				cost *= armor->getMoveCostFlyUp();
			}
			else
			{
				cost *= armor->getMoveCostFlyDown();
			}
		}
		else
		{
			//unit use GravLift
			cost *= armor->getMoveCostGravLift();
		}
	}
	else if (bam == BAM_NORMAL)
	{
		if (flying)
		{
			cost *= armor->getMoveCostFlyWalk();
		}
		else
		{
			cost *= armor->getMoveCostWalk();
		}
	}
	else if (bam == BAM_RUN)
	{
		if (flying)
		{
			cost *= armor->getMoveCostFlyRun();
		}
		else
		{
			cost *= armor->getMoveCostRun();
		}
	}
	else if (bam == BAM_STRAFE)
	{
		if (flying)
		{
			cost *= armor->getMoveCostFlyStrafe();
		}
		else
		{
			cost *= armor->getMoveCostStrafe();
		}
	}
	else if (bam == BAM_SNEAK)
	{
		//no flight
		cost *= armor->getMoveCostSneak();
	}
	else
	{
		assert(false && "Unreachable code in pathfinding cost");
	}

	const int timeCost = Mod::EXTENDED_MOVEMENT_COST_ROUNDING == 0 ? (cost.TimePercent) / costDiv :
		                 Mod::EXTENDED_MOVEMENT_COST_ROUNDING == 1 ? (cost.TimePercent + (costDiv / 2)) / costDiv :
		                                                             (cost.TimePercent - 1 + (costDiv / 2)) / costDiv;

	const int energyCost = Mod::EXTENDED_MOVEMENT_COST_ROUNDING == 0 ? (cost.EnergyPercent) / costDiv :
		                   Mod::EXTENDED_MOVEMENT_COST_ROUNDING == 1 ? (cost.EnergyPercent + (costDiv / 2)) / costDiv :
		                                                               (cost.EnergyPercent - 1 + (costDiv / 2)) / costDiv;

	return { { Clamp(timeCost, 1, INVALID_MOVE_COST - 1), Clamp(energyCost, 0, INVALID_MOVE_COST) }, { firePenaltyCost, 0 }, pos };
}

/**
 * Checks whether a path is ready and gives the first direction.
 * @return Direction where the unit needs to go next, -1 if it's the end of the path.
 */
int Pathfinding::getStartDirection() const
{
	if (_path.empty()) return -1;
	return _path.back();
}

/**
 * Dequeues the next path direction. Ie returns the direction and removes it from queue.
 * @return Direction where the unit needs to go next, -1 if it's the end of the path.
 */
int Pathfinding::dequeuePath()
{
	if (_path.empty()) return -1;
	int last_element = _path.back();
	_path.pop_back();
	return last_element;
}

/**
 * Aborts the current path. Clears the path vector.
 */
void Pathfinding::abortPath()
{
	_totalTUCost = {};
	_path.clear();
}

/**
 * Gets movement type of unit or movement of missile.
 * @param unit Unit we check path for.
 * @param missileTarget Target of missile in case of `BAM_MISSILE
 * @return `MT_FLY` if we have `missileTarget` other wise `unit` move type.
 */
MovementType Pathfinding::getMovementType(const BattleUnit *unit, const BattleUnit *missileTarget, BattleActionMove bam) const
{
	if (missileTarget)
	{
		return MT_FLY;
	}
	else
	{
		if (bam == BAM_SNEAK)
		{
			return MT_WALK;
		}
		else
		{
			return unit->getMovementType();
		}
	}
}

/**
 * Determines whether a certain part of a tile blocks movement.
 * @param tile Specified tile, can be a null pointer.
 * @param part Part of the tile.
 * @param missileTarget Target for a missile.
 * @return True if the movement is blocked.
 */
bool Pathfinding::isBlocked(const BattleUnit *unit, const Tile *tile, const int part, BattleActionMove bam, const BattleUnit *missileTarget, int bigWallExclusion) const
{
	if (tile == 0) return true; // probably outside the map here

	auto movementType = getMovementType(unit, missileTarget, bam);

	if (part == O_BIGWALL)
	{
		if (tile->getMapData(O_OBJECT) &&
			tile->getMapData(O_OBJECT)->getBigWall() != 0 &&
			tile->getMapData(O_OBJECT)->getBigWall() <= BIGWALLNWSE &&
			tile->getMapData(O_OBJECT)->getBigWall() != bigWallExclusion)
			return true; // blocking part
		else
			return false;
	}
	if (part == O_WESTWALL)
	{
		if (tile->getMapData(O_OBJECT) &&
			(tile->getMapData(O_OBJECT)->getBigWall() == BIGWALLWEST ||
			tile->getMapData(O_OBJECT)->getBigWall() == BIGWALLWESTANDNORTH))
			return true; // blocking part
		Tile *tileWest = _save->getTile(tile->getPosition() + Position(-1, 0, 0));
		if (!tileWest) return true;	// do not look outside of map
		if (tileWest->getMapData(O_OBJECT) &&
			(tileWest->getMapData(O_OBJECT)->getBigWall() == BIGWALLEAST ||
			tileWest->getMapData(O_OBJECT)->getBigWall() == BIGWALLEASTANDSOUTH))
			return true; // blocking part
	}
	if (part == O_NORTHWALL)
	{
		if (tile->getMapData(O_OBJECT) &&
			(tile->getMapData(O_OBJECT)->getBigWall() == BIGWALLNORTH ||
			 tile->getMapData(O_OBJECT)->getBigWall() == BIGWALLWESTANDNORTH))
			return true; // blocking part
		Tile *tileNorth = _save->getTile(tile->getPosition() + Position(0, -1, 0));
		if (!tileNorth)
			return true; // do not look outside of map
		if (tileNorth->getMapData(O_OBJECT) &&
			(tileNorth->getMapData(O_OBJECT)->getBigWall() == BIGWALLSOUTH ||
			 tileNorth->getMapData(O_OBJECT)->getBigWall() == BIGWALLEASTANDSOUTH))
			return true; // blocking part
	}
	if (part == O_FLOOR)
	{
		if (tile->getUnit() && !_ignoreFriends)
		{
			BattleUnit *u = tile->getUnit();
			if (u == unit || u == missileTarget || u->isOut())
				return false;
			if (missileTarget && u != missileTarget && u->getFaction() == _unit->getFaction())
				return true;			// AI pathfinding with missiles shouldn't path through their own units
			if (unit)
			{
				if (unit->getFaction() == FACTION_PLAYER && u->getVisible())
					return true; // player know all visible units
				if (unit->getFaction() == u->getFaction())
					return true;
				if (unit->getFaction() != FACTION_PLAYER &&
					std::find(unit->getUnitsSpottedThisTurn().begin(), unit->getUnitsSpottedThisTurn().end(), u) != unit->getUnitsSpottedThisTurn().end())
					return true;
			}
		}
		else if (tile->hasNoFloor(0) && movementType != MT_FLY) // this whole section is devoted to making large units not take part in any kind of falling behaviour
		{
			Position pos = tile->getPosition();
			while (pos.z >= 0)
			{
				Tile *t = _save->getTile(pos);
				BattleUnit *u = t->getUnit();

				if (u != 0 && u != unit)
				{
					// don't let large units fall on other units
					if (unit && unit->isBigUnit())
					{
						return true;
					}
					// don't let any units fall on large units
					if (u != unit && u != missileTarget && !u->isOut() && u->isBigUnit())
					{
						return true;
					}
				}
				// not gonna fall any further, so we can stop checking.
				if (!t->hasNoFloor(0))
				{
					break;
				}
				pos.z--;
			}
		}
	}
	// missiles can't pathfind through closed doors.
	{
		TilePart tp = (TilePart)part;
		if (missileTarget != 0 && tile->getMapData(tp) &&
			(tile->isDoor(tp) ||
			(tile->isUfoDoor(tp) &&
			!tile->isUfoDoorOpen(tp))))
		{
			return true;
		}
	}
	if (tile->getTUCost(part, movementType) == Pathfinding::INVALID_MOVE_COST) return true; // blocking part
	return false;
}

/**
 * Determines whether going from one tile to another blocks movement.
 * @param unit Unit that move.
 * @param startTile The tile to start from.
 * @param direction The direction we are facing.
 * @param bam Move type.
 * @param missileTarget Target for a missile.
 * @return True if the movement is blocked.
 */
bool Pathfinding::isBlockedDirection(const BattleUnit *unit, const Tile *startTile, const int direction, BattleActionMove bam, const BattleUnit *missileTarget) const
{

	// check if the difference in height between start and destination is not too high
	// so we can not jump to the highest part of the stairs from the floor
	// stairs terrainlevel goes typically -8 -16 (2 steps) or -4 -12 -20 (3 steps)
	// this "maximum jump height" is therefore set to 8

	const Position currentPosition = startTile->getPosition();
	static const Position oneTileNorth = Position(0, -1, 0);
	static const Position oneTileEast = Position(1, 0, 0);
	static const Position oneTileSouth = Position(0, 1, 0);
	static const Position oneTileWest = Position(-1, 0, 0);

	switch(direction)
	{
	case 0:	// north
		if (isBlocked(unit, startTile, O_NORTHWALL, bam, missileTarget)) return true;
		if (Options::strictBlockedChecking)
			if (isBlocked(unit, _save->getTile(currentPosition + oneTileNorth), O_BIGWALL, bam, missileTarget, BIGWALLNORTH)) return true;
		break;
	case 1: // north-east
		if (isBlocked(unit, startTile,O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileNorth + oneTileEast),O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast),O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast),O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast), O_BIGWALL, bam, missileTarget, BIGWALLNESW)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileNorth), O_BIGWALL, bam, missileTarget, BIGWALLNESW)) return true;
		break;
	case 2: // east
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast), O_WESTWALL, bam, missileTarget)) return true;
		if (Options::strictBlockedChecking)
			if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast), O_BIGWALL, bam, missileTarget, BIGWALLEAST))	return true;
		break;
	case 3: // south-east
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast), O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth + oneTileEast), O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth + oneTileEast), O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileEast), O_BIGWALL, bam, missileTarget, BIGWALLNWSE)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_BIGWALL, bam, missileTarget, BIGWALLNWSE)) return true;
		break;
	case 4: // south
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_NORTHWALL, bam, missileTarget)) return true;
		if (Options::strictBlockedChecking)
			if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_BIGWALL, bam, missileTarget, BIGWALLSOUTH)) return true;
		break;
	case 5: // south-west
		if (isBlocked(unit, startTile, O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth), O_BIGWALL, bam, missileTarget, BIGWALLNESW)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileWest), O_BIGWALL, bam, missileTarget, BIGWALLNESW)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileSouth + oneTileWest), O_NORTHWALL, bam, missileTarget)) return true;
		break;
	case 6: // west
		if (isBlocked(unit, startTile, O_WESTWALL, bam, missileTarget)) return true;
		if (Options::strictBlockedChecking)
			if (isBlocked(unit, _save->getTile(currentPosition + oneTileWest), O_BIGWALL, bam, missileTarget, BIGWALLWEST))	return true;
		break;
	case 7: // north-west
		if (isBlocked(unit, startTile, O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, startTile, O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileWest), O_NORTHWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileNorth), O_WESTWALL, bam, missileTarget)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileNorth), O_BIGWALL, bam, missileTarget, BIGWALLNWSE)) return true;
		if (isBlocked(unit, _save->getTile(currentPosition + oneTileWest), O_BIGWALL, bam, missileTarget, BIGWALLNWSE)) return true;
		break;
	}

	return false;
}

/**
 * Determines whether going from one tile to another blocks movement.
 * @param unit Unit that move.
 * @param startTile The tile to start from.
 * @param direction The direction we are facing.
 */
bool Pathfinding::isBlockedDirection(const BattleUnit *unit, Tile *startTile, const int direction) const
{
	return isBlockedDirection(unit, startTile, direction, BAM_NORMAL, nullptr);
}

/**
 * Determines whether a unit can fall down from this tile.
 * We can fall down here, if the tile does not exist, the tile has no floor
 * the current position is higher than 0, if there is no unit standing below us.
 * @param here The current tile.
 * @return True if a unit can fall down.
 */
bool Pathfinding::canFallDown(const Tile *here) const
{
	if (here->getPosition().z == 0)
		return false;

	return here->hasNoFloor(_save);
}

/**
 * Determines whether a unit can fall down from this tile.
 * We can fall down here, if the tile does not exist, the tile has no floor
 * the current position is higher than 0, if there is no unit standing below us.
 * @param here The current tile.
 * @param size The size of the unit.
 * @return True if a unit can fall down.
 */
bool Pathfinding::canFallDown(const Tile *here, int size) const
{
	for (int x = 0; x != size; ++x)
	{
		for (int y = 0; y != size; ++y)
		{
			Position checkPos = here->getPosition() + Position(x,y,0);
			Tile *checkTile = _save->getTile(checkPos);
			if (!canFallDown(checkTile))
				return false;
		}
	}

	return true;
}

/**
 * Determines whether the unit is going up a stairs.
 * @param startPosition The position to start from.
 * @param endPosition The position we wanna reach.
 * @return True if the unit is going up a stairs.
 */
bool Pathfinding::isOnStairs(Position startPosition, Position endPosition) const
{
	//condition 1 : endposition has to the south a terrainlevel -16 object (upper part of the stairs)
	if (_save->getTile(endPosition + Position(0, 1, 0)) && _save->getTile(endPosition + Position(0, 1, 0))->getTerrainLevel() == -16)
	{
		// condition 2 : one position further to the south there has to be a terrainlevel -8 object (lower part of the stairs)
		if (_save->getTile(endPosition + Position(0, 2, 0)) && _save->getTile(endPosition + Position(0, 2, 0))->getTerrainLevel() != -8)
		{
			return false;
		}

		// condition 3 : the start position has to be on either of the 3 tiles to the south of the endposition
		if (startPosition == endPosition + Position(0, 1, 0) || startPosition == endPosition + Position(0, 2, 0) || startPosition == endPosition + Position(0, 3, 0))
		{
			return true;
		}
	}

	// same for the east-west oriented stairs.
	if (_save->getTile(endPosition + Position(1, 0, 0)) && _save->getTile(endPosition + Position(1, 0, 0))->getTerrainLevel() == -16)
	{
		if (_save->getTile(endPosition + Position(2, 0, 0)) && _save->getTile(endPosition + Position(2, 0, 0))->getTerrainLevel() != -8)
		{
			return false;
		}
		if (startPosition == endPosition + Position(1, 0, 0) || startPosition == endPosition + Position(2, 0, 0) || startPosition == endPosition + Position(3, 0, 0))
		{
			return true;
		}
	}

	//TFTD stairs 1 : endposition has to the south a terrainlevel -18 object (upper part of the stairs)
	if (_save->getTile(endPosition + Position(0, 1, 0)) && _save->getTile(endPosition + Position(0, 1, 0))->getTerrainLevel() == -18)
	{
		// condition 2 : one position further to the south there has to be a terrainlevel -8 object (lower part of the stairs)
		if (_save->getTile(endPosition + Position(0, 2, 0)) && _save->getTile(endPosition + Position(0, 2, 0))->getTerrainLevel() != -12)
		{
			return false;
		}

		// condition 3 : the start position has to be on either of the 3 tiles to the south of the endposition
		if (startPosition == endPosition + Position(0, 1, 0) || startPosition == endPosition + Position(0, 2, 0) || startPosition == endPosition + Position(0, 3, 0))
		{
			return true;
		}
	}

	// same for the east-west oriented stairs.
	if (_save->getTile(endPosition + Position(1, 0, 0)) && _save->getTile(endPosition + Position(1, 0, 0))->getTerrainLevel() == -18)
	{
		if (_save->getTile(endPosition + Position(2, 0, 0)) && _save->getTile(endPosition + Position(2, 0, 0))->getTerrainLevel() != -12)
		{
			return false;
		}
		if (startPosition == endPosition + Position(1, 0, 0) || startPosition == endPosition + Position(2, 0, 0) || startPosition == endPosition + Position(3, 0, 0))
		{
			return true;
		}
	}
	return false;
}

/**
 * Checks, for the up/down button, if the movement is valid. Either there is a grav lift or the unit can fly and there are no obstructions.
 * @param bu Pointer to unit.
 * @param startPosition Unit starting position.
 * @param direction Up or Down
 * @return bool Whether it's valid.
 */
bool Pathfinding::validateUpDown(const BattleUnit *bu, const Position& startPosition, const int direction, bool missile) const
{
	Position endPosition;
	directionToVector(direction, &endPosition);
	endPosition += startPosition;
	Tile *startTile = _save->getTile(startPosition);
	Tile *destinationTile = _save->getTile(endPosition);

	if (!destinationTile)
	{
		return false;
	}

	if (startTile->hasGravLiftFloor() && destinationTile->hasGravLiftFloor())
	{
		if (missile)
		{
			if (direction == DIR_UP)
			{
				if (destinationTile->getMapData(O_FLOOR)->getLoftID(0) != 0)
					return false;
			}
			else if (startTile->getMapData(O_FLOOR)->getLoftID(0) != 0)
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		if (bu->getMovementType() == MT_FLY || startTile->hasLadder() || missile)
		{
			if ((direction == DIR_UP && destinationTile->hasNoFloor(_save)) // flying up only possible when there is no roof
				|| (direction == DIR_DOWN && startTile->hasNoFloor(_save)) // falling down only possible when there is no floor
				)
			{
				return true;
			}
		}
	}

	return false;
}

/**
 * Marks tiles for the path preview.
 * @param bRemove Remove preview?
 * @return True, if a path is previewed.
 */
bool Pathfinding::previewPath(bool bRemove)
{
	if (_path.empty())
		return false;

	if (!bRemove && _pathPreviewed)
		return false;

	_pathPreviewed = !bRemove;

	if (bRemove)
	{
		// use old values, player could unpress buttons.
	}
	else
	{
		_ctrlUsed = Options::strafe && _save->isCtrlPressed(true);
		_altUsed = Options::strafe && _save->isAltPressed(true);
	}

	refreshPath();

	return true;
}

/**
 * Unmarks the tiles used for the path preview.
 * @return True, if the previewed path was removed.
 */
bool Pathfinding::removePreview()
{
	if (!_pathPreviewed)
		return false;
	previewPath(true);
	return true;
}

/**
 * Refresh the path preview.
 */
void Pathfinding::refreshPath()
{
	Position pos = _unit->getPosition();

	int tus = _unit->getTimeUnits();
	if (_unit->isKneeled())
	{
		tus -= _unit->getKneelUpCost();
	}
	int energy = _unit->getEnergy();
	int size = _unit->getArmor()->getSize() - 1;
	int total = _unit->isKneeled() ? _unit->getKneelUpCost() : 0;
	if (_unit->getArmor()->getTurnBeforeFirstStep())
	{
		int dir = getStartDirection();
		if (dir != -1 && dir != _unit->getDirection())
		{
			int turnCost = std::abs(_unit->getDirection() - dir);
			if (turnCost > 4)
			{
				turnCost = 8 - turnCost;
			}
			turnCost = turnCost * _unit->getTurnCost();

			tus -= turnCost;
			total += turnCost;
		}
	}
	bool switchBack = false;
	if (_save->getBattleGame()->getReservedAction() == BA_NONE)
	{
		switchBack = true;
		_save->getBattleGame()->setTUReserved(BA_AUTOSHOT);
	}

	const bool running = _ctrlUsed && _unit->getArmor()->allowsRunning(_unit->isSmallUnit()) && (_path.size() > 1 || _altUsed);
	const bool strafing = !running && _ctrlUsed && _unit->getArmor()->allowsStrafing(_unit->isSmallUnit()) && _path.size() == 1;
	const bool sneaking = !running && _altUsed && _unit->getArmor()->allowsSneaking(_unit->isSmallUnit());

	const auto bam = strafing ? BAM_STRAFE : running ? BAM_RUN : sneaking ? BAM_SNEAK : BAM_NORMAL;
	const auto movementType = getMovementType(_unit, nullptr, bam); //preview always for unit not missiles
	for (std::vector<int>::reverse_iterator i = _path.rbegin(); i != _path.rend(); ++i)
	{
		int dir = *i;
		auto r = getTUCost(pos, dir, _unit, 0, bam);
		pos = r.pos;
		energy -= r.cost.energy;
		tus -= r.cost.time;
		total += r.cost.time;
		bool reserve = _save->getBattleGame()->checkReservedTU(_unit, total, _unit->getEnergy() - energy, true);
		for (int x = size; x >= 0; x--)
		{
			for (int y = size; y >= 0; y--)
			{
				Tile *tile = _save->getTile(pos + Position(x,y,0));
				Tile *tileAbove = _save->getTile(pos + Position(x,y,1));
				if (_pathPreviewed)
				{
					if (i == _path.rend() - 1)
					{
						tile->setPreview(10);
					}
					else
					{
						int nextDir = *(i + 1);
						tile->setPreview(nextDir);
					}
					if ((x && y) || size == 0)
					{
						tile->setTUMarker(std::max(0,tus));
						tile->setEnergyMarker(std::max(0,energy));
					}
					if (tileAbove && tileAbove->getPreview() == 0 && r.cost.time == 0 && movementType != MT_FLY) //unit fell down, retroactively make the tile above's direction marker to DOWN
					{
						tileAbove->setPreview(DIR_DOWN);
					}
				}
				else
				{
					tile->setPreview(-1);
					tile->setTUMarker(-1);
					tile->setEnergyMarker(-1);
				}
				tile->setMarkerColor(!_pathPreviewed ? 0 : ((tus>=0 && energy>=0)?(reserve?Pathfinding::green : Pathfinding::yellow) : Pathfinding::red));
			}
		}
	}
	if (switchBack)
	{
		_save->getBattleGame()->setTUReserved(BA_NONE);
	}
}

/**
 * Calculates the shortest path using Bresenham path algorithm.
 * @note This only works in the X/Y plane.
 * @param origin The position to start from.
 * @param target The position we want to reach.
 * @param missileTarget Target of the path.
 * @param sneak Is the unit sneaking?
 * @param maxTUCost Maximum time units the path can cost.
 * @return True if a path exists, false otherwise.
 */
bool Pathfinding::bresenhamPath(Position origin, Position target, BattleActionMove bam, const BattleUnit *missileTarget, bool sneak, int maxTUCost)
{
	int xd[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	int yd[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

	int x, x0, x1, delta_x, step_x;
	int y, y0, y1, delta_y, step_y;
	int z, z0, z1, delta_z, step_z;
	int swap_xy, swap_xz;
	int drift_xy, drift_xz;
	int cx, cy, cz;
	Position lastPoint(origin);
	int dir;
	int lastTUCost = -1;
	Position nextPoint;
	_totalTUCost = {};

	//start and end points
	x0 = origin.x;	 x1 = target.x;
	y0 = origin.y;	 y1 = target.y;
	z0 = origin.z;	 z1 = target.z;

	//'steep' xy Line, make longest delta x plane
	swap_xy = abs(y1 - y0) > abs(x1 - x0);
	if (swap_xy)
	{
		std::swap(x0, y0);
		std::swap(x1, y1);
	}

	//do same for xz
	swap_xz = abs(z1 - z0) > abs(x1 - x0);
	if (swap_xz)
	{
		std::swap(x0, z0);
		std::swap(x1, z1);
	}

	//delta is Length in each plane
	delta_x = abs(x1 - x0);
	delta_y = abs(y1 - y0);
	delta_z = abs(z1 - z0);

	//drift controls when to step in 'shallow' planes
	//starting value keeps Line centred
	drift_xy  = (delta_x / 2);
	drift_xz  = (delta_x / 2);

	//direction of line
	step_x = 1;  if (x0 > x1) {  step_x = -1; }
	step_y = 1;  if (y0 > y1) {  step_y = -1; }
	step_z = 1;  if (z0 > z1) {  step_z = -1; }

	//starting point
	y = y0;
	z = z0;

	//step through longest delta (which we have swapped to x)
	for (x = x0; x != (x1+step_x); x += step_x)
	{
		//copy position
		cx = x;	cy = y;	cz = z;

		//unswap (in reverse)
		if (swap_xz) std::swap(cx, cz);
		if (swap_xy) std::swap(cx, cy);

		if (x != x0 || y != y0 || z != z0)
		{
			Position realNextPoint = Position(cx, cy, cz);
			// get direction
			for (dir = 0; dir < 8; ++dir)
			{
				if (xd[dir] == cx-lastPoint.x && yd[dir] == cy-lastPoint.y) break;
			}
			if (dir == 8)
			{
				if (cz-lastPoint.z > 0)
				{
					dir = DIR_UP;
				}
				else
				{
					dir = DIR_DOWN;
				}
			}
			PathfindingStep r = getTUCost(lastPoint, dir, _unit, missileTarget, bam);
			nextPoint = r.pos;
			auto tuCost = r.cost.time + r.penalty.time;

			if (sneak && _save->getTile(nextPoint)->getVisible()) return false;

			// delete the following
			bool isDiagonal = (dir&1);
			int lastTUCostDiagonal = lastTUCost + lastTUCost / 2;
			int tuCostDiagonal = tuCost + tuCost / 2;
			if (nextPoint == realNextPoint && r.cost.time != INVALID_MOVE_COST && (tuCost == lastTUCost || (isDiagonal && tuCost == lastTUCostDiagonal) || (!isDiagonal && tuCostDiagonal == lastTUCost) || lastTUCost == -1)
				&& !isBlockedDirection(_unit, _save->getTile(lastPoint), dir, bam, missileTarget))
			{
				_path.push_back(dir);
			}
			else
			{
				return false;
			}
			if (missileTarget == 0 && r.cost.time != INVALID_MOVE_COST)
			{
				lastTUCost = tuCost;
				_totalTUCost = _totalTUCost + r.cost + r.penalty;
			}
			lastPoint = Position(cx, cy, cz);
		}
		//update progress in other planes
		drift_xy = drift_xy - delta_y;
		drift_xz = drift_xz - delta_z;

		//step in y plane
		if (drift_xy < 0)
		{
			y = y + step_y;
			drift_xy = drift_xy + delta_x;
		}

		//same in z
		if (drift_xz < 0)
		{
			z = z + step_z;
			drift_xz = drift_xz + delta_x;
		}
	}

	return true;
}

/**
 * Locates all tiles reachable to @a *unit with a TU cost no more than @a tuMax.
 * Uses Dijkstra's algorithm.
 * @param unit Pointer to the unit.
 * @param tuMax The maximum cost of the path to each tile.
 * @return An array of reachable tiles, sorted in ascending order of cost. The first tile is the start location.
 */
std::vector<int> Pathfinding::findReachable(BattleUnit *unit, const BattleActionCost &cost, bool &ranOutOfTUs)
{
	std::vector<PathfindingNode *> reachable = findReachablePathFindingNodes(unit, cost, ranOutOfTUs);
	std::vector<int> tiles;
	tiles.reserve(reachable.size());
	for (std::vector<PathfindingNode*>::const_iterator it = reachable.begin(); it != reachable.end(); ++it)
	{
		tiles.push_back(_save->getTileIndex((*it)->getPosition()));
	}
	return tiles;
}

/**
 * Locates all tiles reachable to @a *unit with a TU cost no more than @a tuMax.
 * Uses Dijkstra's algorithm.
 * @param unit Pointer to the unit.
 * @param tuMax The maximum cost of the path to each tile.
 * @param entireMap Ignore the constraints of the unit and create Nodes for the entire map from the unit as starting-point
 * @param missileTarget we can path into this unit as we want to hit it
 * @return A vector of pathfinding-nodes, sorted in ascending order of cost. The first tile is the start location.
 */
std::vector<PathfindingNode*> Pathfinding::findReachablePathFindingNodes(BattleUnit* unit, const BattleActionCost& cost, bool& ranOutOfTUs, bool entireMap, const BattleUnit* missileTarget, const Position* alternateStart, bool justCheckIfAnyMovementIsPossible, bool useMaxTUs, BattleActionMove bam)
{
	_unit = unit;
	Position start = unit->getPosition();
	if (alternateStart)
		start = *alternateStart;
	int tuMax = unit->getTimeUnits() - cost.Time;
	int energyMax = unit->getEnergy() - cost.Energy;
	if (useMaxTUs)
	{
		tuMax = unit->getBaseStats()->tu;
		energyMax = unit->getBaseStats()->stamina;
	}

	PathfindingCost costMax = {tuMax, energyMax};

	if (alternateStart)
	{
		for (auto& pn : _altNodes)
		{
			pn.reset();
		}
	}
	else
	{
		for (auto& pn : _nodes)
		{
			pn.reset();
		}
	}
	PathfindingNode *startNode = getNode(start, alternateStart);
	startNode->connect({}, 0, 0);
	PathfindingOpenSet unvisited;
	unvisited.push(startNode);
	std::vector<PathfindingNode *> reachable;
	int maxTilesToReturn = _size;
	if (Options::aiPerformanceOptimization)
	{
		int myUnits = 0;
		for (BattleUnit *bu : *(_save->getUnits()))
		{
			if (bu->getFaction() == unit->getFaction() && !bu->isOut())
				++myUnits;
		}
		float scaleFactor = (float)60 * 60 * 4 * 30 / (_save->getMapSizeXYZ() * myUnits);
		if (scaleFactor < 1)
			maxTilesToReturn *= scaleFactor;
	}
	int strictMaxTilesToReturn = _size;
	while (!unvisited.empty())
	{
		PathfindingNode *currentNode = unvisited.pop();
		Position const &currentPos = currentNode->getPosition();

		if (reachable.size() > maxTilesToReturn)
			entireMap = false;

		// Try all reachable neighbours.
		for (int direction = 0; direction < 10; direction++)
		{
			auto r = getTUCost(currentPos, direction, unit, missileTarget, bam);
			if (r.cost.time == INVALID_MOVE_COST) // Skip unreachable / blocked
				continue;
			auto totalTuCost = currentNode->getTUCost(false) + r.cost + r.penalty;
			if (!(totalTuCost <= costMax) && !entireMap) // Run out of TUs/Energy
			{
				ranOutOfTUs = true;
				continue;
			}
			PathfindingNode *nextNode = getNode(r.pos, alternateStart);
			if (nextNode->isChecked()) // Our algorithm means this node is already at minimum cost.
				continue;
			// If this node is unvisited or visited from a better path.
			if (!nextNode->inOpenSet() || nextNode->getTUCost(false).time > totalTuCost.time)
			{
				nextNode->connect(totalTuCost, currentNode, direction);
				unvisited.push(nextNode);
			}
		}
		currentNode->setChecked();
		reachable.push_back(currentNode);
		if (justCheckIfAnyMovementIsPossible && reachable.size() > 1)
			break;
	}
	std::sort(reachable.begin(), reachable.end(), MinNodeCosts());
	return reachable;
}

/**
 * Gets the strafe move setting.
 * @return Strafe move.
 */
bool Pathfinding::getStrafeMove() const
{
	return _strafeMove;
}

/**
 * Gets the path preview setting.
 * @return True, if paths are previewed.
 */
bool Pathfinding::isPathPreviewed() const
{
	return _pathPreviewed;
}

/**
 * Sets _unit in order to abuse low-level pathfinding functions from outside the class.
 * @param unit Unit taking the path.
 */
void Pathfinding::setUnit(BattleUnit* unit)
{
	_unit = unit;
}

/**
 * Gets a reference to the current path.
 * @return the actual path.
 */
const std::vector<int> &Pathfinding::getPath() const
{
	return _path;
}

/**
 * Makes a copy of the current path.
 * @return a copy of the path.
 */
std::vector<int> Pathfinding::copyPath() const
{
	return _path;
}

}
