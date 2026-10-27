---
name: acnh-save-inspect
description: Decrypt the ACNH save from Eden to check offsets the mod reads (villagers, island anchor, turnips) without running the game. Use when diagnosing island or player search issues offline.
---

# Inspect the Eden ACNH save offline

Save folder (Eden, Windows):
`%APPDATA%/eden/nand/user/save/0000000000000000/00000000000000000000000000000000/01006F8002326000/`
- `main.dat` + `mainHeader.dat`: island save (decrypted size 0x9B0E90 = `MAIN_SIZE`).
- `Villager0/personal.dat` + `personalHeader.dat`: first player.

Copy them to a temp folder first; never write to the save folder. Delete decrypted
copies when done. Last mod log: `%APPDATA%/eden/sdmc/aden/log.txt`.

Decrypted files have the same layout as the game's in-memory copies, so the offsets in
`src/player.c` apply directly (`MAIN_VILL`, `VILL_SIZE`, `MAIN_KABU`, `PERS_ID`...).

Decryption (NHSE `Encryption.cs`, needs pycryptodome):

```python
import struct
from Crypto.Cipher import AES
from Crypto.Util import Counter
M=0xFFFFFFFF
class SeadRandom:
    def __init__(s,seed):
        c=[0]*4; p=seed
        for i in range(4): p=(0x6C078965*(p^(p>>30))+i+1)&M; c[i]=p
        s.c=c
    def u32(s):
        c=s.c; n=(c[0]^(c[0]<<11))&M
        c[0],c[1],c[2]=c[1],c[2],c[3]
        c[3]=(n^(n>>8)^c[3]^(c[3]>>19))&M
        return c[3]
def param(d,i):
    r=SeadRandom(d[d[i]&0x7F]); n=(d[d[i+1]&0x7F]&0xF)+1
    for _ in range(2*n): r.u32()
    return bytes((r.u32()>>24)&0xFF for _ in range(16))
def decrypt(header,data):
    d=struct.unpack_from('<128I',header,0x100)
    ctr=Counter.new(128,initial_value=int.from_bytes(param(d,2),'big'))
    return AES.new(param(d,0),AES.MODE_CTR,counter=ctr).decrypt(data)
```

Facts seen in a 3.0 save (Oct 2026):
- Empty villager slot: species byte 35, all memories zeroed.
- A villager the player has not talked to yet has no memory of the player (no town id
  at `V+4`), even though it lives on the island.

In-memory layout seen on Eden 3.0.3 (beta.17 log, Oct 2026):
- 2 personal copies back to back (0x74A60 apart: save size + 0x20 heap header), 2 island
  copies back to back, about 23 MB below the personal ones. Every base was 16-aligned and
  carried the file header.
- Full heap scans took 40 s (player, stride 8) and 53 s (island, stride 4); about half of
  that was likely naps (estimate, not measured). Newer builds log `MB read` and `ms napping`.
- beta.18 log with search timings: player scan 32 s for 2568 MB with only 1.4 s napping
  (the reading itself is the cost); island search near the personal save 2.5 s for 116 MB.
- Across two boots the absolute addresses moved (+8 MB) but the offsets from the heap start
  (svcGetInfo 4) stayed the same: the remembered offsets (`sdmc/aden/hint_*.bin`) loaded the
  player in 7 ms and the island in 18 ms.
