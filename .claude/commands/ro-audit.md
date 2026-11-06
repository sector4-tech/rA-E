---
description: ตรวจหาบัค ช่องโหว่ และการ dupe ในระบบที่ระบุ (หรือทั้งเซิร์ฟ)
argument-hint: [ระบบ/ไฟล์ เช่น autobattle, race_system, npc/custom/warper.txt หรือเว้นว่าง = ทั้งหมด]
---
ตรวจสอบความปลอดภัยและบัคของ: **$ARGUMENTS** (ถ้าว่าง ให้ไล่ `npc/custom/`, `src/custom/`, `src/map/autobattle.cpp` และไฟล์ C++ ที่ fork นี้แก้ต่างจาก rAthena upstream)

ขั้นตอน:
1. อ่าน CLAUDE.md แล้วระบุไฟล์ที่เกี่ยวข้องทั้งหมด (script, C++, db yml, conf)
2. ตรวจตาม checklist นี้ และบันทึกเฉพาะสิ่งที่ยืนยันได้จากโค้ดจริง (ระบุไฟล์:บรรทัด):
   - **Dupe / exploit**: ให้ของ/zeny ก่อนหักของ, ไม่เช็ก `checkweight`/`countitem` ซ้ำหลัง `next;`/`select`/`input`, ค่าติดลบหรือ overflow จาก `input`, กดซ้ำ/เปิดสองหน้าต่าง, ใช้ item ระหว่าง trade/vending/storage, logout กลางเมนู
   - **Permission**: atcommand/bindatcmd ที่ผู้เล่นทั่วไปใช้ได้แต่ไม่ควร, ขาดใน `conf/groups.yml`
   - **C++**: null pointer (`map_id2sd`, `sd->status`), buffer/array index, integer overflow, use-after-free ของ timer, memory leak, การเช็ก packet จาก client ที่ไม่ validate
   - **Auto Battle**: ใช้ข้ามแมพต้องห้าม (PvP/WoE/instance/town), ทำงานตอนตาย/ติดแชท/vending, ใช้ skill/item เกินสิทธิ์, โหลดเซิร์ฟ (timer ต่อ player)
   - **SQL/Web**: query ที่ต่อ string จาก input (SQL injection), รหัสผ่าน plain text
   - **Balance**: bonus ซ้อนกันเกินตั้งใจ (Race + VIP + Hat effect), drop/exp rate ผิดไฟล์
   - **Encoding**: ไฟล์ไทยที่เผลอเป็น UTF-8
3. จัดลำดับความรุนแรง: 🔴 Critical (dupe/crash/ยึดสิทธิ์) → 🟠 High → 🟡 Medium → ⚪ Low
4. รายงานเป็นตารางภาษาไทย: ความรุนแรง | ไฟล์:บรรทัด | ปัญหา | วิธีทำให้เกิด | วิธีแก้
5. **ยังไม่ต้องแก้** — ถามก่อนว่าจะให้แก้ข้อไหน (เว้นแต่ผู้ใช้สั่งให้แก้เลย) เมื่อแก้แล้วต้อง compile ผ่านและบอกวิธีทดสอบในเกม
