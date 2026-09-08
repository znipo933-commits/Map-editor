//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Reader/writer for CipSoft RealOTS .mon monster race files.
//////////////////////////////////////////////////////////////////////

#include "sec_mon.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace secmon {

namespace {

const int KEY_WIDTH = 14;          // "RaceNumber    = 35"
const char* CONT_INDENT = "                 ";  // 17 spaces, aligns under '{'

struct NamedFlag { const char* name; MonsterFlag bit; };
const NamedFlag kFlags[] = {
	{"KickBoxes", FLAG_KICK_BOXES},
	{"KickCreatures", FLAG_KICK_CREATURES},
	{"SeeInvisible", FLAG_SEE_INVISIBLE},
	{"Unpushable", FLAG_UNPUSHABLE},
	{"DistanceFighting", FLAG_DISTANCE_FIGHTING},
	{"NoSummon", FLAG_NO_SUMMON},
	{"NoIllusion", FLAG_NO_ILLUSION},
	{"NoConvince", FLAG_NO_CONVINCE},
	{"NoBurning", FLAG_NO_BURNING},
	{"NoPoison", FLAG_NO_POISON},
	{"NoEnergy", FLAG_NO_ENERGY},
	{"NoHit", FLAG_NO_HIT},
	{"NoLifeDrain", FLAG_NO_LIFE_DRAIN},
	{"NoParalyze", FLAG_NO_PARALYZE},
};

// Every one of the 448 corpus files is a subsequence of this order, so a
// field that has to be created lands in an unambiguous place.
const char* kFieldOrder[] = {
	"RaceNumber", "Name", "Article", "Outfit", "Corpse", "Blood", "Experience",
	"SummonCost", "FleeThreshold", "Attack", "Defend", "Armor", "Poison",
	"LoseTarget", "Strategy", "Flags", "Resistance", "Skills", "Spells",
	"Inventory", "Talk",
};
const int kFieldCount = (int)(sizeof(kFieldOrder) / sizeof(kFieldOrder[0]));

// Blocks are separated from their neighbours by a blank line.
bool isBlockField(const std::string& k) {
	return k == "Flags" || k == "Resistance" || k == "Skills" ||
	       k == "Spells" || k == "Inventory" || k == "Talk";
}

// "Corpses" is the plural spelling of the same slot.
std::string canonicalKey(const std::string& k) { return k == "Corpses" ? "Corpse" : k; }

int fieldIndex(const std::string& k) {
	std::string c = canonicalKey(k);
	for(int i = 0; i < kFieldCount; ++i) if(c == kFieldOrder[i]) return i;
	return -1;
}

const char* kShapes[] = {"Actor", "Victim", "Origin", "Destination", "Angle"};
const char* kImpacts[] = {"Damage", "Field", "Healing", "Speed", "Drunken", "Strength", "Outfit", "Summon"};
const char* kBloods[] = {"Blood", "Slime", "Bones", "Fire", "Energy"};

std::string trim(const std::string& s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if(b == std::string::npos) return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

// Pull every integer out of a fragment, in order. Handles negatives, which
// resistances need (-4 means the race takes extra damage).
std::vector<int> allInts(const std::string& s) {
	std::vector<int> out;
	for(size_t i = 0; i < s.size(); ) {
		bool neg = (s[i] == '-' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]));
		if(neg || isdigit((unsigned char)s[i])) {
			size_t j = neg ? i + 1 : i;
			while(j < s.size() && isdigit((unsigned char)s[j])) ++j;
			out.push_back(std::atoi(s.substr(i, j - i).c_str()));
			i = j;
		} else {
			++i;
		}
	}
	return out;
}

std::vector<std::string> splitChar(const std::string& s, char sep) {
	std::vector<std::string> out;
	std::string cur;
	for(char c : s) {
		if(c == sep) { out.push_back(trim(cur)); cur.clear(); }
		else cur += c;
	}
	out.push_back(trim(cur));
	return out;
}

std::string unquote(const std::string& s) {
	std::string t = trim(s);
	if(t.size() >= 2 && t.front() == '"' && t.back() == '"') return t.substr(1, t.size() - 2);
	return t;
}

