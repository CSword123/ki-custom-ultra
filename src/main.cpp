// ki-custom-ultra: editor for custom KI1 ultra combos.
//
// Runs on the user's own MAME "kinst" files only: kinst.zip and kinst.chd go
// in roms/, and are checked and unpacked once into the data folder (data/,
// or $KI1_DATA), with the game segments unpacked from their U98 and the
// move catalog (tools/catalog.py) made from those. Then it edits a project
// (the format tools/kiultra.py compiles), runs kiultra.py to build the patch
// for ki1-port (--patch) and the repacked U98 for MAME, and previews it in
// the game, which is linked in (src/engine.c, ki1-port's runtime): the
// program starts itself again as the preview server (--preview-server).
// Dear ImGui on SDL3.

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "nlohmann/json.hpp"
#include "engine.h"
#include "preview.h"

using json = nlohmann::json;

#ifndef KCU_VERSION
#define KCU_VERSION "dev"
#endif

static const char *ULTRAP = "build/custom.ultrap";
static const char *U98 = "build/ki-l15d-ultra.u98";
static const char *LOADED_ULTRAP = "build/loaded.ultrap";    // a loaded patch, for the preview
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
// where the user puts kinst.zip and kinst.chd (or kinst/kinst.chd)
static const char *ROMS = "roms";

// the Python that runs tools/kiultra.py: $KI_PYTHON, else python3 (python
// on Windows, where python3 is often missing)
static std::string python()
{
    const char *p = getenv("KI_PYTHON");
#ifdef _WIN32
    return p ? p : "python";
#else
    return p ? p : "python3";
#endif
}
// the unpacked game data: $KI1_DATA, else data/
static std::string data_dir()
{
    const char *env = getenv("KI1_DATA");
    return env && *env ? env : "data";
}
static std::string DATA, CATALOG;      // CATALOG: DATA/catalog.json

// this program, started again as the preview server: its folder (SDL's base
// path) and the name it was started under
static std::string self_path(const char *argv0)
{
    const char *name = argv0;
    for (const char *c = argv0; *c; c++)
        if (*c == '/' || *c == '\\')
            name = c + 1;
    const char *base = SDL_GetBasePath();
    return std::string(base ? base : "") + name;
}

struct Segment {
    bool stock = true;      // {"from": "ultra"} or {"from": "move"}
    int index = 0;          // stock segment
    int move = 0;           // move id
    int loops = 1;
    bool one_tick = true;   // "ticks": 1, as the stock ultras play them
    int record = -1;        // hit record for its hitboxes (-1: default, -2: the move's own)
    bool full = false;      // keep the follow-through after the last hit
    bool chain = false;     // loops chain as when the button is pressed again
    int swing = 0;          // start at this swing of the move (re-press target)
    bool whole = false;     // follow the move through its bank switches
    int fast = -1;          // every other frame (1/0; -1: unset, on for moves that morph)
};

struct Fighter {
    std::vector<Segment> phase1, finish;
    bool stock_finish = true;
    bool keep_standing = true;  // P2 stays up until the last hits (kiultra.py)
    bool custom = false;    // in the project at all
};

struct App {
    json cat;
    int fighter = 1;
    int opponent = 6;       // P2 in the preview (Fulgore, the game's default)
    std::map<int, Fighter> fighters;
    int cap = 32;
    int add_to = 0;         // 0 build-up, 1 finish
    char project[256] = "projects/custom.json";
    char filter[64] = "";
    std::string log, dump;
    std::string open_tab;       // --tab: palette tab to select at start

    // preview
    PreviewClient preview;
    SDL_Texture *tex = nullptr;
    SDL_AudioStream *audio = nullptr;
    uint64_t gen = 0;           // preview generation shown
    double pos = 0;             // playback position in frames
    int shown = -1;             // frame in the texture
    bool playing = true, loop = false, mute = false;
    int speed = 2;              // index into SPEEDS
    int fed = -1;               // last frame whose sound was queued

    // a patch made elsewhere: an .ultrap, or a patched U98 (or a zip with one)
    char patch[1024] = "";
    std::vector<int> patch_fighters;    // whose ultra it changes
    std::string patch_u98;              // "stock" / "modified" for a U98, else ""
    std::mutex picked_mu;               // the file dialogs answer on their own thread
    std::string picked, picked_project;
};

static const float SPEEDS[] = {0.25f, 0.5f, 1.0f, 2.0f};

static const json &F(App &a, int c) { return a.cat["fighters"][std::to_string(c)]; }

static const json *find_move(App &a, int c, int id)
{
    for (const json &m : F(a, c)["moves"])
        if (m["id"].get<int>() == id)
            return &m;
    return nullptr;
}

static int seg_hits(App &a, int c, const Segment &s)
{
    int h = 0;
    if (s.stock) {
        const json &u = F(a, c)["ultra"];
        if (s.index < (int)u.size())
            h = u[s.index]["hits"].get<int>();
    } else if (const json *m = find_move(a, c, s.move)) {
        if (s.whole && m->contains("whole_hits") && !s.chain && !s.swing)
            return (*m)["whole_hits"].get<int>() * s.loops;
        if (!m->contains("swings"))
            return m->value("seg_hits", 0) * s.loops;
        // swings as kiultra.swing_order plays them
        const json &sw = (*m)["swings"];
        int n = (int)sw.size(), cyc = (*m)["swing_cycle"].get<int>(), j = s.swing < n ? s.swing : 0;
        if (!s.chain)
            return (j ? sw[j]["hits"].get<int>() : m->value("seg_hits", 0)) * s.loops;
        for (int k = 0; k < s.loops; k++) {
            h += sw[j]["hits"].get<int>();
            j = j + 1 < n ? j + 1 : cyc;
        }
        return h;
    }
    return h * s.loops;
}

static int total_hits(App &a, int c)
{
    Fighter &f = a.fighters[c];
    int n = 0;
    for (auto &s : f.phase1)
        n += seg_hits(a, c, s);
    if (f.stock_finish)
        n += F(a, c)["stock_finish_hits"].get<int>();
    else
        for (auto &s : f.finish)
            n += seg_hits(a, c, s);
    return n;
}

static std::string move_label(const json &m)
{
    char buf[200];
    int h = (m["kind"] == "linker" || m["kind"] == "ender") && m.contains("whole_hits") ? m["whole_hits"].get<int>() : m.value("seg_hits", 0);
    snprintf(buf, sizeof buf, "%s%s   m%d, %d hit%s", m.value("label", std::string("?")).c_str(),
             m.contains("shadow") ? " [shadow]" : "", m["id"].get<int>(), h, h == 1 ? "" : "s");
    return buf;
}

/* "fast" defaults to on for moves that morph into the opponent (catalog
 * "morph"): their finish drops against slow opponents otherwise */
static void fast_default(App &a, int c, Segment &s)
{
    if (s.fast >= 0 || s.stock)
        return;
    const json *m = find_move(a, c, s.move);
    if (m && m->contains("morph"))
        s.fast = 1;             // other moves keep -1: no "fast" in the project
}

/* the piece crashes the game in an ultra (catalog "crashes": the swings
 * that did when every piece was previewed alone, tools/crashes.json) */
static bool crashes(const json &m, int swing)
{
    if (!m.contains("crashes"))
        return false;
    for (const json &w : m["crashes"])
        if (w.get<int>() == swing)
            return true;
    return false;
}

static bool crashes(App &a, int c, const Segment &s)
{
    const json *m = s.stock ? nullptr : find_move(a, c, s.move);
    return m && crashes(*m, s.swing);
}

