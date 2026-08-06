//////////////////////////////////////////////////////////////////////
// CipSoft .sec sector format - implementation. See sec_format.h.
//////////////////////////////////////////////////////////////////////

#include "sec_format.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace sec {

	namespace {

		struct Cursor
		{
			const std::string& s;
			size_t i;

			explicit Cursor(const std::string& str, size_t pos = 0) : s(str), i(pos) {}

			bool eof() const { return i >= s.size(); }
			char peek() const { return i < s.size() ? s[i] : '\0'; }

			void skipSpaces()
			{
				while(i < s.size() && s[i] == ' ') {
					++i;
				}
			}

			bool startsWith(const char* lit) const
			{
				size_t n = 0;
				while(lit[n] != '\0') {
					++n;
				}
				return s.compare(i, n, lit) == 0;
			}

			// [A-Za-z][A-Za-z0-9]*
			std::string word()
			{
				size_t start = i;
				while(i < s.size() && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
					++i;
				}
				while(i < s.size() && s[i] >= '0' && s[i] <= '9' && i > start) {
					++i;
				}
				return s.substr(start, i - start);
			}

			bool number(long& out)
			{
				size_t start = i;
				if(i < s.size() && (s[i] == '-' || s[i] == '+')) {
					++i;
				}
				size_t digits = i;
				while(i < s.size() && s[i] >= '0' && s[i] <= '9') {
					++i;
				}
				if(i == digits) {
					i = start;
					return false;
				}
				out = std::strtol(s.substr(start, i - start).c_str(), nullptr, 10);
				return true;
			}
		};

		void fail(const std::string& what)
		{
			throw std::runtime_error("sec: " + what);
		}

		// Reads a value: either "quoted string" or a signed integer, raw.
		std::string parseValue(Cursor& c)
		{
			if(c.peek() == '"') {
				size_t start = c.i;
				++c.i;
				while(!c.eof()) {
					if(c.s[c.i] == '\\') {
						c.i += 2;
						continue;
					}
					if(c.s[c.i] == '"') {
						++c.i;
						break;
					}
					++c.i;
				}
				return c.s.substr(start, c.i - start);
			}
			size_t start = c.i;
			long dummy = 0;
			if(!c.number(dummy)) {
				fail("bad attribute value at offset " + fromInt((long)c.i));
			}
			return c.s.substr(start, c.i - start);
		}

		std::vector<Item> parseContent(Cursor& c);

		Item parseItem(Cursor& c)
		{
			Item item;
			long id = 0;
			if(!c.number(id) || id < 0) {
				fail("expected item id at offset " + fromInt((long)c.i));
			}
			item.id = (uint16_t)id;

			for(;;) {
				size_t save = c.i;
				c.skipSpaces();
				size_t name_start = c.i;
				std::string name = c.word();
				if(name.empty() || c.peek() != '=') {
					c.i = save;
					return item;
				}
				(void)name_start;
				++c.i; // '='
				if(name == "Content") {
					item.has_content = true;
					item.content = parseContent(c);
					continue;
				}
				item.attrs.push_back(std::make_pair(name, parseValue(c)));
			}
		}

		std::vector<Item> parseContent(Cursor& c)
		{
			if(c.peek() != '{') {
				fail("expected '{' after Content=");
			}
			++c.i;
			std::vector<Item> items;
			for(;;) {
				c.skipSpaces();
				if(c.eof()) {
					fail("unterminated Content={");
				}
				if(c.peek() == '}') {
					++c.i;
					return items;
				}
				items.push_back(parseItem(c));
				c.skipSpaces();
				if(c.peek() == ',') {
					++c.i;
				}
			}
		}

		bool isTileLine(const std::string& line)
		{
			size_t i = 0;
			size_t digits = 0;
			while(i < line.size() && line[i] >= '0' && line[i] <= '9') {
				++i;
				++digits;
			}
			if(digits == 0 || i >= line.size() || line[i] != '-') {
				return false;
			}
			++i;
			digits = 0;
			while(i < line.size() && line[i] >= '0' && line[i] <= '9') {
				++i;
				++digits;
			}
			return digits != 0 && i < line.size() && line[i] == ':';
		}

		Tile parseTile(const std::string& line)
		{
			size_t colon = line.find(':');
			if(colon == std::string::npos) {
				fail("tile line without ':'");
			}
			std::string head = line.substr(0, colon);
			size_t dash = head.find('-');
			Tile tile;
			tile.x = (int)std::strtol(head.substr(0, dash).c_str(), nullptr, 10);
			tile.y = (int)std::strtol(head.substr(dash + 1).c_str(), nullptr, 10);

			Cursor c(line, colon + 1);
			for(;;) {
				c.skipSpaces();
				if(c.eof()) {
					break;
				}
				if(c.startsWith("Content=")) {
					c.i += 8;
					tile.has_content = true;
					tile.content = parseContent(c);
					c.skipSpaces();
					if(c.peek() == ',') {
						++c.i;
					}
					continue;
				}
				std::string flag = c.word();
				if(flag.empty()) {
					break;
				}
				if(flag == "Refresh") {
					tile.refresh = true;
				} else if(flag == "ProtectionZone") {
					tile.protection_zone = true;
				} else if(flag == "NoLogout") {
					tile.no_logout = true;
				} else {
					fail("unknown tile flag '" + flag + "'");
				}
				c.skipSpaces();
				if(c.peek() == ',') {
					++c.i;
				}
			}
			return tile;
		}

		void dumpItem(const Item& item, std::string& out)
		{
			out += fromInt((long)item.id);
			for(size_t i = 0; i < item.attrs.size(); ++i) {
				out += ' ';
				out += item.attrs[i].first;
				out += '=';
				out += item.attrs[i].second;
			}
			if(item.has_content) {
				out += " Content={";
				for(size_t i = 0; i < item.content.size(); ++i) {
					if(i != 0) {
						out += ", ";
					}
					dumpItem(item.content[i], out);
				}
				out += '}';
			}
		}

	} // namespace

	Sector parse(const std::string& text)
	{
		Sector sector;
		std::vector<std::string> lines;
		size_t start = 0;
		for(;;) {
			size_t nl = text.find('\n', start);
			if(nl == std::string::npos) {
				lines.push_back(text.substr(start));
				break;
			}
			lines.push_back(text.substr(start, nl - start));
			start = nl + 1;
		}

		size_t i = 0;
		while(i < lines.size() && !isTileLine(lines[i])) {
			sector.header.push_back(lines[i]);
			++i;
		}
		for(; i < lines.size(); ++i) {
			if(isTileLine(lines[i])) {
				if(!sector.trailer.empty()) {
					fail("tile line after trailer");
				}
				sector.tiles.push_back(parseTile(lines[i]));
			} else {
				sector.trailer.push_back(lines[i]);
			}
		}
		return sector;
	}

	std::string dump(const Sector& sector)
	{
		std::string out;
		bool first = true;
		for(size_t i = 0; i < sector.header.size(); ++i) {
			if(!first) {
				out += '\n';
			}
			out += sector.header[i];
			first = false;
		}
		for(size_t t = 0; t < sector.tiles.size(); ++t) {
			const Tile& tile = sector.tiles[t];
			if(!first) {
				out += '\n';
			}
			first = false;
			out += fromInt(tile.x);
			out += '-';
			out += fromInt(tile.y);
			out += ": ";
			if(tile.refresh) {
				out += "Refresh, ";
			}
			if(tile.protection_zone) {
				out += "ProtectionZone, ";
			}
			if(tile.no_logout) {
				out += "NoLogout, ";
			}
			if(tile.has_content) {
				out += "Content={";
				for(size_t i = 0; i < tile.content.size(); ++i) {
					if(i != 0) {
						out += ", ";
					}
					dumpItem(tile.content[i], out);
				}
				out += '}';
			}
		}
		for(size_t i = 0; i < sector.trailer.size(); ++i) {
			if(!first) {
				out += '\n';
			}
			out += sector.trailer[i];
			first = false;
		}
		return out;
	}

	bool parseFilename(const std::string& basename, int& sx, int& sy, int& sz)
	{
		// NNNN-NNNN-NN.sec
		if(basename.size() < 15) {
			return false;
		}
		if(basename.compare(basename.size() - 4, 4, ".sec") != 0) {
			return false;
		}
		std::string stem = basename.substr(0, basename.size() - 4);
		size_t d1 = stem.find('-');
		if(d1 == std::string::npos) {
			return false;
		}
		size_t d2 = stem.find('-', d1 + 1);
		if(d2 == std::string::npos) {
			return false;
		}
		for(size_t i = 0; i < stem.size(); ++i) {
			if(i == d1 || i == d2) {
				continue;
			}
			if(stem[i] < '0' || stem[i] > '9') {
				return false;
			}
		}
		sx = (int)std::strtol(stem.substr(0, d1).c_str(), nullptr, 10);
		sy = (int)std::strtol(stem.substr(d1 + 1, d2 - d1 - 1).c_str(), nullptr, 10);
		sz = (int)std::strtol(stem.substr(d2 + 1).c_str(), nullptr, 10);
		return true;
	}

	std::string makeFilename(int sx, int sy, int sz)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%04d-%04d-%02d.sec", sx, sy, sz);
		return std::string(buf);
	}

	bool toInt(const std::string& raw, long& out)
	{
		if(raw.empty() || raw[0] == '"') {
			return false;
		}
		char* end = nullptr;
		out = std::strtol(raw.c_str(), &end, 10);
		return end != nullptr && *end == '\0';
	}

	std::string fromInt(long value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%ld", value);
		return std::string(buf);
	}

	std::string unquote(const std::string& raw)
	{
		if(raw.size() < 2 || raw[0] != '"') {
			return raw;
		}
		std::string out;
		for(size_t i = 1; i + 1 < raw.size(); ++i) {
			if(raw[i] == '\\' && i + 2 < raw.size()) {
				char n = raw[i + 1];
				if(n == 'n') {
					out += '\n';
				} else if(n == 't') {
					out += '\t';
				} else {
					out += n;
				}
				++i;
				continue;
			}
			out += raw[i];
		}
		return out;
	}

	std::string quote(const std::string& text)
	{
		std::string out = "\"";
		for(size_t i = 0; i < text.size(); ++i) {
			char ch = text[i];
			if(ch == '\n') {
				out += "\\n";
			} else if(ch == '\t') {
				out += "\\t";
			} else if(ch == '"' || ch == '\\') {
				out += '\\';
				out += ch;
			} else {
				out += ch;
			}
		}
		out += '"';
		return out;
	}

} // namespace sec
