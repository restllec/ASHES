--[[
    TRAMPOLIVBOT
    DarkFlame / MTA:SA

    /tramp - запуск
    ПРОБЕЛ - пауза / продолжить
    F7    - показать/скрыть панель

    Количество циклов выбирается в панели (1–99).

    В чат выводится только:
      - "Бот запущен" и "Бот by xelni_ai"
      - "Бот закончил работу"
      - красная ошибка "Что-то пошло не так. Завершил работу"
]]

local api = {
    triggerServerEvent = dfTriggerServerEvent,
    catchServerEvent = dfCatchServerEvent,
    emulateKey = dfEmulateKey,
    alert = dfPlayAlertSignal,
    log = dfTramLog,
    takeCommand = dfTramTakeCommand,
    updateState = dfTramUpdate,
}

local STOP_DISTANCE = 3
local STOP_WAIT = 0
local DOOR_WAIT = 500 -- стоянка на метке, мс
local KEY_PRESS_TIME = 150
local TICK = 50
local EXIT_LEFT_TIME = 3500
local NEXT_CYCLE_GONE_WAIT = 8000 -- сколько ждать исчезновения трамвая после маршрута, мс
local NEXT_CYCLE_DELAY = 2000     -- пауза перед новым трудоустройством, мс
local MAX_HIRE_RETRIES = 3        -- повторы трудоустройства между циклами, если не посадило
local hireRetries = 0

-- Профили торможения скопированы из TramBot.lua; расстояния в метрах.
local BRAKE_SPEEDS = { 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65 }
local BRAKE_PROFILES = {
    privolzhsk = { 2.74, 5.67, 9.98, 15.49, 22.18, 30.00, 39.15, 49.21, 60.46, 60.44, 60.48, 60.45 },
    neva100 = { 4.16, 9.28, 16.36, 25.37, 36.27, 49.01, 63.58, 79.91, 98.07, 98.01, 98.08, 98.05 },
    mirka3_gray = { 2.61, 5.65, 10.03, 15.47, 22.16, 30.02, 39.03, 49.20, 60.46, 60.45, 60.44, 60.45 },
    mirka8_gray = { 3.36, 7.51, 13.18, 20.47, 29.28, 39.67, 51.46, 64.75, 79.47, 79.50, 79.48, 79.46 },
    mirka3_red = { 2.74, 5.67, 9.98, 15.49, 22.18, 30.00, 39.15, 49.21, 60.46, 60.44, 60.48, 60.45 },
    mirka8_red = { 2.53, 5.64, 9.99, 15.56, 22.16, 30.04, 39.02, 49.17, 60.49, 60.49, 60.50, 60.32 },
    mirka3_blue = { 2.56, 5.65, 9.97, 15.48, 22.18, 30.05, 39.02, 49.19, 60.48, 60.49, 60.48, 60.46 },
    mirka8_blue_pair = { 3.37, 7.47, 13.17, 20.46, 29.27, 39.62, 51.45, 64.79, 79.46, 79.48, 79.49, 79.48 },
    mirka8_blue_single = { 2.59, 5.64, 9.96, 15.44, 22.11, 30.26, 38.99, 49.22, 60.36, 60.38, 60.39, 60.40 },
    priva_611 = { 2.66, 5.65, 9.96, 15.46, 22.16, 30.03, 39.05, 49.22, 60.49, 60.43, 60.44, 60.44 },
    priva_604 = { 2.61, 5.65, 9.99, 15.50, 22.19, 30.05, 39.05, 49.20, 60.65, 60.65, 60.55, 60.48 },
}
local activeBrakeProfile = BRAKE_PROFILES.privolzhsk
local brakeDistance = 2 -- запас (м) к тормозному пути (1–15), меняется в панели (в TramBot у маркера запас 2 м)
local CREEP_SPEED = 0.03 -- если почти встали, не доехав до метки — подталкиваем на W
local autoDistance = true
local distanceMode = 2 -- 1: 1 ваг (~18м), 2: 2 ваг (~36м), 3: 3 ваг (~54м)
local distanceHold = false

