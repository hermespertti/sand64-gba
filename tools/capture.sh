#!/bin/bash
# sand capture: boot mgba, attach gdb, run N secs, SIGINT, dump ewram
cd /home/lex/gba/sand64-gba
pkill -x mgba; sleep 1
(xvfb-run -a mgba --gdb sand64gba.gba > /tmp/mgba.log 2>&1 &)
for i in $(seq 1 20); do
  ss -tln 2>/dev/null | grep -q 2345 && break
  sleep 0.5
done
rm -f /tmp/ewram3.bin
gdb -q > /tmp/gdb.log 2>&1 <<'GDB' &
set pagination off
set confirm off
set mem inaccessible-by-default off
target remote localhost:2345
GDB
GPID=$!
sleep "${1:-10}"
kill -INT $GPID 2>/dev/null
for i in $(seq 1 10); do
  grep -q '(gdb)' /tmp/gdb.log 2>/dev/null && break
  kill -INT $GPID 2>/dev/null
  sleep 0.5
done
# target now halted; feed commands via stdin copy trick — instead use a second attach
kill $GPID 2>/dev/null; sleep 1
# after SIGINT gdb usually closed conn; use fresh batch attach while target halted
timeout 20 gdb -batch -ex "set confirm off" -ex "set mem inaccessible-by-default off" \
  -ex "target remote localhost:2345" \
  -ex "x/2xw 0x02000000" \
  -ex "dump binary memory /tmp/ewram3.bin 0x02000000 0x02009600" \
  -ex "printf \"DUMPED\n\"" \
  -ex "continue" 2>&1 | tail -8
pkill -x mgba
echo "DONE run_secs=${1:-10}"
