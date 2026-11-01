-- Run once against the database used by map-server (the one holding the `market` table):
--   mysql -u <user> -p <database> < fix_market.sql
-- Cleans leftover renewal-era rows from `market` that don't exist in this Pre-Renewal setup.
-- Back up first:  mysqldump -u <user> -p <database> market > market_backup.sql

-- 1) Items that don't exist in db/pre-re (log: "Invalid sell item in table 'market'")
DELETE FROM `market` WHERE `nameid` IN (
 1000564,25187,7664,7665,13220,13219,7663,13218,13217,13216,13215,13221,13228,13229,13230,13222,13232,13231,
 18007,18006,18005,18008,100158,23277,31144,1774,1773,12786,100252,100251,1000227,100004,100003,23368,1000374,
 22658,22659,7642,23016,22687,6755,6607,6608,12680,12676,12679,1000563,101117,1000567,1000566,1000568,1220004,
 1220002,1220001,1000565,1220003,1000569);

-- 2) Shops whose NPC only exists in the renewal scripts (npc/re) - log: "NPC '...' not found, removing..."
DELETE FROM `market` WHERE `name` IN (
 'Ammunition_Gunslinger','change_slot#slo','market_refine_alberta_in','para_arm10','Ammunition_Mechanic',
 'armor_rockridge_4','armor_rockridge_3','armor_rockridge_2','armor_rockridge_1','market_refine_yuno_in01',
 'emp_seller#slo','para_ptn00','para_itm10','Catch_Arrows','para_mag20','market_refine_payon','para_ref10',
 'para_mag10','market_resonance_stone','para_mag11','para_key10','para_alc10','ep172littlemanager00','para_ptn10',
 'para_coin10','Weapons_Ninja','para_wp00','para_alc30','para_fod10','market_soda','ep172cubeshop00',
 'market_shop_prt_mk','para_mora10','market_refine_ein_in01','para_jew10','para_alc20','para_seed10',
 'market_refine_morocc_in','Talisman_Merchant','market_refine_lhz_in02','para_hei10','Arrow_Archer','para_itm00',
 'para_ref20','para_plt01','market_refine_prt_in');
