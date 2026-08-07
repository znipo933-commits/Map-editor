//////////////////////////////////////////////////////////////////////
// CipSoft .sec sector format (RealOTS 7.7) - self-contained parser.
//
// Deliberately free of any RME/wxWidgets dependency so it can be unit
// tested on its own. The grammar was reverse engineered from a live
// RealOTS map and verified by byte-exact round-trip of all 9873 sectors:
//
//   <comment lines>
//   <x>-<y>: [Flag, ]* [Content={ <item> [, <item>]* }]
//
//   flag  := Refresh | ProtectionZone | NoLogout
//   item  := <id> [ <Attr>=<value>]* [ Content={ <item>, ... }]
//   value := integer | "quoted string"
//
// Attribute values are kept as raw text so that a load/save cycle which
// touches nothing reproduces the input byte for byte.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_FORMAT_H_
#define RME_SEC_FORMAT_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sec {

	struct Item
	{
		uint16_t id = 0;
		// (name, raw value) - value keeps its original spelling, quotes included
		std::vector<std::pair<std::string, std::string> > attrs;
		bool has_content = false;
		std::vector<Item> content;

		const std::string* attr(const std::string& name) const
		{
			for(size_t i = 0; i < attrs.size(); ++i) {
				if(attrs[i].first == name) {
					return &attrs[i].second;
				}
			}
			return nullptr;
		}
	};

	struct Tile
	{
		int x = 0;                 // 0..31 within the sector
		int y = 0;
		bool refresh = false;
		bool protection_zone = false;
		bool no_logout = false;
		bool has_content = false;
		std::vector<Item> content;
	};

	struct Sector
	{
		int sx = 0;                // sector coordinates, tiles = sx*32 etc
		int sy = 0;
		int sz = 0;
		std::vector<std::string> header;   // leading comment lines, verbatim
		std::vector<Tile> tiles;
		std::vector<std::string> trailer;  // anything after the tiles, verbatim
	};

	// Throws std::runtime_error on malformed input.
	Sector parse(const std::string& text);
	std::string dump(const Sector& sector);

	// Serialise a content list to the inner text of a Content={...} block
	// ("item, item, ...") and back. Used to park a container's contents
	// verbatim on an item RME cannot represent as a container, so they
	// survive a load/save cycle untouched. parseContent returns false on
	// malformed input instead of throwing.
	std::string dumpContent(const std::vector<Item>& items);
	bool parseContent(const std::string& text, std::vector<Item>& out);

	// "0996-0984-07.sec" -> 996 / 984 / 7. Returns false if it does not match.
	bool parseFilename(const std::string& basename, int& sx, int& sy, int& sz);
	std::string makeFilename(int sx, int sy, int sz);

	// Helpers for numeric attribute values.
	bool toInt(const std::string& raw, long& out);
	std::string fromInt(long value);

	// "quoted" <-> raw text, preserving the escapes the format uses.
	std::string unquote(const std::string& raw);
	std::string quote(const std::string& text);

} // namespace sec

#endif
