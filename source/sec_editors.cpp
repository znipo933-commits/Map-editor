//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "sec_editors.h"
#include "gui.h"
#include "graphics.h"
#include "items.h"
#include "map_display.h"
#include "map_tab.h"

#include <wx/arrstr.h>

#include <wx/statline.h>

#include <algorithm>
#include <map>

// ============================================================================
// helpers

uint16_t secClientToServerId(int client_id)
{
	// Built once; several server ids can share a client id, lowest wins so the
	// choice is stable between runs (same rule as IOMapSec::buildIdMaps).
	static std::map<int, uint16_t> lookup;
	static bool built = false;
	if(!built) {
		const uint16_t max_id = g_items.getMaxID();
		for(uint16_t id = 100; id <= max_id; ++id) {
			const ItemType& type = g_items.getItemType(id);
			if(type.id == 0 || type.clientID == 0) continue;
			if(lookup.find(type.clientID) == lookup.end()) lookup[type.clientID] = type.id;
		}
		built = true;
	}
	std::map<int, uint16_t>::const_iterator it = lookup.find(client_id);
	return it == lookup.end() ? 0 : it->second;
}

wxString secItemLabel(int client_id)
{
	if(client_id <= 0) return "(none)";
	const uint16_t server_id = secClientToServerId(client_id);
	if(server_id == 0) return wxString::Format("%d (not in items.otb)", client_id);
	const ItemType& type = g_items.getItemType(server_id);
	if(type.name.empty()) return wxString::Format("%d", client_id);
	return wxString::Format("%d  %s", client_id, wxstr(type.name));
}

namespace {

wxSpinCtrl* addSpin(wxWindow* parent, wxSizer* grid, const wxString& label,
                    int value, int lo = 0, int hi = 1000000, int width = 80)
{
	grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxSpinCtrl* ctrl = new wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                                  wxSize(width, -1), wxSP_ARROW_KEYS, lo, hi, value);
	grid->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL);
	return ctrl;
}

// Parameter names come from the engine's own shape and impact signatures.
const char* const* shapeParamNames(secmon::SpellShape shape, int& count)
{
	static const char* actor[]       = {"Effect"};
	static const char* victim[]      = {"Range", "Animation", "Effect"};
	static const char* origin[]      = {"Radius", "Effect"};
	static const char* destination[] = {"Range", "Animation", "Radius", "Effect"};
	static const char* angle[]       = {"Angle", "Range", "Effect"};
	switch(shape) {
		case secmon::SHAPE_ACTOR:       count = 1; return actor;
		case secmon::SHAPE_VICTIM:      count = 3; return victim;
		case secmon::SHAPE_ORIGIN:      count = 2; return origin;
		case secmon::SHAPE_DESTINATION: count = 4; return destination;
		case secmon::SHAPE_ANGLE:       count = 3; return angle;
	}
	count = 0;
	return actor;
}

const char* const* impactParamNames(secmon::SpellImpact impact, int& count)
{
	static const char* damage[]   = {"Damage type", "Power", "Variation"};
	static const char* field[]    = {"Field type"};
	static const char* healing[]  = {"Power", "Variation"};
	static const char* speed[]    = {"Speed change", "Duration", "Variation"};
	static const char* drunken[]  = {"Strength", "Duration", "Variation"};
	static const char* strength[] = {"Skill", "Percent", "Variation", "Duration"};
	static const char* outfit[]   = {"Duration"};
	static const char* summon[]   = {"Race", "Max count"};
	switch(impact) {
		case secmon::IMPACT_DAMAGE:   count = 3; return damage;
		case secmon::IMPACT_FIELD:    count = 1; return field;
		case secmon::IMPACT_HEALING:  count = 2; return healing;
		case secmon::IMPACT_SPEED:    count = 3; return speed;
		case secmon::IMPACT_DRUNKEN:  count = 3; return drunken;
		case secmon::IMPACT_STRENGTH: count = 4; return strength;
		case secmon::IMPACT_OUTFIT:   count = 1; return outfit;
		case secmon::IMPACT_SUMMON:   count = 2; return summon;
	}
	count = 0;
	return damage;
}

wxString spellText(const secmon::Spell& sp)
{
	wxString t = secmon::MonFile::shapeName(sp.shape);
	t << " (";
	for(size_t i = 0; i < sp.shapeParams.size(); ++i) {
		if(i) t << ", ";
		t << sp.shapeParams[i];
	}
	t << ") -> " << secmon::MonFile::impactName(sp.impact) << " (";
	bool first = true;
	if(sp.impactHasOutfit) {
		t << "(" << sp.outfitLook << ", ";
		if(sp.outfitIsItem) t << sp.outfitItemType;
		else t << sp.outfitColors[0] << "-" << sp.outfitColors[1] << "-"
		       << sp.outfitColors[2] << "-" << sp.outfitColors[3];
		t << ")";
		first = false;
	}
	for(size_t i = 0; i < sp.impactParams.size(); ++i) {
		if(!first) t << ", ";
		t << sp.impactParams[i];
		first = false;
	}
	t << ")";
	return t;
}

const char* const kBloodNames[] = {"Blood", "Slime", "Bones", "Fire", "Energy"};

const char* const kFlagLabels[14] = {
	"Kick boxes", "Kick creatures", "See invisible", "Unpushable",
	"Distance fighting", "No summon", "No illusion", "No convince",
	"No burning", "No poison", "No energy", "No hit",
	"No life drain", "No paralyze",
};
const uint32_t kFlagBits[14] = {
	secmon::FLAG_KICK_BOXES, secmon::FLAG_KICK_CREATURES, secmon::FLAG_SEE_INVISIBLE,
	secmon::FLAG_UNPUSHABLE, secmon::FLAG_DISTANCE_FIGHTING, secmon::FLAG_NO_SUMMON,
	secmon::FLAG_NO_ILLUSION, secmon::FLAG_NO_CONVINCE, secmon::FLAG_NO_BURNING,
	secmon::FLAG_NO_POISON, secmon::FLAG_NO_ENERGY, secmon::FLAG_NO_HIT,
	secmon::FLAG_NO_LIFE_DRAIN, secmon::FLAG_NO_PARALYZE,
};

const char* const kSkillNames[] = {
	"HitPoints", "GoStrength", "CarryStrength", "FistFighting", "SwordFighting",
	"ClubFighting", "AxeFighting", "DistanceFighting", "Shielding", "Fishing",
	"MagicLevel", "Level", "Mana", "SoulPoints", "Eating",
};

} // namespace

// ============================================================================
// SecSpritePanel

SecSpritePanel::SecSpritePanel(wxWindow* parent, const wxSize& size)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, size, wxBORDER_SUNKEN)
{
	SetBackgroundColour(*wxWHITE);
	Bind(wxEVT_PAINT, &SecSpritePanel::OnPaint, this);
}

void SecSpritePanel::SetOutfit(const Outfit& o) { mode = OUTFIT; outfit = o; Refresh(); }
void SecSpritePanel::SetItem(int client_id) { mode = ITEM; item_client_id = client_id; Refresh(); }
void SecSpritePanel::Clear() { mode = NOTHING; Refresh(); }

void SecSpritePanel::OnPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();

	const wxSize sz = GetClientSize();
	const wxRect rect(0, 0, sz.GetWidth(), sz.GetHeight());

	if(mode == OUTFIT) {
		// An outfit with lookType 0 is an item disguise, drawn as that item.
		if(outfit.lookType == 0 && outfit.lookItem != 0) {
			Sprite* spr = g_gui.gfx.getSprite(secClientToServerId(outfit.lookItem));
			if(spr) spr->DrawTo(&dc, SPRITE_SIZE_32x32, 2, 2, 32, 32);
			return;
		}
		GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
		if(spr) spr->DrawTo(&dc, rect, outfit);
		else dc.DrawText("?", 12, 8);
	} else if(mode == ITEM) {
		const uint16_t server_id = secClientToServerId(item_client_id);
		Sprite* spr = server_id ? g_gui.gfx.getSprite(server_id) : nullptr;
		if(spr) spr->DrawTo(&dc, SPRITE_SIZE_32x32, 2, 2, 32, 32);
		else dc.DrawText("?", 12, 8);
	}
}

// ============================================================================
// SecSpellDialog