// Split a brace body on commas that sit at depth 0 and outside quotes.
std::vector<std::string> splitTop(const std::string& body) {
	std::vector<std::string> out;
	int depth = 0;
	bool quoted = false;
	std::string cur;
	for(size_t i = 0; i < body.size(); ++i) {
		char c = body[i];
		if(quoted) {
			cur += c;
			if(c == '\\' && i + 1 < body.size()) { cur += body[++i]; continue; }
			if(c == '"') quoted = false;
			continue;
		}
		if(c == '"') { quoted = true; cur += c; continue; }
		if(c == '(' || c == '{') { ++depth; cur += c; continue; }
		if(c == ')' || c == '}') { --depth; cur += c; continue; }
		if(c == ',' && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
		cur += c;
	}
	if(!trim(cur).empty()) out.push_back(trim(cur));
	return out;
}

std::string braceBody(const std::string& v) {
	size_t b = v.find('{');
	size_t e = v.rfind('}');
	if(b == std::string::npos || e == std::string::npos || e < b) return std::string();
	return v.substr(b + 1, e - b - 1);
}

std::string joinBlock(const std::string& key, const std::vector<std::string>& items, bool oneLine) {
	std::string out = key;
	if(out.size() < (size_t)KEY_WIDTH) out.append(KEY_WIDTH - out.size(), ' ');
	out += "= {";
	for(size_t i = 0; i < items.size(); ++i) {
		if(i) out += oneLine ? ", " : std::string(",\n") + CONT_INDENT;
		out += items[i];
	}
	out += "}\n";
	return out;
}

std::string kv(const std::string& key, const std::string& value) {
	std::string out = key;
	if(out.size() < (size_t)KEY_WIDTH) out.append(KEY_WIDTH - out.size(), ' ');
	out += "= " + value + "\n";
	return out;
}

std::string itos(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); return b; }

} // namespace

bool Skill::operator==(const Skill& o) const {
	return name == o.name && actual == o.actual && minimum == o.minimum && maximum == o.maximum &&
	       nextLevel == o.nextLevel && factorPercent == o.factorPercent && addLevel == o.addLevel;
}
bool Spell::operator==(const Spell& o) const {
	if(shape != o.shape || shapeParams != o.shapeParams || impact != o.impact ||
	   impactParams != o.impactParams || mana != o.mana) return false;
	if(impactHasOutfit != o.impactHasOutfit) return false;
	if(!impactHasOutfit) return true;
	return outfitLook == o.outfitLook && outfitIsItem == o.outfitIsItem &&
	       outfitItemType == o.outfitItemType &&
	       std::equal(outfitColors, outfitColors + 4, o.outfitColors);
}
bool Loot::operator==(const Loot& o) const {
	return itemId == o.itemId && maxAmount == o.maxAmount && chancePerMille == o.chancePerMille;
}
bool Resistance::operator==(const Resistance& o) const {
	return name == o.name && percent == o.percent;
}

int MonsterType::getHitPoints() const {
	for(const Skill& s : skills) if(s.name == "HitPoints") return s.actual;
	return 0;
}
void MonsterType::setHitPoints(int hp) {
	for(Skill& s : skills) {
		if(s.name == "HitPoints") { s.actual = s.maximum = hp; return; }
	}
	Skill s; s.name = "HitPoints"; s.actual = s.maximum = hp;
	skills.insert(skills.begin(), s);
}

const char* MonFile::shapeName(SpellShape s) { return kShapes[(int)s]; }
const char* MonFile::impactName(SpellImpact i) { return kImpacts[(int)i]; }
const char* MonFile::bloodName(BloodType b) { return kBloods[(int)b]; }
const char* MonFile::flagName(MonsterFlag f) {
	for(const NamedFlag& nf : kFlags) if(nf.bit == f) return nf.name;
	return "";
}

const std::vector<std::string>& MonFile::resistanceNames() {
	static const std::vector<std::string> names = {
		"burning", "death", "drown", "electrified", "energy", "fire", "holy",
		"ice", "lifedrain", "manadrain", "physical", "poison", "poisondot",
	};
	return names;
}

bool MonFile::isUnparsed(const std::string& key) const {
	return std::find(unparsed.begin(), unparsed.end(), key) != unparsed.end();
}

