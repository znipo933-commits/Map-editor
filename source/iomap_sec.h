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
//
// CipSoft sector map support (RealOTS 7.7).
//
// A ".sec map" is a *directory* of NNNN-NNNN-NN.sec files rather than a
// single file, so the identifier passed in may be either the directory
// itself or any .sec file inside it.
//
// Item ids differ between the two worlds: .sec files store *client* ids
// (which equal the server's objects.srv TypeIDs), while everything inside
// RME is keyed by items.otb *server* ids. Translation therefore happens
// only at the file boundary - on load and on save - which is what lets
// you copy a selection out of an OTBM map and paste it straight into a
// CipSoft map with no further conversion.
//////////////////////////////////////////////////////////////////////

#ifndef RME_IOMAP_SEC_H_
#define RME_IOMAP_SEC_H_

#include "iomap.h"
#include "sec_format.h"

#include <map>
#include <string>

class Item;
class Position;

class IOMapSec : public IOMap
{
public:
	IOMapSec(MapVersion ver) { version = ver; }
	~IOMapSec() {}

	// True if this path is a .sec file, or a directory containing any.
	static bool isSecMap(const FileName& identifier);
	// The directory holding the sectors (the parent when given a file).
	static wxString sectorDirectory(const FileName& identifier);

	virtual bool loadMap(Map& map, const FileName& identifier);
	virtual bool saveMap(Map& map, const FileName& identifier);

	// Sectors are 32x32 tiles.
	static const int SECTOR_SIZE = 32;

protected:
	void buildIdMaps();
	// 0 means "no counterpart known".
	uint16_t toServerId(uint16_t client_id) const;
	uint16_t toClientId(uint16_t server_id) const;

	Item* createItem(const sec::Item& source, const Position& pos);
	bool writeItem(const Item* item, sec::Item& out);

	std::map<uint16_t, uint16_t> client_to_server;
	std::map<uint16_t, uint16_t> server_to_client;

	// Ids seen in the map that have no counterpart, reported once each.
	std::map<uint16_t, uint32_t> untranslated_client_ids;
	std::map<uint16_t, uint32_t> untranslated_server_ids;
};

#endif