SecSpellDialog::SecSpellDialog(wxWindow* parent, secmon::Spell& sp)
	: wxDialog(parent, wxID_ANY, "Spell", wxDefaultPosition, wxDefaultSize,
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  spell(sp)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* head = new wxFlexGridSizer(4, 4, 4);
	head->Add(new wxStaticText(this, wxID_ANY, "Shape"), 0, wxALIGN_CENTER_VERTICAL);
	shape_choice = new wxChoice(this, wxID_ANY);
	for(int i = 0; i < 5; ++i)
		shape_choice->Append(secmon::MonFile::shapeName((secmon::SpellShape)i));
	shape_choice->SetSelection((int)spell.shape);
	head->Add(shape_choice, 0, wxRIGHT, 12);

	head->Add(new wxStaticText(this, wxID_ANY, "Impact"), 0, wxALIGN_CENTER_VERTICAL);
	impact_choice = new wxChoice(this, wxID_ANY);
	for(int i = 0; i < 8; ++i)
		impact_choice->Append(secmon::MonFile::impactName((secmon::SpellImpact)i));
	impact_choice->SetSelection((int)spell.impact);
	head->Add(impact_choice);
	top->Add(head, 0, wxALL, 8);

	shape_panel = new wxPanel(this);
	shape_grid = new wxFlexGridSizer(2, 4, 6);
	shape_panel->SetSizer(shape_grid);
	wxStaticBoxSizer* shape_box = new wxStaticBoxSizer(wxVERTICAL, this, "Shape parameters");
	shape_box->Add(shape_panel, 1, wxEXPAND | wxALL, 4);
	top->Add(shape_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

	impact_panel = new wxPanel(this);
	impact_grid = new wxFlexGridSizer(2, 4, 6);
	impact_panel->SetSizer(impact_grid);
	wxStaticBoxSizer* impact_box = new wxStaticBoxSizer(wxVERTICAL, this, "Impact parameters");
	impact_box->Add(impact_panel, 1, wxEXPAND | wxALL, 4);

	// The Outfit impact takes a nested outfit tuple ahead of its numbers.
	outfit_panel = new wxPanel(this);
	{
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		outfit_is_item = new wxCheckBox(outfit_panel, wxID_ANY, "Item disguise");
		outfit_is_item->SetValue(spell.outfitIsItem);
		row->Add(outfit_is_item, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

		row->Add(new wxStaticText(outfit_panel, wxID_ANY, "Look"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
		outfit_look = new wxSpinCtrl(outfit_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
		                             wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, spell.outfitLook);
		row->Add(outfit_look, 0, wxRIGHT, 8);

		for(int i = 0; i < 4; ++i) {
			outfit_colors[i] = new wxSpinCtrl(outfit_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
			                                  wxSize(56, -1), wxSP_ARROW_KEYS, 0, 255, spell.outfitColors[i]);
			row->Add(outfit_colors[i], 0, wxRIGHT, 2);
		}
		outfit_item = new wxSpinCtrl(outfit_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
		                             wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, spell.outfitItemType);
		row->Add(outfit_item, 0, wxRIGHT, 8);

		outfit_preview = new SecSpritePanel(outfit_panel);
		row->Add(outfit_preview, 0, wxALIGN_CENTER_VERTICAL);
		outfit_panel->SetSizer(row);
	}
	impact_box->Add(outfit_panel, 0, wxEXPAND | wxALL, 4);
	top->Add(impact_box, 0, wxEXPAND | wxALL, 8);

	wxBoxSizer* mana_row = new wxBoxSizer(wxHORIZONTAL);
	mana_row->Add(new wxStaticText(this, wxID_ANY, "Mana / cooldown"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	mana_ctrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1),
	                           wxSP_ARROW_KEYS, 0, 100000, spell.mana);
	mana_row->Add(mana_ctrl);
	top->Add(mana_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

	top->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);
	top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);

	auto refreshOutfit = [this]() {
		Outfit o;
		o.lookType = outfit_is_item->GetValue() ? 0 : outfit_look->GetValue();
		if(outfit_is_item->GetValue()) o.lookItem = outfit_item->GetValue();
		else {
			o.lookHead = outfit_colors[0]->GetValue();
			o.lookBody = outfit_colors[1]->GetValue();
			o.lookLegs = outfit_colors[2]->GetValue();
			o.lookFeet = outfit_colors[3]->GetValue();
		}
		outfit_preview->SetOutfit(o);
		for(int i = 0; i < 4; ++i) outfit_colors[i]->Show(!outfit_is_item->GetValue());
		outfit_item->Show(outfit_is_item->GetValue());
		outfit_panel->Layout();
	};

	shape_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RebuildShapeParams(); Fit(); });
	impact_choice->Bind(wxEVT_CHOICE, [this, refreshOutfit](wxCommandEvent&) {
		RebuildImpactParams();
		refreshOutfit();
		Fit();
	});
	outfit_is_item->Bind(wxEVT_CHECKBOX, [refreshOutfit](wxCommandEvent&) { refreshOutfit(); });
	outfit_look->Bind(wxEVT_SPINCTRL, [refreshOutfit](wxSpinEvent&) { refreshOutfit(); });
	outfit_item->Bind(wxEVT_SPINCTRL, [refreshOutfit](wxSpinEvent&) { refreshOutfit(); });
	for(int i = 0; i < 4; ++i)
		outfit_colors[i]->Bind(wxEVT_SPINCTRL, [refreshOutfit](wxSpinEvent&) { refreshOutfit(); });

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_OK) Store();
		e.Skip();
	});

	SetSizer(top);
	RebuildShapeParams();
	RebuildImpactParams();
	refreshOutfit();
	Fit();
	Centre();
}

void SecSpellDialog::RebuildShapeParams()
{
	shape_grid->Clear(true);
	shape_ctrls.clear();
	const secmon::SpellShape shape = (secmon::SpellShape)shape_choice->GetSelection();
	int count = 0;
	const char* const* names = shapeParamNames(shape, count);
	// Keep whatever the file had if it disagrees with the canonical count.
	const int have = (shape == spell.shape) ? (int)spell.shapeParams.size() : count;
	const int n = std::max(count, have);
	for(int i = 0; i < n; ++i) {
		const wxString label = i < count ? wxString(names[i]) : wxString::Format("Param %d", i + 1);
		int value = (shape == spell.shape && i < (int)spell.shapeParams.size()) ? spell.shapeParams[i] : 0;
		shape_ctrls.push_back(addSpin(shape_panel, shape_grid, label, value, -100000, 100000));
	}
	shape_panel->Layout();
}

void SecSpellDialog::RebuildImpactParams()
{
	impact_grid->Clear(true);
	impact_ctrls.clear();
	const secmon::SpellImpact impact = (secmon::SpellImpact)impact_choice->GetSelection();
	int count = 0;
	const char* const* names = impactParamNames(impact, count);
	const int have = (impact == spell.impact) ? (int)spell.impactParams.size() : count;
	const int n = std::max(count, have);
	for(int i = 0; i < n; ++i) {
		const wxString label = i < count ? wxString(names[i]) : wxString::Format("Param %d", i + 1);
		int value = (impact == spell.impact && i < (int)spell.impactParams.size()) ? spell.impactParams[i] : 0;
		impact_ctrls.push_back(addSpin(impact_panel, impact_grid, label, value, -100000, 100000));
	}
	impact_panel->Layout();
	outfit_panel->Show(impact == secmon::IMPACT_OUTFIT);
	Layout();
}

void SecSpellDialog::Store()
{
	spell.shape = (secmon::SpellShape)shape_choice->GetSelection();
	spell.impact = (secmon::SpellImpact)impact_choice->GetSelection();
	spell.shapeParams.clear();
	for(wxSpinCtrl* c : shape_ctrls) spell.shapeParams.push_back(c->GetValue());
	spell.impactParams.clear();
	for(wxSpinCtrl* c : impact_ctrls) spell.impactParams.push_back(c->GetValue());
	spell.mana = mana_ctrl->GetValue();

	spell.impactHasOutfit = (spell.impact == secmon::IMPACT_OUTFIT);
	if(spell.impactHasOutfit) {
		spell.outfitIsItem = outfit_is_item->GetValue();
		spell.outfitLook = outfit_look->GetValue();
		spell.outfitItemType = outfit_item->GetValue();
		for(int i = 0; i < 4; ++i) spell.outfitColors[i] = outfit_colors[i]->GetValue();
	}
}

// ============================================================================
// SecSkillDialog

SecSkillDialog::SecSkillDialog(wxWindow* parent, secmon::Skill& sk)
	: wxDialog(parent, wxID_ANY, "Skill", wxDefaultPosition, wxDefaultSize,
	           wxDEFAULT_DIALOG_STYLE),
	  skill(sk)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 4, 6);

	grid->Add(new wxStaticText(this, wxID_ANY, "Skill"), 0, wxALIGN_CENTER_VERTICAL);
	name_ctrl = new wxComboBox(this, wxID_ANY, wxstr(skill.name));
	for(const char* n : kSkillNames) name_ctrl->Append(n);
	grid->Add(name_ctrl);

	static const char* const labels[6] = {
		"Actual", "Minimum", "Maximum", "Next level", "Factor %", "Add level"
	};
	int* values[6] = {
		&skill.actual, &skill.minimum, &skill.maximum,
		&skill.nextLevel, &skill.factorPercent, &skill.addLevel
	};
	for(int i = 0; i < 6; ++i)
		fields[i] = addSpin(this, grid, labels[i], *values[i], -1000000, 1000000);

	top->Add(grid, 0, wxALL, 10);
	top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);

	Bind(wxEVT_BUTTON, [this, values](wxCommandEvent& e) {
		if(e.GetId() == wxID_OK) {
			skill.name = nstr(name_ctrl->GetValue());
			for(int i = 0; i < 6; ++i) *values[i] = fields[i]->GetValue();
		}
		e.Skip();
	});

	SetSizerAndFit(top);
	Centre();
}

// ============================================================================
// SecLootDialog

SecLootDialog::SecLootDialog(wxWindow* parent, secmon::Loot& l)
	: wxDialog(parent, wxID_ANY, "Loot entry", wxDefaultPosition, wxDefaultSize,
	           wxDEFAULT_DIALOG_STYLE),
	  loot(l)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 4, 6);

	id_ctrl = addSpin(this, grid, "Item id (client)", loot.itemId, 0, 65535);
	amount_ctrl = addSpin(this, grid, "Max amount", loot.maxAmount, 1, 65535);
	chance_ctrl = addSpin(this, grid, "Chance (per 1000)", loot.chancePerMille, 0, 1000);
	top->Add(grid, 0, wxALL, 10);

	wxBoxSizer* info = new wxBoxSizer(wxHORIZONTAL);
	preview = new SecSpritePanel(this);
	info->Add(preview, 0, wxRIGHT, 8);
	wxBoxSizer* text = new wxBoxSizer(wxVERTICAL);
	name_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	percent_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	text->Add(name_label);
	text->Add(percent_label);
	info->Add(text, 1, wxALIGN_CENTER_VERTICAL);
	top->Add(info, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);

	id_ctrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { Refresh(); });
	id_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Refresh(); });
	chance_ctrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { Refresh(); });

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_OK) {
			loot.itemId = id_ctrl->GetValue();
			loot.maxAmount = amount_ctrl->GetValue();
			loot.chancePerMille = chance_ctrl->GetValue();
		}
		e.Skip();
	});

	SetSizerAndFit(top);
	Refresh();
	Centre();
}

