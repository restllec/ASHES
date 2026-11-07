-- TaxiCatcher.lua
-- Автоприем и перехват заказов такси для MTA Province (province_taxi)
-- Разработчик: DarkFlame / Ashes Team

local TAXI = {
    enabled = false,
    maxDistance = 1500.0,
    pollInterval = 500,
    soundAlert = true,
    minPrice = 0,
    maxPrice = 999999,
    targetClass = "all", -- all, econom, comfort, business
    hasActiveOrder = false,
    shiftActive = false,
    lastOrderInfo = "нет",
    ordersCount = 0,
    pollTimer = nil,
    status = "Ожидание запуска",
}

local function getTaxiRoot()
    if getResourceFromName and getResourceRootElement then
        local res = getResourceFromName("province_taxi")
        if res then
            local root = getResourceRootElement(res)
            if root then return root end
        end
    end
    if getRootElement then
        return getRootElement()
    end
    return nil
end

local function callTaxiEvent(eventName, ...)
    local root = getTaxiRoot()
    if triggerServerEvent then
        triggerServerEvent(eventName, root or getRootElement(), ...)
    end
    local cleanName = eventName:gsub("^Taxi:", "")
    if exports and exports.province_taxi and exports.province_taxi[cleanName] then
        pcall(exports.province_taxi[cleanName], root or getRootElement(), ...)
    end
end

local function startShift()
    TAXI.shiftActive = true
    TAXI.status = "Выход на смену..."
    callTaxiEvent("Taxi:startTaxiJob", true)
    callTaxiEvent("Taxi:onJobAccepted", true)
    if outputChatBox then
        outputChatBox("[Taxi] Запрос на выход на смену отправлен", 100, 200, 255)
    end
end

local function parseOrderDistance(order, px, py, pz)
    if type(order) ~= "table" then return 999999 end
    if order.distance then return tonumber(order.distance) or 999999 end
    if order.dist then return tonumber(order.dist) or 999999 end
    local ox = order.x or (order.pos and order.pos[1]) or (order.position and order.position.x)
    local oy = order.y or (order.pos and order.pos[2]) or (order.position and order.position.y)
    local oz = order.z or (order.pos and order.pos[3]) or (order.position and order.position.z)
    if ox and oy and pz and getDistanceBetweenPoints3D then
        return getDistanceBetweenPoints3D(px, py, pz, tonumber(ox), tonumber(oy), tonumber(oz or pz))
    end
    return 999999
end

local function acceptOrder(order, dist)
    if not order then return end
    TAXI.hasActiveOrder = true
    local orderId = order.id or order.orderId or order
    callTaxiEvent("Taxi:checkOrder", orderId)

    local clientName = order.name or order.clientName or order.client or "Клиент"
    local orderClass = order.class or order.tariff or "Стандарт"
    local distInt = math.floor(dist or 0)
    TAXI.lastOrderInfo = tostring(clientName) .. " (" .. tostring(orderClass) .. ", " .. tostring(distInt) .. "м)"
    TAXI.status = "Заказ принят: " .. TAXI.lastOrderInfo

    if outputChatBox then
        outputChatBox("[Taxi] Принят заказ: " .. tostring(clientName) .. " [" .. tostring(orderClass) .. "], дистанция " .. tostring(distInt) .. " м", 50, 230, 100)
    end

    if TAXI.soundAlert and playSoundFrontEnd then
        playSoundFrontEnd(40)
    end
end

local function processOrdersList(orders)
    if type(orders) ~= "table" then return end
    if not TAXI.enabled or TAXI.hasActiveOrder then return end

    local count = 0
    local bestOrder = nil
    local bestDist = 999999

    local px, py, pz = 0, 0, 0
    if localPlayer and getElementPosition then
        px, py, pz = getElementPosition(localPlayer)
    end

    for k, order in pairs(orders) do
        count = count + 1
        local dist = parseOrderDistance(order, px, py, pz)
        if dist <= TAXI.maxDistance then
            local price = tonumber(order.price or order.cost or 0)
            if price >= TAXI.minPrice and price <= TAXI.maxPrice then
                if dist < bestDist then
                    bestDist = dist
                    bestOrder = order
                end
            end
        end
    end

    TAXI.ordersCount = count

    if bestOrder then
        acceptOrder(bestOrder, bestDist)
    else
        TAXI.status = "Поиск... Доступно: " .. tostring(count) .. " заказов"
    end
end

-- Обработчики входящих событий со списком заказов
local knownOrderEvents = {
    "Taxi:onReceiveOrders",
    "Taxi:receiveOrders",
    "Taxi:setOrders",
    "Taxi:getOrders",
    "Taxi:updateOrders",
    "Taxi:orders",
    "Taxi:orderList",
    "Taxi:onOrdersList"
}

