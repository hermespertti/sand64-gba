import sys, struct, zlib
pal = [
 (0,0,0),       # 0 bg black
 (255,191,0),  # 1 sand gold
 (115,154,115),# 2 wall gray-ish (from 0x739A approx)
 (0,0,255),    # 3 water blue
 (0,20,71),    # 4 oil brown
 (255,130,0),  # 5 fire orange
]
def to5b(v): return (v & 0x1f)*8
def bgr555(v):
    return ((v & 0x1f)*8, ((v>>5)&0x1f)*8, ((v>>10)&0x1f)*8)
d=open(sys.argv[1],'rb').read()[:38400]
w,h=240,160
raw=bytearray()
for y in range(h):
    raw.append(0)
    for x in range(w):
        c=d[y*w+x] & 7
        r,g,b=pal[c] if c < len(pal) else (0,0,0)
        raw += bytes((r,g,b))
out=open(sys.argv[2],'wb')
def chunk(t,data):
    out.write(struct.pack('>I',len(data))+t+data+struct.pack('>I',zlib.crc32(t+data)&0xffffffff))
out.write(b'\x89PNG\r\n\x1a\n')
chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
chunk(b'IDAT',zlib.compress(bytes(raw),9))
chunk(b'IEND',b'')
out.close()
print("wrote",sys.argv[2])
