set confirm off
set pagination off
target remote localhost:2345
b *0x08000750
continue
dump binary memory /tmp/vram.bin 0x06000000 0x06009600
printf "DUMPED\n"
detach
quit
