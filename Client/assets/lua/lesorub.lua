-- lesorub.lua
-- Разработчик: xelni_ai & restleSS
-- Управление через меню ASHES или команду /lesorub_toggle
-- Бот работает только с blip icon 0, координаты которых есть в списке KNOWN_POINTS.
-- После исчезновения текущего blip движение останавливается до появления следующего.

local bot = {
    enabled = false,
    target = nil,
    targetPoint = nil,
    debug = false,
    camera = 0,
    cameraReset = true,
}

local CFG = {
    blipIcon = 0,
    pointMatchRadius = 35.0,
    sprintStopRadius = 8.0,
    finishRadius = 4.2,
    rotateSpeed = 0.15,
    rayDistance = 2.4,
}

-- Магистральный перекресток (хаб между северной и южной частью базы)
local ROAD_HUB = {x = -2745.0, y = 2775.0, z = 2.4}

-- Все 13 разрешённых координат blip.
local KNOWN_POINTS = {
    {x = -2748.595703125, y = 2837.2109375, z = 2.535000085830688},
    {x = -2754.171875, y = 2725.91796875, z = 3.234999895095825},
    {x = -2766.21484375, y = 2748.111328125, z = 2.427000045776367},
    {x = -2756.9921875, y = 2852.556640625, z = 2.563999891281128},
    {x = -2732.880859375, y = 2734.193359375, z = 4.130000114440918},
    {x = -2721.8271484375, y = 2853.91796875, z = 2.494999885559082},
    {x = -2715.466796875, y = 2711.07421875, z = 6.14799976348877},
    {x = -2736.201171875, y = 2703.7685546875, z = 4.895999908447266},
    {x = -2747.4853515625, y = 2818.615234375, z = 2.344000101089478},
    {x = -2696.28125, y = 2822.8642578125, z = 2.341000080108643},
    {x = -2719.6962890625, y = 2841.6650390625, z = 2.592000007629395},
    {x = -2764.2265625, y = 2797.0810546875, z = 2.342000007629395},
    {x = -2696.498046875, y = 2848.6328125, z = 2.196000099182129},
}

local function distance2D(x1, y1, x2, y2)
    return getDistanceBetweenPoints2D(x1, y1, x2, y2)
end

local function stopMove()
    setPedControlState(localPlayer, "forwards", false)
    setPedControlState(localPlayer, "sprint", false)
end

local function move(sprint)
    setPedControlState(localPlayer, "forwards", true)
    setPedControlState(localPlayer, "sprint", sprint == true)
end

local function normalizeAngle(angle)
    return (angle % 360 + 360) % 360
end

local function lerpAngle(from, to, amount)
    local delta = to - from
    if delta > 180 then delta = delta - 360 end
    if delta < -180 then delta = delta + 360 end
    return from + delta * amount
end

local function angleTo(x, y)
    local px, py = getElementPosition(localPlayer)
    return normalizeAngle(-math.deg(math.atan2(x - px, y - py)))
end

local function getVectorFromAngle(ang, length)
    local rad = math.rad(ang)
    return -math.sin(rad) * length, math.cos(rad) * length
end

local function isPathClear(px, py, pz, ang, dist)
    local vx, vy = getVectorFromAngle(ang, dist)
    return isLineOfSightClear(px, py, pz, px + vx, py + vy, pz, true, true, false, true, true, false, false, localPlayer)
end

local function findClearAngle(px, py, pz, targetAngle)
    local z = pz + 0.65
    if isPathClear(px, py, z, targetAngle, CFG.rayDistance) then
        return targetAngle, false
    end
    local offsets = { 35, -35, 60, -60, 85, -85 }
    for _, offset in ipairs(offsets) do
        local testAngle = normalizeAngle(targetAngle + offset)
        if isPathClear(px, py, z, testAngle, CFG.rayDistance * 0.85) then
            return testAngle, true
        end
    end
    return targetAngle, true
end

