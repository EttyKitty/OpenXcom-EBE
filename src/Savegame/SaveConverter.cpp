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
#include "SaveConverter.h"
#include <algorithm>
#include "../Engine/Yaml.h"
#include <SDL_endian.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <bitset>
#include "../Engine/Options.h"
#include "../Engine/CrossPlatform.h"
#include "../Engine/Exception.h"
#include "../Engine/Language.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "SavedGame.h"
#include "GameTime.h"
#include "Country.h"
#include "Region.h"
#include "Base.h"
#include "BaseFacility.h"
#include "ItemContainer.h"
#include "Ufo.h"
#include "Craft.h"
#include "AlienBase.h"
#include "Waypoint.h"
#include "MissionSite.h"
#include "CraftWeapon.h"
#include "Transfer.h"
#include "Vehicle.h"
#include "AlienStrategy.h"
#include "AlienMission.h"
#include "Soldier.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleRegion.h"
#include "../Mod/ArticleDefinition.h"
#include "ResearchProject.h"
#include "../Mod/RuleManufacture.h"
#include "Production.h"
#include "../Mod/UfoTrajectory.h"
#include "../Engine/RNG.h"
#include "../Mod/RuleConverter.h"
#include "../Ufopaedia/Ufopaedia.h"
#include "../fmath.h"

namespace OpenXcom
{

enum TargetType { TARGET_NONE, TARGET_UFO, TARGET_CRAFT, TARGET_XBASE, TARGET_ABASE, TARGET_CRASH, TARGET_LANDED, TARGET_WAYPOINT, TARGET_TERROR, TARGET_PORT = 0x51, TARGET_ISLAND = 0x52, TARGET_SHIP = 0x53, TARGET_ARTEFACT = 0x54 };
const char *xcomAltitudes[] = { "STR_GROUND", "STR_VERY_LOW", "STR_LOW_UC", "STR_HIGH_UC", "STR_VERY_HIGH" };
const char *xcomStatus[] = { "STR_READY", "STR_OUT", "STR_REPAIRS", "STR_REFUELLING", "STR_REARMING" };

// Helper functions
template <typename T> T load(char* data) { return *(T*)data; }
template <> Uint16 load(char* data) { return SDL_SwapLE16(*(Uint16*)data); }
template <> Sint16 load(char* data) { return SDL_SwapLE16(*(Sint16*)data); }
template <> int load(char* data) { return SDL_SwapLE32(*(int*)data); }
template <> unsigned int load(char* data) { return SDL_SwapLE32(*(unsigned int*)data); }
template <> std::string load(char* data) { return data; }

char *SaveConverter::binaryBuffer(const std::string &filename, std::vector<char> &buffer) const
{
	auto datFile = CrossPlatform::readFile(_savePath + "/" + filename);
	buffer = std::vector<char>((std::istreambuf_iterator<char>(*datFile)), (std::istreambuf_iterator<char>()));
	return &buffer[0];
}

/**
 * Creates a new converter for the given save folder.
 * @param save Number of the save folder GAME_#
 * @param mod Mod to associate with this save.
 */
SaveConverter::SaveConverter(int save, Mod *mod) : _save(0), _mod(mod), _rules(mod->getConverter()), _year(0), _funds(0)
{
	std::ostringstream ssFolder, ssPath;
	ssFolder << "GAME_" << save;
	ssPath << Options::getMasterUserFolder() << ssFolder.str();
	_saveName = ssFolder.str();
	_savePath = ssPath.str();
	ssPath << "/SAVEINFO.DAT";
	if (!CrossPlatform::fileExists(ssPath.str()))
	{
		throw Exception(_saveName + " is not a valid save folder");
	}
}

SaveConverter::~SaveConverter()
{
}

/**
 * Gets all the info of the saves found in the user folder.
 * @param lang Loaded language.
 * @param info Returned list of saves info.
 */
void SaveConverter::getList(Language *lang, SaveOriginal info[NUM_SAVES])
{
	for (int i = 0; i < NUM_SAVES; ++i)
	{
		SaveOriginal &save = info[i];
		save.id = 0;

		const int id = i + 1;
		std::string datname = Options::getMasterUserFolder() + "GAME_" + std::to_string(id) + "/SAVEINFO.DAT";
		if (!CrossPlatform::fileExists(datname)) {
			continue;
		}
		auto datFile = CrossPlatform::readFile(datname);
		std::vector<char> buffer((std::istreambuf_iterator<char>(*datFile)), (std::istreambuf_iterator<char>()));
		char *data = &buffer[0];

		std::string name = load<std::string>(data + 0x02);
		int year = load<Uint16>(data + 0x1C);
		int month = load<Uint16>(data + 0x1E);
		int day = load<Uint16>(data + 0x20);
		int hour = load<Uint16>(data + 0x22);
		int minute = load<Uint16>(data + 0x24);
		bool tactical = load<char>(data + 0x26) != 0;

		GameTime time = GameTime(0, day, month + 1, year, hour, minute, 0);

		std::ostringstream ssDate, ssTime;
		ssDate << time.getDayString(lang) << "  " << lang->getString(time.getMonthString()) << "  " << time.getYear();
		ssTime << time.getHour() << ":" << std::setfill('0') << std::setw(2) << time.getMinute();

		save.id = id;
		save.name = name;
		save.date = ssDate.str();
		save.time = ssTime.str();
		save.tactical = tactical;
	}
}

/**
 * Converts an original X-COM save into an OpenXcom save.
 * @return New OpenXcom save.
 */
SavedGame *SaveConverter::loadOriginal()
{
	_save = new SavedGame();

	// Load globe data
	_save->getIncomes().clear();
	for (size_t i = 0; i < _rules->getCountries().size(); ++i)
	{
		Country *country = new Country(_mod->getCountry(_rules->getCountries()[i], true));
		country->getActivityAlien().clear();
		country->getActivityXcom().clear();
		country->getFunding().clear();
		_save->getCountries()->push_back(country);
	}
	for (size_t i = 0; i < _rules->getRegions().size(); ++i)
	{
		Region *region = new Region(_mod->getRegion(_rules->getRegions()[i], true));
		region->getActivityAlien().clear();
		region->getActivityXcom().clear();
		_save->getRegions()->push_back(region);
	}
	loadDatXcom();
	loadDatAlien();
	loadDatDiplom();
	loadDatLease();

	// Load graph data
	_save->getExpenditures().clear();
	_save->getMaintenances().clear();
	_save->getFundsList().clear();
	_save->getResearchScores().clear();
	loadDatLIGlob();
	loadDatUIGlob();
	loadDatIGlob();

	// Load alien data
	loadDatZonal();
	loadDatActs();
	loadDatMissions();

	// Load player data
	loadDatLoc();
	loadDatBase();
	loadDatAStore();
	loadDatCraft();
	loadDatSoldier();
	loadDatTransfer();
	loadDatResearch();
	loadDatUp();
	loadDatProject();
	loadDatBProd();
	loadDatXBases();

	return _save;
}

/**
 * Corrects vectors of graph data.
 * Original X-COM uses months as array indexes,
 * while OpenXcom stores month data in a linear fashion.
 * @param vector Vector of graph data.
 * @param month Current month.
 * @param year Has game gone longer than a year?
 */
template <typename T>
void SaveConverter::graphVector(std::vector<T> &vector, int month, bool year)
{
	if (year)
	{
		std::vector<T> newVector;
		int i = month;
		do
		{
			newVector.push_back(vector[i]);
			i = (i + 1) % vector.size();
		}
		while (i != month);
		vector = newVector;
	}
	else
	{
		vector.resize(month);
	}
}

/**
 * Loads the IGLOB.DAT file.
 * Contains game date, time and difficulty.
 */
void SaveConverter::loadDatIGlob()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("IGLOB.DAT", buffer);

