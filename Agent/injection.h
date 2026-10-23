#pragma once

#include <Windows.h>

#include <string>

bool MapLibrary(HANDLE process, const std::wstring& path,
    const void* reserved = nullptr, std::size_t reservedSize = 0,
    bool* exceptionSupport = nullptr);