local antiStuck = {
    lastCheck = 0,
    lastX = 0,
    lastY = 0,
    stuckFrames = 0,
    evading = false,
    evadeUntil = 0,
    evadeOffset = 0,
    evadeDir = 1,
}

local function updateAntiStuck(now, px, py, isMoving)
    if not isMoving then
        antiStuck.lastCheck = now
        antiStuck.lastX = px
        antiStuck.lastY = py
        antiStuck.stuckFrames = 0
        antiStuck.evading = false
        return 0
    end
    if antiStuck.evading then
        if now < antiStuck.evadeUntil then
            return antiStuck.evadeOffset
        else
            antiStuck.evading = false
            antiStuck.lastCheck = now
            antiStuck.lastX = px
            antiStuck.lastY = py
        end
    end
    if now - antiStuck.lastCheck >= 1100 then
        local moved = distance2D(px, py, antiStuck.lastX, antiStuck.lastY)
        antiStuck.lastCheck = now
        antiStuck.lastX = px
        antiStuck.lastY = py
        if moved < 0.38 then
            antiStuck.stuckFrames = antiStuck.stuckFrames + 1
            if antiStuck.stuckFrames >= 1 then
                setPedControlState(localPlayer, "jump", true)
                setTimer(function()
                    setPedControlState(localPlayer, "jump", false)
                end, 150, 1)
                antiStuck.evadeDir = -antiStuck.evadeDir
                antiStuck.evadeOffset = antiStuck.evadeDir * 75
                antiStuck.evading = true
                antiStuck.evadeUntil = now + 900
                if bot.debug and dfPushToast then
                    dfPushToast("Лесоруб", string.format("Анти-застревание: прыжок (сдвиг %.2fм)", moved), "warning")
                end
                return antiStuck.evadeOffset
            end
        else
            antiStuck.stuckFrames = 0
        end
    end
    return 0
end

local function isTargetBlip(blip)
    return isElement(blip) and tonumber(getBlipIcon(blip)) == CFG.blipIcon
end

local function findKnownPoint(blip)
    if not isElement(blip) then return nil, math.huge end

    local bx, by = getElementPosition(blip)
    local nearest, nearestDistance = nil, math.huge

    for index, point in ipairs(KNOWN_POINTS) do
        local distance = distance2D(bx, by, point.x, point.y)
        if distance < nearestDistance then
            nearestDistance = distance
            nearest = {x = point.x, y = point.y, z = point.z, id = index}
        end
    end

    if nearestDistance <= CFG.pointMatchRadius then
        return nearest, nearestDistance
    end

    return nil, nearestDistance
end

local function findNewTarget()
    local bestBlip, bestPoint, bestDistance = nil, nil, math.huge

    for _, blip in ipairs(getElementsByType("blip")) do
        if isTargetBlip(blip) then
            local point, matchDistance = findKnownPoint(blip)
            if point and matchDistance < bestDistance then
                bestBlip = blip
                bestPoint = point
                bestDistance = matchDistance
            end
        end
    end

    return bestBlip, bestPoint, bestDistance
end

local function setTarget(blip, point, matchDistance)
    bot.target = blip
    bot.targetPoint = point
    bot.cameraReset = true


end

local function clearTarget()
    bot.target = nil
    bot.targetPoint = nil
    bot.atTree = false
    bot.cameraReset = true
    stopMove()
end

local miniGame = {
    active = false,
    key = "arrow_r",
    lastDetected = 0,
    lastTap = 0,
}

local ARROW_MAP = {
    [0] = "arrow_r",
    [90] = "arrow_d",
    [180] = "arrow_l",
    [270] = "arrow_u",
}

local function setMiniGameKey(newKey, source)
    if not newKey then return end
    if miniGame.key ~= newKey then
        miniGame.key = newKey

    end
end

