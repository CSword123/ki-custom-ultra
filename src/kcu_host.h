/* The game library: the game code recompiled from the user's U98 on the
 * player's machine (data/gen), compiled there with the player's gcc (MinGW
 * on Windows) together with gameglue.c into data/gen/game.dll / game.so.
 *
 * The recompiled code calls ki1-port's runtime functions, which live in the
 * editor; a DLL cannot import from the program that loads it, so the editor
 * hands them over in this table and gameglue.c defines them as calls
 * through it. The library exports one function, kcu_game.
 */
#ifndef KCU_HOST_H
#define KCU_HOST_H

#include "ki1/rt.h"

#define KCU_HOST_VERSION 1

typedef struct {
    int version;            /* KCU_HOST_VERSION */
    uint32_t (*bad)(ki1_cpu *c, uint32_t pc, uint32_t word);
    uint32_t (*bad_entry)(ki1_cpu *c, uint32_t pc);
    uint32_t (*exception)(ki1_cpu *c, uint32_t pc, const char *what);
    uint32_t (*eret)(ki1_cpu *c);
    void (*tlbop)(ki1_cpu *c, int fn);
    void (*probe)(ki1_cpu *c, uint32_t pc);
    uint32_t (*hook)(ki1_cpu *c, uint32_t pc);
    uint64_t (*mfc0)(ki1_cpu *c, int reg);
    void (*mtc0)(ki1_cpu *c, int reg, uint64_t v);
    uint64_t (*io_read)(ki1_cpu *c, uint32_t addr, int size);
    void (*io_write)(ki1_cpu *c, uint32_t addr, uint64_t v, int size);
} kcu_host;

/* the library's entry table (ki1_entries) and its size, once it has the
 * host's functions; NULL if the table's version differs */
typedef const ki1_entry *(*kcu_game_fn)(const kcu_host *h, unsigned *count);
#define KCU_GAME "kcu_game"

#endif