	int month = load<int>(data + 0x00) + 1;
	int weekday = load<int>(data + 0x04) + 1;
	int day = load<int>(data + 0x08);
	int hour = load<int>(data + 0x0C);
	int minute = load<int>(data + 0x10);
	int second = load<int>(data + 0x14);
	_save->setTime(GameTime(weekday, day, month, _year, hour, minute, second));

	// account for difficulty bug
	if (buffer.size() > 0x3C)
	{
		int coefficient = load<int>(data + 0x3C);
		for (size_t i = DIFF_BEGINNER; i <= DIFF_SUPERHUMAN; ++i)
		{
			if (coefficient == Mod::DIFFICULTY_COEFFICIENT[i])
			{
				_save->setDifficulty((GameDifficulty)i);
				break;
			}
		}
	}

	// Fix up the months
	size_t monthsPassed = month + (_year - _mod->getStartingTime().getYear()) * 12;
	for (size_t i = 0; i < monthsPassed; ++i)
	{
		_save->addMonth();
	}
	graphVector(_save->getIncomes(), month, _year != _mod->getStartingTime().getYear());
	graphVector(_save->getExpenditures(), month, _year != _mod->getStartingTime().getYear());
	graphVector(_save->getMaintenances(), month, _year != _mod->getStartingTime().getYear());
	graphVector(_save->getFundsList(), month, _year != _mod->getStartingTime().getYear());
	graphVector(_save->getResearchScores(), month, _year != _mod->getStartingTime().getYear());
	for (size_t i = 0; i < _rules->getCountries().size(); ++i)
	{
		Country *country = _save->getCountries()->at(i);
		graphVector(country->getActivityAlien(), month, _year != _mod->getStartingTime().getYear());
		graphVector(country->getActivityXcom(), month, _year != _mod->getStartingTime().getYear());
		graphVector(country->getFunding(), month, _year != _mod->getStartingTime().getYear());
	}
	for (size_t i = 0; i < _rules->getRegions().size(); ++i)
	{
		Region *region = _save->getRegions()->at(i);
		graphVector(region->getActivityAlien(), month, _year != _mod->getStartingTime().getYear());
		graphVector(region->getActivityXcom(), month, _year != _mod->getStartingTime().getYear());
	}
	_save->getFundsList().back() = _funds;
}

/**
 * Loads the LIGLOB.DAT file.
 * Contains financial data.
 */
void SaveConverter::loadDatLIGlob()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("LIGLOB.DAT", buffer);

	const size_t MONTHS = 12;
	for (size_t i = 0; i < MONTHS; ++i)
	{
		int expenditure = load<int>(data + 0x04 + i * sizeof(int));
		int maintenance = load<int>(data + 0x34 + i * sizeof(int));
		int balance = load<int>(data + 0x64 + i * sizeof(int));
		_save->getExpenditures().push_back(expenditure);
		_save->getMaintenances().push_back(maintenance);
		_save->getFundsList().push_back(balance);
	}

	_funds = load<int>(data);
}

/**
 * Loads the UIGLOB.DAT file.
 * Contains Geoscape number IDs and scores.
 */
