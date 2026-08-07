//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "iomap_sec.h"

#include "complexitem.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "tile.h"

#include <wx/dir.h>
#include <wx/filename.h>

#include <fstream>
#include <sstream>

namespace {

	// .sec attributes that map onto a first-class RME concept. Everything
	// else is round-tripped verbatim through the item attribute map, so
	// quest/door/key data survives an edit even though RME has no UI for it.
	const char* ATTR_AMOUNT = "Amount";
	const char* ATTR_STRING = "String";
	const char* ATTR_POOL_LIQUID = "PoolLiquidType";
	const char* ATTR_CONTAINER_LIQUID = "ContainerLiquidType";

	// Prefix used when parking an unmapped .sec attribute on an item.
	const char* SEC_ATTR_PREFIX = "sec.";

	// Key (behind the prefix) holding a container subtree parked verbatim
	// on an item whose id is not a container in the loaded item database.
	// Double underscore so it can never collide with a real .sec attribute
	// name, which the grammar restricts to [A-Za-z][A-Za-z0-9]*.
	const char* ATTR_PARKED_CONTENT = "__Content";

	// Key holding an ENTIRE item (id, attributes, contents) parked verbatim
	// because its client id has no items.otb counterpart at all. The item
	// is represented in the editor by a placeholder that renders as an
	// unknown item; on save the parked text is written back untouched, so
	// ids the loaded database has never heard of survive a load/save cycle.
	const char* ATTR_PARKED_RAW = "__Raw";

	// Server id given to those placeholders. Far above any items.otb in
	// existence, so getItemType() returns the dummy type (bounds-checked)
	// and Item::Create() builds a plain, inert Item for it.
	const uint16_t PARKED_PLACEHOLDER_ID = 60000;

