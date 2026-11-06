---
description: วิเคราะห์และปรับ balance (rate, drop, ค่า bonus ของ Race/VIP/Hat effect, monster, skill) สำหรับ Pre-Re 99/70
argument-hint: [หัวข้อ เช่น "exp rate", "Race bonus", "Assassin Cross แรงไป", "MVP drop"]
---
หัวข้อ balance: **$ARGUMENTS**

1. รวบรวมค่าปัจจุบันจากไฟล์จริง (`conf/battle/*.conf`, `conf/import/battle_conf.txt` ถ้ามี, `db/pre-re/*.yml`, bonus ใน `race_system.txt`, VIP ใน `conf/battle/*`, hat effect)
2. แสดงเป็นตาราง: ค่าเดิม | ผลในเกม | ข้อเสนอ | เหตุผล — เทียบกับค่ามาตรฐานของเซิร์ฟ Pre-Re ทั่วไป (official / low / mid rate) และระบุว่ากระทบใคร (สายอาชีพ, ผู้เล่นใหม่, VIP vs ไม่ VIP)
3. ระวัง "pay-to-win": ของที่ขายด้วยเงินจริงห้ามให้พลังต่อสู้เหนือกว่าผู้เล่นฟรีแบบชัดเจน
4. รอผู้ใช้เลือกก่อนแก้ จากนั้นแก้ใน `conf/import/` หรือ `db/import/` เมื่อทำได้ (ไม่ทับไฟล์ upstream)
5. บอกวิธีตรวจในเกม เช่น `@mi`, `@iteminfo`, `@exprate` (ถ้ามี), `@reloadmobdb`
