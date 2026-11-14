-- misc_tools.lua — Утилиты, Камхак, Починка, ГМ, Таран, Антиштраф для ASHES
local legitRepair = false
local carGm = false
local playerGm = false
local noCollision = false
local antiFine = true
local taranEnabled = false
local taranStrength = 3
local taranDelay = 50

-- CamHack переменные
local camActive = false
local camSpeed = 0.5
local smoothFactor = 5.0
local camPos = {x = 0, y = 0, z = 0}
local camRot = {x = 0, y = 0, z = 0}

-- Починка и ГМ автомобиля
addEventHandler("onClientRender", root, function()
    local veh = getPedOccupiedVehicle(localPlayer)
    if veh and isElement(veh) then
        local hp = getElementHealth(veh)
        if carGm then
            if hp < 996 then
                setElementHealth(veh, 996)
            end
            setVehicleDamageProof(veh, true)
        elseif legitRepair then
            if hp < 998 then
                setElementHealth(veh, 998)
            end
        end
    end
end)

addEventHandler("onClientVehicleEnter", localPlayer, function(veh)
    if carGm and veh and isElement(veh) then
        setElementHealth(veh, 996)
        setVehicleDamageProof(veh, true)
    end
end)

-- ГМ персонажа
local function setPlayerGodMode(enabled)
    playerGm = enabled
    setElementData(localPlayer, "godmode", enabled)
end

-- Коллизии (NoCollision)
local function setNoCollision(enabled)
    noCollision = enabled
    local veh = getPedOccupiedVehicle(localPlayer)
    if veh and isElement(veh) then
        for _, otherVeh in ipairs(getElementsByType("vehicle")) do
            if otherVeh ~= veh then setElementCollidableWith(veh, otherVeh, not enabled) end
        end
        for _, obj in ipairs(getElementsByType("object")) do
            setElementCollidableWith(veh, obj, not enabled)
        end
        for _, ped in ipairs(getElementsByType("ped")) do
            setElementCollidableWith(veh, ped, not enabled)
        end
    end
end

local function getMoscowHour()
    if type(getRealTime) ~= "function" then return 12 end
    local t = getRealTime()
    if type(t) ~= "table" or type(t.timestamp) ~= "number" then return 12 end
    local secondsOfDay = (t.timestamp + 3 * 3600) % 86400
    return math.floor(secondsOfDay / 3600)
end

local function isNightTimeMsk()
    local h = getMoscowHour()
    return h >= 0 and h < 12
end

-- Антиштраф: светофоры и блокировка фиксации нарушений (00:00–12:00 МСК авто-отключение)
local function setAntiFine(enabled)
    antiFine = enabled
    if enabled then
        if isNightTimeMsk() then
            pcall(setTrafficLightsLocked, false)
            pcall(setTrafficLightState, "auto")
            if dfPushToast then
                dfPushToast("Антиштраф", "Ночной режим (00:00–12:00 МСК): светофоры свободны", "info")
            end
        else
            pcall(setTrafficLightsLocked, true)
            pcall(setTrafficLightState, "all_green")
            if dfPushToast then
                dfPushToast("Антиштраф", "Светофоры зафиксированы на зеленый", "success")
            end
        end
    else
        pcall(setTrafficLightsLocked, false)
        pcall(setTrafficLightState, "auto")
        if dfPushToast then
            dfPushToast("Антиштраф", "Система отключена", "info")
        end
    end
end

-- Периодическая проверка и поддержание зеленого сигнала
setTimer(function()
    if antiFine then
        if isNightTimeMsk() then
            pcall(setTrafficLightsLocked, false)
            pcall(setTrafficLightState, "auto")
        else
            pcall(setTrafficLightsLocked, true)
            pcall(setTrafficLightState, "all_green")
        end
    end
end, 2000, 0)

-- Обход Алисы (телепорт в безопасный интерьер/dimension 0)
local function bypassAlice()
    setElementDimension(localPlayer, 0)
    setElementPosition(localPlayer, -522.42, -2030.02, 1003.87)
    setElementRotation(localPlayer, 0, 0, 177.57)
    if dfPushToast then
        dfPushToast("Обход Алисы", "Телепортация выполнена успешно (-522, -2030, 1003)", "success")
    end
end

-- Автосдача экзамена автошколы
local function passDrivingTest()
    triggerServerEvent("onPlayerCompleteTest", localPlayer)
    if dfPushToast then
        dfPushToast("Автошкола", "Отправлен запрос завершения теста на сервер", "info")
    end
end

-- Ручная починка автомобиля
local function repairVehicleNow()
    local veh = getPedOccupiedVehicle(localPlayer)
    if veh and isElement(veh) then
        fixVehicle(veh)
        setElementHealth(veh, 1000)
        if dfPushToast then
            dfPushToast("Транспорт", "Автомобиль полностью починен", "success")
        end
    else
        if dfPushToast then
            dfPushToast("Транспорт", "Сядьте в автомобиль для починки", "warning")
        end
    end
end

