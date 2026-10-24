# Builds sd:/aden/items.txt  ->  "<hex id>\t<category>\t<name>\t<max stack>\t<family>\t<dlc>" for every item a player can get.
# usage: make_items.py <String_USen dir> <out> [<romfs>/Bcsv]   (no Bcsv dir: every stack is 1, no DLC flags, nothing hidden)
#   family: F furniture, C clothing, N nature, M tools and materials, O other (Items tab filter)
#   dlc:    - none, H Happy Home Paradise, P Pocket Camp ticket, S Switch Online items (hidden when the DLC is not owned)
# Items whose source is "NotAvailable" (internal or unobtainable) and items that need a parameter are left out.
import sys,os,glob,re,struct,zlib
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'lib'))
import msbt,bcsv
src=sys.argv[1]; out=sys.argv[2]
# stack: ItemParam kind string (0xFC275E86, "Kind_<label>") -> ItemKind MultiHoldMaxNum (0x4C9BA961) by Label (0x87BF00E8); hashes from NHSE
# source: ItemParam 0xDB1A2F07 holds the CRC32 of an ItemFrom.bcsv label (0x87BF00E8)
stacks={}; source={}
if len(sys.argv)>3:
    def col(fn,h,fmt=None):
        rows,sizes=bcsv.load(os.path.join(sys.argv[3],fn)); o,n=sizes[h]
        return [struct.unpack_from(fmt,r,o)[0] if fmt else r[o:o+n].split(b'\0')[0].decode() for r in rows]
    kinds={'Kind_'+l:(10 if m<0 else max(m,1)) for l,m in zip(col('ItemKind.bcsv',0x87BF00E8),col('ItemKind.bcsv',0x4C9BA961,'<h'))}  # 0 = not stackable, -1 = turnips (10)
    froms={zlib.crc32(l.encode())&0xFFFFFFFF:l for l in col('ItemFrom.bcsv',0x87BF00E8)}
    for iid,k,f in zip(col('ItemParam.bcsv',0x54706054,'<H'),col('ItemParam.bcsv',0xFC275E86),col('ItemParam.bcsv',0xDB1A2F07,'<I')):
        stacks[iid]=kinds.get(k,1); source[iid]=froms.get(f,'')
items={}
for fn in sorted(glob.glob(os.path.join(src,'Item','STR_ItemName_*.msbt'))):
    cat=re.match(r'STR_ItemName_\d+_(\w+)\.msbt',os.path.basename(fn)).group(1)
    h,secs=msbt.parse(open(fn,'rb').read()); sd={m:b for m,b in secs}
    L=msbt.labels(sd[b'LBL1']); T=msbt.texts(sd[b'TXT2'])
    for lab,idx in L.items():
        m=re.match(r'(.+)_(\d+)$',lab)
        if not m: continue
        iid=int(m.group(2))
        name=re.sub(r'<[^>]*>','',msbt.show(T[idx])).strip()
        if not name: continue
        items[iid]=(cat,name)
# clothing: group name + color variant, labels '<grp>_<Kind>_<itemid>'
def load(fn):
    h,secs=msbt.parse(open(fn,'rb').read()); sd={m:b for m,b in secs}
    L=msbt.labels(sd[b'LBL1']); T=msbt.texts(sd[b'TXT2'])
    return {l:re.sub(r'<[^>]*>','',msbt.show(T[i])).strip() for l,i in L.items()}
for fn in sorted(glob.glob(os.path.join(src,'Outfit','GroupColor','STR_OutfitGroupColor_*.msbt'))):
    kind=re.match(r'STR_OutfitGroupColor_(\w+)\.msbt',os.path.basename(fn)).group(1)
    names=load(os.path.join(src,'Outfit','GroupName','STR_OutfitGroupName_%s.msbt'%kind))
    for lab,color in load(fn).items():
        m=re.match(r'(\w+?)_\w+_(\d+)$',lab)
        if not m: continue
        grp,iid=m.group(1),int(m.group(2))
        base=names.get(grp)
        if base and iid not in items: items[iid]=(kind,'%s (%s)'%(base,color) if color else base)
FAMILY={}
for f,cats in (('F','Ftr Poster Bromide RoomWall RoomFloor RoomRug Art Music Gyroid HouseDoorDeco HousePost Fence InsectToy FishToy PhotoStudioList'),
               ('C','Tops OnePiece Bottoms Cap Shoes Socks Accessory Helmet Bag HandBag Umbrella MarineSuit'),
               ('N','Plant Insect Fish DiveFish Fossil Shell'),
               ('M','Tool Craft Money Turnip')):
    for c in cats.split(): FAMILY[c]=f
def dlc(s):
    if s.startswith('OneRoom') or s.startswith('ShopOneRoom'): return 'H'   # "OneRoom" is Happy Home Paradise inside the game
    if s=='DeliveryPocketCamp': return 'P'
    if s in ('NSOBenefit2','MileExchangeNsoPresent'): return 'S'
    return '-'
NEEDS_PARAM={0x16A1,0x16A2}   # message bottle, DIY recipe card: the count field holds a recipe ID
hidden=0; n={}
with open(out,'w',encoding='utf-8',newline='\n') as f:
    f.write('# Aden Modmenu item list: hex id, category, name, max stack, family, dlc\n')
    for iid in sorted(items):
        c,nm=items[iid]; s=source.get(iid,'')
        if s=='NotAvailable' or iid in NEEDS_PARAM: hidden+=1; continue
        d=dlc(s); n[d]=n.get(d,0)+1
        f.write('%04X\t%s\t%s\t%d\t%s\t%s\n'%(iid,c,nm,stacks.get(iid,1),FAMILY.get(c,'O'),d))
print(len(items)-hidden,'items,',hidden,'hidden,',sum(1 for i in items if stacks.get(i,1)>1),'stackable, dlc',n)
