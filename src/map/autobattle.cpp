// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "autobattle.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utilities.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "npc.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "status.hpp"
#include "unit.hpp"

// ---------------------------------------------------------------------------------------------
// Messages (English / Thai)
//
// The Thai texts are written as unicode escapes in this source file and converted to TIS-620 (cp874) at
// runtime, which is what the Thai client expects. Select the language with the battle config
// "autobattle_lang" (0 = English, 1 = Thai).
// ---------------------------------------------------------------------------------------------

static TIMER_FUNC(ab_timer);

/// UTF-8 -> TIS-620. Only ASCII and the Thai block (U+0E01..U+0E5B) are converted.
/// A text that is not valid UTF-8 is assumed to already be TIS-620 and is returned unchanged.
/// (The Thai texts are written as unicode escapes so this file can be saved in any encoding.)
static std::string ab_tis(const char* u8) {
	std::string out;
	const unsigned char* p = (const unsigned char*)u8;
	while (*p) {
		if (*p < 0x80) {
			out += (char)*p++;
		} else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
			uint32 cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			out += (cp >= 0x0E01 && cp <= 0x0E5B) ? (char)(cp - 0x0E00 + 0xA0) : '?';
			p += 3;
		} else {
			return u8; // not UTF-8: already TIS-620
		}
	}
	return out;
}

static bool ab_thai() {
	return battle_config.autobattle_lang == 1;
}

/// Picks the text of the configured language (already in the encoding the client needs)
static std::string ab_pick(const char* en, const char* th) {
	if (th && ab_thai())
		return ab_tis(th);
	return en ? en : "";
}

static void ab_msg(map_session_data& sd, const char* en, const char* th, ...) {
	std::string fmt = ab_pick(en, th);
	char buf[CHAT_SIZE_MAX];
	va_list ap;
	va_start(ap, th);
	vsnprintf(buf, sizeof(buf), fmt.c_str(), ap);
	va_end(ap);
	clif_displaymessage(sd.fd, buf);
}

static std::string ab_onoff(bool b) { return ab_pick(b ? "ON" : "OFF", b ? "\u0E40\u0E1B\u0E34\u0E14" : "\u0E1B\u0E34\u0E14"); }

static int32 ab_item_count(const map_session_data& sd, t_itemid nameid) {
	int32 total = 0;
	for (int32 i = 0; i < MAX_INVENTORY; i++)
		if (sd.inventory.u.items_inventory[i].nameid == nameid)
			total += sd.inventory.u.items_inventory[i].amount;
	return total;
}

static const char* ab_item_name(t_itemid nameid) {
	auto id = item_db.find(nameid);
	return id ? id->ename.c_str() : "Unknown";
}

static const char* ab_mob_name(uint16 mob_id) {
	auto mob = mob_db.find(mob_id);
	return mob ? mob->name.c_str() : "Unknown";
}

static bool ab_ignored(s_autobattle& ab, int32 id, t_tick tick) {
	for (auto it = ab.ignore.begin(); it != ab.ignore.end();) {
		if (DIFF_TICK(tick, it->second) >= 0)
			it = ab.ignore.erase(it);
		else if (it->first == id)
			return true;
		else
			++it;
	}
	return false;
}

static void ab_ignore(s_autobattle& ab, int32 id, t_tick tick, t_tick duration) {
	if (ab.ignore.size() > 200)
		ab.ignore.erase(ab.ignore.begin());
	ab.ignore.emplace_back(id, tick + duration);
}

// ---------------------------------------------------------------------------------------------
// Persistence
//
// The settings are stored in PERMANENT character variables (pc_setregistry / pc_readregistry),
// exactly like "set ab_flags, 5;" in a script would do. Every value is kept below 2^31.
// ---------------------------------------------------------------------------------------------

enum {
	ABF_ALL = 1 << 0,
	ABF_NORMAL = 1 << 1,
	ABF_FIGHTBACK = 1 << 2,
	ABF_LOOT = 1 << 3,
	ABF_RESTOCK = 1 << 4,
	ABF_STOPPLAYER = 1 << 5,
	ABF_STOPWEIGHT = 1 << 6,
	ABF_STOPNOITEM = 1 << 7,
	ABF_SITREST = 1 << 8,
	ABF_WEIGHTWARP = 1 << 9,
	ABF_FLEEMVP = 1 << 10,
	ABF_FLEEMINI = 1 << 11,
	ABF_SAVED = 1 << 30, ///< marks "settings were stored at least once"
};

static int64 ab_key(const char* name, int32 idx = 0) {
	return reference_uid(add_str(name), idx);
}

static void ab_set(map_session_data& sd, const char* name, int32 idx, int64 v) {
	pc_setregistry(&sd, ab_key(name, idx), v);
}

static int64 ab_get(map_session_data& sd, const char* name, int32 idx = 0) {
	return pc_readregistry(&sd, ab_key(name, idx));
}

static void ab_save_slots(map_session_data& sd, const char* name, const char* condname, const s_ab_skill* slots, int32 n) {
	for (int32 i = 0; i < n; i++) {
		ab_set(sd, name, i, slots[i].skill_id ? (((int64)slots[i].skill_id << 16) | slots[i].level) : 0);
		if (condname)
			ab_set(sd, condname, i, slots[i].skill_id ? ((int64)slots[i].cond | ((int64)slots[i].cond_val << 8)) : 0);
	}
}

static void ab_load_slots(map_session_data& sd, const char* name, const char* condname, s_ab_skill* slots, int32 n) {
	for (int32 i = 0; i < n; i++) {
		int64 v = ab_get(sd, name, i);
		slots[i] = s_ab_skill();
		slots[i].skill_id = (uint16)(v >> 16);
		slots[i].level = (uint16)(v & 0xFFFF);
		if (condname && slots[i].skill_id) {
			int64 c = ab_get(sd, condname, i);
			slots[i].cond = (uint8)(c & 0xFF);
			slots[i].cond_val = (uint8)((c >> 8) & 0xFF);
			if (slots[i].cond > ABC_MOB_HP_BELOW)
				slots[i].cond = ABC_ALWAYS;
		}
	}
}

void autobattle_save(map_session_data& sd) {
	if (!sd.autobattle || !sd.vars_ok)
		return;
	const s_autobattle& ab = *sd.autobattle;
	int64 flags = ABF_SAVED;
	if (ab.all_monsters) flags |= ABF_ALL;
	if (ab.normal_attack) flags |= ABF_NORMAL;
	if (ab.fight_back) flags |= ABF_FIGHTBACK;
	if (ab.loot) flags |= ABF_LOOT;
	if (ab.restock) flags |= ABF_RESTOCK;
	if (ab.stop_on_player) flags |= ABF_STOPPLAYER;
	if (ab.stop_on_weight) flags |= ABF_STOPWEIGHT;
	if (ab.stop_no_item) flags |= ABF_STOPNOITEM;
	if (ab.sit_rest) flags |= ABF_SITREST;
	if (ab.weight_warp) flags |= ABF_WEIGHTWARP;
	if (ab.flee_mvp) flags |= ABF_FLEEMVP;
	if (ab.flee_mini) flags |= ABF_FLEEMINI;
	ab_set(sd, "ab_flags", 0, flags);
	ab_set(sd, "ab_hpitem", 0, ab.hp_item);
	ab_set(sd, "ab_spitem", 0, ab.sp_item);
	ab_set(sd, "ab_fleeitem", 0, ab.flee_item ? ab.flee_item : -1);
	ab_set(sd, "ab_pct1", 0, (int64)ab.hp_pct | ((int64)ab.sp_pct << 8) | ((int64)ab.support_hp_pct << 16) | ((int64)ab.flee_hp_pct << 24));
	ab_set(sd, "ab_pct2", 0, (int64)ab.weight_pct | ((int64)ab.loot_range << 8));
	ab_set(sd, "ab_pct3", 0, (int64)ab.sit_hp | ((int64)ab.stand_hp << 8) | ((int64)ab.sit_sp << 16) | ((int64)ab.stand_sp << 24));
	ab_set(sd, "ab_misc", 0, (int64)ab.restock_min | ((int64)ab.idle_seconds << 16) | (1LL << 30));
	ab_save_slots(sd, "ab_atk", "ab_atkc", ab.attack, AB_MAX_ATTACK_SLOTS);
	ab_save_slots(sd, "ab_buff", nullptr, ab.buffs, AB_MAX_BUFF_SLOTS);
	ab_save_slots(sd, "ab_sup", nullptr, ab.support, AB_MAX_SUPPORT_SLOTS);
	for (int32 i = 0; i < AB_MAX_MOBS; i++)
		ab_set(sd, "ab_mob", i, i < (int32)ab.mobs.size() ? ab.mobs[i] : 0);
	for (int32 i = 0; i < AB_MAX_BLACKLIST; i++)
		ab_set(sd, "ab_bl", i, i < (int32)ab.blacklist.size() ? ab.blacklist[i] : 0);
	for (int32 i = 0; i < AB_MAX_LOOT_IDS; i++)
		ab_set(sd, "ab_loot", i, i < (int32)ab.loot_ids.size() ? ab.loot_ids[i] : 0);
	for (int32 i = 0; i < AB_MAX_RESTOCK; i++) {
		bool has = i < (int32)ab.restock_items.size();
		ab_set(sd, "ab_rsid", i, has ? ab.restock_items[i].nameid : 0);
		ab_set(sd, "ab_rsamt", i, has ? ab.restock_items[i].amount : 0);
	}
	for (int32 i = 0; i < AB_MAX_BUFFITEMS; i++) {
		ab_set(sd, "ab_bi", i, ab.buff_items[i].nameid);
		ab_set(sd, "ab_biv", i, ab.buff_items[i].nameid ? ab.buff_items[i].interval : 0);
	}
}

static void ab_load(map_session_data& sd, s_autobattle& ab) {
	ab.mobs.clear();
	ab.blacklist.clear();
	ab.loot_ids.clear();
	ab.restock_items.clear();

	int64 flags = ab_get(sd, "ab_flags");
	if (!(flags & ABF_SAVED))
		return; // never configured: keep the defaults
	ab.all_monsters = flags & ABF_ALL;
	ab.normal_attack = flags & ABF_NORMAL;
	ab.fight_back = flags & ABF_FIGHTBACK;
	ab.loot = flags & ABF_LOOT;
	ab.restock = flags & ABF_RESTOCK;
	ab.stop_on_player = flags & ABF_STOPPLAYER;
	ab.stop_on_weight = flags & ABF_STOPWEIGHT;
	ab.stop_no_item = flags & ABF_STOPNOITEM;
	ab.sit_rest = flags & ABF_SITREST;
	ab.weight_warp = flags & ABF_WEIGHTWARP;
	ab.flee_mvp = flags & ABF_FLEEMVP;
	ab.flee_mini = flags & ABF_FLEEMINI;
	ab.hp_item = (t_itemid)ab_get(sd, "ab_hpitem");
	ab.sp_item = (t_itemid)ab_get(sd, "ab_spitem");
	int64 flee = ab_get(sd, "ab_fleeitem");
	ab.flee_item = flee > 0 ? (t_itemid)flee : 0;
	int64 pct = ab_get(sd, "ab_pct1");
	ab.hp_pct = pct & 0xFF;
	ab.sp_pct = (pct >> 8) & 0xFF;
	ab.support_hp_pct = (pct >> 16) & 0xFF;
	ab.flee_hp_pct = (pct >> 24) & 0xFF;
	int64 pct2 = ab_get(sd, "ab_pct2");
	ab.weight_pct = pct2 & 0xFF;
	ab.loot_range = (pct2 >> 8) & 0xFF;
	int64 pct3 = ab_get(sd, "ab_pct3");
	if (pct3) {
		ab.sit_hp = pct3 & 0xFF;
		ab.stand_hp = (pct3 >> 8) & 0xFF;
		ab.sit_sp = (pct3 >> 16) & 0xFF;
		ab.stand_sp = (pct3 >> 24) & 0xFF;
	}
	int64 misc = ab_get(sd, "ab_misc");
	if (misc & (1LL << 30)) {
		ab.restock_min = misc & 0xFFFF;
		ab.idle_seconds = (misc >> 16) & 0x3FFF; // bit 30 is the "saved" marker, not part of this value
		if (ab.idle_seconds > 3600)
			ab.idle_seconds = 3600;
	}
	if (ab.loot_range < 1 || ab.loot_range > 14) ab.loot_range = 8;
	if (ab.weight_pct < 10 || ab.weight_pct > 100) ab.weight_pct = 90;
	if (ab.support_hp_pct < 1 || ab.support_hp_pct > 100) ab.support_hp_pct = 70;
	ab_load_slots(sd, "ab_atk", "ab_atkc", ab.attack, AB_MAX_ATTACK_SLOTS);
	ab_load_slots(sd, "ab_buff", nullptr, ab.buffs, AB_MAX_BUFF_SLOTS);
	ab_load_slots(sd, "ab_sup", nullptr, ab.support, AB_MAX_SUPPORT_SLOTS);
	for (int32 i = 0; i < AB_MAX_MOBS; i++)
		if (uint16 v = (uint16)ab_get(sd, "ab_mob", i))
			ab.mobs.push_back(v);
	for (int32 i = 0; i < AB_MAX_BLACKLIST; i++)
		if (uint16 v = (uint16)ab_get(sd, "ab_bl", i))
			ab.blacklist.push_back(v);
	for (int32 i = 0; i < AB_MAX_LOOT_IDS; i++)
		if (t_itemid v = (t_itemid)ab_get(sd, "ab_loot", i))
			ab.loot_ids.push_back(v);
	for (int32 i = 0; i < AB_MAX_RESTOCK; i++) {
		t_itemid id = (t_itemid)ab_get(sd, "ab_rsid", i);
		if (id) {
			s_ab_restock r;
			r.nameid = id;
			r.amount = (uint16)ab_get(sd, "ab_rsamt", i);
			ab.restock_items.push_back(r);
		}
	}
	for (int32 i = 0; i < AB_MAX_BUFFITEMS; i++) {
		ab.buff_items[i] = s_ab_buffitem();
		ab.buff_items[i].nameid = (t_itemid)ab_get(sd, "ab_bi", i);
		int64 iv = ab_get(sd, "ab_biv", i);
		if (ab.buff_items[i].nameid && iv > 0)
			ab.buff_items[i].interval = (uint16)iv;
	}
}

