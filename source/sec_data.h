//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// The server-side data that sits beside a .sec map but is not part of it:
//
//   <root>/mon/*.mon    monster races
//   <root>/mon/*.evt    raids
//   <root>/dat/monster.db   spawn table
//   <root>/npc/*.npc    NPCs (only the Home line concerns the editor)
//
// A .sec world is <root>/map, so all four are found relative to the sector
// directory the user opened.
//
// Everything here is span preserving: an untouched record is written back
// with its original bytes. See sec_mon.h for why that matters.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_DATA_H_
#define RME_SEC_DATA_H_

#include "main.h"
#include "outfit.h"
#include "position.h"
#include "sec_mon.h"
#include "sec_monsterdb.h"

#include <map>
#include <string>
#include <vector>

struct SecNpc {
	std::string path;
	std::string file;
	std::string name;
	int x = 0, y = 0, z = 0;
	int radius = 2;
	int race = 0;
	Outfit outfit;
	std::string text;              // the whole file
	size_t homeBegin = 0;          // byte span of the "Home = [x,y,z]" line
	size_t homeEnd = 0;
	bool moved = false;
};

struct SecRaidPoint {
	int race = 0;
	int x = 0, y = 0, z = 0;
	int spread = 0;
	int countMin = 1, countMax = 1;
	int delay = 0;
	int lifetime = 0;
	std::string message;
	size_t posBegin = 0;           // byte span of the "Position = [x,y,z]" line
	size_t posEnd = 0;
};

struct SecRaid {
	std::string path;
	std::string file;
	std::string type;              // BigRaid or SmallRaid
	long interval = 0;
	std::string text;
	std::vector<SecRaidPoint> points;
	bool dirty = false;
};

class SecData
{
public:
	static SecData& get();

	void clear();
	// sectorDir is <root>/map. Missing sidecars are reported, not fatal.
	bool loadForSectorDir(const wxString& sectorDir, wxArrayString& warnings);
	bool save(wxArrayString& warnings, wxString& error);
	bool isLoaded() const { return loaded; }
	// True once a .sec map has been opened, whether or not the sidecar
	// folders were found. Lets the UI offer the entry and say why.
	bool wasSecMap() const { return tried; }
	const std::string& expectedRoot() const { return rootDir; }

	// ---- monsters -------------------------------------------------------
	std::map<int, secmon::MonFile> monsters;      // keyed by RaceNumber
	secmon::MonFile* monsterByRace(int race);
	const secmon::MonFile* monsterByRace(int race) const;
	std::string nameForRace(int race) const;
	int raceForName(const std::string& name) const;
	Outfit outfitForRace(int race) const;
	int freeRaceNumber() const;
	// .mon files of races removed in this session; deleted on save.
	std::vector<std::string> deletedRaceFiles;

	// ---- spawns ---------------------------------------------------------
	secmon::MonsterDb db;
	void reindexSpawns();
	void reindexNpcs();
	void reindexRaids();
	const std::vector<size_t>* spawnsAt(const Position& p) const;

	// ---- npcs and raids -------------------------------------------------
	std::vector<SecNpc> npcs;
	std::vector<SecRaid> raids;
	const std::vector<size_t>* npcsAt(const Position& p) const;
	// value is (raid index << 16) | point index
	const std::vector<uint32_t>* raidsAt(const Position& p) const;

	// Publishes every race and NPC to RME's creature database so the palette
	// and the map draw real sprites.
	void registerCreatures();

	std::string rootDir, monDir, datDir, npcDir;

	bool monstersDirty = false;

private:
	SecData() {}
	static uint64_t key(int x, int y, int z) {
		return ((uint64_t)(uint8_t)z << 48) | ((uint64_t)(uint16_t)y << 24) | (uint32_t)x;
	}
	void loadMonsters(wxArrayString& warnings);
	void loadSpawns(wxArrayString& warnings);
	void loadNpcs(wxArrayString& warnings);
	void loadRaids(wxArrayString& warnings);

	bool loaded = false;
	bool tried = false;
	std::map<uint64_t, std::vector<size_t> > spawnIndex;
	std::map<uint64_t, std::vector<size_t> > npcIndex;
	std::map<uint64_t, std::vector<uint32_t> > raidIndex;
	std::map<std::string, int> nameToRace;
};

#endif
