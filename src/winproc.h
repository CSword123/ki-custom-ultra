// Running the tools (Python, gcc) on Windows without a console window each:
// the editor is a GUI program, so every console program it starts would get
// its own window. These start them with CREATE_NO_WINDOW instead, their
// stdout and stderr captured together.
#pragma once

#ifdef _WIN32
#include <string>
#include <vector>

struct WinProc;

// start a command line (the program found on PATH as CreateProcess does);
// nullptr and why in err if it cannot be started
WinProc *winproc_start(const std::string &cmdline, std::string &err);
// read its output to the end, wait for it; its exit code
int winproc_finish(WinProc *p, std::string &out);
// argv as a command line, quoted the way the C runtime splits it again
std::string winproc_cmdline(const std::vector<std::string> &argv);
#endif
