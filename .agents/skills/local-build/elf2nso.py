# Minimal elf2nso: three PT_LOAD segments (text, ro, data) -> uncompressed NSO with segment hashes.
import struct,sys,hashlib
e=open(sys.argv[1],'rb').read()
phoff,=struct.unpack_from('<Q',e,0x20); phentsize,phnum=struct.unpack_from('<HH',e,0x36)
loads=[]
for i in range(phnum):
    t,fl,off,va,pa,fs,ms,al=struct.unpack_from('<IIQQQQQQ',e,phoff+i*phentsize)
    if t==1: loads.append((off,va,fs,ms))
assert len(loads)==3,loads
shoff,=struct.unpack_from('<Q',e,0x28); shentsize,shnum,shstrndx=struct.unpack_from('<HHH',e,0x3A)
secs=[struct.unpack_from('<IIQQQQIIQQ',e,shoff+i*shentsize) for i in range(shnum)]
so=secs[shstrndx][4]
bid=b''
for s in secs:
    n=e[so+s[0]:e.index(b'\0',so+s[0])]
    if n==b'.note.gnu.build-id':
        nsz,dsz,typ=struct.unpack_from('<III',e,s[4]); bid=e[s[4]+12+((nsz+3)&~3):][:dsz]
segs=[e[o:o+fs] for o,va,fs,ms in loads]
h=bytearray(0x100); h[0:4]=b'NSO0'
struct.pack_into('<I',h,0xC,0x38)   # uncompressed, hash checked
fo=0x100
for i,((o,va,fs,ms),d) in enumerate(zip(loads,segs)):
    struct.pack_into('<III',h,0x10+i*0x10,fo,va,fs); struct.pack_into('<I',h,0x60+i*4,fs)
    h[0xA0+i*0x20:0xC0+i*0x20]=hashlib.sha256(d).digest(); fo+=fs
struct.pack_into('<I',h,0x1C,1); struct.pack_into('<I',h,0x2C,1)
dva,dfs,dms=loads[2][1],loads[2][2],loads[2][3]
struct.pack_into('<I',h,0x3C,((dms+0xFFF)&~0xFFF)-dfs)
h[0x40:0x40+len(bid)]=bid[:0x20]
open(sys.argv[2],'wb').write(bytes(h)+b''.join(segs))
print('nso',sys.argv[2],hex(fo),'build id',bid.hex())
