# Builds sd:/aden/items.txt  ->  "<hex id>\t<category>\t<name>\t<max stack>\t<family>\t<dlc>" for every item a player can get.
# usage: make_items.py <String_USen dir> <out> [<romfs>/Bcsv]   (no Bcsv dir: every stack is 1, no DLC flags, nothing hidden)
#   family: F furniture, C clothing, N nature, M tools and materials, O other (Items tab filter)
#   dlc:    - none, H Happy Home Paradise, P Pocket Camp ticket, S Switch Online items (hidden when the DLC is not owned)
#   group:  hex id of the first colour of a clothing item ("-" for that one and for items with one colour); the other
#           colours come right after it, so the Items grid shows one tile per clothing item
#   body, fabric: remake variants "<part name>|<variant 0>|<variant 1>|..." (empty: none); the item count field
#           holds body + 32 * fabric. Stackable items get none (their count field is the stack)
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
outfit={}   # item id -> (kind, group label): the colours of one clothing item
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
        outfit[iid]=(kind,grp)
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
# remake variants: names labelled '<kind>_<itemid>_<index>', part names '<kind>_<itemid>'
remake={}
rdir=os.path.join(src,'Remake')
clean=lambda t:t.replace('|','/').replace('\t',' ') or '?'
if os.path.isdir(rdir):
    for k,cfn,pfn,dflt in (('B','BodyColor','BodyParts','Variation'),('F','FabricColor','FabricParts','Pattern')):
        parts={int(m.group(1)):v for lab,v in load(os.path.join(rdir,'STR_Remake_%s.msbt'%pfn)).items() for m in [re.match(r'.+_(\d+)$',lab)] if m}
        cols={}
        for lab,v in load(os.path.join(rdir,'STR_Remake_%s.msbt'%cfn)).items():
            m=re.match(r'.+_(\d+)_(\d+)$',lab)
            if m: cols.setdefault(int(m.group(1)),{})[int(m.group(2))]=v
        for i,c in cols.items():
            if len(c)<2 or sorted(c)!=list(range(len(c))): continue   # the variant number is the label index
            remake.setdefault(i,{})[k]='|'.join([clean(parts.get(i,dflt))]+[clean(c[x]) for x in range(len(c))])
NEEDS_PARAM={0x16A1,0x16A2}   # message bottle, DIY recipe card: the count field holds a recipe ID
keep=[i for i in sorted(items) if source.get(i,'')!='NotAvailable' and i not in NEEDS_PARAM]
groups={}
for iid in keep:
    if iid in outfit: groups.setdefault(outfit[iid],[]).append(iid)
lead={m:g[0] for g in groups.values() if len(g)>1 for m in g}
order=[]
for iid in keep:
    if lead.get(iid,iid)!=iid: continue   # written right after the first colour
    order+= groups[outfit[iid]] if iid in lead else [iid]
n={}; nv=0
with open(out,'w',encoding='utf-8',newline='\n') as f:
    f.write('# Aden Modmenu item list: hex id, category, name, max stack, family, dlc, group, body, fabric\n')
    for iid in order:
        c,nm=items[iid]; d=dlc(source.get(iid,'')); n[d]=n.get(d,0)+1
        st=stacks.get(iid,1); r=remake.get(iid,{}) if st==1 else {}
        g=lead.get(iid,iid); nv+=bool(r)
        f.write('%04X\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\n'%(iid,c,nm,st,FAMILY.get(c,'O'),d,'%04X'%g if g!=iid else '-',r.get('B',''),r.get('F','')))
print(len(keep),'items,',len(items)-len(keep),'hidden,',sum(1 for i in keep if stacks.get(i,1)>1),'stackable, dlc',n,
      sum(1 for g in groups.values() if len(g)>1),'clothing items with colours,',nv,'items with remake variants')