-- Маршрут: { позиция на пути, тип, состояние }
--   "marker"  — остановка (стоим DOOR_WAIT)
--   "traffic" — светофор: едем, если getTrafficLightState() == состояние, иначе стоим и ждём
-- Записывается через stops_recorder.lua
local ROUTES = {
    current = {
    -- остановка 1
    { 6487.3852539062,  "marker",  "228" },

    -- светофоры до остановки 2
    { 8249.365234375,   "traffic", "3" },
    { 8448.3583984375,  "traffic", "3" },
    { 8701.7685546875,  "traffic", "3" },
    { 9157.95,          "traffic", "3" }, -- было 9177.11
    { 9426.359375,      "traffic", "3" },
    { 9749.7705078125,  "traffic", "3" },
    { 10038.075195312,  "traffic", "3" },
    { 10217.3046875,    "traffic", "0" },

    -- остановка 2
    { 10456.916992188,  "marker",  "228" },

    -- светофоры до остановки 3
    { 10720.59765625,   "traffic", "3" },
    { 11077.209960938,  "traffic", "3" },

    -- остановка 3
    { 12127.993164062,  "marker",  "228" },

    -- светофоры после круга пути (позиция обнуляется) до остановки 4
    { 593.43786621094,  "traffic", "0" },
    { 859.35272216797,  "traffic", "3" },
    { 1139.6849365234,  "traffic", "3" },
    { 1463.3033447266,  "traffic", "3" },
    { 1709.1480712891,  "traffic", "3" },
    { 2191.8205566406,  "traffic", "3" },
    { 2451.9875488281,  "traffic", "0" },
    { 2635.9738769531,  "traffic", "3" },

    -- остановка 4 (конец цикла)
    { 4647.869140625,   "marker",  "228" }, -- +4 м вперёд от исходной 4643.87
    },
    mirny = {
        { 108.39768981934,  "traffic", "0" },
        { 302.02429199219,  "traffic", "3" },
        { 583.02099609375,  "marker",  "228" },
        { 594.45471191406,  "traffic", "3" },
        { 875.43237304688,  "traffic", "0" },
        { 1377.5609130859,  "traffic", "0" },
        { 3915.1696777344,  "marker",  "228" },
        { 6431.6264648438,  "traffic", "0" },
        { 6929.5913085938,  "traffic", "0" },
        { 7402.6162109375,  "marker",  "228" },
        { 8051.578125,      "marker",  "228" }, -- +4 м вперёд от последней метки
    },
    nevsky = {
        { 3283.8566894531,  "traffic", "3" },
        { 4694.982421875,   "traffic", "3" },
        { 4893.3442382812,  "marker",  "228" },
        { 4955.994140625,   "traffic", "3" },
        { 5213.7729492188,  "traffic", "3" },
        { 5307.2172851562,  "traffic", "3" },
        { 5621.7250976562,  "traffic", "0" },
        { 6202.3178710938,  "traffic", "0" },
        { 6558.4243164062,  "traffic", "3" },
        { 6840.1674804688,  "marker",  "228" },
        { 7216.3764648438,  "traffic", "3" },
        { 7574.3388671875,  "traffic", "0" },
        { 8162.9995117188,  "traffic", "0" },
        { 8458.2763671875,  "traffic", "0" },
        { 8720.7685546875,  "traffic", "3" },
        { 9052.3740234375,  "traffic", "3" },
        { 9266.984375,      "traffic", "3" },
        { 9657.6708984375,  "marker",  "228" },
        { 9862.5625,        "traffic", "3" },
        { 10087.12890625,   "traffic", "3" },
        { 10420.078125,     "traffic", "3" },
        { 10665.662109375,  "traffic", "3" },
        { 46.816543579102,  "traffic", "3" },
        { 302.20642089844,  "traffic", "3" },
        { 392.16604614258,  "marker",  "228" },
        { 564.005859375,    "traffic", "3" },
        { 1967.1325683594,  "traffic", "3" },
        { 2183.2033691406,  "marker",  "228" }, -- +4 м вперёд от последней метки
    },
}

local routeNames = { current = "Приволжск", mirny = "Мирный", nevsky = "Невский" }
local routeIds = { "current", "mirny", "nevsky" }
local routeJobs = {
    current = { depot = 1, line = 5 },
    mirny = { depot = 2, line = 5 },
    nevsky = { depot = 3, line = 5 },
}
local selectedRoute = "current"
local stops = ROUTES[selectedRoute]

local function selectBrakeProfile(vehicle)
    local model = isElement(vehicle) and getElementModel(vehicle)
    local profile, profileName
    if model == 476 then
        profile, profileName = BRAKE_PROFILES.neva100, "Нева-100"
    elseif model == 572 then
        profile = selectedRoute == "mirny" and BRAKE_PROFILES.mirka3_gray or BRAKE_PROFILES.mirka8_gray
        profileName = selectedRoute == "mirny" and "Мирка-3 серая" or "Мирка-8 серая"
    elseif model == 577 then
        profile = selectedRoute == "mirny" and BRAKE_PROFILES.mirka3_red or BRAKE_PROFILES.mirka8_red
        profileName = selectedRoute == "mirny" and "Мирка-3 красная" or "Мирка-8 красная"
    elseif model == 18593 then
        local towed = getVehicleTowedByVehicle(vehicle)
        if selectedRoute == "mirny" then
            profile = towed and BRAKE_PROFILES.mirka8_blue_pair or BRAKE_PROFILES.mirka8_blue_single
            profileName = towed and "Мирка-8 сине-зелёная, 2 вагона" or "Мирка-8 сине-зелёная"
        else
            profile, profileName = BRAKE_PROFILES.mirka3_blue, "Мирка-3 сине-зелёная"
        end
    elseif model == 583 then
        profile, profileName = BRAKE_PROFILES.privolzhsk, "Прива-7"
    elseif model == 611 then
        profile, profileName = BRAKE_PROFILES.priva_611, "Прива-7 тип 2"
    elseif model == 604 then
        profile, profileName = BRAKE_PROFILES.priva_604, "Прива-2/8"
    else
        profile, profileName = BRAKE_PROFILES.privolzhsk, "Прива-7 (по умолчанию)"
    end
    activeBrakeProfile = profile
    notify("Профиль торможения: " .. profileName .. ", модель " .. tostring(model or "?"))
end

-- Подготовка после посадки в трамвай: пауза -> свет (L) -> поливалка путей (2) -> движение
local PREP_DELAY_MIN = 3000   -- пауза после посадки, мс
local PREP_DELAY_MAX = 4000
local LIGHT_PRESSES = 2       -- сколько раз нажать L (как в TramBot: L, L)
local SEAT_TIMEOUT = 15000    -- сколько ждать посадки в трамвай, мс

local POSITION_TO_KMH = 3.6       -- перевод "единиц пути в секунду" в км/ч (1 ед. = 1 м). Проверить: /trampspeed

local TRAFFIC_TIMEOUT = 20000  -- как в TramBot: если светофор не меняется 20 с, едем дальше
local ignoreTraffic = false    -- игнорировать светофоры: сервер сам включает ночной режим (00:00–10:00), как в TramBot
local NIGHT_FROM, NIGHT_TO = 0, 10 -- часы по МОСКОВСКОМУ времени, когда светофоры не работают

-- Трудоустройство: Tram:onJobAccepted(root province_tram, 1, 5)
local HIRE_RESOURCE = "province_tram"
local HIRE_EVENT = "Tram:onJobAccepted"
local HIRE_ARG_1 = 1
local HIRE_ARG_2 = 5
local HIRE_AT_START = true   -- устраиваться на работу и при запуске бота (false = только между циклами)

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
local panelVisible = true
local lastLog = "Ожидание запуска"
local routeStartedAt = 0
local sprinklerOn = false
local sprinklerArmed = false
local sprinklerPausedBySlowdown = false
local slowSince = nil
local speedKmh = 0
local lastSamplePos = nil
local lastSampleTick = 0
local lastPointDistance = nil
local lastPointIndex = nil
local lastTrafficLog = nil
local rawPerKmh = 1 / 180 -- сколько единиц getTrainSpeed в 1 км/ч (уточняется на ходу)
local brakeV0 = nil
local brakeD0 = nil

