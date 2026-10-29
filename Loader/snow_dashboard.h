#pragma once

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "terminal_manager.h"
#include "../Shared/snow_ipc.h"
#include "../Client/third_party/imgui/imgui.h"

namespace SnowDashboard
{

inline void DrawRadar(ImDrawList* draw, ImVec2 canvasPos, ImVec2 canvasSize, const SnowIPC::SnowTelemetry& telem)
{
    const ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    const float radius = (std::min(canvasSize.x, canvasSize.y) * 0.5f) - 10.0f;

    draw->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(14, 15, 22, 240), 6.0f);
    draw->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(255, 255, 255, 20), 6.0f, 0, 1.0f);

    // Сетка радара
    draw->AddCircle(center, radius, IM_COL32(255, 255, 255, 25), 32, 1.0f);
    draw->AddCircle(center, radius * 0.66f, IM_COL32(255, 255, 255, 18), 32, 1.0f);
    draw->AddCircle(center, radius * 0.33f, IM_COL32(255, 255, 255, 15), 32, 1.0f);

    // Оси
    draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(255, 255, 255, 18), 1.0f);
    draw->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(255, 255, 255, 18), 1.0f);

    // Масштабирование координат MTA Province (Лесоруб / Хаб)
    const float hubX = -2735.0f;
    const float hubY = 2780.0f;
    const float mapScale = (radius * 0.85f) / 95.0f;

    auto WorldToScreen = [&](float wx, float wy) -> ImVec2 {
        float sx = center.x + (wx - hubX) * mapScale;
        float sy = center.y - (wy - hubY) * mapScale;
        return ImVec2(sx, sy);
    };

    // Линии пути между точками
    const auto& pts = SnowIPC::kKnownRoutePoints;
    const size_t ptCount = SnowIPC::kKnownRoutePointCount;
    for (size_t i = 0; i < ptCount; ++i)
    {
        ImVec2 p1 = WorldToScreen(pts[i].x, pts[i].y);
        ImVec2 p2 = WorldToScreen(pts[(i + 1) % ptCount].x, pts[(i + 1) % ptCount].y);
        draw->AddLine(p1, p2, IM_COL32(110, 85, 230, 90), 1.5f);
    }

    // Точки маршрута
    for (size_t i = 0; i < ptCount; ++i)
    {
        ImVec2 sp = WorldToScreen(pts[i].x, pts[i].y);
        bool isTarget = (static_cast<int>(i) == telem.targetCheckpoint);
        if (isTarget)
        {
            draw->AddCircleFilled(sp, 6.0f, IM_COL32(255, 215, 0, 240));
            draw->AddCircle(sp, 9.0f, IM_COL32(255, 215, 0, 160), 16, 1.5f);
        }
        else
        {
            draw->AddCircleFilled(sp, 3.5f, IM_COL32(120, 100, 240, 200));
        }
    }

    // Стрелочка позиции персонажа
    ImVec2 playerPos = WorldToScreen(telem.x, telem.y);
    float angRad = telem.angle * (3.14159265f / 180.0f);
    float cosA = cosf(angRad);
    float sinA = sinf(angRad);
    ImVec2 pHead(playerPos.x + sinA * 10.0f, playerPos.y - cosA * 10.0f);
    ImVec2 pLeft(playerPos.x - cosA * 6.0f - sinA * 4.0f, playerPos.y - sinA * 6.0f + cosA * 4.0f);
    ImVec2 pRight(playerPos.x + cosA * 6.0f - sinA * 4.0f, playerPos.y + sinA * 6.0f + cosA * 4.0f);

    draw->AddTriangleFilled(pHead, pLeft, pRight, IM_COL32(50, 220, 120, 255));
    draw->AddTriangle(pHead, pLeft, pRight, IM_COL32(255, 255, 255, 220), 1.5f);

    draw->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 6.0f), IM_COL32(160, 165, 185, 220), "РАДАР МАРШРУТА (2D CANVAS)");
}

