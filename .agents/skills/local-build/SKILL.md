---
name: local-build
description: Build subsdk9 on this Windows machine without clang or elf2nso installed (zig's clang/lld from pip plus a Python NSO writer), regenerate items.txt from the local game dump, and install both into Eden for a test. Use when a change must compile or be tried in-game before CI.
---

# Local build and Eden install

No clang, lld or elf2nso on this machine. `pip install ziglang` gives clang 21 and lld:
`python -m ziglang clang ...` and `python -m ziglang ld.lld ...`.

Build (same flags as `build.sh`, minus `-isystem`, which the code does not need):

```bash
Z="python -m ziglang"; O=/tmp/aden/o; mkdir -p $O; rm -f $O/*.o
CF="--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -O2 -fPIC -ffreestanding -fno-builtin -nostdlib -nostdinc -fvisibility=hidden -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-omit-frame-pointer -mno-outline-atomics -Wall -Werror -Wno-unused-function -Wno-unused-command-line-argument"
for f in src/*.c src/*.s; do $Z clang $CF -c "$f" -o "$O/$(basename "$f").o"; done
$Z ld.lld -shared -T src/link.ld --gc-sections -z text -z now --no-undefined-version --build-id=sha1 -init=aden_module_init -o $O/aden.elf $O/*.o
python .agents/skills/local-build/elf2nso.py $O/aden.elf $O/subsdk9
```

`elf2nso.py` writes an uncompressed NSO (hash-checked segments, build id as module id).
Check it with `python tools/lib/nso.py <nso> <img.bin> kernel32.dll` (any DLL works when
nothing is compressed).

Game dump: `C:/Users/User/Documents/Eden/work` (`romfs/Bcsv`, unpacked `gen/String_USen`).

```bash
python tools/make_items.py C:/Users/User/Documents/Eden/work/gen/String_USen /tmp/aden/items.txt C:/Users/User/Documents/Eden/work/romfs/Bcsv
```

Install into Eden (back up first, and keep backups outside `exefs/`):
- `%APPDATA%/eden/load/01006F8002326000/AdenModmenu/exefs/subsdk9`
- `%APPDATA%/eden/sdmc/aden/items.txt`

Release builds still come from CI (`build.sh` on Ubuntu, compressed NSO); the release
`items.txt` lives in the private draft release `game-files`.