-- Настройки циклов
local targetCycles = 1
local completedCycles = 0
local errorStopped = false

-- ============================================================
-- HELPERS
-- ============================================================

local DEBUG = true -- пишет каждый шаг бота в консоль (F8)
local autoLights = false
local silentChat = false

local function log(text)
    lastLog = tostring(text)
    if DEBUG then outputConsole("[TramPolivBot] " .. tostring(text)) end
    if api.log then api.log("[TramPolivBot] " .. tostring(text)) end
end

local function notify(text, nType)
    if dfPushToast then
        dfPushToast("Поливалка", tostring(text), nType or "info")
    end
    log(text)
end

local function chatInfo(text)
    notify(text, "info")
end

local function chatError(text)
    notify(text, "error")
end

local function setBrakeDistance(value)
    value = tonumber(value) or 10
    brakeDistance = math.max(1, math.min(15, math.floor(value)))
end

local function setDistanceMode(value)
    distanceMode = math.max(1, math.min(3, math.floor(tonumber(value) or 2)))
    notify("Дистанция: " .. tostring(distanceMode) .. " вагон(а)")
end

local function setRoute(routeId)
    if running or not ROUTES[routeId] then return false end
    selectedRoute = routeId
    stops = ROUTES[routeId]
    local vehicle = getPedOccupiedVehicle(localPlayer)
    if isElement(vehicle) and getVehicleType(vehicle) == "Train" then
        selectBrakeProfile(vehicle)
    elseif routeId == "nevsky" then
        activeBrakeProfile = BRAKE_PROFILES.neva100
    elseif routeId == "mirny" then
        activeBrakeProfile = BRAKE_PROFILES.mirka3_red
    else
        activeBrakeProfile = BRAKE_PROFILES.privolzhsk
    end
    stopIndex = 1
    phase = "idle"
    lastPointDistance, lastPointIndex = nil, nil
    notify("Маршрут: " .. routeNames[routeId])
    return true
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

local function checkTrainDistance()
    if not autoDistance then return nil end
    local veh = findTrain()
    if not isElement(veh) then return nil end
    local myX, myY, myZ = getElementPosition(veh)
    local _, _, myRotZ = getElementRotation(veh)
    local rad = math.rad(myRotZ)
    local fwdX = -math.sin(rad)
    local fwdY = math.cos(rad)

    local closestDist = 999
    local found = false
    local vehicles = getElementsByType("vehicle")
    for _, other in ipairs(vehicles) do
        if other ~= veh and isElement(other) and getVehicleType(other) == "Train" then
            local tx, ty, tz = getElementPosition(other)
            local dx = tx - myX
            local dy = ty - myY
            local dist = math.sqrt(dx * dx + dy * dy)
            if dist <= 65 and dist > 1.0 then
                local dot = dx * fwdX + dy * fwdY
                if dot > 0 then
                    local _, _, tgtRotZ = getElementRotation(other)
                    local diffAngle = math.abs((myRotZ - tgtRotZ + 180) % 360 - 180)
                    if diffAngle <= 60 then
                        if dist < closestDist then
                            closestDist = dist
                            found = true
                        end
                    end
                end
            end
        end
    end
    if found then
        return closestDist
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

    chatError("Что-то пошло не так. Завершил работу")
    if reason then log("Причина: " .. tostring(reason)) end -- последней, чтобы осталась в панели
end

local function phaseText()
    if not running then
        if phase == "finished" then return "РАБОТА ЗАВЕРШЕНА" end
        if phase == "error" then return "ОШИБКА" end
        return "ГОТОВ"
    end
    if paused then return "ПАУЗА" end
    if phase == "moving" then return "В ПУТИ" end
    if phase == "traffic" then return "СВЕТОФОР" end
    if phase == "stopping" then return "ОСТАНОВКА" end
    if phase == "door" then return "ОСТАНОВКА" end
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
    if not running or not autoLights then return end
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
        local waitedGone = 0
        stopTimer = setTimer(function()
            if errorStopped then return end
            if completedCycles >= targetCycles then
                stopTimer = stopTimerSafe(stopTimer)
                finishAll()
                return
            end

            -- ждём, пока трамвай исчезнет / игрок выйдет из него (макс. NEXT_CYCLE_GONE_WAIT мс)
            local vehicle = getPedOccupiedVehicle(localPlayer)
            local stillInTram = isElement(vehicle) and getVehicleType(vehicle) == "Train"
            waitedGone = waitedGone + 500
            if stillInTram and waitedGone < NEXT_CYCLE_GONE_WAIT then return end

            stopTimer = stopTimerSafe(stopTimer)
            stopTimer = setTimer(function()
                if errorStopped then return end
                stopIndex = 1
                phase = "starting"
                paused = false
                running = true
                hireRetries = 0
                lastPointDistance, lastPointIndex = nil, nil
                notify("Начинаем цикл " .. tostring(completedCycles + 1) .. "/" .. tostring(targetCycles) .. ".")
                startJob()
            end, NEXT_CYCLE_DELAY, 1)
        end, 500, 0)
        return
    end

    finishAll()
end