std::shared_ptr<s_autobattle> autobattle_get(map_session_data& sd) {
	if (!sd.autobattle)
		sd.autobattle = std::make_shared<s_autobattle>();
	// The character variables only exist after the char-server sent them (vars_ok)
	if (!sd.autobattle->loaded && sd.vars_ok) {
		ab_load(sd, *sd.autobattle);
		sd.autobattle->loaded = true;
	}
	return sd.autobattle;
}

bool autobattle_running(const map_session_data& sd) {
	return sd.autobattle && sd.autobattle->running;
}

// ---------------------------------------------------------------------------------------------
// Skill / item usage
// ---------------------------------------------------------------------------------------------

enum e_ab_skill_result { ABS_NO, ABS_USED, ABS_TOOFAR };

/// Checks the requirements that would make the server send a "skill failed" message to the client
/// (weapon, ammo, catalysts, spirit spheres, zeny, state), so that we never spam the player.
static bool ab_skill_requirements_ok(map_session_data& sd, uint16 id, uint16 lv) {
	s_skill_condition req = skill_get_requirement(&sd, id, lv);

	if (req.sp > 0 && (int32)sd.battle_status.sp < req.sp)
		return false;
	if (req.hp > 0 && (int32)sd.battle_status.hp <= req.hp)
		return false;
	if (req.zeny > 0 && sd.status.zeny < (uint32)req.zeny)
		return false;
	if (req.spiritball > 0 && sd.spiritball < req.spiritball)
		return false;
	if (req.weapon && !(req.weapon & (1 << sd.status.weapon)))
		return false;
	if (req.ammo) {
		int16 idx = sd.equip_index[EQI_AMMO];
		if (idx < 0 || !sd.inventory_data[idx] || !(req.ammo & (1 << sd.inventory_data[idx]->subtype))
			|| sd.inventory.u.items_inventory[idx].amount < req.ammo_qty)
			return false;
	}
	for (int32 i = 0; i < MAX_SKILL_ITEM_REQUIRE; i++)
		if (req.itemid[i] && ab_item_count(sd, req.itemid[i]) < req.amount[i])
			return false;
	switch (req.state) {
		case ST_CART: if (!pc_iscarton(&sd)) return false; break;
		case ST_FALCON: if (!pc_isfalcon(&sd)) return false; break;
		case ST_RIDING: if (!pc_isriding(&sd)) return false; break;
		case ST_SHIELD: if (sd.status.shield <= 0) return false; break;
		default: break;
	}
	return true;
}

/// Tries to use one configured skill. 'target' may be nullptr for self skills.
static e_ab_skill_result ab_use_skill(map_session_data& sd, s_ab_skill& sk, block_list* target, t_tick tick, int32* need_range = nullptr) {
	if (!sk.skill_id || DIFF_TICK(sk.next_try, tick) > 0)
		return ABS_NO;
	if (sd.ud.skilltimer != INVALID_TIMER || DIFF_TICK(sd.ud.canact_tick, tick) > 0)
		return ABS_NO;

	uint16 id = sk.skill_id;
	uint16 learned = pc_checkskill(&sd, id);
	uint16 lv = std::min<uint16>(sk.level ? sk.level : learned, learned);
	if (lv < 1) {
		sk.next_try = tick + 10000; // not learned (yet)
		return ABS_NO;
	}
	if (sd.scd.find(id) != sd.scd.end())
		return ABS_NO; // cooldown running
	if (!ab_skill_requirements_ok(sd, id, lv)) {
		sk.next_try = tick + 1000; // wait for SP / ammo / catalyst; stays silent
		return ABS_NO;
	}

	int32 inf = skill_get_inf(id);
	int32 result = 0;

	if (inf & INF_SELF_SKILL) {
		sc_type sct = skill_get_sc(id);
		if (sct != SC_NONE && sd.sc.getSCE(sct))
			return ABS_NO; // already active
		// Area skill centred on the caster (Magnum Break...): only when the enemy is inside the area
		if (sct == SC_NONE && target && target != &sd) {
			int32 splash = std::max(1, skill_get_splash(id, lv));
			if ((int32)distance_bl(&sd, target) > splash) {
				if (need_range) *need_range = splash;
				return ABS_TOOFAR;
			}
		}
		result = unit_skilluse_id(&sd, sd.id, id, lv);
	} else if (inf & INF_GROUND_SKILL) {
		if (!target)
			return ABS_NO;
		int32 range = skill_get_range2(&sd, id, lv, true);
		if ((int32)distance_bl(&sd, target) > range) {
			if (need_range) *need_range = std::max(1, range - 1);
			return ABS_TOOFAR;
		}
		result = unit_skilluse_pos(&sd, target->x, target->y, id, lv);
	} else {
		if (!target)
			target = &sd;
		if (target != &sd) {
			int32 range = skill_get_range2(&sd, id, lv, true);
			if ((int32)distance_bl(&sd, target) > range) {
				if (need_range) *need_range = std::max(1, range - 1);
				return ABS_TOOFAR;
			}
		} else {
			sc_type sct = skill_get_sc(id);
			if (sct != SC_NONE && sd.sc.getSCE(sct) && !(inf & INF_ATTACK_SKILL))
				return ABS_NO;
		}
		result = unit_skilluse_id(&sd, target->id, id, lv);
	}

	if (!result) {
		// Something blocked it (status, map restriction...). Back off: 1.5s, 3s, 6s ... up to 48s
		if (sk.fails < 250)
			sk.fails++;
		sk.next_try = tick + (1500 << std::min<int32>(sk.fails - 1, 5));
		return ABS_NO;
	}
	sk.fails = 0;
	// Don't spam skills faster than once per 300ms
	sk.next_try = tick + 300;
	return ABS_USED;
}

static bool ab_use_item(map_session_data& sd, t_itemid nameid, t_tick tick) {
	if (!nameid || DIFF_TICK(sd.canuseitem_tick, tick) > 0)
		return false;
	int16 idx = pc_search_inventory(&sd, nameid);
	if (idx < 0)
		return false;
	return pc_useitem(&sd, idx) != 0;
}

/// Is the "extra condition" of an attack skill fulfilled for this monster?
static bool ab_cond_ok(const s_ab_skill& sk, const mob_data& md) {
	if (sk.cond == ABC_ALWAYS || !md.status.max_hp)
		return true;
	int32 pct = (int32)((int64)md.status.hp * 100 / md.status.max_hp);
	if (sk.cond == ABC_MOB_HP_ABOVE)
		return pct > sk.cond_val;
	return pct < sk.cond_val;
}

// ---------------------------------------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------------------------------------

/// Hat effect (stays on the character) while Auto Battle is running; the id comes from "autobattle_hat_effect"
static void ab_hat_effect(map_session_data& sd, bool on) {
#if PACKETVER_MAIN_NUM >= 20150507 || PACKETVER_RE_NUM >= 20150429 || defined(PACKETVER_ZERO)
	int16 id = (int16)battle_config.autobattle_hat_effect;
	if (id <= HAT_EF_MIN || id >= HAT_EF_MAX)
		return;
	if (on) {
		if (!rathena::util::vector_exists(sd.ud.hatEffects, id))
			sd.ud.hatEffects.push_back(id);
		clif_hat_effect_single(sd, id, true);
	} else if (rathena::util::vector_erase_if_exists(sd.ud.hatEffects, id))
		clif_hat_effect_single(sd, id, false);
#endif
}

bool autobattle_start(map_session_data& sd, bool from_item) {
	if (!battle_config.autobattle_enable) {
		ab_msg(sd, "Auto Battle is disabled on this server.", "\u0E40\u0E0B\u0E34\u0E23\u0E4C\u0E1F\u0E40\u0E27\u0E2D\u0E23\u0E4C\u0E19\u0E35\u0E49\u0E1B\u0E34\u0E14\u0E23\u0E30\u0E1A\u0E1A Auto Battle");
		return false;
	}
	if (sd.sc.getSCE(SC_JAILED)) {
		ab_msg(sd, "Auto Battle cannot be used in jail.", "\u0E44\u0E21\u0E48\u0E2D\u0E19\u0E38\u0E0D\u0E32\u0E15\u0E43\u0E2B\u0E49\u0E43\u0E0A\u0E49 Auto Battle \u0E02\u0E13\u0E30\u0E16\u0E39\u0E01\u0E02\u0E31\u0E07");
		return false;
	}
	if (pc_isdead(&sd)) {
		ab_msg(sd, "You cannot start Auto Battle while dead.", "\u0E44\u0E21\u0E48\u0E2A\u0E32\u0E21\u0E32\u0E23\u0E16\u0E40\u0E23\u0E34\u0E48\u0E21 Auto Battle \u0E02\u0E13\u0E30\u0E15\u0E32\u0E22\u0E44\u0E14\u0E49");
		return false;
	}
	if (map_flag_vs(sd.m)) {
		ab_msg(sd, "Auto Battle cannot be used on PvP / WoE / Battleground maps.", "\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48 PvP / WoE / Battleground \u0E44\u0E21\u0E48\u0E2D\u0E19\u0E38\u0E0D\u0E32\u0E15\u0E43\u0E2B\u0E49\u0E43\u0E0A\u0E49 Auto Battle");
		return false;
	}
	if (sd.state.vending || sd.state.buyingstore || sd.state.autotrade) {
		ab_msg(sd, "Auto Battle cannot be used while a shop is open.", "\u0E44\u0E21\u0E48\u0E2A\u0E32\u0E21\u0E32\u0E23\u0E16\u0E43\u0E0A\u0E49 Auto Battle \u0E02\u0E13\u0E30\u0E40\u0E1B\u0E34\u0E14\u0E23\u0E49\u0E32\u0E19\u0E04\u0E49\u0E32");
		return false;
	}
	if (!sd.vars_ok) {
		ab_msg(sd, "Your data is still loading, please try again.", "\u0E02\u0E49\u0E2D\u0E21\u0E39\u0E25\u0E15\u0E31\u0E27\u0E25\u0E30\u0E04\u0E23\u0E22\u0E31\u0E07\u0E42\u0E2B\u0E25\u0E14\u0E44\u0E21\u0E48\u0E40\u0E2A\u0E23\u0E47\u0E08 \u0E01\u0E23\u0E38\u0E13\u0E32\u0E25\u0E2D\u0E07\u0E43\u0E2B\u0E21\u0E48\u0E2D\u0E35\u0E01\u0E04\u0E23\u0E31\u0E49\u0E07");
		return false;
	}
	if (battle_config.autobattle_item_only && !from_item) {
		ab_msg(sd, "Use the Auto Battle item to start Auto Battle.", "\u0E01\u0E23\u0E38\u0E13\u0E32\u0E43\u0E0A\u0E49\u0E44\u0E2D\u0E40\u0E17\u0E21 Auto Battle \u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E40\u0E23\u0E34\u0E48\u0E21\u0E23\u0E30\u0E1A\u0E1A");
		return false;
	}
	auto ab = autobattle_get(sd);
	if (ab->running) {
		ab_msg(sd, "Auto Battle is already running.", "Auto Battle \u0E17\u0E33\u0E07\u0E32\u0E19\u0E2D\u0E22\u0E39\u0E48\u0E41\u0E25\u0E49\u0E27");
		return false;
	}
	bool has_attack = ab->normal_attack;
	for (const auto& sk : ab->attack)
		if (sk.skill_id)
			has_attack = true;
	if (!has_attack) {
		ab_msg(sd, "Enable normal attack or add an attack skill first (type @ab).", "\u0E01\u0E23\u0E38\u0E13\u0E32\u0E40\u0E1B\u0E34\u0E14 '\u0E42\u0E08\u0E21\u0E15\u0E35\u0E18\u0E23\u0E23\u0E21\u0E14\u0E32' \u0E2B\u0E23\u0E37\u0E2D\u0E40\u0E1E\u0E34\u0E48\u0E21\u0E2A\u0E01\u0E34\u0E25\u0E42\u0E08\u0E21\u0E15\u0E35\u0E01\u0E48\u0E2D\u0E19 (\u0E1E\u0E34\u0E21\u0E1E\u0E4C @ab)");
		return false;
	}
	if (!ab->all_monsters && ab->mobs.empty() && !ab->fight_back) {
		ab_msg(sd, "Select target monsters first (type @ab).", "\u0E01\u0E23\u0E38\u0E13\u0E32\u0E40\u0E25\u0E37\u0E2D\u0E01\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C\u0E40\u0E1B\u0E49\u0E32\u0E2B\u0E21\u0E32\u0E22\u0E01\u0E48\u0E2D\u0E19 (\u0E1E\u0E34\u0E21\u0E1E\u0E4C @ab)");
		return false;
	}

	ab->running = true;
	ab->phase = ABP_HUNT;
	ab->target_id = 0;
	ab->engaged = 0;
	ab->sat_by_ab = false;
	ab->ignore.clear();
	ab->anchor_m = sd.m;
	ab->anchor_x = sd.x;
	ab->anchor_y = sd.y;
	ab->hunt_mapindex = 0; // recorded by the first tick
	ab->return_tries = 0;
	ab->start_tick = ab->last_target_tick = gettick();
	ab->next_roam = ab->next_hp_item = ab->next_sp_item = ab->next_flee_item = ab->next_boss_check = ab->next_sit_action = 0;
	ab->next_restock_check = ab->start_tick + 3000;
	for (auto& sk : ab->attack) sk.next_try = 0;
	for (auto& sk : ab->buffs) sk.next_try = 0;
	for (auto& sk : ab->support) sk.next_try = 0;
	for (auto& bi : ab->buff_items) bi.next_use = 0;

	if (ab->timer != INVALID_TIMER)
		delete_timer(ab->timer, ab_timer);
	ab->timer = add_timer(gettick() + battle_config.autobattle_interval, ab_timer, sd.id, 0);
	ab->next_effect = 0;
	sc_start(&sd, &sd, SC_AUTOBATTLE, 100, 1, INFINITE_TICK); // status icon + marker for other players
	ab_hat_effect(sd, true);
	ab_msg(sd, "Auto Battle started.", "\u0E40\u0E23\u0E34\u0E48\u0E21 Auto Battle \u0E41\u0E25\u0E49\u0E27");
	return true;
}

