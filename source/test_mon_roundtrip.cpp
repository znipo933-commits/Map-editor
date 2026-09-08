// Acceptance test for the .mon reader/writer.
//
//   gate 1  identity   load -> serialize must reproduce the file byte for byte
//   gate 2  render-all load -> serialize(force) -> reparse must give an equal
//                      struct, which exercises the writer on every field
//
// Usage: test_mon_roundtrip <dir-of-.mon-files>

#include "sec_mon.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace secmon;

static std::string slurp(const std::string& p) {
	std::ifstream in(p.c_str(), std::ios::binary);
	std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static bool sameStruct(const MonsterType& a, const MonsterType& b, std::string& why) {
	#define CHK(expr, label) if(!(expr)) { why = label; return false; }
	CHK(a.raceNumber == b.raceNumber, "raceNumber")
	CHK(a.name == b.name, "name")
	CHK(a.article == b.article, "article")
	CHK(a.outfitLook == b.outfitLook, "outfitLook")
	CHK(a.outfitIsItem == b.outfitIsItem, "outfitIsItem")
	CHK(a.outfitItemType == b.outfitItemType, "outfitItemType")
	CHK(std::equal(a.outfitColors, a.outfitColors + 4, b.outfitColors), "outfitColors")
	CHK(a.corpses == b.corpses, "corpses")
	CHK(a.corpsePlural == b.corpsePlural, "corpsePlural")
	CHK(a.blood == b.blood, "blood")
	CHK(a.experience == b.experience, "experience")
	CHK(a.summonCost == b.summonCost, "summonCost")
	CHK(a.fleeThreshold == b.fleeThreshold, "fleeThreshold")
	CHK(a.attack == b.attack, "attack")
	CHK(a.defend == b.defend, "defend")
	CHK(a.armor == b.armor, "armor")
	CHK(a.poison == b.poison, "poison")
	CHK(a.loseTarget == b.loseTarget, "loseTarget")
	CHK(std::equal(a.strategy, a.strategy + 4, b.strategy), "strategy")
	CHK(a.flags == b.flags, "flags")
	CHK(a.resistances == b.resistances, "resistances")
	CHK(a.skills == b.skills, "skills")
	CHK(a.spells.size() == b.spells.size(), "spells (count)")
	CHK(a.spells == b.spells, "spells")
	CHK(a.inventory == b.inventory, "inventory")
	CHK(a.talk == b.talk, "talk")
	#undef CHK
	return true;
}

int main(int argc, char** argv) {
	if(argc < 2) { fprintf(stderr, "usage: %s <dir>\n", argv[0]); return 2; }
	std::string dir = argv[1];

	std::vector<std::string> files;
	DIR* d = opendir(dir.c_str());
	if(!d) { fprintf(stderr, "cannot open dir %s\n", dir.c_str()); return 2; }
	while(struct dirent* e = readdir(d)) {
		std::string n = e->d_name;
		if(n.size() > 4 && n.compare(n.size() - 4, 4, ".mon") == 0) files.push_back(dir + "/" + n);
	}
	closedir(d);
	std::sort(files.begin(), files.end());

	int identityFail = 0, renderFail = 0, loadFail = 0, mutateFail = 0;
	int forcedDiffered = 0, itemOutfit = 0, pluralCorpse = 0;
	std::vector<std::string> unparsedKeys;

	for(const std::string& f : files) {
		std::string orig = slurp(f);
		MonFile mf;
		std::string err;
		if(!mf.loadFromString(orig, &err)) { printf("LOAD FAIL %s: %s\n", f.c_str(), err.c_str()); ++loadFail; continue; }

		for(const std::string& k : mf.unparsed) {
			std::string tag = k;
			if(std::find(unparsedKeys.begin(), unparsedKeys.end(), tag) == unparsedKeys.end())
				unparsedKeys.push_back(tag);
		}

		// gate 1 — identity
		std::string out = mf.serialize();
		if(out != orig) {
			++identityFail;
			if(identityFail <= 5) {
				size_t i = 0;
				while(i < out.size() && i < orig.size() && out[i] == orig[i]) ++i;
				size_t s = i > 60 ? i - 60 : 0;
				printf("IDENTITY FAIL %s at byte %zu\n  orig: %s\n  ours: %s\n", f.c_str(), i,
				       orig.substr(s, 120).c_str(), out.substr(s, 120).c_str());
			}
		}

		// gate 2 — render-all
		std::string forced = mf.serialize(true);
		MonFile mf2;
		if(!mf2.loadFromString(forced, &err)) { printf("REPARSE FAIL %s\n", f.c_str()); ++renderFail; continue; }
		std::string why;
		if(!sameStruct(mf.data, mf2.data, why)) {
			++renderFail;
			if(renderFail <= 8) printf("RENDER FAIL %s: field %s\n", f.c_str(), why.c_str());
		}
		if(forced != orig) ++forcedDiffered;
		if(mf.data.outfitIsItem) ++itemOutfit;
		if(mf.data.corpsePlural) ++pluralCorpse;

		// gate 3 - mutation: edit like the UI would, save, reload, compare
		MonFile mut;
		mut.loadFromString(orig, &err);
		MonsterType want = mut.data;
		want.experience += 7;
		want.attack += 1;
		want.setHitPoints(mut.data.getHitPoints() + 100);
		want.flags ^= FLAG_SEE_INVISIBLE;
		if(want.outfitIsItem) want.outfitItemType += 1;
		else want.outfitColors[2] = (want.outfitColors[2] + 5) % 133;
		{ Resistance r; r.name = "holy"; r.percent = -13; want.resistances.push_back(r); }
		{ Loot l; l.itemId = 3031; l.maxAmount = 42; l.chancePerMille = 999; want.inventory.push_back(l); }
		want.talk.push_back("round trip");
		// Spells must be exercised too: the Outfit impact carries a nested
		// outfit tuple that a flat number list would destroy.
		for(Spell& sp : want.spells) {
			sp.mana += 1;
			if(!sp.shapeParams.empty()) sp.shapeParams[0] += 1;
			if(sp.impactHasOutfit) {
				if(sp.outfitIsItem) sp.outfitItemType += 1;
				else sp.outfitColors[0] = (sp.outfitColors[0] + 3) % 133;
			} else if(!sp.impactParams.empty()) {
				sp.impactParams[0] += 1;
			}
		}
		{ Spell add; add.shape = SHAPE_ANGLE; add.shapeParams = {0, 8, 12};
		  add.impact = IMPACT_OUTFIT; add.impactHasOutfit = true;
		  add.outfitLook = 35; add.outfitColors[0] = 1; add.outfitColors[3] = 9;
		  add.impactParams.push_back(20); add.mana = 5;
		  want.spells.push_back(add); }
		mut.data = want;

		MonFile back;
		if(!back.loadFromString(mut.serialize(), &err)) {
			printf("MUTATE REPARSE FAIL %s\n", f.c_str()); ++mutateFail; continue;
		}
		if(!sameStruct(want, back.data, why)) {
			++mutateFail;
			if(mutateFail <= 8) printf("MUTATE FAIL %s: field %s\n", f.c_str(), why.c_str());
		}
	}

	printf("\nfiles %zu | load fail %d | identity fail %d | render fail %d | mutate fail %d\n",
	       files.size(), loadFail, identityFail, renderFail, mutateFail);
	printf("writer exercised: %d files re-rendered differently | item-disguise outfits %d | plural Corpses %d\n",
	       forcedDiffered, itemOutfit, pluralCorpse);
	printf("keys not modelled: ");
	if(unparsedKeys.empty()) printf("(none)");
	for(const std::string& k : unparsedKeys) printf("%s ", k.c_str());
	printf("\n");
	return (loadFail || identityFail || renderFail || mutateFail) ? 1 : 0;
}
