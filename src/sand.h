/* sand64-gba core — pure C, host/GBA shared.
 * v1: native resolution grid (240x160 on GBA), dirty-row tracking,
 * dithered materials, fire flicker. Deterministic contract: xorshift rng,
 * flipped scan parity per frame, identical probe lines across platforms.
 * N64 sibling uses SW=120/SH=80; override SW/SH at compile time. */
#ifndef SAND64_CORE_H
#define SAND64_CORE_H

#ifndef SW
#define SW 120
#endif
#ifndef SH
#define SH 80
#endif

enum { C_EMPTY = 0, C_SAND = 1, C_WALL = 2, C_WATER = 3, C_OIL = 4, C_FIRE = 5 };

typedef struct {
    unsigned char g[SW * SH];
    unsigned int rng;
    int frame;
    int emit_x, emit_y;
    int emit_mat;
    int emit_shift;         /* fires when (frame & ((1<<shift)-1))==0 */
    int emit_w;             /* half-width of emitter in cells */
    int emit_on;            /* master gate (START toggles) */
    unsigned char dirty[SH];      /* rows touched this frame */
    unsigned long sand_moved, water_moved;
} SandSim;

static unsigned int sr_next(SandSim *s) {
    unsigned int x = s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->rng = x ? x : 0x9E3779B9u;   /* never zero */
    return s->rng;
}

static void sim_init(SandSim *s, unsigned int seed) {
    int i, x, y;
    s->rng = seed ? seed : 1u;
    s->frame = 0;
    s->emit_x = SW / 2; s->emit_y = 2; s->emit_mat = C_SAND;
    s->emit_shift = 1; s->emit_w = 1; s->emit_on = 1;
    s->sand_moved = s->water_moved = 0;
    for (i = 0; i < SW * SH; i++) s->g[i] = C_EMPTY;
    for (i = 0; i < SH; i++) s->dirty[i] = 1;   /* first frame uploads all */
    /* floor + side walls + central pyramid, scaled with the grid */
    for (x = 0; x < SW; x++) {
        s->g[(SH - 1) * SW + x] = C_WALL;
        s->g[(SH - 2) * SW + x] = C_WALL;
    }
    for (y = 0; y < SH; y++) {
        s->g[y * SW + 0] = C_WALL;
        s->g[y * SW + SW - 1] = C_WALL;
    }
    for (x = SW / 2 - SW / 8; x <= SW / 2 + SW / 8; x++) {
        int r = (x > SW / 2) ? (x - SW / 2) : (SW / 2 - x);
        int h = SW / 8 - r / 2;
        for (y = SH - 3 - h; y < SH - 2; y++)
            s->g[y * SW + x] = C_WALL;
    }
}

static void mark_dirty(SandSim *s, int y) { if (y >= 0 && y < SH) s->dirty[y] = 1; }

