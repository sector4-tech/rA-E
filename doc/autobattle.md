# Auto Battle (server side)

Command: `@autobattle` (alias `@ab`) - **GM only** (Admin group has `all_commands`). Players use the pass item.
Config: `conf/battle/feature.conf` (`autobattle_*`).

## Auto Battle Pass (rA-E)
* The **Auto Battle Pass** (id 32500) is a **rental item**: the client shows the time left, the server deletes it
  when it runs out and Auto Battle stops by itself (`autobattle_pass_item`).
* The pass only comes out of the **Auto Battle Pass Boxes** (tradeable, sold in the Cash Shop / given at events):

  | Box | Id | AegisName | Cash price |
  |---|---|---|---|
  | 1 day | 32520 | `Auto_Battle_Box_1D` | 15 |
  | 3 days | 32521 | `Auto_Battle_Box_3D` | 39 |
  | 7 days | 32522 | `Auto_Battle_Box_7D` | 79 |

  Opening a box adds its days to the pass the player already has (`F_ABPassBox` / `F_ABPassAdd` in
  `npc/custom/autobattle.txt`). The pass can hold at most 60 days; a box that would go over is given back.
* New accounts get 1 day once from **Auto Battle Guide** next to the pre-renewal start point
  (`new_1-1` .. `new_5-1`, 57,114). Account variable `#AB_NEWBIE`. Days: `.newbie_days` in `AutoBattleUI`.
* A permanent pass (old system / `@item 32500`) is changed into a 1 day rental pass at login.
* The pass is the only way for players to open the menu and start / stop Auto Battle.

## Follow / support mode (rA-E)
For support classes (Priest...): the character follows a **master** (any online player, also outside the party)
or the **party leader**, heals / resurrects and buffs the master, itself and the party members nearby, and does
not hunt. With "fight back" on it attacks only monsters that attack it or the master.
* Menu: 4. ซัพพอร์ต / ฮีล / ติดตาม. Commands: `follow master <name>`, `follow party`, `follow off`,
  `follow dist <1-10>`, `follow combat <2-9>`, `follow warp <on|off>`, `pbuff <slot 1-8> <skill> [lv]`, `pbuff clear <slot|all>`.
* Heal skills (`support`, e.g. Heal, Resurrection) are used on the lowest HP % player under the threshold;
  Resurrection is used on dead players first. Buffs for others (`pbuff`, e.g. Blessing, Increase AGI, Kyrie
  Eleison, Impositio Manus) are re-cast on everybody who does not have them.
* **Warp after the master** (master in the SAME PARTY, player option `follow warp <on|off>`, default on):
  * same map, master out of sight (Fly Wing / Teleport) or not reachable on foot: teleport next to him
    (`autobattle_follow_teleport`, not on `noteleport` maps);
  * master on another map (Butterfly Wing, Kafra, portal, death...): warp to him after
    `autobattle_follow_warp_delay` ms (`autobattle_follow_warp`). Never into PvP/WoE/BG maps, `nowarpto` maps
    or instances of another party. Give quest-gated maps the `nowarpto` mapflag if every player must do the quest.
  * A master outside the party is only followed on foot.
* **Stand back while the master fights** (`follow combat <2-9>`, default 5): when the master attacks, casts or is
  attacked, the follower keeps about that distance on the side away from the monster, still inside heal range.
* When the master cannot be followed for `autobattle_follow_timeout` seconds (default 60), Auto Battle stops.
  Restock is not used in follow mode.

## Map rules (rA-E)
Auto Battle never warps a character out of a map where a Butterfly Wing is not allowed (`noreturn`, `nowarp`,
instances) and never warps it into `nowarpto` maps, instances or PvP/WoE/BG maps (restock, weight warp,
automatic return to the hunting map).

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
* Players: use the Auto Battle Pass. GMs: `@ab` (no arguments) opens the same dialog, `@ab help` lists the chat commands.
* The dialog is `npc/custom/autobattle.txt` (enabled in `npc/scripts_custom.conf`).
  **Save that file as TIS-620 / cp874.**
* The dialog never touches variables directly. It uses three script commands:
  * `autobattleinfo("<key>"{,<a>{,<b>}})` - read a setting (numbers).
  * `autobattlestr("<key>"{,<a>})` - read a name (skill / item / monster / map).
  * `autobattlecmd("<@ab arguments>")` - change a setting, same syntax as `@ab` (returns 1 on success).
* Chat messages are Thai (TIS-620) by default: `autobattle_lang: 1` (0 = English) in `conf/battle/feature.conf`.

## Commands
```
(no argument) | menu | start | stop | toggle | status | mobs | reset | skills <attack|buff|support|pbuff> | mapmobs
all <on|off> | attack <on|off> | fightback <on|off>
mob +<id|name> / -<id|name> / reset            blacklist +<id|name> / -<id|name> / reset
skill <slot 1-16> <skill id|name> [lv] | skill clear <slot|all> | skill cond <slot> <always|above|below> [%]
hp|sp <item id|name|off> [%] | pct <hp|sp> <%> | sit <on|off> | sit hp|sp <sit%> <stand%>
buff <slot 1-8> <skill> [lv] | buff clear <slot|all> | buffitem <slot 1-6> <item> [seconds] | buffitem clear <slot|all>
support <slot 1-4> <skill> [lv] | support clear <slot|all> | support hp <%>
follow master <name> | follow party | follow off | follow dist <1-10> | follow combat <2-9> | follow warp <on|off>
pbuff <slot 1-8> <skill> [lv] | pbuff clear <slot|all>
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
`ab_buff[]`, `ab_sup[]`, `ab_mob[]`, `ab_bl[]`, `ab_loot[]`, `ab_rsid[]`, `ab_rsamt[]`, `ab_bi[]`, `ab_biv[]`,
`ab_follow`, `ab_fmaster`, `ab_fmname$`, `ab_pbuff[]`...)
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

## Hat effect (an effect that stays over the character, e.g. a drone floating over the head)
`conf/battle/feature.conf`: `autobattle_hat_effect: 600` (0 = off). The effect is added when Auto Battle starts and
removed when it stops. `600` is `HAT_EF_AUTOBATTLE` (src/map/script.hpp). The client needs an entry with the same id:

1. Copy the effect folder out of the GRF into the client `data` folder, e.g. `data\texture\effect\a_machine\...`
   (the .str file plus its images).
2. `data\luafiles514\lua files\HatEffectInfo\HatEffectIDs.lub`: add `HAT_EF_AUTOBATTLE = 600,` (do not break the commas).
3. `data\luafiles514\lua files\HatEffectInfo\HatEffectInfo.lub`: add inside `hatEffectTable`
   ```lua
   [HatEFID.HAT_EF_AUTOBATTLE] = {
   	resourceFileName = "a_machine\\a_machine\\min_a_machine_waiting.str",
   	hatEffectPos = -10,
   	hatEffectPosX = 0,
   	isAdjustPositionWhenShrinkState = true,
   	isAdjustSizeWhenShrinkState = true
   },
   ```
   `hatEffectPos` / `hatEffectPosX` move the effect up/down / sideways, adjust until it sits where you want.
4. Restart the client, then `@reloadbattleconf` (or restart the server) and use Auto Battle.
