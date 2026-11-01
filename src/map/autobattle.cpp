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

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "status.hpp"
#include "unit.hpp"

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

static TIMER_FUNC(ab_timer);

static void ab_msg(map_session_data& sd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void ab_msg(map_session_data& sd, const char* fmt, ...) {
	char buf[CHAT_SIZE_MAX];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	clif_displaymessage(sd.fd, buf);
}

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
// Persistence (character variables, so the settings survive relogging)
// ---------------------------------------------------------------------------------------------

enum {
	ABF_SAVED = 1 << 30, ///< marks "settings were stored at least once"
	ABF_ALL = 1 << 0,
	ABF_NORMAL = 1 << 1,
	ABF_FIGHTBACK = 1 << 2,
	ABF_LOOT = 1 << 3,
	ABF_RESTOCK = 1 << 4,
	ABF_STOPPLAYER = 1 << 5,
	ABF_STOPWEIGHT = 1 << 6,
	ABF_STOPNOITEM = 1 << 7,
};

static int64 ab_key(const char* name, int32 idx = 0) {
	return reference_uid(add_str(name), idx);
}

static void ab_save_slots(map_session_data& sd, const char* name, const s_ab_skill* slots, int32 n) {
	for (int32 i = 0; i < n; i++)
		pc_setreg(&sd, ab_key(name, i), slots[i].skill_id ? (((int64)slots[i].skill_id << 16) | slots[i].level) : 0);
}

static void ab_load_slots(map_session_data& sd, const char* name, s_ab_skill* slots, int32 n) {
	for (int32 i = 0; i < n; i++) {
		int64 v = pc_readreg(&sd, ab_key(name, i));
		slots[i].skill_id = (uint16)(v >> 16);
		slots[i].level = (uint16)(v & 0xFFFF);
	}
}

void autobattle_save(map_session_data& sd) {
	if (!sd.autobattle)
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
	pc_setreg(&sd, ab_key("ab_flags"), flags);
	pc_setreg(&sd, ab_key("ab_hpitem"), ab.hp_item);
	pc_setreg(&sd, ab_key("ab_spitem"), ab.sp_item);
	pc_setreg(&sd, ab_key("ab_fleeitem"), ab.flee_item ? ab.flee_item : -1);
	pc_setreg(&sd, ab_key("ab_pct"), (int64)ab.hp_pct | ((int64)ab.sp_pct << 8) | ((int64)ab.support_hp_pct << 16)
		| ((int64)ab.flee_hp_pct << 24) | ((int64)ab.weight_pct << 32) | ((int64)ab.loot_range << 40));
	pc_setreg(&sd, ab_key("ab_misc"), (int64)ab.restock_min | ((int64)ab.idle_seconds << 16) | (1LL << 40));
	ab_save_slots(sd, "ab_atk", ab.attack, AB_MAX_ATTACK_SLOTS);
	ab_save_slots(sd, "ab_buff", ab.buffs, AB_MAX_BUFF_SLOTS);
	ab_save_slots(sd, "ab_sup", ab.support, AB_MAX_SUPPORT_SLOTS);
	for (int32 i = 0; i < AB_MAX_MOBS; i++)
		pc_setreg(&sd, ab_key("ab_mob", i), i < (int32)ab.mobs.size() ? ab.mobs[i] : 0);
	for (int32 i = 0; i < AB_MAX_LOOT_IDS; i++)
		pc_setreg(&sd, ab_key("ab_loot", i), i < (int32)ab.loot_ids.size() ? ab.loot_ids[i] : 0);
	for (int32 i = 0; i < AB_MAX_RESTOCK; i++)
		pc_setreg(&sd, ab_key("ab_rs", i), i < (int32)ab.restock_items.size()
			? (((int64)ab.restock_items[i].nameid << 16) | ab.restock_items[i].amount) : 0);
}

static void ab_load(map_session_data& sd, s_autobattle& ab) {
	int64 flags = pc_readreg(&sd, ab_key("ab_flags"));
	if (!(flags & ABF_SAVED))
		return; // keep defaults
	ab.all_monsters = flags & ABF_ALL;
	ab.normal_attack = flags & ABF_NORMAL;
	ab.fight_back = flags & ABF_FIGHTBACK;
	ab.loot = flags & ABF_LOOT;
	ab.restock = flags & ABF_RESTOCK;
	ab.stop_on_player = flags & ABF_STOPPLAYER;
	ab.stop_on_weight = flags & ABF_STOPWEIGHT;
	ab.stop_no_item = flags & ABF_STOPNOITEM;
	ab.hp_item = (t_itemid)pc_readreg(&sd, ab_key("ab_hpitem"));
	ab.sp_item = (t_itemid)pc_readreg(&sd, ab_key("ab_spitem"));
	int64 flee = pc_readreg(&sd, ab_key("ab_fleeitem"));
	ab.flee_item = flee > 0 ? (t_itemid)flee : 0;
	int64 pct = pc_readreg(&sd, ab_key("ab_pct"));
	ab.hp_pct = pct & 0xFF;
	ab.sp_pct = (pct >> 8) & 0xFF;
	ab.support_hp_pct = (pct >> 16) & 0xFF;
	ab.flee_hp_pct = (pct >> 24) & 0xFF;
	ab.weight_pct = (pct >> 32) & 0xFF;
	ab.loot_range = (pct >> 40) & 0xFF;
	int64 misc = pc_readreg(&sd, ab_key("ab_misc"));
	if (misc & (1LL << 40)) {
		ab.restock_min = misc & 0xFFFF;
		ab.idle_seconds = (misc >> 16) & 0xFFFF;
	}
	if (ab.loot_range < 1 || ab.loot_range > 14) ab.loot_range = 8;
	if (ab.weight_pct < 10 || ab.weight_pct > 100) ab.weight_pct = 90;
	ab_load_slots(sd, "ab_atk", ab.attack, AB_MAX_ATTACK_SLOTS);
	ab_load_slots(sd, "ab_buff", ab.buffs, AB_MAX_BUFF_SLOTS);
	ab_load_slots(sd, "ab_sup", ab.support, AB_MAX_SUPPORT_SLOTS);
	for (int32 i = 0; i < AB_MAX_MOBS; i++)
		if (uint16 v = (uint16)pc_readreg(&sd, ab_key("ab_mob", i)))
			ab.mobs.push_back(v);
	for (int32 i = 0; i < AB_MAX_LOOT_IDS; i++)
		if (t_itemid v = (t_itemid)pc_readreg(&sd, ab_key("ab_loot", i)))
			ab.loot_ids.push_back(v);
	for (int32 i = 0; i < AB_MAX_RESTOCK; i++) {
		int64 v = pc_readreg(&sd, ab_key("ab_rs", i));
		if (v) {
			s_ab_restock r;
			r.nameid = (t_itemid)(v >> 16);
			r.amount = (uint16)(v & 0xFFFF);
			ab.restock_items.push_back(r);
		}
	}
}

std::shared_ptr<s_autobattle> autobattle_get(map_session_data& sd) {
	if (!sd.autobattle) {
		sd.autobattle = std::make_shared<s_autobattle>();
		ab_load(sd, *sd.autobattle);
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
	if ((int32)sd.battle_status.sp < skill_get_sp(id, lv) || (int32)sd.battle_status.hp <= skill_get_hp(id, lv))
		return ABS_NO;

	int32 inf = skill_get_inf(id);
	int32 result = 0;

	if (inf & INF_SELF_SKILL) {
		sc_type sct = skill_get_sc(id);
		if (sct != SC_NONE && sd.sc.getSCE(sct))
			return ABS_NO; // already active
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
		sk.next_try = tick + 1500; // something blocked it (items, weapon, status...)
		return ABS_NO;
	}
	// Skills without a status icon (and no cooldown) are naturally limited by their delay;
	// don't spam them faster than once per 300ms.
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

// ---------------------------------------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------------------------------------

bool autobattle_start(map_session_data& sd, bool from_item) {
	if (!battle_config.autobattle_enable) {
		ab_msg(sd, "Auto Battle is disabled on this server.");
		return false;
	}
	if (pc_isdead(&sd)) {
		ab_msg(sd, "You cannot start Auto Battle while dead.");
		return false;
	}
	if (map_flag_vs(sd.m)) {
		ab_msg(sd, "Auto Battle cannot be used on PvP / WoE / Battleground maps.");
		return false;
	}
	if (sd.state.vending || sd.state.buyingstore || sd.state.autotrade) {
		ab_msg(sd, "Auto Battle cannot be used while a shop is open.");
		return false;
	}
	auto ab = autobattle_get(sd);
	if (ab->running) {
		ab_msg(sd, "Auto Battle is already running.");
		return false;
	}
	bool has_attack = ab->normal_attack;
	for (const auto& sk : ab->attack)
		if (sk.skill_id)
			has_attack = true;
	if (!has_attack) {
		ab_msg(sd, "Enable normal attack or add an attack skill first (@ab help).");
		return false;
	}
	if (!ab->all_monsters && ab->mobs.empty() && !ab->fight_back) {
		ab_msg(sd, "Select target monsters first (@ab all on / @ab mob +<id>).");
		return false;
	}

	ab->running = true;
	ab->phase = ABP_HUNT;
	ab->target_id = 0;
	ab->engaged = 0;
	ab->ignore.clear();
	ab->anchor_m = sd.m;
	ab->anchor_x = sd.x;
	ab->anchor_y = sd.y;
	ab->start_tick = ab->last_target_tick = gettick();
	ab->next_roam = ab->next_hp_item = ab->next_sp_item = ab->next_flee_item = 0;
	ab->next_restock_check = ab->start_tick + 3000;
	for (auto& sk : ab->attack) sk.next_try = 0;
	for (auto& sk : ab->buffs) sk.next_try = 0;
	for (auto& sk : ab->support) sk.next_try = 0;

	if (ab->timer != INVALID_TIMER)
		delete_timer(ab->timer, ab_timer);
	ab->timer = add_timer(gettick() + battle_config.autobattle_interval, ab_timer, sd.id, 0);
	ab_msg(sd, "Auto Battle started.");
	return true;
}

void autobattle_stop(map_session_data& sd, const char* reason) {
	if (!sd.autobattle)
		return;
	auto ab = sd.autobattle;
	if (ab->timer != INVALID_TIMER) {
		delete_timer(ab->timer, ab_timer);
		ab->timer = INVALID_TIMER;
	}
	bool was_running = ab->running;
	ab->running = false;
	ab->target_id = 0;
	ab->phase = ABP_HUNT;
	if (was_running) {
		if (sd.prev != nullptr) {
			unit_stop_attack(&sd);
			unit_stop_walking(&sd, USW_FIXPOS);
		}
		if (sd.fd && session_isActive(sd.fd)) {
			if (reason)
				ab_msg(sd, "Auto Battle stopped: %s", reason);
			else
				ab_msg(sd, "Auto Battle stopped.");
		}
	}
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

/// @return false when auto battle was stopped
static bool ab_safety(map_session_data& sd, s_autobattle& ab, t_tick tick) {
	// Low HP: use escape item and stop
	if (ab.flee_hp_pct && (int64)sd.battle_status.hp * 100 < (int64)sd.battle_status.max_hp * ab.flee_hp_pct) {
		bool used = ab.flee_item && ab_use_item(sd, ab.flee_item, tick);
		// Try to heal first when we still have a potion and nothing to flee with
		if (!used && ab.hp_item && ab_use_item(sd, ab.hp_item, tick))
			return true;
		autobattle_stop(sd, used ? "HP too low, escape item used." : "HP too low.");
		return false;
	}
	if (ab.stop_on_player) {
		int32 count = 0;
		map_foreachinrange(ab_count_players_sub, &sd, AREA_SIZE, BL_PC, &sd, &count);
		if (count > 0) {
			autobattle_stop(sd, "another player is nearby.");
			return false;
		}
	}
	if (ab.stop_on_weight && pc_getpercentweight(sd) >= ab.weight_pct && !ab.restock) {
		autobattle_stop(sd, "you are carrying too much.");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Recovery / buffs / support
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
	if (ab.all_monsters || std::find(ab.mobs.begin(), ab.mobs.end(), md.mob_id) != ab.mobs.end())
		return true;
	return ab.fight_back && md.target_id == sd.id;
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
		if (need <= 0 || !id || id->value_buy == 0 || !id->isStackable())
			continue;
		int32 price = std::max(1, pc_modifybuyvalue(&sd, id->value_buy));
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
			ab_msg(sd, "Restock: bought %d x %s for %dz.", n, id->ename.c_str(), price * n);
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
		autobattle_stop(sd, "could not warp to the save point for restocking.");
		return false;
	}
	if (!ab_buy_items(sd, ab)) {
		autobattle_stop(sd, "restock failed (not enough zeny / weight, or item not sold).");
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
		if (!map_getcell(sd.m, x, y, CELL_CHKPASS) || !unit_can_reach_pos(&sd, x, y, 0))
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
		if (!sk.skill_id)
			continue;
		e_ab_skill_result r = ab_use_skill(sd, sk, &target, tick, &need_range);
		if (r == ABS_USED)
			return;
	}

	if (ab.normal_attack) {
		if (sd.ud.target != target.id || sd.ud.attacktimer == INVALID_TIMER)
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
		autobattle_stop(sd, "disabled by the server.");
		return;
	}
	if (pc_isdead(&sd)) {
		autobattle_stop(sd, "you died.");
		return;
	}
	if (sd.state.vending || sd.state.buyingstore) {
		autobattle_stop(sd, "a shop was opened.");
		return;
	}
	if (sd.prev == nullptr || sd.state.warping || sd.state.changemap || sd.npc_id || sd.state.workinprogress)
		return; // busy (loading map / NPC dialogue / etc.)
	if (map_flag_vs(sd.m)) {
		autobattle_stop(sd, "this map does not allow Auto Battle.");
		return;
	}

	// Waiting to go back after restock
	if (ab.phase == ABP_RETURNING) {
		if (DIFF_TICK(tick, ab.return_tick) < 0)
			return;
		if (!ab.return_mapindex || pc_setpos(&sd, ab.return_mapindex, ab.return_x, ab.return_y, CLR_TELEPORT) != SETPOS_OK) {
			autobattle_stop(sd, "could not return to the hunting spot.");
			return;
		}
		ab.phase = ABP_HUNT;
		ab.anchor_m = -1;
		ab.last_target_tick = tick;
		return;
	}

	if (ab.anchor_m != sd.m) {
		ab.anchor_m = sd.m;
		ab.anchor_x = sd.x;
		ab.anchor_y = sd.y;
		ab.target_id = 0;
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
				autobattle_stop(sd, "out of HP recovery items.");
				return;
			}
			if (ab.sp_item && !ab_item_count(sd, ab.sp_item)) {
				autobattle_stop(sd, "out of SP recovery items.");
				return;
			}
		}
	}

	if (sd.ud.skilltimer != INVALID_TIMER)
		return;

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
// Map monster list (the "Target monsters" box)
// ---------------------------------------------------------------------------------------------

static int32 ab_count_mobs_sub(block_list* bl, va_list ap) {
	typedef std::map<uint16, int32> mobcount_t;
	mobcount_t* counts = va_arg(ap, mobcount_t*);
	mob_data* md = (mob_data*)bl;
	if (!md->special_state.ai)
		(*counts)[md->mob_id]++;
	return 0;
}

void autobattle_list_map_mobs(map_session_data& sd) {
	std::map<uint16, int32> counts;
	map_foreachinmap(ab_count_mobs_sub, sd.m, BL_MOB, &counts);
	std::vector<std::pair<uint16, int32>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

	auto ab = autobattle_get(sd);
	ab_msg(sd, "Target monsters (%s) - attack every monster: %s", map_getmapdata(sd.m)->name, ab->all_monsters ? "ON" : "OFF");
	int32 shown = 0;
	for (const auto& p : sorted) {
		if (++shown > 15) {
			ab_msg(sd, "  ...");
			break;
		}
		bool selected = std::find(ab->mobs.begin(), ab->mobs.end(), p.first) != ab->mobs.end();
		ab_msg(sd, "  [%c] %s (%d)  id:%d", selected ? 'x' : ' ', ab_mob_name(p.first), p.second, p.first);
	}
	if (sorted.empty())
		ab_msg(sd, "  (no monsters on this map)");
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

static const char* onoff(bool b) { return b ? "ON" : "OFF"; }

static void ab_show_slots(map_session_data& sd, const char* title, const s_ab_skill* slots, int32 n) {
	std::string line;
	for (int32 i = 0; i < n; i++) {
		if (!slots[i].skill_id)
			continue;
		char buf[96];
		snprintf(buf, sizeof(buf), "[%d] %s Lv%d  ", i + 1, skill_get_desc(slots[i].skill_id), slots[i].level);
		line += buf;
		if (line.size() > 80) {
			ab_msg(sd, "  %s", line.c_str());
			line.clear();
		}
	}
	if (line.empty() && !title[0])
		return;
	ab_msg(sd, "%s%s", title, line.empty() ? " (none)" : "");
	if (!line.empty())
		ab_msg(sd, "  %s", line.c_str());
}

void autobattle_status(map_session_data& sd) {
	auto ab = autobattle_get(sd);
	ab_msg(sd, "=== Auto Battle: %s ===", ab->running ? "RUNNING" : "Stopped");
	ab_msg(sd, "[Hunt] all monsters:%s normal attack:%s fight back:%s", onoff(ab->all_monsters), onoff(ab->normal_attack), onoff(ab->fight_back));
	if (!ab->mobs.empty()) {
		std::string s;
		for (uint16 m : ab->mobs) s += std::string(ab_mob_name(m)) + "(" + std::to_string(m) + ") ";
		ab_msg(sd, "  monsters: %s", s.c_str());
	}
	ab_show_slots(sd, "  Attack skills:", ab->attack, AB_MAX_ATTACK_SLOTS);
	ab_msg(sd, "[Recovery] HP: %s <%d%%  SP: %s <%d%%",
		ab->hp_item ? ab_item_name(ab->hp_item) : "-", ab->hp_pct, ab->sp_item ? ab_item_name(ab->sp_item) : "-", ab->sp_pct);
	ab_show_slots(sd, "[Buffs]", ab->buffs, AB_MAX_BUFF_SLOTS);
	ab_show_slots(sd, "[Support]", ab->support, AB_MAX_SUPPORT_SLOTS);
	ab_msg(sd, "  support heals when HP < %d%%", ab->support_hp_pct);
	ab_msg(sd, "[Loot] %s range:%d filter:%s", onoff(ab->loot), ab->loot_range, ab->loot_ids.empty() ? "everything" : "listed items only");
	ab_msg(sd, "[Restock] %s (below %d)", onoff(ab->restock), ab->restock_min);
	for (const auto& r : ab->restock_items)
		ab_msg(sd, "  %s -> %d", ab_item_name(r.nameid), r.amount);
	ab_msg(sd, "[Safety] flee HP<%d%% (item: %s) stop if player near:%s overweight(%d%%):%s no items:%s idle:%ds",
		ab->flee_hp_pct, ab->flee_item ? ab_item_name(ab->flee_item) : "-", onoff(ab->stop_on_player), ab->weight_pct,
		onoff(ab->stop_on_weight), onoff(ab->stop_no_item), ab->idle_seconds);
}

// ---------------------------------------------------------------------------------------------
// @autobattle command
// ---------------------------------------------------------------------------------------------

static const char* ab_help[] = {
	"=== @autobattle (@ab) ===",
	"start | stop | status | mobs (list monsters on this map)",
	"all <on|off> | attack <on|off> | fightback <on|off>",
	"mob +<id|name> / -<id|name> / reset      (target monsters)",
	"skill <slot 1-16> <skill id|name> [lv] | skill clear <slot>   (attack skills)",
	"hp <item id|name|off> [%]  | sp <item id|name|off> [%]          (recovery)",
	"buff <slot 1-8> <skill> [lv] | buff clear <slot>",
	"support <slot 1-4> <skill> [lv] | support clear <slot> | support hp <%>",
	"loot <on|off> | loot range <1-14> | loot +<item> / -<item> / reset",
	"restock <on|off> | restock +<item> <amount> / -<item> | restock min <n>",
	"safety hp <% 0=off> | safety item <item|off> | safety player <on|off>",
	"safety weight <on|off> [%] | safety noitem <on|off> | safety idle <sec 0=off>",
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
	if (itemdb_searchname_array(found, 1, s.c_str()) > 0 && !found.empty())
		return found.begin()->first;
	return 0;
}

static uint16 ab_find_mob(const std::string& s) {
	uint16 id = ab_is_number(s) ? (uint16)atoi(s.c_str()) : mobdb_searchname(s.c_str());
	return mob_db.exists(id) ? id : 0;
}

bool autobattle_command(map_session_data& sd, const char* message) {
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
	auto arg = [&](size_t i) -> std::string { return i < tok.size() ? tok[i] : std::string(); };
	auto help = [&]() { for (const char* l : ab_help) ab_msg(sd, "%s", l); };

	if (tok.empty() || tok[0] == "help") {
		help();
		return true;
	}
	if (!battle_config.autobattle_enable && tok[0] != "status") {
		ab_msg(sd, "Auto Battle is disabled on this server.");
		return false;
	}

	const std::string& c = tok[0];
	bool changed = true, b = false;

	if (c == "start" || c == "on") { autobattle_start(sd); changed = false; }
	else if (c == "stop" || c == "off") { if (ab->running) autobattle_stop(sd); else ab_msg(sd, "Auto Battle is not running."); changed = false; }
	else if (c == "toggle") { if (ab->running) autobattle_stop(sd); else autobattle_start(sd); changed = false; }
	else if (c == "status") { autobattle_status(sd); changed = false; }
	else if (c == "mobs") { autobattle_list_map_mobs(sd); changed = false; }
	else if (c == "all" && ab_parse_onoff(arg(1), b)) ab->all_monsters = b;
	else if (c == "attack" && ab_parse_onoff(arg(1), b)) ab->normal_attack = b;
	else if (c == "fightback" && ab_parse_onoff(arg(1), b)) ab->fight_back = b;
	else if (c == "mob") {
		std::string a = arg(1);
		if (a == "reset") ab->mobs.clear();
		else if (a.size() > 1 && (a[0] == '+' || a[0] == '-')) {
			uint16 id = ab_find_mob(a.substr(1));
			if (!id) { ab_msg(sd, "Unknown monster."); return false; }
			auto it = std::find(ab->mobs.begin(), ab->mobs.end(), id);
			if (a[0] == '+') {
				if (it == ab->mobs.end()) {
					if (ab->mobs.size() >= AB_MAX_MOBS) { ab_msg(sd, "Monster list is full (%d).", AB_MAX_MOBS); return false; }
					ab->mobs.push_back(id);
				}
				ab->all_monsters = false;
				ab_msg(sd, "Added %s.", ab_mob_name(id));
			} else if (it != ab->mobs.end()) {
				ab->mobs.erase(it);
				ab_msg(sd, "Removed %s.", ab_mob_name(id));
			}
		} else { help(); return false; }
	}
	else if (c == "skill" || c == "buff" || c == "support") {
		s_ab_skill* slots = c == "skill" ? ab->attack : c == "buff" ? ab->buffs : ab->support;
		int32 n = c == "skill" ? AB_MAX_ATTACK_SLOTS : c == "buff" ? AB_MAX_BUFF_SLOTS : AB_MAX_SUPPORT_SLOTS;
		if (c == "support" && arg(1) == "hp") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 100) { ab_msg(sd, "Percent must be 1-100."); return false; }
			ab->support_hp_pct = (uint8)v;
		} else if (arg(1) == "clear") {
			int32 slot = atoi(arg(2).c_str());
			if (arg(2) == "all") { for (int32 i = 0; i < n; i++) slots[i] = s_ab_skill(); }
			else if (slot >= 1 && slot <= n) slots[slot - 1] = s_ab_skill();
			else { ab_msg(sd, "Slot must be 1-%d.", n); return false; }
		} else {
			int32 slot = atoi(arg(1).c_str());
			if (slot < 1 || slot > n) { ab_msg(sd, "Slot must be 1-%d.", n); return false; }
			uint16 id = ab_find_skill(arg(2));
			if (!id) { ab_msg(sd, "Unknown skill."); return false; }
			uint8 learned = pc_checkskill(&sd, id);
			if (!learned) { ab_msg(sd, "You have not learned %s.", skill_get_desc(id)); return false; }
			int32 lv = arg(3).empty() ? learned : atoi(arg(3).c_str());
			lv = std::clamp<int32>(lv, 1, learned);
			for (int32 i = 0; i < n; i++) // no duplicates in the same list
				if (slots[i].skill_id == id && i != slot - 1) slots[i] = s_ab_skill();
			slots[slot - 1].skill_id = id;
			slots[slot - 1].level = (uint16)lv;
			slots[slot - 1].next_try = 0;
			ab_msg(sd, "Slot %d: %s Lv%d", slot, skill_get_desc(id), lv);
		}
	}
	else if (c == "hp" || c == "sp") {
		t_itemid& item = c == "hp" ? ab->hp_item : ab->sp_item;
		uint8& pct = c == "hp" ? ab->hp_pct : ab->sp_pct;
		if (arg(1) == "off") item = 0;
		else {
			t_itemid id = ab_find_item(arg(1));
			if (!id) { ab_msg(sd, "Unknown item."); return false; }
			item = id;
			if (!arg(2).empty()) {
				int32 v = atoi(arg(2).c_str());
				if (v < 1 || v > 99) { ab_msg(sd, "Percent must be 1-99."); return false; }
				pct = (uint8)v;
			}
			ab_msg(sd, "%s item: %s below %d%%", c == "hp" ? "HP" : "SP", ab_item_name(id), pct);
		}
	}
	else if (c == "loot") {
		std::string a = arg(1);
		if (ab_parse_onoff(a, b)) ab->loot = b;
		else if (a == "range") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 14) { ab_msg(sd, "Range must be 1-14."); return false; }
			ab->loot_range = (uint8)v;
		} else if (a == "reset") ab->loot_ids.clear();
		else if (a.size() > 1 && (a[0] == '+' || a[0] == '-')) {
			t_itemid id = ab_find_item(a.substr(1));
			if (!id) { ab_msg(sd, "Unknown item."); return false; }
			auto it = std::find(ab->loot_ids.begin(), ab->loot_ids.end(), id);
			if (a[0] == '+') {
				if (it == ab->loot_ids.end()) {
					if (ab->loot_ids.size() >= AB_MAX_LOOT_IDS) { ab_msg(sd, "Loot list is full (%d).", AB_MAX_LOOT_IDS); return false; }
					ab->loot_ids.push_back(id);
				}
				ab_msg(sd, "Loot filter: +%s", ab_item_name(id));
			} else if (it != ab->loot_ids.end()) ab->loot_ids.erase(it);
		} else { help(); return false; }
	}
	else if (c == "restock") {
		std::string a = arg(1);
		if (ab_parse_onoff(a, b)) ab->restock = b;
		else if (a == "min") {
			int32 v = atoi(arg(2).c_str());
			if (v < 1 || v > 30000) { ab_msg(sd, "Invalid number."); return false; }
			ab->restock_min = (uint16)v;
		} else if (a == "reset") ab->restock_items.clear();
		else if (a.size() > 1 && (a[0] == '+' || a[0] == '-')) {
			t_itemid id = ab_find_item(a.substr(1));
			if (!id) { ab_msg(sd, "Unknown item."); return false; }
			auto it = std::find_if(ab->restock_items.begin(), ab->restock_items.end(), [&](const s_ab_restock& r) { return r.nameid == id; });
			if (a[0] == '+') {
				int32 amount = atoi(arg(2).c_str());
				if (amount < 1 || amount > 30000) { ab_msg(sd, "Usage: @ab restock +<item> <amount>"); return false; }
				auto data = item_db.find(id);
				if (!data->value_buy || !data->isStackable()) { ab_msg(sd, "%s cannot be bought.", data->ename.c_str()); return false; }
				if (it != ab->restock_items.end()) it->amount = (uint16)amount;
				else {
					if (ab->restock_items.size() >= AB_MAX_RESTOCK) { ab_msg(sd, "Restock list is full (%d).", AB_MAX_RESTOCK); return false; }
					s_ab_restock r; r.nameid = id; r.amount = (uint16)amount;
					ab->restock_items.push_back(r);
				}
				ab_msg(sd, "Restock: %s up to %d", ab_item_name(id), amount);
			} else if (it != ab->restock_items.end()) ab->restock_items.erase(it);
		} else { help(); return false; }
	}
	else if (c == "safety") {
		std::string a = arg(1);
		if (a == "hp") {
			int32 v = atoi(arg(2).c_str());
			if (v < 0 || v > 90) { ab_msg(sd, "Percent must be 0-90."); return false; }
			ab->flee_hp_pct = (uint8)v;
		} else if (a == "item") {
			if (arg(2) == "off") ab->flee_item = 0;
			else {
				t_itemid id = ab_find_item(arg(2));
				if (!id) { ab_msg(sd, "Unknown item."); return false; }
				ab->flee_item = id;
			}
		} else if (a == "player" && ab_parse_onoff(arg(2), b)) ab->stop_on_player = b;
		else if (a == "weight" && ab_parse_onoff(arg(2), b)) {
			ab->stop_on_weight = b;
			if (!arg(3).empty()) {
				int32 v = atoi(arg(3).c_str());
				if (v < 10 || v > 100) { ab_msg(sd, "Percent must be 10-100."); return false; }
				ab->weight_pct = (uint8)v;
			}
		} else if (a == "noitem" && ab_parse_onoff(arg(2), b)) ab->stop_no_item = b;
		else if (a == "idle") {
			int32 v = atoi(arg(2).c_str());
			if (v < 0 || v > 3600) { ab_msg(sd, "Seconds must be 0-3600."); return false; }
			ab->idle_seconds = (uint16)v;
		} else { help(); return false; }
	}
	else { help(); return false; }

	if (changed) {
		autobattle_save(sd);
		ab_msg(sd, "OK.");
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

void do_init_autobattle() {
	add_timer_func_list(ab_timer, "ab_timer");
}

void do_final_autobattle() {
}
