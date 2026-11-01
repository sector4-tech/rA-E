// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef CONFIG_CUSTOM_DEFINES_PRE_HPP
#define CONFIG_CUSTOM_DEFINES_PRE_HPP

/**
 * rAthena configuration file (http://rathena.org)
 * For detailed guidance on these check http://rathena.org/wiki/SRC/config/
 **/

// เปิดใช้งานระบบป้องกัน Packet (Packet Obfuscation)
#define PACKET_OBFUSCATION

// ใส่กุญแจ 3 ดอก (แก้ตัวเลขให้ตรงกับที่ใส่ในโปรแกรม Diff Client)
#define PACKET_OBFUSCATION_KEY1 0xCDD640FB
#define PACKET_OBFUSCATION_KEY2 0x47F8A88B
#define PACKET_OBFUSCATION_KEY3 0xD241330B

/// Server mode: Pre-Renewal (disables RENEWAL, RENEWAL_CAST, RENEWAL_DROP, RENEWAL_EXP,
/// RENEWAL_LVDMG, RENEWAL_ASPD and RENEWAL_STAT, and loads db/pre-re + npc/pre-re).
#ifndef PRERE
	#define PRERE
#endif

/// Client: Ragexe20260107
#ifndef PACKETVER
	#define PACKETVER 20260107
#endif

/// Job restriction: only allow classes up to High Class (Trans, 2nd job).
/// Blocks 3rd jobs, 4th jobs and the later expanded classes
/// (Kagerou/Oboro, Rebellion, Summoner, Expanded Super Novice, Star Emperor, Soul Reaper, ...).
/// Comment out to remove the restriction.
#define HICLASS_ONLY

/// Disable Baby classes (and the adoption system that creates them).
/// Comment out to allow Baby classes again.
#define NO_BABY_CLASS

#endif /* CONFIG_CUSTOM_DEFINES_PRE_HPP */
