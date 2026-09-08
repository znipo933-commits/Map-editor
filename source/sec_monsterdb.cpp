#include "sec_monsterdb.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace secmon {

namespace {

// "    50 31927 31622  9     50      3    700" — 42 columns.
std::string renderRow(const SpawnRow& r) {
	char buf[128];
	snprintf(buf, sizeof(buf), "%6d %5d %5d %2d %6d %6d %6d\n",
	         r.race, r.x, r.y, r.z, r.radius, r.amount, r.regen);
	return buf;
}

std::string sectionHeader(const SpawnRow& r) {
	char buf[128];
	snprintf(buf, sizeof(buf), "# ====== %04d,%04d,%02d ====================\n",
	         r.sectorX(), r.sectorY(), r.sectorZ());
	return buf;
}

// A data row is seven integers. Anything after them is an inline comment.
bool parseRow(const std::string& line, SpawnRow& out) {
	int v[7];
	int consumed = 0;
	int n = sscanf(line.c_str(), " %d %d %d %d %d %d %d%n",
	               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &consumed);
	if(n != 7) return false;
	out.race = v[0]; out.x = v[1]; out.y = v[2]; out.z = v[3];
	out.radius = v[4]; out.amount = v[5]; out.regen = v[6];
	return true;
}

bool isBlankOrComment(const std::string& line) {
	for(char c : line) {
		if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		return c == '#';
	}
	return true;
}

} // namespace

bool MonsterDb::load(const std::string& path, std::string* error) {
	std::ifstream in(path.c_str(), std::ios::binary);
	if(!in) { if(error) *error = "cannot open " + path; return false; }
	std::ostringstream ss; ss << in.rdbuf();
	return loadFromString(ss.str(), error);
}

bool MonsterDb::loadFromString(const std::string& text, std::string* error) {
	originalText = text;
	chunks.clear();
	loaded.clear();
	rows.clear();
	hadSentinel = false;

	std::vector<std::string> lines;
	size_t pos = 0;
	while(pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if(nl == std::string::npos) { lines.push_back(text.substr(pos)); break; }
		lines.push_back(text.substr(pos, nl - pos + 1));
		pos = nl + 1;
	}

	std::string pending;
	auto flush = [&]() {
		if(pending.empty()) return;
		Chunk c; c.raw = pending; chunks.push_back(c); pending.clear();
	};

	for(const std::string& line : lines) {
		if(!isBlankOrComment(line)) {
			int first = 0;
			if(sscanf(line.c_str(), " %d", &first) == 1) {
				// Race 0 is the terminator, not a spawn. It is written
				// "0 # zero for end of file", so it is not a 7-column row and
				// must be recognised before parseRow is tried.
				if(first == 0) {
					flush();
					Chunk c; c.kind = CHUNK_SENTINEL; c.raw = line;
					chunks.push_back(c);
					sentinelLine = line;
					hadSentinel = true;
					continue;
				}
				SpawnRow r;
				if(parseRow(line, r)) {
					flush();
					r.id = (int)loaded.size();
					loaded.push_back(r);
					rows.push_back(r);
					Chunk c; c.kind = CHUNK_ROW; c.raw = line; c.rowId = r.id;
					chunks.push_back(c);
					continue;
				}
			}
		}
		pending += line;
	}
	flush();

	if(!hadSentinel && error) *error = "no end-of-file sentinel row found";
	return true;
}

