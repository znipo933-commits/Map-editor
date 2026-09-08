#include "sec_npcraid.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

std::string itos(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); return b; }

std::string unquote(const std::string& s) {
	if(s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
	return s;
}

std::string formatHome(int x, int y, int z) {
	char b[64];
	snprintf(b, sizeof(b), "[%d,%d,%d]", x, y, z);
	return b;
}

std::string formatOutfit(const Outfit& o, bool isItem) {
	char b[80];
	if(isItem) snprintf(b, sizeof(b), "(%d,%d)", o.lookType, o.lookItem);
	else snprintf(b, sizeof(b), "(%d,%d-%d-%d-%d)", o.lookType,
	              o.lookHead, o.lookBody, o.lookLegs, o.lookFeet);
	return b;
}

bool parseOutfit(const std::string& v, Outfit& o, bool& isItem) {
	int look = 0, a = 0, b = 0, c = 0, d = 0;
	if(sscanf(v.c_str(), " ( %d , %d - %d - %d - %d )", &look, &a, &b, &c, &d) == 5) {
		o.lookType = look; o.lookHead = a; o.lookBody = b; o.lookLegs = c; o.lookFeet = d;
		isItem = false;
		return true;
	}
	if(sscanf(v.c_str(), " ( %d , %d )", &look, &a) == 2) {
		o.lookType = look; o.lookItem = a;
		isItem = true;
		return true;
	}
	return false;
}

} // namespace

// ============================================================================
// SecNpc

bool SecNpc::parse() {
	iName = kv.find("Name");
	iSex = kv.find("Sex");
	iRace = kv.find("Race");
	iOutfit = kv.find("Outfit");
	iHome = kv.find("Home");
	iRadius = kv.find("Radius");
	iGoStrength = kv.find("GoStrength");

	if(iHome < 0) return false;      // includes and fragments carry no Home

	if(iName >= 0) name = unquote(kv.lines[iName].value);
	if(iSex >= 0) sex = kv.lines[iSex].value;
	if(iRace >= 0) race = std::atoi(kv.lines[iRace].value.c_str());
	if(iOutfit >= 0) parseOutfit(kv.lines[iOutfit].value, outfit, outfitIsItem);
	if(iRadius >= 0) radius = std::atoi(kv.lines[iRadius].value.c_str());
	if(iGoStrength >= 0) goStrength = std::atoi(kv.lines[iGoStrength].value.c_str());
	if(sscanf(kv.lines[iHome].value.c_str(), " [ %d , %d , %d ]", &x, &y, &z) != 3) return false;

	// The Behaviour block runs to its matching closing brace.
	behaviourBegin = kv.find("Behaviour");
	behaviourEnd = -1;
	if(behaviourBegin >= 0) {
		int depth = 0;
		for(size_t i = (size_t)behaviourBegin; i < kv.lines.size(); ++i) {
			for(char ch : kv.lines[i].raw) {
				if(ch == '{') ++depth;
				else if(ch == '}') --depth;
			}
			if(depth <= 0) { behaviourEnd = (int)i + 1; break; }
		}
		if(behaviourEnd < 0) behaviourEnd = (int)kv.lines.size();
	}
	return true;
}

void SecNpc::apply() {
	if(iName >= 0) kv.setValue(iName, "\"" + name + "\"");
	if(iSex >= 0) kv.setValue(iSex, sex);
	if(iRace >= 0) kv.setValue(iRace, itos(race));
	if(iOutfit >= 0) kv.setValue(iOutfit, formatOutfit(outfit, outfitIsItem));
	if(iHome >= 0) kv.setValue(iHome, formatHome(x, y, z));
	if(iRadius >= 0) kv.setValue(iRadius, itos(radius));
	if(iGoStrength >= 0) kv.setValue(iGoStrength, itos(goStrength));
}

int SecNpc::behaviourLineCount() const {
	return (behaviourBegin < 0 || behaviourEnd <= behaviourBegin) ? 0 : behaviourEnd - behaviourBegin;
}