static void ab_stand(map_session_data& sd) {
	if (pc_issit(&sd) && pc_setstand(&sd, false)) {
		skill_sit(&sd, false);
		clif_standing(sd);
	}
}

static void ab_stop(map_session_data& sd, const char* en, const char* th) {
	if (!sd.autobattle)
		return;
	auto ab = sd.autobattle;
	if (ab->timer != INVALID_TIMER) {
		delete_timer(ab->timer, ab_timer);
		ab->timer = INVALID_TIMER;
	}
	bool was_running = ab->running;
	if (was_running)
		ab->last_stop = en ? en : "stopped by the player";
	ab->running = false;
	if (sd.sc.getSCE(SC_AUTOBATTLE))
		status_change_end(&sd, SC_AUTOBATTLE);
	ab_hat_effect(sd, false);
	ab->target_id = 0;
	ab->phase = ABP_HUNT;
	if (was_running) {
		if (sd.prev != nullptr) {
			unit_stop_attack(&sd);
			unit_stop_walking(&sd, USW_FIXPOS);
			if (ab->sat_by_ab)
				ab_stand(sd);
		}
		ab->sat_by_ab = false;
		if (sd.fd && session_isActive(sd.fd)) {
			if (en) {
				std::string reason = ab_pick(en, th);
				ab_msg(sd, "Auto Battle stopped: %s", "Auto Battle \u0E2B\u0E22\u0E38\u0E14\u0E17\u0E33\u0E07\u0E32\u0E19: %s", reason.c_str());
			} else
				ab_msg(sd, "Auto Battle stopped.", "\u0E2B\u0E22\u0E38\u0E14 Auto Battle \u0E41\u0E25\u0E49\u0E27");
		}
	}
}

void autobattle_stop(map_session_data& sd, const char* reason) {
	if (reason && !strcmp(reason, "you died."))
		ab_stop(sd, "you died.", "\u0E15\u0E31\u0E27\u0E25\u0E30\u0E04\u0E23\u0E15\u0E32\u0E22\u0E41\u0E25\u0E49\u0E27");
	else
		ab_stop(sd, reason, reason);
}

// ---------------------------------------------------------------------------------------------
// Safety
// ---------------------------------------------------------------------------------------------

static int32 ab_count_players_sub(block_list* bl, va_list ap) {
	map_session_data* me = va_arg(ap, map_session_data*);
	int32* count = va_arg(ap, int32*);
	map_session_data* other = (map_session_data*)bl;

	if (other == me || other->state.vending || other->state.buyingstore || other->state.autotrade)
		return 0;
	if (me->status.party_id && other->status.party_id == me->status.party_id)
		return 0;
	if (me->status.guild_id && other->status.guild_id == me->status.guild_id)
		return 0;
	(*count)++;
	return 0;
}

static int32 ab_boss_sub(block_list* bl, va_list ap) {
	s_autobattle* ab = va_arg(ap, s_autobattle*);
	int32* found = va_arg(ap, int32*);
	mob_data* md = (mob_data*)bl;

	if (md->special_state.ai || status_isdead(*md))
		return 0;
	e_mob_bosstype t = md->get_bosstype();
	if (t == BOSSTYPE_MVP && ab->flee_mvp)
		*found = 1;
	else if (t == BOSSTYPE_MINIBOSS && ab->flee_mini && !*found)
		*found = 2;
	return 0;
}