void SaveConverter::loadDatUIGlob()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("UIGLOB.DAT", buffer);

	std::map<std::string, int> ids;
	for (size_t i = 0; i < _rules->getMarkers().size(); ++i)
	{
		ids[_rules->getMarkers()[i]] = load<Uint16>(data + i * sizeof(Uint16));
	}
	ids["STR_CRASH_SITE"] = ids["STR_LANDING_SITE"] = ids["STR_UFO"];

	_year = load<Uint16>(data + 0x16);

	const size_t MONTHS = 12;
	for (size_t i = 0; i < MONTHS; ++i)
	{
		int score = load<Sint16>(data + 0x18 + i * sizeof(Sint16));
		_save->getResearchScores().push_back(score);
	}

	// Loads the SITE.DAT file (TFTD only).
	std::string s = _savePath + "/" + "SITE.DAT";
	if (CrossPlatform::fileExists(s))
	{
		std::vector<char> sitebuffer;
		char* sitedata = binaryBuffer("SITE.DAT", sitebuffer);
		int generatedArtifactSiteMissions = load<Uint16>(sitedata + 0x24);
		if (generatedArtifactSiteMissions > 0)
		{
			_save->getAlienStrategy().addMissionRun("artifacts", generatedArtifactSiteMissions);

			int spawnedArtifactSites = generatedArtifactSiteMissions;
			char siteTypeToBeSpawned = load<char>(sitedata + 0x26);
			if (siteTypeToBeSpawned == 'T')
			{
				// before the first hour of the month, the mission was generated already, but the site has not spawned yet
				spawnedArtifactSites--;
			}
			else
			{
				// after the first hour of the month
				// or not an artifact site type
			}
			ids["STR_ARTIFACT_SITE"] = spawnedArtifactSites + 1; // OXC stores the ID of the next site, thus +1
		}
	}

	_save->setAllIds(ids);
}

/**
 * Loads the LEASE.DAT file.
 * Contains globe camera settings.
 */
void SaveConverter::loadDatLease()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("LEASE.DAT", buffer);

	double lat = -Xcom2Rad(load<Sint16>(data + 0x00));
	double lon = -Xcom2Rad(load<Sint16>(data + 0x06));
	_save->setGlobeLongitude(lon);
	_save->setGlobeLatitude(lat);

	int zoom = load<Sint16>(data + 0x0C);
	const int DISTANCE[] = { 90, 120, 180, 360, 450, 720 };
	for (size_t i = 0; i < 6; ++i)
	{
		if (zoom == DISTANCE[i])
		{
			_save->setGlobeZoom(i);
			break;
		}
	}
}

/**
 * Loads the XCOM.DAT file.
 * Contains X-COM graph info.
 */
void SaveConverter::loadDatXcom()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("XCOM.DAT", buffer);

	const size_t ENTRIES = _rules->getCountries().size() + _rules->getRegions().size();
	const size_t MONTHS = 12;
	for (size_t i = 0; i < ENTRIES * MONTHS; ++i)
	{
		int score = load<int>(data + i * sizeof(int));
		size_t j = i % ENTRIES;
		// country
		if (j < _rules->getCountries().size())
		{
			_save->getCountries()->at(j)->getActivityXcom().push_back(score);
		}
		// region
		else
		{
			j -= _rules->getCountries().size();
			_save->getRegions()->at(j)->getActivityXcom().push_back(score);
		}
	}
}

/**
 * Loads the ALIEN.DAT file.
 * Contains Alien graph info.
 */
void SaveConverter::loadDatAlien()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("ALIEN.DAT", buffer);

	const size_t ENTRIES = _rules->getCountries().size() + _rules->getRegions().size();
	const size_t MONTHS = 12;
	for (size_t i = 0; i < ENTRIES * MONTHS; ++i)
	{
		int score = load<int>(data + i * sizeof(int));
		size_t j = i % ENTRIES;
		// country
		if (j < _rules->getCountries().size())
		{
			_save->getCountries()->at(j)->getActivityAlien().push_back(score);
		}
		// region
		else
		{
			j -= _rules->getCountries().size();
			_save->getRegions()->at(j)->getActivityAlien().push_back(score);
		}
	}
}

/**
 * Loads the DIPLOM.DAT file.
 * Contains country status.
 */
void SaveConverter::loadDatDiplom()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("DIPLOM.DAT", buffer);

	const size_t MONTHS = 12;
	std::vector<int64_t> income;
	for (size_t i = 0; i < MONTHS; ++i)
	{
		income.push_back(0);
	}

	const size_t ENTRY_SIZE = 36;
	for (size_t i = 0; i < _rules->getCountries().size(); ++i)
	{
		char *cdata = (data + i * ENTRY_SIZE);
		Country *country = _save->getCountries()->at(i);

		int satisfaction = load<Sint16>(cdata + 0x02);
		for (size_t j = 0; j < MONTHS; ++j)
		{
			int funding = load<Sint16>(cdata + 0x04 + j * sizeof(Sint16));
			funding *= 1000;
			income[j] += funding;
			country->getFunding().push_back(funding);
		}
		bool pact = satisfaction == 0;
		bool newPact = load<Sint16>(cdata + 0x1E) != 0;

		if (pact)
			country->setPact();
		if (newPact)
			country->setNewPact();
	}
	_save->getIncomes() = income;
}

/**
 * Loads the ZONAL.DAT file.
 * Contains alien region chances.
 */
void SaveConverter::loadDatZonal()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("ZONAL.DAT", buffer);

	std::map<std::string, int> chances;
	const size_t REGIONS = 12;
	for (size_t i = 0; i < REGIONS; ++i)
	{
		chances[_rules->getRegions()[i]] = load<Uint8>(data + i);
	}
	YAML::YamlRootNodeWriter writer;
	writer.setAsMap();
	writer.write("regions", chances);
	_save->getAlienStrategy().load(writer.toReader(), _mod);
}

/**
 * Loads the ACTS.DAT file.
 * Contains alien mission chances.
 */