bool MonFile::load(const std::string& path, std::string* error) {
	std::ifstream in(path.c_str(), std::ios::binary);
	if(!in) { if(error) *error = "cannot open " + path; return false; }
	std::ostringstream ss;
	ss << in.rdbuf();
	sourcePath = path;
	return loadFromString(ss.str(), error);
}

bool MonFile::loadFromString(const std::string& text, std::string* error) {
	(void)error;   // parsing never fails: unknown fields are kept verbatim
	originalText = text;
	chunks.clear();
	unparsed.clear();
	data = MonsterType();

	// Split into lines, keeping their newline so raw bytes survive.
	std::vector<std::string> lines;
	size_t pos = 0;
	while(pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if(nl == std::string::npos) { lines.push_back(text.substr(pos)); break; }
		lines.push_back(text.substr(pos, nl - pos + 1));
		pos = nl + 1;
	}

	std::string passthrough;
	for(size_t i = 0; i < lines.size(); ) {
		const std::string& line = lines[i];

		// A field line is "Key<spaces>= value".
		size_t k = 0;
		while(k < line.size() && (isalpha((unsigned char)line[k]))) ++k;
		size_t eq = line.find('=', k);
		bool isField = k > 0 && eq != std::string::npos &&
		               trim(line.substr(k, eq - k)).empty();

		if(!isField) { passthrough += line; ++i; continue; }

		if(!passthrough.empty()) { Chunk c; c.raw = passthrough; chunks.push_back(c); passthrough.clear(); }

		std::string key = line.substr(0, k);
		std::string raw = line;
		std::string value = line.substr(eq + 1);
		++i;

		// A brace block may run over several lines; balance it, ignoring quotes.
		int depth = 0; bool quoted = false;
		auto scan = [&](const std::string& s) {
			for(size_t j = 0; j < s.size(); ++j) {
				char c = s[j];
				if(quoted) {
					if(c == '\\' && j + 1 < s.size()) { ++j; continue; }
					if(c == '"') quoted = false;
					continue;
				}
				if(c == '"') quoted = true;
				else if(c == '{') ++depth;
				else if(c == '}') --depth;
			}
		};
		scan(value);
		while(depth > 0 && i < lines.size()) {
			raw += lines[i];
			value += lines[i];
			scan(lines[i]);
			++i;
		}

		Chunk c; c.key = key; c.raw = raw;
		chunks.push_back(c);

		// --- parse the value into the struct ---
		std::string v = trim(value);
		bool ok = true;

		if(key == "RaceNumber") data.raceNumber = std::atoi(v.c_str());
		else if(key == "Name") data.name = unquote(v);
		else if(key == "Article") data.article = unquote(v);
		else if(key == "Experience") data.experience = std::atoi(v.c_str());
		else if(key == "SummonCost") data.summonCost = std::atoi(v.c_str());
		else if(key == "FleeThreshold") data.fleeThreshold = std::atoi(v.c_str());
		else if(key == "Attack") data.attack = std::atoi(v.c_str());
		else if(key == "Defend") data.defend = std::atoi(v.c_str());
		else if(key == "Armor") data.armor = std::atoi(v.c_str());
		else if(key == "Poison") data.poison = std::atoi(v.c_str());
		else if(key == "LoseTarget") data.loseTarget = std::atoi(v.c_str());
		else if(key == "Corpse" || key == "Corpses") {
			data.corpses = allInts(v);
			data.corpsePlural = (key == "Corpses");
		}
		else if(key == "Blood") {
			std::string n = trim(v);
			ok = false;
			for(int b = 0; b < 5; ++b) if(n == kBloods[b]) { data.blood = (BloodType)b; ok = true; break; }
		}
		else if(key == "Outfit") {
			// "(look, h-b-l-f)" or the item-disguise "(look, itemType)".
			// The colours are '-' separated, so allInts() must NOT be used
			// here: it would read each separator as a minus sign.
			std::string inner = v;
			size_t b = inner.find('('), e = inner.rfind(')');
			if(b != std::string::npos && e != std::string::npos && e > b)
				inner = inner.substr(b + 1, e - b - 1);
			size_t comma = inner.find(',');
			if(comma == std::string::npos) ok = false;
			else {
				data.outfitLook = std::atoi(trim(inner.substr(0, comma)).c_str());
				std::vector<std::string> parts = splitChar(trim(inner.substr(comma + 1)), '-');
				if(parts.size() == 4) {
					for(int j = 0; j < 4; ++j) data.outfitColors[j] = std::atoi(parts[j].c_str());
					data.outfitIsItem = false;
				} else if(parts.size() == 1) {
					data.outfitItemType = std::atoi(parts[0].c_str());
					data.outfitIsItem = true;
				} else ok = false;
			}
		}
		else if(key == "Strategy") {
			std::vector<int> n = allInts(v);
			if(n.size() >= 4) for(int j = 0; j < 4; ++j) data.strategy[j] = n[j];
			else ok = false;
		}
		else if(key == "Flags") {
			for(const std::string& item : splitTop(braceBody(v))) {
				std::string nm = trim(item);
				if(nm.empty()) continue;
				bool found = false;
				for(const NamedFlag& nf : kFlags) if(nm == nf.name) { data.flags |= nf.bit; found = true; break; }
				if(!found) ok = false;
			}
		}
		else if(key == "Resistance") {
			for(const std::string& item : splitTop(braceBody(v))) {
				std::string inner = item;
				size_t b = inner.find('('), e = inner.rfind(')');
				if(b == std::string::npos || e == std::string::npos) { ok = false; continue; }
				inner = inner.substr(b + 1, e - b - 1);
				size_t comma = inner.find(',');
				if(comma == std::string::npos) { ok = false; continue; }
				Resistance r;
				r.name = trim(inner.substr(0, comma));
				r.percent = std::atoi(trim(inner.substr(comma + 1)).c_str());
				data.resistances.push_back(r);
			}
		}
		else if(key == "Skills") {
			for(const std::string& item : splitTop(braceBody(v))) {
				size_t b = item.find('('), e = item.rfind(')');
				if(b == std::string::npos || e == std::string::npos) { ok = false; continue; }
				std::string inner = item.substr(b + 1, e - b - 1);
				size_t comma = inner.find(',');
				if(comma == std::string::npos) { ok = false; continue; }
				Skill s;
				s.name = trim(inner.substr(0, comma));
				std::vector<int> n = allInts(inner.substr(comma + 1));
				if(n.size() >= 6) {
					s.actual = n[0]; s.minimum = n[1]; s.maximum = n[2];
					s.nextLevel = n[3]; s.factorPercent = n[4]; s.addLevel = n[5];
				} else ok = false;
				data.skills.push_back(s);
			}
		}
		else if(key == "Spells") {
			for(const std::string& item : splitTop(braceBody(v))) {
				// Shape (a, b) -> Impact (c, d) : mana
				size_t arrow = item.find("->");
				size_t colon = item.rfind(':');
				if(arrow == std::string::npos || colon == std::string::npos) { ok = false; continue; }
				std::string lhs = trim(item.substr(0, arrow));
				std::string rhs = trim(item.substr(arrow + 2, colon - arrow - 2));
				Spell sp;
				size_t lp = lhs.find('(');
				std::string shapeNm = trim(lp == std::string::npos ? lhs : lhs.substr(0, lp));
				bool f = false;
				for(int s = 0; s < 5; ++s) if(shapeNm == kShapes[s]) { sp.shape = (SpellShape)s; f = true; break; }
				if(!f) ok = false;
				if(lp != std::string::npos) {
					std::string args = lhs.substr(lp);
					size_t ab = args.find('('), ae = args.rfind(')');
					if(ab != std::string::npos && ae != std::string::npos && ae > ab)
						args = args.substr(ab + 1, ae - ab - 1);
					for(const std::string& fld : splitTop(args)) {
						if(fld.find('(') != std::string::npos) { ok = false; break; }
						sp.shapeParams.push_back(std::atoi(fld.c_str()));
					}
				}
				size_t rp = rhs.find('(');
				std::string impactNm = trim(rp == std::string::npos ? rhs : rhs.substr(0, rp));
				f = false;
				for(int s = 0; s < 8; ++s) if(impactNm == kImpacts[s]) { sp.impact = (SpellImpact)s; f = true; break; }
				if(!f) ok = false;
				if(rp != std::string::npos) {
					std::string args = rhs.substr(rp);
					size_t ab = args.find('('), ae = args.rfind(')');
					if(ab != std::string::npos && ae != std::string::npos && ae > ab)
						args = args.substr(ab + 1, ae - ab - 1);
					std::vector<std::string> fields = splitTop(args);

					if(sp.impact == IMPACT_OUTFIT && !fields.empty() &&
					   !fields[0].empty() && fields[0][0] == '(') {
						// "(look, h-b-l-f)" or "(look, itemType)"
						std::string t = fields[0];
						size_t tb = t.find('('), te = t.rfind(')');
						if(tb != std::string::npos && te != std::string::npos && te > tb)
							t = t.substr(tb + 1, te - tb - 1);
						size_t tc = t.find(',');
						if(tc == std::string::npos) ok = false;
						else {
							sp.impactHasOutfit = true;
							sp.outfitLook = std::atoi(trim(t.substr(0, tc)).c_str());
							std::vector<std::string> cols = splitChar(trim(t.substr(tc + 1)), '-');
							if(cols.size() == 4) {
								for(int j = 0; j < 4; ++j) sp.outfitColors[j] = std::atoi(cols[j].c_str());
								sp.outfitIsItem = false;
							} else if(cols.size() == 1) {
								sp.outfitItemType = std::atoi(cols[0].c_str());
								sp.outfitIsItem = true;
							} else ok = false;
						}
						for(size_t fi = 1; fi < fields.size(); ++fi)
							sp.impactParams.push_back(std::atoi(fields[fi].c_str()));
					} else {
						// Any other nesting is something this reader does not
						// model; keep the whole field verbatim rather than
						// risk rewriting it wrongly.
						for(const std::string& fld : fields) {
							if(fld.find('(') != std::string::npos) { ok = false; break; }
							sp.impactParams.push_back(std::atoi(fld.c_str()));
						}
					}
				}
				sp.mana = std::atoi(trim(item.substr(colon + 1)).c_str());
				data.spells.push_back(sp);
			}
		}
		else if(key == "Inventory") {
			for(const std::string& item : splitTop(braceBody(v))) {
				std::vector<int> n = allInts(item);
				if(n.size() >= 3) {
					Loot l; l.itemId = n[0]; l.maxAmount = n[1]; l.chancePerMille = n[2];
					data.inventory.push_back(l);
				} else ok = false;
			}
		}
		else if(key == "Talk") {
			for(const std::string& item : splitTop(braceBody(v)))
				data.talk.push_back(unquote(item));
		}
		else {
			ok = false;   // a key we do not model; always written back raw
		}

		if(!ok && !isUnparsed(key)) unparsed.push_back(key);
	}

	if(!passthrough.empty()) { Chunk c; c.raw = passthrough; chunks.push_back(c); }

	original = data;
	return true;
}

