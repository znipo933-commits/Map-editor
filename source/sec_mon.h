//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Reader/writer for CipSoft RealOTS .mon monster race files.
//
// The writer is span-preserving: every field keeps the exact bytes it was
// read with and is only re-rendered when it has actually been edited. A
// load/save round trip with no edits is therefore byte-identical, and a
// field this code does not understand cannot be silently dropped.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_MON_H_
#define RME_SEC_MON_H_

#include <cstdint>
#include <string>
#include <vector>

namespace secmon {

enum BloodType {
	BLOOD_BLOOD = 0,
	BLOOD_SLIME,
	BLOOD_BONES,
	BLOOD_FIRE,
	BLOOD_ENERGY,
};

// Bit values are this reader's own; the file stores names.
enum MonsterFlag : uint32_t {
	FLAG_KICK_BOXES        = 1u << 0,
	FLAG_KICK_CREATURES    = 1u << 1,
	FLAG_SEE_INVISIBLE     = 1u << 2,
	FLAG_UNPUSHABLE        = 1u << 3,
	FLAG_DISTANCE_FIGHTING = 1u << 4,
	FLAG_NO_SUMMON         = 1u << 5,
	FLAG_NO_ILLUSION       = 1u << 6,
	FLAG_NO_CONVINCE       = 1u << 7,
	FLAG_NO_BURNING        = 1u << 8,
	FLAG_NO_POISON         = 1u << 9,
	FLAG_NO_ENERGY         = 1u << 10,
	FLAG_NO_HIT            = 1u << 11,
	FLAG_NO_LIFE_DRAIN     = 1u << 12,
	FLAG_NO_PARALYZE       = 1u << 13,
};

enum SpellShape {
	SHAPE_ACTOR = 0,
	SHAPE_VICTIM,
	SHAPE_ORIGIN,
	SHAPE_DESTINATION,
	SHAPE_ANGLE,
};

enum SpellImpact {
	IMPACT_DAMAGE = 0,
	IMPACT_FIELD,
	IMPACT_HEALING,
	IMPACT_SPEED,
	IMPACT_DRUNKEN,
	IMPACT_STRENGTH,
	IMPACT_OUTFIT,
	IMPACT_SUMMON,
};

// Skill names are kept as text: 444 races use the four monster skills, but
// human.mon carries the full player set (Level, MagicLevel, Shielding, ...).
struct Skill {
	std::string name;
	int actual = 0, minimum = 0, maximum = 0;
	int nextLevel = 0, factorPercent = 0, addLevel = 0;
	bool operator==(const Skill& o) const;
	bool operator!=(const Skill& o) const { return !(*this == o); }
};

struct Spell {
	SpellShape shape = SHAPE_ACTOR;
	std::vector<int> shapeParams;
	SpellImpact impact = IMPACT_DAMAGE;
	std::vector<int> impactParams;

	// The Outfit impact does not take flat numbers: it takes a nested outfit
	// tuple first, as in "Outfit ((122, 0-0-0-0), 15)" or "Outfit ((0, 0), 15)".
	// Flattening that on save produces a spell the server cannot read.
	bool impactHasOutfit = false;
	int outfitLook = 0;
	int outfitColors[4] = {0, 0, 0, 0};
	int outfitItemType = 0;
	bool outfitIsItem = false;

	int mana = 0;
	bool operator==(const Spell& o) const;
	bool operator!=(const Spell& o) const { return !(*this == o); }
};

struct Loot {
	int itemId = 0;
	int maxAmount = 1;
	int chancePerMille = 0;   // per 1,000 in a .mon (per 100,000 in 8.6 xml)
	bool operator==(const Loot& o) const;
	bool operator!=(const Loot& o) const { return !(*this == o); }
};

// Damage type name -> percent. Missing in 34 of 448 files.
struct Resistance {
	std::string name;
	int percent = 0;
	bool operator==(const Resistance& o) const;
	bool operator!=(const Resistance& o) const { return !(*this == o); }
};

struct MonsterType {
	int raceNumber = 0;
	std::string name;
	std::string article;

	// Outfit = (looktype, head-body-legs-feet), or the 6-race item-disguise
	// form Outfit = (looktype, itemType). outfitIsItem picks which is written
	// back; getting this wrong is the reported "server won't boot" bug.
	int outfitLook = 0;
	int outfitColors[4] = {0, 0, 0, 0};
	int outfitItemType = 0;
	bool outfitIsItem = false;

	// Corpse = N in 446 files, Corpses = a, b in 2.
	std::vector<int> corpses;
	bool corpsePlural = false;

	BloodType blood = BLOOD_BLOOD;
	int experience = 0;
	int summonCost = 0;
	int fleeThreshold = 0;
	int attack = 0;
	int defend = 0;
	int armor = 0;
	int poison = 0;
	int loseTarget = 0;
	int strategy[4] = {100, 0, 0, 0};

	uint32_t flags = 0;
	std::vector<Resistance> resistances;
	std::vector<Skill> skills;
	std::vector<Spell> spells;
	std::vector<Loot> inventory;
	std::vector<std::string> talk;

	// Convenience: HitPoints out of skills.
	int getHitPoints() const;
	void setHitPoints(int hp);
};

// One field as it appeared in the file, or a run of bytes we pass through
// untouched (the comment header, blank lines, anything unrecognised).
struct Chunk {
	std::string key;    // empty for a pass-through run
	std::string raw;    // original bytes, including the trailing newline
};

class MonFile {
public:
	bool load(const std::string& path, std::string* error = nullptr);
	bool loadFromString(const std::string& text, std::string* error = nullptr);

	// Renders to bytes. Fields equal to their loaded value emit their original
	// bytes verbatim; only edited fields are re-rendered.
	// forceRender re-renders every modelled field instead of emitting its
	// original bytes. Used by the test to exercise the writer on all 448 files.
	std::string serialize(bool forceRender = false) const;
	bool save(const std::string& path, std::string* error = nullptr) const;

	MonsterType data;              // edit this
	MonsterType original;          // as loaded, for the dirty comparison
	std::vector<Chunk> chunks;
	std::string sourcePath;
	std::string originalText;   // bytes as loaded

	// Keys whose value this reader could not fully understand. They are always
	// written back verbatim and should be shown read-only in the editor.
	std::vector<std::string> unparsed;
	bool isUnparsed(const std::string& key) const;

	bool isDirty() const;

	static const char* shapeName(SpellShape s);
	static const char* impactName(SpellImpact i);
	static const char* bloodName(BloodType b);
	static const char* flagName(MonsterFlag f);
	// All 13 damage type names the corpus uses, for the resistance editor.
	static const std::vector<std::string>& resistanceNames();

private:
	std::string renderField(const std::string& key) const;
};

} // namespace secmon

#endif
