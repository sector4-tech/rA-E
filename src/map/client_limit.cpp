// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "client_limit.hpp"

#include <cstdarg>
#include <cstring>
#include <unordered_set>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "atcommand.hpp" // msg_txt
#include "battle.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "mapreg.hpp"
#include "pc.hpp"
#include "script.hpp"

/// Characters that are waiting for their kick (not counted any more)
static std::unordered_set<uint32> cl_pending;

/// Max number of entries read from $cl_white$[]
static const int32 CL_MAX_WHITELIST = 128;

/// GM groups of this level or higher are never limited
static const int32 CL_BYPASS_GROUP_LEVEL = 1;

static bool cl_whitelisted(uint32 ip) {
	char ipstr[16];
	ip2str(ip, ipstr);
	int32 var = add_str("$cl_white$");

	for (int32 i = 0; i < CL_MAX_WHITELIST; i++) {
		const char* w = mapreg_readregstr(reference_uid(var, i));
		if (w != nullptr && strcmp(w, ipstr) == 0)
			return true;
	}
	return false;
}

static int32 cl_count_sub(map_session_data* sd, va_list ap) {
	uint32 ip = va_arg(ap, uint32);
	int32* count = va_arg(ap, int32*);

	if (sd->state.autotrade || !session_isActive(sd->fd))
		return 0; // offline shops do not use a client
	if (session[sd->fd]->client_addr != ip)
		return 0;
	if (cl_pending.find(sd->status.account_id) != cl_pending.end())
		return 0;
	(*count)++;
	return 0;
}

int32 client_limit_count(map_session_data& sd) {
	if (!session_isActive(sd.fd))
		return 0;
	int32 count = 0;
	map_foreachpc(cl_count_sub, session[sd.fd]->client_addr, &count);
	return count;
}

static TIMER_FUNC(cl_kick_timer) {
	uint32 account_id = (uint32)data;
	cl_pending.erase(account_id);

	map_session_data* sd = map_id2sd(id);
	if (sd == nullptr || sd->status.account_id != account_id)
		return 0;
	clif_GM_kick(nullptr, sd);
	return 0;
}

void client_limit_check(map_session_data& sd) {
	int32 limit = battle_config.max_clients_per_ip;

	if (limit <= 0 || sd.state.autotrade || !session_isActive(sd.fd))
		return;
	if (pc_get_group_level(&sd) >= CL_BYPASS_GROUP_LEVEL)
		return;
	if (cl_pending.find(sd.status.account_id) != cl_pending.end())
		return;

	uint32 ip = session[sd.fd]->client_addr;
	if (cl_whitelisted(ip))
		return;

	int32 count = client_limit_count(sd); // includes 'sd'
	if (count <= limit)
		return;

	// Over the limit: the character that just came in has to go
	char output[CHAT_SIZE_MAX];
	int32 delay = battle_config.max_clients_kick_delay;
	safesnprintf(output, sizeof(output), msg_txt(&sd, 1720), limit, delay / 1000); // This computer (IP) may only have %d game windows open. This character will be disconnected in %d seconds.
	clif_displaymessage(sd.fd, output);
	clif_broadcast(&sd, output, strlen(output) + 1, BC_YELLOW, SELF);

	char ipstr[16];
	ShowInfo("client_limit: account %u (char '%s') kicked, IP %s already has %d characters in game (limit %d).\n",
		sd.status.account_id, sd.status.name, ip2str(ip, ipstr), count - 1, limit);

	cl_pending.insert(sd.status.account_id);
	sd.state.block_action |= PCBLOCK_ALL;
	add_timer(gettick() + delay, cl_kick_timer, sd.id, (intptr_t)sd.status.account_id);
}

void do_init_client_limit() {
	add_timer_func_list(cl_kick_timer, "cl_kick_timer");
}
