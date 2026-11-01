// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Auto Battle system (server side).
// Tabs mirror the client "Auto Battle" window: Hunt / Recovery / Buffs / Support / Loot / Restock / Safety.
#ifndef AUTOBATTLE_HPP
#define AUTOBATTLE_HPP

#include <memory>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>
#include <common/timer.hpp>

class map_session_data;

#define AB_MAX_ATTACK_SLOTS 16
#define AB_MAX_BUFF_SLOTS 8
#define AB_MAX_SUPPORT_SLOTS 4
#define AB_MAX_MOBS 20
#define AB_MAX_LOOT_IDS 30
#define AB_MAX_RESTOCK 8

struct s_ab_skill {
	uint16 skill_id = 0; ///< 0 = empty slot
	uint16 level = 0;
	t_tick next_try = 0; ///< runtime only
};

struct s_ab_restock {
	t_itemid nameid = 0;
	uint16 amount = 0; ///< amount to hold after restocking
};

enum e_ab_phase : uint8 {
	ABP_HUNT = 0,
	ABP_RETURNING, ///< waiting to warp back after restock
};

/// Per-player configuration + runtime state
struct s_autobattle {
	// ---- runtime ----
	bool running = false;
	int32 timer = INVALID_TIMER;
	e_ab_phase phase = ABP_HUNT;
	int32 target_id = 0;
	uint8 skill_cursor = 0;
	int16 anchor_m = -1, anchor_x = 0, anchor_y = 0; ///< hunting spot
	uint16 return_mapindex = 0; ///< used by restock
	int16 return_x = 0, return_y = 0;
	t_tick start_tick = 0, last_target_tick = 0, next_roam = 0, next_hp_item = 0, next_sp_item = 0,
		next_restock_check = 0, return_tick = 0, next_flee_item = 0, next_effect = 0, next_toggle = 0; // เติม next_toggle = 0 ไว้ตรงนี้.
	t_tick target_since = 0; ///< when the current target was acquired (stuck detection)
	uint32 target_hp = 0;
	uint32 engaged = 0; ///< number of targets engaged this session
	std::vector<std::pair<int32, t_tick>> ignore; ///< bl ids that are unreachable / not lootable, with expiry

	// ---- Hunt ----
	bool all_monsters = true;
	bool normal_attack = true;
	bool fight_back = false;
	std::vector<uint16> mobs;
	s_ab_skill attack[AB_MAX_ATTACK_SLOTS];

	// ---- Recovery ----
	t_itemid hp_item = 0, sp_item = 0;
	uint8 hp_pct = 50, sp_pct = 30;

	// ---- Buffs ----
	s_ab_skill buffs[AB_MAX_BUFF_SLOTS];

	// ---- Support (heals on self / party members) ----
	s_ab_skill support[AB_MAX_SUPPORT_SLOTS];
	uint8 support_hp_pct = 70;

	// ---- Loot ----
	bool loot = true;
	uint8 loot_range = 8;
	std::vector<t_itemid> loot_ids; ///< empty = pick up everything

	// ---- Restock (go to save point, buy from item DB price, come back) ----
	bool restock = false;
	uint16 restock_min = 10;
	std::vector<s_ab_restock> restock_items;

	// ---- Safety ----
	uint8 flee_hp_pct = 15; ///< 0 = disabled
	t_itemid flee_item = 601; ///< Fly Wing
	bool stop_on_player = false;
	bool stop_on_weight = true;
	uint8 weight_pct = 90;
	bool stop_no_item = true; ///< stop when HP/SP item runs out and restock is off
	uint16 idle_seconds = 90; ///< no target for this long -> use flee_item (0 = disabled)
};

// ---- Public API (can be called from atcommands, scripts or a client packet handler) ----
std::shared_ptr<s_autobattle> autobattle_get(map_session_data& sd);
/// @param from_item true when started by the Auto Battle item (script command "autobattle")
bool autobattle_start(map_session_data& sd, bool from_item = false);
void autobattle_stop(map_session_data& sd, const char* reason = nullptr);
bool autobattle_running(const map_session_data& sd);
/// Script command backend. op: 0 = stop, 1 = start, 2 = toggle. Rate limited. Returns true if running afterwards.
bool autobattle_script(map_session_data& sd, int32 op);
void autobattle_save(map_session_data& sd);
void autobattle_status(map_session_data& sd);
void autobattle_list_map_mobs(map_session_data& sd);
/// Entry point for the @autobattle command
bool autobattle_command(map_session_data& sd, const char* message);

void do_init_autobattle();
void do_final_autobattle();

#endif /* AUTOBATTLE_HPP */