# CLAUDE.md — rA-E (Ragnarok Online private server, rAthena fork)

คุยกับเจ้าของโปรเจกต์เป็น **ภาษาไทย** เสมอ (ศัพท์เทคนิค ชื่อไฟล์ คำสั่ง ใช้ภาษาอังกฤษได้)
อธิบายให้คนที่ไม่ใช่โปรแกรมเมอร์มืออาชีพเข้าใจ: บอกว่าแก้อะไร ทำไม และต้องทดสอบอย่างไรในเกม

## ภาพรวมโปรเจกต์
- Fork ของ rAthena, branch หลักที่ใช้งาน: `prere-hiclass-vip`
- โหมด: **Pre-Renewal**, Hi-Class เท่านั้น, Max Lv **99/70**, ไม่มี Baby class
- Client: **Ragexe 2026-01-07 (kRO)**, `PACKETVER 20260107`
- ภาษาในเกม: ไทย — client ใช้ encoding **TIS-620 / cp874**
- Server รันบน **Ubuntu** (MariaDB/MySQL), Client เก็บที่เครื่อง Windows `C:\johnfon\_2026-01-07\Data2026_`
  (Claude เข้าถึงโฟลเดอร์ client ไม่ได้ เว้นแต่ผู้ใช้แนบไฟล์หรือเชื่อมเครื่องผ่าน desktop app)

## Compile-time config (ห้ามแก้โดยไม่ถาม)
- `src/custom/defines_pre.hpp` — `PRERE`, `PACKETVER 20260107`, `PACKET_OBFUSCATION` (+keys), `HICLASS_ONLY`, `NO_BABY_CLASS`
- `src/config/core.hpp` — `VIP_ENABLE`, `MAX_CHAR_VIP`
- `src/config/renewal.hpp` — ถูกปิดทั้งไฟล์เพราะ `PRERE` ถูก define ไว้แล้ว
- การเปลี่ยน PACKETVER / obfuscation key ต้องตรงกับ client ที่ patch ไว้ ไม่งั้นเข้าเกมไม่ได้

## โครงสร้างที่สำคัญ
| ส่วน | ที่อยู่ |
|---|---|
| Source C++ | `src/map` (เกม), `src/char`, `src/login`, `src/web`, `src/common` |
| ระบบ Auto Battle (C++) | `src/map/autobattle.cpp/.hpp`, เอกสาร `doc/autobattle.md`, config `conf/battle/feature.conf` (`autobattle_*`) |
| Custom script commands / atcommands | `src/custom/script.inc`, `script_def.inc`, `atcommand.inc`, `atcommand_def.inc`, `battle_config_*.inc` |
| NPC custom | `npc/custom/*.txt` (เปิดใช้ใน `npc/scripts_custom.conf`) |
| ระบบที่ทำแล้ว | `autobattle.txt`, `race_system.txt` (@race), `vip_manager.txt`, `hat_effect_viewer.txt`, `hat_effect_mixer.txt`, `warper.txt`, `jobmaster.txt`, `woe_controller.txt` |
| Database (YAML) | `db/pre-re/*.yml` (ข้อมูลหลัก), `db/import/*.yml` (override) |
| Custom item IDs | ช่วง **325xx** ใน `db/pre-re/item_db_usable.yml` (32500 Auto Battle Pass (ไอเทมเช่า), 32501–32503 Race, 32510–32513 VIP Ticket, 32520–32522 Auto Battle Pass Box) |
| Cash Shop | `db/import/item_cash.yml` + NPC `RAE_CashShop` ใน `vip_manager.txt` (ราคาต้องตรงกัน) — `doc/rae/systems/cash_shop.md` |
| จำกัด client ต่อ IP | `src/map/client_limit.cpp`, GM `@clwhite` (`npc/custom/client_limit.txt`) — `doc/rae/systems/client_limit.md` |
| Config ส่วนตัวของเซิร์ฟ | `conf/import/` (gitignored — รหัสผ่าน DB, IP อยู่ที่นี่) |
| ข้อความภาษาไทย | `conf/msg_conf/map_msg_tha.conf` |
| SQL | `sql-files/` (`main.sql`, `logs.sql`, `web.sql`, `upgrades/`) |

