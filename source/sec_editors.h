//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Editors for the data that lives beside a .sec map: monster races,
// spawns, NPC homes and raids. See sec_data.h for the storage.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SEC_EDITORS_H_
#define RME_SEC_EDITORS_H_

#include "main.h"
#include "sec_data.h"

#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>

// Client item id -> RME server item id, and its name. .mon loot and corpse
// ids are objects.srv TypeIDs, which are client ids.
uint16_t secClientToServerId(int client_id);
wxString secItemLabel(int client_id);

// Draws one 32x32 sprite: either a creature outfit or an item by client id.
class SecSpritePanel : public wxPanel
{
public:
	SecSpritePanel(wxWindow* parent, const wxSize& size = wxSize(36, 36));

	void SetOutfit(const Outfit& outfit);
	void SetItem(int client_id);
	void Clear();

private:
	void OnPaint(wxPaintEvent& event);

	enum Mode { NOTHING, OUTFIT, ITEM };
	Mode mode = NOTHING;
	Outfit outfit;
	int item_client_id = 0;
};

// ---------------------------------------------------------------- sub dialogs

class SecSpellDialog : public wxDialog
{
public:
	SecSpellDialog(wxWindow* parent, secmon::Spell& spell);

private:
	void RebuildShapeParams();
	void RebuildImpactParams();
	void Store();

	secmon::Spell& spell;
	wxChoice* shape_choice;
	wxChoice* impact_choice;
	wxSpinCtrl* mana_ctrl;

	wxFlexGridSizer* shape_grid;
	wxFlexGridSizer* impact_grid;
	wxPanel* shape_panel;
	wxPanel* impact_panel;
	std::vector<wxSpinCtrl*> shape_ctrls;
	std::vector<wxSpinCtrl*> impact_ctrls;

	// Only shown for the Outfit impact, which takes a nested outfit tuple.
	wxPanel* outfit_panel;
	wxCheckBox* outfit_is_item;
	wxSpinCtrl* outfit_look;
	wxSpinCtrl* outfit_colors[4];
	wxSpinCtrl* outfit_item;
	SecSpritePanel* outfit_preview;
};

class SecSkillDialog : public wxDialog
{
public:
	SecSkillDialog(wxWindow* parent, secmon::Skill& skill);

private:
	secmon::Skill& skill;
	wxComboBox* name_ctrl;
	wxSpinCtrl* fields[6];
};

class SecLootDialog : public wxDialog
{
public:
	SecLootDialog(wxWindow* parent, secmon::Loot& loot);

private:
	void Refresh();

	secmon::Loot& loot;
	wxSpinCtrl* id_ctrl;
	wxSpinCtrl* amount_ctrl;
	wxSpinCtrl* chance_ctrl;
	wxStaticText* name_label;
	wxStaticText* percent_label;
	SecSpritePanel* preview;
};

class SecResistanceDialog : public wxDialog
{
public:
	SecResistanceDialog(wxWindow* parent, secmon::Resistance& resistance);

private:
	secmon::Resistance& resistance;
	wxComboBox* name_ctrl;
	wxSpinCtrl* percent_ctrl;
};

// ------------------------------------------------------------ monster editor

class SecMonsterEditorDialog : public wxDialog
{
public:
	SecMonsterEditorDialog(wxWindow* parent);

private:
	void BuildList();
	void LoadRace(int race);
	void StoreCurrent();
	void RefreshPreviews();
	void RefreshResistances();
	void RefreshSkills();
	void RefreshSpells();
	void RefreshLoot();
	void RefreshTalk();
	void UpdateWarnings();

	secmon::MonsterType* current();

	int current_race = -1;
	std::vector<int> shown;      // race numbers, filtered

	wxTextCtrl* filter_ctrl;
	wxListBox* race_list;
	wxStaticText* warning_label;

	// basic
	wxSpinCtrl* race_ctrl;
	wxTextCtrl* name_ctrl;
	wxTextCtrl* article_ctrl;
	wxSpinCtrl* look_ctrl;
	wxSpinCtrl* color_ctrl[4];
	wxCheckBox* outfit_item_check;
	wxSpinCtrl* outfit_item_ctrl;
	SecSpritePanel* outfit_preview;
	wxSpinCtrl* corpse_ctrl;
	wxSpinCtrl* corpse2_ctrl;
	wxCheckBox* corpse_plural_check;
	SecSpritePanel* corpse_preview;
	wxChoice* blood_ctrl;
	wxSpinCtrl* hp_ctrl;
	wxSpinCtrl* exp_ctrl;
	wxSpinCtrl* attack_ctrl;
	wxSpinCtrl* defend_ctrl;
	wxSpinCtrl* armor_ctrl;
	wxSpinCtrl* poison_ctrl;
	wxSpinCtrl* summon_ctrl;
	wxSpinCtrl* flee_ctrl;
	wxSpinCtrl* lose_ctrl;
	wxSpinCtrl* strategy_ctrl[4];

