# Auto Battle (server side)

Command: `@autobattle` (alias `@ab`). Permission: `autobattle` (conf/groups.yml).
Config: `conf/battle/feature.conf` (`autobattle_*`).

## Starting / stopping (item based)
* Use the **Auto Battle Pass** (`Auto_Battle_Pass`, id 32500, not consumed) to start or stop Auto Battle.
  Using the item opens the setup menu directly (`callfunc "F_ABUI", 1;`); start / stop is a button inside the menu.
  The script command `autobattle {0|1|2};` (stop / start / toggle) still exists.
* While active the character has the status `SC_AUTOBATTLE` (icon `EFST_AUTOATTACK` = 1741, client file `EFST_AI_AUTOATTACK.tga`, shown to nearby
  players) and a periodic effect is played around the character so other players can tell
  Auto Battle from manual play. Settings: `autobattle_item_only`, `autobattle_effect`,
  `autobattle_effect_interval` in `conf/battle/feature.conf`.
* With `autobattle_item_only: on` (default) `@ab start` is refused; `@ab stop` still works.
* The client needs an `iteminfo` entry for id 32500 to show a proper name/icon. The server item
  and status are in `db/re` and `db/pre-re` (`item_db_usable.yml`, `status.yml`).

## Setup dialog (no client changes needed)
* Type `@ab` (no arguments) to open the Thai setup dialog anywhere, or talk to **Auto Battle Manager**
  (prontera,155,181). `@ab help` lists the chat commands.
* The dialog is `npc/custom/autobattle.txt` (enabled in `npc/scripts_custom.conf`).
  **Save that file as TIS-620 / cp874.**
* The dialog never touches variables directly. It uses three script commands:
  * `autobattleinfo("<key>"{,<a>{,<b>}})` - read a setting (numbers).
  * `autobattlestr("<key>"{,<a>})` - read a name (skill / item / monster / map).
  * `autobattlecmd("<@ab arguments>")` - change a setting, same syntax as `@ab` (returns 1 on success).
* Chat messages are Thai (TIS-620) by default: `autobattle_lang: 1` (0 = English) in `conf/battle/feature.conf`.

## Commands
```
(no argument) | menu | start | stop | toggle | status | mobs | reset | skills <attack|buff|support> | mapmobs
all <on|off> | attack <on|off> | fightback <on|off>
mob +<id|name> / -<id|name> / reset            blacklist +<id|name> / -<id|name> / reset
skill <slot 1-16> <skill id|name> [lv] | skill clear <slot|all> | skill cond <slot> <always|above|below> [%]
hp|sp <item id|name|off> [%] | pct <hp|sp> <%> | sit <on|off> | sit hp|sp <sit%> <stand%>
buff <slot 1-8> <skill> [lv] | buff clear <slot|all> | buffitem <slot 1-6> <item> [seconds] | buffitem clear <slot|all>
support <slot 1-4> <skill> [lv] | support clear <slot|all> | support hp <%>
loot <on|off> | loot range <1-14> | loot +<item> / -<item> / reset
restock <on|off> | restock +<item> <amount> / -<item> / reset | restock min <n>
safety hp <%> | safety item <item|off> | safety player <on|off> | safety weight <on|off> [%]
safety weightwarp <on|off> | safety noitem <on|off> | safety idle <sec> | safety mvp <on|off> | safety mini <on|off>
```
Item/monster/skill names: use the id or the Aegis name (underscores instead of spaces).

## Features
* Skill conditions: an attack skill can require the target monster to have more / less than N % HP.
* Blacklist: monsters that are never attacked unless they attack the player.
* Sit rest: sits down to regenerate when HP/SP are low and nothing attacks, stands up when restored.
* Buff items: consumables (concentration potion...) re-used every N seconds.
* Safety: flee item on low HP, stop when a player is near, overweight (stop or warp to the save point),
  flee from MVP / mini boss, idle timeout.

## Saved data
Settings are stored in permanent character variables (`ab_flags`, `ab_pct1..3`, `ab_atk[]`, `ab_atkc[]`,
`ab_buff[]`, `ab_sup[]`, `ab_mob[]`, `ab_bl[]`, `ab_loot[]`, `ab_rsid[]`, `ab_rsamt[]`, `ab_bi[]`, `ab_biv[]`...)
written with `pc_setregistry`, so they are saved to the `char_reg_num` table and survive relogging.

## Client UI
The window shown in newer clients is not part of Ragexe 2026-01-06/07. A DLL / client patch can
reuse the same commands or variables; see the packet notes in the discussion.

## Changing the effect shown to other players
`conf/battle/feature.conf`: `autobattle_effect: <EF_ id>` (see `doc/effect_list.md`, default 76 = EF_MAGNIFICAT)
and `autobattle_effect_interval: <ms>` (0 = off). Apply with `@reloadbattleconf`, no restart needed.

## Status icon order (client)
The client sorts status icons by the priority table in `stateiconimginfo.lub`. `EFST_AUTOATTACK` is placed in
`PRIORITY_GOLD` right after `EFST_VIPSTATUS` so the icon is shown first, after the VIP icon.
