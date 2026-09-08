#include "sec_keyfile.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace seckv {

namespace {

std::string trim(const std::string& s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if(b == std::string::npos) return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

} // namespace

bool KeyFile::load(const std::string& path) {
	std::ifstream in(path.c_str(), std::ios::binary);
	if(!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	sourcePath = path;
	loadFromString(ss.str());
	return true;
}

void KeyFile::loadFromString(const std::string& text) {
	originalText = text;
	lines.clear();

	size_t pos = 0;
	while(pos < text.size()) {
		size_t nl = text.find('\n', pos);
		size_t end = (nl == std::string::npos) ? text.size() : nl + 1;

		Line line;
		line.raw = text.substr(pos, end - pos);

		// "Key<spaces>= value" - the key must be the first thing on the line.
		size_t k = 0;
		while(k < line.raw.size() && isalpha((unsigned char)line.raw[k])) ++k;
		size_t eq = line.raw.find('=', k);
		if(k > 0 && eq != std::string::npos && trim(line.raw.substr(k, eq - k)).empty()) {
			line.isField = true;
			line.key = line.raw.substr(0, k);
			line.value = trim(line.raw.substr(eq + 1));
		}

		lines.push_back(line);
		pos = end;
	}
}

std::string KeyFile::serialize() const {
	std::string out;
	for(const Line& line : lines) out += line.raw;
	return out;
}

bool KeyFile::save(const std::string& path, std::string* error) const {
	const std::string text = serialize();
	std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
	if(!out) { if(error) *error = "cannot write " + path; return false; }
	out.write(text.data(), (std::streamsize)text.size());
	if(!out) { if(error) *error = "write failed for " + path; return false; }
	return true;
}

int KeyFile::find(const std::string& key, int from) const {
	for(size_t i = (size_t)(from < 0 ? 0 : from); i < lines.size(); ++i)
		if(lines[i].isField && lines[i].key == key) return (int)i;
	return -1;
}

std::string KeyFile::get(const std::string& key, int from, const std::string& fallback) const {
	const int i = find(key, from);
	return i < 0 ? fallback : lines[i].value;
}

int KeyFile::alignment() const {
	// The column the '=' sits in. Every one of these files lines them up.
	for(const Line& line : lines) {
		if(!line.isField) continue;
		const size_t eq = line.raw.find('=');
		if(eq != std::string::npos) return (int)eq;
	}
	return 10;
}

std::string KeyFile::makeLine(const std::string& key, const std::string& value) const {
	std::string out = key;
	const int col = alignment();
	if((int)out.size() < col) out.append(col - out.size(), ' ');
	out += "= " + value + "\n";
	return out;
}

void KeyFile::setValue(int index, const std::string& value) {
	if(index < 0 || index >= (int)lines.size()) return;
	Line& line = lines[index];
	if(!line.isField) return;
	if(line.value == value) return;          // untouched stays byte-identical

	// Keep whatever alignment this line already had.
	const size_t eq = line.raw.find('=');
	std::string head = (eq == std::string::npos) ? line.key + " = " : line.raw.substr(0, eq + 1) + " ";
	std::string tail = "\n";
	if(!line.raw.empty() && line.raw[line.raw.size() - 1] != '\n') tail.clear();

	line.raw = head + value + tail;
	line.value = value;
}

void KeyFile::insert(int at, const std::vector<std::string>& raw_lines) {
	if(at < 0) at = 0;
	if(at > (int)lines.size()) at = (int)lines.size();
	std::vector<Line> block;
	for(const std::string& raw : raw_lines) {
		KeyFile tmp;
		tmp.loadFromString(raw.empty() || raw[raw.size() - 1] == '\n' ? raw : raw + "\n");
		for(const Line& l : tmp.lines) block.push_back(l);
	}
	lines.insert(lines.begin() + at, block.begin(), block.end());
}

void KeyFile::erase(int from, int count) {
	if(from < 0 || count <= 0 || from >= (int)lines.size()) return;
	if(from + count > (int)lines.size()) count = (int)lines.size() - from;
	lines.erase(lines.begin() + from, lines.begin() + from + count);
}

} // namespace seckv