local function detectKeyFromImage(image, rotation)
    if type(image) == "string" then
        local imgLower = image:lower()
        if imgLower:find("right") or imgLower:find("arrow_r") or imgLower:find("arrow_right") then
            return "arrow_r"
        elseif imgLower:find("left") or imgLower:find("arrow_l") or imgLower:find("arrow_left") then
            return "arrow_l"
        elseif imgLower:find("up") or imgLower:find("arrow_u") or imgLower:find("arrow_up") then
            return "arrow_u"
        elseif imgLower:find("down") or imgLower:find("arrow_d") or imgLower:find("arrow_down") then
            return "arrow_d"
        end
    end
    if rotation then
        local rot = (math.floor(rotation + 0.5) % 360)
        local closestRot = math.floor((rot + 45) / 90) % 4 * 90
        return ARROW_MAP[closestRot]
    end
    return nil
end

if dxDrawText and not _G.origDxDrawText then
    _G.origDxDrawText = dxDrawText
    dxDrawText = function(text, ...)
        if type(text) == "string" then
            if text:find("шкалу") or text:find("нажимайте") or text:find("Быстро") then
                miniGame.active = true
                miniGame.lastDetected = getTickCount()
            end
            if miniGame.active then
                if text:find("→") or text:find("Вправо") or text:find("ВПРАВО") then
                    setMiniGameKey("arrow_r", "text:→")
                elseif text:find("←") or text:find("Влево") or text:find("ВЛЕВО") then
                    setMiniGameKey("arrow_l", "text:←")
                elseif text:find("↑") or text:find("Вверх") or text:find("ВВЕРХ") then
                    setMiniGameKey("arrow_u", "text:↑")
                elseif text:find("↓") or text:find("Вниз") or text:find("ВНИЗ") then
                    setMiniGameKey("arrow_d", "text:↓")
                end
            end
        end
        return _G.origDxDrawText(text, ...)
    end
end

if dxDrawImage and not _G.origDxDrawImage then
    _G.origDxDrawImage = dxDrawImage
    dxDrawImage = function(x, y, w, h, image, rotation, ...)
        if miniGame.active and (getTickCount() - miniGame.lastDetected < 2500) then
            local k = detectKeyFromImage(image, rotation)
            if k then setMiniGameKey(k, "image") end
        end
        return _G.origDxDrawImage(x, y, w, h, image, rotation, ...)
    end
end

if dxDrawImageSection and not _G.origDxDrawImageSection then
    _G.origDxDrawImageSection = dxDrawImageSection
    dxDrawImageSection = function(x, y, w, h, u, v, us, vs, image, rotation, ...)
        if miniGame.active and (getTickCount() - miniGame.lastDetected < 2500) then
            local k = detectKeyFromImage(image, rotation)
            if k then setMiniGameKey(k, "section") end
        end
        return _G.origDxDrawImageSection(x, y, w, h, u, v, us, vs, image, rotation, ...)
    end
end

local function tapMiniGameKey()
    local now = getTickCount()
    if now - miniGame.lastTap < 60 then return end
    miniGame.lastTap = now

    local key = miniGame.key or "arrow_r"
    local altKey = "d"
    if key == "arrow_l" then altKey = "a"
    elseif key == "arrow_u" then altKey = "w"
    elseif key == "arrow_d" then altKey = "s"
    end

    if dfEmulateKey then
        dfEmulateKey(key, true)
        dfEmulateKey(altKey, true)
        setTimer(function()
            if dfEmulateKey then
                dfEmulateKey(key, false)
                dfEmulateKey(altKey, false)
            end
        end, 20, 1)
    end
    triggerEvent("onClientKey", root, key, true)
    triggerEvent("onClientKey", root, key, false)
    triggerEvent("onClientKey", root, altKey, true)
    triggerEvent("onClientKey", root, altKey, false)
end

local function steerTo(x, y, evadeOffset)
    local px, py, pz = getElementPosition(localPlayer)
    local desired = angleTo(x, y)
    local clearAngle, obstacleDetected = findClearAngle(px, py, pz, desired)

    local finalAngle = clearAngle
    if evadeOffset and evadeOffset ~= 0 then
        finalAngle = normalizeAngle(desired + evadeOffset)
    end

    if bot.cameraReset then
        bot.camera = finalAngle
        bot.cameraReset = false
    else
        bot.camera = normalizeAngle(lerpAngle(bot.camera, finalAngle, CFG.rotateSpeed))
    end

    setPedCameraRotation(localPlayer, bot.camera)
