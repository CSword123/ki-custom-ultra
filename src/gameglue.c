/*
 * Compiled into the game library on the player's machine (kcu_host.h): the
 * runtime functions the recompiled code calls, as calls into the editor.
 */
#include "kcu_host.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

static const kcu_host *H;

uint32_t ki1_bad(ki1_cpu *c, uint32_t pc, uint32_t word) { return H->bad(c, pc, word); }
uint32_t ki1_bad_entry(ki1_cpu *c, uint32_t pc) { return H->bad_entry(c, pc); }
uint32_t ki1_exception(ki1_cpu *c, uint32_t pc, const char *what) { return H->exception(c, pc, what); }
uint32_t ki1_eret(ki1_cpu *c) { return H->eret(c); }
void ki1_tlbop(ki1_cpu *c, int fn) { H->tlbop(c, fn); }
void ki1_probe(ki1_cpu *c, uint32_t pc) { H->probe(c, pc); }
uint32_t ki1_hook(ki1_cpu *c, uint32_t pc) { return H->hook(c, pc); }
uint64_t ki1_mfc0(ki1_cpu *c, int reg) { return H->mfc0(c, reg); }
void ki1_mtc0(ki1_cpu *c, int reg, uint64_t v) { H->mtc0(c, reg, v); }
uint64_t ki1_io_read(ki1_cpu *c, uint32_t addr, int size) { return H->io_read(c, addr, size); }
void ki1_io_write(ki1_cpu *c, uint32_t addr, uint64_t v, int size) { H->io_write(c, addr, v, size); }

EXPORT const ki1_entry *kcu_game(const kcu_host *h, unsigned *count)
{
    if (!h || h->version != KCU_HOST_VERSION)
        return 0;
    H = h;
    *count = ki1_entry_count;
    return ki1_entries;
}