std::string MonsterDb::serialize() const {
	// Index surviving rows by id, and collect the ones created this session.
	std::vector<const SpawnRow*> byId(loaded.size(), nullptr);
	std::vector<const SpawnRow*> created;
	for(const SpawnRow& r : rows) {
		if(r.id >= 0 && (size_t)r.id < byId.size()) byId[r.id] = &r;
		else created.push_back(&r);
	}

	// Where does each new row belong? After the last existing row of its
	// sector, so the file keeps its "# ====== sx,sy,sz" grouping.
	std::vector<std::vector<const SpawnRow*>> insertAfter(chunks.size());
	std::vector<const SpawnRow*> appendAtEnd;
	for(const SpawnRow* nr : created) {
		int best = -1;
		for(size_t ci = 0; ci < chunks.size(); ++ci) {
			if(chunks[ci].kind != CHUNK_ROW) continue;
			const SpawnRow* ex = byId[chunks[ci].rowId];
			if(!ex) continue;
			if(ex->sectorX() == nr->sectorX() && ex->sectorY() == nr->sectorY() &&
			   ex->sectorZ() == nr->sectorZ())
				best = (int)ci;
		}
		if(best >= 0) insertAfter[best].push_back(nr);
		else appendAtEnd.push_back(nr);
	}

	std::string out;
	for(size_t ci = 0; ci < chunks.size(); ++ci) {
		const Chunk& c = chunks[ci];
		if(c.kind == CHUNK_SENTINEL) continue;      // re-emitted last, always

		if(c.kind == CHUNK_ROW) {
			const SpawnRow* live = byId[c.rowId];
			if(!live) continue;                      // deleted by the user
			out += live->sameValues(loaded[c.rowId]) ? c.raw : renderRow(*live);
		} else {
			out += c.raw;
		}
		for(const SpawnRow* nr : insertAfter[ci]) out += renderRow(*nr);
	}

	// New sectors go at the end — still above the sentinel.
	if(!appendAtEnd.empty()) {
		std::vector<const SpawnRow*> sorted = appendAtEnd;
		std::sort(sorted.begin(), sorted.end(), [](const SpawnRow* a, const SpawnRow* b) {
			if(a->sectorX() != b->sectorX()) return a->sectorX() < b->sectorX();
			if(a->sectorY() != b->sectorY()) return a->sectorY() < b->sectorY();
			return a->sectorZ() < b->sectorZ();
		});
		int lastX = -1, lastY = -1, lastZ = -1;
		for(const SpawnRow* nr : sorted) {
			if(nr->sectorX() != lastX || nr->sectorY() != lastY || nr->sectorZ() != lastZ) {
				out += sectionHeader(*nr);
				lastX = nr->sectorX(); lastY = nr->sectorY(); lastZ = nr->sectorZ();
			}
			out += renderRow(*nr);
		}
	}

	// The sentinel is the last line, no exceptions.
	if(!out.empty() && out[out.size() - 1] != '\n') out += "\n";
	out += sentinelLine;
	if(out[out.size() - 1] != '\n') out += "\n";
	return out;
}

bool MonsterDb::validate(std::string* problem) const {
	std::string text = serialize();

	size_t sent = text.rfind("\n0 ");
	if(text.compare(0, 2, "0 ") != 0 && sent == std::string::npos) {
		if(problem) *problem = "the end-of-file sentinel row is missing";
		return false;
	}
	// Nothing but whitespace may follow the sentinel.
	size_t after = text.find('\n', sent + 1);
	if(after != std::string::npos) {
		for(size_t i = after; i < text.size(); ++i) {
			if(!isspace((unsigned char)text[i])) {
				if(problem) *problem = "rows appear below the end-of-file sentinel; "
				                       "LoadMonsterhomes would never read them";
				return false;
			}
		}
	}
	// Every row must survive a reparse.
	MonsterDb check;
	check.loadFromString(text, nullptr);
	if(check.rows.size() != rows.size()) {
		if(problem) {
			char buf[160];
			snprintf(buf, sizeof(buf), "row count changed on save: %zu in the editor, %zu read back",
			         rows.size(), check.rows.size());
			*problem = buf;
		}
		return false;
	}
	return true;
}

bool MonsterDb::save(const std::string& path, std::string* error) const {
	std::string problem;
	if(!validate(&problem)) { if(error) *error = problem; return false; }
	std::string text = serialize();
	std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
	if(!out) { if(error) *error = "cannot write " + path; return false; }
	out.write(text.data(), (std::streamsize)text.size());
	if(!out) { if(error) *error = "write failed for " + path; return false; }
	return true;
}

} // namespace secmon