/* one physics step: materials fall/flow; bottom-up scan, parity flip per frame */
static void sim_step(SandSim *s) {
    int x, y;
    unsigned char *g = s->g;
    s->frame++;

    int rev = (s->frame & 1);
    for (y = SH - 2; y >= 1; y--) {
        int row = y * SW;
        int nrow = row + SW;
        for (int xi = 0; xi < SW; xi++) {
            x = rev ? (SW - 1 - xi) : xi;
            unsigned char c = g[row + x];
            if (c == C_EMPTY || c == C_WALL) continue;

            int fell = 0;
            if (c == C_SAND || c == C_OIL || c == C_WATER) {
                int below = nrow + x;
                unsigned char b = g[below];
                if (c == C_SAND && (b == C_WATER || b == C_OIL)) {
                    g[below] = C_SAND; g[row + x] = b; s->sand_moved++; fell = 1;
                    mark_dirty(s, y + 1);
                } else if (c == C_WATER && b == C_OIL) {
                    g[below] = C_WATER; g[row + x] = C_OIL; s->water_moved++; fell = 1;
                    mark_dirty(s, y + 1);
                } else if (b == C_EMPTY) {
                    g[below] = c; g[row + x] = C_EMPTY;
                    if (c == C_SAND) s->sand_moved++; else s->water_moved++;
                    fell = 1;
                    mark_dirty(s, y + 1);
                }
                if (!fell) {
                    int d = (sr_next(s) & 1) ? 1 : -1;
                    for (int k = 0; k < 2 && !fell; k++) {
                        int nx = x + (k ? -d : d);
                        if (nx <= 0 || nx >= SW - 1) continue;
                        unsigned char nb = g[nrow + nx];
                        if (nb == C_EMPTY) {
                            g[nrow + nx] = c; g[row + x] = C_EMPTY;
                            if (c == C_SAND) s->sand_moved++; else s->water_moved++;
                            fell = 1;
                            mark_dirty(s, y + 1);
                        } else if (c == C_SAND && (nb == C_WATER || nb == C_OIL)) {
                            g[nrow + nx] = C_SAND; g[row + x] = nb;
                            s->sand_moved++; fell = 1;
                            mark_dirty(s, y + 1);
                        }
                    }
                }
                if (fell) { mark_dirty(s, y); continue; }
            }
            if (c == C_WATER || c == C_OIL) {
                int d = (sr_next(s) & 1) ? 1 : -1;
                for (int k = 0; k < 2; k++) {
                    int dir = k ? -d : d;
                    int moved = 0;
                    for (int sp = 1; sp <= 4; sp++) {
                        int nx = x + dir * sp;
                        if (nx <= 0 || nx >= SW - 1) break;
                        if (g[row + nx] != C_EMPTY) break;
                        moved = sp;
                    }
                    if (moved > 0 && (sr_next(s) % (unsigned)moved) == 0) {
                        int nx = x + dir * moved;
                        g[row + nx] = c; g[row + x] = C_EMPTY;
                        s->water_moved++;
                        mark_dirty(s, y);
                        break;
                    }
                }
                continue;
            }
            if (c == C_FIRE) {
                /* douse if water adjacent */
                int doused = 0;
                if (x > 0 && g[row + x - 1] == C_WATER) doused = 1;
                if (x < SW - 1 && g[row + x + 1] == C_WATER) doused = 1;
                if (y + 1 < SH && g[nrow + x] == C_WATER) doused = 1;
                if (doused) { g[row + x] = C_EMPTY; mark_dirty(s, y); continue; }
                int life = (int)(sr_next(s) & 3);
                if (life == 0) { g[row + x] = C_EMPTY; mark_dirty(s, y); continue; }
                /* ignite oil above */
                if (y >= 1) {
                    int ux = x + ((sr_next(s) & 1) ? 1 : -1);
                    if (ux >= 1 && ux < SW - 1 &&
                        g[(y - 1) * SW + ux] == C_OIL && (sr_next(s) & 3) == 0) {
                        g[(y - 1) * SW + ux] = C_FIRE;
                        mark_dirty(s, y - 1);
                    }
                }
            }
        }
    }
    /* emitter: width 2*emit_w+1, rate 1/(1<<emit_shift), gated by emit_on */
    if (s->emit_mat != C_EMPTY && s->emit_on &&
        (s->frame & ((1 << s->emit_shift) - 1)) == 0) {
        int ex = s->emit_x, ey = s->emit_y;
        for (int dx = -s->emit_w; dx <= s->emit_w; dx++) {
            int nx = ex + dx;
            if (nx >= 1 && nx < SW - 1 && ey >= 0 && ey < SH - 1 &&
                g[ey * SW + nx] == C_EMPTY) {
                g[ey * SW + nx] = (unsigned char)s->emit_mat;
                mark_dirty(s, ey);
            }
        }
    }
}

/* FNV-1a over grid + rng — cross-platform probe hash */
static unsigned int sim_hash(SandSim *s) {
    unsigned int h = 2166136261u;
    unsigned char *p = (unsigned char *)s->g;
    int i, n = SW * SH;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= s->rng; h *= 16777619u;
    return h;
}

#endif /* SAND64_CORE_H */
