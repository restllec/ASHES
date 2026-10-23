#include "injection.h"

#include "../Shared/manual_map.h"

bool MapLibrary(HANDLE process, const std::wstring& path,
    const void* reserved, std::size_t reservedSize,
    bool* exceptionSupport)
{
    return ManualMap::Map(process, path, reserved, reservedSize, exceptionSupport);
}
