/* sand64-gba core — pure C, host/GBA shared. Deterministic contract same
 * as sand64 N64: fixed terrain, xorshift RNG, deterministic scan order,
 * identical probe lines across platforms. */
#ifndef SAND64_CORE_H
#define SAND64_CORE_H

#define SW 120
#define SH 80

enum { C_EMPTY = 0, C_SAND = 1, C_WALL = 2, C_WATER = 3, C_OIL = 4, C_FIRE = 5 };

typedef struct {
    unsigned char g[SW * SH];
    unsigned int rng;
    int frame;
    int emit_x, emit_y;
    int emit_mat;
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
    s->sand_moved = s->water_moved = 0;
    for (i = 0; i < SW * SH; i++) s->g[i] = C_EMPTY;
    /* floor + side walls + a central pyramid (fixed terrain) */
    for (x = 0; x < SW; x++) {
        s->g[(SH - 1) * SW + x] = C_WALL;
        s->g[(SH - 2) * SW + x] = C_WALL;
    }
    for (y = 0; y < SH; y++) {
        s->g[y * SW + 0] = C_WALL;
        s->g[y * SW + SW - 1] = C_WALL;
    }
    for (x = SW / 2 - 14; x <= SW / 2 + 14; x++) {
        int h = 14 - ((x > SW / 2) ? (x - SW / 2) : (SW / 2 - x)) / 2;
        for (y = SH - 3 - h; y < SH - 2; y++)
            s->g[y * SW + x] = C_WALL;
    }
}

static inline unsigned char *cell(SandSim *s, int x, int y) { return &s->g[y * SW + x]; }

/* one physics step: materials fall/flow. Scan order flips each frame for
 * symmetry-breaking parity, matching sand64's phase scheme. */
static void sim_step(SandSim *s) {
    int x, y;
    unsigned char *g = s->g;
    s->frame++;

    /* bottom-up so falling lands in one pass */
    int rev = (s->frame & 1);   /* horizontal scan direction */
    for (y = SH - 2; y >= 1; y--) {
        for (int xi = 0; xi < SW; xi++) {
            x = rev ? (SW - 1 - xi) : xi;
            unsigned char c = g[y * SW + x];
            if (c == C_EMPTY || c == C_WALL) continue;

            int fell = 0;
            if (c == C_SAND || c == C_OIL || c == C_WATER) {
                int below = (y + 1) * SW + x;
                unsigned char b = g[below];
                /* swap with lighter liquid below */
                if (c == C_SAND && (b == C_WATER || b == C_OIL)) {
                    g[below] = C_SAND; g[y * SW + x] = b; s->sand_moved++; fell = 1;
                } else if (c == C_WATER && b == C_OIL) {   /* water sinks */
                    g[below] = C_WATER; g[y * SW + x] = C_OIL; s->water_moved++; fell = 1;
                } else if (b == C_EMPTY) {
                    g[below] = c; g[y * SW + x] = C_EMPTY;
                    if (c == C_SAND) s->sand_moved++; else s->water_moved++;
                    fell = 1;
                }
                if (!fell) {
                    /* diagonal slide */
                    int d = (sr_next(s) & 1) ? 1 : -1;
                    for (int k = 0; k < 2 && !fell; k++) {
                        int nx = x + (k ? -d : d);
                        if (nx <= 0 || nx >= SW - 1) continue;
                        unsigned char nb = g[(y + 1) * SW + nx];
                        if (nb == C_EMPTY) {
                            g[(y + 1) * SW + nx] = c; g[y * SW + x] = C_EMPTY;
                            if (c == C_SAND) s->sand_moved++; else s->water_moved++;
                            fell = 1;
                        } else if (c == C_SAND && (nb == C_WATER || nb == C_OIL)) {
                            g[(y + 1) * SW + nx] = C_SAND; g[y * SW + x] = nb;
                            s->sand_moved++; fell = 1;
                        }
                    }
                }
                if (fell) continue;
            }
            if (c == C_WATER || c == C_OIL) {
                /* lateral spread up to 3 cells, deterministic dir */
                int d = (sr_next(s) & 1) ? 1 : -1;
                for (int k = 0; k < 2; k++) {
                    int dir = k ? -d : d;
                    int moved = 0;
                    for (int sp = 1; sp <= 3; sp++) {
                        int nx = x + dir * sp;
                        if (nx <= 0 || nx >= SW - 1) break;
                        unsigned char nb = g[y * SW + nx];
                        if (nb != C_EMPTY) break;
                        moved = sp;
                    }
                    if (moved > 0 && (sr_next(s) % (unsigned)moved) == 0) {
                        int nx = x + dir * (moved);
                        g[y * SW + nx] = c; g[y * SW + x] = C_EMPTY;
                        s->water_moved++;
                        break;
                    }
                }
                continue;
            }
            if (c == C_FIRE) {
                /* fire flickers up, dies fast, ignites oil */
                int life = (int)(sr_next(s) & 3);
                if (life == 0) { g[y * SW + x] = C_EMPTY; continue; }
                int ux = x + ((sr_next(s) & 1) ? 1 : -1);
                int uy = y - 1;
                if (uy >= 1 && ux >= 1 && ux < SW - 1 &&
                    g[uy * SW + ux] == C_OIL && (sr_next(s) & 3) == 0)
                    g[uy * SW + ux] = C_FIRE;
            }
        }
    }
    /* emitter feeds into grid top */
    if (s->emit_mat != C_EMPTY) {
        int ex = s->emit_x, ey = s->emit_y;
        if (ex >= 1 && ex < SW - 1 && ey >= 0 && ey < SH - 1 &&
            g[ey * SW + ex] == C_EMPTY)
            g[ey * SW + ex] = (unsigned char)s->emit_mat;
    }
}

/* FNV-1a over grid + rng — the cross-platform probe hash */
static unsigned int sim_hash(SandSim *s) {
    unsigned int h = 2166136261u;
    unsigned char *p = (unsigned char *)s->g;
    int i, n = SW * SH;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= s->rng; h *= 16777619u;
    return h;
}

#endif /* SAND64_CORE_H */
