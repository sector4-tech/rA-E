# rA-E Roadmap (Pre-Renewal Hi-Class 99/70 · Ragexe 2026-01-07)

สถานะ: ✅ เสร็จ · 🔨 กำลังทำ · 📋 วางแผน · 💡 ไอเดีย

## สิ่งที่มีแล้ว
- ✅ Pre-Re + Hi-Class only + No Baby, PACKETVER 20260107
- ✅ VIP (icon นับถอยหลัง, VIP Ticket 1/5/15/30 วัน)
- ✅ Auto Battle (C++ `autobattle.cpp` + เมนูไทย `@ab`)
- ✅ Race System 10 เผ่า 3 ดาว 6 ช่อง (`@race`)
- ✅ Hat Effect Viewer / Mixer, Warper, Job Master, WoE Controller, Healer, Stylist

## Phase 1 — เสถียรภาพและความปลอดภัย (ทำก่อนเปิดจริง)
- 📋 `/ro-audit` ทั้งเซิร์ฟ: dupe, permission, crash ในระบบ custom
- 📋 ป้องกันบอท/โปรแกรมช่วยเล่นฝั่ง client (GEPARD Shield หรือ anti-cheat ที่รองรับ client 2026) — **เสียเงิน ต้องตัดสินใจ**
- 📋 systemd + backup อัตโนมัติ + firewall + Discord แจ้งเตือน (`/ro-ops`)
- 📋 เปิด log ครบ (`conf/log_athena.conf`: pick/zeny/trade/vend/MVP/chat GM) เพื่อสอบสวนการโกง
- 📋 เซิร์ฟทดสอบแยก (DB คนละตัว) ก่อนขึ้นของจริงทุกครั้ง

## Phase 2 — UI และความน่าเล่น
- 📋 Library UI กลาง (`npc/custom/lib/ui_lib.txt`) ให้ทุกเมนูหน้าตาเดียวกัน (`/ro-ui ทุกเมนู`)
- 📋 Auto Battle: preset ต่ออาชีพ, บันทึก/โหลดชุดตั้งค่า, สรุปผลหลังหยุด (EXP/ชม., ของที่เก็บ)
- 📋 Race: หน้า "สมุดเผ่า" แสดงดาว/โบนัสทั้งหมด, ภารกิจปลดดาว, title ตามเผ่า
- 💡 ผู้เล่นใหม่: NPC ไกด์ + เควสต์ 1–99 พาเที่ยว, ของเริ่มต้น, Navigation
- 💡 Daily/Weekly quest board, Attendance (มีใน client แล้ว), Achievement + Title
- 💡 Cosmetic: costume, hat effect, aura ตาม rebirth/rank, กรอบชื่อ

## Phase 3 — เว็บไซต์ + หลังบ้าน (`/ro-web`)
- 📋 เว็บผู้เล่น: ข่าว, ปฏิทิน event, ranking, สถานะเซิร์ฟ, ดาวน์โหลด client, database item/mob
- 📋 Admin: 2FA, สิทธิ์แยกระดับ, audit log, จัดการข่าว/event, ค้นหา/แบนผู้เล่น, ส่งของทาง Rodex
- 💡 Discord bot: ประกาศ event, ผู้ชนะ WoE, MVP kill, ผู้เล่นออนไลน์

## Phase 4 — Event & Community (`/ro-event`)
- 💡 Event ตามเทศกาลไทย (สงกรานต์, ลอยกระทง, ปีใหม่), Double EXP สุดสัปดาห์, WoE ทุกสัปดาห์ + ranking กิลด์
- 💡 Battleground / PvP ladder แบบ season

## เครื่องมือที่แนะนำ
**Ubuntu (server)**: `build-essential cmake git libmariadb-dev libmariadb-dev-compat zlib1g-dev libpcre3-dev` (สำหรับ build), MariaDB, `tmux`, `htop`, `ufw`, `fail2ban`, `rclone` (backup นอกเครื่อง), Nginx + Certbot (เว็บ), Node.js LTS (เว็บ)
**Windows (client/content)**: GRF Editor (จัดการ GRF/ไฟล์ data), WARP patcher (patch Ragexe), BrowEdit 3 (แก้แผนที่), Act Editor (sprite/act), Notepad++ (ตั้ง encoding เป็น TIS-620 / Windows-874 สำหรับไฟล์ไทย), HeidiSQL (ดู DB)

## การสร้างรายได้ — ข้อควรรู้ก่อน
> Ragnarok Online (ตัวเกม, client, กราฟิก, ชื่อ) เป็นทรัพย์สินทางปัญญาของ Gravity การเปิดเซิร์ฟเถื่อนและเก็บเงินจากผู้เล่นมีความเสี่ยงทางกฎหมาย (ทั้งไทยและต่างประเทศ) ข้อมูลนี้ไม่ใช่คำแนะนำทางกฎหมาย — ควรปรึกษาผู้เชี่ยวชาญก่อนรับเงิน

ถ้าตัดสินใจทำ แนวทางที่ผู้เล่นยอมรับและไม่ทำลายเกม:
- **Donation → Cash Point** ใช้ซื้อของ **cosmetic** (costume, hat effect, aura, กรอบชื่อ, เปลี่ยนชื่อ/ทรงผม) — ไม่ขายพลังโจมตี
- **VIP รายเดือน**: สะดวกขึ้น (storage, ช่องตัวละคร, Auto Battle นานขึ้น, warp ฟรี) แต่ไม่แรงขึ้นแบบชัดเจน
- **Battle Pass / Season Pass**: ฟรี track + premium track ที่เป็น cosmetic
- ช่องทางรับเงินไทย: payment gateway ที่รองรับ PromptPay/TrueMoney (ต้องมีตัวตนธุรกิจ) — webhook เข้าเว็บ → เติม point ลงตาราง `cashpoints`/`acc_reg_num` อัตโนมัติ + บันทึก log
- รายได้เสริม: Discord สปอนเซอร์, สตรีมเมอร์ร่วม event, merchandise ของเซิร์ฟเอง (ห้ามใช้โลโก้/ตัวละครของ Gravity)
- ทำหน้า "กติกาการเติมเงิน + นโยบายคืนเงิน" ให้ชัด
