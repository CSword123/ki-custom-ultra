/*
 * The game side of the editor: ki1-port's machine (rig.c), setup (setup.c)
 * and preview server (preview.c). libki1.a is the runtime alone: the game
 * segments come from the user's U98 (seg*.bin, ki1_builtin_segments stays
 * NULL), the game code is recompiled from them on the player's machine
 * (at setup: build/recomp/recomp.py into DIR/gen, compiled with the player's
 * gcc, MinGW on Windows, into the game library, kcu_host.h), which the
 * preview server loads; and the DCS sound program runs on ki1-port's
 * interpreter from the user's sound ROMs (ki1_dcs_code stays NULL).
 */
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game.h"
#include "kcu_host.h"
#include "rig.h"
#include "setup.h"

static int file_size_is(const char *dir, const char *file, Sint64 size)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    SDL_PathInfo info;
    return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE && (size < 0 || (Sint64)info.size == size);
}

const char *kcu_data_missing(const char *dir)
{
    if (!file_size_is(dir, "sound/dcs_rom.bin", -1))
        return "the sound ROMs (kinst.zip)";
    if (!file_size_is(dir, "hdd/kinst.img", -1))
        return "the hard disk (kinst.chd)";
    if (!file_size_is(dir, "ki-l15d.u98", 0x80000))
        return "the v1.5d program ROM (ki-l15d.u98 in kinst.zip)";
    if (!kcu_segments_ready(dir))
        return "the game segments (unpacked from ki-l15d.u98)";
    if (!file_size_is(dir, "gen/entries.c", -1))
        return "the game code (recompiled from the segments)";
    if (!file_size_is(dir, KCU_GAME_LIB, -1))
        return "the game library (the game code, compiled)";
    return "";
}

int kcu_segments_ready(const char *dir)
{
    for (int i = 0; i < KI1_NSEGMENTS; i++)
        if (!file_size_is(dir, ki1_segments[i].file, ki1_segments[i].size))
            return 0;
    return 1;
}

int kcu_roms_needed(const char *dir)
{
    return !file_size_is(dir, "sound/dcs_rom.bin", -1) || !file_size_is(dir, "hdd/kinst.img", -1) ||
           !file_size_is(dir, "ki-l15d.u98", 0x80000);
}

int kcu_data_ready(const char *dir)
{
    return !*kcu_data_missing(dir);
}

const char *kcu_setup(const char *dir, const char *zip, const char *chd)
{
    int sound = file_size_is(dir, "sound/dcs_rom.bin", -1), disk = file_size_is(dir, "hdd/kinst.img", -1);
    int u98 = file_size_is(dir, "ki-l15d.u98", 0x80000);
    if (!zip && (!sound || !u98))
        return "kinst.zip is missing";
    if (!chd && !disk)
        return "kinst.chd is missing";
    const char *e = ki1_setup_data(dir, sound ? NULL : zip, disk ? NULL : chd);
    if (!e && !u98)
        e = ki1_setup_u98(dir, zip);
    return e;
}

/* the game library DIR/KCU_GAME_LIB, given the runtime functions it calls */
static int load_code(const char *dir)
{
    static const kcu_host host = {KCU_HOST_VERSION, ki1_bad, ki1_bad_entry, ki1_exception, ki1_eret,
                                  ki1_tlbop, ki1_probe, ki1_hook, ki1_mfc0, ki1_mtc0, ki1_io_read,
                                  ki1_io_write};
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, KCU_GAME_LIB);
    SDL_SharedObject *lib = SDL_LoadObject(p);      /* kept: the code lives in it */
    kcu_game_fn game = lib ? (kcu_game_fn)SDL_LoadFunction(lib, KCU_GAME) : NULL;
    unsigned n = 0;
    const ki1_entry *e = game ? game(&host, &n) : NULL;
    if (!e) {
        fprintf(stderr, "ki-custom-ultra: cannot load the game code from %s: %s\n", p,
                !lib || !game ? SDL_GetError() : "made for another version (set up again)");
        return 1;
    }
    ki1_code = e, ki1_code_count = n;
    return 0;
}

int kcu_preview_server(const char *dir)
{
    if (!kcu_data_ready(dir)) {
        fprintf(stderr, "ki-custom-ultra: no game data in %s (%s missing)\n", dir, kcu_data_missing(dir));
        return 1;
    }
    if (load_code(dir))
        return 1;
    /* as ki1-port --preview-server: a clean disk, nothing kept */
    ki1_rig r = {0};
    ki1_rig_paths(&r, dir);
    int rc = ki1_rig_build(&r) ? 1 : ki1_preview_server(r.m);
    ki1_rig_free(&r);
    ki1_rig_paths_free(&r);
    return rc;
}