void SecLootDialog::Refresh()
{
	const int id = id_ctrl->GetValue();
	preview->SetItem(id);
	name_label->SetLabel(secItemLabel(id));
	percent_label->SetLabel(wxString::Format("%.1f%% chance per kill",
	                                         chance_ctrl->GetValue() / 10.0));

	// Terrain cannot be looted - the server needs the Take flag on the type.
	const uint16_t server_id = secClientToServerId(id);
	if(server_id && !g_items.getItemType(server_id).pickupable)
		name_label->SetLabel(name_label->GetLabel() + "   [not pickupable]");
}

// ============================================================================
// SecResistanceDialog

SecResistanceDialog::SecResistanceDialog(wxWindow* parent, secmon::Resistance& r)
	: wxDialog(parent, wxID_ANY, "Resistance", wxDefaultPosition, wxDefaultSize,
	           wxDEFAULT_DIALOG_STYLE),
	  resistance(r)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 4, 6);

	grid->Add(new wxStaticText(this, wxID_ANY, "Damage type"), 0, wxALIGN_CENTER_VERTICAL);
	name_ctrl = new wxComboBox(this, wxID_ANY, wxstr(resistance.name));
	for(const std::string& n : secmon::MonFile::resistanceNames()) name_ctrl->Append(wxstr(n));
	grid->Add(name_ctrl);

	percent_ctrl = addSpin(this, grid, "Percent", resistance.percent, -1000, 1000);
	top->Add(grid, 0, wxALL, 10);
	top->Add(new wxStaticText(this, wxID_ANY,
		"Positive resists damage, negative takes extra."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
	top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_OK) {
			resistance.name = nstr(name_ctrl->GetValue());
			resistance.percent = percent_ctrl->GetValue();
		}
		e.Skip();
	});

	SetSizerAndFit(top);
	Centre();
}

// ============================================================================
// SecMonsterEditorDialog

secmon::MonsterType* SecMonsterEditorDialog::current()
{
	secmon::MonFile* mf = SecData::get().monsterByRace(current_race);
	return mf ? &mf->data : nullptr;
}