std::string MonFile::renderField(const std::string& key) const {
	const MonsterType& m = data;

	if(key == "RaceNumber") return kv(key, itos(m.raceNumber));
	if(key == "Name") return kv(key, "\"" + m.name + "\"");
	if(key == "Article") return kv(key, "\"" + m.article + "\"");
	if(key == "Experience") return kv(key, itos(m.experience));
	if(key == "SummonCost") return kv(key, itos(m.summonCost));
	if(key == "FleeThreshold") return kv(key, itos(m.fleeThreshold));
	if(key == "Attack") return kv(key, itos(m.attack));
	if(key == "Defend") return kv(key, itos(m.defend));
	if(key == "Armor") return kv(key, itos(m.armor));
	if(key == "Poison") return kv(key, itos(m.poison));
	if(key == "LoseTarget") return kv(key, itos(m.loseTarget));
	if(key == "Blood") return kv(key, bloodName(m.blood));

	if(key == "Corpse" || key == "Corpses") {
		std::string v;
		for(size_t i = 0; i < m.corpses.size(); ++i) { if(i) v += ", "; v += itos(m.corpses[i]); }
		return kv(m.corpsePlural ? "Corpses" : "Corpse", v);
	}
	if(key == "Outfit") {
		if(m.outfitIsItem)
			return kv(key, "(" + itos(m.outfitLook) + ", " + itos(m.outfitItemType) + ")");
		return kv(key, "(" + itos(m.outfitLook) + ", " + itos(m.outfitColors[0]) + "-" +
		               itos(m.outfitColors[1]) + "-" + itos(m.outfitColors[2]) + "-" +
		               itos(m.outfitColors[3]) + ")");
	}
	if(key == "Strategy")
		return kv(key, "(" + itos(m.strategy[0]) + ", " + itos(m.strategy[1]) + ", " +
		               itos(m.strategy[2]) + ", " + itos(m.strategy[3]) + ")");

	if(key == "Flags") {
		std::vector<std::string> items;
		for(const NamedFlag& nf : kFlags) if(m.flags & nf.bit) items.push_back(nf.name);
		return joinBlock(key, items, false);
	}
	if(key == "Resistance") {
		std::vector<std::string> items;
		for(const Resistance& r : m.resistances)
			items.push_back("(" + r.name + ", " + itos(r.percent) + ")");
		return joinBlock(key, items, true);
	}
	if(key == "Skills") {
		std::vector<std::string> items;
		for(const Skill& s : m.skills)
			items.push_back("(" + s.name + ", " + itos(s.actual) + ", " + itos(s.minimum) + ", " +
			                itos(s.maximum) + ", " + itos(s.nextLevel) + ", " +
			                itos(s.factorPercent) + ", " + itos(s.addLevel) + ")");
		return joinBlock(key, items, false);
	}
	if(key == "Spells") {
		std::vector<std::string> items;
		for(const Spell& sp : m.spells) {
			std::string t = shapeName(sp.shape);
			t += " (";
			for(size_t i = 0; i < sp.shapeParams.size(); ++i) { if(i) t += ", "; t += itos(sp.shapeParams[i]); }
			t += ") -> ";
			t += impactName(sp.impact);
			t += " (";
			bool first = true;
			if(sp.impactHasOutfit) {
				t += "(" + itos(sp.outfitLook) + ", ";
				if(sp.outfitIsItem) t += itos(sp.outfitItemType);
				else t += itos(sp.outfitColors[0]) + "-" + itos(sp.outfitColors[1]) + "-" +
				          itos(sp.outfitColors[2]) + "-" + itos(sp.outfitColors[3]);
				t += ")";
				first = false;
			}
			for(size_t i = 0; i < sp.impactParams.size(); ++i) {
				if(!first) t += ", ";
				t += itos(sp.impactParams[i]);
				first = false;
			}
			t += ") : " + itos(sp.mana);
			items.push_back(t);
		}
		return joinBlock(key, items, false);
	}
	if(key == "Inventory") {
		std::vector<std::string> items;
		for(const Loot& l : m.inventory)
			items.push_back("(" + itos(l.itemId) + ", " + itos(l.maxAmount) + ", " + itos(l.chancePerMille) + ")");
		return joinBlock(key, items, false);
	}
	if(key == "Talk") {
		std::vector<std::string> items;
		for(const std::string& t : m.talk) items.push_back("\"" + t + "\"");
		return joinBlock(key, items, false);
	}
	return std::string();
}

