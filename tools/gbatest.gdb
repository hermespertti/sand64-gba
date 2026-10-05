set confirm off
set pagination off
target remote :2345
continue &
shell sleep 4
interrupt
shell sleep 1
x/120xb 0x06000000
dump binary memory /tmp/vram.bin 0x06000000 0x060A0000
printf "VRAM DUMPED\n"
detach
quit
