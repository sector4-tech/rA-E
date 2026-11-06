---
description: ออกแบบและสร้างระบบใหม่ในเกม (NPC + C++ ถ้าจำเป็น + DB + ฝั่ง client) แบบครบวงจร
argument-hint: [ชื่อ/ไอเดียระบบ เช่น "ระบบ Daily Quest", "Pet Evolution", "Race tier 4"]
---
ระบบที่ต้องการ: **$ARGUMENTS**

ทำตามลำดับนี้:

**1. ออกแบบ (ส่งให้ผู้ใช้ดูก่อนเขียนโค้ด)**
- เป้าหมายของระบบ, flow ของผู้เล่น, ของรางวัล/ราคา, ผลต่อ balance ของ Pre-Renewal 99/70
- ทำด้วย script ล้วนได้หรือไม่ ถ้าต้องแก้ C++ ให้บอกเหตุผล
- ข้อมูลที่เก็บ (char var / account var / ตาราง SQL ใหม่) และผลต่อ performance
- จะต่อกับระบบเดิมยังไง (Race, VIP, Auto Battle, Hat Effect) — ห้ามให้ bonus ซ้อนเกินตั้งใจ
- รายการ item ID ใหม่ (ช่วง 325xx ที่ว่าง), status icon, effect ที่ใช้
- ถามคำถามที่ตัดสินใจไม่ได้เองรวมกันครั้งเดียว

**2. สร้าง (หลังผู้ใช้ตกลง)** — branch `feature/<ชื่อ>`
- NPC ใน `npc/custom/<ชื่อ>.txt` + เพิ่มใน `npc/scripts_custom.conf`, ข้อความไทยเซฟเป็น TIS-620
- UI สไตล์คลาสสิก: หัวเมนูสี, ใช้ `select()` แบบมีสัญลักษณ์/สี, หน้าแบ่งหมวด (tab) แบบ `autobattle.txt`, ปุ่มย้อนกลับทุกหน้า, `cutin` ถ้ามี, คำสั่งลัด `bindatcmd`
- C++ ใน `src/map` หรือ `src/custom/*.inc` เฉพาะส่วนที่จำเป็น + battle config ใน `conf/battle/feature.conf`
- DB: `db/pre-re/*.yml` / SQL ใน `sql-files/upgrades/` (ถ้ามีตารางใหม่)
- กัน dupe/exploit ตาม checklist ใน `/ro-audit`

**3. ฝั่ง client** — เขียนไฟล์ตัวอย่างไว้ที่ `doc/rae/client/<ชื่อ>/` (iteminfo lua, stateiconinfo, msgstringtable, รายการรูปที่ต้องทำ) พร้อมบอกว่าวางไว้ที่ไหนใน `Data2026_`

**4. ตรวจ** — compile ผ่าน, map-server โหลดไม่มี error, เขียน `doc/rae/systems/<ชื่อ>.md` (วิธีใช้สำหรับผู้เล่น + config สำหรับแอดมิน), สรุปขั้นตอนทดสอบในเกม
