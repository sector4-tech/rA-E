# Auto Battle (server side)

Command: `@autobattle` (alias `@ab`). Permission: `autobattle` (conf/groups.yml).
Config: `conf/battle/feature.conf` (`autobattle_*`).

## Starting / stopping (item based)
* Use the **Auto Battle Pass** (`Auto_Battle_Pass`, id 32500, not consumed) to start or stop Auto Battle.
  The item runs the script command `autobattle 2;` (0 = stop, 1 = start, 2 = toggle).
* While active the character has the status `SC_AUTOBATTLE` (icon `EFST_AUTOBERSERK`, shown to nearby
  players) and a periodic effect is played around the character so other players can tell
  Auto Battle from manual play. Settings: `autobattle_item_only`, `autobattle_effect`,
  `autobattle_effect_interval` in `conf/battle/feature.conf`.
* With `autobattle_item_only: on` (default) `@ab start` is refused; `@ab stop` still works.
* The client needs an `iteminfo` entry for id 32500 to show a proper name/icon. The server item
  and status are in `db/re` and `db/pre-re` (`item_db_usable.yml`, `status.yml`).

## Setup dialog (no client changes needed)
* `@ab menu` opens the dialog anywhere, or talk to **Auto Battle Manager** (prontera,155,181).
* The dialog is `npc/custom/autobattle.txt` (enabled in `npc/scripts_custom.conf`).
* It only uses `@ab -q ...` commands (`-q` = quiet) and reads the character variables `ab_*`.

## Commands
```
start | stop | toggle | status | mobs | menu | sync | skills <attack|buff|support>
all <on|off> | attack <on|off> | fightback <on|off>
mob +<id|name> / -<id|name> / reset
skill <slot 1-16> <skill id|name> [lv] | skill clear <slot|all>
hp|sp <item id|name|off> [%]
buff <slot 1-8> <skill> [lv] | buff clear <slot|all>
support <slot 1-4> <skill> [lv] | support clear <slot|all> | support hp <%>
loot <on|off> | loot range <1-14> | loot +<item> / -<item> / reset
restock <on|off> | restock +<item> <amount> / -<item> / reset | restock min <n>
safety hp <%> | safety item <item|off> | safety player <on|off>
safety weight <on|off> [%] | safety noitem <on|off> | safety idle <sec>
```
Item/monster/skill names: use the id or the Aegis name (underscores instead of spaces).

## Character variables (read by the dialog, written by the server)
`ab_flags` (1 all, 2 normal attack, 4 fight back, 8 loot, 16 restock, 32 stop on player,
64 stop on weight, 128 stop on no item), `ab_hpitem`, `ab_spitem`, `ab_fleeitem`, `ab_pct1`, `ab_pct2`,
`ab_misc`, `ab_atk[16]`, `ab_buff[8]`, `ab_sup[4]` (skill id << 16 | level), `ab_mob[20]`, `ab_loot[30]`,
`ab_rsid[8]` / `ab_rsamt[8]`. Temporary: `@ab_running`, `@ab_mobn`, `@ab_mobid[]`, `@ab_mobcnt[]`,
`@ab_skn`, `@ab_skid[]`, `@ab_sklv[]`, `@ab_skname$[]`, `@ab_atkn$[]`, `@ab_buffn$[]`, `@ab_supn$[]`.

## Client UI
The window shown in newer clients is not part of Ragexe 2026-01-06/07. A DLL / client patch can
reuse the same commands or variables; see the packet notes in the discussion.