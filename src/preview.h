// Client for the preview server (ki1-port's, linked in: this program started
// again with --preview-server, see engine.c): starts it, sends preview
// requests, and collects the streamed frames on a thread.
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PreviewFrame {
    uint32_t index;
    uint8_t flag, bank, combo, move;   // ultra flag, P1 +0x34, +0x8D, +0x1E
    uint16_t anim, p2hp;               // P1 +0x14, P2 +0x54
    uint32_t rate;                     // sound sample rate
    uint8_t p2bank, p2state;           // P2 +0x34; bit 0: P2 off the ground
    uint16_t p2anim;                   // P2 +0x14
    int16_t p1x, p2x;                  // x positions (pixels)
    std::vector<uint16_t> pixels;      // 320x240 XBGR1555
    std::vector<int16_t> samples;      // mono
};

class PreviewClient {
public:
    ~PreviewClient();
    // start the server (program to run, data folder, stderr log file)
    bool start(const std::string &port, const std::string &data, const std::string &log);
    // request a preview of fighter c with a KIPATCH1 file ("-": stock game)
    // against opponent o (P2, 1-10; 6 Fulgore is the game's default); after
    // the game crashed (the server exited) this starts the server again
    void request(int fighter, const std::string &ultrap, int max_frames = 1500, int opponent = 6);
    bool crashed() const { return exited_; }   // the server exited (the game crashed)
    void stop();

    // what the server is doing, for the status line
    std::string status();
    bool streaming() const { return busy_; }
    size_t frame_count();
    // copy of frame i (false if not there yet)
    bool frame(size_t i, PreviewFrame &out);
    uint64_t generation() const { return gen_; }    // bumps on every new request
    int opponent() const { return opponent_; }      // P2 of the last request

private:
    void reader();
    bool read_all(void *p, size_t n);
    void send(const std::string &line);
    bool launch(bool append_log);
    void shut();
    std::string crash_reason();     // the runtime's last "ki1:" line in the log

    std::string prog_, data_, log_path_;
    std::atomic<bool> exited_{false};

    SDL_Process *proc_ = nullptr;
    SDL_IOStream *to_ = nullptr, *from_ = nullptr, *log_ = nullptr;
    std::atomic<bool> quit_{false};
    std::thread thread_;
    std::mutex mu_;
    std::vector<PreviewFrame> frames_;
    std::string status_ = "preview server not started";
    std::atomic<bool> busy_{false};
    std::atomic<uint64_t> gen_{0};
    int fighter_ = 0;
    std::atomic<int> opponent_{6};
    int outstanding_ = 0;   // requests sent and not finished; frames count only when 1
};