namespace {

// Is this key's value the same in both structs?
bool sameField(const std::string& key, const MonsterType& a, const MonsterType& b) {
	if(key == "RaceNumber") return a.raceNumber == b.raceNumber;
	if(key == "Name") return a.name == b.name;
	if(key == "Article") return a.article == b.article;
	if(key == "Experience") return a.experience == b.experience;
	if(key == "SummonCost") return a.summonCost == b.summonCost;
	if(key == "FleeThreshold") return a.fleeThreshold == b.fleeThreshold;
	if(key == "Attack") return a.attack == b.attack;
	if(key == "Defend") return a.defend == b.defend;
	if(key == "Armor") return a.armor == b.armor;
	if(key == "Poison") return a.poison == b.poison;
	if(key == "LoseTarget") return a.loseTarget == b.loseTarget;
	if(key == "Blood") return a.blood == b.blood;
	if(key == "Corpse" || key == "Corpses") return a.corpses == b.corpses && a.corpsePlural == b.corpsePlural;
	if(key == "Outfit") {
		if(a.outfitLook != b.outfitLook || a.outfitIsItem != b.outfitIsItem) return false;
		if(a.outfitIsItem) return a.outfitItemType == b.outfitItemType;
		return std::equal(a.outfitColors, a.outfitColors + 4, b.outfitColors);
	}
	if(key == "Strategy") return std::equal(a.strategy, a.strategy + 4, b.strategy);
	if(key == "Flags") return a.flags == b.flags;
	if(key == "Resistance") return a.resistances == b.resistances;
	if(key == "Skills") return a.skills == b.skills;
	if(key == "Spells") return a.spells == b.spells;
	if(key == "Inventory") return a.inventory == b.inventory;
	if(key == "Talk") return a.talk == b.talk;
	return true;   // unmodelled key: never considered dirty
}

} // namespace

