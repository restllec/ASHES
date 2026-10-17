#pragma once

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <Windows.h>

namespace Launcher
{
using LogSink = std::function<void(std::wstring_view)>;

struct ProcessInfo
{
    DWORD pid{};
    std::wstring exeName;
    bool found{};
};

ProcessInfo CheckProcess();
int Run(std::stop_token stop, const LogSink& log);
int InjectOnce(const LogSink& log, DWORD specificPid = 0);
}
