--[[
    JTU BOT | ЖТУ
    DarkFlame / MTA:SA

    /jtu  - запуск
    F6    - аварийная остановка
    F7    - показать/скрыть панель

    Количество циклов выбирается в панели (1–99).

    В чат выводится только:
      - "Бот запущен"
      - "Бот закончил работу"
      - красная ошибка "Что-то пошло не так. Завершил работу"
]]

local api = {
    triggerServerEvent = dfTriggerServerEvent,
    emulateKey = dfEmulateKey,
    alert = dfPlayAlertSignal,
    log = dfTramLog,
    takeCommand = dfTramTakeCommand,
    updateState = dfTramUpdate,
}

local STOP_DISTANCE = 3
local STOP_WAIT = 1800
local DOOR_WAIT = 6000
local KEY_PRESS_TIME = 150
local TICK = 50
local EXIT_LEFT_TIME = 3500

-- Плавное торможение
local BRAKE_DISTANCE = 120   -- с какого расстояния до маркера начинаем тормозить (больше = плавнее)
local BRAKE_MIN_RATIO = 0.12 -- минимальная "ползущая" скорость в конце (доля от скорости начала торможения)

local POINT_1 = 214.82203674316
local POINT_2 = 1073.6032714844

local stops = {
    POINT_1, POINT_2,
    POINT_1, POINT_2,
    POINT_1, POINT_2,
    POINT_1, POINT_2,
    POINT_1, POINT_2,
    POINT_1, POINT_2
}

local running = false
local paused = false
local stopIndex = 1
local phase = "idle"
local train = nil
local stopTimer = nil
local doorTimer = nil
local jobTimer = nil
local headlightTimer1 = nil
local headlightTimer2 = nil
local exitTimer = nil
local panelVisible = false
local lastLog = "Ожидание запуска"
local routeStartedAt = 0
local brakeV0 = nil
local brakeD0 = nil

-- Настройки циклов
local targetCycles = 1
local completedCycles = 0
local errorStopped = false

-- ============================================================
-- HELPERS
-- ============================================================

local function log(text)
    lastLog = tostring(text)
    if api.log then api.log("[JTU] " .. tostring(text)) end
end

-- Обычные сообщения: только в панель/лог, в чат НЕ пишем
local function notify(text)
    log(text)
end

-- Сообщение в чат (только важные)
local function chatInfo(text)
    outputChatBox("[JTU] " .. tostring(text), 185, 110, 255)
    log(text)
end

-- Красное предупреждение в чат
local function chatError(text)
    outputChatBox("[JTU] " .. tostring(text), 255, 0, 0)
    log("ОШИБКА: " .. tostring(text))
end

local function setTargetCycles(value)
    value = tonumber(value) or 1
    -- во время работы нельзя поставить меньше, чем идёт текущий цикл
    local minValue = running and (completedCycles + 1) or 1
    targetCycles = math.max(minValue, math.min(99, math.floor(value)))
end

local function stopTimerSafe(timer)
    if timer and isTimer(timer) then killTimer(timer) end
    return nil
end

local function findTrain()
    if isElement(train) then return train end
    local vehicle = getPedOccupiedVehicle(localPlayer)
    if isElement(vehicle) then
        train = vehicle
        return vehicle
    end
    return nil
end

local function getPosition()
    local vehicle = findTrain()
    if not isElement(vehicle) then return nil end
    if type(getTrainPosition) == "function" then
        return getTrainPosition(vehicle)
    end
    local x = getElementPosition(vehicle)
    return x
end

local function getSpeed()
    local vehicle = findTrain()
    if not isElement(vehicle) then return 0 end
    if type(getTrainSpeed) == "function" then
        return getTrainSpeed(vehicle)
    end
    return 0
end

local function setSpeed(speed)
    local vehicle = findTrain()
    if not isElement(vehicle) then return end
    if type(setTrainSpeed) == "function" then
        setTrainSpeed(vehicle, speed)
    end
end

local function keyDown(key)
    api.emulateKey(key, true)
end

local function keyUp(key)
    api.emulateKey(key, false)
end

local function pressKey(key)
    keyDown(key)
    setTimer(function()
        keyUp(key)
    end, KEY_PRESS_TIME, 1)
end

local function releaseMovement()
    keyUp("W")
    keyUp("S")
    keyUp("A")