bool MonFile::isDirty() const {
	for(const Chunk& c : chunks)
		if(!c.key.empty() && !sameField(c.key, data, original)) return true;
	// A block the file did not have, or one the user emptied, also counts.
	return serialize() != originalText;
}

std::string MonFile::serialize(bool forceRender) const {
	// Which modelled fields should the output carry?
	bool want[kFieldCount];
	for(int i = 0; i < kFieldCount; ++i) {
		const std::string k = kFieldOrder[i];
		if(k == "Flags") want[i] = data.flags != 0;
		else if(k == "Resistance") want[i] = !data.resistances.empty();
		else if(k == "Skills") want[i] = !data.skills.empty();
		else if(k == "Spells") want[i] = !data.spells.empty();
		else if(k == "Inventory") want[i] = !data.inventory.empty();
		else if(k == "Talk") want[i] = !data.talk.empty();
		else if(k == "Corpse") want[i] = !data.corpses.empty();
		else want[i] = true;   // the scalars are mandatory
	}

	bool present[kFieldCount];
	for(int i = 0; i < kFieldCount; ++i) present[i] = false;
	for(const Chunk& c : chunks) {
		int idx = fieldIndex(c.key);
		if(idx >= 0) present[idx] = true;
	}

	// A field kept verbatim because we could not parse it must never be dropped.
	for(int i = 0; i < kFieldCount; ++i)
		if(present[i] && isUnparsed(kFieldOrder[i])) want[i] = true;

	std::string out;
	bool emitted[kFieldCount];
	for(int i = 0; i < kFieldCount; ++i) emitted[i] = false;

	auto endsWithBlankLine = [&]() {
		return out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n';
	};
	auto emitNew = [&](int i) {
		std::string text = renderField(kFieldOrder[i]);
		if(text.empty()) return;
		if(isBlockField(kFieldOrder[i]) && !out.empty() && !endsWithBlankLine()) out += "\n";
		out += text;
		emitted[i] = true;
	};

	size_t lastFieldChunk = 0;
	bool anyFieldChunk = false;
	for(size_t ci = 0; ci < chunks.size(); ++ci)
		if(fieldIndex(chunks[ci].key) >= 0) { lastFieldChunk = ci; anyFieldChunk = true; }

	for(size_t ci = 0; ci < chunks.size(); ++ci) {
		const Chunk& c = chunks[ci];
		int idx = fieldIndex(c.key);

		if(idx < 0) { out += c.raw; continue; }

		// Insert any missing field that sorts before this one.
		for(int i = 0; i < idx; ++i)
			if(want[i] && !present[i] && !emitted[i]) emitNew(i);

		if(!want[idx]) continue;          // block emptied by the user: drop the field

		if(isUnparsed(c.key) || (!forceRender && sameField(c.key, data, original))) {
			out += c.raw;
		} else {
			std::string rendered = renderField(c.key);
			out += rendered.empty() ? c.raw : rendered;
		}
		emitted[idx] = true;

		// Anything still missing goes right after the final field, ahead of
		// whatever trailing blank lines the file ends with.
		if(anyFieldChunk && ci == lastFieldChunk)
			for(int i = idx + 1; i < kFieldCount; ++i)
				if(want[i] && !present[i] && !emitted[i]) emitNew(i);
	}

	// A file with no fields at all (a brand new race) is written from scratch.
	if(!anyFieldChunk)
		for(int i = 0; i < kFieldCount; ++i)
			if(want[i] && !emitted[i]) emitNew(i);

	return out;
}

bool MonFile::save(const std::string& path, std::string* error) const {
	std::string text = serialize();
	std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
	if(!out) { if(error) *error = "cannot write " + path; return false; }
	out.write(text.data(), (std::streamsize)text.size());
	if(!out) { if(error) *error = "write failed for " + path; return false; }
	return true;
}

} // namespace secmon