	wxCheckBox* flag_check[14];

	wxListCtrl* resist_list;
	wxListCtrl* skill_list;
	wxListCtrl* spell_list;
	wxListCtrl* loot_list;
	wxListBox* talk_list;
};

// ------------------------------------------------------------------- spawns

// One row of monster.db: which race stands where, how many, how far it may
// roam and how fast it comes back.
class SecSpawnRowDialog : public wxDialog
{
public:
	SecSpawnRowDialog(wxWindow* parent, secmon::SpawnRow& row, bool allow_position);

private:
	void UpdateNotes();

	secmon::SpawnRow& row;
	wxComboBox* race_ctrl;
	wxSpinCtrl* x_ctrl;
	wxSpinCtrl* y_ctrl;
	wxSpinCtrl* z_ctrl;
	wxSpinCtrl* amount_ctrl;
	wxSpinCtrl* radius_ctrl;
	wxSpinCtrl* regen_ctrl;
	wxStaticText* note_label;
	SecSpritePanel* preview;
};

// Every spawn row in the world, filterable.
class SecSpawnBrowserDialog : public wxDialog
{
public:
	SecSpawnBrowserDialog(wxWindow* parent);

private:
	void Rebuild();
	void OnGoto();
	int selectedRow() const;

	wxTextCtrl* filter_ctrl;
	wxListCtrl* list;
	wxStaticText* count_label;
	std::vector<size_t> shown;    // indices into SecData::db.rows
};

// The spawns standing on one tile - a tile may carry several races.
class SecTileSpawnDialog : public wxDialog
{
public:
	SecTileSpawnDialog(wxWindow* parent, const Position& pos);

private:
	void Rebuild();

	Position pos;
	wxListCtrl* list;
	std::vector<size_t> shown;
};

// ------------------------------------------------------------- npcs / raids

class SecNpcEditorDialog : public wxDialog
{
public:
	SecNpcEditorDialog(wxWindow* parent);

private:
	void BuildList();
	void Load(int index);
	void Store();
	void RefreshPreview();
	SecNpc* current();

	int current_npc = -1;
	std::vector<size_t> shown;

	wxTextCtrl* filter_ctrl;
	wxListBox* npc_list;

	wxTextCtrl* name_ctrl;
	wxChoice* sex_ctrl;
	wxSpinCtrl* race_ctrl;
	wxSpinCtrl* look_ctrl;
	wxSpinCtrl* color_ctrl[4];
	wxCheckBox* item_check;
	wxSpinCtrl* item_ctrl;
	SecSpritePanel* preview;
	wxSpinCtrl* x_ctrl;
	wxSpinCtrl* y_ctrl;
	wxSpinCtrl* z_ctrl;
	wxSpinCtrl* radius_ctrl;
	wxSpinCtrl* speed_ctrl;
	wxTextCtrl* behaviour_ctrl;
	wxStaticText* info_label;
};

// One wave of a raid.
class SecRaidWaveDialog : public wxDialog
{
public:
	SecRaidWaveDialog(wxWindow* parent, SecRaidPoint& wave);

private:
	SecRaidPoint& wave;
	wxSpinCtrl* delay_ctrl;
	wxComboBox* race_ctrl;
	wxSpinCtrl* x_ctrl;
	wxSpinCtrl* y_ctrl;
	wxSpinCtrl* z_ctrl;
	wxSpinCtrl* spread_ctrl;
	wxSpinCtrl* min_ctrl;
	wxSpinCtrl* max_ctrl;
	wxSpinCtrl* lifetime_ctrl;
	wxTextCtrl* message_ctrl;
	SecSpritePanel* preview;
};

class SecRaidEditorDialog : public wxDialog
{
public:
	SecRaidEditorDialog(wxWindow* parent);

private:
	void BuildRaidList();
	void LoadRaid(int index);
	void StoreHeader();
	void RefreshWaves();
	SecRaid* current();

	int current_raid = -1;

	wxListBox* raid_list;
	wxChoice* type_ctrl;
	wxSpinCtrl* interval_ctrl;
	wxStaticText* interval_label;
	wxListCtrl* wave_list;
};

#endif
