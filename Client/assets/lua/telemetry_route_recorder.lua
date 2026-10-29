-- telemetry_route_recorder.lua
-- Отдельный рекордер телеметрии и точек сдачи.
-- P: начать запись нового маршрута / повторно остановить и сохранить.
-- O: поставить следующую постоянную точку сдачи.
-- Все данные сохраняются в route_recorder_data.log и не удаляются при новых записях.

local recorder = {
    recording = false,
    route = {},
    routeId = 0,
    nextPointId = 1,
    follower = nil,
    linePoints = {},
    savedLinePoints = {},
    lastPoint = nil,
    lastSampleTick = 0,
    data = {
        routes = {},
        deliveryPoints = {},
    },
}

local CFG = {
    sampleDistance = 0.75,
    sampleInterval = 100,
    lineWidth = 3.0,
    lineColor = {40, 130, 255, 230},
    pointSize = 1.15,
    pointColor = {255, 190, 40, 230},
}

local function say(text, r, g, b)
    outputChatBox("[ROUTE RECORDER] " .. text, r or 100, g or 200, b or 255, false)
end

local function getPosition()
    return getElementPosition(localPlayer)
end

local function distance(x1, y1, z1, x2, y2, z2)
    return getDistanceBetweenPoints3D(x1, y1, z1, x2, y2, z2)
end

local function makeKey(prefix, id)
    return prefix .. tostring(id)
end

local function destroyFollower()
    recorder.follower = nil
    recorder.linePoints = {}
end