SecMonsterEditorDialog::SecMonsterEditorDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Monsters", wxDefaultPosition, wxSize(940, 660),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);

	// ---- left: the race list -------------------------------------------
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	filter_ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                             wxSize(220, -1), wxTE_PROCESS_ENTER);
	filter_ctrl->SetHint("Filter by name or race number");
	left->Add(filter_ctrl, 0, wxEXPAND | wxBOTTOM, 4);

	race_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(220, 460));
	left->Add(race_list, 1, wxEXPAND);

	wxBoxSizer* left_buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* new_button = new wxButton(this, wxID_ANY, "New", wxDefaultPosition, wxSize(60, -1));
	wxButton* clone_button = new wxButton(this, wxID_ANY, "Clone", wxDefaultPosition, wxSize(60, -1));
	wxButton* delete_button = new wxButton(this, wxID_ANY, "Delete", wxDefaultPosition, wxSize(70, -1));
	left_buttons->Add(new_button, 0, wxRIGHT, 3);
	left_buttons->Add(clone_button, 0, wxRIGHT, 3);
	left_buttons->Add(delete_button);
	left->Add(left_buttons, 0, wxTOP, 4);
	body->Add(left, 0, wxEXPAND | wxALL, 8);

	// ---- right: the tabs ------------------------------------------------
	wxNotebook* book = new wxNotebook(this, wxID_ANY);

	// Basic ---------------------------------------------------------------
	wxPanel* basic = new wxPanel(book);
	{
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);

		wxFlexGridSizer* ident = new wxFlexGridSizer(4, 6, 6);
		race_ctrl = addSpin(basic, ident, "Race number", 0, 1, 4095);
		ident->Add(new wxStaticText(basic, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
		name_ctrl = new wxTextCtrl(basic, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(180, -1));
		ident->Add(name_ctrl);
		ident->Add(new wxStaticText(basic, wxID_ANY, "Article"), 0, wxALIGN_CENTER_VERTICAL);
		article_ctrl = new wxTextCtrl(basic, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1));
		ident->Add(article_ctrl);
		ident->AddStretchSpacer();
		ident->AddStretchSpacer();
		col->Add(ident, 0, wxALL, 8);

		// outfit
		wxStaticBoxSizer* look = new wxStaticBoxSizer(wxHORIZONTAL, basic, "Outfit");
		wxWindow* look_parent = look->GetStaticBox();
		wxFlexGridSizer* look_grid = new wxFlexGridSizer(6, 4, 6);
		look_ctrl = addSpin(look_parent, look_grid, "Look type", 0, 0, 65535, 70);
		for(int i = 0; i < 4; ++i) {
			static const char* const names[4] = {"Head", "Body", "Legs", "Feet"};
			color_ctrl[i] = addSpin(look_parent, look_grid, names[i], 0, 0, 255, 56);
		}
		look->Add(look_grid, 0, wxALL, 4);
		wxBoxSizer* look_side = new wxBoxSizer(wxVERTICAL);
		outfit_item_check = new wxCheckBox(look_parent, wxID_ANY, "Item disguise");
		look_side->Add(outfit_item_check, 0, wxBOTTOM, 4);
		wxBoxSizer* item_row = new wxBoxSizer(wxHORIZONTAL);
		item_row->Add(new wxStaticText(look_parent, wxID_ANY, "Item"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
		outfit_item_ctrl = new wxSpinCtrl(look_parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
		                                  wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
		item_row->Add(outfit_item_ctrl);
		look_side->Add(item_row);
		look->Add(look_side, 0, wxALL, 4);
		outfit_preview = new SecSpritePanel(look_parent, wxSize(40, 40));
		look->Add(outfit_preview, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
		col->Add(look, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

		// corpse
		wxStaticBoxSizer* corpse = new wxStaticBoxSizer(wxHORIZONTAL, basic, "Corpse");
		wxWindow* cp = corpse->GetStaticBox();
		wxFlexGridSizer* corpse_grid = new wxFlexGridSizer(4, 4, 6);
		corpse_ctrl = addSpin(cp, corpse_grid, "Item id", 0, 0, 65535, 70);
		corpse2_ctrl = addSpin(cp, corpse_grid, "Second id", 0, 0, 65535, 70);
		corpse->Add(corpse_grid, 0, wxALL, 4);
		corpse_plural_check = new wxCheckBox(cp, wxID_ANY, "Two corpses (Corpses =)");
		corpse->Add(corpse_plural_check, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
		corpse_preview = new SecSpritePanel(cp, wxSize(40, 40));
		corpse->Add(corpse_preview, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
		col->Add(corpse, 0, wxEXPAND | wxALL, 8);

		// combat numbers
		wxFlexGridSizer* stats = new wxFlexGridSizer(6, 6, 6);
		hp_ctrl = addSpin(basic, stats, "Hitpoints", 0, 0, 10000000);
		exp_ctrl = addSpin(basic, stats, "Experience", 0, 0, 10000000);
		stats->Add(new wxStaticText(basic, wxID_ANY, "Blood"), 0, wxALIGN_CENTER_VERTICAL);
		blood_ctrl = new wxChoice(basic, wxID_ANY);
		for(const char* b : kBloodNames) blood_ctrl->Append(b);
		stats->Add(blood_ctrl);

		attack_ctrl = addSpin(basic, stats, "Attack", 0, 0, 100000);
		defend_ctrl = addSpin(basic, stats, "Defend", 0, 0, 100000);
		armor_ctrl = addSpin(basic, stats, "Armor", 0, 0, 100000);

		poison_ctrl = addSpin(basic, stats, "Poison", 0, 0, 100000);
		summon_ctrl = addSpin(basic, stats, "Summon cost", 0, 0, 100000);
		flee_ctrl = addSpin(basic, stats, "Flee threshold", 0, 0, 100000);

		lose_ctrl = addSpin(basic, stats, "Lose target", 0, 0, 100000);
		stats->AddStretchSpacer();
		stats->AddStretchSpacer();
		stats->AddStretchSpacer();
		stats->AddStretchSpacer();
		col->Add(stats, 0, wxALL, 8);

		wxStaticBoxSizer* strat = new wxStaticBoxSizer(wxHORIZONTAL, basic,
			"Strategy (chance %: closest, weakest, strongest, random)");
		wxFlexGridSizer* strat_grid = new wxFlexGridSizer(8, 4, 6);
		static const char* const strat_names[4] = {"Closest", "Weakest", "Strongest", "Random"};
		for(int i = 0; i < 4; ++i)
			strategy_ctrl[i] = addSpin(strat->GetStaticBox(), strat_grid, strat_names[i], 0, 0, 100, 56);
		strat->Add(strat_grid, 0, wxALL, 4);
		col->Add(strat, 0, wxEXPAND | wxALL, 8);

		basic->SetSizer(col);
	}
	book->AddPage(basic, "Basic", true);

	// Flags & resistances ---------------------------------------------------
	wxPanel* flags_page = new wxPanel(book);
	{
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		wxStaticBoxSizer* fbox = new wxStaticBoxSizer(wxVERTICAL, flags_page, "Flags");
		wxFlexGridSizer* fgrid = new wxFlexGridSizer(2, 2, 8);
		for(int i = 0; i < 14; ++i) {
			flag_check[i] = new wxCheckBox(fbox->GetStaticBox(), wxID_ANY, kFlagLabels[i]);
			fgrid->Add(flag_check[i]);
		}
		fbox->Add(fgrid, 0, wxALL, 6);
		row->Add(fbox, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer* rbox = new wxStaticBoxSizer(wxVERTICAL, flags_page, "Resistances");
		resist_list = new wxListCtrl(rbox->GetStaticBox(), wxID_ANY, wxDefaultPosition,
		                             wxSize(280, 300), wxLC_REPORT | wxLC_SINGLE_SEL);
		resist_list->AppendColumn("Damage type", wxLIST_FORMAT_LEFT, 160);
		resist_list->AppendColumn("Percent", wxLIST_FORMAT_RIGHT, 90);
		rbox->Add(resist_list, 1, wxEXPAND | wxALL, 4);
		wxBoxSizer* rb = new wxBoxSizer(wxHORIZONTAL);
		wxButton* r_add = new wxButton(rbox->GetStaticBox(), wxID_ANY, "Add");
		wxButton* r_edit = new wxButton(rbox->GetStaticBox(), wxID_ANY, "Edit");
		wxButton* r_del = new wxButton(rbox->GetStaticBox(), wxID_ANY, "Remove");
		rb->Add(r_add, 0, wxRIGHT, 3); rb->Add(r_edit, 0, wxRIGHT, 3); rb->Add(r_del);
		rbox->Add(rb, 0, wxALL, 4);
		row->Add(rbox, 1, wxEXPAND | wxALL, 8);
		flags_page->SetSizer(row);

		r_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if(!current()) return;
			secmon::Resistance r; r.name = "physical";
			SecResistanceDialog dlg(this, r);
			if(dlg.ShowModal() == wxID_OK) { current()->resistances.push_back(r); RefreshResistances(); }
		});
		r_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = resist_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			SecResistanceDialog dlg(this, current()->resistances[sel]);
			if(dlg.ShowModal() == wxID_OK) RefreshResistances();
		});
		r_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = resist_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			current()->resistances.erase(current()->resistances.begin() + sel);
			RefreshResistances();
		});
	}
	book->AddPage(flags_page, "Flags & resistances");

	// Skills ---------------------------------------------------------------
	wxPanel* skills_page = new wxPanel(book);
	{
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		skill_list = new wxListCtrl(skills_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		                            wxLC_REPORT | wxLC_SINGLE_SEL);
		skill_list->AppendColumn("Skill", wxLIST_FORMAT_LEFT, 150);
		skill_list->AppendColumn("Actual", wxLIST_FORMAT_RIGHT, 80);
		skill_list->AppendColumn("Min", wxLIST_FORMAT_RIGHT, 70);
		skill_list->AppendColumn("Max", wxLIST_FORMAT_RIGHT, 80);
		skill_list->AppendColumn("Next level", wxLIST_FORMAT_RIGHT, 90);
		skill_list->AppendColumn("Factor %", wxLIST_FORMAT_RIGHT, 80);
		skill_list->AppendColumn("Add level", wxLIST_FORMAT_RIGHT, 80);
		col->Add(skill_list, 1, wxEXPAND | wxALL, 8);
		wxBoxSizer* b = new wxBoxSizer(wxHORIZONTAL);
		wxButton* s_add = new wxButton(skills_page, wxID_ANY, "Add");
		wxButton* s_edit = new wxButton(skills_page, wxID_ANY, "Edit");
		wxButton* s_del = new wxButton(skills_page, wxID_ANY, "Remove");
		b->Add(s_add, 0, wxRIGHT, 3); b->Add(s_edit, 0, wxRIGHT, 3); b->Add(s_del);
		col->Add(b, 0, wxLEFT | wxBOTTOM, 8);
		skills_page->SetSizer(col);

		s_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if(!current()) return;
			secmon::Skill s; s.name = "FistFighting";
			SecSkillDialog dlg(this, s);
			if(dlg.ShowModal() == wxID_OK) { current()->skills.push_back(s); RefreshSkills(); }
		});
		s_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = skill_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			SecSkillDialog dlg(this, current()->skills[sel]);
			if(dlg.ShowModal() == wxID_OK) { RefreshSkills(); RefreshPreviews(); }
		});
		s_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = skill_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			current()->skills.erase(current()->skills.begin() + sel);
			RefreshSkills();
		});
	}
	book->AddPage(skills_page, "Skills");

	// Spells ---------------------------------------------------------------
	wxPanel* spells_page = new wxPanel(book);
	{
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		spell_list = new wxListCtrl(spells_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		                            wxLC_REPORT | wxLC_SINGLE_SEL);
		spell_list->AppendColumn("Spell", wxLIST_FORMAT_LEFT, 520);
		spell_list->AppendColumn("Mana", wxLIST_FORMAT_RIGHT, 80);
		col->Add(spell_list, 1, wxEXPAND | wxALL, 8);
		wxBoxSizer* b = new wxBoxSizer(wxHORIZONTAL);
		wxButton* p_add = new wxButton(spells_page, wxID_ANY, "Add");
		wxButton* p_edit = new wxButton(spells_page, wxID_ANY, "Edit");
		wxButton* p_del = new wxButton(spells_page, wxID_ANY, "Remove");
		b->Add(p_add, 0, wxRIGHT, 3); b->Add(p_edit, 0, wxRIGHT, 3); b->Add(p_del);
		col->Add(b, 0, wxLEFT | wxBOTTOM, 8);
		spells_page->SetSizer(col);

		p_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if(!current()) return;
			secmon::Spell sp;
			sp.shapeParams.assign(1, 0);
			sp.impactParams.assign(3, 0);
			SecSpellDialog dlg(this, sp);
			if(dlg.ShowModal() == wxID_OK) { current()->spells.push_back(sp); RefreshSpells(); }
		});
		p_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = spell_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			SecSpellDialog dlg(this, current()->spells[sel]);
			if(dlg.ShowModal() == wxID_OK) RefreshSpells();
		});
		p_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = spell_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			current()->spells.erase(current()->spells.begin() + sel);
			RefreshSpells();
		});
	}
	book->AddPage(spells_page, "Spells");

	// Loot -----------------------------------------------------------------
	wxPanel* loot_page = new wxPanel(book);
	{
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		loot_list = new wxListCtrl(loot_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		                           wxLC_REPORT | wxLC_SINGLE_SEL);
		loot_list->AppendColumn("Item", wxLIST_FORMAT_LEFT, 330);
		loot_list->AppendColumn("Max amount", wxLIST_FORMAT_RIGHT, 110);
		loot_list->AppendColumn("Chance", wxLIST_FORMAT_RIGHT, 100);
		col->Add(loot_list, 1, wxEXPAND | wxALL, 8);
		wxBoxSizer* b = new wxBoxSizer(wxHORIZONTAL);
		wxButton* l_add = new wxButton(loot_page, wxID_ANY, "Add");
		wxButton* l_edit = new wxButton(loot_page, wxID_ANY, "Edit");
		wxButton* l_del = new wxButton(loot_page, wxID_ANY, "Remove");
		b->Add(l_add, 0, wxRIGHT, 3); b->Add(l_edit, 0, wxRIGHT, 3); b->Add(l_del);
		col->Add(b, 0, wxLEFT | wxBOTTOM, 8);
		loot_page->SetSizer(col);

		l_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if(!current()) return;
			secmon::Loot l; l.maxAmount = 1; l.chancePerMille = 100;
			SecLootDialog dlg(this, l);
			if(dlg.ShowModal() == wxID_OK) { current()->inventory.push_back(l); RefreshLoot(); UpdateWarnings(); }
		});
		l_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = loot_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			SecLootDialog dlg(this, current()->inventory[sel]);
			if(dlg.ShowModal() == wxID_OK) { RefreshLoot(); UpdateWarnings(); }
		});
		l_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = loot_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if(!current() || sel < 0) return;
			current()->inventory.erase(current()->inventory.begin() + sel);
			RefreshLoot();
			UpdateWarnings();
		});
	}
	book->AddPage(loot_page, "Loot");

	// Talk -----------------------------------------------------------------
	wxPanel* talk_page = new wxPanel(book);
	{
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		talk_list = new wxListBox(talk_page, wxID_ANY);
		col->Add(talk_list, 1, wxEXPAND | wxALL, 8);
		wxBoxSizer* b = new wxBoxSizer(wxHORIZONTAL);
		wxButton* t_add = new wxButton(talk_page, wxID_ANY, "Add");
		wxButton* t_edit = new wxButton(talk_page, wxID_ANY, "Edit");
		wxButton* t_del = new wxButton(talk_page, wxID_ANY, "Remove");
		b->Add(t_add, 0, wxRIGHT, 3); b->Add(t_edit, 0, wxRIGHT, 3); b->Add(t_del);
		col->Add(b, 0, wxLEFT | wxBOTTOM, 8);
		col->Add(new wxStaticText(talk_page, wxID_ANY,
			"Prefix a line with #Y or #y to colour it, as the vanilla files do."),
			0, wxLEFT | wxBOTTOM, 8);
		talk_page->SetSizer(col);

		t_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if(!current()) return;
			wxTextEntryDialog dlg(this, "Sentence", "Add talk line");
			if(dlg.ShowModal() == wxID_OK && !dlg.GetValue().empty()) {
				current()->talk.push_back(nstr(dlg.GetValue()));
				RefreshTalk();
			}
		});
		t_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			int sel = talk_list->GetSelection();
			if(!current() || sel == wxNOT_FOUND) return;
			wxTextEntryDialog dlg(this, "Sentence", "Edit talk line", wxstr(current()->talk[sel]));
			if(dlg.ShowModal() == wxID_OK) { current()->talk[sel] = nstr(dlg.GetValue()); RefreshTalk(); }
		});
		t_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			int sel = talk_list->GetSelection();
			if(!current() || sel == wxNOT_FOUND) return;
			current()->talk.erase(current()->talk.begin() + sel);
			RefreshTalk();
		});
	}
	book->AddPage(talk_page, "Talk");

	body->Add(book, 1, wxEXPAND | wxALL, 8);
	outer->Add(body, 1, wxEXPAND);

	warning_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	warning_label->SetForegroundColour(wxColour(160, 60, 0));
	outer->Add(warning_label, 0, wxLEFT | wxRIGHT, 12);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* save_button = new wxButton(this, wxID_ANY, "Save files now");
	wxButton* close_button = new wxButton(this, wxID_CLOSE, "Close");
	buttons->AddStretchSpacer();
	buttons->Add(save_button, 0, wxRIGHT, 6);
	buttons->Add(close_button);
	outer->Add(buttons, 0, wxEXPAND | wxALL, 10);
	SetSizer(outer);

	// ---- behaviour -------------------------------------------------------
	filter_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { StoreCurrent(); BuildList(); });
	race_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
		StoreCurrent();
		int sel = race_list->GetSelection();
		if(sel != wxNOT_FOUND && sel < (int)shown.size()) LoadRace(shown[sel]);
	});

	auto previewChanged = [this](wxSpinEvent&) { StoreCurrent(); RefreshPreviews(); };
	look_ctrl->Bind(wxEVT_SPINCTRL, previewChanged);
	outfit_item_ctrl->Bind(wxEVT_SPINCTRL, previewChanged);
	corpse_ctrl->Bind(wxEVT_SPINCTRL, previewChanged);
	for(int i = 0; i < 4; ++i) color_ctrl[i]->Bind(wxEVT_SPINCTRL, previewChanged);
	outfit_item_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { StoreCurrent(); RefreshPreviews(); });
	hp_ctrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { StoreCurrent(); UpdateWarnings(); });

	new_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		StoreCurrent();
		SecData& data = SecData::get();
		const int race = data.freeRaceNumber();
		if(race == 0) {
			wxMessageBox("No free race number below 512 - the engine's race table is full.",
			             "Cannot add", wxOK | wxICON_ERROR, this);
			return;
		}
		wxTextEntryDialog dlg(this, "Name of the new monster", "New monster");
		if(dlg.ShowModal() != wxID_OK || dlg.GetValue().empty()) return;
		secmon::MonFile mf;
		mf.data.raceNumber = race;
		mf.data.name = nstr(dlg.GetValue());
		mf.data.article = "a";
		mf.data.corpses.push_back(0);
		mf.data.setHitPoints(100);
		{ secmon::Skill s; s.name = "GoStrength"; s.actual = s.maximum = 100; mf.data.skills.push_back(s); }
		{ secmon::Skill s; s.name = "CarryStrength"; s.actual = s.maximum = 200; mf.data.skills.push_back(s); }
		{ secmon::Skill s; s.name = "FistFighting"; s.actual = s.minimum = s.maximum = 10;
		  s.factorPercent = 1000; s.addLevel = 5; mf.data.skills.push_back(s); }
		mf.data.strategy[0] = 100;
		data.monsters[race] = mf;
		data.monstersDirty = true;
		BuildList();
		LoadRace(race);
	});

	clone_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		StoreCurrent();
		SecData& data = SecData::get();
		secmon::MonFile* src = data.monsterByRace(current_race);
		if(!src) return;
		const int race = data.freeRaceNumber();
		if(race == 0) {
			wxMessageBox("No free race number below 512 - the engine's race table is full.",
			             "Cannot clone", wxOK | wxICON_ERROR, this);
			return;
		}
		wxTextEntryDialog dlg(this, "Name of the copy", "Clone monster",
		                      wxstr(src->data.name) + " copy");
		if(dlg.ShowModal() != wxID_OK || dlg.GetValue().empty()) return;
		secmon::MonFile copy;
		copy.data = src->data;             // no chunks: written from scratch
		copy.data.raceNumber = race;
		copy.data.name = nstr(dlg.GetValue());
		data.monsters[race] = copy;
		data.monstersDirty = true;
		BuildList();
		LoadRace(race);
	});

	delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		SecData& data = SecData::get();
		secmon::MonFile* mf = data.monsterByRace(current_race);
		if(!mf) return;
		int spawns = 0;
		for(const secmon::SpawnRow& r : data.db.rows) if(r.race == current_race) ++spawns;
		wxString msg = wxString::Format("Delete '%s' (race %d)?", wxstr(mf->data.name), current_race);
		if(spawns) msg << wxString::Format("\n\n%d spawn row(s) still point at this race. "
		                                   "They would spawn nothing.", spawns);
		if(wxMessageBox(msg, "Delete monster", wxYES_NO | wxICON_WARNING, this) != wxYES) return;
		if(!mf->sourcePath.empty()) data.deletedRaceFiles.push_back(mf->sourcePath);
		data.monsters.erase(current_race);
		data.monstersDirty = true;
		current_race = -1;
		BuildList();
		if(!shown.empty()) LoadRace(shown[0]);
	});

	save_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		StoreCurrent();
		wxArrayString notes;
		wxString error;
		if(!SecData::get().save(notes, error)) {
			wxMessageBox(error, "Nothing was written", wxOK | wxICON_ERROR, this);
			return;
		}
		wxString msg;
		for(size_t i = 0; i < notes.GetCount(); ++i) msg << notes[i] << "\n";
		if(msg.empty()) msg = "Nothing had changed.";
		wxMessageBox(msg, "Saved", wxOK | wxICON_INFORMATION, this);
	});

	close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StoreCurrent(); EndModal(wxID_CLOSE); });
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { StoreCurrent(); EndModal(wxID_CLOSE); });

	BuildList();
	if(!shown.empty()) {
		race_list->SetSelection(0);
		LoadRace(shown[0]);
	}
	Centre();
}