	std::string readWholeFile(const wxString& path)
	{
		std::ifstream in(std::string(path.mb_str()), std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	bool writeWholeFile(const wxString& path, const std::string& data)
	{
		std::ofstream out(std::string(path.mb_str()), std::ios::binary | std::ios::trunc);
		if(!out) {
			return false;
		}
		out.write(data.data(), (std::streamsize)data.size());
		return out.good();
	}

} // namespace

bool IOMapSec::isSecMap(const FileName& identifier)
{
	if(identifier.GetExt().Lower() == "sec") {
		return true;
	}
	if(wxDirExists(identifier.GetFullPath())) {
		wxString found = wxFindFirstFile(identifier.GetFullPath() + wxFileName::GetPathSeparator() + "*.sec", wxFILE);
		return !found.empty();
	}
	return false;
}

wxString IOMapSec::sectorDirectory(const FileName& identifier)
{
	if(wxDirExists(identifier.GetFullPath())) {
		return identifier.GetFullPath();
	}
	return identifier.GetPath();
}

void IOMapSec::buildIdMaps()
{
	client_to_server.clear();
	server_to_client.clear();

	const uint16_t max_id = g_items.getMaxID();
	for(uint16_t id = 100; id <= max_id; ++id) {
		const ItemType& type = g_items.getItemType(id);
		if(type.id == 0 || type.clientID == 0) {
			continue;
		}
		server_to_client[type.id] = type.clientID;
		// Several server ids can share a client id; the lowest wins so the
		// choice is deterministic between runs.
		if(client_to_server.find(type.clientID) == client_to_server.end()) {
			client_to_server[type.clientID] = type.id;
		}
	}
}

uint16_t IOMapSec::toServerId(uint16_t client_id) const
{
	std::map<uint16_t, uint16_t>::const_iterator it = client_to_server.find(client_id);
	return it == client_to_server.end() ? 0 : it->second;
}

uint16_t IOMapSec::toClientId(uint16_t server_id) const
{
	std::map<uint16_t, uint16_t>::const_iterator it = server_to_client.find(server_id);
	return it == server_to_client.end() ? 0 : it->second;
}

Item* IOMapSec::createItem(const sec::Item& source, const Position& pos)
{
	const uint16_t server_id = toServerId(source.id);
	if(server_id == 0) {
		// No items.otb counterpart. Do NOT drop the item: park it whole
		// (id, attributes, contents - client ids and all) on a placeholder
		// so the save path can restore it verbatim. The placeholder can be
		// moved or deleted in the editor like any item; editing its
		// properties has no effect, the parked text always wins.
		++untranslated_client_ids[source.id];
		Item* placeholder = Item::Create(PARKED_PLACEHOLDER_ID);
		if(placeholder == nullptr) {
			return nullptr;
		}
		std::vector<sec::Item> wrap(1, source);
		placeholder->setAttribute(std::string(SEC_ATTR_PREFIX) + ATTR_PARKED_RAW,
		                          sec::dumpContent(wrap));
		return placeholder;
	}

	Item* item = Item::Create(server_id);
	if(item == nullptr) {
		return nullptr;
	}

	for(size_t i = 0; i < source.attrs.size(); ++i) {
		const std::string& name = source.attrs[i].first;
		const std::string& raw = source.attrs[i].second;

		if(name == ATTR_AMOUNT) {
			long value = 0;
			if(sec::toInt(raw, value)) {
				item->setSubtype((uint16_t)value);
			}
			continue;
		}
		if(name == ATTR_STRING) {
			item->setText(sec::unquote(raw));
			continue;
		}
		if(name == ATTR_POOL_LIQUID || name == ATTR_CONTAINER_LIQUID) {
			// Set the subtype so the editor renders a liquid, but ALSO
			// fall through and park the raw value: CipSoft's liquid
			// numbering is not RME's, and Item::getCount() returns a
			// constant 1 for non-stackables, so the only way to write
			// the correct value back is to keep the original.
			long value = 0;
			if(sec::toInt(raw, value)) {
				item->setSubtype((uint16_t)value);
			}
		}

		// No RME equivalent: keep it verbatim so saving restores it. The
		// prefix keeps these out of the way of RME's own attribute names.
		const std::string key = std::string(SEC_ATTR_PREFIX) + name;
		long value = 0;
		if(sec::toInt(raw, value)) {
			item->setAttribute(key, (int32_t)value);
		} else {
			item->setAttribute(key, sec::unquote(raw));
		}
	}

	if(source.has_content) {
		Container* container = dynamic_cast<Container*>(item);
		if(container) {
			for(size_t i = 0; i < source.content.size(); ++i) {
				Item* child = createItem(source.content[i], pos);
				if(child) {
					container->getVector().push_back(child);
				}
			}
		} else {
			// This client id is not a container in the loaded item
			// database (quest chests are the common case: 7.7 and 8.6
			// client ids below 5098 name different items), so RME has
			// nowhere to hang the children. Park the whole subtree
			// verbatim - client ids and all - so saving restores it
			// untouched instead of silently emptying the container.
			item->setAttribute(std::string(SEC_ATTR_PREFIX) + ATTR_PARKED_CONTENT,
			                   sec::dumpContent(source.content));
		}
	}

	return item;
}

bool IOMapSec::loadMap(Map& map, const FileName& identifier)
{
	buildIdMaps();
	if(client_to_server.empty()) {
		error("No item database loaded - cannot translate CipSoft item ids.");
		return false;
	}

	const wxString dir = sectorDirectory(identifier);
	wxArrayString files;
	wxDir::GetAllFiles(dir, &files, "*.sec", wxDIR_FILES);
	if(files.IsEmpty()) {
		error("No .sec files found in %s", dir.c_str());
		return false;
	}
	files.Sort();

	int max_x = 0, max_y = 0;
	uint32_t tile_count = 0;

	for(size_t f = 0; f < files.GetCount(); ++f) {
		const wxFileName fn(files[f]);
		const std::string base = std::string(fn.GetFullName().mb_str());

		int sx = 0, sy = 0, sz = 0;
		if(!sec::parseFilename(base, sx, sy, sz)) {
			warning("Skipping '%s': not a sector filename", base.c_str());
			continue;
		}

		sec::Sector sector;
		try {
			sector = sec::parse(readWholeFile(files[f]));
		} catch(const std::exception& e) {
			error("Failed to parse %s: %s", base.c_str(), e.what());
			return false;
		}

		for(size_t t = 0; t < sector.tiles.size(); ++t) {
			const sec::Tile& src = sector.tiles[t];
			const Position pos(sx * SECTOR_SIZE + src.x, sy * SECTOR_SIZE + src.y, sz);

			if(map.getTile(pos)) {
				warning("Duplicate tile at %d:%d:%d, keeping the first", pos.x, pos.y, pos.z);
				continue;
			}

			Tile* tile = map.allocator(map.createTileL(pos));

			uint16_t flags = 0;
			if(src.protection_zone) {
				flags |= TILESTATE_PROTECTIONZONE;
			}
			if(src.no_logout) {
				flags |= TILESTATE_NOLOGOUT;
			}
			if(src.refresh) {
				flags |= TILESTATE_REFRESH;
			}
			tile->setMapFlags(flags);

			// Insert in file order. Tile::addItem re-sorts border items
			// by the item database's top order, which regularly differs
			// from the order CipSoft stored (25k+ tiles in the RealOTS
			// map) and must be preserved for a faithful round-trip. The
			// ground slot is still honoured so brushes and rendering
			// behave; everything else keeps its position.
			for(size_t i = 0; i < src.content.size(); ++i) {
				Item* item = createItem(src.content[i], pos);
				if(item == nullptr) {
					continue;
				}
				if(item->isGroundTile() && tile->ground == nullptr) {
					tile->ground = item;
				} else {
					tile->items.push_back(item);
				}
			}

			tile->update();
			map.setTile(pos.x, pos.y, pos.z, tile);

			if(pos.x > max_x) {
				max_x = pos.x;
			}
			if(pos.y > max_y) {
				max_y = pos.y;
			}
			++tile_count;
		}
	}

	map.setWidth(std::min(65000, max_x + SECTOR_SIZE));
	map.setHeight(std::min(65000, max_y + SECTOR_SIZE));

	for(std::map<uint16_t, uint32_t>::const_iterator it = untranslated_client_ids.begin();
	    it != untranslated_client_ids.end(); ++it) {
		warning("Client item id %d has no items.otb counterpart, %u occurrence(s) parked "
		        "(preserved on save, but shown as an unknown item in the editor)",
		        (int)it->first, it->second);
	}

	return true;
}

bool IOMapSec::writeItem(const Item* item, sec::Item& out)
{
	ItemAttributeMap attributes = item->getAttributes();

	// An item parked whole at load time (client id unknown to items.otb):
	// emit the original text verbatim and ignore the placeholder entirely.
	{
		ItemAttributeMap::const_iterator raw = attributes.find(std::string(SEC_ATTR_PREFIX) + ATTR_PARKED_RAW);
		if(raw != attributes.end()) {
			if(const std::string* blob = raw->second.getString()) {
				std::vector<sec::Item> parsed;
				if(sec::parseContent(*blob, parsed) && parsed.size() == 1) {
					out = parsed[0];
					return true;
				}
			}
			warning("Could not restore a parked item, it will be missing from the saved map");
			return false;
		}
	}

	const uint16_t client_id = toClientId(item->getID());
	if(client_id == 0) {
		++untranslated_server_ids[item->getID()];
		return false;
	}
	out.id = client_id;

	const ItemType& type = g_items.getItemType(item->getID());
	const std::string pool_key = std::string(SEC_ATTR_PREFIX) + ATTR_POOL_LIQUID;
	const std::string cont_key = std::string(SEC_ATTR_PREFIX) + ATTR_CONTAINER_LIQUID;
	const std::string parked_content_key = std::string(SEC_ATTR_PREFIX) + ATTR_PARKED_CONTENT;

	if(type.stackable) {
		out.attrs.push_back(std::make_pair(std::string(ATTR_AMOUNT), sec::fromInt(item->getCount())));
	} else if(type.isSplash()) {
		// Loaded items carry their original value parked (CipSoft liquid
		// numbering differs from RME's) - the parked loop below writes it
		// back verbatim. Only items newly placed in the editor reach this
		// branch. NB: getSubtype(), never getCount() - getCount() is a
		// constant 1 for anything non-stackable.
		if(attributes.find(pool_key) == attributes.end() && item->getSubtype() != 0) {
			out.attrs.push_back(std::make_pair(std::string(ATTR_POOL_LIQUID), sec::fromInt(item->getSubtype())));
		}
	} else if(type.isFluidContainer()) {
		if(attributes.find(cont_key) == attributes.end() && item->getSubtype() != 0) {
			out.attrs.push_back(std::make_pair(std::string(ATTR_CONTAINER_LIQUID), sec::fromInt(item->getSubtype())));
		}
	}

	const std::string text = item->getText();
	if(!text.empty()) {
		out.attrs.push_back(std::make_pair(std::string(ATTR_STRING), sec::quote(text)));
	}

	// Restore the attributes we parked on load.
	for(ItemAttributeMap::const_iterator it = attributes.begin(); it != attributes.end(); ++it) {
		const std::string& key = it->first;
		if(key.compare(0, strlen(SEC_ATTR_PREFIX), SEC_ATTR_PREFIX) != 0) {
			continue;
		}
		if(key == parked_content_key) {
			continue; // not an attribute - restored as contents below
		}
		const std::string name = key.substr(strlen(SEC_ATTR_PREFIX));
		const ItemAttribute& attr = it->second;
		if(const int32_t* number = attr.getInteger()) {
			out.attrs.push_back(std::make_pair(name, sec::fromInt(*number)));
		} else if(const std::string* str = attr.getString()) {
			out.attrs.push_back(std::make_pair(name, sec::quote(*str)));
		}
	}

	const Container* container = dynamic_cast<const Container*>(item);
	if(container) {
		const ItemVector& contents = const_cast<Container*>(container)->getVector();
		if(!contents.empty()) {
			out.has_content = true;
			for(size_t i = 0; i < contents.size(); ++i) {
				sec::Item child;
				if(writeItem(contents[i], child)) {
					out.content.push_back(child);
				}
			}
		}
	}

	if(out.content.empty()) {
		// Contents parked at load time because this id is not a container
		// in the item database: restore the subtree verbatim. The parked
		// text already holds client ids, so no translation.
		ItemAttributeMap::const_iterator parked = attributes.find(parked_content_key);
		if(parked != attributes.end()) {
			if(const std::string* blob = parked->second.getString()) {
				if(sec::parseContent(*blob, out.content)) {
					out.has_content = true;
				} else {
					warning("Could not restore parked contents of item %d", (int)client_id);
				}
			}
		}
	}

	return true;
}

bool IOMapSec::saveMap(Map& map, const FileName& identifier)
{
	buildIdMaps();

	const wxString dir = sectorDirectory(identifier);
	if(!wxDirExists(dir)) {
		error("Sector directory does not exist: %s", dir.c_str());
		return false;
	}

	// Bucket every non-empty tile into its sector.
	typedef std::map<std::string, sec::Sector> SectorMap;
	SectorMap sectors;

	// A sector is written only if it contains a tile the user modified
	// (or its file does not exist yet). Untouched sectors keep their
	// original bytes, which confines any load/save infidelity to the
	// sectors actually edited.
	std::map<std::string, bool> sector_dirty;

	MapIterator it = map.begin();
	while(it != map.end()) {
		Tile* tile = (*it)->get();
		// NB: flags-only tiles (e.g. a bare Refresh) have size() == 0
		// but must still be written.
		if(!tile || (tile->size() == 0 && tile->getMapFlags() == 0)) {
			++it;
			continue;
		}
		const Position& pos = tile->getPosition();
		const int sx = pos.x / SECTOR_SIZE;
		const int sy = pos.y / SECTOR_SIZE;
		const int sz = pos.z;

		const std::string name = sec::makeFilename(sx, sy, sz);
		SectorMap::iterator found = sectors.find(name);
		if(found == sectors.end()) {
			sec::Sector fresh;
			fresh.sx = sx;
			fresh.sy = sy;
			fresh.sz = sz;
			fresh.header.push_back("# Tibia - graphical Multi-User-Dungeon");
			fresh.header.push_back("# Data for sector " + sec::fromInt(sx) + "/" +
			                       sec::fromInt(sy) + "/" + sec::fromInt(sz));
			fresh.header.push_back("");
			fresh.trailer.push_back("");
			found = sectors.insert(std::make_pair(name, fresh)).first;
		}

		sec::Tile out;
		out.x = pos.x % SECTOR_SIZE;
		out.y = pos.y % SECTOR_SIZE;
		const uint16_t flags = tile->getMapFlags();
		out.refresh = (flags & TILESTATE_REFRESH) != 0;
		out.protection_zone = (flags & TILESTATE_PROTECTIONZONE) != 0;
		out.no_logout = (flags & TILESTATE_NOLOGOUT) != 0;

		if(tile->ground) {
			sec::Item ground;
			if(writeItem(tile->ground, ground)) {
				out.content.push_back(ground);
			}
		}
		for(ItemVector::const_iterator item = tile->items.begin(); item != tile->items.end(); ++item) {
			sec::Item entry;
			if(writeItem(*item, entry)) {
				out.content.push_back(entry);
			}
		}
		out.has_content = !out.content.empty();

		found->second.tiles.push_back(out);
		sector_dirty[name] = sector_dirty[name] || tile->isModified();
		++it;
	}

	// Tiles are stored in x-then-y order, matching the original files.
	size_t written = 0, kept = 0;
	for(SectorMap::iterator sector = sectors.begin(); sector != sectors.end(); ++sector) {
		const wxString path = dir + wxFileName::GetPathSeparator() + wxString(sector->first.c_str(), wxConvUTF8);

		if(!sector_dirty[sector->first] && wxFileName::FileExists(path)) {
			++kept;
			continue;
		}

		std::vector<sec::Tile>& tiles = sector->second.tiles;
		std::sort(tiles.begin(), tiles.end(), [](const sec::Tile& a, const sec::Tile& b) {
			return a.x != b.x ? a.x < b.x : a.y < b.y;
		});

		if(!writeWholeFile(path, sec::dump(sector->second))) {
			error("Could not write %s", sector->first.c_str());
			return false;
		}
		++written;
	}
	warning("Wrote %u modified sector(s), left %u untouched sector(s) as-is",
	        (unsigned)written, (unsigned)kept);

	for(std::map<uint16_t, uint32_t>::const_iterator bad = untranslated_server_ids.begin();
	    bad != untranslated_server_ids.end(); ++bad) {
		warning("Server item id %d has no client id, %u occurrence(s) not written",
		        (int)bad->first, bad->second);
	}

	return true;
}
