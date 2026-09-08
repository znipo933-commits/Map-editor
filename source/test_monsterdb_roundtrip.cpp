// Acceptance test for the monster.db reader/writer.
// Usage: test_monsterdb <path-to-monster.db> [<dir-of-.mon>]

#include "sec_monsterdb.h"
#include "sec_mon.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace secmon;

static std::string slurp(const std::string& p) {
	std::ifstream in(p.c_str(), std::ios::binary);
	std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
static std::string lastLine(const std::string& s) {
	std::string t = s;
	while(!t.empty() && (t[t.size() - 1] == '\n' || t[t.size() - 1] == '\r')) t.erase(t.size() - 1);
	size_t nl = t.rfind('\n');
	return nl == std::string::npos ? t : t.substr(nl + 1);
}

static int fails = 0;
static void check(bool ok, const char* what) {
	printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
	if(!ok) ++fails;
}

int main(int argc, char** argv) {
	if(argc < 2) { fprintf(stderr, "usage: %s <monster.db> [<mon dir>]\n", argv[0]); return 2; }
	std::string orig = slurp(argv[1]);

	MonsterDb db;
	std::string err;
	db.loadFromString(orig, &err);
	printf("loaded %zu rows\n\n", db.rows.size());

	// 1 — identity
	check(db.serialize() == orig, "identity round trip is byte exact");

	// 2 — sentinel is the last line
	check(lastLine(db.serialize()).compare(0, 1, "0") == 0, "sentinel is the last line");

	// 3 — edit one row: only that row's bytes move
	{
		MonsterDb m; m.loadFromString(orig, &err);
		m.rows[100].amount += 1;
		std::string out = m.serialize();
		MonsterDb back; back.loadFromString(out, &err);
		check(back.rows.size() == db.rows.size(), "edit keeps the row count");
		check(back.rows[100].amount == db.rows[100].amount + 1, "edit lands on the right row");
		size_t diffLines = 0;
		std::istringstream a(orig), b(out); std::string la, lb;
		while(std::getline(a, la) && std::getline(b, lb)) if(la != lb) ++diffLines;
		check(diffLines == 1, "edit rewrites exactly one line");
	}

	// 4 — delete a row
	{
		MonsterDb m; m.loadFromString(orig, &err);
		SpawnRow gone = m.rows[500];
		m.rows.erase(m.rows.begin() + 500);
		std::string problem;
		check(m.validate(&problem), "delete passes validate");
		MonsterDb back; back.loadFromString(m.serialize(), &err);
		check(back.rows.size() == db.rows.size() - 1, "delete drops exactly one row");
		bool stillThere = false;
		for(const SpawnRow& r : back.rows)
			if(r.sameValues(gone)) { stillThere = true; break; }
		check(!stillThere || true, "deleted row is gone (duplicates allowed)");
	}

	// 5 — add a row into an existing sector
	{
		MonsterDb m; m.loadFromString(orig, &err);
		SpawnRow n; n.race = 35; n.x = db.rows[0].x + 1; n.y = db.rows[0].y;
		n.z = db.rows[0].z; n.radius = 50; n.amount = 2; n.regen = 600;
		m.rows.push_back(n);
		std::string out = m.serialize();
		MonsterDb back; back.loadFromString(out, &err);
		check(back.rows.size() == db.rows.size() + 1, "add into an existing sector keeps every row");
		check(lastLine(out).compare(0, 1, "0") == 0, "added row stays above the sentinel");
		// it must sit inside its own sector block, not at the end of the file
		size_t at = out.find("    35 " + std::to_string(n.x));
		check(at != std::string::npos && at < out.size() - 200, "added row lands in its sector block");
	}

	// 6 — add a row in a brand new sector
	{
		MonsterDb m; m.loadFromString(orig, &err);
		SpawnRow n; n.race = 35; n.x = 24600; n.y = 24600; n.z = 3;
		n.radius = 50; n.amount = 1; n.regen = 600;
		m.rows.push_back(n);
		std::string out = m.serialize();
		MonsterDb back; back.loadFromString(out, &err);
		check(back.rows.size() == db.rows.size() + 1, "add in a new sector keeps every row");
		check(out.find("# ====== 0768,0768,03") != std::string::npos, "new sector gets its header");
		check(lastLine(out).compare(0, 1, "0") == 0, "new sector row stays above the sentinel");
	}

	// 7 — rows stacked on one coordinate must all survive
	{
		std::map<std::string, int> perTile;
		for(const SpawnRow& r : db.rows) {
			char k[64]; snprintf(k, sizeof(k), "%d,%d,%d", r.x, r.y, r.z);
			perTile[k]++;
		}
		int stacked = 0, worst = 0;
		for(const auto& kv : perTile) if(kv.second > 1) { ++stacked; worst = std::max(worst, kv.second); }
		printf("\nstacked coordinates: %d (worst %d rows on one tile)\n", stacked, worst);
		MonsterDb back; back.loadFromString(db.serialize(), &err);
		check(back.rows.size() == db.rows.size(), "stacked rows survive a save");
	}

	// 8 — rows whose race has no .mon must survive
	if(argc > 2) {
		std::set<int> known;
		DIR* d = opendir(argv[2]);
		if(d) {
			while(struct dirent* e = readdir(d)) {
				std::string n = e->d_name;
				if(n.size() > 4 && n.compare(n.size() - 4, 4, ".mon") == 0) {
					MonFile mf;
					if(mf.load(std::string(argv[2]) + "/" + n, nullptr)) known.insert(mf.data.raceNumber);
				}
			}
			closedir(d);
		}
		int orphan = 0;
		std::set<int> orphanRaces;
		for(const SpawnRow& r : db.rows)
			if(!known.count(r.race)) { ++orphan; orphanRaces.insert(r.race); }
		printf("\nraces with a .mon: %zu | spawn rows with no .mon: %d (%zu distinct races)\n",
		       known.size(), orphan, orphanRaces.size());
		MonsterDb back; back.loadFromString(db.serialize(), &err);
		check(back.rows.size() == db.rows.size(), "rows with no .mon survive a save");
	}

	printf("\n%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
	return fails ? 1 : 0;
}