// why a piece cannot be used: whom it crashed against
static std::string crash_tip(App &a, const json &m)
{
    std::string who;
    for (const json &o : m.value("crashes_against", json::array()))
        who += (who.empty() ? "" : ", ") + F(a, o.get<int>())["name"].get<std::string>();
    return "crashes the game when played in an ultra (the CPU takes an exception)\nagainst: " +
           (who.empty() ? std::string("?") : who) +
           "\n(found by previewing every piece against every opponent: tools/crashes.json)";
}

// the pieces of fighter c's ultra that crash the game, for the log
static std::string crash_pieces(App &a, int c);

static std::string swing_name(int i)
{
    return std::string(" swing ") + (char)('A' + i);
}

/* secondary swing i of a move worth offering: it has frames of its own */
static bool swing_option(const json &m, int i)
{
    return m.contains("swings") && i < (int)m["swings"].size() && !m["swings"][i]["frames"].is_null();
}

static std::string seg_label(App &a, int c, const Segment &s)
{
    char buf[200];
    if (s.stock) {
        const json &u = F(a, c)["ultra"][s.index];
        snprintf(buf, sizeof buf, "stock #%d  (bank %d%s)", s.index, u["bank"].get<int>(),
                 u["phase"].get<int>() == 2 ? ", finish" : "");
    } else {
        const json *m = find_move(a, c, s.move);
        snprintf(buf, sizeof buf, "%s%s%s  (m%d)", m ? m->value("label", std::string("?")).c_str() : "?",
                 s.swing > 0 ? swing_name(s.swing).c_str() : "", m && m->contains("shadow") ? " [shadow]" : "",
                 s.move);
    }
    return buf;
}

static void stock_fighter(App &a, int c)
{
    Fighter f;
    for (const json &u : F(a, c)["ultra"])
        if (u["phase"].get<int>() == 1) {
            Segment s;
            s.index = u["index"].get<int>();
            f.phase1.push_back(s);
        }
    a.fighters[c] = f;
}

// --- project file (tools/kiultra.py format) --------------------------------

static json seg_json(const Segment &s)
{
    json j;
    if (s.stock)
        j = {{"from", "ultra"}, {"index", s.index}};
    else {
        j = {{"from", "move"}, {"move", s.move}};
        if (s.one_tick)
            j["ticks"] = 1;
    }
    if (s.loops != 1)
        j["loops"] = s.loops;
    if (s.record >= 0)
        j["record"] = s.record;
    else if (s.record == -2 && !s.stock)
        j["record"] = nullptr;
    if (s.full)
        j["full"] = true;
    if (s.chain && !s.stock)
        j["chain"] = true;
    if (s.swing > 0 && !s.stock)
        j["swing"] = s.swing;
    if (s.whole && !s.stock)
        j["whole"] = true;
    if (s.fast >= 0 && !s.stock)
        j["fast"] = s.fast == 1;
    return j;
}

static Segment seg_from(const json &j)
{
    Segment s;
    s.stock = j.value("from", "ultra") == "ultra";
    s.index = j.value("index", 0);
    s.move = j.value("move", 0);
    s.loops = j.value("loops", 1);
    s.one_tick = j.contains("ticks") && !j["ticks"].is_null();
    s.record = j.contains("record") && j["record"].is_number() ? j["record"].get<int>()
             : j.contains("record") && j["record"].is_null() ? -2 : -1;
    s.full = j.value("full", false);
    s.chain = j.value("chain", false);
    s.swing = j.value("swing", 0);
    s.whole = j.value("whole", false);
    s.fast = j.contains("fast") ? (int)j.value("fast", false) : -1;
    return s;
}

static json project_json(App &a)
{
    json p = {{"version", 1}, {"hit_cap", a.cap}, {"fighters", json::object()}};
    for (auto &[c, f] : a.fighters) {
        if (!f.custom)
            continue;
        json jf = {{"phase1", json::array()}};
        if (!f.keep_standing)
            jf["keep_standing"] = false;
        for (auto &s : f.phase1)
            jf["phase1"].push_back(seg_json(s));
        if (f.stock_finish)
            jf["finish"] = "stock";
        else {
            jf["finish"] = json::array();
            for (auto &s : f.finish)
                jf["finish"].push_back(seg_json(s));
        }
        p["fighters"][std::to_string(c)] = jf;
    }
    return p;
}

static bool save_project(App &a)
{
    std::ofstream o(a.project);
    if (!o) {
        a.log = std::string("cannot write ") + a.project;
        return false;
    }
    o << project_json(a).dump(1) << "\n";
    return true;
}

static void load_project(App &a)
{
    std::ifstream i(a.project);
    if (!i) {
        a.log = std::string("cannot read ") + a.project;
        return;
    }
    json p = json::parse(i, nullptr, false);
    if (p.is_discarded()) {
        a.log = "not JSON";
        return;
    }
    a.cap = p.value("hit_cap", 32);
    for (int c = 1; c <= 10; c++)
        stock_fighter(a, c);
    for (auto &[k, jf] : p["fighters"].items()) {
        Fighter f;
        f.custom = true;
        for (auto &j : jf["phase1"])
            f.phase1.push_back(seg_from(j));
        f.stock_finish = jf["finish"].is_string();
        f.keep_standing = jf.value("keep_standing", true);
        if (!f.stock_finish)
            for (auto &j : jf["finish"])
                f.finish.push_back(seg_from(j));
        for (auto *v : {&f.phase1, &f.finish})
            for (auto &s : *v)
                fast_default(a, std::stoi(k), s);
        a.fighters[std::stoi(k)] = f;
    }
    a.log = std::string("loaded ") + a.project;
}

static void start_preview(App &a, const char *ultrap = ULTRAP);

// --- running the tools ------------------------------------------------------

static std::string run(const std::string &cmd)
{
    std::string out;
    FILE *p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p)
        return "cannot run: " + cmd;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0)
        out.append(buf, n);
    int rc = pclose(p);
    if (rc)
        out += "\n(exit " + std::to_string(rc) + ")";
    return out;
}

static std::string crash_pieces(App &a, int c)
{
    std::string out;
    const Fighter &f = a.fighters[c];
    for (const std::vector<Segment> *v : {&f.phase1, &f.finish})
        for (const Segment &s : *v)
            if ((v == &f.phase1 || !f.stock_finish) && crashes(a, c, s))
                out += "  " + F(a, c)["name"].get<std::string>() + ": " + seg_label(a, c, s) + "\n";
    return out;
}

static bool compile(App &a, bool u98)
{
    // pieces that crash the game: not built (the preview would only crash)
    std::string bad;
    for (auto &[c, f] : a.fighters)
        if (f.custom)
            bad += crash_pieces(a, c);
    if (!bad.empty()) {
        a.log = "not built: these pieces crash the game when played in an ultra; remove them:\n" + bad;
        return false;
    }
    if (!save_project(a))
        return false;
    std::string cmd = python() + " tools/kiultra.py build " + a.project + " --ultrap " + ULTRAP;
    if (u98)
        cmd += std::string(" --u98 ") + U98;
    a.log = run(cmd);
    a.dump = run(python() + " tools/kiultra.py dump " + a.project);
    return a.log.find("(exit") == std::string::npos;
}