end

local function fullStop()
    releaseMovement()
    setSpeed(0)
end

local function killAllTimers()
    stopTimer = stopTimerSafe(stopTimer)
    doorTimer = stopTimerSafe(doorTimer)
    jobTimer = stopTimerSafe(jobTimer)
    headlightTimer1 = stopTimerSafe(headlightTimer1)
    headlightTimer2 = stopTimerSafe(headlightTimer2)
    exitTimer = stopTimerSafe(exitTimer)
end

-- Единая обработка любой ошибки бота
local function failBot(reason)
    if errorStopped then return end
    errorStopped = true

    running = false
    paused = false
    phase = "error"

    killAllTimers()
    releaseMovement()
    fullStop()

    if reason then log("Причина: " .. tostring(reason)) end
    chatError("Что-то пошло не так. Завершил работу")
end

local function phaseText()
    if not running then
        if phase == "finished" then return "РАБОТА ЗАВЕРШЕНА" end
        if phase == "error" then return "ОШИБКА" end
        return "ГОТОВ"
    end
    if paused then return "ПАУЗА" end
    if phase == "moving" then return "В ПУТИ" end
    if phase == "stopping" then return "ОСТАНОВКА" end
    if phase == "door" then return "ДВЕРИ" end
    if phase == "starting" then return "ЗАПУСК" end
    return string.upper(tostring(phase))
end

local function phaseStatusText()
    if phase == "error" then return "Ошибка" end
    if not running then return "Остановлен" end
    if paused then return "Пауза" end
    return "Активен"
end

-- ============================================================
-- HEADLIGHTS / DOORS / ROUTE
-- ============================================================

local startJob

local function startHeadlights()
    if not running then return end
    notify("Включаем фары: L + L")

    headlightTimer1 = setTimer(function()
        if not running then return end
        pressKey("L")
    end, 500, 1)

    headlightTimer2 = setTimer(function()
        if not running then return end
        pressKey("L")
        notify("Фары включены.")
    end, 800, 1)
end

local function finishAll()
    notify("Все выбранные циклы завершены.")
    chatInfo("Бот закончил работу")

    exitTimer = setTimer(function()
        keyDown("A")
        exitTimer = setTimer(function()
            keyUp("A")
        end, EXIT_LEFT_TIME, 1)
    end, 500, 1)
end

local function finishRoute()
    running = false
    paused = false
    phase = "finished"

    killAllTimers()
    releaseMovement()
    fullStop()

    completedCycles = completedCycles + 1
    notify("Цикл " .. tostring(completedCycles) .. "/" .. tostring(targetCycles) .. " завершён.")

    if completedCycles < targetCycles then
        notify("Подготовка к следующему циклу...")
        stopTimer = setTimer(function()
            if errorStopped then return end
            if completedCycles >= targetCycles then
                finishAll()
                return
            end
            stopIndex = 1
            phase = "starting"
            paused = false
            running = true
            notify("Начинаем цикл " .. tostring(completedCycles + 1) .. "/" .. tostring(targetCycles) .. ".")
            startJob()
        end, 1500, 1)
        return
    end

    finishAll()
end

