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

#ifndef PRERE
	#define PRERE
#endif

#ifndef PACKETVER
	#define PACKETVER 20260107
#endif

#define HICLASS_ONLY

#endif /* CONFIG_CUSTOM_DEFINES_PRE_HPP */
