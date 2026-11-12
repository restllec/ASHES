-- botstroika.lua — Бот работы на стройке для ASHES
local isActive = false
local teleportDelay = 14500
local godmodeEnabled = true
local checkpointsDone = 0
local earnedRubles = 0
local teleportTimer = nil

local function logMsg(msg)
    if dfPushToast then
        dfPushToast("Стройка", tostring(msg), "info")
    end
    outputChatBox("[Стройка] " .. tostring(msg), 200, 200, 200)
end

local function setGodmode(enabled)
    local player = getLocalPlayer()
    if player then
        setElementData(player, "godmode", enabled)
    end
end

local function findNextBlip()
    local player = getLocalPlayer()
    if not player then return nil end
    local px, py, pz = getElementPosition(player)
    local nearestBlip = nil
    local minDistance = math.huge

    local blips = getElementsByType("blip")
    for _, blip in ipairs(blips) do
        local bx, by, bz = getElementPosition(blip)
        local isBlipOnPlayerOrVehicle = false

        for _, playerElement in ipairs(getElementsByType("player")) do
            if playerElement ~= player then
                local px2, py2, pz2 = getElementPosition(playerElement)
                if getDistanceBetweenPoints3D(bx, by, bz, px2, py2, pz2) < 1.0 then
                    isBlipOnPlayerOrVehicle = true
                    break
                end
            end
        end

        if not isBlipOnPlayerOrVehicle then
            for _, vehicleElement in ipairs(getElementsByType("vehicle")) do
                local vx, vy, vz = getElementPosition(vehicleElement)
                if getDistanceBetweenPoints3D(bx, by, bz, vx, vy, vz) < 1.0 then
                    isBlipOnPlayerOrVehicle = true
                    break
                end
            end
        end

        if not isBlipOnPlayerOrVehicle then
            local distance = getDistanceBetweenPoints3D(px, py, pz, bx, by, bz)
            if distance < minDistance then
                minDistance = distance
                nearestBlip = blip
            end
        end
    end

    return nearestBlip, minDistance
end

local function teleportToNextBlip()
    if not isActive then return end

    local nearestBlip, dist = findNextBlip()
    local player = getLocalPlayer()
    if nearestBlip and player then
        local bx, by, bz = getElementPosition(nearestBlip)
        setElementPosition(player, bx, by, bz + 0.5)
        checkpointsDone = checkpointsDone + 1
        earnedRubles = earnedRubles + 1500
        if dfStroikaUpdate then
            dfStroikaUpdate("checkpoints", tostring(checkpointsDone))
            dfStroikaUpdate("rubles", tostring(earnedRubles))
            dfStroikaUpdate("status", string.format("Метка #%d (дистанция %.1fм)", checkpointsDone, dist or 0))
        end
    else
        if dfStroikaUpdate then
            dfStroikaUpdate("status", "Блипы не найдены")
        end
    end

    teleportTimer = setTimer(teleportToNextBlip, teleportDelay, 1)
end

local function startBot()
    if isActive then return end
    isActive = true
    if godmodeEnabled then
        setGodmode(true)
    end
    logMsg("Бот стройки запущен")
    if dfStroikaUpdate then
        dfStroikaUpdate("bot", "1")
        dfStroikaUpdate("status", "Поиск ближайшей метки...")
    end
    teleportToNextBlip()
end

local function stopBot()
    if not isActive then return end
    isActive = false
    if isTimer(teleportTimer) then
        killTimer(teleportTimer)
        teleportTimer = nil
    end
    setGodmode(false)
    logMsg("Бот стройки остановлен")
    if dfStroikaUpdate then
        dfStroikaUpdate("bot", "0")
        dfStroikaUpdate("status", "Остановлен")
    end
end

-- Обработка команд из C++ GUI
setTimer(function()
    if dfStroikaTakeCommand then
        local cmd = dfStroikaTakeCommand()
        while cmd do
            if cmd == "bot:1" then
                startBot()
            elseif cmd == "bot:0" then
                stopBot()
            elseif cmd:sub(1, 6) == "delay:" then
                teleportDelay = tonumber(cmd:sub(7)) or 14500
            elseif cmd == "godmode:1" then
                godmodeEnabled = true
                if isActive then setGodmode(true) end
            elseif cmd == "godmode:0" then
                godmodeEnabled = false
                if isActive then setGodmode(false) end
            elseif cmd == "reset_stats" then
                checkpointsDone = 0
                earnedRubles = 0
                if dfStroikaUpdate then
                    dfStroikaUpdate("checkpoints", "0")
                    dfStroikaUpdate("rubles", "0")
                end
            end
            cmd = dfStroikaTakeCommand()
        end
    end
    if dfStroikaUpdate then
        dfStroikaUpdate("loaded", "1")
        dfStroikaUpdate("bot", isActive and "1" or "0")
        dfStroikaUpdate("delay", tostring(teleportDelay))
        dfStroikaUpdate("godmode", godmodeEnabled and "1" or "0")
    end
end, 200, 0)

