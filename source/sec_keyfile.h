//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// A line-oriented "Key = value" file, kept byte for byte.
//
// The CipSoft .npc and .evt files are both of this shape. Editing goes
// through setValue(), which rewrites one line and leaves every other byte
// alone, so a load/save with no edits reproduces the file exactly and a
// field this code does not understand cannot be lost.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_KEYFILE_H_
#define RME_SEC_KEYFILE_H_

#include <string>
#include <vector>

namespace seckv {

struct Line {
	std::string raw;      // the original bytes, including the newline
	bool isField = false;
	std::string key;      // "Position"
	std::string value;    // "[32103,32192,5]" - trimmed, no key or '='
};

class KeyFile {
public:
	bool load(const std::string& path);
	void loadFromString(const std::string& text);
	std::string serialize() const;
	bool save(const std::string& path, std::string* error = nullptr) const;
	bool isDirty() const { return serialize() != originalText; }

	std::vector<Line> lines;
	std::string originalText;
	std::string sourcePath;

	// -1 when absent. Searches from `from` onwards.
	int find(const std::string& key, int from = 0) const;
	// Rewrites that one line, keeping the file's own column alignment.
	void setValue(int index, const std::string& value);
	// value of the first `key` at or after `from`, or the fallback.
	std::string get(const std::string& key, int from = 0,
	                const std::string& fallback = std::string()) const;

	void insert(int at, const std::vector<std::string>& raw_lines);
	void erase(int from, int count);

	// Builds "Key<pad>= value\n" using the alignment this file already uses.
	std::string makeLine(const std::string& key, const std::string& value) const;

private:
	// Column the '=' sits in, learned from the file. 10 in .npc, 10 in .evt.
	int alignment() const;
};

} // namespace seckv

#endif