void SaveConverter::loadDatActs()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("ACTS.DAT", buffer);

	std::map < std::string, std::map<std::string, int> > chances;
	const size_t REGIONS = 12;
	const size_t MISSIONS = 7;
	for (size_t i = 0; i < REGIONS * MISSIONS; ++i)
	{
		size_t mission = i % MISSIONS;
		size_t region = i / MISSIONS;

		chances[_rules->getRegions()[region]][_rules->getMissions()[mission]] = load<Uint8>(data + i);
	}

	YAML::YamlRootNodeWriter writer;
	writer.setAsMap();
	if (!chances.empty())
	{
		auto possibleMisions = writer["possibleMissions"];
		possibleMisions.setAsSeq();
		for (const auto& keyMapPair : chances)
		{
			auto regionAndMissions = possibleMisions.write();
			regionAndMissions.setAsMap();
			regionAndMissions.write("region", keyMapPair.first);
			regionAndMissions.write("missions", keyMapPair.second);
		}
	}
	_save->getAlienStrategy().load(writer.toReader(), _mod);
}

/**
 * Loads the MISSIONS.DAT file.
 * Contains ongoing alien missions.
 */
void SaveConverter::loadDatMissions()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("MISSIONS.DAT", buffer);

	const size_t REGIONS = 12;
	const size_t MISSIONS = 7;
	const size_t ENTRY_SIZE = 8;
	for (size_t i = 0; i < REGIONS * MISSIONS; ++i)
	{
		char *mdata = (data + i * ENTRY_SIZE);
		int wave = load<Uint16>(mdata + 0x00);
		if (wave != 0xFFFF)
		{
			int ufoCounter = load<Uint16>(mdata + 0x02);
			int spawn = load<Uint16>(mdata + 0x04);
			int race = load<Uint16>(mdata + 0x06);
			int mission = i % MISSIONS;
			int region = i / MISSIONS;

			YAML::YamlRootNodeWriter writer;
			writer.setAsMap();
			AlienMission *m = new AlienMission(*_mod->getAlienMission(_rules->getMissions()[mission], true));
			writer.write("region", _rules->getRegions()[region]);
			writer.write("race", _rules->getCrews()[race]);
			writer.write("nextWave", wave);
			writer.write("nextUfoCounter", ufoCounter);
			writer.write("spawnCountdown", spawn * 30);
			writer.write("uniqueID", _save->getId("ALIEN_MISSIONS"));
			if (m->getRules().getObjective() == OBJECTIVE_SITE)
			{
				int missionZone = 3; // pick a city for terror missions
				RuleRegion *rule = _mod->getRegion(_rules->getRegions()[region], true);
				if (rule->getMissionZones().size() <= 3)
				{
					// try to account for TFTD's artefacts and such
					missionZone = 0;
				}
				writer.write("missionSiteZone", RNG::generate(0, rule->getMissionZones().at(missionZone).areas.size() - 1));
			}
			m->load(writer.toReader(), *_save, _mod);
			_save->getAlienMissions().push_back(m);
			_missions[std::make_pair(mission, region)] = m;
		}
	}
}

/**
 * Loads the LOC.DAT file.
 * Contains globe markers.
 */
void SaveConverter::loadDatLoc()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("LOC.DAT", buffer);

	const size_t ENTRIES = 50;
	const size_t ENTRY_SIZE = buffer.size() / ENTRIES;
	for (size_t i = 0; i < ENTRIES; ++i)
	{
		char *tdata = (data + i * ENTRY_SIZE);
		TargetType type = (TargetType)load<Uint8>(tdata);

		int dat = load<Uint8>(tdata + 0x01);
		double lon = Xcom2Rad(load<Sint16>(tdata + 0x02));
		double lat = Xcom2Rad(load<Sint16>(tdata + 0x04));
		int timer = load<Sint16>(tdata + 0x06);
		int id = load<Sint16>(tdata + 0x0A);
		std::bitset<3> visibility(load<int>(tdata + 0x10));
		bool detected = !visibility.test(0);

		// can't declare variables in switches :(
		Target *target = 0;
		Ufo *ufo = 0;
		Craft *craft = 0;
		Base *xbase = 0;
		AlienBase *abase = 0;
		Waypoint *waypoint = 0;
		MissionSite *mission = 0;
		switch (type)
		{
		case TARGET_NONE:
			target = 0;
			break;
		case TARGET_UFO:
		case TARGET_CRASH:
		case TARGET_LANDED:
			ufo = new Ufo(_mod->getUfo(_rules->getUfos()[0], true), 0);
			ufo->setId(id);
			ufo->setCrashId(id);
			ufo->setLandId(id);
			ufo->setSecondsRemaining(timer);
			ufo->setDetected(detected);
			target = ufo;
			break;
		case TARGET_CRAFT:
			craft = new Craft(_mod->getCraft(_rules->getCrafts()[0], true), 0, id);
			target = craft;
			break;
		case TARGET_XBASE:
			xbase = new Base(_mod);
			target = xbase;
			break;
		case TARGET_ABASE:
			abase = new AlienBase(_mod->getDeployment("STR_ALIEN_BASE_ASSAULT", true), 0);
			abase->setId(id);
			abase->setAlienRace(_rules->getCrews()[dat]);
			abase->setDiscovered(detected);
			_save->getAlienBases()->push_back(abase);
			target = abase;
			break;
		case TARGET_WAYPOINT:
			waypoint = new Waypoint();
			waypoint->setId(id);
			_save->getWaypoints()->push_back(waypoint);
			target = waypoint;
			break;
		case TARGET_TERROR:
			mission = new MissionSite(_mod->getAlienMission("STR_ALIEN_TERROR", true), _mod->getDeployment("STR_TERROR_MISSION", true), nullptr);
			break;
		case TARGET_PORT:
			mission = new MissionSite(_mod->getAlienMission("STR_ALIEN_SURFACE_ATTACK", true), _mod->getDeployment("STR_PORT_TERROR", true), nullptr);
			break;
		case TARGET_ISLAND:
			mission = new MissionSite(_mod->getAlienMission("STR_ALIEN_SURFACE_ATTACK", true), _mod->getDeployment("STR_ISLAND_TERROR", true), nullptr);
			break;
		case TARGET_SHIP:
			mission = new MissionSite(_mod->getAlienMission("STR_ALIEN_SHIP_ATTACK", true), _mod->getDeployment("STR_CARGO_SHIP_P1", true), nullptr);
			break;
		case TARGET_ARTEFACT:
			mission = new MissionSite(_mod->getAlienMission("STR_ALIEN_ARTIFACT", true), _mod->getDeployment("STR_ARTIFACT_SITE_P1", true), nullptr);
			break;
		}
		if (mission != 0)
		{
			mission->setId(id);
			mission->setAlienRace(_rules->getCrews()[dat]);
			mission->setSecondsRemaining(timer * 3600);
			mission->setDetected(detected);
			_save->getMissionSites()->push_back(mission);
			target = mission;
		}
		if (target != 0)
		{
			target->setLongitude(lon);
			target->setLatitude(lat);
		}
		_targets.push_back(target);
		_targetDat.push_back(dat);
	}
}