void SecMonsterEditorDialog::BuildList()
{
	const wxString filter = filter_ctrl->GetValue().Lower();
	shown.clear();
	race_list->Freeze();
	race_list->Clear();
	const SecData& data = SecData::get();
	for(std::map<int, secmon::MonFile>::const_iterator it = data.monsters.begin();
	    it != data.monsters.end(); ++it) {
		const wxString name = wxstr(it->second.data.name);
		const wxString number = wxString::Format("%d", it->first);
		if(!filter.empty() && !name.Lower().Contains(filter) && !number.Contains(filter))
			continue;
		shown.push_back(it->first);
		race_list->Append(wxString::Format("%4d  %s", it->first, name));
	}
	race_list->Thaw();

	for(size_t i = 0; i < shown.size(); ++i)
		if(shown[i] == current_race) { race_list->SetSelection((int)i); break; }
}

void SecMonsterEditorDialog::LoadRace(int race)
{
	current_race = race;
	secmon::MonsterType* m = current();
	if(!m) return;

	race_ctrl->SetValue(m->raceNumber);
	name_ctrl->SetValue(wxstr(m->name));
	article_ctrl->SetValue(wxstr(m->article));

	look_ctrl->SetValue(m->outfitLook);
	for(int i = 0; i < 4; ++i) color_ctrl[i]->SetValue(m->outfitColors[i]);
	outfit_item_check->SetValue(m->outfitIsItem);
	outfit_item_ctrl->SetValue(m->outfitItemType);

	corpse_ctrl->SetValue(m->corpses.empty() ? 0 : m->corpses[0]);
	corpse2_ctrl->SetValue(m->corpses.size() > 1 ? m->corpses[1] : 0);
	corpse_plural_check->SetValue(m->corpsePlural);

	blood_ctrl->SetSelection((int)m->blood);
	hp_ctrl->SetValue(m->getHitPoints());
	exp_ctrl->SetValue(m->experience);
	attack_ctrl->SetValue(m->attack);
	defend_ctrl->SetValue(m->defend);
	armor_ctrl->SetValue(m->armor);
	poison_ctrl->SetValue(m->poison);
	summon_ctrl->SetValue(m->summonCost);
	flee_ctrl->SetValue(m->fleeThreshold);
	lose_ctrl->SetValue(m->loseTarget);
	for(int i = 0; i < 4; ++i) strategy_ctrl[i]->SetValue(m->strategy[i]);

	for(int i = 0; i < 14; ++i) flag_check[i]->SetValue((m->flags & kFlagBits[i]) != 0);

	RefreshResistances();
	RefreshSkills();
	RefreshSpells();
	RefreshLoot();
	RefreshTalk();
	RefreshPreviews();
	UpdateWarnings();
}

void SecMonsterEditorDialog::StoreCurrent()
{
	secmon::MonsterType* m = current();
	if(!m) return;

	m->raceNumber = race_ctrl->GetValue();
	m->name = nstr(name_ctrl->GetValue());
	m->article = nstr(article_ctrl->GetValue());

	m->outfitLook = look_ctrl->GetValue();
	for(int i = 0; i < 4; ++i) m->outfitColors[i] = color_ctrl[i]->GetValue();
	m->outfitIsItem = outfit_item_check->GetValue();
	m->outfitItemType = outfit_item_ctrl->GetValue();

	m->corpsePlural = corpse_plural_check->GetValue();
	m->corpses.clear();
	m->corpses.push_back(corpse_ctrl->GetValue());
	if(m->corpsePlural) m->corpses.push_back(corpse2_ctrl->GetValue());

	m->blood = (secmon::BloodType)std::max(0, blood_ctrl->GetSelection());
	m->setHitPoints(hp_ctrl->GetValue());
	m->experience = exp_ctrl->GetValue();
	m->attack = attack_ctrl->GetValue();
	m->defend = defend_ctrl->GetValue();
	m->armor = armor_ctrl->GetValue();
	m->poison = poison_ctrl->GetValue();
	m->summonCost = summon_ctrl->GetValue();
	m->fleeThreshold = flee_ctrl->GetValue();
	m->loseTarget = lose_ctrl->GetValue();
	for(int i = 0; i < 4; ++i) m->strategy[i] = strategy_ctrl[i]->GetValue();

	m->flags = 0;
	for(int i = 0; i < 14; ++i) if(flag_check[i]->GetValue()) m->flags |= kFlagBits[i];
}

