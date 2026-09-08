//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "sec_data.h"
#include "creatures.h"

#include <wx/dir.h>
#include <wx/filename.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string readFile(const std::string& path) {
	std::ifstream in(path.c_str(), std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool writeFile(const std::string& path, const std::string& body) {
	std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
	if(!out) return false;
	out.write(body.data(), (std::streamsize)body.size());
	return (bool)out;
}

std::string trimmed(const std::string& s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if(b == std::string::npos) return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

// Walks a file line by line, handing out each line with its byte span.
template <typename Fn>
void forEachLine(const std::string& text, Fn fn) {
	size_t pos = 0;
	while(pos < text.size()) {
		size_t nl = text.find('\n', pos);
		size_t end = (nl == std::string::npos) ? text.size() : nl + 1;
		fn(text.substr(pos, end - pos), pos, end);
		pos = end;
	}
}

// "Key = value" split; returns false when the line is not one.
bool splitKey(const std::string& line, std::string& key, std::string& value) {
	size_t k = 0;
	while(k < line.size() && isalpha((unsigned char)line[k])) ++k;
	if(k == 0) return false;
	size_t eq = line.find('=', k);
	if(eq == std::string::npos) return false;
	if(!trimmed(line.substr(k, eq - k)).empty()) return false;
	key = line.substr(0, k);
	value = trimmed(line.substr(eq + 1));
	return true;
}

bool parseTriple(const std::string& v, int& a, int& b, int& c) {
	return sscanf(v.c_str(), " [ %d , %d , %d ]", &a, &b, &c) == 3;
}

std::vector<std::string> filesWithExt(const wxString& dir, const wxString& ext) {
	wxArrayString found;
	std::vector<std::string> out;
	if(!wxDirExists(dir)) return out;
	wxDir::GetAllFiles(dir, &found, ext, wxDIR_FILES);
	found.Sort();
	for(size_t i = 0; i < found.GetCount(); ++i)
		out.push_back(std::string(found[i].mb_str()));
	return out;
}

} // namespace

SecData& SecData::get() {
	static SecData instance;
	return instance;
}

void SecData::clear() {
	monsters.clear();
	deletedRaceFiles.clear();
	db = secmon::MonsterDb();
	npcs.clear();
	raids.clear();
	spawnIndex.clear();
	npcIndex.clear();
	raidIndex.clear();
	nameToRace.clear();
	rootDir.clear(); monDir.clear(); datDir.clear(); npcDir.clear();
	loaded = false;
	tried = false;
	monstersDirty = false;
}

bool SecData::loadForSectorDir(const wxString& sectorDir, wxArrayString& warnings) {
	clear();

	wxFileName root(sectorDir, wxEmptyString);
	root.RemoveLastDir();                       // <root>/map -> <root>
	const wxString rootPath = root.GetPath();
	const wxChar sep = wxFileName::GetPathSeparator();

	rootDir = std::string(rootPath.mb_str());
	monDir  = std::string((rootPath + sep + "mon").mb_str());
	datDir  = std::string((rootPath + sep + "dat").mb_str());
	npcDir  = std::string((rootPath + sep + "npc").mb_str());

	tried = true;

	// Collect the sidecar warnings separately: when none of the folders are
	// there at all, three cryptic lines are worse than one clear one.
	wxArrayString found;
	loadMonsters(found);
	loadSpawns(found);
	loadNpcs(found);
	loadRaids(found);

	loaded = !monsters.empty() || !db.rows.empty() || !npcs.empty();

	if(!loaded) {
		warnings.Add(wxString::Format(
			"No monster data found, so the Monsters, Spawns, NPCs and Raids editors "
			"stay disabled. They need mon, dat and npc folders sitting beside the "
			"folder that holds the .sec files. Looked beside the map for: %s",
			wxString(rootDir.c_str(), wxConvUTF8)));
		return false;
	}

	for(size_t i = 0; i < found.GetCount(); ++i) warnings.Add(found[i]);
	registerCreatures();
	return true;
}

void SecData::loadMonsters(wxArrayString& warnings) {
	std::vector<std::string> files = filesWithExt(wxString(monDir.c_str(), wxConvUTF8), "*.mon");
	if(files.empty()) {
		warnings.Add(wxString::Format("No .mon files found in %s - the monster editor will be empty.",
		                              wxString(monDir.c_str(), wxConvUTF8)));
		return;
	}
	for(const std::string& path : files) {
		secmon::MonFile mf;
		std::string err;
		if(!mf.load(path, &err)) { warnings.Add(wxString(err.c_str(), wxConvUTF8)); continue; }
		if(monsters.count(mf.data.raceNumber)) {
			warnings.Add(wxString::Format("Race %d is defined twice, keeping the first",
			                              mf.data.raceNumber));
			continue;
		}
		monsters[mf.data.raceNumber] = mf;
		nameToRace[mf.data.name] = mf.data.raceNumber;
	}
}

void SecData::loadSpawns(wxArrayString& warnings) {
	const std::string path = datDir + std::string(1, wxFileName::GetPathSeparator()) + "monster.db";
	if(!wxFileName::FileExists(wxString(path.c_str(), wxConvUTF8))) {
		warnings.Add(wxString::Format("No monster.db at %s - spawns cannot be edited.",
		                              wxString(path.c_str(), wxConvUTF8)));
		return;
	}
	std::string err;
	db.load(path, &err);
	if(!err.empty()) warnings.Add(wxString(err.c_str(), wxConvUTF8));
	reindexSpawns();

	int orphan = 0;
	for(const secmon::SpawnRow& r : db.rows)
		if(!monsters.count(r.race)) ++orphan;
	if(orphan)
		warnings.Add(wxString::Format("%d spawn row(s) name a race with no .mon file. "
		                              "They are shown as 'race N' and are preserved on save.", orphan));
}

void SecData::loadNpcs(wxArrayString& warnings) {
	std::vector<std::string> files = filesWithExt(wxString(npcDir.c_str(), wxConvUTF8), "*.npc");
	for(const std::string& path : files) {
		SecNpc npc;
		if(!loadNpcFile(path, npc)) continue;   // no Home: an include, not an NPC
		npc.file = wxFileName(wxString(path.c_str(), wxConvUTF8)).GetFullName().ToStdString();
		if(npc.name.empty()) npc.name = npc.file;
		npcs.push_back(npc);
	}
	if(npcs.empty() && !files.empty())
		warnings.Add("No .npc file carried a Home position.");
	reindexNpcs();
}

void SecData::loadRaids(wxArrayString& warnings) {
	std::vector<std::string> files = filesWithExt(wxString(monDir.c_str(), wxConvUTF8), "*.evt");
	for(const std::string& path : files) {
		SecRaid raid;
		if(!loadRaidFile(path, raid)) continue;
		raid.file = wxFileName(wxString(path.c_str(), wxConvUTF8)).GetFullName().ToStdString();
		raids.push_back(raid);
	}
	(void)warnings;
	reindexRaids();
}

void SecData::reindexSpawns() {
	spawnIndex.clear();
	for(size_t i = 0; i < db.rows.size(); ++i) {
		const secmon::SpawnRow& r = db.rows[i];
		spawnIndex[key(r.x, r.y, r.z)].push_back(i);
	}
}

void SecData::reindexNpcs() {
	npcIndex.clear();
	for(size_t i = 0; i < npcs.size(); ++i)
		npcIndex[key(npcs[i].x, npcs[i].y, npcs[i].z)].push_back(i);
}

void SecData::reindexRaids() {
	raidIndex.clear();
	for(size_t r = 0; r < raids.size(); ++r)
		for(size_t p = 0; p < raids[r].points.size(); ++p) {
			const SecRaidPoint& pt = raids[r].points[p];
			raidIndex[key(pt.x, pt.y, pt.z)].push_back((uint32_t)((r << 16) | p));
		}
}

const std::vector<size_t>* SecData::spawnsAt(const Position& p) const {
	std::map<uint64_t, std::vector<size_t> >::const_iterator it = spawnIndex.find(key(p.x, p.y, p.z));
	return it == spawnIndex.end() ? nullptr : &it->second;
}
const std::vector<size_t>* SecData::npcsAt(const Position& p) const {
	std::map<uint64_t, std::vector<size_t> >::const_iterator it = npcIndex.find(key(p.x, p.y, p.z));
	return it == npcIndex.end() ? nullptr : &it->second;
}
const std::vector<uint32_t>* SecData::raidsAt(const Position& p) const {
	std::map<uint64_t, std::vector<uint32_t> >::const_iterator it = raidIndex.find(key(p.x, p.y, p.z));
	return it == raidIndex.end() ? nullptr : &it->second;
}

secmon::MonFile* SecData::monsterByRace(int race) {
	std::map<int, secmon::MonFile>::iterator it = monsters.find(race);
	return it == monsters.end() ? nullptr : &it->second;
}
const secmon::MonFile* SecData::monsterByRace(int race) const {
	std::map<int, secmon::MonFile>::const_iterator it = monsters.find(race);
	return it == monsters.end() ? nullptr : &it->second;
}

std::string SecData::nameForRace(int race) const {
	const secmon::MonFile* m = monsterByRace(race);
	if(m) return m->data.name;
	char buf[32];
	snprintf(buf, sizeof(buf), "race %d", race);
	return buf;
}

int SecData::raceForName(const std::string& name) const {
	std::map<std::string, int>::const_iterator it = nameToRace.find(name);
	return it == nameToRace.end() ? 0 : it->second;
}

Outfit SecData::outfitForRace(int race) const {
	Outfit o;
	const secmon::MonFile* m = monsterByRace(race);
	if(!m) return o;
	o.lookType = m->data.outfitLook;
	if(m->data.outfitIsItem) {
		o.lookItem = m->data.outfitItemType;
	} else {
		o.lookHead = m->data.outfitColors[0];
		o.lookBody = m->data.outfitColors[1];
		o.lookLegs = m->data.outfitColors[2];
		o.lookFeet = m->data.outfitColors[3];
	}
	return o;
}

int SecData::freeRaceNumber() const {
	// The engine's race table is 512 entries, so stay inside it.
	for(int i = 1; i < 512; ++i)
		if(!monsters.count(i)) return i;
	return 0;
}

void SecData::registerCreatures() {
	for(std::map<int, secmon::MonFile>::const_iterator it = monsters.begin(); it != monsters.end(); ++it) {
		const std::string& name = it->second.data.name;
		if(name.empty()) continue;
		g_creatures.addCreatureType(name, false, outfitForRace(it->first));
	}
	for(const SecNpc& npc : npcs)
		g_creatures.addCreatureType(npc.name, true, npc.outfit);
}

bool SecData::save(wxArrayString& warnings, wxString& error) {
	if(!loaded) return true;
	const wxChar sep = wxFileName::GetPathSeparator();

	// --- monsters ---
	int monWritten = 0;
	for(std::map<int, secmon::MonFile>::iterator it = monsters.begin(); it != monsters.end(); ++it) {
		secmon::MonFile& mf = it->second;
		if(!mf.isDirty()) continue;
		std::string path = mf.sourcePath;
		if(path.empty()) {
			std::string base = mf.data.name;
			for(size_t i = 0; i < base.size(); ++i) {
				char c = (char)tolower((unsigned char)base[i]);
				base[i] = isalnum((unsigned char)c) ? c : '\0';
			}
			std::string clean;
			for(char c : base) if(c) clean += c;
			if(clean.empty()) clean = "race" + std::to_string(it->first);
			path = monDir + std::string(1, (char)sep) + clean + ".mon";
			mf.sourcePath = path;
		}
		std::string err;
		if(!mf.save(path, &err)) { error = wxString(err.c_str(), wxConvUTF8); return false; }
		++monWritten;
	}
	for(const std::string& gone : deletedRaceFiles) {
		wxString path(gone.c_str(), wxConvUTF8);
		if(wxFileName::FileExists(path) && !wxRemoveFile(path))
			warnings.Add(wxString::Format("Could not delete %s", path));
	}
	deletedRaceFiles.clear();
	if(monWritten) warnings.Add(wxString::Format("Wrote %d .mon file(s)", monWritten));

	// --- spawns ---
	if(!db.rows.empty() || db.loadedRowCount()) {
		const std::string path = datDir + std::string(1, (char)sep) + "monster.db";
		std::string err;
		if(!db.save(path, &err)) {
			error = "monster.db was NOT written: " + wxString(err.c_str(), wxConvUTF8);
			return false;
		}
		warnings.Add(wxString::Format("Wrote monster.db (%d spawn rows)", (int)db.rows.size()));
	}

	// --- npcs ---
	int npcWritten = 0;
	for(SecNpc& npc : npcs) {
		if(!npc.isDirty()) continue;
		std::string err;
		if(!npc.kv.save(npc.path, &err)) {
			error = wxString(err.c_str(), wxConvUTF8);
			return false;
		}
		npc.kv.originalText = npc.kv.serialize();
		npc.moved = false;
		++npcWritten;
	}
	if(npcWritten) warnings.Add(wxString::Format("Wrote %d .npc file(s)", npcWritten));

	// --- raids ---
	int raidWritten = 0;
	for(SecRaid& raid : raids) {
		if(!raid.isDirty()) continue;
		std::string err;
		if(!raid.kv.save(raid.path, &err)) {
			error = wxString(err.c_str(), wxConvUTF8);
			return false;
		}
		raid.kv.originalText = raid.kv.serialize();
		++raidWritten;
	}
	if(raidWritten) warnings.Add(wxString::Format("Wrote %d raid file(s)", raidWritten));

	return true;
}