/**
 * Loads the BASE.DAT file.
 * Contains X-COM base contents.
 */
void SaveConverter::loadDatBase()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("BASE.DAT", buffer);

	const size_t BASES = 8;
	const size_t BASE_SIZE = 6;
	const size_t FACILITIES = BASE_SIZE * BASE_SIZE;
	const size_t ENTRY_SIZE = buffer.size() / BASES;
	std::vector<Base*> bases(BASES, 0);
	for (size_t i = 0; i < _targets.size(); ++i)
	{
		Base *base = dynamic_cast<Base*>(_targets[i]);
		if (base != 0)
		{
			int j = _targetDat[i];
			char *bdata = (data + j * ENTRY_SIZE);
			std::string name = load<std::string>(bdata);
			// facilities
			for (size_t k = 0; k < FACILITIES; ++k)
			{
				size_t facilityType = load<Uint8>(bdata + _rules->getOffset("BASE.DAT_FACILITIES") + k);
				if (facilityType < _rules->getFacilities().size())
				{
					BaseFacility *facility = new BaseFacility(_mod->getBaseFacility(_rules->getFacilities()[facilityType], true), base);
					int x = k % BASE_SIZE;
					int y = k / BASE_SIZE;
					int days = load<Uint8>(bdata + _rules->getOffset("BASE.DAT_FACILITIES") + FACILITIES + k);
					facility->setX(x);
					facility->setY(y);
					facility->setBuildTime(days);
					base->getFacilities()->push_back(facility);
				}
			}
			int engineers = load<Uint8>(bdata + _rules->getOffset("BASE.DAT_ENGINEERS"));
			int scientists = load<Uint8>(bdata + _rules->getOffset("BASE.DAT_SCIENTISTS"));
			// items
			for (size_t k = 0; k < _rules->getItems().size(); ++k)
			{
				int qty = load<Uint16>(bdata + _rules->getOffset("BASE.DAT_ITEMS") + k * 2);
				if (qty != 0 && !_rules->getItems()[k].empty())
				{
					const RuleItem *rule = _mod->getItem(_rules->getItems()[k], true);
					base->getStorageItems()->addItem(rule, qty);
				}
			}
			base->setEngineers(engineers);
			base->setScientists(scientists);
			base->setName(name);
			bases[j] = base;
		}
	}

	for (auto i = bases.begin(); i != bases.end(); ++i)
	{
		if (*i != 0)
		{
			_save->getBases()->push_back(*i);
		}
	}
}

/**
 * Loads the ASTORE.DAT file.
 * Contains X-COM alien containment.
 */
void SaveConverter::loadDatAStore()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("ASTORE.DAT", buffer);

	const size_t ENTRY_SIZE = 12;
	const size_t ENTRIES = buffer.size() / ENTRY_SIZE;
	for (size_t i = 0; i < ENTRIES; ++i)
	{
		char *adata = (data + i * ENTRY_SIZE);
		int race = load<Uint8>(adata + 0x00);
		std::string liveAlien;
		if (race != 0)
		{
			int rank = load<Uint8>(adata + 0x01);
			int base = load<Uint8>(adata + 0x02);
			liveAlien = _rules->getAlienRaces()[race];
			liveAlien += _rules->getAlienRanks()[rank];
			if (base != 0xFF)
			{
				Base *b = dynamic_cast<Base*>(_targets[base]);
				const RuleItem *rule = _mod->getItem(liveAlien, true);
				b->getStorageItems()->addItem(rule);
			}
		}
		_aliens.push_back(liveAlien);
	}
}

/**
 * Loads the TRANSFER.DAT file.
 * Contains transfers to X-COM bases.
 */
