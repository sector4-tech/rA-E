// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Auto Battle system (server side).
// Tabs mirror the client "Auto Battle" window: Hunt / Recovery / Buffs / Support / Loot / Restock / Safety.
#ifndef AUTOBATTLE_HPP
#define AUTOBATTLE_HPP

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>
#include <common/timer.hpp>

class map_session_data;

#define AB_MAX_ATTACK_SLOTS 16
#define AB_MAX_BUFF_SLOTS 8
#define AB_MAX_SUPPORT_SLOTS 4
#define AB_MAX_MOBS 20
#define AB_MAX_BLACKLIST 20
#define AB_MAX_LOOT_IDS 30
#define AB_MAX_RESTOCK 8
#define AB_MAX_BUFFITEMS 6

/// Extra condition for using an attack skill
enum e_ab_cond : uint8 {
	ABC_ALWAYS = 0,
	ABC_MOB_HP_ABOVE = 1, ///< only while the target monster has more than cond_val % HP
	ABC_MOB_HP_BELOW = 2, ///< only while the target monster has less than cond_val % HP
};

/// Skill categories used by the setup dialog
enum e_ab_kind : uint8 {
	ABK_ATTACK = 1,
	ABK_BUFF = 2,
	ABK_SUPPORT = 3,
};

struct s_ab_skill {
	uint16 skill_id = 0; ///< 0 = empty slot
	uint16 level = 0;
	uint8 cond = ABC_ALWAYS; ///< attack skills: e_ab_cond
	uint8 cond_val = 0; ///< attack skills: monster HP % used by cond
	uint8 fails = 0; ///< runtime only: consecutive failed attempts (back-off, avoids client error spam)
	t_tick next_try = 0; ///< runtime only
};

struct s_ab_restock {
	t_itemid nameid = 0;
	uint16 amount = 0; ///< amount to hold after restocking
};

/// Consumable that is used again every <interval> seconds (concentration potion, etc.)
struct s_ab_buffitem {
	t_itemid nameid = 0; ///< 0 = empty
	uint16 interval = 600; ///< seconds
	t_tick next_use = 0; ///< runtime only
};

enum e_ab_phase : uint8 {
	ABP_HUNT = 0,
	ABP_RETURNING, ///< waiting to warp back after restock
};

/// Per-player configuration + runtime state
struct s_autobattle {
	// ---- runtime ----
	bool loaded = false; ///< settings were read from the character variables
	bool running = false;
	int32 timer = INVALID_TIMER;
	e_ab_phase phase = ABP_HUNT;
	int32 target_id = 0;
	uint8 skill_cursor = 0;
	int16 anchor_m = -1, anchor_x = 0, anchor_y = 0; ///< hunting spot
	uint16 return_mapindex = 0; ///< used by restock
	int16 return_x = 0, return_y = 0;
	t_tick start_tick = 0, last_target_tick = 0, next_roam = 0, next_hp_item = 0, next_sp_item = 0,
		next_restock_check = 0, return_tick = 0, next_flee_item = 0, next_effect = 0, next_toggle = 0,
		next_boss_check = 0, next_sit_action = 0;
	t_tick target_since = 0; ///< when the current target was acquired (stuck detection)
	uint32 target_hp = 0;
	uint32 engaged = 0; ///< number of targets engaged this session
	bool sat_by_ab = false; ///< we sat down for resting (so we stand up again)
	uint16 hunt_mapindex = 0; ///< map where the hunt started (0 = none yet)
	int16 hunt_x = 0, hunt_y = 0;
	uint8 return_tries = 0; ///< automatic returns to the hunting map (reset after a minute)
	t_tick return_tries_reset = 0;
	std::string last_stop; ///< why the last session ended (shown by @ab status)
	std::vector<std::pair<int32, t_tick>> ignore; ///< bl ids that are unreachable / not lootable, with expiry

	// lists prepared for the setup dialog (autobattleinfo "skills" / "mapmobs")
	std::vector<std::pair<uint16, uint16>> cache_skills; ///< skill id, learned level
	std::vector<std::pair<uint16, int32>> cache_mobs; ///< mob id, count on the map
	std::vector<std::pair<t_itemid, int32>> cache_items; ///< item id, amount (inventory picker of the setup dialog)

	// ---- Hunt ----
	bool all_monsters = true;
	bool normal_attack = true;
	bool fight_back = false;
	std::vector<uint16> mobs; ///< monsters to hunt when "all_monsters" is off
	std::vector<uint16> blacklist; ///< monsters that are never attacked
	s_ab_skill attack[AB_MAX_ATTACK_SLOTS];

	// ---- Recovery ----
	t_itemid hp_item = 0, sp_item = 0;
	uint8 hp_pct = 50, sp_pct = 30;
	bool sit_rest = false; ///< sit down to regenerate when HP/SP are low and nothing attacks us
	uint8 sit_hp = 40, stand_hp = 80, sit_sp = 20, stand_sp = 60;

	// ---- Buffs ----
	s_ab_skill buffs[AB_MAX_BUFF_SLOTS];
	s_ab_buffitem buff_items[AB_MAX_BUFFITEMS];

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
	bool weight_warp = false; ///< when too heavy: go back to the save point (instead of just stopping)
	uint8 weight_pct = 90;
	bool stop_no_item = true; ///< stop when HP/SP item runs out and restock is off
	bool flee_mvp = false; ///< escape (flee item) when an MVP monster shows up
	bool flee_mini = false; ///< escape (flee item) when a mini boss shows up
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
/// Entry point for the @autobattle command and the "autobattlecmd" script command.
/// @param quiet no informational chat output (used by the NPC dialog)
bool autobattle_command(map_session_data& sd, const char* message, bool quiet = false);
/// Value lookup for the "autobattleinfo" script command (see doc/autobattle.md for the keys)
int64 autobattle_info(map_session_data& sd, const char* key, int32 a, int32 b);
/// String lookup for the "autobattlestr" script command
std::string autobattle_str(map_session_data& sd, const char* key, int32 a);

void do_init_autobattle();
void do_final_autobattle();

#endif /* AUTOBATTLE_HPP */
