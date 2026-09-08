// Gates for the .npc and .evt reader/writer.
//   identity  load -> save reproduces the file byte for byte
//   mutation  edit every field the UI can edit, save, reload, compare
//
// Usage: test_npcraid <npc dir> <evt dir>

#include "sec_npcraid.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <vector>

static std::vector<std::string> listDir(const std::string& dir, const std::string& ext) {
	std::vector<std::string> out;
	DIR* d = opendir(dir.c_str());
	if(!d) return out;
	while(struct dirent* e = readdir(d)) {
		std::string n = e->d_name;
		if(n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0)
			out.push_back(dir + "/" + n);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
	return out;
}

int main(int argc, char** argv) {
	if(argc < 3) { fprintf(stderr, "usage: %s <npc dir> <evt dir>\n", argv[0]); return 2; }

	int identityFail = 0, mutateFail = 0, parseFail = 0, npcs = 0;

	for(const std::string& path : listDir(argv[1], ".npc")) {
		SecNpc npc;
		if(!loadNpcFile(path, npc)) continue;   // no Home: an include, not an NPC
		++npcs;

		if(npc.kv.serialize() != npc.kv.originalText) {
			if(++identityFail <= 5) printf("NPC IDENTITY FAIL %s\n", path.c_str());
			continue;
		}

		SecNpc m;
		loadNpcFile(path, m);
		m.name = m.name + " X";
		m.sex = (m.sex == "male") ? "female" : "male";
		m.race += 1;
		m.radius += 3;
		m.goStrength += 5;
		m.x += 1; m.y += 2; m.z = (m.z + 1) % 16;
		if(m.outfitIsItem) m.outfit.lookItem += 1;
		else { m.outfit.lookType += 1; m.outfit.lookLegs = (m.outfit.lookLegs + 7) % 133; }
		m.apply();
		const std::string behaviour = m.behaviourText();
		m.setBehaviourText(behaviour);          // must be a no-op

		SecNpc back;
		back.kv.loadFromString(m.kv.serialize());
		if(!back.parse()) { printf("NPC REPARSE FAIL %s\n", path.c_str()); ++parseFail; continue; }

		bool ok = back.name == m.name && back.sex == m.sex && back.race == m.race &&
		          back.radius == m.radius && back.goStrength == m.goStrength &&
		          back.x == m.x && back.y == m.y && back.z == m.z &&
		          back.outfitIsItem == m.outfitIsItem &&
		          back.outfit.lookType == m.outfit.lookType &&
		          back.outfit.lookItem == m.outfit.lookItem &&
		          back.outfit.lookLegs == m.outfit.lookLegs &&
		          back.behaviourText() == behaviour;
		if(!ok && ++mutateFail <= 5) printf("NPC MUTATE FAIL %s\n", path.c_str());
	}
	printf("\n.npc  files %d | identity fail %d | mutate fail %d | reparse fail %d\n",
	       npcs, identityFail, mutateFail, parseFail);

	int rIdentity = 0, rMutate = 0, rStruct = 0, raids = 0, waves = 0;
	for(const std::string& path : listDir(argv[2], ".evt")) {
		SecRaid raid;
		if(!loadRaidFile(path, raid)) continue;
		++raids;
		waves += (int)raid.points.size();

		if(raid.kv.serialize() != raid.kv.originalText) {
			if(++rIdentity <= 5) printf("RAID IDENTITY FAIL %s\n", path.c_str());
			continue;
		}

		// edit every wave and the file header
		SecRaid m;
		loadRaidFile(path, m);
		const size_t before = m.points.size();
		if(!m.type.empty()) m.type = (m.type == "BigRaid") ? "SmallRaid" : "BigRaid";
		m.interval += 60;
		for(SecRaidPoint& w : m.points) {
			w.delay += 1; w.x += 1; w.y += 2; w.spread += 1; w.race += 1;
			w.countMin += 1; w.countMax += 2;
			if(w.hasLifetime) w.lifetime += 10;
			if(w.hasMessage) w.message += "!";
		}
		m.apply();

		SecRaid back;
		back.kv.loadFromString(m.kv.serialize());
		back.parse();
		bool ok = back.points.size() == m.points.size() && back.type == m.type &&
		          back.interval == m.interval;
		for(size_t i = 0; ok && i < back.points.size(); ++i) {
			const SecRaidPoint& a = back.points[i];
			const SecRaidPoint& b = m.points[i];
			ok = a.delay == b.delay && a.x == b.x && a.y == b.y && a.z == b.z &&
			     a.spread == b.spread && a.race == b.race &&
			     a.countMin == b.countMin && a.countMax == b.countMax &&
			     (!b.hasLifetime || a.lifetime == b.lifetime) &&
			     (!b.hasMessage || a.message == b.message);
		}
		if(!ok && ++rMutate <= 5) printf("RAID MUTATE FAIL %s\n", path.c_str());

		// add a wave, then remove it again - must come back to where we were
		SecRaid s;
		loadRaidFile(path, s);
		SecRaidPoint w;
		w.delay = 2; w.x = 32000; w.y = 32000; w.z = 7; w.spread = 5; w.race = 21;
		w.countMin = 3; w.countMax = 9; w.hasCount = true;
		w.hasLifetime = true; w.lifetime = 600;
		w.hasMessage = true; w.message = "round trip";
		s.addWave(w);
		if(s.points.size() != before + 1) { if(++rStruct <= 5) printf("RAID ADD FAIL %s\n", path.c_str()); continue; }
		const SecRaidPoint& added = s.points.back();
		if(added.race != 21 || added.countMin != 3 || added.countMax != 9 ||
		   added.x != 32000 || added.lifetime != 600 || added.message != "round trip") {
			if(++rStruct <= 5) printf("RAID ADD VALUES FAIL %s\n", path.c_str());
			continue;
		}
		s.removeWave((int)s.points.size() - 1);
		if(s.points.size() != before) { if(++rStruct <= 5) printf("RAID REMOVE FAIL %s\n", path.c_str()); }
	}
	printf(".evt  files %d, %d waves | identity fail %d | mutate fail %d | add/remove fail %d\n",
	       raids, waves, rIdentity, rMutate, rStruct);

	const int fails = identityFail + mutateFail + parseFail + rIdentity + rMutate + rStruct;
	printf("\n%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
	return fails ? 1 : 0;
}
