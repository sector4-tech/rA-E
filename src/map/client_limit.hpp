// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef CLIENT_LIMIT_HPP
#define CLIENT_LIMIT_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

/// rA-E: limit the number of game clients that one IP address may have in the game at the same time.
/// Config: max_clients_per_ip / max_clients_kick_delay (conf/battle/feature.conf).
/// IP addresses listed in the permanent server array $cl_white$[] (managed with @clwhite, see
/// npc/custom/client_limit.txt) are not limited, e.g. internet cafes where every PC shares one IP.

/// Called once when a character enters the game. Kicks the character (after a short warning) when the IP
/// already has max_clients_per_ip characters in the game.
void client_limit_check(map_session_data& sd);

/// Number of characters in the game with the IP of 'sd' (characters waiting to be kicked are not counted).
int32 client_limit_count(map_session_data& sd);

void do_init_client_limit();

#endif /* CLIENT_LIMIT_HPP */
