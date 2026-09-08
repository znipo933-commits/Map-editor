//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Reader/writer for CipSoft RealOTS dat/monster.db (the spawn table).
//
// Like the .mon reader this is span-preserving: an untouched row is written
// back with its original bytes, comments and all.
//
// Two properties are load-bearing and are enforced by validate():
//   * the file ends with the row "0 # zero for end of file". LoadMonsterhomes
//     stops reading there, so anything written below it is silently dead.
//   * a row whose race has no .mon must survive a save. Dropping unknown races
//     would delete live spawns.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_MONSTERDB_H_
#define RME_SEC_MONSTERDB_H_

#include <string>
#include <vector>

namespace secmon {

struct SpawnRow {
	int id = -1;        // stable handle; -1 means "created in this session"
	int race = 0;
	int x = 0, y = 0, z = 0;
	int radius = 50;    // a movement leash, not spawn scatter. Vanilla is 50.
	int amount = 1;
	int regen = 600;    // respawn delay

	bool sameValues(const SpawnRow& o) const {
		return race == o.race && x == o.x && y == o.y && z == o.z &&
		       radius == o.radius && amount == o.amount && regen == o.regen;
	}
	// Sector the row is filed under in the file's "# ====== sx,sy,sz" sections.
	int sectorX() const { return x / 32; }
	int sectorY() const { return y / 32; }
	int sectorZ() const { return z; }
};

class MonsterDb {
public:
	bool load(const std::string& path, std::string* error = nullptr);
	bool loadFromString(const std::string& text, std::string* error = nullptr);
	std::string serialize() const;
	bool save(const std::string& path, std::string* error = nullptr) const;

	// Rows the editor may add to, remove from and modify freely.
	std::vector<SpawnRow> rows;

	// Checks the invariants above. Returns false and fills problem on failure.
	bool validate(std::string* problem) const;

	size_t loadedRowCount() const { return loaded.size(); }
	const std::string& text() const { return originalText; }

private:
	enum ChunkKind { CHUNK_RAW, CHUNK_ROW, CHUNK_SENTINEL };
	struct Chunk {
		ChunkKind kind = CHUNK_RAW;
		std::string raw;
		int rowId = -1;
	};

	std::vector<Chunk> chunks;
	std::vector<SpawnRow> loaded;   // indexed by row id
	std::string originalText;
	std::string sentinelLine = "0 # zero for end of file\n";
	bool hadSentinel = false;
};

} // namespace secmon

#endif
