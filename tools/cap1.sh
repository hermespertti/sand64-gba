#!/bin/bash
# single gdb capture: boot mgba --gdb, halt, read heartbeat, dump backbuf
cd /home/lex/gba/sand64-gba || exit 1
pkill -x mgba 2>/dev/null
sleep 1
(xvfb-run -a mgba --gdb sand64gba.gba > /tmp/mgba.log 2>&1 &)
for i in $(seq 1 20); do
  ss -tln 2>/dev/null | grep -q 2345 && break
  sleep 0.5
done
rm -f /tmp/ewram_hb.bin
timeout 40 gdb -batch -ex "set pagination off" -ex "set confirm off" \
  -ex "set mem inaccessible-by-default off" \
  -ex "target remote localhost:2345" \
  -ex "x/2xw 0x02000000" \
  -ex "dump binary memory /tmp/ewram_hb.bin 0x02000000 0x02009600" \
  -ex "printf \"DUMPED\n\"" 2>&1 | grep -viE "warning|openclaw|fish|no such" | tail -8
ls -la /tmp/ewram_hb.bin 2>/dev/null
pkill -x mgba 2>/dev/null
echo CAP_DONE