## กฎการทำงาน (สำคัญมาก)
1. **Encoding ภาษาไทย**: ไฟล์ NPC / msg ที่มีภาษาไทยเก็บเป็น **TIS-620 (cp874)** ไม่ใช่ UTF-8
   - ห้ามใช้ Write/Edit ตรง ๆ กับข้อความไทยในไฟล์พวกนี้ — ให้แก้ผ่าน Python:
     `open(p, encoding='cp874')` → แก้ → เขียนกลับด้วย `encoding='cp874'`
   - เช็กหลังแก้: `file -i <file>` ต้องไม่เป็น `utf-8`; `iconv -f cp874 -t utf-8 <file> | head` อ่านไทยออก
2. **ID ใหม่**: item ใหม่ใช้ช่วง 325xx ต่อจากของเดิม (grep ก่อนว่า ID ว่าง), status ใหม่ต้องเพิ่มทั้ง `status.yml` และฝั่ง client (stateiconinfo)
3. **ทุกอย่างที่ผู้เล่นเห็น** ต้องคิดถึงฝั่ง client ด้วย: iteminfo, รูป icon, sprite, effect — ระบุไฟล์ client ที่ผู้ใช้ต้องแก้เองทุกครั้ง
4. **Pre-Renewal เท่านั้น**: แก้ข้อมูลใน `db/pre-re/` ไม่ใช่ `db/re/` (เว้นแต่ต้องการให้ทั้งสองโหมดตรงกัน) และสูตรต้องอยู่ใต้ `#ifndef RENEWAL` เมื่อเกี่ยวข้อง
5. **ความปลอดภัย**: ทุก script ที่ให้ของ / เงิน / zeny ต้องกัน dupe (เช็ก `checkweight`, จำนวน, ค่าติดลบ, overflow, เรียกซ้ำระหว่าง `next;`/`select`) — ทุก atcommand ใหม่ต้องมี permission ใน `conf/groups.yml`
6. **ห้าม commit ความลับ** (รหัสผ่าน DB, token, key ของ payment) — repo นี้เป็น public
7. UI ในเกมยึด **สไตล์คลาสสิก RO**: เมนู NPC dialog (`select`, `mes` สี `^RRGGBB`), cutin, icon status, effect — ไม่ทำให้หลุดธีม
8. แก้ทีละเรื่อง, diff เล็ก, อธิบายผลกระทบต่อ balance, เขียนเอกสารใน `doc/` เมื่อเพิ่มระบบ

## Build & ทดสอบ
```bash
# CMake (แนะนำ)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j"$(nproc)"
# หรือแบบดั้งเดิม
./configure --enable-prere=yes --enable-vip=yes && make clean && make server
./athena-start start | stop | restart       # เปิด/ปิดเซิร์ฟ
```
- ก่อนบอกว่าเสร็จ: ต้อง **compile ผ่าน** และ map-server โหลด NPC/DB ไม่มี error/warning ใหม่
- ตรวจ YAML: map-server จะแจ้ง error ตอนโหลด — อ่าน log ทุกครั้ง
- สรุปท้ายงานเสมอ: ไฟล์ที่แก้, วิธีทดสอบในเกม (คำสั่ง @ ที่ต้องพิมพ์), สิ่งที่ต้องแก้ฝั่ง client

## Git
- ทำงานบน branch ย่อยจาก `prere-hiclass-vip` (`feature/<ชื่อ>`, `fix/<ชื่อ>`) แล้วค่อย merge
- commit message ภาษาอังกฤษสั้น ๆ บอกระบบ เช่น `AutoBattle: ...`, `Race: ...`, `Security: ...`

## เอกสารแผนงาน
- Roadmap / ไอเดีย / แหล่งรายได้: `doc/rae/ROADMAP.md`
- ชุดคำสั่ง slash command: `.claude/commands/` (พิมพ์ `/` ใน Claude Code เพื่อเลือก)
