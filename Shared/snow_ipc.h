#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SnowIPC
{

enum class SessionStatus : int
{
    Offline = 0,
    Connecting = 1,
    Authorized = 2,
    InWork = 3,
    Paused = 4,
    Error = 5,
    Stopped = 6
};

inline const char* StatusToString(SessionStatus status)
{
    switch (status)
    {
    case SessionStatus::Offline: return "ОФФЛАЙН";
    case SessionStatus::Connecting: return "ПОДКЛЮЧЕНИЕ";
    case SessionStatus::Authorized: return "АВТОРИЗОВАН";
    case SessionStatus::InWork: return "В РАБОТЕ";
    case SessionStatus::Paused: return "ПАУЗА";
    case SessionStatus::Error: return "ОШИБКА";
    case SessionStatus::Stopped: return "ОСТАНОВЛЕН";
    default: return "НЕИЗВЕСТНО";
    }
}

struct RoutePoint
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

// Координаты маршрута лесоруба / хаба для 2D радара
inline const RoutePoint kKnownRoutePoints[] = {
    { -2745.0f, 2775.0f, 2.4f },
    { -2748.59f, 2837.21f, 2.53f },
    { -2754.17f, 2725.91f, 3.23f },
    { -2766.21f, 2748.11f, 2.42f },
    { -2756.99f, 2852.55f, 2.56f },
    { -2732.88f, 2734.19f, 4.13f },
    { -2721.82f, 2853.91f, 2.49f },
    { -2715.46f, 2711.07f, 6.14f },
    { -2736.20f, 2703.76f, 4.89f },
    { -2747.48f, 2818.61f, 2.34f },
    { -2696.28f, 2822.86f, 2.34f },
    { -2719.69f, 2841.66f, 2.59f },
    { -2764.22f, 2797.08f, 2.34f },
    { -2696.49f, 2848.63f, 2.19f }
};

inline constexpr size_t kKnownRoutePointCount = sizeof(kKnownRoutePoints) / sizeof(kKnownRoutePoints[0]);

struct SnowTelemetry
{
    SessionStatus status{SessionStatus::Offline};
    int ping{24};
    int dimension{0};
    int interior{0};
    float x{-2748.59f};
    float y{2837.21f};
    float z{2.53f};
    float angle{0.0f};
    float health{100.0f};
    int targetCheckpoint{0};
    int totalCheckpoints{static_cast<int>(kKnownRoutePointCount)};
    int cycles{0};
    int balance{0};
    char taskType[48]{"default_route"};
    bool isPaused{false};
};

struct ChatMessage
{
    std::string timestamp; // HH:MM:SS
    std::string sender;
    std::string text;
    uint32_t color{0xFFFFFFFF}; // IM_COL32
};

} // namespace SnowIPC