std::string SecNpc::behaviourText() const {
	std::string out;
	if(behaviourBegin < 0) return out;
	for(int i = behaviourBegin; i < behaviourEnd && i < (int)kv.lines.size(); ++i)
		out += kv.lines[i].raw;
	return out;
}

void SecNpc::setBehaviourText(const std::string& text) {
	if(behaviourBegin < 0) return;
	// Three of the 592 live files end at "}" with no trailing newline. Adding
	// one would rewrite a file the user only looked at.
	const std::string current = behaviourText();
	const bool had_newline = !current.empty() && current[current.size() - 1] == '\n';
	std::string body = text;
	if(!body.empty()) {
		const bool has_newline = body[body.size() - 1] == '\n';
		if(had_newline && !has_newline) body += "\n";
		if(!had_newline && has_newline) body.erase(body.size() - 1);
	}
	if(body == current) return;

	const int count = behaviourEnd - behaviourBegin;
	kv.erase(behaviourBegin, count);
	kv.insert(behaviourBegin, std::vector<std::string>(1, body));
	parse();
}

// ============================================================================
// SecRaid

bool SecRaid::parse() {
	iType = kv.find("Type");
	iInterval = kv.find("Interval");
	type = iType >= 0 ? kv.lines[iType].value : std::string();
	interval = iInterval >= 0 ? std::atol(kv.lines[iInterval].value.c_str()) : 0;

	points.clear();

	// Every wave opens with Delay - 348 Delay lines for 348 Position lines
	// across the live raid files - and runs to the next one.
	std::vector<int> starts;
	for(size_t i = 0; i < kv.lines.size(); ++i)
		if(kv.lines[i].isField && kv.lines[i].key == "Delay") starts.push_back((int)i);

	for(size_t s = 0; s < starts.size(); ++s) {
		SecRaidPoint w;
		w.lineBegin = starts[s];
		w.lineEnd = (s + 1 < starts.size()) ? starts[s + 1] : (int)kv.lines.size();

		for(int i = w.lineBegin; i < w.lineEnd; ++i) {
			const seckv::Line& line = kv.lines[i];
			if(!line.isField) continue;
			const std::string& k = line.key;
			const std::string& v = line.value;
			if(k == "Delay") w.delay = std::atoi(v.c_str());
			else if(k == "Position") sscanf(v.c_str(), " [ %d , %d , %d ]", &w.x, &w.y, &w.z);
			else if(k == "Spread") w.spread = std::atoi(v.c_str());
			else if(k == "Race") w.race = std::atoi(v.c_str());
			else if(k == "Lifetime") { w.lifetime = std::atoi(v.c_str()); w.hasLifetime = true; }
			else if(k == "Message") { w.message = unquote(v); w.hasMessage = true; }
			else if(k == "Count") {
				w.hasCount = true;
				int a = 1, b = 1;
				if(sscanf(v.c_str(), " ( %d , %d )", &a, &b) == 2) { w.countMin = a; w.countMax = b; }
				else w.countMin = w.countMax = std::atoi(v.c_str());
			}
		}
		points.push_back(w);
	}
	return true;
}