/// @return false when auto battle was stopped
static bool ab_safety(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	// Low HP: use escape item and stop
	if (ab.flee_hp_pct && (int64)sd.battle_status.hp * 100 < (int64)sd.battle_status.max_hp * ab.flee_hp_pct) {
		bool used = ab.flee_item && ab_use_item(sd, ab.flee_item, tick);
		// Try to heal first when we still have a potion and nothing to flee with
		if (!used && ab.hp_item && ab_use_item(sd, ab.hp_item, tick))
			return true;
		if (used)
			ab_stop(sd, "HP too low, escape item used.", "HP \u0E15\u0E48\u0E33\u0E40\u0E01\u0E34\u0E19\u0E44\u0E1B \u0E43\u0E0A\u0E49\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E2B\u0E19\u0E35\u0E41\u0E25\u0E49\u0E27");
		else
			ab_stop(sd, "HP too low.", "HP \u0E15\u0E48\u0E33\u0E40\u0E01\u0E34\u0E19\u0E44\u0E1B");
		return false;
	}
	if (ab.stop_on_player) {
		int32 count = 0;
		map_foreachinrange(ab_count_players_sub, &sd, AREA_SIZE, BL_PC, &sd, &count);
		if (count > 0) {
			ab_stop(sd, "another player is nearby.", "\u0E21\u0E35\u0E1C\u0E39\u0E49\u0E40\u0E25\u0E48\u0E19\u0E2D\u0E37\u0E48\u0E19\u0E2D\u0E22\u0E39\u0E48\u0E43\u0E01\u0E25\u0E49\u0E46");
			return false;
		}
	}
	if (ab.stop_on_weight && pc_getpercentweight(sd) >= ab.weight_pct && !ab.restock) {
		if (ab.weight_warp) {
			ab_stop(sd, "you are carrying too much, returned to the save point.", "\u0E19\u0E49\u0E33\u0E2B\u0E19\u0E31\u0E01\u0E40\u0E01\u0E34\u0E19 \u0E40\u0E14\u0E34\u0E19\u0E17\u0E32\u0E07\u0E01\u0E25\u0E31\u0E1A\u0E08\u0E38\u0E14\u0E40\u0E0B\u0E1F\u0E41\u0E25\u0E49\u0E27");
			pc_setpos_savepoint(sd);
		} else
			ab_stop(sd, "you are carrying too much.", "\u0E19\u0E49\u0E33\u0E2B\u0E19\u0E31\u0E01\u0E40\u0E01\u0E34\u0E19\u0E01\u0E33\u0E2B\u0E19\u0E14");
		return false;
	}
	if ((ab.flee_mvp || ab.flee_mini) && DIFF_TICK(tick, ab.next_boss_check) >= 0) {
		ab.next_boss_check = tick + 1000;
		int32 found = 0;
		map_foreachinrange(ab_boss_sub, &sd, battle_config.autobattle_search_range + 4, BL_MOB, &ab, &found);
		if (found) {
			if (ab.flee_item)
				ab_use_item(sd, ab.flee_item, tick);
			if (found == 1)
				ab_stop(sd, "an MVP monster appeared.", "\u0E1E\u0E1A\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C MVP");
			else
				ab_stop(sd, "a mini boss appeared.", "\u0E1E\u0E1A\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C Mini Boss");
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Recovery / resting / buffs / support
// ---------------------------------------------------------------------------------------------

static void ab_recovery(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	const status_data& st = sd.battle_status;
	if (ab.hp_item && DIFF_TICK(tick, ab.next_hp_item) >= 0
		&& (int64)st.hp * 100 < (int64)st.max_hp * ab.hp_pct) {
		if (ab_use_item(sd, ab.hp_item, tick))
			ab.next_hp_item = tick + 600;
	}
	if (ab.sp_item && DIFF_TICK(tick, ab.next_sp_item) >= 0 && st.max_sp
		&& (int64)st.sp * 100 < (int64)st.max_sp * ab.sp_pct) {
		if (ab_use_item(sd, ab.sp_item, tick))
			ab.next_sp_item = tick + 600;
	}
}

static int32 ab_threat_sub(block_list* bl, va_list ap) {
	map_session_data* me = va_arg(ap, map_session_data*);
	int32* count = va_arg(ap, int32*);
	mob_data* md = (mob_data*)bl;
	if (md->target_id == me->id && !status_isdead(*md))
		(*count)++;
	return 0;
}

/// Sit down to regenerate when HP / SP are low, stand up when they are restored or a monster attacks.
/// @return true while we are sitting (nothing else should be done)
static bool ab_rest(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	bool sitting = pc_issit(&sd);
	if (!ab.sit_rest) {
		if (sitting && ab.sat_by_ab) {
			ab_stand(sd);
			ab.sat_by_ab = false;
		}
		return false;
	}

	const status_data& st = sd.battle_status;
	int32 hp = st.max_hp ? (int32)((int64)st.hp * 100 / st.max_hp) : 100;
	int32 sp = st.max_sp ? (int32)((int64)st.sp * 100 / st.max_sp) : 100;
	int32 threats = 0;
	map_foreachinrange(ab_threat_sub, &sd, 12, BL_MOB, &sd, &threats);

	if (sitting) {
		bool healed = hp >= ab.stand_hp && sp >= ab.stand_sp;
		if (healed || threats > 0) {
			ab_stand(sd);
			ab.sat_by_ab = false;
			ab.next_sit_action = tick + 3000;
			return false;
		}
		return true;
	}
	if (threats > 0 || DIFF_TICK(tick, ab.next_sit_action) < 0)
		return false;
	if (hp >= ab.sit_hp && (sp >= ab.sit_sp || !st.max_sp))
		return false;
	if (sd.ud.skilltimer != INVALID_TIMER || sd.sc.opt1 || (sd.state.block_action & PCBLOCK_SITSTAND))
		return false;
	if (sd.sc.getSCE(SC_DANCING))
		return false;
	pc_setsit(&sd);
	skill_sit(&sd, true);
	clif_sitting(sd);
	ab.sat_by_ab = true;
	return true;
}

/// Consumables used on a timer (concentration potion, awakening potion...)
static void ab_buff_items(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	for (auto& bi : ab.buff_items) {
		if (!bi.nameid || DIFF_TICK(tick, bi.next_use) < 0)
			continue;
		if (ab_use_item(sd, bi.nameid, tick))
			bi.next_use = tick + (t_tick)bi.interval * 1000;
		else
			bi.next_use = tick + 15000; // none left (or still locked): check again later
		return; // one item per tick
	}
}

/// @return true if an action was started (we should wait for the next tick)
static bool ab_buffs(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	for (auto& sk : ab.buffs) {
		if (!sk.skill_id)
			continue;
		uint16 learned = pc_checkskill(&sd, sk.skill_id);
		uint16 lv = std::min<uint16>(sk.level ? sk.level : learned, learned);
		if (lv < 1)
			continue;
		sc_type sct = skill_get_sc(sk.skill_id);
		if (sct == SC_NONE) {
			// Unknown status: fall back to a time based re-cast
			if (DIFF_TICK(sk.next_try, tick) > 0)
				continue;
		} else if (sd.sc.getSCE(sct))
			continue;
		if (sd.state.warping)
			continue;
		if (ab_use_skill(sd, sk, &sd, tick) == ABS_USED) {
			if (sct == SC_NONE)
				sk.next_try = tick + std::max<int32>(10000, skill_get_time(sk.skill_id, lv) * 9 / 10);
			return true;
		}
	}
	return false;
}

struct ab_support_ctx {
	map_session_data* self;
	map_session_data* best;
	int32 best_pct;
	int32 threshold;
};

static int32 ab_support_sub(block_list* bl, va_list ap) {
	ab_support_ctx* ctx = va_arg(ap, ab_support_ctx*);
	map_session_data* psd = (map_session_data*)bl;

	if (psd != ctx->self && (!ctx->self->status.party_id || psd->status.party_id != ctx->self->status.party_id))
		return 0;
	if (pc_isdead(psd) || !psd->battle_status.max_hp)
		return 0;
	int32 pct = (int32)((int64)psd->battle_status.hp * 100 / psd->battle_status.max_hp);
	if (pct < ctx->threshold && pct < ctx->best_pct) {
		ctx->best = psd;
		ctx->best_pct = pct;
	}
	return 0;
}

static bool ab_support(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	bool any = false;
	for (const auto& sk : ab.support)
		if (sk.skill_id) { any = true; break; }
	if (!any)
		return false;

	ab_support_ctx ctx = { &sd, nullptr, 101, ab.support_hp_pct };
	map_foreachinrange(ab_support_sub, &sd, 9, BL_PC, &ctx);
	if (!ctx.best)
		return false;
	for (auto& sk : ab.support)
		if (ab_use_skill(sd, sk, ctx.best, tick) == ABS_USED)
			return true;
	return false;
}

// ---------------------------------------------------------------------------------------------
// Target selection
// ---------------------------------------------------------------------------------------------

static bool ab_mob_wanted(map_session_data& sd, s_autobattle& ab, const mob_data& md) {
	bool attacks_us = md.target_id == sd.id;
	// "never attack" list: only fought when it attacks us
	if (!attacks_us && std::find(ab.blacklist.begin(), ab.blacklist.end(), md.mob_id) != ab.blacklist.end())
		return false;
	if (ab.all_monsters || std::find(ab.mobs.begin(), ab.mobs.end(), md.mob_id) != ab.mobs.end())
		return true;
	return ab.fight_back && attacks_us;
}

static bool ab_mob_valid(map_session_data& sd, s_autobattle& ab, mob_data& md, t_tick tick) {
	if (md.prev == nullptr || md.m != sd.m || status_isdead(md))
		return false;
	if (md.special_state.ai) // summoned helpers / pets of other players
		return false;
	if (ab_ignored(ab, md.id, tick))
		return false;
	if (!ab_mob_wanted(sd, ab, md))
		return false;
	if (battle_check_target(&sd, &md, BCT_ENEMY) <= 0 || !status_check_skilluse(&sd, &md, 0, 0))
		return false;
	return true;
}

struct ab_find_ctx {
	map_session_data* sd;
	s_autobattle* ab;
	t_tick tick;
	mob_data* best;
	int32 best_score;
};

static int32 ab_find_sub(block_list* bl, va_list ap) {
	ab_find_ctx* ctx = va_arg(ap, ab_find_ctx*);
	mob_data* md = (mob_data*)bl;

	if (!ab_mob_valid(*ctx->sd, *ctx->ab, *md, ctx->tick))
		return 0;
	int32 score = distance_bl(ctx->sd, bl) * 2;
	if (md->target_id == ctx->sd->id)
		score -= 100; // monsters already attacking us come first
	if (score >= ctx->best_score)
		return 0;
	if (!unit_can_reach_bl(ctx->sd, bl, 0, 1, nullptr, nullptr)) {
		ab_ignore(*ctx->ab, md->id, ctx->tick, 8000);
		return 0;
	}
	ctx->best = md;
	ctx->best_score = score;
	return 0;
}

static mob_data* ab_pick_target(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	// Keep the current target while it stays valid
	if (ab.target_id) {
		block_list* bl = map_id2bl(ab.target_id);
		if (bl && bl->type == BL_MOB) {
			mob_data* md = (mob_data*)bl;
			if (ab_mob_valid(sd, ab, *md, tick) && (int32)distance_bl(&sd, bl) <= battle_config.autobattle_search_range + 4) {
				// stuck detection: no damage dealt for 15s -> give up on it
				if (md->status.hp != ab.target_hp) {
					ab.target_hp = md->status.hp;
					ab.target_since = tick;
				} else if (DIFF_TICK(tick, ab.target_since) > 15000) {
					ab_ignore(ab, md->id, tick, 30000);
					ab.target_id = 0;
					unit_stop_attack(&sd);
					return nullptr;
				}
				return md;
			}
		}
		ab.target_id = 0;
	}

	ab_find_ctx ctx = { &sd, &ab, tick, nullptr, INT32_MAX };
	map_foreachinrange(ab_find_sub, &sd, battle_config.autobattle_search_range, BL_MOB, &ctx);
	if (ctx.best) {
		ab.target_id = ctx.best->id;
		ab.target_hp = ctx.best->status.hp;
		ab.target_since = tick;
		ab.engaged++;
	}
	return ctx.best;
}

// ---------------------------------------------------------------------------------------------
// Loot
// ---------------------------------------------------------------------------------------------

struct ab_loot_ctx {
	map_session_data* sd;
	s_autobattle* ab;
	t_tick tick;
	flooritem_data* best;
	int32 best_dist;
};

static int32 ab_loot_sub(block_list* bl, va_list ap) {
	ab_loot_ctx* ctx = va_arg(ap, ab_loot_ctx*);
	flooritem_data* fitem = (flooritem_data*)bl;

	if (ab_ignored(*ctx->ab, fitem->id, ctx->tick))
		return 0;
	if (!ctx->ab->loot_ids.empty()
		&& std::find(ctx->ab->loot_ids.begin(), ctx->ab->loot_ids.end(), fitem->item.nameid) == ctx->ab->loot_ids.end())
		return 0;
	// Respect drop ownership so we don't spam failing pickups
	if (fitem->first_get_charid > 0 && (uint32)fitem->first_get_charid != ctx->sd->status.char_id
		&& DIFF_TICK(ctx->tick, fitem->first_get_tick) < 0 && !ctx->sd->status.party_id)
		return 0;
	int32 dist = distance_bl(ctx->sd, bl);
	if (dist < ctx->best_dist) {
		ctx->best = fitem;
		ctx->best_dist = dist;
	}
	return 0;
}

static bool ab_loot(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	if (!ab.loot || pc_getpercentweight(sd) >= ab.weight_pct)
		return false;
	ab_loot_ctx ctx = { &sd, &ab, tick, nullptr, INT32_MAX };
	map_foreachinrange(ab_loot_sub, &sd, ab.loot_range, BL_ITEM, &ctx);
	if (!ctx.best)
		return false;

	if (ctx.best_dist <= 1 || (ctx.best->x == sd.x && ctx.best->y == sd.y)) {
		unit_stop_walking(&sd, USW_FIXPOS);
		if (!pc_takeitem(&sd, ctx.best))
			ab_ignore(ab, ctx.best->id, tick, 15000);
		return true;
	}
	if (!unit_is_walking(&sd)) {
		if (!unit_walktobl(&sd, ctx.best, 1, 1)) {
			ab_ignore(ab, ctx.best->id, tick, 15000);
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Restock
// ---------------------------------------------------------------------------------------------

/// Lowest price (after discount skills) at which a normal zeny NPC shop sells the item, 0 = not sold anywhere.
/// Restock only buys what a real shop sells, so it can never be used to get items out of the item DB for free-ish.
static int32 ab_shop_price(map_session_data& sd, t_itemid nameid) {
	int64 best = 0;
	for (int32 m = 0; m < map_num; m++) {
		map_data* mapdata = map_getmapdata(m);
		if (!mapdata)
			continue;
		for (int32 i = 0; i < mapdata->npc_num; i++) {
			npc_data* nd = mapdata->npc[i];
			if (!nd || nd->subtype != NPCTYPE_SHOP || !nd->u.shop.shop_item)
				continue;
			for (int32 j = 0; j < nd->u.shop.count; j++) {
				const npc_item_list& li = nd->u.shop.shop_item[j];
				if (li.nameid != nameid || li.value == 0)
					continue;
				int64 price = nd->u.shop.discount ? pc_modifybuyvalue(&sd, li.value) : li.value;
				if (price > 0 && (best == 0 || price < best))
					best = price;
			}
		}
	}
	return (int32)std::min<int64>(best, INT32_MAX);
}

static bool ab_need_restock(map_session_data& sd, s_autobattle& ab) {
	if (!ab.restock)
		return false;
	for (const auto& r : ab.restock_items)
		if (r.amount > 0 && ab_item_count(sd, r.nameid) < std::min<int32>(ab.restock_min, r.amount))
			return true;
	return false;
}

/// Buys the configured items with zeny at their NPC buy price (with discount skills applied)
static int32 ab_buy_items(map_session_data& sd, s_autobattle& ab) {
	int32 bought_types = 0;
	for (const auto& r : ab.restock_items) {
		int32 need = (int32)r.amount - ab_item_count(sd, r.nameid);
		auto id = item_db.find(r.nameid);
		if (need <= 0 || !id || !id->isStackable())
			continue;
		int32 price = ab_shop_price(sd, r.nameid);
		if (price <= 0)
			continue; // not sold by any NPC shop
		int64 by_zeny = sd.status.zeny / price;
		int64 by_weight = id->weight ? ((int64)sd.max_weight - sd.weight) / id->weight : need;
		int32 n = (int32)std::min<int64>({ (int64)need, by_zeny, by_weight, (int64)MAX_AMOUNT });
		if (n <= 0)
			continue;
		item it = {};
		it.nameid = r.nameid;
		it.identify = 1;
		if (pc_additem(&sd, &it, n, LOG_TYPE_NPC) == ADDITEM_SUCCESS) {
			pc_payzeny(&sd, price * n, LOG_TYPE_NPC);
			ab_msg(sd, "Restock: bought %d x %s for %dz.", "\u0E0B\u0E37\u0E49\u0E2D\u0E02\u0E2D\u0E07: %d x %s \u0E23\u0E32\u0E04\u0E32 %dz", n, id->ename.c_str(), price * n);
			bought_types++;
		}
	}
	return bought_types;
}

static bool ab_restock(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	map_data* md = map_getmapdata(sd.m);
	ab.return_mapindex = md ? md->index : 0;
	ab.return_x = sd.x;
	ab.return_y = sd.y;

	unit_stop_attack(&sd);
	ab.target_id = 0;
	if (pc_setpos_savepoint(sd) != SETPOS_OK) {
		ab_stop(sd, "could not warp to the save point for restocking.", "\u0E44\u0E21\u0E48\u0E2A\u0E32\u0E21\u0E32\u0E23\u0E16\u0E27\u0E32\u0E23\u0E4C\u0E1B\u0E44\u0E1B\u0E08\u0E38\u0E14\u0E40\u0E0B\u0E1F\u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E0B\u0E37\u0E49\u0E2D\u0E02\u0E2D\u0E07\u0E44\u0E14\u0E49");
		return false;
	}
	if (!ab_buy_items(sd, ab)) {
		ab_stop(sd, "restock failed (not enough zeny / weight, or item not sold).", "\u0E0B\u0E37\u0E49\u0E2D\u0E02\u0E2D\u0E07\u0E44\u0E21\u0E48\u0E2A\u0E33\u0E40\u0E23\u0E47\u0E08 (Zeny/\u0E19\u0E49\u0E33\u0E2B\u0E19\u0E31\u0E01\u0E44\u0E21\u0E48\u0E1E\u0E2D \u0E2B\u0E23\u0E37\u0E2D\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E44\u0E21\u0E48\u0E21\u0E35\u0E02\u0E32\u0E22)");
		return false;
	}
	ab.phase = ABP_RETURNING;
	ab.return_tick = tick + 3000;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Roaming
// ---------------------------------------------------------------------------------------------

static void ab_roam(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	if (DIFF_TICK(tick, ab.next_roam) < 0 || unit_is_walking(&sd))
		return;
	ab.next_roam = tick + 1500 + rnd_value<int32>(0, 1500);

	map_data* mapdata = map_getmapdata(sd.m);
	if (!mapdata)
		return;
	int32 R = battle_config.autobattle_roam_radius;
	for (int32 i = 0; i < 12; i++) {
		int32 x = sd.x + rnd_value<int32>(-6, 6);
		int32 y = sd.y + rnd_value<int32>(-6, 6);
		x = std::clamp<int32>(x, ab.anchor_x - R, ab.anchor_x + R);
		y = std::clamp<int32>(y, ab.anchor_y - R, ab.anchor_y + R);
		if (x < 1 || y < 1 || x >= mapdata->xs - 1 || y >= mapdata->ys - 1 || (x == sd.x && y == sd.y))
			continue;
		if (!map_getcell(sd.m, x, y, CELL_CHKPASS) || map_getcell(sd.m, x, y, CELL_CHKNPC) || !unit_can_reach_pos(&sd, x, y, 0))
			continue;
		if (unit_walktoxy(&sd, (int16)x, (int16)y, 0))
			return;
	}
}

// ---------------------------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------------------------

static void ab_combat(map_session_data& sd, s_autobattle& ab, mob_data& target, t_tick tick) {
	if (sd.ud.skilltimer != INVALID_TIMER)
		return; // casting

	int32 need_range = 0;
	for (auto& sk : ab.attack) {
		if (!sk.skill_id || !ab_cond_ok(sk, target))
			continue;
		if (ab_use_skill(sd, sk, &target, tick, &need_range) == ABS_USED)
			return;
	}

	if (ab.normal_attack) {
		if (sd.ud.target != target.id || (sd.ud.attacktimer == INVALID_TIMER && !unit_is_walking(&sd)))
			unit_attack(&sd, target.id, 1);
	} else if (need_range > 0 && !unit_is_walking(&sd)) {
		// skills only: close in until the skill is in range
		if (!unit_walktobl(&sd, &target, need_range, 1))
			ab_ignore(ab, target.id, tick, 8000);
	}
}

// ---------------------------------------------------------------------------------------------
// Main tick
// ---------------------------------------------------------------------------------------------

static void ab_tick(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	if (!battle_config.autobattle_enable) {
		ab_stop(sd, "disabled by the server.", "\u0E40\u0E0B\u0E34\u0E23\u0E4C\u0E1F\u0E40\u0E27\u0E2D\u0E23\u0E4C\u0E1B\u0E34\u0E14\u0E23\u0E30\u0E1A\u0E1A\u0E19\u0E35\u0E49");
		return;
	}
	if (pc_isdead(&sd)) {
		ab_stop(sd, "you died.", "\u0E15\u0E31\u0E27\u0E25\u0E30\u0E04\u0E23\u0E15\u0E32\u0E22\u0E41\u0E25\u0E49\u0E27");
		return;
	}
	if (sd.state.vending || sd.state.buyingstore) {
		ab_stop(sd, "a shop was opened.", "\u0E40\u0E1B\u0E34\u0E14\u0E23\u0E49\u0E32\u0E19\u0E04\u0E49\u0E32");
		return;
	}
	if (sd.prev != nullptr && !sd.state.warping) {
		// Visible marker: status icon (re-applied if something removed it) and a periodic effect for everybody nearby
		if (!sd.sc.getSCE(SC_AUTOBATTLE))
			sc_start(&sd, &sd, SC_AUTOBATTLE, 100, 1, INFINITE_TICK);
		if (battle_config.autobattle_effect_interval > 0 && DIFF_TICK(tick, ab.next_effect) >= 0) {
			ab.next_effect = tick + battle_config.autobattle_effect_interval;
			clif_specialeffect(&sd, battle_config.autobattle_effect, AREA);
		}
	}
	if (sd.sc.getSCE(SC_JAILED)) {
		ab_stop(sd, "Auto Battle cannot be used in jail.", "\u0E44\u0E21\u0E48\u0E2D\u0E19\u0E38\u0E0D\u0E32\u0E15\u0E43\u0E2B\u0E49\u0E43\u0E0A\u0E49 Auto Battle \u0E02\u0E13\u0E30\u0E16\u0E39\u0E01\u0E02\u0E31\u0E07");
		return;
	}
	if (sd.prev == nullptr || sd.state.warping || sd.state.changemap || sd.npc_id || sd.state.workinprogress
		|| sd.state.trading || sd.state.storage_flag || sd.state.prevend || sd.npc_shopid || sd.state.refineui_open
		|| sd.state.barter_open || sd.state.barter_extended_open || sd.state.roulette_open || sd.state.stylist_open)
		return; // busy (loading map / NPC dialogue / trade / storage...)
	if (map_flag_vs(sd.m)) {
		ab_stop(sd, "this map does not allow Auto Battle.", "\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48\u0E19\u0E35\u0E49\u0E44\u0E21\u0E48\u0E2D\u0E19\u0E38\u0E0D\u0E32\u0E15\u0E43\u0E2B\u0E49\u0E43\u0E0A\u0E49 Auto Battle");
		return;
	}

	// Waiting to go back after restock
	if (ab.phase == ABP_RETURNING) {
		if (DIFF_TICK(tick, ab.return_tick) < 0)
			return;
		if (!ab.return_mapindex || pc_setpos(&sd, ab.return_mapindex, ab.return_x, ab.return_y, CLR_TELEPORT) != SETPOS_OK) {
			ab_stop(sd, "could not return to the hunting spot.", "\u0E44\u0E21\u0E48\u0E2A\u0E32\u0E21\u0E32\u0E23\u0E16\u0E01\u0E25\u0E31\u0E1A\u0E44\u0E1B\u0E08\u0E38\u0E14\u0E25\u0E48\u0E32\u0E44\u0E14\u0E49");
			return;
		}
		ab.phase = ABP_HUNT;
		ab.anchor_m = -1;
		ab.last_target_tick = tick;
		return;
	}

	if (ab.anchor_m != sd.m) {
		map_data* cur = map_getmapdata(sd.m);
		uint16 cur_index = cur ? cur->index : 0;
		// Carried to another map by accident (walked onto a warp portal...): go back to the hunting spot
		if (ab.hunt_mapindex && cur_index && cur_index != ab.hunt_mapindex) {
			if (DIFF_TICK(tick, ab.return_tries_reset) >= 0) {
				ab.return_tries = 0;
				ab.return_tries_reset = tick + 60000;
			}
			if (battle_config.autobattle_warp_return && ab.return_tries < 3) {
				ab.return_tries++;
				ab_msg(sd, "Left the hunting map (warp portal?), returning to the hunting spot.",
					"\u0E2D\u0E2D\u0E01\u0E08\u0E32\u0E01\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48\u0E25\u0E48\u0E32 (\u0E0A\u0E19\u0E27\u0E32\u0E23\u0E4C\u0E1B?) \u0E01\u0E33\u0E25\u0E31\u0E07\u0E01\u0E25\u0E31\u0E1A\u0E44\u0E1B\u0E08\u0E38\u0E14\u0E25\u0E48\u0E32");
				unit_stop_attack(&sd);
				ab.target_id = 0;
				if (pc_setpos(&sd, ab.hunt_mapindex, ab.hunt_x, ab.hunt_y, CLR_TELEPORT) == SETPOS_OK)
					return;
			}
			ab_stop(sd, "moved to another map (warp portal / teleport).", "\u0E22\u0E49\u0E32\u0E22\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48 (\u0E0A\u0E19\u0E27\u0E32\u0E23\u0E4C\u0E1B / \u0E40\u0E17\u0E40\u0E25\u0E1E\u0E2D\u0E23\u0E4C\u0E15)");
			return;
		}
		ab.anchor_m = sd.m;
		ab.anchor_x = sd.x;
		ab.anchor_y = sd.y;
		ab.target_id = 0;
		if (!ab.hunt_mapindex) {
			ab.hunt_mapindex = cur_index;
			ab.hunt_x = sd.x;
			ab.hunt_y = sd.y;
		}
	}

	if (!ab_safety(sd, ab, tick))
		return;

	ab_recovery(sd, ab, tick);

	// Item supplies
	if (DIFF_TICK(tick, ab.next_restock_check) >= 0) {
		ab.next_restock_check = tick + 3000;
		if (ab_need_restock(sd, ab)) {
			ab_restock(sd, ab, tick);
			return;
		}
		if (ab.stop_no_item && !ab.restock) {
			if (ab.hp_item && !ab_item_count(sd, ab.hp_item)) {
				ab_stop(sd, "out of HP recovery items.", "\u0E22\u0E32\u0E1F\u0E37\u0E49\u0E19 HP \u0E2B\u0E21\u0E14");
				return;
			}
			if (ab.sp_item && !ab_item_count(sd, ab.sp_item)) {
				ab_stop(sd, "out of SP recovery items.", "\u0E22\u0E32\u0E1F\u0E37\u0E49\u0E19 SP \u0E2B\u0E21\u0E14");
				return;
			}
		}
	}

	if (sd.ud.skilltimer != INVALID_TIMER)
		return;

	if (ab_rest(sd, ab, tick))
		return; // sitting down to regenerate

	ab_buff_items(sd, ab, tick);

	if (ab_support(sd, ab, tick))
		return;
	if (ab_buffs(sd, ab, tick))
		return;

	mob_data* target = ab_pick_target(sd, ab, tick);
	if (target) {
		ab.last_target_tick = tick;
		ab_combat(sd, ab, *target, tick);
		return;
	}

	if (ab_loot(sd, ab, tick))
		return;

	// Nothing to do: wander around the hunting spot, teleport away when the spot is dead
	if (ab.idle_seconds && DIFF_TICK(tick, ab.last_target_tick) > (t_tick)ab.idle_seconds * 1000) {
		ab.last_target_tick = tick;
		if (ab.flee_item && ab_use_item(sd, ab.flee_item, tick)) {
			ab.anchor_m = -1; // re-anchor at the new position
			ab.hunt_mapindex = 0; // the item may have moved us to another map on purpose
			return;
		}
	}
	ab_roam(sd, ab, tick);
}

static TIMER_FUNC(ab_timer) {
	map_session_data* sd = map_id2sd(id);
	if (!sd || !sd->autobattle)
		return 0;
	std::shared_ptr<s_autobattle> ab = sd->autobattle;
	if (ab->timer != tid)
		return 0;
	ab->timer = INVALID_TIMER;
	if (!ab->running)
		return 0;

	ab_tick(*sd, *ab, tick);

	if (ab->running && ab->timer == INVALID_TIMER)
		ab->timer = add_timer(tick + battle_config.autobattle_interval, ab_timer, sd->id, 0);
	return 0;
}

// ---------------------------------------------------------------------------------------------
// Lists for the setup dialog (usable skills / monsters on the map)
// ---------------------------------------------------------------------------------------------

static int32 ab_count_mobs_sub(block_list* bl, va_list ap) {
	typedef std::map<uint16, int32> mobcount_t;
	mobcount_t* counts = va_arg(ap, mobcount_t*);
	mob_data* md = (mob_data*)bl;
	if (!md->special_state.ai)
		(*counts)[md->mob_id]++;
	return 0;
}

static bool ab_quiet = false; ///< suppress informational output (used by the NPC dialog)

void autobattle_list_map_mobs(map_session_data& sd) {
	auto ab = autobattle_get(sd);
	std::map<uint16, int32> counts;
	map_foreachinmap(ab_count_mobs_sub, sd.m, BL_MOB, &counts);
	std::vector<std::pair<uint16, int32>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	if (sorted.size() > 60)
		sorted.resize(60);
	ab->cache_mobs = sorted;
	if (ab_quiet)
		return;

	ab_msg(sd, "Monsters on %s - attack every monster: %s", "\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C\u0E43\u0E19\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48 %s - \u0E42\u0E08\u0E21\u0E15\u0E35\u0E17\u0E38\u0E01\u0E15\u0E31\u0E27: %s",
		map_getmapdata(sd.m)->name, ab_onoff(ab->all_monsters).c_str());
	int32 shown = 0;
	for (const auto& p : sorted) {
		if (++shown > 15) {
			ab_msg(sd, "  ...", nullptr);
			break;
		}
		bool selected = std::find(ab->mobs.begin(), ab->mobs.end(), p.first) != ab->mobs.end();
		ab_msg(sd, "  [%c] %s (%d)  id:%d", nullptr, selected ? 'x' : ' ', ab_mob_name(p.first), p.second, p.first);
	}
	if (sorted.empty())
		ab_msg(sd, "  (no monsters on this map)", "  (\u0E44\u0E21\u0E48\u0E21\u0E35\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C\u0E43\u0E19\u0E41\u0E1C\u0E19\u0E17\u0E35\u0E48\u0E19\u0E35\u0E49)");
}

/// Inventory items for the item picker of the setup dialog (done here because a script loop over a big inventory
/// hits the script engine's loop limit). mode: 0 = healing items, 1 = healing + usable, 2 = everything except weapons / armor.
static void ab_list_items(map_session_data& sd, s_autobattle& ab, int32 mode) {
	ab.cache_items.clear();
	std::map<t_itemid, int32> total;
	std::vector<t_itemid> order;
	for (int32 i = 0; i < MAX_INVENTORY; i++) {
		const item& it = sd.inventory.u.items_inventory[i];
		if (!it.nameid || it.amount <= 0 || it.equip)
			continue;
		auto data = item_db.find(it.nameid);
		if (!data)
			continue;
		bool ok;
		if (mode == 0)
			ok = data->type == IT_HEALING;
		else if (mode == 1)
			ok = data->type == IT_HEALING || data->type == IT_USABLE;
		else
			ok = data->type != IT_WEAPON && data->type != IT_ARMOR;
		if (!ok)
			continue;
		if (total.find(it.nameid) == total.end())
			order.push_back(it.nameid);
		total[it.nameid] += it.amount;
	}
	for (t_itemid id : order) {
		if (ab.cache_items.size() >= 200)
			break;
		ab.cache_items.emplace_back(id, total[id]);
	}
}

/// kind: "attack" / "buff" / "support"
static void ab_list_skills(map_session_data& sd, s_autobattle& ab, const std::string& kind) {
	ab.cache_skills.clear();
	for (int32 i = 0; i < MAX_SKILL; i++) {
		uint16 id = sd.status.skill[i].id;
		uint16 lv = id ? pc_checkskill(&sd, id) : 0;
		if (!id || !lv)
			continue;
		int32 inf = skill_get_inf(id);
		sc_type sct = skill_get_sc(id);
		bool ok = false;
		if (kind == "buff")
			ok = (inf & (INF_SELF_SKILL | INF_SUPPORT_SKILL)) && !(inf & INF_ATTACK_SKILL) && sct != SC_NONE;
		else if (kind == "support")
			ok = (inf & INF_SUPPORT_SKILL) && !(inf & (INF_SELF_SKILL | INF_ATTACK_SKILL)) && sct == SC_NONE;
		else {
			ok = (inf & (INF_ATTACK_SKILL | INF_GROUND_SKILL)) && !skill_get_nk(id, NK_NODAMAGE);
			if (!ok && (inf & INF_SELF_SKILL) && sct == SC_NONE && skill_get_splash(id, lv) > 0 && !skill_get_nk(id, NK_NODAMAGE))
				ok = true; // area skill around the caster (Magnum Break...)
		}
		if (ok && ab.cache_skills.size() < 200)
			ab.cache_skills.emplace_back(id, lv);
	}
}

// ---------------------------------------------------------------------------------------------
// Status display
// ---------------------------------------------------------------------------------------------

bool autobattle_script(map_session_data& sd, int32 op) {
	auto ab = autobattle_get(sd);
	t_tick tick = gettick();
	if (DIFF_TICK(tick, ab->next_toggle) < 0) // anti spam
		return ab->running;
	ab->next_toggle = tick + 1500;
	if (op == 2)
		op = ab->running ? 0 : 1;
	if (op == 0) {
		if (ab->running)
			autobattle_stop(sd);
	} else if (!ab->running)
		autobattle_start(sd, true);
	return ab->running;
}

static void ab_show_slots(map_session_data& sd, const char* en, const char* th, const s_ab_skill* slots, int32 n) {
	std::string line;
	for (int32 i = 0; i < n; i++) {
		if (!slots[i].skill_id)
			continue;
		char buf[96];
		snprintf(buf, sizeof(buf), "[%d] %s Lv%d  ", i + 1, skill_get_desc(slots[i].skill_id), slots[i].level);
		line += buf;
		if (line.size() > 80) {
			ab_msg(sd, "  %s", nullptr, line.c_str());
			line.clear();
		}
	}
	std::string title = ab_pick(en, th);
	ab_msg(sd, "%s%s", nullptr, title.c_str(), line.empty() ? ab_pick(" (none)", " (\u0E44\u0E21\u0E48\u0E21\u0E35)").c_str() : "");
	if (!line.empty())
		ab_msg(sd, "  %s", nullptr, line.c_str());
}

void autobattle_status(map_session_data& sd) {
	auto ab = autobattle_get(sd);
	ab_msg(sd, "=== Auto Battle: %s ===", "=== Auto Battle: %s ===",
		ab_pick(ab->running ? "RUNNING" : "Stopped", ab->running ? "\u0E01\u0E33\u0E25\u0E31\u0E07\u0E17\u0E33\u0E07\u0E32\u0E19" : "\u0E2B\u0E22\u0E38\u0E14\u0E2D\u0E22\u0E39\u0E48").c_str());
	ab_msg(sd, "[Hunt] all monsters:%s normal attack:%s fight back:%s", "[\u0E25\u0E48\u0E32] \u0E42\u0E08\u0E21\u0E15\u0E35\u0E17\u0E38\u0E01\u0E15\u0E31\u0E27:%s \u0E42\u0E08\u0E21\u0E15\u0E35\u0E18\u0E23\u0E23\u0E21\u0E14\u0E32:%s \u0E2A\u0E39\u0E49\u0E01\u0E25\u0E31\u0E1A\u0E40\u0E09\u0E1E\u0E32\u0E30\u0E17\u0E35\u0E48\u0E15\u0E35\u0E40\u0E23\u0E32:%s",
		ab_onoff(ab->all_monsters).c_str(), ab_onoff(ab->normal_attack).c_str(), ab_onoff(ab->fight_back).c_str());
	if (!ab->mobs.empty()) {
		std::string s;
		for (uint16 m : ab->mobs) s += std::string(ab_mob_name(m)) + "(" + std::to_string(m) + ") ";
		ab_msg(sd, "  monsters: %s", "  \u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C: %s", s.c_str());
	}
	ab_show_slots(sd, "  Attack skills:", "  \u0E2A\u0E01\u0E34\u0E25\u0E42\u0E08\u0E21\u0E15\u0E35:", ab->attack, AB_MAX_ATTACK_SLOTS);
	ab_msg(sd, "[Recovery] HP: %s <%d%%  SP: %s <%d%%", "[\u0E1F\u0E37\u0E49\u0E19\u0E1F\u0E39] HP: %s <%d%%  SP: %s <%d%%",
		ab->hp_item ? ab_item_name(ab->hp_item) : "-", ab->hp_pct, ab->sp_item ? ab_item_name(ab->sp_item) : "-", ab->sp_pct);
	ab_show_slots(sd, "[Buffs]", "[\u0E1A\u0E31\u0E1E]", ab->buffs, AB_MAX_BUFF_SLOTS);
	ab_show_slots(sd, "[Support]", "[\u0E0B\u0E31\u0E1E\u0E1E\u0E2D\u0E23\u0E4C\u0E15]", ab->support, AB_MAX_SUPPORT_SLOTS);
	ab_msg(sd, "[Loot] %s range:%d filter:%s", "[\u0E40\u0E01\u0E47\u0E1A\u0E02\u0E2D\u0E07] %s \u0E23\u0E30\u0E22\u0E30:%d \u0E15\u0E31\u0E27\u0E01\u0E23\u0E2D\u0E07:%s", ab_onoff(ab->loot).c_str(), ab->loot_range,
		ab_pick("everything", "\u0E40\u0E01\u0E47\u0E1A\u0E17\u0E38\u0E01\u0E2D\u0E22\u0E48\u0E32\u0E07").c_str());
	ab_msg(sd, "[Restock] %s (below %d)", "[\u0E0B\u0E37\u0E49\u0E2D\u0E02\u0E2D\u0E07] %s (\u0E40\u0E21\u0E37\u0E48\u0E2D\u0E40\u0E2B\u0E25\u0E37\u0E2D\u0E19\u0E49\u0E2D\u0E22\u0E01\u0E27\u0E48\u0E32 %d)", ab_onoff(ab->restock).c_str(), ab->restock_min);
	if (!ab->running && !ab->last_stop.empty())
		ab_msg(sd, "Last stop reason: %s", "\u0E2A\u0E32\u0E40\u0E2B\u0E15\u0E38\u0E17\u0E35\u0E48\u0E2B\u0E22\u0E38\u0E14\u0E04\u0E23\u0E31\u0E49\u0E07\u0E25\u0E48\u0E32\u0E2A\u0E38\u0E14: %s", ab->last_stop.c_str());
	ab_msg(sd, "[Safety] flee HP<%d%% (item: %s) idle:%ds", "[\u0E04\u0E27\u0E32\u0E21\u0E1B\u0E25\u0E2D\u0E14\u0E20\u0E31\u0E22] \u0E2B\u0E19\u0E35\u0E40\u0E21\u0E37\u0E48\u0E2D HP<%d%% (\u0E44\u0E2D\u0E40\u0E17\u0E21: %s) \u0E27\u0E48\u0E32\u0E07\u0E07\u0E32\u0E19:%d \u0E27\u0E34\u0E19\u0E32\u0E17\u0E35",
		ab->flee_hp_pct, ab->flee_item ? ab_item_name(ab->flee_item) : "-", ab->idle_seconds);
}

// ---------------------------------------------------------------------------------------------
// Data for the NPC dialog (script commands autobattleinfo / autobattlestr)
// ---------------------------------------------------------------------------------------------

int64 autobattle_info(map_session_data& sd, const char* key, int32 a, int32 b) {
	auto ab = autobattle_get(sd);
	const std::string k = key ? key : "";

	auto sk = [&](const s_ab_skill* arr, int32 n) -> const s_ab_skill* { return (a >= 0 && a < n) ? &arr[a] : nullptr; };

	if (k == "running") return ab->running;
	if (k == "ready") return sd.vars_ok;
	if (k == "lang") return ab_thai();
	if (k == "itemonly") return battle_config.autobattle_item_only;
	if (k == "all") return ab->all_monsters;
	if (k == "normal") return ab->normal_attack;
	if (k == "fightback") return ab->fight_back;
	if (k == "loot") return ab->loot;
	if (k == "restock") return ab->restock;
	if (k == "stopplayer") return ab->stop_on_player;
	if (k == "stopweight") return ab->stop_on_weight;
	if (k == "stopnoitem") return ab->stop_no_item;
	if (k == "sitrest") return ab->sit_rest;
	if (k == "weightwarp") return ab->weight_warp;
	if (k == "fleemvp") return ab->flee_mvp;
	if (k == "fleemini") return ab->flee_mini;
	if (k == "hpitem") return ab->hp_item;
	if (k == "spitem") return ab->sp_item;
	if (k == "fleeitem") return ab->flee_item;
	if (k == "hppct") return ab->hp_pct;
	if (k == "sppct") return ab->sp_pct;
	if (k == "suppct") return ab->support_hp_pct;
	if (k == "fleepct") return ab->flee_hp_pct;
	if (k == "weightpct") return ab->weight_pct;
	if (k == "lootrange") return ab->loot_range;
	if (k == "restockmin") return ab->restock_min;
	if (k == "idle") return ab->idle_seconds;
	if (k == "sithp") return ab->sit_hp;
	if (k == "standhp") return ab->stand_hp;
	if (k == "sitsp") return ab->sit_sp;
	if (k == "standsp") return ab->stand_sp;
	if (k == "weight") return pc_getpercentweight(sd);
	if (k == "invcount") return ab_item_count(sd, (t_itemid)a);
	if (k == "learned") return a > 0 ? pc_checkskill(&sd, (uint16)a) : 0;

	if (k == "atk_id" || k == "atk_lv" || k == "atk_cond" || k == "atk_cv") {
		const s_ab_skill* s = sk(ab->attack, AB_MAX_ATTACK_SLOTS);
		if (!s) return 0;
		return k == "atk_id" ? s->skill_id : k == "atk_lv" ? s->level : k == "atk_cond" ? s->cond : s->cond_val;
	}
	if (k == "buff_id" || k == "buff_lv") {
		const s_ab_skill* s = sk(ab->buffs, AB_MAX_BUFF_SLOTS);
		return s ? (k == "buff_id" ? s->skill_id : s->level) : 0;
	}
	if (k == "sup_id" || k == "sup_lv") {
		const s_ab_skill* s = sk(ab->support, AB_MAX_SUPPORT_SLOTS);
		return s ? (k == "sup_id" ? s->skill_id : s->level) : 0;
	}
	if (k == "bi_id") return (a >= 0 && a < AB_MAX_BUFFITEMS) ? ab->buff_items[a].nameid : 0;
	if (k == "bi_iv") return (a >= 0 && a < AB_MAX_BUFFITEMS) ? ab->buff_items[a].interval : 0;

	if (k == "mobn") return ab->mobs.size();
	if (k == "mob") return (a >= 0 && a < (int32)ab->mobs.size()) ? ab->mobs[a] : 0;
	if (k == "mobsel") return std::find(ab->mobs.begin(), ab->mobs.end(), (uint16)a) != ab->mobs.end();
	if (k == "bln") return ab->blacklist.size();
	if (k == "bl") return (a >= 0 && a < (int32)ab->blacklist.size()) ? ab->blacklist[a] : 0;
	if (k == "blsel") return std::find(ab->blacklist.begin(), ab->blacklist.end(), (uint16)a) != ab->blacklist.end();
	if (k == "lootn") return ab->loot_ids.size();
	if (k == "lootid") return (a >= 0 && a < (int32)ab->loot_ids.size()) ? ab->loot_ids[a] : 0;
	if (k == "rsn") return ab->restock_items.size();
	if (k == "rsid") return (a >= 0 && a < (int32)ab->restock_items.size()) ? ab->restock_items[a].nameid : 0;
	if (k == "rsamt") return (a >= 0 && a < (int32)ab->restock_items.size()) ? ab->restock_items[a].amount : 0;

	// lists filled by the commands "skills <kind>" and "mapmobs"
	if (k == "skn") return ab->cache_skills.size();
	if (k == "skid") return (a >= 0 && a < (int32)ab->cache_skills.size()) ? ab->cache_skills[a].first : 0;
	if (k == "sklv") return (a >= 0 && a < (int32)ab->cache_skills.size()) ? ab->cache_skills[a].second : 0;
	if (k == "itn") return ab->cache_items.size();
	if (k == "itid") return (a >= 0 && a < (int32)ab->cache_items.size()) ? ab->cache_items[a].first : 0;
	if (k == "itcnt") return (a >= 0 && a < (int32)ab->cache_items.size()) ? ab->cache_items[a].second : 0;
	if (k == "mmn") return ab->cache_mobs.size();
	if (k == "mmid") return (a >= 0 && a < (int32)ab->cache_mobs.size()) ? ab->cache_mobs[a].first : 0;
	if (k == "mmcnt") return (a >= 0 && a < (int32)ab->cache_mobs.size()) ? ab->cache_mobs[a].second : 0;

	(void)b;
	return 0;
}

std::string autobattle_str(map_session_data& sd, const char* key, int32 a) {
	auto ab = autobattle_get(sd);
	const std::string k = key ? key : "";
	auto name = [](uint16 id) -> std::string { return id ? skill_get_desc(id) : std::string(); };

	if (k == "skill") return name((uint16)a);
	if (k == "item") return a > 0 ? ab_item_name((t_itemid)a) : "";
	if (k == "mob") return a > 0 ? ab_mob_name((uint16)a) : "";
	if (k == "map") { map_data* md = map_getmapdata(sd.m); return md ? md->name : ""; }
	if (k == "atk") return (a >= 0 && a < AB_MAX_ATTACK_SLOTS) ? name(ab->attack[a].skill_id) : "";
	if (k == "buff") return (a >= 0 && a < AB_MAX_BUFF_SLOTS) ? name(ab->buffs[a].skill_id) : "";
	if (k == "sup") return (a >= 0 && a < AB_MAX_SUPPORT_SLOTS) ? name(ab->support[a].skill_id) : "";
	if (k == "skname") return (a >= 0 && a < (int32)ab->cache_skills.size()) ? name(ab->cache_skills[a].first) : "";
	if (k == "itname") return (a >= 0 && a < (int32)ab->cache_items.size()) ? ab_item_name(ab->cache_items[a].first) : "";
	if (k == "mmname") return (a >= 0 && a < (int32)ab->cache_mobs.size()) ? ab_mob_name(ab->cache_mobs[a].first) : "";
	return "";
}

// ---------------------------------------------------------------------------------------------
// @autobattle command
// ---------------------------------------------------------------------------------------------

static const char* ab_help_en[] = {
	"=== @autobattle (@ab) ===",
	"@ab  -> open the setup menu      start | stop | status | mobs | reset",
	"all <on|off> | attack <on|off> | fightback <on|off>",
	"mob +<id|name> / -<id|name> / reset     blacklist +<id|name> / -<id|name> / reset",
	"skill <slot 1-16> <skill id|name> [lv] | skill clear <slot|all> | skill cond <slot> <always|above|below> [%]",
	"hp <item|off> [%]  | sp <item|off> [%]  | sit <on|off> | sit hp <sit%> <stand%> | sit sp <sit%> <stand%>",
	"buff <slot 1-8> <skill> [lv] | buff clear <slot|all> | buffitem <slot 1-6> <item> [seconds] | buffitem clear <slot>",
	"support <slot 1-4> <skill> [lv] | support clear <slot|all> | support hp <%>",
	"loot <on|off> | loot range <1-14> | loot +<item> / -<item> / reset",
	"restock <on|off> | restock +<item> <amount> / -<item> / reset | restock min <n>",
	"safety hp <% 0=off> | safety item <item|off> | safety player <on|off> | safety weight <on|off> [%]",
	"safety weightwarp <on|off> | safety noitem <on|off> | safety idle <sec 0=off> | safety mvp <on|off> | safety mini <on|off>",
};

static const char* ab_help_th[] = {
	"=== \u0E04\u0E33\u0E2A\u0E31\u0E48\u0E07 @autobattle (@ab) ===",
	"\u0E1E\u0E34\u0E21\u0E1E\u0E4C @ab \u0E40\u0E09\u0E22\u0E46 \u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E40\u0E1B\u0E34\u0E14\u0E40\u0E21\u0E19\u0E39\u0E15\u0E31\u0E49\u0E07\u0E04\u0E48\u0E32 (\u0E07\u0E48\u0E32\u0E22\u0E17\u0E35\u0E48\u0E2A\u0E38\u0E14)    start | stop | status | mobs | reset",
	"all <on|off> \u0E42\u0E08\u0E21\u0E15\u0E35\u0E17\u0E38\u0E01\u0E15\u0E31\u0E27 | attack <on|off> \u0E42\u0E08\u0E21\u0E15\u0E35\u0E18\u0E23\u0E23\u0E21\u0E14\u0E32 | fightback <on|off> \u0E2A\u0E39\u0E49\u0E01\u0E25\u0E31\u0E1A",
	"mob +<id|\u0E0A\u0E37\u0E48\u0E2D> / -<id|\u0E0A\u0E37\u0E48\u0E2D> / reset \u0E40\u0E25\u0E37\u0E2D\u0E01\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C    blacklist +<id|\u0E0A\u0E37\u0E48\u0E2D> / -<id|\u0E0A\u0E37\u0E48\u0E2D> / reset \u0E2B\u0E49\u0E32\u0E21\u0E15\u0E35",
	"skill <\u0E0A\u0E48\u0E2D\u0E07 1-16> <id|\u0E0A\u0E37\u0E48\u0E2D\u0E2A\u0E01\u0E34\u0E25> [\u0E40\u0E25\u0E40\u0E27\u0E25] | skill clear <\u0E0A\u0E48\u0E2D\u0E07|all> | skill cond <\u0E0A\u0E48\u0E2D\u0E07> <always|above|below> [%]",
	"hp <\u0E44\u0E2D\u0E40\u0E17\u0E21|off> [%] | sp <\u0E44\u0E2D\u0E40\u0E17\u0E21|off> [%] | sit <on|off> \u0E19\u0E31\u0E48\u0E07\u0E1E\u0E31\u0E01 | sit hp <\u0E19\u0E31\u0E48\u0E07%> <\u0E25\u0E38\u0E01%> | sit sp <\u0E19\u0E31\u0E48\u0E07%> <\u0E25\u0E38\u0E01%>",
	"buff <\u0E0A\u0E48\u0E2D\u0E07 1-8> <\u0E2A\u0E01\u0E34\u0E25> [\u0E40\u0E25\u0E40\u0E27\u0E25] | buff clear <\u0E0A\u0E48\u0E2D\u0E07|all> | buffitem <\u0E0A\u0E48\u0E2D\u0E07 1-6> <\u0E44\u0E2D\u0E40\u0E17\u0E21> [\u0E27\u0E34\u0E19\u0E32\u0E17\u0E35] | buffitem clear <\u0E0A\u0E48\u0E2D\u0E07>",
	"support <\u0E0A\u0E48\u0E2D\u0E07 1-4> <\u0E2A\u0E01\u0E34\u0E25> [\u0E40\u0E25\u0E40\u0E27\u0E25] | support clear <\u0E0A\u0E48\u0E2D\u0E07|all> | support hp <%>",
	"loot <on|off> | loot range <1-14> | loot +<\u0E44\u0E2D\u0E40\u0E17\u0E21> / -<\u0E44\u0E2D\u0E40\u0E17\u0E21> / reset",
	"restock <on|off> | restock +<\u0E44\u0E2D\u0E40\u0E17\u0E21> <\u0E08\u0E33\u0E19\u0E27\u0E19> / -<\u0E44\u0E2D\u0E40\u0E17\u0E21> / reset | restock min <\u0E08\u0E33\u0E19\u0E27\u0E19>",
	"safety hp <% 0=\u0E1B\u0E34\u0E14> | safety item <\u0E44\u0E2D\u0E40\u0E17\u0E21|off> | safety player <on|off> | safety weight <on|off> [%]",
	"safety weightwarp <on|off> | safety noitem <on|off> | safety idle <\u0E27\u0E34\u0E19\u0E32\u0E17\u0E35 0=\u0E1B\u0E34\u0E14> | safety mvp <on|off> | safety mini <on|off>",
};

static bool ab_parse_onoff(const std::string& s, bool& out) {
	if (s == "on" || s == "1" || s == "yes") { out = true; return true; }
	if (s == "off" || s == "0" || s == "no") { out = false; return true; }
	return false;
}

static bool ab_is_number(const std::string& s) {
	return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return isdigit(c); });
}

static uint16 ab_find_skill(const std::string& s) {
	uint16 id = ab_is_number(s) ? (uint16)atoi(s.c_str()) : skill_name2id(s.c_str());
	return (id && skill_get_name(id) && skill_get_name(id)[0]) ? id : 0;
}

static t_itemid ab_find_item(const std::string& s) {
	if (ab_is_number(s)) {
		t_itemid id = (t_itemid)strtoul(s.c_str(), nullptr, 10);
		return item_db.find(id) ? id : 0;
	}
	std::map<t_itemid, std::shared_ptr<item_data>> found;
	if (itemdb_searchname_array(found, 200, s.c_str()) == 0)
		return 0;
	for (const auto& it : found) // prefer an exact (case-insensitive) match on the AegisName
		if (strcmpi(it.second->name.c_str(), s.c_str()) == 0)
			return it.first;
	return found.begin()->first;
}

static uint16 ab_find_mob(const std::string& s) {
	uint16 id = ab_is_number(s) ? (uint16)atoi(s.c_str()) : mobdb_searchname(s.c_str());
	return mob_db.exists(id) ? id : 0;
}

/// Range error message (always shown)
static bool ab_range_err(map_session_data& sd, int32 lo, int32 hi) {
	ab_msg(sd, "Value must be %d-%d.", "\u0E04\u0E48\u0E32\u0E15\u0E49\u0E2D\u0E07\u0E2D\u0E22\u0E39\u0E48\u0E23\u0E30\u0E2B\u0E27\u0E48\u0E32\u0E07 %d \u0E16\u0E36\u0E07 %d", lo, hi);
	return false;
}

/// +id / -id handling shared by the monster / blacklist lists
static bool ab_edit_mob_list(map_session_data& sd, std::vector<uint16>& list, size_t max, const std::string& a, bool& changed_all) {
	if (a == "reset") {
		list.clear();
		return true;
	}
	if (a.size() <= 1 || (a[0] != '+' && a[0] != '-')) {
		ab_msg(sd, "Usage: +<id|name> / -<id|name> / reset", "\u0E27\u0E34\u0E18\u0E35\u0E43\u0E0A\u0E49: +<id|\u0E0A\u0E37\u0E48\u0E2D> \u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E40\u0E1E\u0E34\u0E48\u0E21 / -<id|\u0E0A\u0E37\u0E48\u0E2D> \u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E25\u0E1A / reset \u0E40\u0E1E\u0E37\u0E48\u0E2D\u0E25\u0E49\u0E32\u0E07");
		return false;
	}
	uint16 id = ab_find_mob(a.substr(1));
	if (!id) {
		ab_msg(sd, "Unknown monster.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E21\u0E2D\u0E19\u0E2A\u0E40\u0E15\u0E2D\u0E23\u0E4C\u0E19\u0E35\u0E49");
		return false;
	}
	auto it = std::find(list.begin(), list.end(), id);
	if (a[0] == '+') {
		if (it == list.end()) {
			if (list.size() >= max) {
				ab_msg(sd, "The list is full (%d).", "\u0E23\u0E32\u0E22\u0E01\u0E32\u0E23\u0E40\u0E15\u0E47\u0E21\u0E41\u0E25\u0E49\u0E27 (%d)", (int32)max);
				return false;
			}
			list.push_back(id);
		}
		changed_all = true;
		if (!ab_quiet)
			ab_msg(sd, "Added %s.", "\u0E40\u0E1E\u0E34\u0E48\u0E21 %s \u0E41\u0E25\u0E49\u0E27", ab_mob_name(id));
	} else if (it != list.end()) {
		list.erase(it);
		if (!ab_quiet)
			ab_msg(sd, "Removed %s.", "\u0E25\u0E1A %s \u0E41\u0E25\u0E49\u0E27", ab_mob_name(id));
	}
	return true;
}

bool autobattle_command(map_session_data& sd, const char* message, bool quiet) {
	auto ab = autobattle_get(sd);
	std::vector<std::string> tok;
	{
		std::istringstream iss(message ? message : "");
		std::string t;
		while (iss >> t) {
			std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)tolower(c); });
			tok.push_back(t);
		}
	}
	ab_quiet = quiet;
	struct QuietReset { ~QuietReset() { ab_quiet = false; } } quiet_reset;
	auto arg = [&](size_t i) -> std::string { return i < tok.size() ? tok[i] : std::string(); };
	auto help = [&]() {
		if (ab_thai())
			for (const char* l : ab_help_th) { std::string s = ab_tis(l); clif_displaymessage(sd.fd, s.c_str()); }
		else
			for (const char* l : ab_help_en) clif_displaymessage(sd.fd, l);
	};

	if (tok.empty() || tok[0] == "menu") {
		if (sd.npc_id) {
			ab_msg(sd, "Close the current NPC dialog first.", "\u0E01\u0E23\u0E38\u0E13\u0E32\u0E1B\u0E34\u0E14\u0E2B\u0E19\u0E49\u0E32\u0E15\u0E48\u0E32\u0E07 NPC \u0E17\u0E35\u0E48\u0E40\u0E1B\u0E34\u0E14\u0E2D\u0E22\u0E39\u0E48\u0E01\u0E48\u0E2D\u0E19");
			return false;
		}
		if (!sd.vars_ok) {
			ab_msg(sd, "Your data is still loading, please try again.", "\u0E02\u0E49\u0E2D\u0E21\u0E39\u0E25\u0E15\u0E31\u0E27\u0E25\u0E30\u0E04\u0E23\u0E22\u0E31\u0E07\u0E42\u0E2B\u0E25\u0E14\u0E44\u0E21\u0E48\u0E40\u0E2A\u0E23\u0E47\u0E08 \u0E01\u0E23\u0E38\u0E13\u0E32\u0E25\u0E2D\u0E07\u0E43\u0E2B\u0E21\u0E48\u0E2D\u0E35\u0E01\u0E04\u0E23\u0E31\u0E49\u0E07");
			return false;
		}
		npc_event(&sd, "AutoBattleUI::OnOpen", 0);
		return true;
	}
	if (tok[0] == "help") {
		help();
		return true;
	}
	if (!battle_config.autobattle_enable && tok[0] != "status") {
		ab_msg(sd, "Auto Battle is disabled on this server.", "\u0E40\u0E0B\u0E34\u0E23\u0E4C\u0E1F\u0E40\u0E27\u0E2D\u0E23\u0E4C\u0E19\u0E35\u0E49\u0E1B\u0E34\u0E14\u0E23\u0E30\u0E1A\u0E1A Auto Battle");
		return false;
	}
	if (!sd.vars_ok && tok[0] != "status") {
		ab_msg(sd, "Your data is still loading, please try again.", "\u0E02\u0E49\u0E2D\u0E21\u0E39\u0E25\u0E15\u0E31\u0E27\u0E25\u0E30\u0E04\u0E23\u0E22\u0E31\u0E07\u0E42\u0E2B\u0E25\u0E14\u0E44\u0E21\u0E48\u0E40\u0E2A\u0E23\u0E47\u0E08 \u0E01\u0E23\u0E38\u0E13\u0E32\u0E25\u0E2D\u0E07\u0E43\u0E2B\u0E21\u0E48\u0E2D\u0E35\u0E01\u0E04\u0E23\u0E31\u0E49\u0E07");
		return false;
	}

	const std::string& c = tok[0];
	bool changed = true, b = false;

	if (c == "start" || c == "on") { autobattle_start(sd); changed = false; }
	else if (c == "stop" || c == "off") {
		if (ab->running) autobattle_stop(sd);
		else ab_msg(sd, "Auto Battle is not running.", "Auto Battle \u0E44\u0E21\u0E48\u0E44\u0E14\u0E49\u0E17\u0E33\u0E07\u0E32\u0E19\u0E2D\u0E22\u0E39\u0E48");
		changed = false;
	}
	else if (c == "toggle") { if (ab->running) autobattle_stop(sd); else autobattle_start(sd); changed = false; }
	else if (c == "status") { autobattle_status(sd); changed = false; }
	else if (c == "sync") { autobattle_save(sd); changed = false; }
	else if (c == "reset") {
		if (ab->running) {
			ab_msg(sd, "Stop Auto Battle first.", "\u0E01\u0E23\u0E38\u0E13\u0E32\u0E2B\u0E22\u0E38\u0E14 Auto Battle \u0E01\u0E48\u0E2D\u0E19");
			return false;
		}
		*ab = s_autobattle();
		ab->loaded = true;
		ab_msg(sd, "All Auto Battle settings were reset.", "\u0E23\u0E35\u0E40\u0E0B\u0E47\u0E15\u0E01\u0E32\u0E23\u0E15\u0E31\u0E49\u0E07\u0E04\u0E48\u0E32 Auto Battle \u0E17\u0E31\u0E49\u0E07\u0E2B\u0E21\u0E14\u0E41\u0E25\u0E49\u0E27");
	}
	else if (c == "skills") { ab_list_skills(sd, *ab, arg(1)); changed = false; }
	else if (c == "items") { ab_list_items(sd, *ab, atoi(arg(1).c_str())); changed = false; }
	else if (c == "mapmobs") { autobattle_list_map_mobs(sd); changed = false; }
	else if (c == "mobs") { ab_quiet = false; autobattle_list_map_mobs(sd); changed = false; }
	else if (c == "all" && ab_parse_onoff(arg(1), b)) ab->all_monsters = b;
	else if (c == "attack" && ab_parse_onoff(arg(1), b)) ab->normal_attack = b;
	else if (c == "fightback" && ab_parse_onoff(arg(1), b)) ab->fight_back = b;
	else if (c == "mob") {
		bool added = false;
		if (!ab_edit_mob_list(sd, ab->mobs, AB_MAX_MOBS, arg(1), added)) return false;
		if (added) ab->all_monsters = false; // choosing a monster means "only these"
	}
	else if (c == "blacklist") {
		bool added = false;
		if (!ab_edit_mob_list(sd, ab->blacklist, AB_MAX_BLACKLIST, arg(1), added)) return false;
	}
	else if (c == "skill" && arg(1) == "cond") {
		int32 slot = atoi(arg(2).c_str());
		if (slot < 1 || slot > AB_MAX_ATTACK_SLOTS) { ab_msg(sd, "Slot must be 1-%d.", "\u0E0A\u0E48\u0E2D\u0E07\u0E15\u0E49\u0E2D\u0E07\u0E40\u0E1B\u0E47\u0E19\u0E40\u0E25\u0E02 1-%d", AB_MAX_ATTACK_SLOTS); return false; }
		s_ab_skill& s = ab->attack[slot - 1];
		std::string m = arg(3);
		if (m == "always") { s.cond = ABC_ALWAYS; s.cond_val = 0; }
		else if (m == "above" || m == "below") {
			int32 v = atoi(arg(4).c_str());
			if (v < 1 || v > 99) return ab_range_err(sd, 1, 99);
			s.cond = m == "above" ? ABC_MOB_HP_ABOVE : ABC_MOB_HP_BELOW;
			s.cond_val = (uint8)v;
		} else { help(); return false; }
	}
	else if (c == "skill" || c == "buff" || c == "support") {
		s_ab_skill* slots = c == "skill" ? ab->attack : c == "buff" ? ab->buffs : ab->support;
		int32 n = c == "skill" ? AB_MAX_ATTACK_SLOTS : c == "buff" ? AB_MAX_BUFF_SLOTS : AB_MAX_SUPPORT_SLOTS;
		if (c == "support" && arg(1) == "hp") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 100) return ab_range_err(sd, 1, 100);
			ab->support_hp_pct = (uint8)v;
		} else if (arg(1) == "clear") {
			int32 slot = atoi(arg(2).c_str());
			if (arg(2) == "all") { for (int32 i = 0; i < n; i++) slots[i] = s_ab_skill(); }
			else if (slot >= 1 && slot <= n) slots[slot - 1] = s_ab_skill();
			else { ab_msg(sd, "Slot must be 1-%d.", "\u0E0A\u0E48\u0E2D\u0E07\u0E15\u0E49\u0E2D\u0E07\u0E40\u0E1B\u0E47\u0E19\u0E40\u0E25\u0E02 1-%d", n); return false; }
		} else {
			int32 slot = atoi(arg(1).c_str());
			if (slot < 1 || slot > n) { ab_msg(sd, "Slot must be 1-%d.", "\u0E0A\u0E48\u0E2D\u0E07\u0E15\u0E49\u0E2D\u0E07\u0E40\u0E1B\u0E47\u0E19\u0E40\u0E25\u0E02 1-%d", n); return false; }
			uint16 id = ab_find_skill(arg(2));
			if (!id) { ab_msg(sd, "Unknown skill.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E2A\u0E01\u0E34\u0E25\u0E19\u0E35\u0E49"); return false; }
			uint16 learned = pc_checkskill(&sd, id);
			if (!learned) { ab_msg(sd, "You have not learned %s.", "\u0E04\u0E38\u0E13\u0E22\u0E31\u0E07\u0E44\u0E21\u0E48\u0E44\u0E14\u0E49\u0E40\u0E23\u0E35\u0E22\u0E19\u0E2A\u0E01\u0E34\u0E25 %s", skill_get_desc(id)); return false; }
			int32 lv = arg(3).empty() ? learned : atoi(arg(3).c_str());
			lv = std::clamp<int32>(lv, 1, learned);
			for (int32 i = 0; i < n; i++) // no duplicates in the same list
				if (slots[i].skill_id == id && i != slot - 1) slots[i] = s_ab_skill();
			slots[slot - 1] = s_ab_skill();
			slots[slot - 1].skill_id = id;
			slots[slot - 1].level = (uint16)lv;
			if (!ab_quiet)
				ab_msg(sd, "Slot %d: %s Lv%d", "\u0E0A\u0E48\u0E2D\u0E07 %d: %s Lv%d", slot, skill_get_desc(id), lv);
		}
	}
	else if (c == "buffitem") {
		if (arg(1) == "clear") {
			int32 slot = atoi(arg(2).c_str());
			if (arg(2) == "all") { for (auto& bi : ab->buff_items) bi = s_ab_buffitem(); }
			else if (slot >= 1 && slot <= AB_MAX_BUFFITEMS) ab->buff_items[slot - 1] = s_ab_buffitem();
			else { ab_msg(sd, "Slot must be 1-%d.", "\u0E0A\u0E48\u0E2D\u0E07\u0E15\u0E49\u0E2D\u0E07\u0E40\u0E1B\u0E47\u0E19\u0E40\u0E25\u0E02 1-%d", AB_MAX_BUFFITEMS); return false; }
		} else {
			int32 slot = atoi(arg(1).c_str());
			if (slot < 1 || slot > AB_MAX_BUFFITEMS) { ab_msg(sd, "Slot must be 1-%d.", "\u0E0A\u0E48\u0E2D\u0E07\u0E15\u0E49\u0E2D\u0E07\u0E40\u0E1B\u0E47\u0E19\u0E40\u0E25\u0E02 1-%d", AB_MAX_BUFFITEMS); return false; }
			t_itemid id = ab_find_item(arg(2));
			if (!id) { ab_msg(sd, "Unknown item.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E19\u0E35\u0E49"); return false; }
			int32 sec = arg(3).empty() ? 600 : atoi(arg(3).c_str());
			if (sec < 10 || sec > 3600) return ab_range_err(sd, 10, 3600);
			ab->buff_items[slot - 1] = s_ab_buffitem();
			ab->buff_items[slot - 1].nameid = id;
			ab->buff_items[slot - 1].interval = (uint16)sec;
		}
	}
	else if (c == "hp" || c == "sp") {
		t_itemid& item = c == "hp" ? ab->hp_item : ab->sp_item;
		uint8& pct = c == "hp" ? ab->hp_pct : ab->sp_pct;
		if (arg(1) == "off") item = 0;
		else if (arg(1).empty()) {
			help();
			return false;
		} else {
			t_itemid id = ab_find_item(arg(1));
			if (!id) { ab_msg(sd, "Unknown item.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E19\u0E35\u0E49"); return false; }
			item = id;
			if (!arg(2).empty()) {
				int32 v = atoi(arg(2).c_str());
				if (v < 1 || v > 99) return ab_range_err(sd, 1, 99);
				pct = (uint8)v;
			}
			if (!ab_quiet)
				ab_msg(sd, "%s item: %s below %d%%", "\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E1F\u0E37\u0E49\u0E19 %s: %s \u0E40\u0E21\u0E37\u0E48\u0E2D\u0E15\u0E48\u0E33\u0E01\u0E27\u0E48\u0E32 %d%%", c == "hp" ? "HP" : "SP", ab_item_name(id), pct);
		}
	}
	else if (c == "pct") {
		// @ab pct hp <1-99> / @ab pct sp <1-99> : change only the percentage
		int32 v = atoi(arg(2).c_str());
		if (v < 1 || v > 99) return ab_range_err(sd, 1, 99);
		if (arg(1) == "hp") ab->hp_pct = (uint8)v;
		else if (arg(1) == "sp") ab->sp_pct = (uint8)v;
		else { help(); return false; }
	}
	else if (c == "sit") {
		std::string a = arg(1);
		if (ab_parse_onoff(a, b)) ab->sit_rest = b;
		else if (a == "hp" || a == "sp") {
			int32 s = atoi(arg(2).c_str()), u = atoi(arg(3).c_str());
			if (s < 1 || s > 95 || u < 2 || u > 100 || u <= s) return ab_range_err(sd, 1, 100);
			if (a == "hp") { ab->sit_hp = (uint8)s; ab->stand_hp = (uint8)u; }
			else { ab->sit_sp = (uint8)s; ab->stand_sp = (uint8)u; }
		} else { help(); return false; }
	}
	else if (c == "loot") {
		std::string a = arg(1);
		if (ab_parse_onoff(a, b)) ab->loot = b;
		else if (a == "range") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 14) return ab_range_err(sd, 1, 14);
			ab->loot_range = (uint8)v;
		} else if (a == "reset") ab->loot_ids.clear();
		else if (a.size() > 1 && (a[0] == '+' || a[0] == '-')) {
			t_itemid id = ab_find_item(a.substr(1));
			if (!id) { ab_msg(sd, "Unknown item.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E19\u0E35\u0E49"); return false; }
			auto it = std::find(ab->loot_ids.begin(), ab->loot_ids.end(), id);
			if (a[0] == '+') {
				if (it == ab->loot_ids.end()) {
					if (ab->loot_ids.size() >= AB_MAX_LOOT_IDS) { ab_msg(sd, "The list is full (%d).", "\u0E23\u0E32\u0E22\u0E01\u0E32\u0E23\u0E40\u0E15\u0E47\u0E21\u0E41\u0E25\u0E49\u0E27 (%d)", AB_MAX_LOOT_IDS); return false; }
					ab->loot_ids.push_back(id);
				}
			} else if (it != ab->loot_ids.end()) ab->loot_ids.erase(it);
		} else { help(); return false; }
	}
	else if (c == "restock") {
		std::string a = arg(1);
		if (ab_parse_onoff(a, b)) ab->restock = b;
		else if (a == "min") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 30000) return ab_range_err(sd, 1, 30000);
			ab->restock_min = (uint16)v;
		} else if (a == "reset") ab->restock_items.clear();
		else if (a.size() > 1 && (a[0] == '+' || a[0] == '-')) {
			t_itemid id = ab_find_item(a.substr(1));
			if (!id) { ab_msg(sd, "Unknown item.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E19\u0E35\u0E49"); return false; }
			auto it = std::find_if(ab->restock_items.begin(), ab->restock_items.end(), [&](const s_ab_restock& r) { return r.nameid == id; });
			if (a[0] == '+') {
				int32 amount = atoi(arg(2).c_str());
				if (amount < 1 || amount > 30000) return ab_range_err(sd, 1, 30000);
				auto data = item_db.find(id);
				if (!data->value_buy || !data->isStackable()) {
					ab_msg(sd, "%s cannot be bought.", "\u0E44\u0E21\u0E48\u0E2A\u0E32\u0E21\u0E32\u0E23\u0E16\u0E0B\u0E37\u0E49\u0E2D %s \u0E08\u0E32\u0E01 NPC \u0E44\u0E14\u0E49", data->ename.c_str());
					return false;
				}
				if (it != ab->restock_items.end()) it->amount = (uint16)amount;
				else {
					if (ab->restock_items.size() >= AB_MAX_RESTOCK) { ab_msg(sd, "The list is full (%d).", "\u0E23\u0E32\u0E22\u0E01\u0E32\u0E23\u0E40\u0E15\u0E47\u0E21\u0E41\u0E25\u0E49\u0E27 (%d)", AB_MAX_RESTOCK); return false; }
					s_ab_restock r; r.nameid = id; r.amount = (uint16)amount;
					ab->restock_items.push_back(r);
				}
			} else if (it != ab->restock_items.end()) ab->restock_items.erase(it);
		} else { help(); return false; }
	}
	else if (c == "safety") {
		std::string a = arg(1);
		if (a == "hp") {
			int32 v = atoi(arg(2).c_str());
			if (v < 0 || v > 90) return ab_range_err(sd, 0, 90);
			ab->flee_hp_pct = (uint8)v;
		} else if (a == "item") {
			if (arg(2) == "off") ab->flee_item = 0;
			else {
				t_itemid id = ab_find_item(arg(2));
				if (!id) { ab_msg(sd, "Unknown item.", "\u0E44\u0E21\u0E48\u0E1E\u0E1A\u0E44\u0E2D\u0E40\u0E17\u0E21\u0E19\u0E35\u0E49"); return false; }
				ab->flee_item = id;
			}
		} else if (a == "player" && ab_parse_onoff(arg(2), b)) ab->stop_on_player = b;
		else if (a == "weight" && ab_parse_onoff(arg(2), b)) {
			ab->stop_on_weight = b;
			if (!arg(3).empty()) {
				int32 v = atoi(arg(3).c_str());
				if (v < 10 || v > 100) return ab_range_err(sd, 10, 100);
				ab->weight_pct = (uint8)v;
			}
		} else if (a == "weightwarp" && ab_parse_onoff(arg(2), b)) ab->weight_warp = b;
		else if (a == "noitem" && ab_parse_onoff(arg(2), b)) ab->stop_no_item = b;
		else if (a == "mvp" && ab_parse_onoff(arg(2), b)) ab->flee_mvp = b;
		else if (a == "mini" && ab_parse_onoff(arg(2), b)) ab->flee_mini = b;
		else if (a == "idle") {
			int32 v = atoi(arg(2).c_str());
			if (v < 0 || v > 3600) return ab_range_err(sd, 0, 3600);
			ab->idle_seconds = (uint16)v;
		} else { help(); return false; }
	}
	else { help(); return false; }

	if (changed) {
		autobattle_save(sd);
		if (!ab_quiet && c != "reset")
			ab_msg(sd, "OK.", "\u0E1A\u0E31\u0E19\u0E17\u0E36\u0E01\u0E41\u0E25\u0E49\u0E27");
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

void do_init_autobattle() {
	add_timer_func_list(ab_timer, "ab_timer");
}

void do_final_autobattle() {
}