void SecMonsterEditorDialog::RefreshPreviews()
{
	secmon::MonsterType* m = current();
	if(!m) { outfit_preview->Clear(); corpse_preview->Clear(); return; }
	outfit_preview->SetOutfit(SecData::get().outfitForRace(current_race));
	corpse_preview->SetItem(m->corpses.empty() ? 0 : m->corpses[0]);
	for(int i = 0; i < 4; ++i) color_ctrl[i]->Enable(!m->outfitIsItem);
	outfit_item_ctrl->Enable(m->outfitIsItem);
	corpse2_ctrl->Enable(m->corpsePlural);
}

void SecMonsterEditorDialog::RefreshResistances()
{
	resist_list->DeleteAllItems();
	secmon::MonsterType* m = current();
	if(!m) return;
	for(size_t i = 0; i < m->resistances.size(); ++i) {
		long row = resist_list->InsertItem((long)i, wxstr(m->resistances[i].name));
		resist_list->SetItem(row, 1, wxString::Format("%d", m->resistances[i].percent));
	}
}

void SecMonsterEditorDialog::RefreshSkills()
{
	skill_list->DeleteAllItems();
	secmon::MonsterType* m = current();
	if(!m) return;
	for(size_t i = 0; i < m->skills.size(); ++i) {
		const secmon::Skill& s = m->skills[i];
		long row = skill_list->InsertItem((long)i, wxstr(s.name));
		skill_list->SetItem(row, 1, wxString::Format("%d", s.actual));
		skill_list->SetItem(row, 2, wxString::Format("%d", s.minimum));
		skill_list->SetItem(row, 3, wxString::Format("%d", s.maximum));
		skill_list->SetItem(row, 4, wxString::Format("%d", s.nextLevel));
		skill_list->SetItem(row, 5, wxString::Format("%d", s.factorPercent));
		skill_list->SetItem(row, 6, wxString::Format("%d", s.addLevel));
	}
	if(m->getHitPoints() != hp_ctrl->GetValue()) hp_ctrl->SetValue(m->getHitPoints());
}

void SecMonsterEditorDialog::RefreshSpells()
{
	spell_list->DeleteAllItems();
	secmon::MonsterType* m = current();
	if(!m) return;
	for(size_t i = 0; i < m->spells.size(); ++i) {
		long row = spell_list->InsertItem((long)i, spellText(m->spells[i]));
		spell_list->SetItem(row, 1, wxString::Format("%d", m->spells[i].mana));
	}
}

void SecMonsterEditorDialog::RefreshLoot()
{
	loot_list->DeleteAllItems();
	secmon::MonsterType* m = current();
	if(!m) return;
	for(size_t i = 0; i < m->inventory.size(); ++i) {
		const secmon::Loot& l = m->inventory[i];
		long row = loot_list->InsertItem((long)i, secItemLabel(l.itemId));
		loot_list->SetItem(row, 1, wxString::Format("%d", l.maxAmount));
		loot_list->SetItem(row, 2, wxString::Format("%.1f%%", l.chancePerMille / 10.0));
	}
}

void SecMonsterEditorDialog::RefreshTalk()
{
	talk_list->Clear();
	secmon::MonsterType* m = current();
	if(!m) return;
	for(const std::string& line : m->talk) talk_list->Append(wxstr(line));
}

void SecMonsterEditorDialog::UpdateWarnings()
{
	secmon::MonsterType* m = current();
	if(!m) { warning_label->SetLabel(wxEmptyString); return; }

	wxArrayString notes;

	// Loot over the carry budget is destroyed at spawn, not dropped.
	int carry = 0;
	bool haveCarry = false;
	for(const secmon::Skill& s : m->skills)
		if(s.name == "CarryStrength") { carry = s.actual; haveCarry = true; }
	if(!m->inventory.empty() && (!haveCarry || carry == 0))
		notes.Add("CarryStrength is 0, so every loot drop is destroyed at spawn.");

	// Terrain cannot be carried, so it can never be loot.
	int unpickable = 0;
	for(const secmon::Loot& l : m->inventory) {
		const uint16_t sid = secClientToServerId(l.itemId);
		if(sid == 0 || !g_items.getItemType(sid).pickupable) ++unpickable;
	}
	if(unpickable)
		notes.Add(wxString::Format("%d loot entr%s not a pickupable item.",
		                           unpickable, unpickable == 1 ? "y is" : "ies are"));

	if(!m->corpses.empty() && m->corpses[0] != 0 && secClientToServerId(m->corpses[0]) == 0)
		notes.Add(wxString::Format("Corpse id %d is not in items.otb.", m->corpses[0]));

	int strategy_sum = 0;
	for(int i = 0; i < 4; ++i) strategy_sum += m->strategy[i];
	if(strategy_sum != 100)
		notes.Add(wxString::Format("Strategy adds up to %d, not 100.", strategy_sum));

	// 7.7 has no hostile flag; harmless means no attack and a high flee point.
	if(m->attack == 0 && m->fleeThreshold == 0)
		notes.Add("Attack 0 with FleeThreshold 0: this monster will neither fight nor flee.");

	warning_label->SetLabel(notes.IsEmpty() ? wxString()
	                                        : wxString("Note: ") + wxJoin(notes, ' '));
	Layout();
}

// ============================================================================
// SecSpawnRowDialog

SecSpawnRowDialog::SecSpawnRowDialog(wxWindow* parent, secmon::SpawnRow& r, bool allow_position)
	: wxDialog(parent, wxID_ANY, "Spawn", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
	  row(r)
{
	SecData& data = SecData::get();

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(4, 4, 6);

	grid->Add(new wxStaticText(this, wxID_ANY, "Monster"), 0, wxALIGN_CENTER_VERTICAL);
	race_ctrl = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(200, -1));
	for(std::map<int, secmon::MonFile>::const_iterator it = data.monsters.begin();
	    it != data.monsters.end(); ++it)
		race_ctrl->Append(wxString::Format("%d  %s", it->first, wxstr(it->second.data.name)));
	race_ctrl->SetValue(wxString::Format("%d  %s", row.race, wxstr(data.nameForRace(row.race))));
	grid->Add(race_ctrl);

	preview = new SecSpritePanel(this, wxSize(40, 40));
	grid->Add(preview, 0, wxALIGN_CENTER_VERTICAL);
	grid->AddStretchSpacer();

	x_ctrl = addSpin(this, grid, "X", row.x, 0, 65535);
	y_ctrl = addSpin(this, grid, "Y", row.y, 0, 65535);
	z_ctrl = addSpin(this, grid, "Z", row.z, 0, 15, 50);
	grid->AddStretchSpacer();

	amount_ctrl = addSpin(this, grid, "Amount", row.amount, 1, 999);
	radius_ctrl = addSpin(this, grid, "Radius", row.radius, 0, 5000);
	regen_ctrl = addSpin(this, grid, "Respawn", row.regen, 0, 1000000);
	grid->AddStretchSpacer();

	top->Add(grid, 0, wxALL, 10);

	if(!allow_position) {
		x_ctrl->Enable(false);
		y_ctrl->Enable(false);
		z_ctrl->Enable(false);
	}

	note_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	note_label->SetForegroundColour(wxColour(160, 60, 0));
	top->Add(note_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

	top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);

	auto refresh = [this]() { UpdateNotes(); };
	race_ctrl->Bind(wxEVT_COMBOBOX, [refresh](wxCommandEvent&) { refresh(); });
	race_ctrl->Bind(wxEVT_TEXT, [refresh](wxCommandEvent&) { refresh(); });
	radius_ctrl->Bind(wxEVT_SPINCTRL, [refresh](wxSpinEvent&) { refresh(); });

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_OK) {
			long parsed = 0;
			wxString text = race_ctrl->GetValue().BeforeFirst(' ');
			if(text.ToLong(&parsed)) row.race = (int)parsed;
			row.x = x_ctrl->GetValue();
			row.y = y_ctrl->GetValue();
			row.z = z_ctrl->GetValue();
			row.amount = amount_ctrl->GetValue();
			row.radius = radius_ctrl->GetValue();
			row.regen = regen_ctrl->GetValue();
		}
		e.Skip();
	});

	SetSizerAndFit(top);
	UpdateNotes();
	Centre();
}

void SecSpawnRowDialog::UpdateNotes()
{
	long parsed = 0;
	int race = row.race;
	if(race_ctrl->GetValue().BeforeFirst(' ').ToLong(&parsed)) race = (int)parsed;

	SecData& data = SecData::get();
	preview->SetOutfit(data.outfitForRace(race));

	wxArrayString notes;
	if(!data.monsterByRace(race))
		notes.Add(wxString::Format("Race %d has no .mon file.", race));
	// Radius is a leash: leave the box and the engine logs the monster out.
	const int radius = radius_ctrl->GetValue();
	if(radius != 50 && radius != 0)
		notes.Add(wxString::Format("Radius %d is a movement leash, not spawn scatter. "
		                           "Vanilla uses 50; a small value makes the monster "
		                           "vanish when it walks out of the box.", radius));
	note_label->SetLabel(wxJoin(notes, ' '));
	Layout();
	Fit();
}

// ============================================================================
// SecSpawnBrowserDialog