for _, ev in ipairs(knownOrderEvents) do
    if addEvent and addEventHandler then
        addEvent(ev, true)
        addEventHandler(ev, getRootElement(), function(ordersData)
            processOrdersList(ordersData)
        end)
    end
end

-- Перехват серверного подтверждения взятия заказа
local orderCaughtEvents = {
    "Taxi:onOrderAccepted",
    "Taxi:orderAccepted",
    "Taxi:onTakeOrder",
    "Taxi:takeOrder",
    "Taxi:setClient",
    "Taxi:onSetClient",
    "Taxi:onClientAccepted",
    "Taxi:onOrderCheck",
    "Taxi:onCheckOrder",
    "Taxi:startOrder",
    "Taxi:onStartOrder",
    "Taxi:acceptOrder",
    "Taxi:onAcceptOrder"
}

for _, ev in ipairs(orderCaughtEvents) do
    if addEvent and addEventHandler then
        addEvent(ev, true)
        addEventHandler(ev, getRootElement(), function(...)
            TAXI.hasActiveOrder = true
            TAXI.status = "Заказ зафиксирован (сервер)"
            if dfTaxiUpdate then
                dfTaxiUpdate("has_order", "1")
                dfTaxiUpdate("status", TAXI.status)
            end
            if outputChatBox then
                outputChatBox("[Taxi] Заказ зафиксирован! Автокликер отключен", 50, 230, 100)
            end
        end)
    end
end

-- Сброс состояния заказа при завершении
local finishEvents = {
    "Taxi:onOrderFinished",
    "Taxi:orderFinished",
    "Taxi:onOrderCancelled",
    "Taxi:cancelOrder",
    "Taxi:resetOrder"
}

for _, ev in ipairs(finishEvents) do
    if addEvent and addEventHandler then
        addEvent(ev, true)
        addEventHandler(ev, getRootElement(), function()
            TAXI.hasActiveOrder = false
            TAXI.status = "Поездка завершена. Поиск заказов..."
        end)
    end
end

-- Цикл автоопроса заказов
local function pollOrders()
    if not TAXI.enabled then return end
    if TAXI.hasActiveOrder then return end
    callTaxiEvent("Taxi:getOrders")
end

local function updateTimer()
    if TAXI.pollTimer and isTimer and isTimer(TAXI.pollTimer) then
        killTimer(TAXI.pollTimer)
        TAXI.pollTimer = nil
    end
    if TAXI.enabled and setTimer then
        TAXI.pollTimer = setTimer(pollOrders, math.max(200, TAXI.pollInterval), 0)
    end
end

-- Основной цикл синхронизации с C++ GUI
if setTimer then
    setTimer(function()
        if dfTaxiTakeCommand then
            local cmd = dfTaxiTakeCommand()
            while cmd do
                if cmd == "enable:1" then
                    TAXI.enabled = true
                    TAXI.status = "Автоприем активен"
                    updateTimer()
                    pollOrders()
                elseif cmd == "enable:0" then
                    TAXI.enabled = false
                    TAXI.status = "Остановлен"
                    updateTimer()
                elseif cmd == "start_shift" then
                    startShift()
                elseif cmd == "reset_order" then
                    TAXI.hasActiveOrder = false
                    TAXI.status = "Поиск заказов..."
                elseif cmd == "sound:1" then
                    TAXI.soundAlert = true
                    if dfTaxiUpdate then dfTaxiUpdate("sound", "1") end
                elseif cmd == "sound:0" then
                    TAXI.soundAlert = false
                    if dfTaxiUpdate then dfTaxiUpdate("sound", "0") end
                elseif cmd:sub(1, 9) == "max_dist:" then
                    TAXI.maxDistance = tonumber(cmd:sub(10)) or 1500.0
                    if dfTaxiUpdate then dfTaxiUpdate("max_dist", tostring(math.floor(TAXI.maxDistance))) end
                elseif cmd:sub(1, 10) == "poll_rate:" then
                    TAXI.pollInterval = tonumber(cmd:sub(11)) or 500
                    updateTimer()
                    if dfTaxiUpdate then dfTaxiUpdate("poll_rate", tostring(math.floor(TAXI.pollInterval))) end
                end
                cmd = dfTaxiTakeCommand()
            end
        end

        if dfTaxiUpdate then
            dfTaxiUpdate("loaded", "1")
            dfTaxiUpdate("enabled", TAXI.enabled and "1" or "0")
            dfTaxiUpdate("status", TAXI.status)
            dfTaxiUpdate("orders_count", tostring(TAXI.ordersCount))
            dfTaxiUpdate("last_order", TAXI.lastOrderInfo)
            dfTaxiUpdate("shift_active", TAXI.shiftActive and "1" or "0")
            dfTaxiUpdate("has_order", TAXI.hasActiveOrder and "1" or "0")
        end
    end, 200, 0)
end