inline void Draw(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    auto& mgr = TerminalMgr::TerminalManager::Instance();
    auto& accounts = mgr.GetAccounts();

    static int s_selectedProfileIdx = 0;
    static int s_inspectorTab = 0; // 0: Радар, 1: Чат, 2: Настройки
    static char s_chatInputBuf[256]{};
    static bool s_showAddModal = false;

    // Буферы для создания нового профиля
    static char s_newLogin[64]{"player_new"};
    static char s_newPass[64]{"secret123"};
    static int s_newServer = 0;
    static int s_newTask = 0;
    static char s_newProxy[128]{""};

    if (s_selectedProfileIdx >= static_cast<int>(accounts.size()))
        s_selectedProfileIdx = accounts.empty() ? 0 : static_cast<int>(accounts.size()) - 1;

    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const float cardHeight = fullHeight - 72.0f;
    const float availableW = fullWidth - 32.0f;
    const float leftW = std::floor(availableW * 0.55f);
    const float rightW = availableW - leftW - 12.0f;

    // ==========================================
    // ЛЕВАЯ ПАНЕЛЬ: КАРТОЧКИ АККАУНТОВ (55%)
    // ==========================================
    const ImVec2 leftEnd(cardPos.x + leftW, cardPos.y + cardHeight);
    draw->AddRectFilled(cardPos, leftEnd, IM_COL32(18, 19, 26, 170), 8.0f);
    draw->AddRect(cardPos, leftEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 10.0f), IM_COL32(140, 145, 165, 255), "СНЕЖОК: КАРТОЧКИ МУЛЬТИ-СЕССИЙ");

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 12.0f, cardPos.y + 32.0f));
    ImGui::BeginGroup();

    // Верхние кнопки управления
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(75, 70, 140, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(95, 90, 175, 255));
    if (ImGui::Button("+ Добавить аккаунт", ImVec2(145.0f, 24.0f)))
    {
        s_showAddModal = true;
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 140, 75, 210));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 175, 95, 255));
    if (ImGui::Button("▶ Запустить все", ImVec2(115.0f, 24.0f)))
    {
        mgr.StartAllActive();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(165, 45, 45, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(195, 55, 55, 255));
    if (ImGui::Button("⏹ Стоп все", ImVec2(90.0f, 24.0f)))
    {
        mgr.StopAllActive();
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    // Скроллируемый список карточек
    const float cardListH = cardHeight - 74.0f;
    ImGui::BeginChild("##accounts_scroll_list", ImVec2(leftW - 24.0f, cardListH), false);

    for (int i = 0; i < static_cast<int>(accounts.size()); ++i)
    {
        auto& acc = accounts[i];
        auto session = mgr.GetSessionByLogin(acc.login);
        const bool isRunning = (session && session->isRunning);
        const bool isSelected = (s_selectedProfileIdx == i);

        ImGui::PushID(i);

        // Индикатор статуса и цвет рамки
        ImU32 statusColor = IM_COL32(120, 125, 140, 255); // OFFLINE
        std::string statusText = "ОФФЛАЙН";
        int pingVal = 0;
        int cycVal = 0;
        int balVal = 0;
        float px = -2745.0f, py = 2775.0f;

        if (session)
        {
            pingVal = session->telemetry.ping;
            cycVal = session->cycles;
            balVal = session->telemetry.balance;
            px = session->telemetry.x;
            py = session->telemetry.y;

            if (session->status == "WORKING")
            {
                statusColor = IM_COL32(50, 205, 50, 255);
                statusText = "В РАБОТЕ";
            }
            else if (session->status == "PAUSED")
            {
                statusColor = IM_COL32(240, 200, 40, 255);
                statusText = "ПАУЗА";
            }
            else if (session->status == "AUTHORIZED")
            {
                statusColor = IM_COL32(70, 160, 255, 255);
                statusText = "АВТОРИЗОВАН";
            }
            else if (session->status == "CONNECTING")
            {
                statusColor = IM_COL32(230, 180, 50, 255);
                statusText = "ПОДКЛЮЧЕНИЕ";
            }
            else if (session->status == "ERROR")
            {
                statusColor = IM_COL32(230, 60, 60, 255);
                statusText = "ОШИБКА";
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        if (isSelected)
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(110, 85, 230, 230));
        else
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 25));

        char childName[32];
        snprintf(childName, sizeof(childName), "##card_%d", i);
        ImGui::BeginChild(childName, ImVec2(0, 108.0f), true, ImGuiWindowFlags_NoScrollbar);

        // Клик по фону карточки выбирает ее
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            s_selectedProfileIdx = i;
        }

        // ВЕРХНЯЯ СТРОКА: Статус-кружок, Логин (Сервер) и Ping
        ImVec2 curScreen = ImGui::GetCursorScreenPos();
        draw->AddCircleFilled(ImVec2(curScreen.x + 6.0f, curScreen.y + 7.0f), 5.0f, statusColor);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);

        const char* srvName = (acc.serverIndex >= 0 && acc.serverIndex < 7) ?
            TerminalMgr::kProvinceServers[acc.serverIndex].name : "Сервер";
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%s (%s)", acc.login.c_str(), srvName);

        ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f);
        if (isRunning && session && session->status != "ERROR" && session->status != "CONNECTING" && pingVal > 0)
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Ping: %d ms", pingVal);
        else
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor), "[%s]", statusText.c_str());

        // СРЕДНЯЯ СТРОКА: Задача, Циклы, Баланс, Координаты
        ImGui::SetNextItemWidth(130.0f);
        const char* taskShortNames[] = { "Трамвай", "Снегоуборка", "Завод ЖТУ", "Лесоруб", "Табачка" };
        int taskI = acc.taskIndex % 5;
        if (ImGui::Combo("##card_task", &taskI, taskShortNames, IM_ARRAYSIZE(taskShortNames)))
        {
            acc.taskIndex = taskI;
            mgr.SaveAccounts();
            if (session && session->isRunning)
            {
                session->SendCommand(std::string("CMD_SET_TASK ") + TerminalMgr::kAvailableTasks[taskI].id);
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.75f, 0.8f, 0.9f, 1.0f), "Ц: %d | %d руб.", cycVal, balVal);

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.6f, 0.7f, 1.0f), "X:%.0f Y:%.0f", px, py);

        // НИЖНЯЯ СТРОКА: Кнопки Подключить/Отключить, Старт/Пауза, Удалить
        if (!isRunning)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 140, 75, 210));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 175, 95, 255));
            if (ImGui::Button("Подключить", ImVec2(95.0f, 22.0f)))
            {
                mgr.StartTerminal(acc, false);
                s_selectedProfileIdx = i;
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(185, 45, 45, 220));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220, 55, 55, 255));
            if (ImGui::Button("Отключить", ImVec2(95.0f, 22.0f)))
            {
                mgr.StopTerminal(session);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        if (isRunning)
        {
            const bool isPaused = (session->telemetry.isPaused || session->status == "PAUSED");
            if (isPaused)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 150, 30, 210));
                if (ImGui::Button("▶ Старт", ImVec2(75.0f, 22.0f)))
                {
                    session->TogglePause();
                }
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 75, 95, 210));
                if (ImGui::Button("⏸ Пауза", ImVec2(75.0f, 22.0f)))
                {
                    session->TogglePause();
                }
                ImGui::PopStyleColor();
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 55, 70, 150));
            ImGui::Button("⏸ Пауза", ImVec2(75.0f, 22.0f));
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("Инспектор", ImVec2(80.0f, 22.0f)))
        {
            s_selectedProfileIdx = i;
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 65, 80, 180));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(180, 50, 50, 220));
        if (ImGui::Button("Удалить", ImVec2(65.0f, 22.0f)))
        {
            mgr.RemoveAccount(i);
            ImGui::PopStyleColor(2);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleColor(2);

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    // ==========================================
    // ПРАВАЯ ПАНЕЛЬ: ИНСПЕКТОР ВЫБРАННОГО АККАУНТА (45%)
    // ==========================================
    const ImVec2 rightPos(cardPos.x + leftW + 12.0f, cardPos.y);
    const ImVec2 rightEnd(rightPos.x + rightW, cardPos.y + cardHeight);
    draw->AddRectFilled(rightPos, rightEnd, IM_COL32(18, 19, 26, 170), 8.0f);
    draw->AddRect(rightPos, rightEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);

    std::string selLogin = (s_selectedProfileIdx >= 0 && s_selectedProfileIdx < static_cast<int>(accounts.size()))
        ? accounts[s_selectedProfileIdx].login : "нет аккаунта";
    char titleBuf[128];
    snprintf(titleBuf, sizeof(titleBuf), "ИНСПЕКТОР: %s", selLogin.c_str());
    draw->AddText(ImVec2(rightPos.x + 14.0f, rightPos.y + 10.0f), IM_COL32(140, 145, 165, 255), titleBuf);

    ImGui::SetCursorScreenPos(ImVec2(rightPos.x + 12.0f, rightPos.y + 32.0f));
    ImGui::BeginGroup();

    // Вкладки инспектора
    if (ImGui::Button("2D Радар", ImVec2(rightW * 0.31f, 24.0f))) s_inspectorTab = 0;
    ImGui::SameLine();
    if (ImGui::Button("Чат и Логи", ImVec2(rightW * 0.32f, 24.0f))) s_inspectorTab = 1;
    ImGui::SameLine();
    if (ImGui::Button("Настройки", ImVec2(rightW * 0.31f, 24.0f))) s_inspectorTab = 2;

    ImGui::Spacing();

    std::shared_ptr<TerminalMgr::TerminalSession> selSession = nullptr;
    if (s_selectedProfileIdx >= 0 && s_selectedProfileIdx < static_cast<int>(accounts.size()))
        selSession = mgr.GetSessionByLogin(accounts[s_selectedProfileIdx].login);

    SnowIPC::SnowTelemetry telem{};
    if (selSession) telem = selSession->telemetry;

    if (s_inspectorTab == 0)
    {
        // 1. 2D Радар точек и позиции персонажа
        const float radarSize = cardHeight - 120.0f;
        ImVec2 rStart = ImGui::GetCursorScreenPos();
        DrawRadar(draw, rStart, ImVec2(rightW - 24.0f, radarSize - 20.0f), telem);

        ImGui::Dummy(ImVec2(rightW - 24.0f, radarSize - 20.0f));
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f),
            "Координаты: X: %.1f  Y: %.1f  Z: %.1f | Чекпоинт: #%d / 13 | HP: %.0f",
            telem.x, telem.y, telem.z, telem.targetCheckpoint, telem.health);
    }
    else if (s_inspectorTab == 1)
    {
        // 2. Игровой чат и события
        const float chatBoxH = cardHeight - 118.0f;
        ImGui::BeginChild("##inspector_chat_view", ImVec2(rightW - 24.0f, chatBoxH), true);
        if (selSession)
        {
            std::lock_guard<std::mutex> lock(selSession->chatMutex);
            if (selSession->chatMessages.empty())
            {
                ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Чат пуст. Ожидание сообщений от сервера и бота...");
            }
            else
            {
                for (const auto& msg : selSession->chatMessages)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.0f), "[%s]", msg.timestamp.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(ImColor(msg.color), "%s: %s", msg.sender.c_str(), msg.text.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Сессия оффлайн. Запустите аккаунт для отображения чата.");
        }
        ImGui::EndChild();

        // Поле ввода для отправки в чат
        ImGui::SetNextItemWidth(rightW - 110.0f);
        bool enterPressed = ImGui::InputText("##chat_input", s_chatInputBuf, sizeof(s_chatInputBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Отправить", ImVec2(80.0f, 22.0f)) || enterPressed) && s_chatInputBuf[0] != '\0')
        {
            if (selSession && selSession->isRunning)
            {
                selSession->SendChatMessage(s_chatInputBuf);
            }
            s_chatInputBuf[0] = '\0';
        }
    }
    else if (s_inspectorTab == 2)
    {
        // 3. Настройки сессии
        if (s_selectedProfileIdx >= 0 && s_selectedProfileIdx < static_cast<int>(accounts.size()))
        {
            auto& acc = accounts[s_selectedProfileIdx];
            ImGui::TextUnformatted("Логин аккаунта:");
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", acc.login.c_str());

            ImGui::Spacing();
            ImGui::TextUnformatted("Сервер подключения:");
            const char* serverNames[] = { "Сервер #1", "Сервер #2", "Сервер #3", "Сервер #4", "Сервер #5", "Сервер #6", "Сервер #7" };
            ImGui::SetNextItemWidth(rightW - 24.0f);
            ImGui::Combo("##edit_server", &acc.serverIndex, serverNames, IM_ARRAYSIZE(serverNames));

            ImGui::Spacing();
            ImGui::TextUnformatted("Основная задача:");
            const char* taskNames[] = { "Трамвай (Маршрут)", "Поливалка (Снег)", "Завод ЖТУ (Пешеход)", "Лесоруб", "Табачная фабрика" };
            ImGui::SetNextItemWidth(rightW - 24.0f);
            ImGui::Combo("##edit_task", &acc.taskIndex, taskNames, IM_ARRAYSIZE(taskNames));

            ImGui::Spacing();
            ImGui::Checkbox("Авто-реконнект при обрыве (CD09 / рестарт)", &acc.autoReconnect);
            ImGui::SetNextItemWidth(rightW - 24.0f);
            ImGui::SliderInt("Задержка (сек)", &acc.reconnectDelaySec, 5, 60);

            ImGui::Spacing();
            ImGui::TextUnformatted("Настройка Proxy (SOCKS5):");
            char proxyBuf[128]{};
            strncpy_s(proxyBuf, acc.proxy.c_str(), sizeof(proxyBuf) - 1);
            ImGui::SetNextItemWidth(rightW - 24.0f);
            if (ImGui::InputText("##edit_proxy", proxyBuf, sizeof(proxyBuf)))
            {
                acc.proxy = proxyBuf;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("СОХРАНИТЬ ПРОФИЛЬ (DPAPI)", ImVec2(rightW - 24.0f, 26.0f)))
            {
                mgr.SaveAccounts();
            }
        }
    }

    ImGui::EndGroup();

    // ==========================================
    // МОДАЛЬНОЕ ОКНО ДОБАВЛЕНИЯ НОВОГО АККАУНТА
    // ==========================================
    if (s_showAddModal)
    {
        ImGui::OpenPopup("Новый аккаунт");
    }

    if (ImGui::BeginPopupModal("Новый аккаунт", &s_showAddModal, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Добавление аккаунта в менеджер мульти-сессий");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Логин:");
        ImGui::InputText("##new_login", s_newLogin, sizeof(s_newLogin));

        ImGui::TextUnformatted("Пароль:");
        ImGui::InputText("##new_pass", s_newPass, sizeof(s_newPass), ImGuiInputTextFlags_Password);

        ImGui::TextUnformatted("Сервер:");
        const char* srvList[] = { "Сервер #1", "Сервер #2", "Сервер #3", "Сервер #4", "Сервер #5", "Сервер #6", "Сервер #7" };
        ImGui::Combo("##new_server", &s_newServer, srvList, IM_ARRAYSIZE(srvList));

        ImGui::TextUnformatted("Задача:");
        const char* tskList[] = { "Трамвай (Маршрут)", "Поливалка (Снег)", "Завод ЖТУ (Пешеход)", "Лесоруб", "Табачная фабрика" };
        ImGui::Combo("##new_task", &s_newTask, tskList, IM_ARRAYSIZE(tskList));

        ImGui::TextUnformatted("Proxy (SOCKS5, необязательно):");
        ImGui::InputText("##new_proxy", s_newProxy, sizeof(s_newProxy));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Добавить и сохранить", ImVec2(180.0f, 24.0f)))
        {
            TerminalMgr::AccountProfile newAcc;
            newAcc.login = s_newLogin;
            newAcc.encryptedPassword = TerminalMgr::EncryptPassword(s_newPass);
            newAcc.serverIndex = s_newServer;
            newAcc.taskIndex = s_newTask;
            newAcc.proxy = s_newProxy;
            newAcc.autoReconnect = true;
            newAcc.reconnectDelaySec = 15;
            mgr.AddAccount(newAcc);
            s_showAddModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(90.0f, 24.0f)))
        {
            s_showAddModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace SnowDashboard