SecSpawnBrowserDialog::SecSpawnBrowserDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Spawns", wxDefaultPosition, wxSize(780, 560),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	filter_ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                             wxDefaultSize, wxTE_PROCESS_ENTER);
	filter_ctrl->SetHint("Filter by monster name, race number or x,y,z");
	top->Add(filter_ctrl, 0, wxEXPAND | wxALL, 8);

	list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                      wxLC_REPORT | wxLC_SINGLE_SEL);
	list->AppendColumn("Race", wxLIST_FORMAT_RIGHT, 60);
	list->AppendColumn("Monster", wxLIST_FORMAT_LEFT, 200);
	list->AppendColumn("X", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Y", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Z", wxLIST_FORMAT_RIGHT, 40);
	list->AppendColumn("Amount", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Radius", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Respawn", wxLIST_FORMAT_RIGHT, 80);
	top->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	count_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	top->Add(count_label, 0, wxALL, 8);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* add = new wxButton(this, wxID_ANY, "Add");
	wxButton* edit = new wxButton(this, wxID_ANY, "Edit");
	wxButton* remove = new wxButton(this, wxID_ANY, "Remove");
	wxButton* goto_button = new wxButton(this, wxID_ANY, "Go to");
	buttons->Add(add, 0, wxRIGHT, 4);
	buttons->Add(edit, 0, wxRIGHT, 4);
	buttons->Add(remove, 0, wxRIGHT, 12);
	buttons->Add(goto_button);
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(this, wxID_CLOSE, "Close"));
	top->Add(buttons, 0, wxEXPAND | wxALL, 8);
	SetSizer(top);

	list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
		int row = selectedRow();
		if(row < 0) return;
		SecSpawnRowDialog dlg(this, SecData::get().db.rows[row], true);
		if(dlg.ShowModal() == wxID_OK) { SecData::get().reindexSpawns(); Rebuild(); }
	});
	filter_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Rebuild(); });

	add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		secmon::SpawnRow r;
		int sel = selectedRow();
		if(sel >= 0) { r = SecData::get().db.rows[sel]; r.id = -1; }
		else if(g_gui.IsEditorOpen()) {
			// Default to the middle of the current view.
			r.radius = 50; r.regen = 600; r.amount = 1;
		}
		SecSpawnRowDialog dlg(this, r, true);
		if(dlg.ShowModal() != wxID_OK) return;
		r.id = -1;
		SecData::get().db.rows.push_back(r);
		SecData::get().reindexSpawns();
		Rebuild();
	});
	edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		int row = selectedRow();
		if(row < 0) return;
		SecSpawnRowDialog dlg(this, SecData::get().db.rows[row], true);
		if(dlg.ShowModal() == wxID_OK) { SecData::get().reindexSpawns(); Rebuild(); }
	});
	remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		int row = selectedRow();
		if(row < 0) return;
		std::vector<secmon::SpawnRow>& rows = SecData::get().db.rows;
		if(wxMessageBox(wxString::Format("Remove the %s spawn at %d,%d,%d?",
		                                 wxstr(SecData::get().nameForRace(rows[row].race)),
		                                 rows[row].x, rows[row].y, rows[row].z),
		                "Remove spawn", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
		rows.erase(rows.begin() + row);
		SecData::get().reindexSpawns();
		Rebuild();
	});
	goto_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnGoto(); });
	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_CLOSE) EndModal(wxID_CLOSE);
		else e.Skip();
	});

	Rebuild();
	Centre();
}

int SecSpawnBrowserDialog::selectedRow() const
{
	long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if(sel < 0 || sel >= (long)shown.size()) return -1;
	return (int)shown[sel];
}

void SecSpawnBrowserDialog::Rebuild()
{
	const wxString filter = filter_ctrl->GetValue().Lower();
	SecData& data = SecData::get();
	shown.clear();

	for(size_t i = 0; i < data.db.rows.size(); ++i) {
		const secmon::SpawnRow& r = data.db.rows[i];
		if(!filter.empty()) {
			const wxString hay = wxString::Format("%d %s %d,%d,%d", r.race,
			                                      wxstr(data.nameForRace(r.race)),
			                                      r.x, r.y, r.z).Lower();
			if(!hay.Contains(filter)) continue;
		}
		shown.push_back(i);
	}

	// The world has ~22,000 rows; filling the control with all of them is
	// slow and useless. Show a window of them and let the filter do the work.
	static const size_t kMaxShown = 2000;
	const size_t matched = shown.size();
	if(shown.size() > kMaxShown) shown.resize(kMaxShown);

	list->Freeze();
	list->DeleteAllItems();
	for(size_t n = 0; n < shown.size(); ++n) {
		const secmon::SpawnRow& r = data.db.rows[shown[n]];
		long row = list->InsertItem((long)n, wxString::Format("%d", r.race));
		list->SetItem(row, 1, wxstr(data.nameForRace(r.race)));
		list->SetItem(row, 2, wxString::Format("%d", r.x));
		list->SetItem(row, 3, wxString::Format("%d", r.y));
		list->SetItem(row, 4, wxString::Format("%d", r.z));
		list->SetItem(row, 5, wxString::Format("%d", r.amount));
		list->SetItem(row, 6, wxString::Format("%d", r.radius));
		list->SetItem(row, 7, wxString::Format("%d", r.regen));
	}
	list->Thaw();
	if(matched > shown.size())
		count_label->SetLabel(wxString::Format(
			"showing %zu of %zu matching rows (%zu in the world) - narrow the filter to see the rest",
			shown.size(), matched, data.db.rows.size()));
	else
		count_label->SetLabel(wxString::Format("%zu of %zu spawn rows",
		                                       matched, data.db.rows.size()));
}

void SecSpawnBrowserDialog::OnGoto()
{
	int row = selectedRow();
	if(row < 0 || !g_gui.IsEditorOpen()) return;
	const secmon::SpawnRow& r = SecData::get().db.rows[row];
	g_gui.SetScreenCenterPosition(Position(r.x, r.y, r.z));
}

// ============================================================================
// SecTileSpawnDialog

SecTileSpawnDialog::SecTileSpawnDialog(wxWindow* parent, const Position& p)
	: wxDialog(parent, wxID_ANY,
	           wxString::Format("Spawns at %d, %d, %d", p.x, p.y, p.z),
	           wxDefaultPosition, wxSize(560, 340), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  pos(p)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	top->Add(new wxStaticText(this, wxID_ANY,
		"A tile may carry several races. Each line is one row of monster.db."),
		0, wxALL, 8);

	list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                      wxLC_REPORT | wxLC_SINGLE_SEL);
	list->AppendColumn("Race", wxLIST_FORMAT_RIGHT, 60);
	list->AppendColumn("Monster", wxLIST_FORMAT_LEFT, 200);
	list->AppendColumn("Amount", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Radius", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Respawn", wxLIST_FORMAT_RIGHT, 80);
	top->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* add = new wxButton(this, wxID_ANY, "Add");
	wxButton* edit = new wxButton(this, wxID_ANY, "Edit");
	wxButton* remove = new wxButton(this, wxID_ANY, "Remove");
	buttons->Add(add, 0, wxRIGHT, 4);
	buttons->Add(edit, 0, wxRIGHT, 4);
	buttons->Add(remove);
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(this, wxID_CLOSE, "Close"));
	top->Add(buttons, 0, wxEXPAND | wxALL, 8);
	SetSizer(top);

	add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		secmon::SpawnRow r;
		r.x = pos.x; r.y = pos.y; r.z = pos.z;
		r.radius = 50; r.amount = 1; r.regen = 600;
		if(!SecData::get().monsters.empty()) r.race = SecData::get().monsters.begin()->first;
		SecSpawnRowDialog dlg(this, r, false);
		if(dlg.ShowModal() != wxID_OK) return;
		r.id = -1;
		r.x = pos.x; r.y = pos.y; r.z = pos.z;
		SecData::get().db.rows.push_back(r);
		SecData::get().reindexSpawns();
		Rebuild();
	});
	edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if(sel < 0 || sel >= (long)shown.size()) return;
		SecSpawnRowDialog dlg(this, SecData::get().db.rows[shown[sel]], false);
		if(dlg.ShowModal() == wxID_OK) { SecData::get().reindexSpawns(); Rebuild(); }
	});
	remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if(sel < 0 || sel >= (long)shown.size()) return;
		std::vector<secmon::SpawnRow>& rows = SecData::get().db.rows;
		rows.erase(rows.begin() + shown[sel]);
		SecData::get().reindexSpawns();
		Rebuild();
	});
	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_CLOSE) EndModal(wxID_CLOSE);
		else e.Skip();
	});

	Rebuild();
	Centre();
}

void SecTileSpawnDialog::Rebuild()
{
	SecData& data = SecData::get();
	shown.clear();
	list->DeleteAllItems();

	const std::vector<size_t>* here = data.spawnsAt(pos);
	if(here) shown = *here;

	for(size_t n = 0; n < shown.size(); ++n) {
		const secmon::SpawnRow& r = data.db.rows[shown[n]];
		long row = list->InsertItem((long)n, wxString::Format("%d", r.race));
		list->SetItem(row, 1, wxstr(data.nameForRace(r.race)));
		list->SetItem(row, 2, wxString::Format("%d", r.amount));
		list->SetItem(row, 3, wxString::Format("%d", r.radius));
		list->SetItem(row, 4, wxString::Format("%d", r.regen));
	}
}

// ============================================================================
// SecNpcBrowserDialog

