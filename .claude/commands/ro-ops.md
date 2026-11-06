---
description: ดูแลเซิร์ฟ Ubuntu — ติดตั้งเครื่องมือ, backup, monitor, อัปเดตจาก rAthena upstream, ความปลอดภัยเครื่อง
argument-hint: [งาน เช่น "ติดตั้ง backup อัตโนมัติ", "merge upstream", "ตั้ง firewall", "เซิร์ฟ crash"]
---
งานดูแลระบบ: **$ARGUMENTS**

แนวทาง:
- ให้คำสั่งเป็นชุดที่ copy ไปวางบน Ubuntu ได้ทันที พร้อมอธิบายแต่ละบรรทัดเป็นภาษาไทย; คำสั่งที่ลบ/เขียนทับข้อมูลต้องมี backup ก่อนเสมอ
- **Service**: รัน login/char/map/web เป็น `systemd` service (restart อัตโนมัติเมื่อ crash, เก็บ log ด้วย journald) แทน `athena-start`
- **Backup**: `mysqldump --single-transaction` ทุกวัน + เก็บ 14 วัน + ส่งออกนอกเครื่อง (rclone ไป Google Drive/S3), ทดสอบ restore จริง
- **Security**: `ufw` เปิดเฉพาะพอร์ต 6900/6121/5121 (+ 80/443 เว็บ, 22 SSH เฉพาะ key), `fail2ban`, MariaDB bind localhost, ปิด root login, user แยกสำหรับรันเซิร์ฟ
- **Crash**: build แบบ `RelWithDebInfo`, เปิด core dump, ใช้ `gdb` + `tools/stackdump` อ่าน backtrace แล้วหาสาเหตุใน source
- **Monitor**: เช็กพอร์ต/ผู้เล่นออนไลน์ทุก 1 นาที แจ้งเตือนเข้า Discord webhook; ใช้ `htop`, `iotop`
- **Upstream merge**: `git remote add upstream https://github.com/rathena/rathena`, merge เข้า branch ทดสอบก่อน, แก้ conflict โดยคงระบบ custom (autobattle, race, VIP, hat effect, HICLASS_ONLY), compile + ทดสอบ แล้วจึง merge เข้า `prere-hiclass-vip`
- ถ้าเชื่อม Claude กับเครื่อง Ubuntu ไม่ได้ ให้ผลลัพธ์เป็นสคริปต์ใน `tools/rae/` แล้วบอกวิธีรัน