-- Импульс тарана (разовое ускорение вперед и возврат)
local function triggerTaranImpulse()
    local veh = getPedOccupiedVehicle(localPlayer)
    if not veh or not isElement(veh) then return end
    local curPos = Vector3(getElementPosition(veh))
    local curRot = Vector3(getElementRotation(veh))
    local vel = Vector3(getElementVelocity(veh))
    local mult = 1.0 + (taranStrength * 0.4)
    setElementVelocity(veh, vel.x * mult, vel.y * mult, vel.z * mult)
    setTimer(function()
        if isElement(veh) then
            setElementPosition(veh, curPos.x, curPos.y, curPos.z)
            setElementRotation(veh, curRot.x, curRot.y, curRot.z)
            setElementVelocity(veh, 0, 0, 0)
        end
    end, taranDelay, 1)
    if dfPushToast then
        dfPushToast("Таран", "Импульс выполнен", "info")
    end
end

-- CamHack функции
local function updateCam()
    if not camActive then return end
    local x, y, z = camPos.x, camPos.y, camPos.z
    local rx, ry, rz = camRot.x, camRot.y, camRot.z
    setCameraMatrix(x, y, z,
        x + math.cos(math.rad(rz)),
        y + math.sin(math.rad(rz)),
        z + math.sin(math.rad(ry)))
end

local function toggleCamHack(enabled)
    camActive = enabled
    if camActive then
        toggleAllControls(false, true, false)
        local px, py, pz = getElementPosition(localPlayer)
        camPos.x, camPos.y, camPos.z = px, py, pz + 4.0
        camRot.x, camRot.y, camRot.z = getElementRotation(localPlayer)
        addEventHandler("onClientRender", root, updateCam)
        if dfPushToast then dfPushToast("Камхак", "Свободная камера включена", "info") end
    else
        toggleAllControls(true)
        removeEventHandler("onClientRender", root, updateCam)
        setCameraTarget(localPlayer)
        if dfPushToast then dfPushToast("Камхак", "Свободная камера выключена", "info") end
    end
end

local function moveCam(dirX, dirY, dirZ)
    if camActive then
        camPos.x = camPos.x + dirX * camSpeed
        camPos.y = camPos.y + dirY * camSpeed
        camPos.z = camPos.z + dirZ * camSpeed
    end
end

local function rotateCam(dirZ, dirY)
    if camActive then
        camRot.z = camRot.z + dirZ * smoothFactor
        camRot.y = camRot.y + dirY * smoothFactor
    end
end

-- Обработка команд из C++ GUI
setTimer(function()
    if dfMiscTakeCommand then
        local cmd = dfMiscTakeCommand()
        while cmd do
            if cmd == "legit_repair:1" then legitRepair = true
            elseif cmd == "legit_repair:0" then legitRepair = false
            elseif cmd == "car_gm:1" then carGm = true
            elseif cmd == "car_gm:0" then
                carGm = false
                local veh = getPedOccupiedVehicle(localPlayer)
                if veh and isElement(veh) then setVehicleDamageProof(veh, false) end
            elseif cmd == "player_gm:1" then setPlayerGodMode(true)
            elseif cmd == "player_gm:0" then setPlayerGodMode(false)
            elseif cmd == "nocollision:1" then setNoCollision(true)
            elseif cmd == "nocollision:0" then setNoCollision(false)
            elseif cmd == "antifine:1" then setAntiFine(true)
            elseif cmd == "antifine:0" then setAntiFine(false)
            elseif cmd == "repair_now" then repairVehicleNow()
            elseif cmd == "alice_bypass" then bypassAlice()
            elseif cmd == "pass_driving_test" then passDrivingTest()
            elseif cmd == "taran_impulse" then triggerTaranImpulse()
            elseif cmd:sub(1, 10) == "taran_str:" then taranStrength = tonumber(cmd:sub(11)) or 3
            elseif cmd:sub(1, 10) == "taran_del:" then taranDelay = tonumber(cmd:sub(11)) or 50
            elseif cmd == "camhack:1" then toggleCamHack(true)
            elseif cmd == "camhack:0" then toggleCamHack(false)
            elseif cmd:sub(1, 9) == "camspeed:" then camSpeed = tonumber(cmd:sub(10)) or 0.5
            elseif cmd:sub(1, 10) == "camsmooth:" then smoothFactor = tonumber(cmd:sub(11)) or 5.0
            elseif cmd == "cam_rot_left" then rotateCam(-2, 0)
            elseif cmd == "cam_rot_right" then rotateCam(2, 0)
            elseif cmd == "cam_tilt_up" then rotateCam(0, 2)
            elseif cmd == "cam_tilt_down" then rotateCam(0, -2)
            end
            cmd = dfMiscTakeCommand()
        end
    end
    if dfMiscUpdate then
        dfMiscUpdate("loaded", "1")
        dfMiscUpdate("legit_repair", legitRepair and "1" or "0")
        dfMiscUpdate("car_gm", carGm and "1" or "0")
        dfMiscUpdate("player_gm", playerGm and "1" or "0")
        dfMiscUpdate("nocollision", noCollision and "1" or "0")
        dfMiscUpdate("antifine", antiFine and "1" or "0")
        dfMiscUpdate("camhack", camActive and "1" or "0")
        dfMiscUpdate("cam_x", string.format("%.1f", camPos.x))
        dfMiscUpdate("cam_y", string.format("%.1f", camPos.y))
        dfMiscUpdate("cam_z", string.format("%.1f", camPos.z))
    end
end, 200, 0)