SecNpcBrowserDialog::SecNpcBrowserDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "NPCs", wxDefaultPosition, wxSize(700, 540),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	filter_ctrl = new wxTextCtrl(this, wxID_ANY);
	filter_ctrl->SetHint("Filter by name or file");
	top->Add(filter_ctrl, 0, wxEXPAND | wxALL, 8);

	list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                      wxLC_REPORT | wxLC_SINGLE_SEL);
	list->AppendColumn("Name", wxLIST_FORMAT_LEFT, 190);
	list->AppendColumn("File", wxLIST_FORMAT_LEFT, 190);
	list->AppendColumn("X", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Y", wxLIST_FORMAT_RIGHT, 70);
	list->AppendColumn("Z", wxLIST_FORMAT_RIGHT, 40);
	list->AppendColumn("Radius", wxLIST_FORMAT_RIGHT, 60);
	top->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	top->Add(new wxStaticText(this, wxID_ANY,
		"Moving an NPC rewrites only its Home line; the rest of the .npc file is untouched."),
		0, wxALL, 8);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* move = new wxButton(this, wxID_ANY, "Set home...");
	wxButton* here = new wxButton(this, wxID_ANY, "Move to view centre");
	wxButton* goto_button = new wxButton(this, wxID_ANY, "Go to");
	buttons->Add(move, 0, wxRIGHT, 4);
	buttons->Add(here, 0, wxRIGHT, 12);
	buttons->Add(goto_button);
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(this, wxID_CLOSE, "Close"));
	top->Add(buttons, 0, wxEXPAND | wxALL, 8);
	SetSizer(top);

	filter_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Rebuild(); });

	auto setHome = [this](int x, int y, int z) {
		int sel = selected();
		if(sel < 0) return;
		SecNpc& npc = SecData::get().npcs[sel];
		npc.x = x; npc.y = y; npc.z = z;
		npc.moved = true;
		SecData::get().reindexNpcs();
		Rebuild();
	};

	move->Bind(wxEVT_BUTTON, [this, setHome](wxCommandEvent&) {
		int sel = selected();
		if(sel < 0) return;
		const SecNpc& npc = SecData::get().npcs[sel];
		wxTextEntryDialog dlg(this, "Home position as x,y,z", "Set NPC home",
		                      wxString::Format("%d,%d,%d", npc.x, npc.y, npc.z));
		if(dlg.ShowModal() != wxID_OK) return;
		int x = 0, y = 0, z = 0;
		if(sscanf(dlg.GetValue().mb_str(), "%d , %d , %d", &x, &y, &z) != 3) {
			wxMessageBox("Expected three numbers, as in 32660,32112,8.", "Not understood",
			             wxOK | wxICON_ERROR, this);
			return;
		}
		setHome(x, y, z);
	});

	here->Bind(wxEVT_BUTTON, [this, setHome](wxCommandEvent&) {
		if(!g_gui.IsEditorOpen()) return;
		MapCanvas* canvas = g_gui.GetCurrentMapTab() ? g_gui.GetCurrentMapTab()->GetCanvas() : nullptr;
		if(!canvas) return;
		int x = 0, y = 0;
		canvas->GetScreenCenter(&x, &y);
		setHome(x, y, g_gui.GetCurrentFloor());
	});

	goto_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		int sel = selected();
		if(sel < 0 || !g_gui.IsEditorOpen()) return;
		const SecNpc& npc = SecData::get().npcs[sel];
		g_gui.SetScreenCenterPosition(Position(npc.x, npc.y, npc.z));
	});

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_CLOSE) EndModal(wxID_CLOSE);
		else e.Skip();
	});

	Rebuild();
	Centre();
}

int SecNpcBrowserDialog::selected() const
{
	long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if(sel < 0 || sel >= (long)shown.size()) return -1;
	return (int)shown[sel];
}

void SecNpcBrowserDialog::Rebuild()
{
	const wxString filter = filter_ctrl->GetValue().Lower();
	const std::vector<SecNpc>& npcs = SecData::get().npcs;
	shown.clear();
	list->Freeze();
	list->DeleteAllItems();
	for(size_t i = 0; i < npcs.size(); ++i) {
		const wxString name = wxstr(npcs[i].name);
		const wxString file = wxstr(npcs[i].file);
		if(!filter.empty() && !name.Lower().Contains(filter) && !file.Lower().Contains(filter))
			continue;
		long row = list->InsertItem((long)shown.size(), npcs[i].moved ? name + " *" : name);
		list->SetItem(row, 1, file);
		list->SetItem(row, 2, wxString::Format("%d", npcs[i].x));
		list->SetItem(row, 3, wxString::Format("%d", npcs[i].y));
		list->SetItem(row, 4, wxString::Format("%d", npcs[i].z));
		list->SetItem(row, 5, wxString::Format("%d", npcs[i].radius));
		shown.push_back(i);
	}
	list->Thaw();
}

// ============================================================================
// SecRaidBrowserDialog

SecRaidBrowserDialog::SecRaidBrowserDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Raids", wxDefaultPosition, wxSize(820, 540),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);

	raid_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(230, -1));
	body->Add(raid_list, 0, wxEXPAND | wxALL, 8);

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	info_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	right->Add(info_label, 0, wxBOTTOM, 6);

	point_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                            wxLC_REPORT | wxLC_SINGLE_SEL);
	point_list->AppendColumn("Wave", wxLIST_FORMAT_RIGHT, 50);
	point_list->AppendColumn("Monster", wxLIST_FORMAT_LEFT, 160);
	point_list->AppendColumn("X", wxLIST_FORMAT_RIGHT, 70);
	point_list->AppendColumn("Y", wxLIST_FORMAT_RIGHT, 70);
	point_list->AppendColumn("Z", wxLIST_FORMAT_RIGHT, 40);
	point_list->AppendColumn("Count", wxLIST_FORMAT_RIGHT, 70);
	point_list->AppendColumn("Spread", wxLIST_FORMAT_RIGHT, 60);
	right->Add(point_list, 1, wxEXPAND);
	body->Add(right, 1, wxEXPAND | wxALL, 8);
	top->Add(body, 1, wxEXPAND);

	top->Add(new wxStaticText(this, wxID_ANY,
		"Raids roll once at server boot. Only the Position of a wave can be moved here; "
		"the rest of the .evt file is left alone."), 0, wxLEFT | wxRIGHT, 12);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* move = new wxButton(this, wxID_ANY, "Set position...");
	wxButton* goto_button = new wxButton(this, wxID_ANY, "Go to");
	buttons->Add(move, 0, wxRIGHT, 12);
	buttons->Add(goto_button);
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(this, wxID_CLOSE, "Close"));
	top->Add(buttons, 0, wxEXPAND | wxALL, 8);
	SetSizer(top);

	raid_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
		current_raid = raid_list->GetSelection();
		RebuildPoints();
	});

	move->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		long sel = point_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if(current_raid < 0 || sel < 0) return;
		SecRaid& raid = SecData::get().raids[current_raid];
		SecRaidPoint& pt = raid.points[sel];
		wxTextEntryDialog dlg(this, "Wave position as x,y,z", "Set raid position",
		                      wxString::Format("%d,%d,%d", pt.x, pt.y, pt.z));
		if(dlg.ShowModal() != wxID_OK) return;
		int x = 0, y = 0, z = 0;
		if(sscanf(dlg.GetValue().mb_str(), "%d , %d , %d", &x, &y, &z) != 3) {
			wxMessageBox("Expected three numbers, as in 33170,32432,7.", "Not understood",
			             wxOK | wxICON_ERROR, this);
			return;
		}
		pt.x = x; pt.y = y; pt.z = z;
		raid.dirty = true;
		SecData::get().reindexRaids();
		RebuildPoints();
	});

	goto_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		long sel = point_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if(current_raid < 0 || sel < 0 || !g_gui.IsEditorOpen()) return;
		const SecRaidPoint& pt = SecData::get().raids[current_raid].points[sel];
		g_gui.SetScreenCenterPosition(Position(pt.x, pt.y, pt.z));
	});

	Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
		if(e.GetId() == wxID_CLOSE) EndModal(wxID_CLOSE);
		else e.Skip();
	});

	RebuildRaids();
	Centre();
}

void SecRaidBrowserDialog::RebuildRaids()
{
	raid_list->Clear();
	for(const SecRaid& raid : SecData::get().raids)
		raid_list->Append(wxString::Format("%s (%zu)", wxstr(raid.file), raid.points.size()));
	if(!SecData::get().raids.empty()) {
		raid_list->SetSelection(0);
		current_raid = 0;
		RebuildPoints();
	}
}

void SecRaidBrowserDialog::RebuildPoints()
{
	point_list->DeleteAllItems();
	if(current_raid < 0 || current_raid >= (int)SecData::get().raids.size()) return;
	const SecRaid& raid = SecData::get().raids[current_raid];
	SecData& data = SecData::get();

	wxString info = wxString::Format("%s", wxstr(raid.file));
	if(!raid.type.empty()) info << "   type " << wxstr(raid.type);
	if(raid.interval) info << wxString::Format("   interval %ld s (about every %.1f days)",
	                                           raid.interval, raid.interval / 86400.0);
	if(raid.dirty) info << "   [edited]";
	info_label->SetLabel(info);

	for(size_t i = 0; i < raid.points.size(); ++i) {
		const SecRaidPoint& pt = raid.points[i];
		long row = point_list->InsertItem((long)i, wxString::Format("%zu", i + 1));
		point_list->SetItem(row, 1, wxstr(data.nameForRace(pt.race)));
		point_list->SetItem(row, 2, wxString::Format("%d", pt.x));
		point_list->SetItem(row, 3, wxString::Format("%d", pt.y));
		point_list->SetItem(row, 4, wxString::Format("%d", pt.z));
		point_list->SetItem(row, 5, pt.countMin == pt.countMax
			? wxString::Format("%d", pt.countMin)
			: wxString::Format("%d-%d", pt.countMin, pt.countMax));
		point_list->SetItem(row, 6, wxString::Format("%d", pt.spread));
	}
	Layout();
}
