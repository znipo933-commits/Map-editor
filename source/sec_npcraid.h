//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// The CipSoft .npc (NPC) and .evt (raid) files.
//
// Both are "Key = value" line files, so both sit on seckv::KeyFile and are
// span preserving: editing a field rewrites that one line. An NPC's
// Behaviour block is a whole dialogue language; it is kept as text and
// written back exactly as given rather than modelled.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_NPCRAID_H_
#define RME_SEC_NPCRAID_H_

#include <cstdint>

#include "outfit.h"
#include "sec_keyfile.h"

#include <string>
#include <vector>

struct SecNpc {
	std::string path;
	std::string file;
	seckv::KeyFile kv;

	// Every one of the 592 live files carries exactly these, in this order.
	std::string name;
	std::string sex;            // male / female
	int race = 1;
	Outfit outfit;
	bool outfitIsItem = false;
	int x = 0, y = 0, z = 0;
	int radius = 2;
	int goStrength = 10;

	bool moved = false;         // home changed, for the map overlay

	bool parse();               // fills the fields from kv
	void apply();               // writes the fields back into kv
	bool isDirty() const { return kv.isDirty(); }

	// The Behaviour block, "Behaviour = {" through its closing "}".
	std::string behaviourText() const;
	void setBehaviourText(const std::string& text);
	int behaviourLineCount() const;

private:
	int iName = -1, iSex = -1, iRace = -1, iOutfit = -1;
	int iHome = -1, iRadius = -1, iGoStrength = -1;
	int behaviourBegin = -1, behaviourEnd = -1;   // [begin, end)
};

struct SecRaidPoint {
	// Line range of this wave inside the file, [begin, end).
	int lineBegin = 0, lineEnd = 0;

	int delay = 0;
	int x = 0, y = 0, z = 0;
	int spread = 0;
	int race = 0;
	int countMin = 1, countMax = 1;
	int lifetime = 0;
	std::string message;

	bool hasCount = false;
	bool hasLifetime = false;
	bool hasMessage = false;
};

struct SecRaid {
	std::string path;
	std::string file;
	seckv::KeyFile kv;

	std::string type;           // BigRaid or SmallRaid
	long interval = 0;
	std::vector<SecRaidPoint> points;

	bool parse();
	void apply();
	bool isDirty() const { return kv.isDirty(); }

	// Structural edits. Both re-parse afterwards, so indices stay valid.
	void addWave(const SecRaidPoint& wave);
	void removeWave(int index);

private:
	int iType = -1, iInterval = -1;
};

bool loadNpcFile(const std::string& path, SecNpc& out);
bool loadRaidFile(const std::string& path, SecRaid& out);

#endif
