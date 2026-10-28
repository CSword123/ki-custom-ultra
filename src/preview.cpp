#include "preview.h"

#include <cstring>

// The server runs as a child process with its stdin and stdout piped to us
// (SDL's process API: the same on Linux and Windows); its stderr goes to
// the log file.
PreviewClient::~PreviewClient()
{
    shut();
}

void PreviewClient::shut()
{
    if (proc_) {
        send("quit\n");
        if (!SDL_WaitProcess(proc_, false, nullptr)) {
            SDL_Delay(200);
            if (!SDL_WaitProcess(proc_, false, nullptr))
                SDL_KillProcess(proc_, true);
        }
        SDL_WaitProcess(proc_, true, nullptr);
    }
    quit_ = true;
    if (thread_.joinable())
        thread_.join();
    if (proc_)
        SDL_DestroyProcess(proc_);
    if (log_)
        SDL_CloseIO(log_);
    proc_ = nullptr, log_ = nullptr, to_ = from_ = nullptr;
}

bool PreviewClient::start(const std::string &port, const std::string &data, const std::string &log)
{
    prog_ = port, data_ = data, log_path_ = log;
    return launch(false);
}

bool PreviewClient::launch(bool append_log)
{
    const std::string &port = prog_, &data = data_, &log = log_path_;
    quit_ = false;
    exited_ = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        outstanding_ = 0;
        frames_.clear();
    }
    busy_ = false;
    const char *args[] = {port.c_str(), "--preview-server", "--data", data.c_str(), nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    log_ = SDL_IOFromFile(log.c_str(), append_log ? "a" : "w");  // a restart keeps the crash
    if (log_) {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, log_);
    }
    proc_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!proc_) {
        status_ = std::string("cannot start ") + port + ": " + SDL_GetError();
        return false;
    }
    to_ = SDL_GetProcessInput(proc_);
    from_ = SDL_GetProcessOutput(proc_);
    status_ = "booting the game...";
    thread_ = std::thread(&PreviewClient::reader, this);
    return true;
}

void PreviewClient::send(const std::string &line)
{
    if (!to_)
        return;
    SDL_WriteIO(to_, line.data(), line.size());
    SDL_FlushIO(to_);
}

std::string PreviewClient::crash_reason()
{
    std::string last;
    if (SDL_IOStream *f = SDL_IOFromFile(log_path_.c_str(), "r")) {
        Sint64 n = SDL_GetIOSize(f);
        std::string all(n > 0 ? (size_t)n : 0, '\0');
        if (n > 0)
            SDL_ReadIO(f, all.data(), all.size());
        SDL_CloseIO(f);
        for (size_t at = all.find("ki1:"); at != std::string::npos; at = all.find("ki1:", at + 1))
            last = all.substr(at, all.find('\n', at) - at);
    }
    return last;
}

void PreviewClient::request(int fighter, const std::string &ultrap, int max_frames, int opponent)
{
    if (exited_) {          // the game crashed: start it again (boots in ~1 s)
        shut();
        if (!launch(true))
            return;
    }
    if (busy_)
        send("stop\n");
    {
        std::lock_guard<std::mutex> l(mu_);
        frames_.clear();
        fighter_ = fighter;
        opponent_ = opponent;
        outstanding_++;
        status_ = "starting fighter " + std::to_string(fighter) + " vs " + std::to_string(opponent) +
                  " (first time: character select + walk-in)...";
    }
    gen_++;
    busy_ = true;
    send("preview " + std::to_string(fighter) + " " + ultrap + " " + std::to_string(max_frames) + " " +
         std::to_string(opponent) + "\n");
}

void PreviewClient::stop()
{
    if (busy_)
        send("stop\n");
}

std::string PreviewClient::status()
{
    std::lock_guard<std::mutex> l(mu_);
    return status_;
}

size_t PreviewClient::frame_count()
{
    std::lock_guard<std::mutex> l(mu_);
    return frames_.size();
}

bool PreviewClient::frame(size_t i, PreviewFrame &out)
{
    std::lock_guard<std::mutex> l(mu_);
    if (i >= frames_.size())
        return false;
    out = frames_[i];
    return true;
}

bool PreviewClient::read_all(void *p, size_t n)
{
    // the process stream does not block: wait for data a little at a time
    char *c = static_cast<char *>(p);
    while (n && !quit_) {
        size_t r = from_ ? SDL_ReadIO(from_, c, n) : 0;
        if (r) {
            c += r, n -= r;
            continue;
        }
        if (!from_ || SDL_GetIOStatus(from_) != SDL_IO_STATUS_NOT_READY)
            return false;       /* the server exited */
        SDL_Delay(1);
    }
    return n == 0;
}

void PreviewClient::reader()
{
    for (;;) {
        uint32_t h[3];
        if (!read_all(h, sizeof h) || h[0] != 0x5650494Bu)
            break;
        std::vector<uint8_t> body(h[2]);
        if (!read_all(body.data(), body.size()))
            break;
        if (h[1] == 2) {
            std::string msg(body.begin(), body.end());
            std::lock_guard<std::mutex> l(mu_);
            if (msg == "booted" && !outstanding_)
                status_ = "game booted; ready for previews";
            else if (!msg.compare(0, 5, "ready"))
                status_ = "fighter ready, streaming...";
            else if (!msg.compare(0, 5, "error") && outstanding_ == 1)
                status_ = msg;
            // a request ends with "done", or with these errors from the root
            bool end = !msg.compare(0, 4, "done") || !msg.compare(0, 12, "error fighte") ||
                       !msg.compare(0, 10, "error fork");
            if (end && outstanding_ > 0 && --outstanding_ == 0) {
                if (!msg.compare(0, 4, "done") && status_.compare(0, 5, "error"))
                    status_ = "done: " + std::to_string(frames_.size()) + " frames";
                busy_ = false;
            }
            continue;
        }
        const size_t H = 28;            // status header before the pixels
        if (h[1] != 1 || body.size() < H + 320 * 240 * 2)
            continue;
        PreviewFrame f;
        uint32_t ns;
        memcpy(&f.index, &body[0], 4);
        f.flag = body[4], f.bank = body[5];
        memcpy(&f.anim, &body[6], 2);
        f.combo = body[8], f.move = body[9];
        memcpy(&f.p2hp, &body[10], 2);
        memcpy(&f.rate, &body[12], 4);
        memcpy(&ns, &body[16], 4);
        f.p2bank = body[20], f.p2state = body[21];
        memcpy(&f.p2anim, &body[22], 2);
        memcpy(&f.p1x, &body[24], 2);
        memcpy(&f.p2x, &body[26], 2);
        f.pixels.resize(320 * 240);
        memcpy(f.pixels.data(), &body[H], 320 * 240 * 2);
        if (body.size() >= H + 320 * 240 * 2 + ns * 2) {
            f.samples.resize(ns);
            memcpy(f.samples.data(), &body[H + 320 * 240 * 2], ns * 2);
        }
        std::lock_guard<std::mutex> l(mu_);
        if (outstanding_ == 1)          // not a stopped, older preview
            frames_.push_back(std::move(f));
    }
    std::string why = quit_ ? "" : crash_reason();
    std::lock_guard<std::mutex> l(mu_);
    status_ = "the game crashed" + (frames_.empty() ? std::string() : " at frame " + std::to_string(frames_.size())) +
              (why.empty() ? "" : " (" + why + ")") + "; the next preview starts it again (log: " + log_path_ + ")";
    busy_ = false;
    exited_ = true;
}