// a patch made elsewhere (.ultrap, or a patched U98 or a zip with one, as for
// MAME): kiultra.py import checks it, says which ultras it changes and turns
// it into LOADED_ULTRAP; then the current fighter's (or the first changed
// one's) ultra is previewed with it
static bool load_patch(App &a)
{
    std::string p = a.patch, cmd = python() + " tools/kiultra.py import \"" + p + "\"";
    a.log = run(cmd + " --ultrap " + LOADED_ULTRAP);
    a.patch_fighters.clear();
    a.patch_u98 = a.log.find("ki-l15d.u98: stock") != std::string::npos      ? "stock"
                  : a.log.find("ki-l15d.u98: modified") != std::string::npos ? "modified"
                                                                              : "";
    if (a.log.find("(exit") != std::string::npos)
        return false;
    size_t at = a.log.find("fighters");
    if (at != std::string::npos) {
        std::string line = a.log.substr(at + 8, a.log.find('\n', at) - at - 8);
        for (char *t = strtok(line.data(), " "); t; t = strtok(nullptr, " "))
            a.patch_fighters.push_back(atoi(t));
    }
    a.dump = run(cmd + " --dump");
    if (a.patch_fighters.empty())
        return false;
    if (std::find(a.patch_fighters.begin(), a.patch_fighters.end(), a.fighter) == a.patch_fighters.end())
        a.fighter = a.patch_fighters[0];
    start_preview(a, LOADED_ULTRAP);
    return true;
}

static void SDLCALL project_picked(void *u, const char *const *files, int)
{
    App &a = *static_cast<App *>(u);
    if (files && files[0]) {
        std::lock_guard<std::mutex> l(a.picked_mu);
        a.picked_project = files[0];
    }
}

static void SDLCALL patch_picked(void *u, const char *const *files, int)
{
    App &a = *static_cast<App *>(u);
    if (files && files[0]) {
        std::lock_guard<std::mutex> l(a.picked_mu);
        a.picked = files[0];
    }
}

// --- game data --------------------------------------------------------------

static bool is_file(const std::string &p)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

static SDL_Time mtime(const std::string &p)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) ? info.modify_time : 0;
}

static std::string quote(const std::string &s) { return "\"" + s + "\""; }

static std::string roms_zip()
{
    std::string p = std::string(ROMS) + "/kinst.zip";
    return is_file(p) ? p : "";
}

static std::string roms_chd()
{
    for (const char *f : {"/kinst.chd", "/kinst/kinst.chd"})
        if (is_file(ROMS + std::string(f)))
            return ROMS + std::string(f);
    return "";
}

// the catalog is made from the user's seg1, again whenever the tools change
static bool catalog_stale()
{
    SDL_Time t = mtime(CATALOG);
    for (const char *f : {"tools/catalog.py", "tools/kiultra.py", "tools/movenames.py", "tools/crashes.json"})
        if (mtime(f) > t)
            return true;
    return !t;
}

// the game code is recompiled from the user's segments, again whenever the
// recompiler or its config change
static const char *RECOMP = "build/recomp";
static bool code_stale()
{
    SDL_Time t = mtime(DATA + "/gen/entries.c");
    for (const char *f : {"recomp.py", "functions.txt", "entries.txt", "hooks.txt", "probes.txt"})
        if (mtime(RECOMP + std::string("/") + f) > t)
            return true;
    return !t;
}

// The game library: the recompiled code and build/recomp/glue.c compiled
// with the player's C compiler, $KI_CC, else gcc (on Windows MinGW-w64's,
// with its bin folder on PATH), again whenever what it is made from changes.
static std::string compiler()
{
    const char *cc = getenv("KI_CC");
    return cc && *cc ? cc : "gcc";
}

static const char *COMPILER_HELP =
#ifdef _WIN32
    "The game code is compiled on this computer with gcc from MinGW-w64: install it (e.g. the "
    "UCRT build from winlibs.com, or MSYS2's mingw-w64-ucrt-x86_64-gcc) and put its bin folder "
    "on PATH, or set KI_CC to its gcc.exe; then start the editor again.";
#else
    "The game code is compiled on this computer with gcc: install it (e.g. the gcc package) or "
    "set KI_CC to another C compiler (clang works); then start the editor again.";
#endif

static bool library_stale()
{
    SDL_Time t = mtime(DATA + "/" + KCU_GAME_LIB);
    std::vector<std::string> from = {DATA + "/gen/entries.c", RECOMP + std::string("/glue.c")};
    for (const char *h : {"kcu_host.h", "ki1/rt.h", "ki1/cpu.h", "ki1/hw.h"})
        from.push_back(RECOMP + std::string("/include/") + h);
    for (const std::string &f : from)
        if (mtime(f) > t)
            return true;
    return !t;
}

// run the commands, `jobs` at a time; "" if they all succeeded, else what
// the failed ones printed
static std::string run_all(const std::vector<std::vector<std::string>> &cmds, int jobs)
{
    std::string errors;
    std::vector<std::pair<SDL_Process *, size_t>> running;
    size_t next = 0;
    while (next < cmds.size() || !running.empty()) {
        while (next < cmds.size() && (int)running.size() < jobs) {
            std::vector<const char *> argv;
            for (const std::string &a : cmds[next])
                argv.push_back(a.c_str());
            argv.push_back(nullptr);
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)argv.data());
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
            SDL_Process *p = SDL_CreateProcessWithProperties(props);
            SDL_DestroyProperties(props);
            if (!p)
                return "cannot run " + cmds[next][0] + ": " + SDL_GetError();
            running.push_back({p, next++});
        }
        auto [p, i] = running.front();
        running.erase(running.begin());
        size_t n = 0;
        int code = -1;
        char *out = (char *)SDL_ReadProcess(p, &n, &code);
        if (code) {
            std::string line;
            for (const std::string &a : cmds[i])
                line += a + " ";
            errors += line + "\n" + std::string(out ? out : "", n) + "(exit " + std::to_string(code) + ")\n";
        }
        SDL_free(out);
        SDL_DestroyProcess(p);
    }
    return errors;
}

static std::string compile_library()
{
    std::string cc = compiler(), gen = DATA + "/gen", obj = gen + "/obj";
    SDL_CreateDirectory(obj.c_str());
    std::vector<std::string> srcs = {RECOMP + std::string("/glue.c")};
    int n = 0;
    char **list = SDL_GlobDirectory(gen.c_str(), "*.c", 0, &n);
    for (int i = 0; list && i < n; i++)
        srcs.push_back(gen + "/" + list[i]);
    SDL_free(list);
    std::vector<std::vector<std::string>> cmds;
    std::vector<std::string> link = {cc, "-shared", "-o", DATA + "/" + KCU_GAME_LIB};
    for (const std::string &src : srcs) {
        std::string base = src.substr(src.find_last_of('/') + 1);
        std::string o = obj + "/" + base.substr(0, base.size() - 2) + ".o";
        // ki1-port's flags for its generated code (its Makefile, GEN_CFLAGS)
        std::vector<std::string> c = {cc, "-O1", "-g0", "-std=gnu11", "-fwrapv", "-ffp-contract=off",
                                      "-fno-strict-aliasing", "-w", "-I" + std::string(RECOMP) + "/include"};
#ifndef _WIN32
        c.insert(c.end(), {"-fPIC", "-fvisibility=hidden"});
#endif
        c.insert(c.end(), {"-c", src, "-o", o});
        cmds.push_back(c);
        link.push_back(o);
    }
#ifdef _WIN32
    link.push_back("-static-libgcc");
#else
    link.push_back("-lm");
#endif
    std::string e = run_all({{cc, "--version"}}, 1);
    if (!e.empty())
        return "no C compiler (" + cc + "): " + e + "\n" + COMPILER_HELP;
    remove((DATA + "/" + KCU_GAME_LIB).c_str());
    e = run_all(cmds, std::max(1, SDL_GetNumLogicalCPUCores()));
    if (e.empty())
        e = run_all({link}, 1);
    if (!e.empty())
        return "cannot compile the game code with " + cc + ":\n" + e;
    return "";
}

