/* sand64-gba main: mode 4 (8bpp bitmap), 2x2 cell scaling, pad input.
 * Grid 120x80 -> screen 240x160. Back buffer in EWRAM pool. */
#include "../src/sand.h"

#define REG_DISPCNT   (*(volatile unsigned short *)0x04000000)
#define REG_DISPSTAT  (*(volatile unsigned short *)0x04000004)
#define REG_KEYINPUT  (*(volatile unsigned short *)0x04000130)

#define BG_CNT        (*(volatile unsigned short *)0x04000008)
#define PAL_RAM       ((volatile unsigned short *)0x05000000)
#define PAL_BG        ((unsigned short *)0x05000000)
#define VRAM          ((unsigned char *)0x06000000)

#define KEY_A         0x0001
#define KEY_B         0x0002
#define KEY_SELECT    0x0004
#define KEY_START     0x0008
#define KEY_RIGHT     0x0010
#define KEY_LEFT      0x0020
#define KEY_UP        0x0040
#define KEY_DOWN      0x0080
#define KEY_R         0x0100
#define KEY_L         0x0200

/* material -> color index mapping */
static unsigned char mat2col[8] = { 0, 1, 2, 3, 4, 5, 0, 0 };
/* palette slots: 0=bg black, 1=sand, 2=wall gray, 3=water blue, 4=oil brown, 5=fire orange */

static SandSim sim;
static unsigned char backbuf[240 * 160] __attribute__((section(".ewram")));
volatile unsigned int g_frame __attribute__((section(".ewram")));  /* heartbeat */
volatile unsigned int g_hash __attribute__((section(".ewram")));   /* sim hash */

/* freestanding helper */
void *memset(void *d, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static void set_palette(void) {
    PAL_BG[0] = 0x0000;   /* black */
    PAL_BG[1] = 0x01BF;   /* sand gold  (255,191,0) approx */
    PAL_BG[2] = 0x739A;   /* wall gray */
    PAL_BG[3] = 0x004F;   /* water blue */
    PAL_BG[4] = 0x0147;   /* oil brown */
    PAL_BG[5] = 0x041F;   /* fire orange */
    for (int i = 6; i < 16; i++) PAL_BG[i] = 0x0000;
}

static void render(void) {
    int x, y;
    unsigned char *out = backbuf;   /* render into RAM, copy to VRAM at vblank */
    for (y = 0; y < SH; y++) {
        unsigned short *row0 = (unsigned short *)(out + (y * 2) * 240);
        unsigned short *row1 = (unsigned short *)(out + (y * 2 + 1) * 240);
        for (x = 0; x < SW; x++) {
            unsigned short px = mat2col[sim.g[y * SW + x] & 7];
            unsigned short pair = px | (px << 8);
            row0[x] = pair;
            row1[x] = pair;
        }
    }
}

static int prev_keys;

static void poll_input(void) {
    int keys = (~REG_KEYINPUT) & 0x03FF;
    int pressed = keys & ~prev_keys;
    prev_keys = keys;

    if (pressed & KEY_LEFT)  sim.emit_x -= 4;
    if (pressed & KEY_RIGHT) sim.emit_x += 4;
    if (pressed & KEY_UP)    sim.emit_y -= 2;
    if (pressed & KEY_DOWN)  sim.emit_y += 2;
    if (sim.emit_x < 1) sim.emit_x = 1;
    if (sim.emit_x > SW - 2) sim.emit_x = SW - 2;
    if (sim.emit_y < 0) sim.emit_y = 0;
    if (sim.emit_y > SH - 3) sim.emit_y = SH - 3;

    if (pressed & KEY_A) {
        sim.emit_mat++;
        if (sim.emit_mat > C_FIRE) sim.emit_mat = C_SAND;
    }
    if (pressed & KEY_B) {
        /* dump a blob of current material at emitter */
        int ex = sim.emit_x, ey = sim.emit_y;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int nx = ex + dx, ny = ey + dy;
                if (nx >= 1 && nx < SW - 1 && ny >= 0 && ny < SH - 1)
                    if (((dx * dx) + (dy * dy)) <= 9)
                        sim.g[ny * SW + nx] = (unsigned char)sim.emit_mat;
            }
    }
    if (pressed & KEY_SELECT) sim_init(&sim, 0xBEEFu);
    sim.emit_mat = sim.emit_mat ? sim.emit_mat : C_EMPTY;
}

int main(void) {
    unsigned int seed = 0x1234567Fu;
    sim_init(&sim, seed);
    set_palette();
    REG_DISPCNT = 0x04C4;   /* LCD(bit7)+OBJ1D(bit6)+BG2(bit10)+mode4(bits0-3=0100) */

    for (;;) {
        poll_input();
        sim_step(&sim);
        render();
        g_frame++;
        g_hash = sim_hash(&sim);
        /* wait VBlank flag = bit 0 (bit 3 was the IRQ ENABLE bit — never set!) */
        {
            volatile unsigned int t = 0;
            while (!(REG_DISPSTAT & 1)) {
                if (++t > 300000u) break;   /* deadlock insurance */
            }
        }
        {
            volatile unsigned char *v = VRAM;
            const unsigned char *s = backbuf;
            int i;
            for (i = 0; i < 240 * 160; i += 4)
                *(volatile unsigned int *)(v + i) = *(const unsigned int *)(s + i);
        }
    }
    return 0;
}
