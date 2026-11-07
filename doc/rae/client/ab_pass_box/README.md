# ฝั่ง Client: Auto Battle Pass Box (32520-32522)

ไอเทมใหม่ต้องมีข้อมูลใน iteminfo ของ client ไม่งั้นในเกมจะเห็นชื่อเป็น `Unknown Item` หรือรูปหาย

## วิธีเพิ่ม
1. เปิดไฟล์ iteminfo ที่ client ใช้อยู่ (เช่น `System/itemInfo_true.lub` หรือ `System/itemInfo*.lub` ใน `Data2026_`)
2. คัดลอก entry ของ **VIP Ticket (32510)** ที่มีอยู่แล้ว แล้ววางเพิ่มอีก 3 ชุดสำหรับเลข 32520, 32521, 32522
   (จะได้รูปกล่องแบบเดียวกัน ถ้าต้องการรูปใหม่ ให้เปลี่ยน `identifiedResourceName`)
3. แก้ชื่อและคำอธิบายตามตัวอย่างด้านล่าง แล้ว **บันทึกไฟล์เป็น encoding เดิมของไฟล์** (ไฟล์ของ client ไทยส่วนใหญ่เป็น TIS-620)

```lua
[32520] = {
	unidentifiedDisplayName = "Auto Battle Pass Box (1 วัน)",
	unidentifiedResourceName = "<เหมือน 32510>",
	unidentifiedDescriptionName = { "" },
	identifiedDisplayName = "Auto Battle Pass Box (1 วัน)",
	identifiedResourceName = "<เหมือน 32510>",
	identifiedDescriptionName = {
		"กล่องที่บรรจุ ^0000FFAuto Battle Pass^000000",
		"ดับเบิลคลิกเพื่อเพิ่มอายุ Pass ^009900+1 วัน^000000",
		"อายุ Pass สะสมได้สูงสุด 60 วัน",
		"ตัว Pass ใช้เปิดเมนูและเริ่ม/หยุด Auto Battle",
		"น้ำหนัก : ^7777771^000000",
	},
	slotCount = 0,
	ClassNum = 0,
	costume = false
},
```
สำหรับ 32521 และ 32522 ให้ใช้ข้อความเดิม แต่เปลี่ยนจำนวนวันเป็น 3 วัน และ 7 วัน ตามลำดับ

## ควรแก้ด้วย
- คำอธิบายของ **VIP Ticket 32510-32513** ควรระบุว่า "แลกเปลี่ยนได้ / ใช้แล้วเพิ่มเวลา VIP"
- คำอธิบายของ **Auto Battle Pass 32500** ควรระบุว่า "เป็นไอเทมมีอายุ หมดเวลาแล้วไอเทมจะหายไป"
  (client จะแสดงเวลาที่เหลือของไอเทมเช่าให้เอง)

## หน้าต่าง Cash Shop
ไอเทมที่ขายใน Cash Shop ต้องมีข้อมูลใน iteminfo ด้วย ไม่งั้นช่องในหน้าต่าง Cash Shop จะว่าง
