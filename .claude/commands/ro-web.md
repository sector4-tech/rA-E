---
description: สร้าง/พัฒนาเว็บไซต์ผู้เล่น (ข่าว, event, ranking, สถานะเซิร์ฟ) และระบบหลังบ้านแอดมิน
argument-hint: [ส่วนที่จะทำ เช่น "หน้าแรก+ข่าว", "ranking", "admin: จัดการ item", "ระบบเติมเงิน"]
---
งานเว็บ: **$ARGUMENTS**

สถาปัตยกรรมที่ตกลงไว้ (ถ้ายังไม่มีโฟลเดอร์ `web-portal/` ให้เสนอโครงสร้างก่อนสร้าง):
- `web-portal/` แยกจาก source เซิร์ฟ, deploy บน Ubuntu เดียวกันหลัง Nginx + HTTPS (Certbot)
- Frontend ผู้เล่น: ธีม RO คลาสสิก (โทนครีม/น้ำตาล/ฟ้า, กรอบหน้าต่างแบบในเกม, รูป sprite) แต่ responsive บนมือถือ, ภาษาไทยเป็นหลัก
- ข้อมูล: อ่านจากฐานข้อมูล rAthena ด้วย **DB user แบบ read-only** สำหรับหน้า public; ตารางของเว็บเองขึ้นต้นด้วย `web_` (ข่าว, event, ผู้ดูแล, audit log)
- หน้า public: ข่าว/ประกาศ, ปฏิทิน event, ranking (Lv, MVP, WoE, Race star), ผู้เล่นออนไลน์, สถานะ login/char/map, ฐานข้อมูล item/monster, วิธีดาวน์โหลด/ติดตั้ง client, สมัครสมาชิก
- หลังบ้าน (Admin): login + 2FA, สิทธิ์แยกระดับ (Admin/GM/Editor), audit log ทุกการแก้ไข, จัดการข่าว/event, ค้นหา account/char, แบน/ปลดแบน, ส่งของทาง Rodex (`mail` table), ดู log การค้า/การเติมเงิน, ตั้งค่า event
- **ห้ามแก้ข้อมูลตัวละครที่ออนไลน์อยู่** — เขียนผ่าน Rodex / ตาราง queue ที่ NPC/C++ มารับ หรือเช็ก `online=0` ก่อน
- ความปลอดภัย: prepared statement ทุก query, CSRF, rate limit, hash รหัสผ่านตามที่ login-server รองรับ (`use_MD5_passwords` / ต้องตรงกัน), ความลับเก็บใน `.env` ที่ไม่ commit

ขั้นตอน: ยืนยัน stack กับผู้ใช้ครั้งแรก (ค่าเริ่มต้นที่แนะนำ: Next.js + TypeScript + Prisma/MySQL หรือ Laravel ถ้าผู้ใช้ถนัด PHP) → สร้างทีละหน้า → ทดสอบ local → ให้คำสั่ง deploy (systemd/pm2 + nginx) → สรุปวิธีใช้หลังบ้านเป็นภาษาไทย