void SaveConverter::loadDatTransfer()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("TRANSFER.DAT", buffer);

	const size_t ENTRY_SIZE = 8;
	const size_t ENTRIES = buffer.size() / ENTRY_SIZE;
	for (size_t i = 0; i < ENTRIES; ++i)
	{
		char *tdata = (data + i * ENTRY_SIZE);
		int qty = load<Uint8>(tdata + 0x06);
		if (qty != 0)
		{
			int baseSrc = load<Uint8>(tdata + 0x00);
			int baseDest = load<Uint8>(tdata + 0x01);
			Base *b = dynamic_cast<Base*>(_targets[baseDest]);
			int hours = load<Uint8>(tdata + 0x02);
			TransferType type = (TransferType)load<Uint8>(tdata + 0x03);
			int dat = load<Uint8>(tdata + 0x04);

			Transfer *transfer = new Transfer(hours);
			switch (type)
			{
			case TRANSFER_CRAFT:
				if (baseSrc == 255)
				{
					std::string newCraft = _rules->getCrafts()[dat];
					transfer->setCraft(new Craft(_mod->getCraft(newCraft, true), b, _save->getId(newCraft)));
				}
				else
				{
					transfer->setCraft(dynamic_cast<Craft*>(_targets[dat]));
				}
				break;
			case TRANSFER_SOLDIER:
				transfer->setSoldier(_soldiers[dat]);
				break;
			case TRANSFER_SCIENTIST:
				transfer->setScientists(qty);
				break;
			case TRANSFER_ENGINEER:
				transfer->setEngineers(qty);
				break;
			default:
				if (type == TRANSFER_ITEM)
					transfer->setItems(_mod->getItem(_rules->getItems()[dat], true), qty);
				else
					transfer->setItems(_mod->getItem(_aliens[dat], true));
				break;
			}

			b->getTransfers()->push_back(transfer);
		}
	}
}


/**
 * Loads the CRAFT.DAT file.
 * Contains X-COM craft and Alien UFOs.
 */
