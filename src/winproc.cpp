// see winproc.h
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "winproc.h"

struct WinProc {
    HANDLE proc, out;
};

static std::wstring wide(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

WinProc *winproc_start(const std::string &cmdline, std::string &err)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd, wr, nul;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        err = "cannot make a pipe";
        return nullptr;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = wide(cmdline);
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, nullptr, &si, &pi);
    DWORD e = GetLastError();
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        err = "error " + std::to_string(e);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    return new WinProc{pi.hProcess, rd};
}

int winproc_finish(WinProc *p, std::string &out)
{
    char buf[4096];
    DWORD n;
    while (ReadFile(p->out, buf, sizeof buf, &n, nullptr) && n)
        out.append(buf, n);
    WaitForSingleObject(p->proc, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(p->proc, &code);
    CloseHandle(p->out);
    CloseHandle(p->proc);
    delete p;
    return (int)code;
}

std::string winproc_cmdline(const std::vector<std::string> &argv)
{
    std::string line;
    for (const std::string &a : argv) {
        if (!line.empty())
            line += ' ';
        if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) {
            line += a;
            continue;
        }
        line += '"';
        size_t slashes = 0;
        for (char c : a) {
            if (c == '\\')
                slashes++;
            else {
                if (c == '"')
                    line.append(slashes + 1, '\\');
                slashes = 0;
            }
            line += c;
        }
        line.append(slashes, '\\');
        line += '"';
    }
    return line;
}
#endif
