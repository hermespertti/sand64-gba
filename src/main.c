/* sand64-gba v1 — native 120x80 grid upscaled 2x to mode 4 (240x160).
 * 8bpp palette with 4 dither shades per material (Bayer-ish), so sand has
 * texture without burning memory. Back buffer lives in IWRAM (fast CPU side),
 * VRAM upload happens in VBlank. Keys:
 *   D-pad     move emitter (hold)
 *   A         cycle material  sand -> wall -> water -> oil -> fire
 *   B         dump blob
 *   L / R     emitter rate down/up (emit_shift)
 *   SELECT    reset
 *   START     toggle emitter on/off
 */
#include "sand.h"

#define REG_DISPCNT   (*(volatile unsigned short *)0x04000000)
#define REG_DISPSTAT  (*(volatile unsigned short *)0x04000004)
#define REG_KEYINPUT  (*(volatile unsigned short *)0x04000130)
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

/* material -> first of its 4 shade slots (material*4, empty is slot 0 dup) */
static const unsigned short shades[6][4] = {
    {0x0000,0x0000,0x0000,0x0000},          /* empty */
    {0x1F3F,0x0EDF,0x0ABC,0x065A},          /* sand gold (255,200,60).. */
    {0x5252,0x4610,0x5AB5,0x3DAD},         /* wall gray */
    {0x7DE7,0x7985,0x7E2A,0x6D42},         /* water blue */
    {0x154F,0x0CEC,0x1971,0x0CCA},         /* oil brown */
    {0x023F,0x017F,0x16DF,0x00FF},         /* fire orange/red */
};

static SandSim sim;
static unsigned char backbuf[240 * 160] __attribute__((section(".ewram")));
volatile unsigned int g_frame __attribute__((section(".ewram")));  /* heartbeat */
volatile unsigned int g_hash __attribute__((section(".ewram")));   /* sim hash */
volatile unsigned int g_pour  __attribute__((section(".ewram")));  /* emitter on */

void *memset(void *d, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static void set_palette(void) {
    for (int m = 0; m < 6; m++)
        for (int k = 0; k < 4; k++)
            PAL_BG[m * 4 + k] = shades[m][k];
    for (int i = 24; i < 256; i++) PAL_BG[i] = 0;
}

/* one src pair (a,b) -> one 32-bit word of 4 x 8bpp indices [ia,ia,ib,ib]
 * (horizontal 2x). mode 4 stores INDICES into BG palette. */
static inline unsigned int pair_word(unsigned char a, unsigned char b, int ph) {
    unsigned char ia = (unsigned char)(((a & 7) << 2) | (ph & 3));
    unsigned char ib = (unsigned char)(((b & 7) << 2) | ((ph + 1) & 3));
    return (unsigned int)ia | ((unsigned int)ia << 8) |
           ((unsigned int)ib << 16) | ((unsigned int)ib << 24);
}

static void render(void) {
    int y, i;
    for (y = 0; y < SH; y++) {
        unsigned int *row0 = (unsigned int *)(backbuf + (y * 2) * 240);
        unsigned int *row1 = (unsigned int *)(backbuf + (y * 2 + 1) * 240);
        const unsigned char *src = &sim.g[y * SW];
        int ph = y & 3;
        /* SW=120 src px -> 240 screen px = 60 words per row */
        for (i = 0; i < SW / 2; i++) {
            unsigned int w = pair_word(src[2 * i], src[2 * i + 1], ph + i);
            row0[i] = w;
            row1[i] = w;    /* vertical 2x */
        }
    }
}

static int prev_keys;

static void poll_input(void) {
    int keys = (~REG_KEYINPUT) & 0x03FF;
    int pressed = keys & ~prev_keys;
    prev_keys = keys;

    if (pressed & KEY_START)  g_pour ^= 1;
    if (keys & KEY_LEFT)  sim.emit_x -= 2;
    if (keys & KEY_RIGHT) sim.emit_x += 2;
    if (keys & KEY_UP)    sim.emit_y -= 1;
    if (keys & KEY_DOWN)  sim.emit_y += 1;
    if (sim.emit_x < 1) sim.emit_x = 1;
    if (sim.emit_x > SW - 2) sim.emit_x = SW - 2;
    if (sim.emit_y < 0) sim.emit_y = 0;
    if (sim.emit_y > SH - 3) sim.emit_y = SH - 3;

    if (pressed & KEY_L) { if (sim.emit_shift > 0) sim.emit_shift--; }
    if (pressed & KEY_R) { if (sim.emit_shift < 5) sim.emit_shift++; }

    if (pressed & KEY_A) {
        sim.emit_mat++;
        if (sim.emit_mat > C_FIRE) sim.emit_mat = C_SAND;
    }
    if (pressed & KEY_B) {
        int ex = sim.emit_x, ey = sim.emit_y;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -4; dx <= 4; dx++)
                if ((dx * dx) + (dy * dy) <= 13) {
                    int nx = ex + dx, ny = ey + dy;
                    if (nx >= 1 && nx < SW - 1 && ny >= 0 && ny < SH - 1) {
                        sim.g[ny * SW + nx] = (unsigned char)sim.emit_mat;
                        sim.dirty[ny] = 1;
                    }
                }
    }
    if (pressed & KEY_SELECT) { sim_init(&sim, 0xBEEFu); g_pour = 1; }

    /* emitter master gate */
    sim.emit_on = g_pour ? 1 : 0;
}

int main(void) {
    sim_init(&sim, 0x1234567Fu);
    g_pour = 1;
    set_palette();
    REG_DISPCNT = 0x04C4;   /* LCD(bit7)+OBJ1D(bit6)+BG2(bit10)+mode4 */

    for (;;) {
        poll_input();
        sim_step(&sim);
        render();
        g_frame++;
        g_hash = sim_hash(&sim);
        {
            volatile unsigned int t = 0;
            while (!(REG_DISPSTAT & 1))
                if (++t > 300000u) break;   /* deadlock insurance */
        }
        {   /* 32-bit copy backbuf -> VRAM */
            volatile unsigned int *v = (volatile unsigned int *)VRAM;
            const unsigned int *s = (const unsigned int *)backbuf;
            int i;
            for (i = 0; i < (240 * 160) / 4; i++) v[i] = s[i];
        }
    }
}