// unpack what the data folder is missing from roms/, the segments from the
// U98, recompile the game code from them and compile it, and make the
// catalog from seg1; "" or the error
static std::string prepare_data()
{
    if (kcu_roms_needed(DATA.c_str())) {
        std::string zip = roms_zip(), chd = roms_chd();
        if (const char *e = kcu_setup(DATA.c_str(), zip.empty() ? nullptr : zip.c_str(),
                                      chd.empty() ? nullptr : chd.c_str()))
            return e;
    }
    if (!kcu_segments_ready(DATA.c_str())) {
        std::string out = run(python() + " build/py/u98pack.py unpack " + quote(DATA + "/ki-l15d.u98") + " " +
                              quote(DATA));
        if (!kcu_segments_ready(DATA.c_str()))
            return "cannot unpack the game segments from ki-l15d.u98:\n" + out;
    }
    if (code_stale()) {
        std::string r = RECOMP;
        std::string out = run(python() + " " + r + "/recomp.py " + quote(DATA + "/seg0.bin") + " " + r +
                              "/functions.txt " + quote(DATA + "/gen") + " --data " +
                              quote(DATA + "/seg1.bin@0x88033900") + " --data " + quote(DATA + "/seg2.bin@0x88033880") +
                              " --entries " + r + "/entries.txt --hooks " + r + "/hooks.txt --probes " + r +
                              "/probes.txt");
        if (out.find("(exit") != std::string::npos || code_stale())
            return "cannot recompile the game code (build/recomp/recomp.py):\n" + out;
    }
    if (library_stale())
        if (std::string e = compile_library(); !e.empty())
            return e;
    if (!kcu_data_ready(DATA.c_str()))
        return std::string("still missing: ") + kcu_data_missing(DATA.c_str());
    if (catalog_stale()) {
        std::string out = run(python() + " tools/catalog.py --seg1 " + quote(DATA + "/seg1.bin") + " -o " +
                              quote(CATALOG));
        if (out.find("(exit") != std::string::npos || !is_file(CATALOG))
            return "cannot make the move catalog (tools/catalog.py):\n" + out;
    }
    return "";
}

// Until the data folder is ready: ask for kinst.zip and kinst.chd in roms/,
// then check and unpack them. False if the window was closed (or, with
// shot, once the screen is saved there).
static bool setup_screen(SDL_Window *win, SDL_Renderer *ren, const char *shot)
{
    SDL_CreateDirectory(ROMS);
    char *cwd = SDL_GetCurrentDirectory();
    std::string folder = std::string(cwd ? cwd : "") + ROMS;
    SDL_free(cwd);
    std::thread worker;
    std::atomic<bool> working{false};
    std::string error;
    bool done = false, quit = false;
    // nothing to ask for (only what is made from the files there): go on
    bool go = !kcu_roms_needed(DATA.c_str());
    for (int frame = 0; !done && !quit; frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win)))
                quit = true;
        }
        if (go && !working) {
            go = false;
            working = true;
            if (worker.joinable())
                worker.join();
            worker = std::thread([&] {
                error = prepare_data();
                working = false;
            });
        }
        if (!working && worker.joinable()) {
            worker.join();
            done = error.empty();
        }
        std::string zip = roms_zip(), chd = roms_chd();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("game files", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::SeparatorText("Game files");
        ImGui::TextWrapped("ki-custom-ultra runs on your own copy of Killer Instinct: the MAME set \"kinst\" "
                           "(v1.5d). Put these two files in this folder:");
        ImGui::Spacing();
        ImGui::Indent();
        ImGui::TextUnformatted(folder.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("open folder"))
            SDL_OpenURL(("file://" + folder).c_str());
        ImGui::Spacing();
        ImVec4 ok(0.4f, 0.85f, 0.45f, 1), no(0.9f, 0.4f, 0.35f, 1);
        ImGui::TextColored(zip.empty() ? no : ok, "%s kinst.zip", zip.empty() ? "[missing]" : "[found]  ");
        ImGui::SameLine();
        ImGui::TextDisabled("the ROMs: ki-l15d.u98 and the sound ROMs u10-l1 ... u36-l1");
        ImGui::TextColored(chd.empty() ? no : ok, "%s kinst.chd", chd.empty() ? "[missing]" : "[found]  ");
        ImGui::SameLine();
        ImGui::TextDisabled("the hard disk (also found as kinst/kinst.chd)");
        ImGui::Unindent();
        ImGui::Spacing();
        ImGui::TextWrapped("Every file is checked against the original dumps, then unpacked once into %s "
                           "(about 140 MB), and the game code is recompiled from them and compiled on "
                           "this computer (with gcc%s). The editor does not work without them.",
                           DATA.c_str(),
#ifdef _WIN32
                           " from MinGW-w64"
#else
                           ""
#endif
        );
        ImGui::Spacing();
        ImGui::BeginDisabled(working || zip.empty() || chd.empty());
        if (ImGui::Button("check and unpack"))
            go = true, error.clear();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("quit"))
            quit = true;
        if (working)
            ImGui::TextUnformatted("checking, unpacking and compiling (under a minute)...");
        else if (!error.empty()) {
            ImGui::TextColored(no, "%s", error.c_str());
            if (error.find("ki-l15d.u98 is not the stock") != std::string::npos)
                ImGui::TextWrapped("This kinst.zip has a modified U98 (a custom ultra built for MAME, say). The "
                                   "editor needs the original one to start from; once it runs, the modified "
                                   "U98 can be previewed with \"load patch\".");
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 20, 20, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (shot && frame == 3 && !working) {
            if (SDL_Surface *img = SDL_RenderReadPixels(ren, nullptr)) {
                SDL_SaveBMP(img, shot);
                SDL_DestroySurface(img);
            }
            quit = true;
        }
        SDL_RenderPresent(ren);
    }
    if (worker.joinable())
        worker.join();
    return done;
}

// --- UI ---------------------------------------------------------------------

/* hit record for a segment: default (stock pieces keep theirs, moves get
 * the stock ultra's first) or one of the stock ultra's records */
