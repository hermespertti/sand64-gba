#!/bin/bash
# wait for gdb dump, then SIGINT gdb to break the continue
GPID=$1
for i in $(seq 1 14); do
  sleep 1
  if [ -f /tmp/vram.bin ]; then break; fi
  kill -INT $GPID 2>/dev/null
done
sleep 1
kill $GPID 2>/dev/null