local function openDoors()
    if not running or paused then return end

    phase = "door"
    fullStop()
    notify("Остановка " .. tostring(stopIndex) .. "/" .. tostring(#stops) .. " — стоим на метке")

    -- двери не открываем: просто стоим DOOR_WAIT

    doorTimer = setTimer(function()
        if not running or paused then return end

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

local function trafficGreen(expected)
    if type(getTrafficLightState) ~= "function" then return true end
    return tonumber(getTrafficLightState()) == tonumber(expected)
end

local trafficStopTick = 0

-- Перейти к следующей точке маршрута (без открытия дверей)
local function advancePoint()
    brakeV0, brakeD0 = nil, nil
    lastPointDistance = nil
    stopIndex = stopIndex + 1
    if stopIndex > #stops then
        finishRoute()
        return
    end
    phase = "moving"
end

local function arriveAtTraffic()
    if phase ~= "moving" then return end

    phase = "traffic"
    brakeV0, brakeD0 = nil, nil
    trafficStopTick = getTickCount()
    fullStop()
    notify("Светофор " .. tostring(stopIndex) .. "/" .. tostring(#stops) .. " — ждём зелёный")
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

-- Тормозной путь по таблице TramBot (линейная интерполяция, как brakeDistance() в TramBot)
local function tableBrakeDistance(speedRaw)
    local speed = math.abs(tonumber(speedRaw) or 0)
    local distances = activeBrakeProfile
    if speed <= 0 then return 0 end
    if speed <= BRAKE_SPEEDS[1] then
        return distances[1] * speed / BRAKE_SPEEDS[1]
    end
    for i = 2, #BRAKE_SPEEDS do
        if speed <= BRAKE_SPEEDS[i] then
            local s0, s1 = BRAKE_SPEEDS[i - 1], BRAKE_SPEEDS[i]
            local d0, d1 = distances[i - 1], distances[i]
            return d0 + (speed - s0) * (d1 - d0) / (s1 - s0)
        end
    end
    return distances[#distances]
end

-- С какого расстояния начинать тормозить: тормозной путь TramBot + запас из панели
local function brakeTriggerDistance()
    return tableBrakeDistance(getSpeed() or 0) + brakeDistance
end

local function movementTick()
    if not running or paused then return end

    -- Стоим на светофоре: ждём зелёный (или таймаут)
    if phase == "traffic" then
        local point = stops[stopIndex]
        if not point then
            finishRoute()
            return
        end

        fullStop()

        local timeout = getTickCount() - trafficStopTick > TRAFFIC_TIMEOUT
        if ignoreTraffic or trafficGreen(point[3]) or timeout then
            notify(timeout and "Светофор: таймаут, едем дальше" or "Светофор: зелёный, едем")
            advancePoint()
        end
        return
    end

    if phase ~= "moving" then return end

    local vehicle = findTrain()
    if not isElement(vehicle) then
        if stopIndex >= #stops then
            -- сервер убрал трамвай на конечной остановке — это конец маршрута, не ошибка
            notify("Трамвай исчез на конечной — маршрут завершён")
            finishRoute()
        else
            failBot("Транспорт не найден")
        end
        return
    end

    local position = getPosition()
    if not position then return end

    if autoDistance then
        local aheadDist = checkTrainDistance()
        if aheadDist then
            -- Целевой зазор: примерно длина одного/двух/трёх вагонов.
            -- Тормозим до него заранее, учитывая текущий тормозной путь.
            local safeGap = (distanceMode == 1 and 18) or (distanceMode == 3 and 54) or 36
            local brakePath = tableBrakeDistance(getSpeed() or 0)
            local brakeStart = safeGap + brakePath + 3
            local coastStart = safeGap + 10

            if aheadDist <= safeGap then
                -- Держим состав неподвижно только пока впереди действительно стоит поезд.
                distanceHold = true
                keyUp("W")
                keyUp("S")
                setSpeed(0)
                return
            elseif aheadDist <= brakeStart then
                -- Плавно снижаем скорость по оставшемуся зазору, не включая S.
                -- S на трамвае может дать задний ход, поэтому ограничиваем скорость напрямую.
                local currentSpeed = math.abs(getSpeed() or 0)
                local availableGap = math.max(0, aheadDist - safeGap)
                local brakingRatio = math.min(1, availableGap / math.max(1, brakePath + 3))
                local targetSpeed = currentSpeed * brakingRatio
                keyUp("W")
                keyUp("S")
                if targetSpeed < currentSpeed - 0.01 then
                    setSpeed(math.max(0.02, targetSpeed))
                end
                return
            elseif aheadDist <= coastStart then
                -- Накат: тяга отпущена, но поезд не тормозим резко.
                keyUp("W")
                keyUp("S")
                return
            elseif distanceHold then
                -- Передний поезд ушёл: снимаем удержание и снова трогаемся вперёд.
                distanceHold = false
                keyUp("S")
                keyDown("W")
                return
            end
        end
    end

    local point = stops[stopIndex]
    if not point then
        finishRoute()
        return
    end

    local target, kind, state = point[1], point[2], point[3]
    local distance = math.abs(target - position)
    local trigger = brakeTriggerDistance()

    if kind == "traffic" then
        if ignoreTraffic then
            advancePoint()
            return
        end

        local green = trafficGreen(state)

        -- отладка: что бот видит на светофоре (консоль F8)
        if distance <= trigger then
            local signature = tostring(stopIndex) .. ":" .. tostring(green)
            if signature ~= lastTrafficLog then
                lastTrafficLog = signature
                log(string.format("Светофор %d/%d: сейчас=%s, нужно=%s, до него %.0f м -> %s",
                    stopIndex, #stops, tostring(type(getTrafficLightState) == "function" and getTrafficLightState() or "?"),
                    tostring(state), distance, green and "ЗЕЛЁНЫЙ, едем" or "НЕ ЗЕЛЁНЫЙ, тормозим"))
            end
        end

        -- проскочили светофор (расстояние начало расти): идём к следующей точке
        if lastPointIndex == stopIndex and lastPointDistance
            and lastPointDistance <= 25 and distance > lastPointDistance + 1 then
            advancePoint()
            return
        end
        lastPointIndex = stopIndex
        lastPointDistance = distance

        if distance <= STOP_DISTANCE then
            if green then
                advancePoint()
            else
                arriveAtTraffic()
            end
            return
        end

        -- зелёный: едем без торможения. Решение пересматривается каждый тик,
        -- поэтому если загорится красный на подъезде — начнём тормозить сразу.
        if green then
            keyUp("S")
            keyDown("W")
            return
        end
    elseif distance <= STOP_DISTANCE then
        arriveAtStop()
        return
    end

    -- Торможение как в TramBot: в зоне тормозного пути зажимаем S
    if distance <= trigger then
        local speedRaw = math.abs(getSpeed() or 0)
        if speedRaw <= CREEP_SPEED and distance > STOP_DISTANCE then
            if kind == "traffic" and distance <= STOP_DISTANCE + 6 then
                -- встали у светофора чуть раньше — считаем, что приехали
                arriveAtTraffic()
                return
            end
            keyUp("S")
            keyDown("W") -- встали, не доехав до метки — доползаем
        else
            keyUp("W")
            keyDown("S")
        end
        return
    end

    keyUp("S")
    keyDown("W")
end

-- ============================================================
-- ПОЛИВ: отключаем при замедлении (другие трамваи на пути)
-- ============================================================

-- Скорость считаем по смещению на пути, чтобы не зависеть от единиц getTrainSpeed
local function sampleSpeed()
    local position = getPosition()
    local now = getTickCount()

    if position and lastSamplePos then
        local dt = (now - lastSampleTick) / 1000
        local delta = math.abs(position - lastSamplePos)
        if dt > 0 and delta < 50 then -- большой скачок = круг пути замкнулся, пропускаем
            local raw = delta / dt * POSITION_TO_KMH
            speedKmh = speedKmh * 0.6 + raw * 0.4

            -- уточняем перевод единиц getTrainSpeed в км/ч
            local rawSpeed = math.abs(getSpeed() or 0)
            if speedKmh > 10 and rawSpeed > 0 then
                rawPerKmh = rawPerKmh * 0.9 + (rawSpeed / speedKmh) * 0.1
            end
        end
    end

    lastSamplePos = position
    lastSampleTick = now
end

-- Автоотключение/автовключение полива при снижении скорости убрано:
-- полив включается один раз при старте и дальше не трогается.
local function sprinklerTick()
    sampleSpeed()
end

-- ============================================================
-- JOB START / STOP / PAUSE
-- ============================================================

-- Принять работу и поехать
local function startPreparation()
    if not running then return end

    local delay = math.random(PREP_DELAY_MIN, PREP_DELAY_MAX)
    notify("Сели в трамвай, подготовка...")

    -- 1) через 3-4 секунды включаем свет (L)
    headlightTimer1 = setTimer(function()
        if not running then return end

        if autoLights then
            for n = 1, LIGHT_PRESSES do
                setTimer(function()
                    if not running then return end
                    pressKey("L")
                end, (n - 1) * 300, 1)
            end
        end

        -- 2) затем поливалка путей (2)
        headlightTimer2 = setTimer(function()
            if not running then return end
            pressKey("2")
            sprinklerOn = true
            sprinklerArmed = false
            sprinklerPausedBySlowdown = false
            slowSince = nil

            -- 3) и только потом начинаем движение
            stopTimer = setTimer(function()
                if not running then return end
                phase = "moving"
                notify("Начинаем маршрут.")
            end, 800, 1)
        end, (LIGHT_PRESSES - 1) * 300 + 600, 1)
    end, delay, 1)
end

local function acceptJob(hire)
    if not running then return end

    phase = "starting"
    train = nil -- после трудоустройства состав может смениться, ищем заново
    log("acceptJob: hire=" .. tostring(hire))

    if hire then
        local resource = getResourceFromName(HIRE_RESOURCE)
        if not resource then
            failBot(HIRE_RESOURCE .. " не найден")
            return
        end

        local root = getResourceRootElement(resource)
        if not isElement(root) then
            failBot("root " .. HIRE_RESOURCE .. " не найден")
            return
        end

        if type(api.triggerServerEvent) ~= "function" then
            failBot("dfTriggerServerEvent недоступен")
            return
        end

        -- Для каждого маршрута используется своё трудоустройство.
        local job = routeJobs[selectedRoute] or { depot = HIRE_ARG_1, line = HIRE_ARG_2 }
        HIRE_ARG_1, HIRE_ARG_2 = job.depot, job.line
        local ok, err = pcall(api.triggerServerEvent, HIRE_EVENT, root, HIRE_ARG_1, HIRE_ARG_2)
        if not ok then
            failBot("не удалось отправить " .. HIRE_EVENT .. ": " .. tostring(err))
            return
        end
        notify("Отправлено " .. HIRE_EVENT .. " (" .. tostring(HIRE_ARG_1) .. ", " .. tostring(HIRE_ARG_2) .. "), ждём посадки...")
    end

    -- ждём, пока персонаж окажется в трамвае
    local waited = 0
    jobTimer = setTimer(function()
        if not running then return end

        local vehicle = getPedOccupiedVehicle(localPlayer)
        if isElement(vehicle) and getVehicleType(vehicle) == "Train" then
            jobTimer = stopTimerSafe(jobTimer)
            train = vehicle
            startPreparation()
            return
        end

        waited = waited + 250
        if waited >= SEAT_TIMEOUT then
            jobTimer = stopTimerSafe(jobTimer)
            if completedCycles > 0 and hireRetries < MAX_HIRE_RETRIES then
                hireRetries = hireRetries + 1
                notify("Не посадило в трамвай, повтор " .. tostring(hireRetries) .. "/" .. tostring(MAX_HIRE_RETRIES))
                acceptJob(true)
            else
                failBot("Не удалось сесть в трамвай")
            end
        end
    end, 250, 0)
end

startJob = function()
    -- после выполнения всех остановок (цикл 2 и далее) бот сам устраивается на работу
    acceptJob(completedCycles > 0 or HIRE_AT_START)
end

-- Московское время (UTC+3) — не игровое и не по часовому поясу компьютера
local MSK_OFFSET = 3
local lastNightState = nil

local function getMoscowTime()
    if type(getRealTime) ~= "function" then return nil end
    local t = getRealTime()
    if type(t) ~= "table" or type(t.timestamp) ~= "number" then return nil end
    -- timestamp — unix-время в UTC, поэтому часовой пояс компьютера не важен
    local secondsOfDay = (t.timestamp + MSK_OFFSET * 3600) % 86400
    local hour = math.floor(secondsOfDay / 3600)
    local minute = math.floor((secondsOfDay % 3600) / 60)
    return hour, minute
end

local function isNightMsk()
    local hour = getMoscowTime()
    if not hour then return nil end
    return hour >= NIGHT_FROM and hour < NIGHT_TO
end

-- force = применить сразу; иначе только когда ночь/день сменились (чтобы не перебивать ручной выбор)
function applyTrafficByTime(force)
    local night = isNightMsk()
    if night == nil then return end
    if force or night ~= lastNightState then
        ignoreTraffic = night
        local hour, minute = getMoscowTime()
        log(string.format("МСК %02d:%02d: светофоры %s", hour, minute, night and "игнорируем (ночь)" or "учитываем"))
    end
    lastNightState = night
end

function startJTU()
    if running then
        notify("Бот уже запущен.")
        return
    end

    local vehicle = getPedOccupiedVehicle(localPlayer)
    if isElement(vehicle) and getVehicleType(vehicle) == "Train" then
        train = vehicle
        selectBrakeProfile(vehicle)
        stopIndex = 1
        phase = "moving"
        paused = false
        running = true
        errorStopped = false
        completedCycles = 0
        routeStartedAt = getTickCount()
        hireRetries = 0
        brakeV0, brakeD0 = nil, nil
        lastPointDistance, lastPointIndex = nil, nil
        chatInfo("Бот запущен")
        applyTrafficByTime(true)
        startPreparation()
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
    hireRetries = 0
    brakeV0, brakeD0 = nil, nil
    lastPointDistance, lastPointIndex = nil, nil

    chatInfo("Бот запущен")
    chatInfo("Бот by xelni_ai")

    -- режим светофоров по московскому времени (дальше его переключает сообщение сервера)
    applyTrafficByTime(true)
    log("startJTU: запускаем startJob")

    local ok, err = pcall(startJob)
    if not ok then
        failBot("ошибка в startJob: " .. tostring(err))
    end
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
    h = 700,
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
    return string.format("%.2f", stops[stopIndex][1])
end

local function getDistanceText()
    if not running or not stops[stopIndex] then return "—" end
    local position = getPosition()
    if not position then return "—" end
    return string.format("%.1f", math.abs(stops[stopIndex][1] - position))
end

local function drawButton(x, y, w, h, text, accent, enabled)
    local color = enabled and accent or tocolor(35, 35, 35, 220)
    roundedRect(x, y, w, h, 8, color)
    drawText(text, x, y, w, h, enabled and tocolor(0, 0, 0, 255) or tocolor(120, 120, 120, 255), 1.0, "center", "center", "default-bold")
end

local function isInside(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

local function renderJTU()
    do return end

    sw, sh = guiGetScreenSize()

    local purple = tocolor(240, 240, 240, 255)       -- основной акцент (белый)
    local purpleBright = tocolor(255, 255, 255, 255)
    local purpleSoft = tocolor(185, 185, 185, 255)
    local bg = tocolor(0, 0, 0, 248)
    local card = tocolor(20, 20, 20, 255)
    local card2 = tocolor(30, 30, 30, 255)
    local muted = tocolor(150, 150, 150, 255)
    local white = tocolor(255, 255, 255, 255)
    local headerBg = tocolor(10, 10, 10, 255)

    -- Shadow / body
    roundedRect(ui.x + 6, ui.y + 8, ui.w, ui.h, 14, tocolor(0, 0, 0, 150))
    roundedRect(ui.x, ui.y, ui.w, ui.h, 14, bg)

    -- Header
    roundedRect(ui.x, ui.y, ui.w, 82, 14, headerBg)
    dxDrawRectangle(ui.x, ui.y + 66, ui.w, 16, headerBg)
    dxDrawRectangle(ui.x, ui.y + 80, ui.w, 2, purple)

    drawText("TRAMPOLIVBOT", ui.x + 22, ui.y + 9, 260, 32, purpleBright, 1.45, "left", "center", "default-bold")
    drawText("ЖЕЛЕЗНОДОРОЖНАЯ РАБОТА", ui.x + 24, ui.y + 43, 250, 18, muted, 0.78, "left", "center", "default")
    drawText("by xelni_ai", ui.x + 310, ui.y + 25, 100, 24, purpleBright, 0.85, "right", "center", "default-bold")

    local statusColor = phase == "error" and tocolor(255, 255, 255, 255) or (running and not paused and tocolor(255, 255, 255, 255) or tocolor(130, 130, 130, 255))
    drawText("● " .. phaseStatusText(), ui.x + 270, ui.y + 49, 140, 20, statusColor, 0.78, "right", "center", "default-bold")

    -- Status block
    local statusY = ui.y + 100
    roundedRect(ui.x + 18, statusY, ui.w - 36, 82, 9, card)
    drawText("СТАТУС", ui.x + 30, statusY + 9, 100, 18, muted, 0.72, "left", "center", "default-bold")
    drawText(phaseText(), ui.x + 30, statusY + 27, 200, 30, white, 1.05, "left", "center", "default-bold")
    drawText("TRAMPOLIVBOT", ui.x + 30, statusY + 56, 150, 17, purpleBright, 0.72, "left", "center", "default")

    -- Completed work cycles
    drawText(tostring(completedCycles) .. " / " .. tostring(targetCycles), ui.x + 275, statusY + 10, 115, 34, purpleBright, 1.25, "right", "center", "default-bold")
    drawText("ЦИКЛЫ ВЫПОЛНЕНО", ui.x + 235, statusY + 47, 155, 18, muted, 0.66, "right", "center", "default-bold")

    -- Progress details, no route information
    local progress = getProgress()
    local infoY = ui.y + 195
    roundedRect(ui.x + 18, infoY, ui.w - 36, 68, 9, card2)
    drawText("ПРОГРЕСС РАБОТЫ", ui.x + 30, infoY + 8, 180, 18, muted, 0.72, "left", "center", "default-bold")
    drawText(tostring(progress) .. "%", ui.x + 30, infoY + 30, 100, 25, white, 0.95, "left", "center", "default-bold")
    local barX = ui.x + 120
    local barY = infoY + 40
    local barW = ui.w - 36 - 120 + 18 - 30 + 12
    local barH = 6
    roundedRect(barX, barY, barW, barH, 3, tocolor(55, 55, 55, 255))
    if progress > 0 then
        roundedRect(barX, barY, math.max(4, barW * progress / 100), barH, 3, purple)
    end

    -- Route selector
    local routeY = ui.y + 275
    roundedRect(ui.x + 18, routeY, ui.w - 36, 42, 9, card)
    drawText("МАРШРУТ", ui.x + 30, routeY + 3, 100, 18, muted, 0.68, "left", "center", "default-bold")
    drawText(routeNames[selectedRoute], ui.x + 130, routeY + 3, 135, 18, white, 0.82, "left", "center", "default-bold")
    drawButton(ui.x + 278, routeY + 7, 60, 28, "‹", purpleSoft, not running)
    drawButton(ui.x + 346, routeY + 7, 60, 28, ">", purpleSoft, not running)
    local routePosition = 1
    for i, id in ipairs(routeIds) do
        if id == selectedRoute then routePosition = i break end
    end
    drawText(tostring(routePosition) .. "/" .. tostring(#routeIds), ui.x + 338, routeY + 3, 8, 18, muted, 0.6, "center", "center", "default")

    -- Current speed only; no speed limit
    local speedY = ui.y + 330
    roundedRect(ui.x + 18, speedY, 190, 62, 9, card)
    roundedRect(ui.x + 223, speedY, 190, 62, 9, card)

    drawText("ЗАПАС ТОРМОЖ.", ui.x + 30, speedY + 8, 160, 17, muted, 0.68, "left", "center", "default-bold")
    drawText("−  " .. tostring(brakeDistance) .. " м  +", ui.x + 18, speedY + 27, 190, 25, purpleBright, 0.82, "center", "center", "default-bold")

    drawText("ЦИКЛЫ", ui.x + 235, speedY + 8, 160, 17, muted, 0.68, "left", "center", "default-bold")
    drawText("−  " .. tostring(targetCycles) .. "  +", ui.x + 223, speedY + 27, 190, 25, purpleBright, 0.82, "center", "center", "default-bold")

    -- Distance mode selector
    local distY = ui.y + 405
    roundedRect(ui.x + 18, distY, ui.w - 36, 40, 8, card)
    drawText("ДИСТАНЦИЯ", ui.x + 30, distY + 2, 120, 24, muted, 0.72, "left", "center", "default-bold")
    local distLabel = (distanceMode == 1 and "1 вагон (~18 м)") or (distanceMode == 3 and "3 вагона (~54 м)") or "2 вагона (~36 м)"
    drawText(distLabel, ui.x + 155, distY + 2, 150, 24, white, 0.72, "center", "center", "default-bold")
    drawButton(ui.x + 315, distY + 6, 42, 28, "−", purpleSoft, true)
    drawButton(ui.x + 363, distY + 6, 42, 28, "+", purpleSoft, true)

    -- Systems
    local indY = ui.y + 455
    roundedRect(ui.x + 18, indY, 190, 40, 8, card)
    roundedRect(ui.x + 223, indY, 190, 40, 8, card)

    local lightsOn = running and phase ~= "idle"
    local doorsOn = sprinklerOn
    local lightsColor = lightsOn and purpleBright or muted
    local doorsColor = doorsOn and white or muted

    drawText("●  ФАРЫ", ui.x + 30, indY, 90, 40, lightsColor, 0.78, "left", "center", "default-bold")
    drawText(lightsOn and "ON" or "OFF", ui.x + 125, indY, 68, 40, lightsOn and white or muted, 0.72, "right", "center", "default-bold")
    drawText("●  ПОЛИВ", ui.x + 235, indY, 95, 40, doorsColor, 0.78, "left", "center", "default-bold")
    drawText(doorsOn and "ON" or "OFF", ui.x + 325, indY, 78, 40, doorsOn and white or muted, 0.72, "right", "center", "default-bold")

    -- Last action
    -- Traffic lights mode
    local trY = ui.y + 505
    roundedRect(ui.x + 18, trY, ui.w - 36, 40, 8, card)
    drawText("●  СВЕТОФОРЫ", ui.x + 30, trY + 2, 140, 24, ignoreTraffic and white or muted, 0.78, "left", "center", "default-bold")
    drawText(ignoreTraffic and "ИГНОР" or "УЧИТЫВАЮТСЯ", ui.x + 150, trY + 2, 135, 24, ignoreTraffic and white or muted, 0.72, "right", "center", "default-bold")
    drawButton(ui.x + 295, trY + 6, 108, 28, ignoreTraffic and "ВЫКЛ ИГНОР" or "ВКЛ ИГНОР", ignoreTraffic and purpleSoft or purple, true)
    drawText((function()
        local h, m = getMoscowTime()
        return "авто по МСК: 00:00–10:00 не работают" .. (h and string.format("   (сейчас %02d:%02d)", h, m) or "")
    end)(), ui.x + 30, trY + 24, 262, 13, tocolor(110, 110, 110, 255), 0.56, "left", "center", "default")

    local logY = ui.y + 551
    roundedRect(ui.x + 18, logY, ui.w - 36, 34, 7, tocolor(14, 14, 14, 255))
    drawText("●  " .. lastLog, ui.x + 29, logY, ui.w - 58, 34, muted, 0.70, "left", "center", "default")

    -- Controls
    local by = ui.y + 595
    local bw = 125
    local bh = 42
    local bgap = 10

    drawButton(ui.x + 18, by, bw, bh, "▶  СТАРТ", purple, not running)
    drawButton(ui.x + 18 + bw + bgap, by, bw, bh, paused and "▶  ПРОДОЛЖИТЬ" or "Ⅱ  ПАУЗА", purpleSoft, running)
    drawButton(ui.x + 18 + (bw + bgap) * 2, by, bw, bh, "■  СТОП", tocolor(110, 110, 110, 255), running)

    drawText("F7 — панель", ui.x + 18, ui.y + 650, 120, 15, tocolor(110, 110, 110, 255), 0.65, "left", "center", "default")
    drawText("ПРОБЕЛ — пауза", ui.x + 245, ui.y + 650, 168, 15, tocolor(110, 110, 110, 255), 0.65, "right", "center", "default")
end

-- Встроенная DX-панель отключена; управление выполняется командами и горячими клавишами.

-- Обработчики кликов и перетаскивания встроенной панели удалены.

-- ============================================================
-- НОЧНОЙ / ДНЕВНОЙ РЕЖИМ СВЕТОФОРОВ (сообщения сервера, как в TramBot)
-- ============================================================

addEventHandler("onClientChatMessage", root, function(message)
    local text = tostring(message or "")
    if text:find("Светофоры переведены в ночной режим", 1, true) then
        ignoreTraffic = true
        log("Сервер: ночной режим, светофоры игнорируются")
        if api.updateState then api.updateState("tramp_traffic", "1") end
    elseif text:find("Светофоры переведены в дневной режим", 1, true) then
        ignoreTraffic = false
        log("Сервер: дневной режим, светофоры учитываются")
        if api.updateState then api.updateState("tramp_traffic", "0") end
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
        if running and completedCycles > 0 and phase == "starting" and hireRetries < MAX_HIRE_RETRIES then
            -- между циклами сервер может ещё не отпустить: ждём и пробуем снова (см. acceptJob)
            cancelEvent()
            log("Сервер: очередь/не удалось начать работу — подождём и повторим")
        elseif running then
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

-- Как в TramBot: если вручную устроиться на работу через игру, бот запомнит depot и line
-- (событие перехватывается, первый аргумент — источник — пропускается)
if type(api.catchServerEvent) == "function" then
    pcall(api.catchServerEvent, HIRE_EVENT, 1, function(depot, line)
        depot, line = tonumber(depot), tonumber(line)
        if depot and line then
            HIRE_ARG_1, HIRE_ARG_2 = depot, line
            log("Запомнил трудоустройство: " .. depot .. " / " .. line)
        end
    end)
end

addCommandHandler("tramp", function()
    startJTU()
end)

-- /trampcycles N — изменить количество циклов (можно во время работы)
-- /trampbrake N — дистанция торможения, 1–15 м
addCommandHandler("trampbrake", function(_, value)
    setBrakeDistance(value)
end)

-- /tramptraffic — вручную игнорировать светофоры вкл/выкл (обычно переключается само)
addCommandHandler("tramptraffic", function()
    ignoreTraffic = not ignoreTraffic
    notify("Светофоры: " .. (ignoreTraffic and "игнорируем" or "учитываем"))
end)

addCommandHandler("trampcycles", function(_, value)
    setTargetCycles(value)
end)

bindKey("space", "down", function()
    togglePause()
end)

-- F7 больше не используется: встроенная панель удалена.

setTimer(function()
    movementTick()
    if api.takeCommand then
        local cmd = api.takeCommand()
        while cmd do
            if cmd == "tramp_start" or cmd == "bot:1" then
                startJTU()
            elseif cmd == "tramp_take_route" or cmd == "take_route" then
                running = true
                acceptJob(true)
            elseif cmd == "tramp_stop" or cmd == "bot:0" then
                stopJTU()
            elseif cmd == "tramp_pause" then
                togglePause()
            elseif cmd == "tramp_reset_cycles" then
                completedCycles = 0
            elseif cmd:sub(1, 13) == "tramp_cycles:" then
                setTargetCycles(cmd:sub(14))
            elseif cmd:sub(1, 12) == "tramp_brake:" then
                setBrakeDistance(cmd:sub(13))
            elseif cmd == "tramp_traffic:1" then
                ignoreTraffic = true
                notify("Светофоры: игнорируем")
            elseif cmd == "tramp_traffic:0" then
                ignoreTraffic = false
                notify("Светофоры: учитываем")
            elseif cmd == "tramp_autodist:1" then
                autoDistance = true
            elseif cmd == "tramp_autodist:0" then
                autoDistance = false
            elseif cmd:sub(1, 15) == "tramp_distmode:" then
                setDistanceMode(cmd:sub(16))
            elseif cmd:sub(1, 12) == "tramp_route:" then
                setRoute(cmd:sub(13))
            elseif cmd == "clear_chat" then
                if clearChatBox then clearChatBox() end
            elseif cmd == "autolights:1" then
                autoLights = true
            elseif cmd == "autolights:0" then
                autoLights = false
            elseif cmd == "silent_chat:1" then
                silentChat = true
            elseif cmd == "silent_chat:0" then
                silentChat = false
            end
            cmd = api.takeCommand()
        end
    end
end, TICK, 0)

setTimer(sprinklerTick, 100, 0)

-- следим за московским временем: в 00:00 и 10:00 режим светофоров меняется сам
setTimer(function() applyTrafficByTime(false) end, 5000, 0)

-- /trampspeed — показать скорость, как её считает бот (сверьте со спидометром)
addCommandHandler("trampspeed", function()
    notify(string.format("Скорость: %.1f км/ч", speedKmh), "info")
end)

setTimer(function()
    if api.updateState then
        api.updateState("tramp_completed", tostring(completedCycles))
        api.updateState("tramp_target", tostring(targetCycles))
        api.updateState("tramp_brake", tostring(brakeDistance))
        api.updateState("tramp_traffic", ignoreTraffic and "1" or "0")
        api.updateState("tramp_sprinkler", sprinklerOn and "1" or "0")
        api.updateState("tramp_autodist", autoDistance and "1" or "0")
        api.updateState("tramp_distmode", tostring(distanceMode or 2))
        api.updateState("tramp_route", selectedRoute)
        api.updateState("tramp_route_name", routeNames[selectedRoute])
        api.updateState("status", phaseText())
        api.updateState("bot", running and "1" or "0")
    end
end, 300, 0)

addEventHandler("onClientResourceStop", resourceRoot, function()
    running = false
    paused = false
    killAllTimers()
    releaseMovement()
    if isElement(train) then
        setSpeed(0)
    end
end)