static void record_combo(App &a, int c, Segment &s)
{
    char cur[32] = "default";
    if (s.record == -2)
        snprintf(cur, sizeof cur, "own");
    const json &recs = F(a, c)["records"];
    for (const json &r : recs)
        if (r["id"].get<int>() == s.record)
            snprintf(cur, sizeof cur, "%02X %s", s.record, r["tag"].get<std::string>().c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##rec", cur)) {
        if (ImGui::Selectable("default", s.record == -1))
            s.record = -1;
        if (!s.stock && ImGui::Selectable("own", s.record == -2))
            s.record = -2;
        for (const json &r : recs) {
            char l[32];
            snprintf(l, sizeof l, "%02X %s", r["id"].get<int>(), r["tag"].get<std::string>().c_str());
            if (ImGui::Selectable(l, s.record == r["id"].get<int>()))
                s.record = r["id"].get<int>();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("hit record for this segment's hits: light / heavy (big reaction) / lifts (launches P2)\n"
                          "default: the stock ultra's first; own: the move's records, as the game plays it\n"
                          "(keep P2 standing still swaps launching ones unless this piece ends the ultra)");
}

static void segment_list(App &a, int c, std::vector<Segment> &v, const char *id)
{
    ImGui::PushID(id);
    int del = -1, up = -1;
    if (ImGui::BeginTable("segs", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("segment", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("hits", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("record", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("loops", ImGuiTableColumnFlags_WidthFixed, 72);
        ImGui::TableSetupColumn("1 tick", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("chain", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("full", ImGuiTableColumnFlags_WidthFixed, 35);
        ImGui::TableSetupColumn("banks", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("2x", ImGuiTableColumnFlags_WidthFixed, 26);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90);
        // the headers, with "clear" (all pieces of this list) over up / dn / x
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int col = 0; col < 9; col++) {
            ImGui::TableSetColumnIndex(col);
            ImGui::TableHeader(ImGui::TableGetColumnName(col));
        }
        ImGui::TableSetColumnIndex(9);
        ImGui::BeginDisabled(v.empty());
        if (ImGui::SmallButton("clear"))
            ImGui::OpenPopup("clear?");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("remove all the pieces of this list");
        if (ImGui::BeginPopup("clear?")) {
            ImGui::Text("Remove all %d piece%s?", (int)v.size(), v.size() == 1 ? "" : "s");
            if (ImGui::Button("remove")) {
                v.clear();
                a.fighters[c].custom = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        for (int i = 0; i < (int)v.size(); i++) {
            Segment &s = v[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (const json *m = s.stock ? nullptr : find_move(a, c, s.move); m && m->contains("swings")) {
                ImGui::SetNextItemWidth(22);
                if (ImGui::BeginCombo("##swing", std::string(1, (char)('A' + s.swing)).c_str(),
                                      ImGuiComboFlags_NoArrowButton)) {
                    for (int i = 0; i < (int)(*m)["swings"].size(); i++)
                        if ((i == 0 || swing_option(*m, i)) &&
                            ImGui::Selectable(swing_name(i).c_str() + 1, s.swing == i))
                            s.swing = i;
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("swing the piece starts at (B, C: what pressing the button again jumps to)");
                ImGui::SameLine();
            }
            std::string label = seg_label(a, c, s);
            if (crashes(a, c, s)) {
                ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s  [crashes]", label.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%s\nRemove it to preview or build.", label.c_str(),
                                      crash_tip(a, *find_move(a, c, s.move)).c_str());
            } else {
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", label.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", seg_hits(a, c, s));
            ImGui::TableNextColumn();
            record_combo(a, c, s);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##loops", &s.loops))
                s.loops = s.loops < 1 ? 1 : s.loops > 8 ? 8 : s.loops;
            ImGui::TableNextColumn();
            if (!s.stock)
                ImGui::Checkbox("##tick", &s.one_tick);
            ImGui::TableNextColumn();
            if (!s.stock) {
                ImGui::Checkbox("##chain", &s.chain);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("loops play as when the button is pressed again: each loop jumps to the\n"
                                      "move's next swing instead of recovering and restarting (Spinal's B+HP\n"
                                      "alternates two swings, as in his stock ultra). Moves without a re-press\n"
                                      "window loop as before.");
            }
            ImGui::TableNextColumn();
            ImGui::Checkbox("##full", &s.full);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("play the whole animation: keep the follow-through after the last hit\n"
                                  "(with \"keep P2 standing\", a piece followed by another one stops at its last hit)");
            ImGui::TableNextColumn();
            if (const json *m = s.stock ? nullptr : find_move(a, c, s.move); m && m->contains("more_banks")) {
                ImGui::Checkbox("##whole", &s.whole);
                if (ImGui::IsItemHovered()) {
                    std::string b;
                    for (const json &x : (*m)["more_banks"])
                        b += " " + std::to_string(x.get<int>());
                    ImGui::SetTooltip("all banks: follow the move through its animation bank switches (banks%s,\n"
                                      "%d hits) instead of stopping at the first; linkers play this way",
                                      b.c_str(), (*m)["whole_hits"].get<int>());
                }
            }
            ImGui::TableNextColumn();
            if (const json *m = s.stock ? nullptr : find_move(a, c, s.move); m && m->contains("morph")) {
                fast_default(a, c, s);
                bool on = s.fast == 1;
                if (ImGui::Checkbox("##fast", &on))
                    s.fast = on ? 1 : 0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("2x: play every other frame of this piece.\n"
                                      "It morphs into the opponent and plays their move, which must hit before\n"
                                      "they recover (~27 frames after the last hit; an ender started by its\n"
                                      "command freezes them, inside an ultra nothing does). At full length the\n"
                                      "teleport is too slow against e.g. Jago and Combo. Check with \"vs\".");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("up") && i > 0)
                up = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("dn") && i + 1 < (int)v.size())
                up = i + 1;
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                del = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (up > 0)
        std::swap(v[up], v[up - 1]);
    if (del >= 0)
        v.erase(v.begin() + del);
    ImGui::PopID();
}

static void add_segment(App &a, Segment s)
{
    fast_default(a, a.fighter, s);
    Fighter &f = a.fighters[a.fighter];
    f.custom = true;
    if (a.add_to == 1) {
        if (f.stock_finish) {
            f.stock_finish = false;
            f.finish.clear();
        }
        f.finish.push_back(s);
    } else {
        f.phase1.push_back(s);
    }
}

static void palette(App &a)
{
    int c = a.fighter;
    ImGui::RadioButton("add to build-up", &a.add_to, 0);
    ImGui::SameLine();
    ImGui::RadioButton("add to finish", &a.add_to, 1);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter (bank, move, input)", a.filter, sizeof a.filter);
    if (!ImGui::BeginTabBar("palette"))
        return;
    if (ImGui::BeginTabItem("Stock", nullptr, a.open_tab == "stock" ? ImGuiTabItemFlags_SetSelected : 0)) {
        for (const json &u : F(a, c)["ultra"]) {
            Segment s;
            s.index = u["index"].get<int>();
            std::string l = seg_label(a, c, s) + "  " + std::to_string(u["hits"].get<int>()) + " hits";
            if (ImGui::Selectable(l.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(0))
                add_segment(a, s);
        }
        ImGui::EndTabItem();
    }
    // Openers: what starts a combo (the moves an auto-double may follow), also
    // listed under Normals / Specials
    for (const char *kind : {"opener", "normal", "special", "linker", "ender", "other"}) {
        std::string tab = std::string(kind) + "s";
        tab[0] = (char)toupper(tab[0]);
        if (!ImGui::BeginTabItem(tab.c_str(), nullptr,
                                 a.open_tab == std::string(kind) + "s" ? ImGuiTabItemFlags_SetSelected : 0))
            continue;
        if (!strcmp(kind, "opener") && ImGui::IsItemHovered())
            ImGui::SetTooltip("moves an auto-double may follow (the game's prev-move condition names them):\n"
                              "the specials, jump-ins and command normals that start a combo");
        ImGui::BeginChild("moves");
        std::vector<const json *> list;
        for (const json &m : F(a, c)["moves"])
            if ((!strcmp(kind, "opener") ? m.value("opener", false) : m["kind"] == kind) && m.contains("seg") &&
                !m["seg"].empty())
                list.push_back(&m);
        std::sort(list.begin(), list.end(), [](const json *x, const json *y) { return (*x)["order"] < (*y)["order"]; });
        std::string group;
        for (const json *mp : list) {
            const json &m = *mp;
            std::string l = move_label(m);
            if (a.filter[0] && l.find(a.filter) == std::string::npos)
                continue;
            std::string g = m.value("group", std::string("other"));
            if (g != group)
                ImGui::SeparatorText(g.c_str());
            group = g;
            bool crash = crashes(m, 0);
            if (crash)
                l += "  [crashes]";
            if (ImGui::Selectable(l.c_str(), false,
                                  crash ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_AllowDoubleClick) &&
                !crash && ImGui::IsMouseDoubleClicked(0)) {
                Segment s;
                s.stock = false;
                s.move = m["id"].get<int>();
                if (m["kind"] == "linker" || m["kind"] == "ender") {
                    // as the game plays them: all their banks, their own hits
                    s.whole = m.contains("more_banks");
                    s.record = -2;
                }
                add_segment(a, s);
            }
            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            for (int i = 1; m.contains("swings") && i < (int)m["swings"].size(); i++) {
                if (!swing_option(m, i))
                    continue;
                const json &w = m["swings"][i];
                int h = w["hits"].get<int>();
                bool crash2 = crashes(m, i);
                char l2[160];
                snprintf(l2, sizeof l2, "    +%s  frames %d-%d, %d hit%s%s##m%ds%d", swing_name(i).c_str(),
                         w["frames"][0].get<int>(), w["frames"][1].get<int>(), h, h == 1 ? "" : "s",
                         crash2 ? "  [crashes]" : "", m["id"].get<int>(), i);
                if (ImGui::Selectable(l2, false,
                                      crash2 ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_AllowDoubleClick) &&
                    !crash2 && ImGui::IsMouseDoubleClicked(0)) {
                    Segment s;
                    s.stock = false;
                    s.move = m["id"].get<int>();
                    s.swing = i;
                    add_segment(a, s);
                }
                if (crash2 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", crash_tip(a, m).c_str());
                else if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("the animation a re-press of this move jumps to (op 10)\n"
                                      "with \"chain\", loops continue through the move's swings from here");
            }
            if (hovered) {
                std::string tip = crash ? crash_tip(a, m) + "\n\n" : "";
                if (m.contains("shadow"))
                    tip += m["shadow"] == "skulls"
                               ? "[shadow] leaves an after-image trail. In the game only when Spinal, morphed into this\n"
                                 "fighter by a Skeleport ender, has skulls; in a custom ultra it always trails.\n\n"
                               : "[shadow] always leaves an after-image trail.\n\n";
                for (const json &x : m["labels"])
                    tip += x.get<std::string>() + "\n";
                for (const json &x : m["seg"])
                    tip += x.contains("f") ? std::to_string(x["f"].get<int>()) + "x" + std::to_string(x["t"].get<int>()) + " "
                           : x.contains("hit") ? "[HIT] " : "op" + std::to_string(x["op"].get<int>()) + " ";
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

// --- preview -----------------------------------------------------------------

static void start_preview(App &a, const char *ultrap)
{
    char path[4096];
#ifdef _WIN32
    bool full = _fullpath(path, ultrap, sizeof path) != nullptr;
#else
    bool full = realpath(ultrap, path) != nullptr;
#endif
    a.preview.request(a.fighter, full ? path : ultrap, 1500, a.opponent);
}

/* play position, texture and sound for this UI frame */
static void preview_update(App &a)
{
    size_t n = a.preview.frame_count();
    if (a.gen != a.preview.generation()) {
        a.gen = a.preview.generation();
        a.pos = 0, a.shown = -1, a.fed = -1, a.playing = true;
        if (a.audio)
            SDL_ClearAudioStream(a.audio);
    }
    if (!n)
        return;
    if (a.playing) {
        a.pos += ImGui::GetIO().DeltaTime * 58.9634 * SPEEDS[a.speed];
        if (a.pos >= (double)(n - 1)) {
            if (a.preview.streaming())
                a.pos = (double)(n - 1);
            else if (a.loop)
                a.pos = 0, a.fed = -1;
            else
                a.pos = (double)(n - 1), a.playing = false;
        }
    }
    int idx = (int)a.pos;
    PreviewFrame fr;
    if (idx != a.shown && a.preview.frame((size_t)idx, fr)) {
        SDL_UpdateTexture(a.tex, nullptr, fr.pixels.data(), 320 * 2);
        a.shown = idx;
    }
    // sound only when playing forward at 1x
    if (a.audio && a.playing && !a.mute && SPEEDS[a.speed] == 1.0f && idx > a.fed &&
        SDL_GetAudioStreamQueued(a.audio) < 31250 / 2) {
        for (int k = a.fed + 1; k <= idx; k++) {
            PreviewFrame s;
            if (!a.preview.frame((size_t)k, s) || !s.rate || s.samples.empty())
                continue;
            SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, (int)s.rate};
            SDL_SetAudioStreamFormat(a.audio, &spec, nullptr);
            SDL_PutAudioStreamData(a.audio, s.samples.data(), (int)(s.samples.size() * 2));
        }
    }
    a.fed = idx;
}

static void preview_panel(App &a)
{
    preview_update(a);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("vs");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo("##opp", F(a, a.opponent)["name"].get<std::string>().c_str())) {
        for (int c = 1; c <= 10; c++)
            if (ImGui::Selectable(F(a, c)["name"].get<std::string>().c_str(), c == a.opponent))
                a.opponent = c;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("P2 in the preview (the next preview uses it; first one per pair ~4 s).\n"
                          "Finishes that morph into the opponent (Spinal's Skeleport enders) play\n"
                          "that opponent's move, so check them against several.");
    std::string st = a.preview.status();
    ImGui::TextWrapped("%s", st.c_str());
    ImVec2 av = ImGui::GetContentRegionAvail();
    float iw = av.x, ih = iw * 0.75f;
    if (ih > av.y - 110)
        ih = av.y - 110, iw = ih / 0.75f;
    if (ih < 60)
        ih = 60, iw = 80;
    size_t n = a.preview.frame_count();
    if (n)
        ImGui::Image((ImTextureID)(intptr_t)a.tex, ImVec2(iw, ih));
    else
        ImGui::Dummy(ImVec2(iw, ih));

    // timeline: ultra phase per frame, ticks where the hit counter goes up;
    // below it, red where P2 is off the ground and where the combo drops
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float tw = iw, th = 14, lane = 6;
    dl->AddRectFilled(p0, ImVec2(p0.x + tw, p0.y + th + lane), IM_COL32(40, 40, 46, 255));
    int hits = 0, lifted_at = -1, lifted_hit = 0, dropped_at = -1, dropped_hit = 0, called_at = -1;
    int ended_at = -1, ended_hit = 0;
    if (n) {
        PreviewFrame fr;
        int last = -1;
        bool started = false;
        int reset_at = -1, reset_hit = 0;
        for (size_t i = 0; i < n; i++) {
            if (!a.preview.frame(i, fr))
                break;
            float x0 = p0.x + tw * i / n, x1 = p0.x + tw * (i + 1) / n;
            ImU32 col = fr.flag == 1 ? IM_COL32(60, 110, 200, 255) : fr.flag == 2 ? IM_COL32(220, 140, 40, 255)
                                                                                 : IM_COL32(70, 70, 76, 255);
            dl->AddRectFilled(ImVec2(x0, p0.y + 3), ImVec2(x1, p0.y + th - 3), col);
            // a hit raises the counter, or restarts it at 1 (the preview seeds
            // it at 2 and the game resets it when the first hit lands)
            if (last >= 0 && fr.combo != last && (fr.combo > last || fr.combo == 1)) {
                dl->AddLine(ImVec2(x0, p0.y), ImVec2(x0, p0.y + th), IM_COL32(255, 255, 255, 220));
                hits++;
                started = true;
            }
            if (fr.flag && (fr.p2state & 1)) {
                dl->AddRectFilled(ImVec2(x0, p0.y + th + 1), ImVec2(x1, p0.y + th + lane), IM_COL32(220, 60, 60, 255));
                if (lifted_at < 0)
                    lifted_at = (int)i, lifted_hit = hits;
            }
            // dropped: the counter went back to 0 and later hits still came
            // (a reset after the last hit is just the end of the combo)
            if (started && fr.flag && fr.combo == 0 && last > 0 && reset_at < 0)
                reset_at = ended_at = (int)i, reset_hit = ended_hit = hits;
            if (reset_at >= 0 && dropped_at < 0 && fr.combo > 0 && hits > reset_hit) {
                dropped_at = reset_at, dropped_hit = reset_hit;
                float xd = p0.x + tw * reset_at / n;
                dl->AddTriangleFilled(ImVec2(xd, p0.y + th), ImVec2(xd - 5, p0.y + th + lane + 4),
                                      ImVec2(xd + 5, p0.y + th + lane + 4), IM_COL32(255, 60, 60, 255));
            }
            // the announcer's "Ultra Combo!" (state bit 1)
            if ((fr.p2state & 2) && called_at < 0) {
                called_at = (int)i;
                dl->AddRectFilled(ImVec2(x0 - 2, p0.y - 3), ImVec2(x0 + 2, p0.y + th + lane + 3),
                                  IM_COL32(255, 210, 60, 255));
            }
            last = fr.combo;
        }
        float xp = p0.x + tw * (float)a.pos / n;
        dl->AddLine(ImVec2(xp, p0.y - 2), ImVec2(xp, p0.y + th + lane + 2), IM_COL32(255, 80, 80, 255), 2);
    }
    ImGui::Dummy(ImVec2(tw, th + lane + 4));

    // transport
    if (ImGui::Button(a.playing ? "pause" : "play")) {
        a.playing = !a.playing;
        if (a.playing && n && a.pos >= n - 1 && !a.preview.streaming())
            a.pos = 0;
        a.fed = (int)a.pos;
    }
    ImGui::SameLine();
    if (ImGui::Button("|<"))
        a.pos = 0, a.fed = -1;
    ImGui::SameLine();
    if (ImGui::Button("<") && a.pos >= 1)
        a.pos = (int)a.pos - 1, a.playing = false;
    ImGui::SameLine();
    if (ImGui::Button(">") && n && a.pos + 1 < n)
        a.pos = (int)a.pos + 1, a.playing = false;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    const char *sp[] = {"0.25x", "0.5x", "1x", "2x"};
    ImGui::Combo("##speed", &a.speed, sp, 4);
    ImGui::SameLine();
    ImGui::Checkbox("loop", &a.loop);
    ImGui::SameLine();
    ImGui::Checkbox("mute", &a.mute);
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.preview.streaming());
    if (ImGui::Button("stop"))
        a.preview.stop();
    ImGui::EndDisabled();
    int fi = (int)a.pos;
    ImGui::SetNextItemWidth(-1);
    if (n && ImGui::SliderInt("##frame", &fi, 0, (int)n - 1, "frame %d")) {
        a.pos = fi, a.playing = false;
    }
    PreviewFrame fr;
    if (n && a.preview.frame((size_t)a.pos, fr))
        ImGui::Text("ultra flag %d   bank %3d frame %3d   hits %d/%d   P2 %s", fr.flag, fr.bank, fr.anim,
                    fr.combo, hits, fr.p2state & 1 ? "in the air" : "on the ground");
    if (lifted_at >= 0 && lifted_hit < hits)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "P2 lifted at frame %d (after hit %d of %d)", lifted_at,
                           lifted_hit, hits);
    if (dropped_at >= 0)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "combo dropped at frame %d after %d hits", dropped_at,
                           dropped_hit);
    if (ended_at >= 0 && dropped_at < 0)
        ImGui::Text("combo ended at frame %d after %d hits", ended_at, ended_hit);
    if (called_at >= 0)
        ImGui::TextColored(ImVec4(1, 0.82f, 0.25f, 1), "\"Ultra Combo!\" at frame %d (gold mark)", called_at);
    else if (n && !a.preview.streaming())
        ImGui::TextDisabled("no \"Ultra Combo!\" call");
}

static void ui(App &a)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("ki-custom-ultra", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    // fighter
    ImGui::SetNextItemWidth(160);
    std::string cur = F(a, a.fighter)["name"].get<std::string>();
    if (ImGui::BeginCombo("fighter", cur.c_str())) {
        for (int c = 1; c <= 10; c++) {
            std::string n = F(a, c)["name"].get<std::string>() + (a.fighters[c].custom ? "  *" : "");
            if (ImGui::Selectable(n.c_str(), c == a.fighter))
                a.fighter = c;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("reset to stock"))
        stock_fighter(a, a.fighter);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("hit cap", &a.cap))
        a.cap = a.cap < 1 ? 1 : a.cap > 80 ? 80 : a.cap;

    int hits = total_hits(a, a.fighter);
    char ov[64];
    snprintf(ov, sizeof ov, "%d / %d hits", hits, a.cap);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hits > a.cap ? IM_COL32(200, 50, 50, 255) : IM_COL32(60, 160, 80, 255));
    ImGui::ProgressBar(hits > a.cap ? 1.0f : (float)hits / a.cap, ImVec2(-1, 0), ov);
    ImGui::PopStyleColor();

    float h = ImGui::GetContentRegionAvail().y - 130;
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("left", ImVec2(w * 0.22f, h), ImGuiChildFlags_Borders);
    palette(a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("middle", ImVec2(w * 0.46f, h), ImGuiChildFlags_Borders);
    Fighter &f = a.fighters[a.fighter];
    ImGui::SeparatorText("build-up (ultra flag 1)");
    ImGui::TextDisabled("prologue: stock setup check + op 74");
    size_t n1 = f.phase1.size();
    segment_list(a, a.fighter, f.phase1, "p1");
    ImGui::SeparatorText("finish (ultra flag 2)");
    bool sf = f.stock_finish;
    if (ImGui::Checkbox("stock finish", &f.stock_finish) && sf != f.stock_finish)
        f.custom = true;
    if (f.stock_finish)
        ImGui::TextDisabled("%d hits, then back to stance", F(a, a.fighter)["stock_finish_hits"].get<int>());
    else
        segment_list(a, a.fighter, f.finish, "fin");
    if (ImGui::Checkbox("keep P2 standing until the last hits", &f.keep_standing))
        f.custom = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pieces not in their stock order lose their follow-through after the last hit,\n"
                          "and their launching hit records and hold releases, so only the final\n"
                          "segment knocks P2 down. Stock order is left as designed.");
    if (f.phase1.size() != n1)
        f.custom = true;
    if (ImGui::CollapsingHeader("compiled script")) {
        ImGui::BeginChild("dump", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(a.dump.c_str());
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("preview", ImVec2(0, h), ImGuiChildFlags_Borders);
    preview_panel(a);
    ImGui::EndChild();

    // actions
    {
        std::lock_guard<std::mutex> l(a.picked_mu);
        if (!a.picked_project.empty()) {
            snprintf(a.project, sizeof a.project, "%s", a.picked_project.c_str());
            a.picked_project.clear();
            load_project(a);
        }
    }
    ImGui::SetNextItemWidth(260);
    ImGui::InputText("##project", a.project, sizeof a.project);
    ImGui::SameLine();
    if (ImGui::Button("browse...##project")) {
        static const SDL_DialogFileFilter filters[] = {{"Projects (.json)", "json"}, {"All files", "*"}};
        char *cwd = SDL_GetCurrentDirectory();
        static std::string where;
        where = std::string(cwd ? cwd : "") + "projects/";
        SDL_free(cwd);
        SDL_ShowOpenFileDialog(project_picked, &a, SDL_GetKeyboardFocus(), filters, 2, where.c_str(), false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("open a project (.json); save writes to the path in the box");
    ImGui::SameLine();
    if (ImGui::Button("save"))
        a.log = save_project(a) ? std::string("saved ") + a.project : a.log;
    ImGui::SameLine();
    if (ImGui::Button("load"))
        load_project(a);
    ImGui::SameLine();
    ImGui::BeginDisabled(hits > a.cap);
    if (ImGui::Button("compile"))
        compile(a, false);
    ImGui::SameLine();
    if (ImGui::Button("build U98 (MAME)"))
        compile(a, true);
    ImGui::SameLine();
    if (ImGui::Button("preview") && compile(a, false))
        start_preview(a);
    ImGui::SameLine();
    if (ImGui::Button("preview stock"))
        a.preview.request(a.fighter, "-", 1500, a.opponent);
    ImGui::EndDisabled();

    // a patch made elsewhere
    {
        std::lock_guard<std::mutex> l(a.picked_mu);
        if (!a.picked.empty()) {
            snprintf(a.patch, sizeof a.patch, "%s", a.picked.c_str());
            a.picked.clear();
            load_patch(a);
        }
    }
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##patch", ".ultrap, patched .u98 or kinst.zip", a.patch, sizeof a.patch);
    ImGui::SameLine();
    if (ImGui::Button("browse...")) {
        static const SDL_DialogFileFilter filters[] = {{"Ultra patches (.ultrap, .kip, .u98, .zip)", "ultrap;kip;u98;zip"},
                                                       {"All files", "*"}};
        SDL_ShowOpenFileDialog(patch_picked, &a, SDL_GetKeyboardFocus(), filters, 2, nullptr, false);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.patch[0]);
    if (ImGui::Button("load patch"))
        load_patch(a);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Preview a custom ultra made elsewhere: an .ultrap, or a U98 built for MAME\n"
                          "(or a zip with one). Its changed ultras are listed; the compiled script\n"
                          "shows what it plays. It is previewed as it is, not opened for editing.");
    if (!a.patch_u98.empty()) {
        ImGui::SameLine();
        if (a.patch_u98 == "stock")
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.45f, 1), "U98: stock v1.5d (checksum matches)");
        else
            ImGui::TextColored(ImVec4(1, 0.65f, 0.2f, 1), "U98: modified (checksum differs from stock)");
    }
    for (int c : a.patch_fighters) {
        ImGui::SameLine();
        std::string b = "preview " + F(a, c)["name"].get<std::string>() + "##patch" + std::to_string(c);
        if (ImGui::Button(b.c_str())) {
            a.fighter = c;
            start_preview(a, LOADED_ULTRAP);
        }
    }
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(a.log.c_str());
    ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("ki-custom-ultra %s\n", KCU_VERSION);
        return 0;
    }
    // the preview server (PreviewClient starts the program again with it)
    if (argc > 1 && !strcmp(argv[1], "--preview-server"))
        return kcu_preview_server(argc > 3 && !strcmp(argv[2], "--data") ? argv[3] : data_dir().c_str());
    DATA = data_dir();
    CATALOG = DATA + "/catalog.json";
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "KI1_DATA", DATA.c_str(), true);  // for tools/kiultra.py
#ifdef _WIN32
    _putenv_s("KI1_DATA", DATA.c_str());        // _popen takes the C runtime's environment
#else
    setenv("KI1_DATA", DATA.c_str(), 1);
#endif
    // --setup: unpack roms/ into the data folder without the GUI
    if (argc > 1 && !strcmp(argv[1], "--setup")) {
        std::string e = kcu_roms_needed(DATA.c_str()) && (roms_zip().empty() || roms_chd().empty())
                            ? std::string("put kinst.zip and kinst.chd in ") + ROMS + "/"
                            : prepare_data();
        fprintf(stderr, "ki-custom-ultra: %s\n", e.empty() ? ("game data ready in " + DATA).c_str() : e.c_str());
        return e.empty() ? 0 : 1;
    }
    App a;
    // --shot FILE.bmp [--project P] [--fighter N] [--preview-frame N]: render,
    // save a screenshot, exit (with --preview-frame: after previewing, at frame N)
    // --load FILE: load that patch at start (as "load patch"), and with --shot
    // preview it instead of the project
    const char *shot = nullptr;
    int preview_frame = -1;
    bool load = false;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--shot"))
            shot = argv[++i];
        else if (!strcmp(argv[i], "--project"))
            snprintf(a.project, sizeof a.project, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--tab"))
            a.open_tab = argv[++i];
        else if (!strcmp(argv[i], "--preview-frame"))
            preview_frame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fighter"))
            a.fighter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--opponent"))
            a.opponent = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--load"))       // a patch to preview (load patch)
            snprintf(a.patch, sizeof a.patch, "%s", argv[++i]), load = true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("KI custom ultra " KCU_VERSION, 1600, 900, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
    if (!ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = "build/imgui.ini";
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    // the user's game files, unpacked (--shot without them: the setup screen)
    bool ready;
    if (shot && !kcu_roms_needed(DATA.c_str())) {
        std::string e = prepare_data();
        if (!e.empty())
            fprintf(stderr, "ki-custom-ultra: %s\n", e.c_str());
        ready = e.empty();
    } else {
        ready = setup_screen(win, ren, shot);
    }
    {
        std::ifstream i(CATALOG);
        if (ready && !i)
            fprintf(stderr, "ki-custom-ultra: no %s\n", CATALOG.c_str());
        if (ready && i)
            a.cat = json::parse(i);
        else
            ready = false;
    }
    if (!ready) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return shot ? 1 : 0;    // without --shot: the setup screen was closed
    }
    for (int c = 1; c <= 10; c++)
        stock_fighter(a, c);
    if (argc > 1) {
        std::ifstream probe(a.project);
        if (probe)
            load_project(a);
    }

    a.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XBGR1555, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    SDL_SetTextureScaleMode(a.tex, SDL_SCALEMODE_NEAREST);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, 31250};
        a.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (a.audio)
            SDL_ResumeAudioStreamDevice(a.audio);
    }
    if (!a.preview.start(self_path(argv[0]), DATA, "build/preview.log"))
        a.log = "cannot start the preview server: " + a.preview.status();

    for (int frame = 0, done = 0; !done; frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win)))
                done = 1;
        }
        static int shot_at = 4;
        if (frame == 3)
            a.open_tab.clear();
        if (load && frame == 1 && load_patch(a) && shot && preview_frame >= 0)
            shot_at = 1 << 30;
        else if (!load && shot && frame == 1 && compile(a, false) && preview_frame >= 0) {
            start_preview(a);
            shot_at = 1 << 30;
        }
        if (shot && preview_frame >= 0 && shot_at == 1 << 30 && frame > 2 && !a.preview.streaming() &&
            a.preview.frame_count() > 0) {
            a.pos = preview_frame, a.playing = false;
            shot_at = frame + 3;
        }
        static Uint64 shot_start = SDL_GetTicks();
        if (shot && SDL_GetTicks() - shot_start > 120000 && shot_at > frame)    /* 2 minutes */
            shot_at = frame;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ui(a);
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 20, 20, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (shot && frame == shot_at) {
            SDL_Surface *img = SDL_RenderReadPixels(ren, nullptr);
            if (img) {
                SDL_SaveBMP(img, shot);
                SDL_DestroySurface(img);
            }
            done = 1;
        }
        SDL_RenderPresent(ren);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (a.audio)
        SDL_DestroyAudioStream(a.audio);
    SDL_DestroyTexture(a.tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