void SaveConverter::loadDatCraft()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("CRAFT.DAT", buffer);

	const size_t ENTRY_SIZE = buffer.size() / _targets.size();
	for (size_t i = 0; i < _targets.size(); ++i)
	{
		int j = _targetDat[i];
		char *cdata = (data + j * ENTRY_SIZE);
		int type = load<Uint8>(cdata);
		if (type != 0xFF)
		{
			YAML::YamlRootNodeWriter writer;
			writer.setAsMap();
			Craft *craft = dynamic_cast<Craft*>(_targets[i]);
			if (craft != 0)
			{
				craft->changeRules(_mod->getCraft(_rules->getCrafts()[type], true));

				int lweapon = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_LEFT_WEAPON"));
				int lammo = load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_LEFT_AMMO"));
				if (lweapon != 0xFF)
				{
					CraftWeapon *cw = new CraftWeapon(_mod->getCraftWeapon(_rules->getCraftWeapons()[lweapon], true), lammo);
					craft->getWeapons()->at(0) = cw;
				}
				int flight = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_FLIGHT"));
				int rweapon = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_RIGHT_WEAPON"));
				int rammo = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_RIGHT_AMMO"));
				if (rweapon != 0xFF)
				{
					CraftWeapon *cw = new CraftWeapon(_mod->getCraftWeapon(_rules->getCraftWeapons()[rweapon], true), rammo);
					craft->getWeapons()->at(1) = cw;
				}

				writer.write("damage", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_DAMAGE")));
				writer.write("speed", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_SPEED")));
				int dest = load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_DESTINATION"));
				writer.write("fuel", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_FUEL")));
				int base = load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_BASE"));
				writer.write("status", xcomStatus[load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_STATUS"))]);

				// vehicles
				const size_t VEHICLES = 5;
				for (size_t k = 0; k < VEHICLES; ++k)
				{
					int qty = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_ITEMS") + k);
					for (int v = 0; v < qty; ++v)
					{
						const RuleItem *rule = _mod->getItem(_rules->getItems()[k + 10], true);
						craft->getVehicles()->push_back(new Vehicle(rule, rule->getVehicleClipSize(), 4, -1));
					}
				}
				// items
				const size_t ITEMS = 50;
				for (size_t k = VEHICLES; k < VEHICLES + ITEMS; ++k)
				{
					int qty = load<Uint8>(cdata + _rules->getOffset("CRAFT.DAT_ITEMS") + k);
					if (qty != 0 && !_rules->getItems()[k + 10].empty())
					{
						const RuleItem *rule = _mod->getItem(_rules->getItems()[k + 10], true);
						craft->getItems()->addItem(rule, qty);
					}
				}

				std::bitset<7> state(load<int>(cdata + _rules->getOffset("CRAFT.DAT_STATE")));
				writer.write("lowFuel", state.test(1));

				craft->load(writer.toReader(), _mod->getScriptGlobal(), _mod, _save);

				if (flight != 0 && dest != 0xFFFF)
				{
					Target *t = _targets[dest];
					craft->setDestination(t);
				}
				if (base != 0xFFFF)
				{
					Base *b = dynamic_cast<Base*>(_targets[base]);
					craft->setBase(b, false);
					b->getCrafts()->push_back(craft);
				}
			}
			Ufo *ufo = dynamic_cast<Ufo*>(_targets[i]);
			if (ufo != 0)
			{
				ufo->changeRules(_mod->getUfo(_rules->getUfos()[type - 5], true));
				writer.write("damage", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_DAMAGE")));
				writer.write("altitude", xcomAltitudes[load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_ALTITUDE"))]);
				writer.write("speed", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_SPEED")));
				auto destWriter = writer["dest"];
				destWriter.setAsMap();
				destWriter.write("lon", Xcom2Rad(load<Sint16>(cdata + _rules->getOffset("CRAFT.DAT_DEST_LON"))));
				destWriter.write("lat", Xcom2Rad(load<Sint16>(cdata + _rules->getOffset("CRAFT.DAT_DEST_LAT"))));

				int mission = load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_MISSION"));
				int region = load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_REGION"));
				std::ostringstream trajectory;
				AlienMission *m = _missions[std::make_pair(mission, region)];
				if (m == 0)
				{
					YAML::YamlRootNodeWriter missionWriter;
					missionWriter.setAsMap();
					m = new AlienMission(*_mod->getAlienMission(_rules->getMissions()[mission], true));
					missionWriter.write("region", _rules->getRegions()[region]);
					missionWriter.write("race", _rules->getCrews()[load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_RACE"))]);
					missionWriter.write("nextWave", 1);
					missionWriter.write("nextUfoCounter", 0);
					missionWriter.write("spawnCountdown", 1000);
					missionWriter.write("uniqueID", _save->getId("ALIEN_MISSIONS"));
					m->load(missionWriter.toReader(), *_save, _mod);
					_save->getAlienMissions().push_back(m);
					_missions[std::make_pair(mission, region)] = m;
					if (mission == 6)
					{
						trajectory << UfoTrajectory::RETALIATION_ASSAULT_RUN;
					}
				}
				writer.write("mission", m->getId());
				m->increaseLiveUfos();
				if (trajectory.str().empty())
				{
					trajectory << "P" << load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_TRAJECTORY"));
				}
				writer.write("trajectory", trajectory.str());
				writer.write("trajectoryPoint", (int)load<Uint16>(cdata + _rules->getOffset("CRAFT.DAT_TRAJECTORY_POINT")));
				std::bitset<7> state(load<int>(cdata + _rules->getOffset("CRAFT.DAT_STATE")));
				writer.write("hyperDetected", state.test(6));

				ufo->load(writer.toReader(), _mod->getScriptGlobal(), *_mod, *_save);
				ufo->setSpeed(ufo->getSpeed());
				if (ufo->getStatus() == Ufo::CRASHED)
				{
					ufo->setSecondsRemaining(ufo->getSecondsRemaining() * 3600);
				}
				else if (ufo->getStatus() == Ufo::LANDED)
				{
					ufo->setSecondsRemaining(ufo->getSecondsRemaining() * 5);
				}
				else
				{
					ufo->setSecondsRemaining(0);
				}

				_save->getUfos()->push_back(ufo);
			}
		}
	}
}

/**
 * Loads the SOLDIER.DAT file.
 * Contains X-COM soldiers.
 */
void SaveConverter::loadDatSoldier()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("SOLDIER.DAT", buffer);

	const size_t SOLDIERS = 250;
	const size_t ENTRY_SIZE = buffer.size() / SOLDIERS;
	for (size_t i = 0; i < SOLDIERS; ++i)
	{
		char *sdata = (data + i * ENTRY_SIZE);
		int rank = load<Uint16>(sdata + _rules->getOffset("SOLDIER.DAT_RANK"));
		if (rank != 0xFFFF)
		{
			YAML::YamlRootNodeWriter writer;
			writer.setAsMap();
			int base = load<Uint16>(sdata + _rules->getOffset("SOLDIER.DAT_BASE"));
			int craft = load<Uint16>(sdata + _rules->getOffset("SOLDIER.DAT_CRAFT"));
			writer.write("missions", (int)load<Sint16>(sdata + _rules->getOffset("SOLDIER.DAT_MISSIONS")));
			writer.write("kills", (int)load<Sint16>(sdata + _rules->getOffset("SOLDIER.DAT_KILLS")));
			writer.write("recovery", (int)load<Sint16>(sdata + _rules->getOffset("SOLDIER.DAT_RECOVERY")));
			writer.write("name", load<std::string>(sdata + _rules->getOffset("SOLDIER.DAT_NAME")));
			writer.write("rank", rank);

			UnitStats initial;
			initial.tu = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_TU"));
			initial.health = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_HE"));
			initial.stamina = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_STA"));
			initial.reactions = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_RE"));
			initial.strength = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_STR"));
			initial.firing = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_FA"));
			initial.throwing = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_TA"));
			initial.melee = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_ME"));
			initial.psiStrength = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_PST"));
			initial.psiSkill = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_PSK"));
			initial.bravery = 110 - (10 * load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_INITIAL_BR")));
			writer.write("initialStats", initial);

			UnitStats current;
			current.tu = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_TU"));
			current.health = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_HE"));
			current.stamina = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_STA"));
			current.reactions = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_RE"));
			current.strength = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_STR"));
			current.firing = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_FA"));
			current.throwing = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_TA"));
			current.melee = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_ME"));
			current.psiStrength = 0;
			current.psiSkill = 0;
			current.bravery = 10 * load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_IMPROVED_BR"));
			current += initial;
			writer.write("currentStats", current);

			int armor = load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_ARMOR"));
			const std::vector<std::string> &armors = _rules->getArmor();
			if (armor >= 0 && armor < (int)armors.size())
			{
				writer.write("armor", armors[armor]);
			}
			else
			{
				throw Exception("Invalid armor index. Modded saves are not supported.");
			}
			writer.write("improvement", (int)load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_PSI")));
			writer.write("psiTraining", (int)load<char>(sdata + _rules->getOffset("SOLDIER.DAT_PSILAB")) != 0);
			writer.write("gender", (int)load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_GENDER")));
			writer.write("look", (int)load<Uint8>(sdata + _rules->getOffset("SOLDIER.DAT_LOOK")));
			writer.write("id", _save->getId("STR_SOLDIER"));

			Soldier *soldier = new Soldier(_mod->getSoldier(_mod->getSoldiersList().front(), true), nullptr, 0 /*nationality*/);
			soldier->load(writer.toReader(), _mod, _save, _mod->getScriptGlobal());
			if (base != 0xFFFF)
			{
				Base *b = dynamic_cast<Base*>(_targets[base]);
				b->getSoldiers()->push_back(soldier);
			}
			if (craft != 0xFFFF)
			{
				Craft *c = dynamic_cast<Craft*>(_targets[craft]);
				soldier->setCraft(c);
			}
			_soldiers.push_back(soldier);
		}
		else
		{
			_soldiers.push_back(0);
		}
	}
}

/**
 * Loads the RESEARCH.DAT file.
 * Contains X-COM research progress.
 */
void SaveConverter::loadDatResearch()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("RESEARCH.DAT", buffer);

	const size_t ENTRY_SIZE = buffer.size() / _rules->getResearch().size();
	for (size_t i = 0; i < _rules->getResearch().size(); ++i)
	{
		char *rdata = (data + i * ENTRY_SIZE);
		if (!_rules->getResearch()[i].empty())
		{
			RuleResearch *research = _mod->getResearch(_rules->getResearch()[i]);
			if (research != 0 && research->getCost() != 0)
			{
				bool discovered = load<Uint8>(rdata + 0x0A) != 0;
				bool popped = load<Uint8>(rdata + 0x12) != 0;
				if (discovered)
				{
					_save->addFinishedResearch(research, _mod, 0, false);
				}
				else if (popped)
				{
					_save->addPoppedResearch(research);
				}
			}
		}
	}
}

/**
 * Loads the UP.DAT file.
 * Contains X-COM Ufopaedia progress.
 */
void SaveConverter::loadDatUp()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("UP.DAT", buffer);

	const size_t ENTRY_SIZE = buffer.size() / _rules->getUfopaedia().size();
	for (size_t i = 0; i < _rules->getUfopaedia().size(); ++i)
	{
		char *rdata = (data + i * ENTRY_SIZE);
		ArticleDefinition *article = _mod->getUfopaediaArticle(_rules->getUfopaedia()[i]);
		if (article != 0 && article->section != UFOPAEDIA_NOT_AVAILABLE)
		{
			bool discovered = load<Uint8>(rdata + 0x08) == 2;
			if (discovered)
			{
				for (const auto& req : article->_requires)
				{
					RuleResearch *research = _mod->getResearch(req);
					if (research && research->getCost() == 0)
					{
						_save->addFinishedResearch(research, _mod, 0, false);
					}
				}
			}
		}
	}
}

/**
 * Loads the PROJECT.DAT file.
 * Contains X-COM research projects.
 */
void SaveConverter::loadDatProject()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("PROJECT.DAT", buffer);

	const size_t ENTRIES = _rules->getResearch().size();
	// days (Uint16) | scientists (Uint8)
	const size_t ENTRY_SIZE = ENTRIES * (sizeof(Uint16) + sizeof(Uint8));
	for (size_t i = 0; i < _save->getBases()->size(); ++i)
	{
		Base *base = _save->getBases()->at(i);
		char *pdata = (data + i * ENTRY_SIZE);
		Uint16 *arrRemaining = (Uint16*)pdata;
		Uint8 *arrScientists = (Uint8*)(&arrRemaining[ENTRIES]);
		for (size_t j = 0; j < _rules->getResearch().size(); ++j)
		{
			int remaining = load<Uint16>((char*)&arrRemaining[j]);
			int scientists = load<Uint8>((char*)&arrScientists[j]);
			if (remaining != 0 && !_rules->getResearch()[j].empty())
			{
				RuleResearch *research = _mod->getResearch(_rules->getResearch()[j]);
				if (research != 0 && research->getCost() != 0)
				{
					ResearchProject *project = new ResearchProject(research, research->getCost());
					project->setAssigned(scientists);
					project->setSpent(project->getCost() - remaining);
					base->addResearch(project);
					base->setScientists(base->getScientists() - scientists);
				}
			}
		}
	}
}

/**
 * Loads the BPROD.DAT file.
 * Contains X-COM manufacture projects.
 */
void SaveConverter::loadDatBProd()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("BPROD.DAT", buffer);

	const size_t ENTRIES = _rules->getManufacture().size();
	// hours (int) | engineers (Uint16) | quantity (Uint16)| produced (Uint16)
	const size_t ENTRY_SIZE = ENTRIES * (sizeof(int) + 3 * sizeof(Uint16));
	for (size_t i = 0; i < _save->getBases()->size(); ++i)
	{
		Base *base = _save->getBases()->at(i);
		char *pdata = (data + i * ENTRY_SIZE);
		int *arrRemaining = (int*)pdata;
		Uint16 *arrEngineers = (Uint16*)(&arrRemaining[ENTRIES]);
		Uint16 *arrTotal = (Uint16*)(&arrEngineers[ENTRIES]);
		Uint16 *arrProduced = (Uint16*)(&arrTotal[ENTRIES]);
		for (size_t j = 0; j < _rules->getManufacture().size(); ++j)
		{
			int remaining = load<int>((char*)&arrRemaining[j]);
			int engineers = load<Uint16>((char*)&arrEngineers[j]);
			int total = load<Uint16>((char*)&arrTotal[j]);
			int produced = load<Uint16>((char*)&arrProduced[j]);
			if (remaining != 0 && !_rules->getManufacture()[j].empty())
			{
				RuleManufacture *manufacture = _mod->getManufacture(_rules->getManufacture()[j]);
				if (manufacture != 0)
				{
					Production *project = new Production(manufacture, total);
					project->setAssignedEngineers(engineers);
					project->setTimeSpent(produced * manufacture->getManufactureTime() + manufacture->getManufactureTime() - remaining);
					base->addProduction(project);
					base->setEngineers(base->getEngineers() - engineers);
				}
			}
		}
	}
}

/**
 * Loads the XBASES.DAT file.
 * Contains targeted X-COM bases.
 */
void SaveConverter::loadDatXBases()
{
	std::vector<char> buffer;
	char *data = binaryBuffer("XBASES.DAT", buffer);
	const size_t REGIONS = 12;
	for (size_t i = 0; i < REGIONS; ++i)
	{
		char *bdata = (data + i * 4);
		bool detected = load<Uint16>(bdata + 0x00) != 0;
		if (detected)
		{
			int loc = load<Uint16>(bdata + 0x02);
			Base *base = dynamic_cast<Base*>(_targets[loc]);
			if (base != 0)
			{
				base->setRetaliationTarget(true);
			}
		}
	}

}

}
