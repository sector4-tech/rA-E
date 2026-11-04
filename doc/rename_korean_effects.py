# rename_korean_effects.py  -- run INSIDE data\texture\effect :   python rename_korean_effects.py
# The Thai client cannot open Korean file names, so the .str files are renamed in place (same folder, so textures keep working).
import os,sys
R=[['각성.str', 'kr_awakening.str'], ['노란포션.str', 'kr_yellow_potion.str'], ['도깨비불\\strangelights.str', 'kr_dokkaebi_fire\\strangelights.str'], ['디스펠.str', 'kr_dispell.str'], ['매직로드.str', 'kr_magic_road.str'], ['버서크.str', 'kr_berserk.str'], ['불길한붉은달빛.str', 'kr_ominous_red_moonlight.str'], ['불스아이.str', 'kr_bulls_eye.str'], ['빨간포션.str', 'kr_red_potion.str'], ['사람효과.str', 'kr_person_effect.str'], ['소울번.str', 'kr_soul_burn.str'], ['술식전개2.str', 'kr_spell_deploy2.str'], ['술식전개3.str', 'kr_spell_deploy3.str'], ['술식전개4.str', 'kr_spell_deploy4.str'], ['술식전개5.str', 'kr_spell_deploy5.str'], ['술식해방.str', 'kr_spell_release.str'], ['연환.str', 'kr_chain_ring.str'], ['이그니션브레이크.str', 'kr_ignition_break.str'], ['주홍포션.str', 'kr_scarlet_potion.str'], ['집중.str', 'kr_concentration.str'], ['찹쌀떡.str', 'kr_mochi.str'], ['초록포션.str', 'kr_green_potion.str'], ['초승달2.str', 'kr_crescent_moon2.str'], ['트랙킹.str', 'kr_tracking.str'], ['파란포션.str', 'kr_blue_potion.str'], ['하얀포션.str', 'kr_white_potion.str'], ['허무의그림자.str', 'kr_shadow_of_void.str'], ['흐린달빛환상2.str', 'kr_hazy_moonlight2.str']]
for old,new in R:
    o=old.replace('\\',os.sep); n=new.replace('\\',os.sep)
    if '\\' in old:   # folder rename: 도깨비불 -> kr_dokkaebi_fire (all files in it move together)
        od=os.path.dirname(o); nd=os.path.dirname(n)
        if os.path.isdir(od) and not os.path.exists(nd): os.rename(od,nd)
        print('folder',od,'->',nd); continue
    if os.path.exists(o) and not os.path.exists(n):
        os.rename(o,n); print('ok  ',o,'->',n)
    else: print('skip',o,'(missing or target exists)')