void SecRaid::apply() {
	// Update the lines that exist first: setValue never shifts an index.
	if(iType >= 0) kv.setValue(iType, type);
	if(iInterval >= 0) {
		char b[32];
		snprintf(b, sizeof(b), "%ld", interval);
		kv.setValue(iInterval, b);
	}

	// Collect lines that have to be created. 46 of venoreelfinvasion's 48
	// waves carry no Count, and halloweenhare has no Interval at all, so a
	// value typed into the editor had nowhere to go.
	std::vector<std::pair<int, std::string> > additions;

	if(iInterval < 0 && interval != 0) {
		char b[32];
		snprintf(b, sizeof(b), "%ld", interval);
		const int at = (iType >= 0) ? iType + 1 : 0;
		additions.push_back(std::make_pair(at, kv.makeLine("Interval", b)));
	}

	for(const SecRaidPoint& w : points) {
		bool haveCount = false, haveLifetime = false, haveMessage = false;
		int lastField = w.lineBegin;

		for(int i = w.lineBegin; i < w.lineEnd && i < (int)kv.lines.size(); ++i) {
			if(!kv.lines[i].isField) continue;
			lastField = i;
			const std::string k = kv.lines[i].key;
			if(k == "Delay") kv.setValue(i, itos(w.delay));
			else if(k == "Position") kv.setValue(i, formatHome(w.x, w.y, w.z));
			else if(k == "Spread") kv.setValue(i, itos(w.spread));
			else if(k == "Race") kv.setValue(i, itos(w.race));
			else if(k == "Lifetime") { kv.setValue(i, itos(w.lifetime)); haveLifetime = true; }
			else if(k == "Message") { kv.setValue(i, "\"" + w.message + "\""); haveMessage = true; }
			else if(k == "Count") {
				haveCount = true;
				kv.setValue(i, w.countMin == w.countMax
					? itos(w.countMin)
					: "(" + itos(w.countMin) + "," + itos(w.countMax) + ")");
			}
		}

		if(!haveCount && (w.hasCount || w.countMin != 1 || w.countMax != 1))
			additions.push_back(std::make_pair(lastField + 1, kv.makeLine("Count",
				w.countMin == w.countMax ? itos(w.countMin)
					: "(" + itos(w.countMin) + "," + itos(w.countMax) + ")")));
		if(!haveLifetime && (w.hasLifetime || w.lifetime != 0))
			additions.push_back(std::make_pair(lastField + 1, kv.makeLine("Lifetime", itos(w.lifetime))));
		if(!haveMessage && (w.hasMessage || !w.message.empty()))
			additions.push_back(std::make_pair(lastField + 1, kv.makeLine("Message", "\"" + w.message + "\"")));
	}

	if(additions.empty()) return;

	// Back to front, so the earlier insertion points stay valid.
	std::sort(additions.begin(), additions.end(),
	          [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
		return a.first > b.first;
	});
	for(const std::pair<int, std::string>& add : additions)
		kv.insert(add.first, std::vector<std::string>(1, add.second));

	parse();   // every index moved
}

void SecRaid::addWave(const SecRaidPoint& wave) {
	std::vector<std::string> block;
	block.push_back("\n");
	block.push_back(kv.makeLine("Delay", itos(wave.delay)));
	block.push_back(kv.makeLine("Position", formatHome(wave.x, wave.y, wave.z)));
	block.push_back(kv.makeLine("Spread", itos(wave.spread)));
	block.push_back(kv.makeLine("Race", itos(wave.race)));
	block.push_back(kv.makeLine("Count", wave.countMin == wave.countMax
		? itos(wave.countMin)
		: "(" + itos(wave.countMin) + "," + itos(wave.countMax) + ")"));
	if(wave.hasLifetime) block.push_back(kv.makeLine("Lifetime", itos(wave.lifetime)));
	if(wave.hasMessage) block.push_back(kv.makeLine("Message", "\"" + wave.message + "\""));

	kv.insert((int)kv.lines.size(), block);
	parse();
}

void SecRaid::removeWave(int index) {
	if(index < 0 || index >= (int)points.size()) return;
	const SecRaidPoint& w = points[index];

	// Take the comment block sitting directly above it too, or it would be
	// left describing a wave that no longer exists.
	int from = w.lineBegin;
	while(from > 0) {
		const seckv::Line& prev = kv.lines[from - 1];
		const std::string t = prev.raw;
		size_t p = t.find_first_not_of(" \t\r\n");
		if(p != std::string::npos && t[p] == '#') { --from; continue; }
		break;
	}
	kv.erase(from, w.lineEnd - from);
	parse();
}

// ============================================================================

bool loadNpcFile(const std::string& path, SecNpc& out) {
	if(!out.kv.load(path)) return false;
	out.path = path;
	return out.parse();
}

bool loadRaidFile(const std::string& path, SecRaid& out) {
	if(!out.kv.load(path)) return false;
	out.path = path;
	return out.parse();
}
