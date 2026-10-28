// The game side of the editor (engine.c): ki1-port's runtime, linked in from
// ../ki1-port (libki1.a) with nothing made from the game in it. The game
// code is recompiled from the user's own MAME "kinst" files on the player's
// machine and compiled there with the player's gcc (MinGW on Windows) into
// the game library (kcu_host.h), which the preview server loads.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// the game library in the data folder
#ifdef _WIN32
#define KCU_GAME_LIB "gen/game.dll"
#else
#define KCU_GAME_LIB "gen/game.so"
#endif

// the data folder has everything the game needs: the disk image, the sound
// ROM, the stock U98, the segments unpacked from it, the game code
// recompiled from those and compiled into the game library
int kcu_data_ready(const char *dir);
// what is still missing from dir, for the setup screen ("" when ready)
const char *kcu_data_missing(const char *dir);
// something only the user's files give is missing (the rest, segments and
// recompiled code, is made from what is there)
int kcu_roms_needed(const char *dir);
// the segments unpacked from the U98 are there
int kcu_segments_ready(const char *dir);
// check and unpack the user's files into dir (only what is missing there):
// zip = kinst.zip (sound ROMs, U98), chd = kinst.chd. NULL on success,
// else the error. The caller then unpacks the segments from dir/ki-l15d.u98
// (build/py/u98pack.py), recompiles the game code from them
// (build/recomp/recomp.py, into dir/gen) and compiles it (gcc).
const char *kcu_setup(const char *dir, const char *zip, const char *chd);
// ki1-port's preview server on stdin/stdout over the game in dir, its code
// loaded from the game library
int kcu_preview_server(const char *dir);

#ifdef __cplusplus
}
#endif
