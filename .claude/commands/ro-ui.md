---
description: ปรับปรุงเมนู/หน้าตา UI ของระบบในเกมให้สวยและใช้ง่าย แต่ยังคงสไตล์ RO คลาสสิก
argument-hint: [ระบบที่จะปรับ เช่น autobattle, race_system, warper หรือ "ทุกเมนู"]
---
ปรับ UI ของ: **$ARGUMENTS**

ข้อจำกัดที่ต้องรู้: ฝั่ง server ส่งได้แค่ของที่ client 2026-01-07 รองรับอยู่แล้ว — หน้าต่างใหม่จริง ๆ ต้องแก้ client (lua/texture/patch) ซึ่งต้องให้ผู้ใช้ทำเองที่เครื่อง Windows

1. อ่านเมนูเดิม และทำรายการจุดที่ใช้ยาก: กดหลายชั้นเกินไป, ข้อความยาว, ไม่มีปุ่มกลับ, ไม่บอกค่าปัจจุบัน, สีไม่สม่ำเสมอ
2. เสนอแบบใหม่ (mockup เป็นข้อความเหมือนหน้าจอ NPC) ใช้เครื่องมือที่ client รองรับ:
   - NPC dialog: สี `^RRGGBB`, เส้นคั่น, ไอคอน item `<ITEM>` / `<ITEMLINK>`, `<URL>` (ถ้า client รองรับ), `cutin` รูปประกอบ
   - Navigation (`navigateto`), `showevent` (ไอคอนเหนือ NPC), `specialeffect`/`hateffect`, status icon (`efst`) พร้อมนับเวลา
   - Clan/Title, Roulette, Stylist, Rodex, Attendance, Achievement — ระบบ UI ที่ client มีอยู่แล้ว
   - Chat ของระบบ (`dispbottom` สี, `announce` สี) และ `@command` ลัด
3. รักษาแนวทาง: หัวเมนูรูปแบบเดียวกันทุกระบบ, ปุ่ม "กลับ / ปิด" ตำแหน่งเดิมเสมอ, แสดงค่าที่ตั้งไว้ปัจจุบันข้างตัวเลือก, ถามยืนยันก่อนใช้ของ/เงิน
4. สร้าง helper function กลาง (เช่น `npc/custom/lib/ui_lib.txt`: `F_UI_Header`, `F_UI_Confirm`, `F_UI_Bar` แสดงหลอดเปอร์เซ็นต์) เพื่อให้ทุกระบบหน้าตาเหมือนกัน
5. ถ้าต้องแก้ client ให้ระบุไฟล์และขนาดรูป (bmp/tga) ที่ต้องทำ และตัวอย่าง lua
6. เซฟ TIS-620, reload (`@reloadscript` บนเซิร์ฟทดสอบ), สรุปภาพก่อน/หลัง