local function rebuildSavedLines()
    recorder.savedLinePoints = {}
    for _, route in ipairs(recorder.data.routes or {}) do
        local points = {}
        for _, point in ipairs(route.points or {}) do
            local x, y, z = tonumber(point[1]), tonumber(point[2]), tonumber(point[3])
            if x and y and z then points[#points + 1] = {x, y, z} end
        end
        if #points > 1 then recorder.savedLinePoints[#recorder.savedLinePoints + 1] = points end
    end
end

local function createFollower()
    destroyFollower()
    local x, y, z = getPosition()
    recorder.linePoints = {{x, y, z}}
end

local function updateFollower()
    if not recorder.recording then return end
    local x, y, z = getPosition()
    local last = recorder.linePoints[#recorder.linePoints]
    if not last or distance(x, y, z, last[1], last[2], last[3]) >= CFG.sampleDistance then
        recorder.linePoints[#recorder.linePoints + 1] = {x, y, z}
    end
end

local function drawLine(points)
    if #points < 2 then return end
    for i = 2, #points do
        local a, b = points[i - 1], points[i]
        dxDrawLine3D(
            a[1], a[2], a[3] + 0.08,
            b[1], b[2], b[3] + 0.08,
            tocolor(CFG.lineColor[1], CFG.lineColor[2], CFG.lineColor[3], CFG.lineColor[4]),
            CFG.lineWidth
        )
    end
end

local function drawFollowerLine()
    for _, points in ipairs(recorder.savedLinePoints) do drawLine(points) end
    if recorder.recording then drawLine(recorder.linePoints) end
end

local function encodeData()
    return toJSON(recorder.data, true)
end

local function saveData()
    -- Перезаписываем только сериализованный контейнер: старые маршруты и точки
    -- остаются в recorder.data и добавляются, а не очищаются.
    local file
    if fileExists("route_recorder_data.log") then
        file = fileOpen("route_recorder_data.log")
        if file then fileSetPos(file, 0) end
    else
        file = fileCreate("route_recorder_data.log")
    end
    if not file then
        say("не удалось открыть route_recorder_data.log", 255, 90, 90)
        return false
    end
    fileWrite(file, encodeData())
    fileFlush(file)
    fileClose(file)
    return true
end

local function loadData()
    if not fileExists("route_recorder_data.log") then return end
    local file = fileOpen("route_recorder_data.log", true)
    if not file then return end
    local content = fileRead(file, fileGetSize(file)) or ""
    fileClose(file)
    if content == "" then return end

    local decoded = fromJSON(content)
    if type(decoded) == "table" then
        recorder.data = decoded
        recorder.data.routes = recorder.data.routes or {}
        recorder.data.deliveryPoints = recorder.data.deliveryPoints or {}
    end

    local maxRoute, maxPoint = 0, 0
    for _, route in ipairs(recorder.data.routes) do
        maxRoute = math.max(maxRoute, tonumber(route.id) or 0)
    end
    for _, point in ipairs(recorder.data.deliveryPoints) do
        maxPoint = math.max(maxPoint, tonumber(point.id) or 0)
    end
    recorder.routeId = maxRoute
    recorder.nextPointId = maxPoint + 1
end

local function clearRouteMarkers(route)
    if type(route) ~= "table" or type(route.markers) ~= "table" then return end
    for _, marker in ipairs(route.markers) do
        if isElement(marker) then destroyElement(marker) end
    end
    route.markers = nil
end

local function sanitizeData()
    local validPoints = {}
    for _, point in ipairs(recorder.data.deliveryPoints or {}) do
        if tonumber(point.x) and tonumber(point.y) and tonumber(point.z) then
            point.x, point.y, point.z = tonumber(point.x), tonumber(point.y), tonumber(point.z)
            validPoints[#validPoints + 1] = point
        end
    end
    recorder.data.deliveryPoints = validPoints

    for _, route in ipairs(recorder.data.routes or {}) do
        local validRoute = {}
        for _, point in ipairs(route.points or {}) do
            local x, y, z = tonumber(point[1]), tonumber(point[2]), tonumber(point[3])
            if x and y and z then validRoute[#validRoute + 1] = {x, y, z} end
        end
        route.points = validRoute
    end
end

local function showDeliveryPoints()
    for _, point in ipairs(recorder.data.deliveryPoints) do
        local x = tonumber(point.x)
        local y = tonumber(point.y)
        local z = tonumber(point.z)
        if x and y and z then
            point.x, point.y, point.z = x, y, z
            if not isElement(point.marker) then
                point.marker = createMarker(
                    x, y, z - 1.0, "cylinder", CFG.pointSize,
                    CFG.pointColor[1], CFG.pointColor[2], CFG.pointColor[3], CFG.pointColor[4]
                )
                if isElement(point.marker) then
                    setElementInterior(point.marker, tonumber(point.interior) or getElementInterior(localPlayer))
                    setElementDimension(point.marker, tonumber(point.dimension) or getElementDimension(localPlayer))
                end
            end
        else
            -- Не даём старой повреждённой записи остановить весь ресурс.
            point.marker = nil
            if recorder.debug then
                say("пропущена точка без корректных координат", 255, 180, 80)
            end
        end
    end
end

local function startRoute()
    if recorder.recording then return end
    recorder.recording = true
    recorder.route = {}
    recorder.lastPoint = nil
    recorder.lastSampleTick = 0
    createFollower()
    say("запись маршрута №" .. (recorder.routeId + 1) .. " начата", 100, 255, 130)
end

local function finishRoute()
    if not recorder.recording then return end
    recorder.recording = false
    destroyFollower()

    if #recorder.route < 2 then
        recorder.route = {}
        say("маршрут не сохранён: нужно минимум 2 точки", 255, 180, 80)
        return
    end

    recorder.routeId = recorder.routeId + 1
    local route = {
        id = recorder.routeId,
        createdAt = getRealTime().timestamp,
        points = recorder.route,
    }
    table.insert(recorder.data.routes, route)
    recorder.route = {}
    rebuildSavedLines()

    if saveData() then
        say("маршрут №" .. route.id .. " сохранён, точек: " .. #route.points, 100, 255, 130)
    end
end

local function placeDeliveryPoint()
    local x, y, z = getPosition()
    local point = {
        id = recorder.nextPointId,
        x = x,
        y = y,
        z = z,
        interior = getElementInterior(localPlayer),
        dimension = getElementDimension(localPlayer),
        createdAt = getRealTime().timestamp,
    }
    recorder.nextPointId = recorder.nextPointId + 1
    table.insert(recorder.data.deliveryPoints, point)
    if saveData() then
        showDeliveryPoints()
        say(string.format("Точка %d сохранена: %.3f %.3f %.3f", point.id, x, y, z), 255, 210, 70)
    end
end

local function sampleRoute()
    if not recorder.recording then return end
    local now = getTickCount()
    if now - recorder.lastSampleTick < CFG.sampleInterval then return end
    recorder.lastSampleTick = now

    local x, y, z = getPosition()
    if recorder.lastPoint and distance(x, y, z, recorder.lastPoint[1], recorder.lastPoint[2], recorder.lastPoint[3]) < CFG.sampleDistance then
        return
    end
    local sample = {x, y, z}
    table.insert(recorder.route, sample)
    recorder.lastPoint = sample
end

bindKey("p", "down", function()
    if recorder.recording then finishRoute() else startRoute() end
end)

bindKey("o", "down", placeDeliveryPoint)

addEventHandler("onClientPreRender", root, function()
    if recorder.recording then
        sampleRoute()
        updateFollower()
    end
end)

addEventHandler("onClientRender", root, function()
    drawFollowerLine()
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if recorder.recording then finishRoute() end
    destroyFollower()
    for _, point in ipairs(recorder.data.deliveryPoints) do
        if isElement(point.marker) then destroyElement(point.marker) end
    end
end)

loadData()
sanitizeData()
rebuildSavedLines()
showDeliveryPoints()
say("загружено маршрутов: " .. #recorder.data.routes .. ", точек сдачи: " .. #recorder.data.deliveryPoints, 100, 200, 255)
say("P — запись/сохранение маршрута, O — поставить постоянную точку", 100, 200, 255)