local function openDoors()
    if not running or paused then return end

    phase = "door"
    fullStop()
    notify("Остановка " .. tostring(stopIndex) .. "/" .. tostring(#stops) .. " — открываем двери")

    pressKey("2")

    doorTimer = setTimer(function()
        if not running or paused then return end

        pressKey("2")

        setTimer(function()
            if not running or paused then return end

            stopIndex = stopIndex + 1
            if stopIndex > #stops then
                finishRoute()
                return
            end

            phase = "moving"
            notify("Следующая остановка " .. tostring(stopIndex) .. "/" .. tostring(#stops))
        end, KEY_PRESS_TIME + 50, 1)
    end, DOOR_WAIT, 1)
end

local function arriveAtStop()
    if phase ~= "moving" then return end

    phase = "stopping"
    brakeV0, brakeD0 = nil, nil
    fullStop()
    notify("Остановка " .. tostring(stopIndex) .. "/" .. tostring(#stops) .. " — остановились")

    stopTimer = setTimer(function()
        if not running or paused then return end
        openDoors()
    end, STOP_WAIT, 1)
end

local function movementTick()
    if not running or paused then return end
    if phase ~= "moving" then return end

    local vehicle = findTrain()
    if not isElement(vehicle) then
        failBot("Транспорт не найден")
        return
    end

    local position = getPosition()
    if not position then return end

    local target = stops[stopIndex]
    if not target then
        finishRoute()
        return
    end

    local distance = math.abs(target - position)

    if distance <= STOP_DISTANCE then
        arriveAtStop()
        return
    end

    -- Плавное торможение: скорость снижается линейно по мере приближения к маркеру
    if distance <= BRAKE_DISTANCE then
        local speed = getSpeed() or 0

        if not brakeV0 and math.abs(speed) > 0.05 then
            brakeV0 = speed
            brakeD0 = math.max(distance, 1)
        end

        if brakeV0 then
            keyUp("W")
            local ratio = math.max(BRAKE_MIN_RATIO, math.min(1, distance / brakeD0))
            local wanted = brakeV0 * ratio

            if type(setTrainSpeed) == "function" then
                if math.abs(speed) > math.abs(wanted) then
                    setSpeed(wanted)
                elseif math.abs(speed) < math.abs(brakeV0) * BRAKE_MIN_RATIO * 0.5 then
                    keyDown("W") -- не даём остановиться, не доехав до маркера
                end
            else
                -- запасной вариант: тормозим клавишей S
                if math.abs(speed) > math.abs(wanted) then
                    keyDown("S")
                else
                    keyUp("S")
                    keyDown("W")
                end
            end
            return
        end
    end

    keyUp("S")
    keyDown("W")
end

-- ============================================================
-- JOB START / STOP / PAUSE
-- ============================================================

startJob = function()
    if not running then return end

    notify("ЖТУ: открываем работу...")

    local resource = getResourceFromName("province_ngrail")
    if not resource then
        failBot("province_ngrail не найден")
        return
    end

    local root = getResourceRootElement(resource)

    api.triggerServerEvent("ngrail:start", root)
    notify("ЖТУ: запрос на начало работы отправлен.")

    jobTimer = setTimer(function()
        if not running then return end

        notify("ЖТУ: устраиваемся на работу...")
        api.triggerServerEvent("ngrail:onJobAccepted", root)

        setTimer(function()
            if not running then return end
            startHeadlights()
        end, 500, 1)

        setTimer(function()
            if not running then return end
            phase = "moving"
            notify("ЖТУ: работа принята. Начинаем маршрут.")
        end, 2000, 1)
    end, 1000, 1)
end

function startJTU()
    if running then
        notify("Бот уже запущен.")
        return
    end

    train = nil
    stopIndex = 1
    phase = "starting"
    paused = false
    running = true
    errorStopped = false
    completedCycles = 0
    routeStartedAt = getTickCount()
    brakeV0, brakeD0 = nil, nil

    chatInfo("Бот запущен")
    startJob()
end

function stopJTU()
    local wasRunning = running

    running = false
    paused = false
    phase = "idle"
    errorStopped = false

    killAllTimers()
    releaseMovement()
    fullStop()

    if wasRunning then
        chatInfo("Бот закончил работу")
    end
end

local function togglePause()
    if not running then
        notify("Бот не запущен.")
        return
    end

    paused = not paused

    if paused then
        releaseMovement()
        setSpeed(0)
        notify("Бот поставлен на паузу.")
    else
        notify("Бот продолжает маршрут.")
    end
end

-- ============================================================
-- PROGRESS
-- ============================================================

local function getProgress()
    if phase == "finished" then
        return 100
    end
    if not running and stopIndex == 1 then
        return 0
    end
    local total = #stops
    if total <= 0 then return 0 end
    return math.max(0, math.min(100, math.floor(((stopIndex - 1) / total) * 100)))
end

-- ============================================================
-- DX DESIGN
-- ============================================================

local sw, sh = guiGetScreenSize()
local ui = {
    x = 0,
    y = 0,
    w = 440,
    h = 525,
    dragging = false,
    dragX = 0,
    dragY = 0,
}

local function updateUIPosition()
    sw, sh = guiGetScreenSize()
    ui.x = math.floor((sw - ui.w) / 2)
    ui.y = math.floor((sh - ui.h) / 2)
end

updateUIPosition()

local function roundedRect(x, y, w, h, radius, color)
    -- Simple layered rounded-style rectangle without external libraries.
    dxDrawRectangle(x + radius, y, w - radius * 2, h, color)
    dxDrawRectangle(x, y + radius, radius, h - radius * 2, color)
    dxDrawRectangle(x + w - radius, y + radius, radius, h - radius * 2, color)
    dxDrawCircle(x + radius, y + radius, radius, 180, 270, color, color, 8)
    dxDrawCircle(x + w - radius, y + radius, radius, 270, 360, color, color, 8)
    dxDrawCircle(x + radius, y + h - radius, radius, 90, 180, color, color, 8)
    dxDrawCircle(x + w - radius, y + h - radius, radius, 0, 90, color, color, 8)
end

local function drawText(text, x, y, w, h, color, scale, alignX, alignY, font)
    dxDrawText(
        tostring(text), x, y, x + w, y + h,
        color, scale or 1, font or "default",
        alignX or "left", alignY or "center",
        false, false, false, true
    )
end

local function getCurrentSpeed()
    local speed = getSpeed()
    if not speed then return 0 end
    return math.floor(math.abs(speed) * 1 + 0.5)
end

local function getTargetText()
    if not running or not stops[stopIndex] then return "—" end
    return string.format("%.2f", stops[stopIndex])
end

local function getDistanceText()
    if not running or not stops[stopIndex] then return "—" end
    local position = getPosition()
    if not position then return "—" end
    return string.format("%.1f", math.abs(stops[stopIndex] - position))
end

local function drawButton(x, y, w, h, text, accent, enabled)
    local color = enabled and accent or tocolor(45, 48, 56, 220)
    roundedRect(x, y, w, h, 8, color)
    drawText(text, x, y, w, h, tocolor(255, 255, 255, 245), 1.0, "center", "center", "default-bold")
end

local function isInside(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

local function renderJTU()
    if true then return end

    sw, sh = guiGetScreenSize()

    local purple = tocolor(155, 80, 255, 255)
    local purpleBright = tocolor(190, 115, 255, 255)
    local purpleSoft = tocolor(125, 80, 210, 255)
    local bg = tocolor(13, 10, 22, 248)
    local card = tocolor(24, 18, 38, 255)
    local card2 = tocolor(31, 23, 49, 255)
    local muted = tocolor(155, 145, 175, 255)
    local white = tocolor(245, 242, 255, 255)

    -- Shadow / body
    roundedRect(ui.x + 6, ui.y + 8, ui.w, ui.h, 14, tocolor(0, 0, 0, 150))
    roundedRect(ui.x, ui.y, ui.w, ui.h, 14, bg)

    -- Header
    roundedRect(ui.x, ui.y, ui.w, 82, 14, tocolor(25, 17, 42, 255))
    dxDrawRectangle(ui.x, ui.y + 66, ui.w, 16, tocolor(25, 17, 42, 255))
    dxDrawRectangle(ui.x, ui.y + 80, ui.w, 2, purple)

    drawText("JTU BOT", ui.x + 22, ui.y + 9, 190, 32, purpleBright, 1.45, "left", "center", "default-bold")
    drawText("ЖЕЛЕЗНОДОРОЖНАЯ РАБОТА", ui.x + 24, ui.y + 43, 250, 18, muted, 0.78, "left", "center", "default")
    drawText("by xelni_ai", ui.x + 285, ui.y + 25, 125, 24, purpleBright, 0.85, "right", "center", "default-bold")

    local statusColor = phase == "error" and tocolor(255, 70, 90, 255) or (running and not paused and tocolor(180, 110, 255, 255) or tocolor(145, 130, 165, 255))
    drawText("● " .. phaseStatusText(), ui.x + 270, ui.y + 49, 140, 20, statusColor, 0.78, "right", "center", "default-bold")

    -- Status block
    local statusY = ui.y + 100
    roundedRect(ui.x + 18, statusY, ui.w - 36, 82, 9, card)
    drawText("СТАТУС", ui.x + 30, statusY + 9, 100, 18, muted, 0.72, "left", "center", "default-bold")
    drawText(phaseText(), ui.x + 30, statusY + 27, 200, 30, white, 1.05, "left", "center", "default-bold")
    drawText("ЖТУ BOT", ui.x + 30, statusY + 56, 150, 17, purpleBright, 0.72, "left", "center", "default")

    -- Completed work cycles
    drawText(tostring(completedCycles) .. " / " .. tostring(targetCycles), ui.x + 275, statusY + 10, 115, 34, purpleBright, 1.25, "right", "center", "default-bold")
    drawText("ЦИКЛЫ ВЫПОЛНЕНО", ui.x + 235, statusY + 47, 155, 18, muted, 0.66, "right", "center", "default-bold")

    -- Progress details, no route information
    local progress = getProgress()
    local infoY = ui.y + 195
    roundedRect(ui.x + 18, infoY, ui.w - 36, 68, 9, card2)
    drawText("ПРОГРЕСС РАБОТЫ", ui.x + 30, infoY + 8, 180, 18, muted, 0.72, "left", "center", "default-bold")
    drawText(tostring(math.min(stopIndex - 1, #stops)) .. " / " .. tostring(#stops) .. " остановок", ui.x + 30, infoY + 30, 200, 25, white, 0.9, "left", "center", "default-bold")
    drawText(tostring(progress) .. "%", ui.x + 300, infoY + 25, 90, 25, purpleBright, 0.95, "right", "center", "default-bold")
    local barX = ui.x + 235
    local barY = infoY + 14
    local barW = 65
    local barH = 6
    roundedRect(barX, barY, barW, barH, 3, tocolor(47, 35, 65, 255))
    if progress > 0 then
        roundedRect(barX, barY, math.max(4, barW * progress / 100), barH, 3, purple)
    end

    -- Current speed only; no speed limit
    local speedY = ui.y + 275
    roundedRect(ui.x + 18, speedY, 125, 62, 9, card)
    roundedRect(ui.x + 153, speedY, 125, 62, 9, card)
    roundedRect(ui.x + 288, speedY, 125, 62, 9, card)

    drawText("СКОРОСТЬ", ui.x + 28, speedY + 8, 105, 17, muted, 0.68, "left", "center", "default-bold")
    drawText(tostring(getCurrentSpeed()) .. " км/ч", ui.x + 28, speedY + 27, 105, 25, white, 0.92, "left", "center", "default-bold")

    drawText("ОСТАНОВОК", ui.x + 163, speedY + 8, 105, 17, muted, 0.68, "left", "center", "default-bold")
    drawText(tostring(math.min(stopIndex - 1, #stops)) .. "/" .. tostring(#stops), ui.x + 163, speedY + 27, 105, 25, purpleBright, 1.0, "left", "center", "default-bold")

    drawText("ЦИКЛЫ", ui.x + 298, speedY + 8, 105, 17, muted, 0.68, "left", "center", "default-bold")
    drawText("−  " .. tostring(targetCycles) .. "  +", ui.x + 298, speedY + 27, 105, 25, purpleBright, 0.82, "center", "center", "default-bold")

    -- Systems
    local indY = ui.y + 350
    roundedRect(ui.x + 18, indY, 190, 40, 8, card)
    roundedRect(ui.x + 223, indY, 190, 40, 8, card)

    local lightsOn = running and phase ~= "idle"
    local doorsOn = phase == "door"
    local lightsColor = lightsOn and purpleBright or muted
    local doorsColor = doorsOn and tocolor(220, 150, 255, 255) or muted

    drawText("●  ФАРЫ", ui.x + 30, indY, 90, 40, lightsColor, 0.78, "left", "center", "default-bold")
    drawText(lightsOn and "ON" or "OFF", ui.x + 125, indY, 68, 40, muted, 0.72, "right", "center", "default-bold")
    drawText("●  ДВЕРИ", ui.x + 235, indY, 95, 40, doorsColor, 0.78, "left", "center", "default-bold")
    drawText(doorsOn and "OPEN" or "CLOSED", ui.x + 325, indY, 78, 40, muted, 0.66, "right", "center", "default-bold")

    -- Last action
    local logY = ui.y + 400
    roundedRect(ui.x + 18, logY, ui.w - 36, 34, 7, tocolor(17, 12, 28, 255))
    drawText("●  " .. lastLog, ui.x + 29, logY, ui.w - 58, 34, muted, 0.70, "left", "center", "default")

    -- Controls
    local by = ui.y + 444
    local bw = 125
    local bh = 42
    local bgap = 10

    drawButton(ui.x + 18, by, bw, bh, "▶  СТАРТ", purple, not running)
    drawButton(ui.x + 18 + bw + bgap, by, bw, bh, paused and "▶  ПРОДОЛЖИТЬ" or "Ⅱ  ПАУЗА", purpleSoft, running)
    drawButton(ui.x + 18 + (bw + bgap) * 2, by, bw, bh, "■  СТОП", tocolor(115, 55, 130, 255), running)

    drawText("F7 — панель", ui.x + 18, ui.y + 491, 120, 15, tocolor(105, 90, 125, 255), 0.65, "left", "center", "default")
    drawText("F6 — аварийная остановка", ui.x + 245, ui.y + 491, 168, 15, tocolor(105, 90, 125, 255), 0.65, "right", "center", "default")
end

-- addEventHandler("onClientRender", root, renderJTU)

addEventHandler("onClientClick", root, function(button, state, cx, cy)
    if true then return end

    -- Header drag
    if isInside(cx, cy, ui.x, ui.y, ui.w, 82) then
        ui.dragging = true
        ui.dragX = cx - ui.x
        ui.dragY = cy - ui.y
        return
    end

    local speedY = ui.y + 275
    if isInside(cx, cy, ui.x + 288, speedY, 125, 62) then
        if cx < ui.x + 350 then
            setTargetCycles(targetCycles - 1)
        else
            setTargetCycles(targetCycles + 1)
        end
        return
    end

    local by = ui.y + 444
    local bw = 125
    local bh = 42
    local bgap = 10

    if isInside(cx, cy, ui.x + 18, by, bw, bh) then
        startJTU()
    elseif isInside(cx, cy, ui.x + 18 + bw + bgap, by, bw, bh) then
        togglePause()
    elseif isInside(cx, cy, ui.x + 18 + (bw + bgap) * 2, by, bw, bh) then
        stopJTU()
    end
end)

addEventHandler("onClientCursorMove", root, function(_, _, cx, cy)
    if not ui.dragging then return end
    ui.x = cx - ui.dragX
    ui.y = cy - ui.dragY
end)

addEventHandler("onClientClick", root, function(button, state)
    if button == "left" and state == "up" then
        ui.dragging = false
    end
end)

-- ============================================================
-- ОШИБКИ ОЧЕРЕДИ (серверное сообщение скрываем, показываем своё красное)
-- ============================================================

addEventHandler("onClientChatMessage", root, function(message)
    local text = tostring(message or "")

    if text:find("Не удалось начать работу", 1, true)
        or text:find("Займите очередь и ждите", 1, true)
        or text:find("Займите очередь", 1, true) then
        if running then
            cancelEvent()
            failBot("Очередь")
        elseif errorStopped then
            cancelEvent()
        end
    end
end)

-- ============================================================
-- HOTKEYS / LIFECYCLE
-- ============================================================

addCommandHandler("jtu", function()
    startJTU()
end)

-- /jtucycles N — изменить количество циклов (можно во время работы)
addCommandHandler("jtucycles", function(_, value)
    setTargetCycles(value)
end)

bindKey("F6", "down", function()
    if running then
        stopJTU()
    end
end)

-- bindKey("F7", "down", function()
--     panelVisible = not panelVisible
-- end)

setTimer(function()
    movementTick()
    if api.takeCommand then
        local cmd = api.takeCommand()
        while cmd do
            if cmd == "jtu_start" or cmd == "bot:1" then
                startJTU()
            elseif cmd == "jtu_stop" or cmd == "bot:0" then
                stopJTU()
            elseif cmd == "jtu_reset_cycles" then
                completedCycles = 0
            elseif cmd:sub(1, 11) == "jtu_cycles:" then
                setTargetCycles(cmd:sub(12))
            end
            cmd = api.takeCommand()
        end
    end
end, TICK, 0)

setTimer(function()
    if api.updateState then
        api.updateState("jtu_completed", tostring(completedCycles))
        api.updateState("jtu_target", tostring(targetCycles))
        if running then
            api.updateState("status", phaseText())
            api.updateState("bot", "1")
        end
    end
end, 300, 0)

addEventHandler("onClientResourceStart", resourceRoot, function()
    updateUIPosition()
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    running = false
    paused = false
    killAllTimers()
    releaseMovement()
    if isElement(train) then
        setSpeed(0)
    end
end)
