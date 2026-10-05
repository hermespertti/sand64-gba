/* host test: determinism + sanity + ASCII render for the core */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sand.h"

int main(int argc, char **argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 600;
    unsigned int seed = argc > 2 ? (unsigned)atoi(argv[2]) : 12345u;

    SandSim a, b;
    sim_init(&a, seed);
    sim_init(&b, seed);

    for (int f = 0; f < frames; f++) {
        sim_step(&a);
        /* vary emitter like GBA input would */
        a.emit_mat = (f < frames / 3) ? C_SAND : (f < 2 * frames / 3 ? C_WATER : C_SAND);
        sim_step(&b);
        b.emit_mat = a.emit_mat;
    }
    printf("hashA=%08x hashB=%08x %s\n", sim_hash(&a), sim_hash(&b),
           sim_hash(&a) == sim_hash(&b) ? "DETERMINISM IDENTICAL" : "MISMATCH!!");
    printf("moved sand=%lu water=%lu\n", a.sand_moved, a.water_moved);

    /* ASCII render */
    static const char *sym = ".#~o*f";
    for (int y = 0; y < SH; y += 2) {
        for (int x = 0; x < SW; x++)
            putchar(sym[a.g[y * SW + x] & 7]);
        putchar('\n');
    }
    return 0;
}
