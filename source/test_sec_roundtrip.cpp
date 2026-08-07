//////////////////////////////////////////////////////////////////////
// Round-trip test for the .sec parser.
//
// Every sector in the given directory is parsed and re-serialised; the
// output must be byte-identical to the input. Any difference means a
// load/save cycle in the editor would silently alter the map.
//
//   test_sec_roundtrip <map-directory>
//
// This is the test that runs in CI before a binary is produced.
//////////////////////////////////////////////////////////////////////

#include "sec_format.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string readFile(const fs::path& p)
{
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// The editor parks an ENTIRE item (unknown client id) by serialising it as
// a one-element content list. That wrap -> text -> wrap cycle must be an
// identity, including exotic ids, attributes and nested contents.
static bool parkedItemRoundTrip()
{
	const char* cases[] = {
		"60123",
		"15001 Level=5 String=\"a b, c\"",
		"9999 Amount=64 Content={3031 Amount=100, 2853 Content={3031, 2917 Level=2}}",
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		std::vector<sec::Item> items;
		if(!sec::parseContent(cases[i], items) || items.size() != 1) {
			std::cout << "  PARKED-ITEM parse failed: " << cases[i] << "\n";
			return false;
		}
		if(sec::dumpContent(items) != cases[i]) {
			std::cout << "  PARKED-ITEM round-trip failed: " << cases[i]
			          << " -> " << sec::dumpContent(items) << "\n";
			return false;
		}
	}
	return true;
}

int main(int argc, char** argv)
{
	if(argc < 2) {
		std::cerr << "usage: test_sec_roundtrip <map-directory>\n";
		return 2;
	}

	if(!parkedItemRoundTrip()) {
		return 1;
	}

	std::vector<fs::path> files;
	for(const auto& entry : fs::directory_iterator(argv[1])) {
		if(entry.is_regular_file() && entry.path().extension() == ".sec") {
			files.push_back(entry.path());
		}
	}
	std::sort(files.begin(), files.end());

	size_t ok = 0, mismatched = 0, failed = 0, tiles = 0, items = 0;
	size_t bad_names = 0;
	size_t blob_failures = 0;

	for(size_t i = 0; i < files.size(); ++i) {
		const std::string raw = readFile(files[i]);
		try {
			sec::Sector sector = sec::parse(raw);

			int sx = 0, sy = 0, sz = 0;
			const std::string base = files[i].filename().string();
			if(!sec::parseFilename(base, sx, sy, sz) || sec::makeFilename(sx, sy, sz) != base) {
				++bad_names;
				if(bad_names <= 3) {
					std::cout << "  filename round-trip failed: " << base << "\n";
				}
			}

			tiles += sector.tiles.size();
			for(size_t t = 0; t < sector.tiles.size(); ++t) {
				items += sector.tiles[t].content.size();

				// dumpContent/parseContent round-trip - the pair the
				// editor uses to park container subtrees it cannot
				// represent. Text -> items -> text must be identity.
				const std::string blob = sec::dumpContent(sector.tiles[t].content);
				std::vector<sec::Item> back;
				if(!sec::parseContent(blob, back) || sec::dumpContent(back) != blob) {
					++blob_failures;
					if(blob_failures <= 3) {
						std::cout << "  CONTENT BLOB round-trip failed in " << base
						          << " tile " << sector.tiles[t].x << "-" << sector.tiles[t].y << "\n";
					}
				}
			}

			const std::string out = sec::dump(sector);
			if(out == raw) {
				++ok;
			} else {
				++mismatched;
				if(mismatched <= 3) {
					std::cout << "  MISMATCH: " << base
					          << " (in " << raw.size() << " bytes, out " << out.size() << ")\n";
					size_t n = std::min(raw.size(), out.size());
					for(size_t k = 0; k < n; ++k) {
						if(raw[k] != out[k]) {
							size_t from = k > 60 ? k - 60 : 0;
							std::cout << "    at " << k << "\n";
							std::cout << "    in : " << raw.substr(from, 100) << "\n";
							std::cout << "    out: " << out.substr(from, 100) << "\n";
							break;
						}
					}
				}
			}
		} catch(const std::exception& e) {
			++failed;
			if(failed <= 3) {
				std::cout << "  PARSE ERROR in " << files[i].filename().string()
				          << ": " << e.what() << "\n";
			}
		}
	}

	std::cout << "sectors: " << files.size()
	          << "  tiles: " << tiles
	          << "  top-level items: " << items << "\n";
	std::cout << "byte-exact: " << ok
	          << "   mismatched: " << mismatched
	          << "   parse errors: " << failed
	          << "   filename issues: " << bad_names
	          << "   content-blob failures: " << blob_failures << "\n";

	return (mismatched == 0 && failed == 0 && bad_names == 0 && blob_failures == 0) ? 0 : 1;
}