end

local function startBot()
    bot.enabled = true
    clearTarget()
    if dfPushToast then
        dfPushToast("Лесоруб", "Бот запущен. Авто-рубка активна", "success")
    end
end

local function stopBot()
    bot.enabled = false
    clearTarget()
    if dfPushToast then
        dfPushToast("Лесоруб", "Бот выключен", "warning")
    end
end

local function toggleBot()
    if bot.enabled then
        stopBot()
    else
        startBot()
    end
end

-- Интеграция с UI через Lua Bridge
setTimer(function()
    if dfLesorubTakeCommand then
        local cmd = dfLesorubTakeCommand()
        while cmd do
            if cmd == "bot:1" or cmd == "start" then
                if not bot.enabled then startBot() end
            elseif cmd == "bot:0" or cmd == "stop" then
                if bot.enabled then stopBot() end
            elseif cmd:sub(1, 7) == "radius:" then
                local val = tonumber(cmd:sub(8))
                if val and val >= 5.0 and val <= 100.0 then
                    CFG.pointMatchRadius = val
                end
            elseif cmd:sub(1, 12) == "rotatespeed:" then
                local val = tonumber(cmd:sub(13))
                if val and val > 0.0 then
                    CFG.rotateSpeed = val
                end
            elseif cmd == "debug:1" then
                bot.debug = true
            elseif cmd == "debug:0" then
                bot.debug = false
            end
            cmd = dfLesorubTakeCommand()
        end
    end

    if dfLesorubUpdate then
        dfLesorubUpdate("loaded", "1")
        dfLesorubUpdate("enabled", bot.enabled and "1" or "0")
        local statusText = "Отключен"
        if bot.enabled then
            if bot.atTree then
                statusText = "Рубка дерева"
            elseif bot.target and isElement(bot.target) then
                statusText = "В движении"
            else
                statusText = "Ожидание меток"
            end
        end
        dfLesorubUpdate("status", statusText)
    end
end, 100, 0)

addEventHandler("onClientPreRender", root, function()
    if not bot.enabled then return end

    -- Пока цель не появилась или уже исчезла, не двигаемся.
    if not bot.target or not isElement(bot.target) or not isTargetBlip(bot.target) then
        clearTarget()
        local blip, point, matchDistance = findNewTarget()
        if not blip then return end
        setTarget(blip, point, matchDistance)
    end

    local px, py = getElementPosition(localPlayer)
    local point = bot.targetPoint
    local distance = distance2D(px, py, point.x, point.y)

    -- Если подошли к дереву или активна мини-игра
    local now = getTickCount()
    if distance <= CFG.finishRadius or (miniGame.active and (now - miniGame.lastDetected < 2000) and distance <= (CFG.finishRadius + 2.0)) then
        bot.atTree = true
        stopMove()
        tapMiniGameKey()
        return
    end

    bot.atTree = false

    -- Определение направления (с учетом магистрального хаба при пересечении зон)
    local navTarget = point
    local crossesZone = (py > 2785 and point.y < 2750) or (py < 2750 and point.y > 2785)
    if crossesZone then
        local hubDist = distance2D(px, py, ROAD_HUB.x, ROAD_HUB.y)
        if hubDist > 6.0 then
            navTarget = ROAD_HUB
        end
    end

    -- Детектор залипания с прыжком и динамический raycast-обход
    local evadeOffset = updateAntiStuck(now, px, py, true)
    steerTo(navTarget.x, navTarget.y, evadeOffset)
    move(distance > CFG.sprintStopRadius)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    bot.enabled = false
    clearTarget()
end)

if dfPushToast then
    dfPushToast("Лесоруб", "Скрипт загружен и готов к работе", "info")
end
