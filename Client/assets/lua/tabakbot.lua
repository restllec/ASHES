-- TABAKBOT v2
-- Логика движения как в jbkbot, маршруты записываются автоматически
--
-- КАК ЗАПИСАТЬ МАРШРУТ:
--   1. Встань на склад > /tabakstorage  (записать склад)
--   2. Встань у точки назначения > /tabakdest  (добавить точку)
--   3. /tabakrecord [номер точки]  — начать запись маршрута к точке
--   4. Иди вручную от склада к точке — координаты пишутся автоматически
--   5. /tabakrecord stop  — остановить запись
--   6. /tabakrecord back [номер]  — записать обратный путь (от точки к складу)
--
-- ЗАПУСК БОТА:
--   /tabakbot — включить/выключить
--   /tabakroutes — GUI просмотра маршрутов

local sw, sh = guiGetScreenSize()
local function log(t) outputChatBox("[TABAK] "..t, 0,200,255,false) end

-- Настройки (объявляем сразу)
local Settings = {
    RotationSpeed   = 5,
    AllowBunnyHop   = false,
    BunnyHopDelay   = 600,
    BunnyHopMinDist = 8.0,
    AutoDisable     = true,
    showStats       = true,
    sprintToStorage = false,
    sprintWithBox   = true,
    autoWalkToDest  = true,   -- идти к зелёной метке если at_dest > 1 сек
}

-- Статистика сессии
local STATS = {
    boxes       = 0,       -- ящиков отнесено
    startTick   = 0,       -- когда запустили бота
}

local function getSessionTime()
    if STATS.startTick == 0 then return "00:00" end
    local sec = math.floor((getTickCount() - STATS.startTick) / 1000)
    return string.format("%02d:%02d", math.floor(sec/60), sec%60)
end

-- Дебаг (объявляем сразу чтобы было доступно везде)
local DBG_ENABLED = false
local dbgLog = {}
local function dbg(t)
    if not DBG_ENABLED then return end
    table.insert(dbgLog, t)
    if #dbgLog > 8 then table.remove(dbgLog, 1) end
    -- outputChatBox убран — только HUD
end

-- =============================================
-- ДАННЫЕ
-- =============================================

-- Склад — одна точка
local STORAGE = {x=2173.784, y=-1505.407, z=1397.539, set=true}

-- Точки назначения (40 штук, собраны сканером)
local DEST_POINTS = {
    [1]  = {x=2161.478, y=-1531.323, z=1397.539, name="T1"},
    [2]  = {x=2161.842, y=-1509.603, z=1397.539, name="T2"},
    [3]  = {x=2171.658, y=-1490.037, z=1397.539, name="T3"},
    [4]  = {x=2161.824, y=-1521.908, z=1397.539, name="T4"},
    [5]  = {x=2175.782, y=-1486.041, z=1397.539, name="T5"},
    [6]  = {x=2176.459, y=-1493.133, z=1397.539, name="T6"},
    [7]  = {x=2171.537, y=-1493.259, z=1397.539, name="T7"},
    [8]  = {x=2171.919, y=-1482.086, z=1397.539, name="T8"},
    [9]  = {x=2176.459, y=-1494.539, z=1397.539, name="T9"},
    [10] = {x=2168.868, y=-1530.622, z=1397.539, name="T10"},
    [11] = {x=2161.677, y=-1517.109, z=1397.539, name="T11"},
    [12] = {x=2162.508, y=-1513.214, z=1397.539, name="T12"},
    [13] = {x=2165.390, y=-1537.748, z=1397.539, name="T13"},
    [14] = {x=2176.062, y=-1489.817, z=1397.539, name="T14"},
    [15] = {x=2172.179, y=-1486.283, z=1397.539, name="T15"},
    [16] = {x=2165.116, y=-1550.222, z=1397.539, name="T16"},
    [17] = {x=2163.069, y=-1523.925, z=1397.539, name="T17"},
    [18] = {x=2172.181, y=-1549.150, z=1397.539, name="T18"},
    [19] = {x=2172.477, y=-1558.521, z=1397.539, name="T19"},
    [20] = {x=2162.043, y=-1525.976, z=1397.539, name="T20"},
    [21] = {x=2176.077, y=-1482.021, z=1397.539, name="T21"},
    [22] = {x=2166.646, y=-1552.553, z=1397.539, name="T22"},
    [23] = {x=2171.618, y=-1545.852, z=1397.539, name="T23"},
    [24] = {x=2172.756, y=-1481.275, z=1397.539, name="T24"},
    [25] = {x=2173.736, y=-1490.134, z=1397.539, name="T25"},
    [26] = {x=2175.780, y=-1484.102, z=1397.539, name="T26"},
    [27] = {x=2173.127, y=-1491.804, z=1397.539, name="T27"},
    [28] = {x=2162.518, y=-1520.793, z=1397.539, name="T28"},
    [29] = {x=2173.010, y=-1543.625, z=1397.539, name="T29"},
    [30] = {x=2175.232, y=-1491.794, z=1397.539, name="T30"},
    [31] = {x=2162.507, y=-1511.298, z=1397.539, name="T31"},
    [32] = {x=2175.783, y=-1487.707, z=1397.539, name="T32"},
    [33] = {x=2166.103, y=-1548.334, z=1397.539, name="T33"},
    [34] = {x=2175.062, y=-1490.968, z=1397.539, name="T34"},
    [35] = {x=2166.109, y=-1540.959, z=1397.539, name="T35"},
    [36] = {x=2166.340, y=-1543.446, z=1397.539, name="T36"},
    [37] = {x=2169.662, y=-1515.516, z=1397.539, name="T37"},
    [38] = {x=2172.541, y=-1491.030, z=1397.539, name="T38"},
    [39] = {x=2163.267, y=-1528.810, z=1397.539, name="T39"},
    [40] = {x=2169.371, y=-1514.641, z=1397.539, name="T40"},
}

-- Маршруты [destIdx] = {to={{x,y,z},...}, back=реверс to}
local routes = {}
local function getRoute(i)
    if not routes[i] then routes[i]={to={},back={}} end
    return routes[i]
end
for i=1,40 do routes[i]={to={},back={}} end

-- T1-T6 (из лога)
routes[1].to = {{2173.5,-1505.2,1397.539},{2170.6,-1505.4,1397.539},{2170.1,-1506.2,1397.539},{2169.7,-1508.1,1397.539},{2169.1,-1509.7,1397.539},{2167.5,-1509.6,1397.539},{2165.3,-1509.7,1397.539},{2163.6,-1509.7,1397.539},{2163.1,-1510.6,1397.539},{2163.1,-1512.6,1397.539},{2163.1,-1515.7,1397.539},{2162.9,-1519.0,1397.539},{2163.3,-1522.8,1397.539},{2163.4,-1527.5,1397.539},{2163.4,-1531.4,1397.539},{2161.6,-1531.5,1397.539}}
routes[2].to = {{2172.2,-1505.4,1397.539},{2170.5,-1505.5,1397.539},{2169.8,-1506.1,1397.539},{2169.8,-1507.4,1397.539},{2169.4,-1509.0,1397.539},{2168.5,-1509.4,1397.539},{2166.5,-1509.5,1397.539},{2163.5,-1509.4,1397.539},{2161.8,-1509.5,1397.539}}
routes[3].to = {{2172.2,-1505.4,1397.539},{2170.7,-1505.4,1397.539},{2169.8,-1505.1,1397.539},{2169.7,-1504.4,1397.539},{2169.7,-1503.3,1397.539},{2170.8,-1501.8,1397.539},{2171.3,-1500.7,1397.539},{2171.5,-1497.7,1397.539},{2171.1,-1495.0,1397.539},{2171.1,-1492.8,1397.539},{2171.6,-1489.9,1397.539}}
routes[4].to = {{2172.4,-1505.3,1397.539},{2171.4,-1505.3,1397.539},{2170.2,-1505.5,1397.539},{2170.0,-1506.6,1397.539},{2169.8,-1508.1,1397.539},{2169.3,-1509.9,1397.539},{2167.4,-1510.1,1397.539},{2165.5,-1509.9,1397.539},{2164.1,-1510.1,1397.539},{2163.2,-1511.5,1397.539},{2163.0,-1513.7,1397.539},{2163.1,-1516.6,1397.539},{2163.1,-1518.3,1397.539},{2163.0,-1520.8,1397.539},{2162.9,-1521.9,1397.539},{2161.8,-1521.9,1397.539}}
routes[5].to = {{2173.1,-1505.4,1397.539},{2171.9,-1505.5,1397.539},{2170.3,-1505.5,1397.539},{2169.8,-1505.0,1397.539},{2170.1,-1503.4,1397.539},{2170.6,-1502.3,1397.539},{2174.3,-1498.2,1397.539},{2176.8,-1496.0,1397.539},{2177.0,-1493.2,1397.539},{2176.9,-1489.6,1397.539},{2176.9,-1486.7,1397.539},{2175.8,-1486.2,1397.539}}
routes[6].to = {{2172.8,-1505.5,1397.539},{2171.9,-1505.5,1397.539},{2169.9,-1505.5,1397.539},{2169.7,-1503.8,1397.539},{2169.7,-1503.0,1397.539},{2170.9,-1501.6,1397.539},{2176.5,-1496.7,1397.539},{2177.0,-1494.8,1397.539},{2176.5,-1493.1,1397.539}}
-- T7-T35 (из routetabak.txt)
routes[7].to = {{2173.040,-1505.410,1397.539},{2170.620,-1505.190,1397.539},{2169.830,-1504.480,1397.539},{2169.620,-1503.530,1397.539},{2170.460,-1501.620,1397.539},{2171.200,-1499.950,1397.539},{2171.310,-1497.720,1397.539},{2171.260,-1495.420,1397.539},{2171.540,-1493.310,1397.539}}
routes[8].to = {{2173.400,-1505.520,1397.539},{2172.360,-1505.510,1397.539},{2170.860,-1505.500,1397.539},{2169.660,-1505.150,1397.539},{2169.990,-1503.360,1397.539},{2170.580,-1501.770,1397.539},{2171.660,-1500.160,1397.539},{2171.400,-1497.130,1397.539},{2171.120,-1494.030,1397.539},{2171.010,-1491.090,1397.539},{2170.970,-1488.230,1397.539},{2171.060,-1485.460,1397.539},{2172.170,-1482.270,1397.539}}
routes[9].to = {{2172.860,-1505.290,1397.539},{2171.450,-1505.430,1397.539},{2169.950,-1505.180,1397.539},{2169.830,-1504.610,1397.539},{2169.870,-1502.700,1397.539},{2171.930,-1501.430,1397.539},{2175.940,-1497.900,1397.539},{2176.840,-1495.440,1397.539},{2176.460,-1494.450,1397.539}}
routes[10].to = {{2173.450,-1505.480,1397.539},{2172.230,-1505.480,1397.539},{2170.180,-1505.540,1397.539},{2169.940,-1506.890,1397.539},{2168.950,-1509.250,1397.539},{2167.810,-1511.110,1397.539},{2167.770,-1514.490,1397.539},{2167.850,-1519.100,1397.539},{2168.040,-1522.960,1397.539},{2168.050,-1526.160,1397.539},{2168.760,-1530.460,1397.539}}
routes[11].to = {{2172.910,-1505.330,1397.539},{2171.450,-1505.400,1397.539},{2170.150,-1506.130,1397.539},{2169.470,-1509.490,1397.539},{2165.760,-1510.140,1397.539},{2163.770,-1510.970,1397.539},{2162.840,-1513.160,1397.539},{2162.980,-1517.260,1397.539},{2161.680,-1517.270,1397.539}}
routes[12].to = {{2173.030,-1505.550,1397.539},{2171.860,-1505.480,1397.539},{2170.570,-1505.550,1397.539},{2170.170,-1506.010,1397.539},{2169.800,-1507.170,1397.539},{2169.510,-1508.460,1397.539},{2167.370,-1509.640,1397.539},{2164.310,-1510.470,1397.539},{2162.550,-1513.380,1397.539}}
routes[13].to = {{2173.560,-1505.450,1397.539},{2173.430,-1507.010,1397.539},{2173.300,-1511.060,1397.539},{2173.380,-1514.800,1397.539},{2173.460,-1518.380,1397.539},{2173.440,-1522.100,1397.539},{2173.420,-1525.570,1397.539},{2173.410,-1529.530,1397.539},{2173.390,-1533.440,1397.539},{2173.380,-1535.470,1397.539},{2173.430,-1537.890,1397.539},{2171.320,-1537.960,1397.539},{2168.330,-1537.920,1397.539},{2166.130,-1537.740,1397.539},{2165.300,-1537.760,1397.539}}
routes[14].to = {{2173.100,-1505.320,1397.539},{2171.230,-1505.060,1397.539},{2169.710,-1504.850,1397.539},{2169.600,-1503.100,1397.539},{2170.970,-1501.450,1397.539},{2175.940,-1496.930,1397.539},{2176.800,-1496.060,1397.539},{2176.840,-1494.500,1397.539},{2176.810,-1491.790,1397.539},{2176.140,-1490.010,1397.539}}
routes[15].to = {{2173.200,-1505.480,1397.539},{2171.820,-1505.560,1397.539},{2170.260,-1505.480,1397.539},{2169.570,-1504.240,1397.539},{2169.870,-1501.820,1397.539},{2171.800,-1500.710,1397.539},{2171.610,-1497.470,1397.539},{2170.960,-1495.170,1397.539},{2170.900,-1491.490,1397.539},{2170.910,-1488.320,1397.539},{2170.880,-1486.700,1397.539},{2172.170,-1486.190,1397.539}}
routes[16].to = {{2173.370,-1505.950,1397.539},{2173.380,-1507.460,1397.539},{2173.380,-1509.980,1397.539},{2173.450,-1514.170,1397.539},{2173.440,-1518.080,1397.539},{2173.420,-1522.460,1397.539},{2173.390,-1527.560,1397.539},{2173.360,-1533.550,1397.539},{2173.350,-1536.110,1397.539},{2172.870,-1538.350,1397.539},{2171.150,-1538.400,1397.539},{2169.880,-1538.530,1397.539},{2168.130,-1538.570,1397.539},{2167.170,-1540.100,1397.539},{2166.520,-1541.210,1397.539},{2166.710,-1543.380,1397.539},{2166.670,-1545.460,1397.539},{2166.630,-1547.530,1397.539},{2166.590,-1549.470,1397.539},{2165.320,-1550.160,1397.539}}
routes[17].to = {{2173.160,-1505.340,1397.539},{2171.440,-1505.300,1397.539},{2170.090,-1505.990,1397.539},{2169.830,-1507.640,1397.539},{2169.600,-1509.030,1397.539},{2167.610,-1509.640,1397.539},{2165.500,-1509.780,1397.539},{2163.730,-1510.800,1397.539},{2163.110,-1512.640,1397.539},{2163.170,-1515.120,1397.539},{2163.180,-1517.360,1397.539},{2163.170,-1519.440,1397.539},{2163.180,-1521.880,1397.539},{2163.070,-1523.950,1397.539}}
routes[18].to = {{2173.490,-1506.030,1397.539},{2173.520,-1507.640,1397.539},{2173.330,-1510.180,1397.539},{2173.210,-1512.020,1397.539},{2173.310,-1513.970,1397.539},{2173.390,-1516.750,1397.539},{2173.360,-1519.630,1397.539},{2173.400,-1522.270,1397.539},{2173.500,-1525.290,1397.539},{2173.470,-1528.040,1397.539},{2173.410,-1530.470,1397.539},{2173.370,-1533.620,1397.539},{2173.370,-1535.760,1397.539},{2172.950,-1537.860,1397.539},{2172.280,-1539.920,1397.539},{2171.690,-1542.340,1397.539},{2171.340,-1544.440,1397.539},{2171.300,-1546.570,1397.539},{2171.430,-1548.760,1397.539},{2172.480,-1549.270,1397.539}}
routes[19].to = {{2173.720,-1506.190,1397.539},{2173.460,-1509.720,1397.539},{2173.420,-1512.620,1397.539},{2173.400,-1516.130,1397.539},{2173.420,-1520.660,1397.539},{2173.380,-1524.920,1397.539},{2173.340,-1528.990,1397.539},{2173.310,-1533.610,1397.539},{2173.330,-1536.330,1397.539},{2172.890,-1538.150,1397.539},{2172.190,-1540.660,1397.539},{2171.660,-1542.740,1397.539},{2171.440,-1544.520,1397.539},{2171.380,-1546.850,1397.539},{2171.360,-1550.500,1397.539},{2171.450,-1552.620,1397.539},{2171.570,-1554.840,1397.539},{2171.760,-1556.940,1397.539},{2172.480,-1558.450,1397.539}}
routes[20].to = {{2173.420,-1504.880,1397.539},{2172.130,-1505.190,1397.539},{2170.540,-1505.540,1397.539},{2169.960,-1507.030,1397.539},{2169.280,-1509.020,1397.539},{2168.560,-1509.450,1397.539},{2167.080,-1509.940,1397.539},{2165.550,-1510.130,1397.539},{2164.200,-1510.680,1397.539},{2163.460,-1512.110,1397.539},{2163.050,-1514.390,1397.539},{2163.030,-1516.470,1397.539},{2163.010,-1518.670,1397.539},{2163.120,-1521.280,1397.539},{2163.390,-1522.610,1397.539},{2163.360,-1524.590,1397.539},{2163.320,-1525.670,1397.539},{2162.040,-1526.310,1397.539}}
routes[21].to = {{2173.290,-1505.340,1397.539},{2172.080,-1505.520,1397.539},{2170.490,-1505.500,1397.539},{2169.650,-1504.980,1397.539},{2170.090,-1502.420,1397.539},{2173.380,-1499.320,1397.539},{2174.950,-1497.740,1397.539},{2176.420,-1496.470,1397.539},{2177.120,-1495.530,1397.539},{2176.990,-1493.220,1397.539},{2176.900,-1490.800,1397.539},{2176.860,-1488.250,1397.539},{2176.910,-1485.850,1397.539},{2176.870,-1483.830,1397.539},{2176.840,-1482.020,1397.539},{2176.110,-1481.880,1397.539}}
routes[22].to = {{2173.450,-1506.160,1397.539},{2173.520,-1507.800,1397.539},{2173.610,-1509.850,1397.539},{2173.550,-1511.760,1397.539},{2173.440,-1514.510,1397.539},{2173.470,-1518.010,1397.539},{2173.470,-1521.040,1397.539},{2173.470,-1524.260,1397.539},{2173.440,-1527.340,1397.539},{2173.340,-1530.540,1397.539},{2173.410,-1534.090,1397.539},{2173.350,-1536.670,1397.539},{2171.940,-1538.290,1397.539},{2169.360,-1538.570,1397.539},{2167.750,-1539.040,1397.539},{2166.980,-1540.720,1397.539},{2166.800,-1542.970,1397.539},{2166.680,-1545.220,1397.539},{2166.640,-1547.600,1397.539},{2166.700,-1549.930,1397.539},{2166.760,-1551.160,1397.539},{2166.650,-1552.510,1397.539}}
routes[23].to = {{2173.630,-1506.380,1397.539},{2173.450,-1508.530,1397.539},{2173.470,-1511.180,1397.539},{2173.480,-1514.000,1397.539},{2173.470,-1517.240,1397.539},{2173.450,-1521.550,1397.539},{2173.430,-1524.870,1397.539},{2173.420,-1527.850,1397.539},{2173.400,-1530.990,1397.539},{2173.380,-1534.720,1397.539},{2173.260,-1537.810,1397.539},{2172.100,-1541.940,1397.539},{2171.590,-1543.820,1397.539},{2171.780,-1546.090,1397.539}}
routes[24].to = {{2173.010,-1505.530,1397.539},{2171.800,-1505.470,1397.539},{2170.450,-1505.510,1397.539},{2169.650,-1504.900,1397.539},{2170.080,-1503.040,1397.539},{2170.500,-1501.980,1397.539},{2171.320,-1499.380,1397.539},{2171.450,-1497.200,1397.539},{2171.330,-1494.500,1397.539},{2171.200,-1491.160,1397.539},{2170.930,-1489.990,1397.539},{2170.950,-1487.800,1397.539},{2171.020,-1485.630,1397.539},{2171.180,-1483.630,1397.539},{2171.560,-1481.640,1397.539},{2172.780,-1481.310,1397.539}}
routes[25].to = {{2173.220,-1505.380,1397.539},{2172.220,-1505.370,1397.539},{2170.700,-1505.420,1397.539},{2169.680,-1504.820,1397.539},{2170.010,-1503.020,1397.539},{2170.960,-1501.850,1397.539},{2173.440,-1500.180,1397.539},{2174.800,-1498.850,1397.539},{2176.030,-1496.990,1397.539},{2176.800,-1495.280,1397.539},{2176.770,-1493.120,1397.539},{2176.420,-1491.400,1397.539},{2175.380,-1491.360,1397.539},{2174.410,-1491.310,1397.539},{2173.810,-1490.950,1397.539},{2173.740,-1490.140,1397.539}}
routes[26].to = {{2172.840,-1505.420,1397.539},{2171.730,-1505.380,1397.539},{2170.290,-1505.390,1397.539},{2169.740,-1504.280,1397.539},{2170.200,-1502.320,1397.539},{2171.440,-1501.430,1397.539},{2173.440,-1500.360,1397.539},{2175.070,-1499.060,1397.539},{2176.270,-1497.340,1397.539},{2176.820,-1495.320,1397.539},{2177.000,-1492.800,1397.539},{2177.030,-1490.590,1397.539},{2177.060,-1488.190,1397.539},{2177.070,-1486.090,1397.539},{2175.920,-1484.280,1397.539}}
routes[27].to = {{2173.170,-1505.290,1397.539},{2171.960,-1505.490,1397.539},{2170.410,-1505.520,1397.539},{2169.480,-1504.500,1397.539},{2169.850,-1502.810,1397.539},{2171.040,-1501.520,1397.539},{2171.530,-1499.140,1397.539},{2171.440,-1497.150,1397.539},{2171.270,-1494.690,1397.539},{2171.420,-1492.090,1397.539},{2172.340,-1491.360,1397.539},{2173.330,-1491.800,1397.539}}
routes[28].to = {{2173.340,-1505.150,1397.539},{2172.570,-1505.290,1397.539},{2171.150,-1505.650,1397.539},{2170.180,-1506.240,1397.539},{2169.730,-1507.690,1397.539},{2168.860,-1509.710,1397.539},{2166.650,-1510.160,1397.539},{2164.260,-1510.770,1397.539},{2163.140,-1512.480,1397.539},{2162.880,-1515.320,1397.539},{2163.060,-1518.070,1397.539},{2162.590,-1520.690,1397.539}}
routes[29].to = {{2173.220,-1505.920,1397.539},{2173.390,-1507.600,1397.539},{2173.510,-1510.280,1397.539},{2173.600,-1512.760,1397.539},{2173.670,-1515.500,1397.539},{2173.640,-1517.980,1397.539},{2173.610,-1520.890,1397.539},{2173.580,-1523.640,1397.539},{2173.540,-1526.530,1397.539},{2173.460,-1529.550,1397.539},{2173.440,-1532.090,1397.539},{2173.470,-1534.890,1397.539},{2173.360,-1537.750,1397.539},{2173.220,-1540.720,1397.539},{2173.140,-1543.660,1397.539}}
routes[30].to = {{2173.240,-1505.270,1397.539},{2171.940,-1505.400,1397.539},{2170.140,-1505.430,1397.539},{2169.410,-1504.560,1397.539},{2169.840,-1503.300,1397.539},{2170.790,-1501.880,1397.539},{2174.840,-1498.400,1397.539},{2176.270,-1496.820,1397.539},{2177.010,-1494.680,1397.539},{2176.980,-1493.160,1397.539},{2177.080,-1491.540,1397.539},{2175.170,-1491.800,1397.539}}
routes[31].to = {{2173.210,-1505.260,1397.539},{2172.050,-1505.210,1397.539},{2170.810,-1505.440,1397.539},{2170.140,-1506.180,1397.539},{2169.740,-1507.870,1397.539},{2168.780,-1509.490,1397.539},{2166.640,-1509.890,1397.539},{2164.080,-1511.180,1397.539},{2162.510,-1511.350,1397.539}}
routes[32].to = {{2173.000,-1505.490,1397.539},{2171.720,-1505.520,1397.539},{2170.280,-1505.410,1397.539},{2169.630,-1504.170,1397.539},{2170.160,-1502.760,1397.539},{2171.520,-1501.470,1397.539},{2172.970,-1500.520,1397.539},{2174.580,-1499.060,1397.539},{2175.910,-1497.400,1397.539},{2176.740,-1495.640,1397.539},{2177.050,-1493.510,1397.539},{2176.960,-1491.320,1397.539},{2176.610,-1489.250,1397.539},{2175.780,-1487.640,1397.539}}
routes[33].to = {{2173.330,-1506.160,1397.539},{2173.390,-1508.040,1397.539},{2173.520,-1511.170,1397.539},{2173.590,-1514.220,1397.539},{2173.590,-1517.230,1397.539},{2173.580,-1520.570,1397.539},{2173.470,-1523.840,1397.539},{2173.420,-1527.210,1397.539},{2173.360,-1530.900,1397.539},{2173.420,-1534.330,1397.539},{2173.090,-1536.920,1397.539},{2171.320,-1538.370,1397.539},{2168.850,-1538.160,1397.539},{2167.380,-1539.280,1397.539},{2166.680,-1541.750,1397.539},{2166.580,-1544.320,1397.539},{2166.570,-1547.500,1397.539}}
routes[34].to = {{2172.850,-1505.450,1397.539},{2171.510,-1505.410,1397.539},{2170.000,-1505.180,1397.539},{2169.710,-1503.610,1397.539},{2170.530,-1501.980,1397.539},{2171.850,-1501.080,1397.539},{2174.060,-1498.960,1397.539},{2175.470,-1497.200,1397.539},{2177.020,-1496.020,1397.539},{2176.990,-1494.810,1397.539},{2177.100,-1492.870,1397.539},{2176.920,-1491.810,1397.539},{2175.140,-1490.970,1397.539}}
routes[35].to = {{2173.470,-1506.180,1397.539},{2173.540,-1508.840,1397.539},{2173.580,-1511.230,1397.539},{2173.580,-1514.190,1397.539},{2173.570,-1517.390,1397.539},{2173.510,-1520.510,1397.539},{2173.430,-1523.590,1397.539},{2173.380,-1526.100,1397.539},{2173.330,-1528.820,1397.539},{2173.320,-1531.370,1397.539},{2173.320,-1534.730,1397.539},{2173.080,-1537.610,1397.539},{2172.190,-1537.700,1397.539},{2170.300,-1538.240,1397.539},{2168.320,-1538.640,1397.539},{2167.140,-1539.360,1397.539},{2166.110,-1540.860,1397.539}}
routes[36].to = {{2173.580,-1506.600,1397.539},{2173.510,-1509.030,1397.539},{2173.500,-1511.160,1397.539},{2173.490,-1513.420,1397.539},{2173.470,-1515.780,1397.539},{2173.460,-1517.980,1397.539},{2173.420,-1520.390,1397.539},{2173.380,-1522.980,1397.539},{2173.350,-1526.990,1397.539},{2173.350,-1529.900,1397.539},{2173.350,-1532.970,1397.539},{2173.350,-1535.490,1397.539},{2172.760,-1537.520,1397.539},{2170.800,-1538.240,1397.539},{2168.580,-1538.470,1397.539},{2166.880,-1540.020,1397.539},{2166.640,-1542.600,1397.539}}
routes[37].to = {{2172.760,-1505.470,1397.539},{2171.400,-1505.420,1397.539},{2170.090,-1505.670,1397.539},{2169.960,-1507.120,1397.539},{2169.230,-1508.700,1397.539},{2167.790,-1510.440,1397.539},{2167.760,-1511.760,1397.539},{2167.830,-1513.170,1397.539},{2167.940,-1515.080,1397.539},{2169.700,-1515.520,1397.539}}
routes[38].to = {{2172.880,-1505.520,1397.539},{2171.520,-1505.480,1397.539},{2170.200,-1505.330,1397.539},{2169.870,-1504.050,1397.539},{2170.660,-1502.180,1397.539},{2171.830,-1500.120,1397.539},{2171.650,-1498.190,1397.539},{2171.320,-1495.400,1397.539},{2171.360,-1492.900,1397.539},{2171.730,-1491.370,1397.539}}
routes[39].to = {{2173.180,-1505.240,1397.539},{2171.110,-1505.520,1397.539},{2170.110,-1506.350,1397.539},{2169.540,-1508.850,1397.539},{2167.770,-1510.200,1397.539},{2167.810,-1512.300,1397.539},{2167.860,-1515.030,1397.539},{2167.890,-1517.820,1397.539},{2167.920,-1521.040,1397.539},{2168.000,-1524.040,1397.539},{2168.060,-1526.200,1397.539},{2168.020,-1528.390,1397.539},{2167.440,-1530.190,1397.539},{2165.570,-1530.960,1397.539},{2163.580,-1530.520,1397.539},{2163.230,-1528.600,1397.539}}
routes[40].to = {{2172.810,-1505.400,1397.539},{2171.650,-1505.430,1397.539},{2170.130,-1505.770,1397.539},{2169.660,-1506.610,1397.539},{2169.710,-1508.370,1397.539},{2167.800,-1510.400,1397.539},{2167.770,-1511.470,1397.539},{2167.850,-1513.340,1397.539},{2167.920,-1514.630,1397.539},{2169.300,-1514.720,1397.539}}

-- Генерируем back как реверс to
for i=1,40 do
    if #routes[i].to > 0 then
        local rev = {}
        for j=#routes[i].to,1,-1 do table.insert(rev, routes[i].to[j]) end
        routes[i].back = rev
    end
end

-- =============================================
-- ЗАПИСЬ МАРШРУТА
-- =============================================

local REC = {
    active   = false,
    destIdx  = 0,
    dir      = "to",   -- "to" или "back"
    points   = {},
    lastPos  = nil,
    MIN_DIST = 1.5,    -- минимальное расстояние между точками
}

local recTimer = nil

local function startRecord(destIdx, dir)
    if REC.active then
        log("Сначала останови запись: /tabakrecord stop")
        return
    end
    if destIdx < 1 or destIdx > #DEST_POINTS then
        log("Нет точки #"..destIdx..". Добавь через /tabakdest")
        return
    end
    REC.active  = true
    REC.destIdx = destIdx
    REC.dir     = dir or "to"
    REC.points  = {}   -- всегда начинаем с чистого листа
    REC.lastPos = nil

    recTimer = setTimer(function()
        if not REC.active then return end
        local x,y,z = getElementPosition(localPlayer)
        if REC.lastPos then
            local d = math.sqrt((x-REC.lastPos[1])^2+(y-REC.lastPos[2])^2)
            if d < REC.MIN_DIST then return end
        end
        table.insert(REC.points, {x,y,z})
        REC.lastPos = {x,y,z}
    end, 300, 0)

    local dp = DEST_POINTS[destIdx]
    log(string.format("? Запись маршрута #%d '%s' [%s] — иди вручную!",
        destIdx, dp.name, dir == "to" and "к точке" or "обратно"))
    log("Останови: /tabakrecord stop")
end

local function stopRecord(save)
    if not REC.active then log("Запись не активна"); return end
    if recTimer and isTimer(recTimer) then killTimer(recTimer) end
    recTimer = nil
    REC.active = false

    if save ~= false then
        local r = getRoute(REC.destIdx)
        local dst = REC.dir == "to" and r.to or r.back
        for k in pairs(dst) do dst[k]=nil end
        for _,p in ipairs(REC.points) do table.insert(dst,{p[1],p[2],p[3]}) end
        log(string.format("? Сохранено: точка#%d [%s] %d точек",
            REC.destIdx, REC.dir, #REC.points))
    else
        log("Запись отменена")
    end
    REC.points = {}
end

addCommandHandler("tabakrecord", function(cmd, arg1, arg2)
    if arg1 == "stop" then
        stopRecord(true)
    elseif arg1 == "cancel" then
        stopRecord(false)
    elseif arg1 == "back" then
        local idx = tonumber(arg2) or 1
        startRecord(idx, "back")
    elseif tonumber(arg1) then
        startRecord(tonumber(arg1), "to")
    else
        log("Использование:")
        log("  /tabakrecord [номер] — запись пути К точке")
        log("  /tabakrecord back [номер] — запись пути ОБРАТНО")
        log("  /tabakrecord stop — сохранить и остановить")
        log("  /tabakrecord cancel — отменить без сохранения")
        log("Точек назначения: "..#DEST_POINTS)
        for i,p in ipairs(DEST_POINTS) do
            local r = routes[i]
            log(string.format("  #%d '%s' > %d точек | < %d точек",
                i, p.name,
                r and #r.to or 0,
                r and #r.back or 0))
        end
    end
end)

-- Записать склад
addCommandHandler("tabakstorage", function()
    local x,y,z = getElementPosition(localPlayer)
    STORAGE.x,STORAGE.y,STORAGE.z,STORAGE.set = x,y,z,true
    log(string.format("? Склад записан: %.2f, %.2f, %.2f", x,y,z))
end)

-- Добавить точку назначения (текущая позиция)
addCommandHandler("tabakdest", function(cmd, name)
    local x,y,z = getElementPosition(localPlayer)
    name = name or ("Точка #"..(#DEST_POINTS+1))
    table.insert(DEST_POINTS, {x=x,y=y,z=z,name=name})
    local idx = #DEST_POINTS
    routes[idx] = {to={},back={}}
    log(string.format("? Точка #%d '%s' добавлена: %.2f, %.2f, %.2f", idx, name, x,y,z))
    log("Теперь: /tabakrecord "..idx.." — запиши маршрут к ней")
end)

-- =============================================
-- ДВИЖЕНИЕ БОТА
-- =============================================

local BOT = {
    enabled     = false,
    phase       = "idle",
    destIdx     = 0,
    cameraAngle = 0,
    isReset     = true,
}

-- Маркеры пути (как в jbkbot STORAGE.MARKERS)
local WP = {
    markers = {},   -- список маркеров
    target  = nil,  -- текущий маркер-цель
    idx     = 0,    -- индекс текущего
    finish  = nil,  -- финальный маркер (зелёный от сервера)
    isPoint = true, -- true=идём к точке, false=идём на склад
}

local storageTimer = 0  -- когда пришли на склад

-- Утилиты
local function d3(x1,y1,z1,x2,y2,z2) return math.sqrt((x2-x1)^2+(y2-y1)^2+(z2-z1)^2) end
local function lerp(a,b,t) local d=b-a; if d>180 then d=d-360 elseif d<-180 then d=d+360 end; return a+d*t end
local function norm(a) return (a%360+360)%360 end
local function ang(tx,ty) local px,py=getElementPosition(localPlayer); local t=-math.deg(math.atan2(tx-px,ty-py)); return t<0 and t+360 or t end

local function mkm(x,y,z)
    local m=createMarker(x,y,z,"cylinder",1,0,0,0,0)
    setElementInterior(m,getElementInterior(localPlayer))
    setElementDimension(m,getElementDimension(localPlayer))
    return m
end

local function clearWP()
    for _,m in ipairs(WP.markers) do
        -- Не уничтожаем финальный серверный маркер (зелёный)
        if isElement(m) and m ~= WP.finish then
            destroyElement(m)
        end
    end
    WP.markers={}; WP.target=nil; WP.idx=0; WP.finish=nil
end

local function getGreenMarker()
    for _,m in ipairs(getElementsByType("marker",root,true)) do
        local r,g,b,a=getMarkerColor(m)
        if r==0 and g==155 and b==0 and a==170 then return m end
    end
    return nil
end

local function nearestDest(marker)
    local mx,my,mz=getElementPosition(marker)
    local best,bd=1,math.huge
    for i,p in ipairs(DEST_POINTS) do
        local d=d3(mx,my,mz,p.x,p.y,p.z)
        if d<bd then bd=d;best=i end
    end
    dbg(string.format("nearestDest: T%d dist=%.1f",best,bd))
    return best
end

-- Построить путь: вейпоинты из таблицы + финальный зелёный маркер сервера
local function buildWP(pts, greenMarker)
    clearWP()
    WP.idx = 0  -- сброс до создания маркеров
    -- Вейпоинты
    for _,p in ipairs(pts) do
        local m=mkm(p[1],p[2],p[3])
        table.insert(WP.markers,m)
    end
    -- Финальный — реальный зелёный маркер (как в jbkbot)
    if greenMarker and isElement(greenMarker) then
        table.insert(WP.markers, greenMarker)
        WP.finish = greenMarker
    end
    if #WP.markers>0 then WP.target=WP.markers[1]; WP.idx=1 end
    dbg(string.format("buildWP: %d маркеров",#WP.markers))
end

-- Следующий маркер или финал
local function advanceWP()
    if WP.idx < #WP.markers then
        WP.idx=WP.idx+1
        WP.target=WP.markers[WP.idx]
        dbg(string.format("WP %d/%d",WP.idx,#WP.markers))
    else
        -- Финал
        setPedControlState(localPlayer,"forwards",false)
        setPedControlState(localPlayer,"sprint",false)
        clearWP()
        BOT.isReset=true
        if WP.isPoint then
            BOT.phase="at_dest"
            dbg("Финал > at_dest T"..BOT.destIdx)
        else
            -- Дошли до склада — сразу проверяем маркер
            lastMarkerPos = nil  -- сброс чтобы at_storage мог подхватить
            BOT.phase="at_storage"
            storageTimer=0
            dbg("Финал > at_storage")
        end
    end
end

-- Основной цикл движения (как в jbkbot onUpdate)
addEventHandler("onClientPreRender", root, function()
    if not BOT.enabled then return end

    -- walk_to_marker — шагом к зелёной метке
    if BOT.phase == "walk_to_marker" then
        if not isElement(WP.target) then
            BOT.phase = "at_dest"; return
        end
        local tx,ty = getElementPosition(WP.target)
        local px,py = getElementPosition(localPlayer)
        local d = math.sqrt((tx-px)^2+(ty-py)^2)
        if d < 1.2 then
            setPedControlState(localPlayer,"forwards",false)
            setPedControlState(localPlayer,"sprint",false)
            clearWP(); BOT.phase = "at_dest"
            return
        end
        if BOT.isReset then BOT.cameraAngle=-(getPedCameraRotation(localPlayer)); BOT.isReset=false end
        BOT.cameraAngle=norm(lerp(BOT.cameraAngle,ang(tx,ty),0.08))
        setPedCameraRotation(localPlayer,BOT.cameraAngle)
        setPedControlState(localPlayer,"forwards",true)
        setPedControlState(localPlayer,"sprint",false)
        return
    end

    if BOT.phase ~= "moving" then
        setPedControlState(localPlayer,"forwards",false)
        setPedControlState(localPlayer,"sprint",false)
        return
    end
    if not isElement(WP.target) then
        setPedControlState(localPlayer,"forwards",false)
        setPedControlState(localPlayer,"sprint",false)
        BOT.isReset=true
        dbg("WP.target не элемент! idx="..WP.idx.." markers="..#WP.markers)
        -- Если мы на последней точке или за ней — завершаем маршрут принудительно
        if WP.idx >= #WP.markers then
            clearWP()
            BOT.isReset=true
            if WP.isPoint then
                BOT.phase="at_dest"
                dbg("Принудительный финал > at_dest")
            else
                BOT.phase="at_storage"
                storageTimer=0
                lastMarkerPos=nil
                dbg("Принудительный финал > at_storage")
            end
        end
        return
    end

    local tx,ty=getElementPosition(WP.target)
    if BOT.isReset then BOT.cameraAngle=-(getPedCameraRotation(localPlayer)); BOT.isReset=false end
    BOT.cameraAngle=norm(lerp(BOT.cameraAngle,ang(tx,ty), Settings.RotationSpeed/100))
    setPedCameraRotation(localPlayer,BOT.cameraAngle)
    setPedControlState(localPlayer,"forwards",true)
    -- Последние 2 маркера — шаг. На складе — зависит от настройки
    local nearEnd = WP.idx >= #WP.markers-1
    local doSprint = (not nearEnd) and (WP.isPoint and Settings.sprintWithBox or (not WP.isPoint and Settings.sprintToStorage))
    setPedControlState(localPlayer,"sprint", doSprint)
end)

-- Захват маркеров по расстоянию
setTimer(function()
    if not BOT.enabled or BOT.phase~="moving" then return end
    if not isElement(WP.target) then
        -- Если на последней точке — завершаем принудительно
        if WP.idx >= #WP.markers then
            setPedControlState(localPlayer,"forwards",false)
            setPedControlState(localPlayer,"sprint",false)
            clearWP(); BOT.isReset=true
            if WP.isPoint then
                BOT.phase="at_dest"
            else
                BOT.phase="at_storage"; storageTimer=0; lastMarkerPos=nil
            end
        end
        return
    end
    -- Защита от выхода за пределы массива
    if WP.idx > #WP.markers then
        dbg("WP.idx > #WP.markers! Сброс")
        WP.idx = #WP.markers
        WP.target = WP.markers[WP.idx]
        return
    end

    local px,py,pz=getElementPosition(localPlayer)
    local tx,ty,tz=getElementPosition(WP.target)
    -- Используем только 2D расстояние (как в jbkbot)
    local d=math.sqrt((tx-px)^2+(ty-py)^2)

    -- Последние 2 — шаг
    if WP.idx >= #WP.markers-1 then
        setPedControlState(localPlayer,"sprint",false)
    end

    local r = WP.idx >= #WP.markers-1 and 1.5 or 1.8
    if d < r then advanceWP() end
end, 100, 0)

-- Ждём маркер на складе
setTimer(function()
    if not BOT.enabled or BOT.phase~="at_storage" then
        if BOT.phase~="at_storage" then storageTimer=0 end
        return
    end
    if storageTimer==0 then storageTimer=getTickCount(); return end
    -- Минимальная задержка 300мс чтобы сервер успел обновить маркер
    if getTickCount()-storageTimer < 300 then return end

    local gm=getGreenMarker()
    if not gm then
        -- Маркер ещё не появился — продолжаем ждать до 5 сек
        if getTickCount()-storageTimer > 5000 then
            storageTimer=getTickCount()  -- сброс таймера, ждём ещё
        end
        return
    end
    local mx,my,mz=getElementPosition(gm)
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z)<8 then return end

    local idx=nearestDest(gm)
    local dp=DEST_POINTS[idx]
    if not dp then return end

    storageTimer=0
    BOT.destIdx=idx
    BOT.phase="moving"
    BOT.isReset=true
    WP.isPoint=true
    buildWP(routes[idx].to, gm)
    log(string.format("Маркер > T%d (%d вейпоинтов)",idx,#WP.markers))
end, 100, 0)

-- На точке — ждём мини-игру или смену маркера
local atDestTick = 0  -- когда вошли в at_dest

-- Следим за маркером пока идём на склад — при низком пинге маркер
-- появляется раньше чем бот доходит до склада
local lastMarkerPos = nil
setTimer(function()
    if not BOT.enabled then return end
    -- Работает только когда идём на склад (isPoint=false)
    if BOT.phase ~= "moving" or WP.isPoint then return end

    local gm = getGreenMarker()
    if not gm then return end
    local mx,my,mz = getElementPosition(gm)

    -- Маркер НЕ на складе — значит уже появилась новая точка
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z) >= 8 then
        -- Проверяем что это действительно новый маркер (не тот что мы несём)
        local idx = nearestDest(gm)
        local dp = DEST_POINTS[idx]
        if not dp then return end
        -- Запоминаем позицию чтобы не реагировать дважды
        local posKey = string.format("%.0f%.0f",mx,my)
        if lastMarkerPos == posKey then return end
        lastMarkerPos = posKey
        -- Дошли ли мы до склада? Проверяем расстояние
        local px,py,pz = getElementPosition(localPlayer)
        local distToStorage = d3(px,py,pz,STORAGE.x,STORAGE.y,STORAGE.z)
        if distToStorage < 5 then
            -- Уже на складе — сразу идём к новой точке
            storageTimer=0
            BOT.destIdx=idx
            BOT.phase="moving"
            BOT.isReset=true
            WP.isPoint=true
            buildWP(routes[idx].to, gm)
            log(string.format("Склад+маркер > T%d (%d вейпоинтов)",idx,#WP.markers))
        end
        -- Если ещё не на складе — продолжаем идти, at_storage подхватит
    end
end, 200, 0)

setTimer(function()
    if not BOT.enabled then return end
    if BOT.phase~="at_dest" and BOT.phase~="minigame" then
        atDestTick = 0
        return
    end

    -- Если at_dest > 1 сек и включена опция — идём к зелёной метке
    if BOT.phase == "at_dest" and Settings.autoWalkToDest then
        if atDestTick == 0 then atDestTick = getTickCount(); return end
        if getTickCount() - atDestTick > 1000 then
            local gm = getGreenMarker()
            if gm and isElement(gm) then
                local mx,my,mz = getElementPosition(gm)
                local px,py,pz = getElementPosition(localPlayer)
                local d = math.sqrt((mx-px)^2+(my-py)^2)
                if d > 1.5 then
                    -- Идём к зелёной метке шагом
                    clearWP()
                    local m = mkm(mx,my,mz)
                    WP.markers = {m}; WP.finish = m; WP.target = m; WP.idx = 1; WP.isPoint = true
                    BOT.phase = "walk_to_marker"
                    atDestTick = 0
                    dbg("at_dest timeout → walk_to_marker")
                end
            end
        end
    else
        atDestTick = 0
    end

    local gm=getGreenMarker()
    if not gm then return end
    local mx,my,mz=getElementPosition(gm)

    -- Маркер переместился на склад — ящик подобран
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z)<10 then
        local idx=BOT.destIdx
        if idx>0 and routes[idx] and #routes[idx].back>0 then
            STATS.boxes = STATS.boxes + 1
            BOT.phase="moving"
            BOT.destIdx=0
            BOT.isReset=true
            WP.isPoint=false
            buildWP(routes[idx].back, gm)
            log(string.format("Ящик #%d! Иду на склад (%d вейпоинтов)", STATS.boxes, #WP.markers))
        else
            BOT.phase="at_storage"; storageTimer=0
        end
    end
end, 300, 0)

-- Мини-игра
addEvent("tobacco:startMiniGame", true)
addEventHandler("tobacco:startMiniGame", localPlayer, function(difficulty, isNew, token)
    if not BOT.enabled then return end
    BOT.phase="minigame"
    setPedControlState(localPlayer,"forwards",false)
    setPedControlState(localPlayer,"sprint",false)
    dbg("Мини-игра — авто через 3.2 сек")
    setTimer(function()
        if not BOT.enabled or BOT.phase~="minigame" then return end
        local res=getResourceFromName("province_tobacco")
        local rr=res and getResourceRootElement(res)
        if rr and token then triggerServerEvent("tobacco:liftStart",rr,token) end
    end, 3200, 1)
end)

-- Победа через hook
if type(addDebugHook)=="function" then
    addDebugHook("preEvent", function(src,ev,...)
        if not BOT.enabled then return end
        if ev=="tobacco:liftStart" then
            local idx=BOT.destIdx
            if idx==0 or not routes[idx] or #routes[idx].back==0 then
                BOT.phase="at_storage"; storageTimer=0; return
            end
            local gm=getGreenMarker()
            BOT.phase="moving"; BOT.destIdx=0; BOT.isReset=true; WP.isPoint=false
            buildWP(routes[idx].back, gm)
            log(string.format("Победа! Иду на склад (%d вейпоинтов)",#WP.markers))
        end
    end, {"tobacco:liftStart"})
end

addCommandHandler("tabakbot", function()
    BOT.enabled = not BOT.enabled
    if BOT.enabled then
        if not STORAGE.set then log("Сначала /tabakstorage"); BOT.enabled=false; return end
        BOT.phase="at_storage"; BOT.destIdx=0; BOT.isReset=true; storageTimer=0
        STATS.boxes=0; STATS.startTick=getTickCount()
        clearWP()
        log("Включён! Жду маркер... Маршрутов: "..(function()
            local c=0; for i=1,40 do if #routes[i].to>0 then c=c+1 end end; return c
        end)())
    else
        setPedControlState(localPlayer,"forwards",false)
        setPedControlState(localPlayer,"sprint",false)
        BOT.phase="idle"; clearWP()
        log("Выключён")
    end
end)

addCommandHandler("tabakgo", function(cmd,n)
    n=tonumber(n)
    if not n or not routes[n] or #routes[n].to==0 then log("Нет маршрута "..tostring(n)); return end
    local gm=getGreenMarker()
    BOT.enabled=true; BOT.phase="moving"; BOT.destIdx=n; BOT.isReset=true; WP.isPoint=true
    buildWP(routes[n].to, gm)
    log(string.format("Иду к T%d (%d вейпоинтов)",n,#WP.markers))
end)

-- =============================================
-- МОЩНЫЙ ДЕБАГ
-- =============================================

-- HUD дебага — всегда виден
addEventHandler("onClientRender", root, function()
    if not DBG_ENABLED then return end

    local px,py,pz = getElementPosition(localPlayer)
    local x, y = 10, 10
    local lh = 16  -- высота строки

    -- Фон
    dxDrawRectangle(x, y, 420, 14 + lh*12, tocolor(0,0,0,170))

    local function line(i, text, col)
        col = col or tocolor(255,255,255,220)
        dxDrawText(text, x+6, y+4+i*lh, x+414, y+4+(i+1)*lh, col, 0.85, "default")
    end

    -- Строка 0: фаза и позиция
    line(0, string.format("Phase: %s | Pos: %.1f, %.1f, %.1f", BOT.phase, px,py,pz),
        BOT.enabled and tocolor(0,255,100,255) or tocolor(150,150,150,255))

    -- Строка 1: вейпоинты
    local wpInfo = string.format("WP: %d/%d", WP.idx, #WP.markers)
    if WP.target and isElement(WP.target) then
        local tx,ty = getElementPosition(WP.target)
        local d = math.sqrt((tx-px)^2+(ty-py)^2)
        wpInfo = wpInfo..string.format(" | Next: %.1f,%.1f dist=%.1f", tx,ty,d)
    end
    line(1, wpInfo, tocolor(100,200,255,255))

    -- Строка 2: destIdx и маршрут
    local routeInfo = string.format("DestIdx: %d | to: %d | back: %d | isPoint: %s",
        BOT.destIdx,
        BOT.destIdx > 0 and #routes[BOT.destIdx].to or 0,
        BOT.destIdx > 0 and #routes[BOT.destIdx].back or 0,
        tostring(WP.isPoint))
    line(2, routeInfo, tocolor(255,220,0,255))

    -- Строка 3: ближайший маркер
    local bestM, bestD = nil, math.huge
    for _, m in ipairs(getElementsByType("marker", root, true)) do
        local mx,my,mz = getElementPosition(m)
        local d = math.sqrt((mx-px)^2+(my-py)^2+(mz-pz)^2)
        if d < bestD then bestD=d; bestM=m end
    end
    if bestM then
        local r,g,b,a = getMarkerColor(bestM)
        line(3, string.format("Nearest marker: dist=%.1f rgb=(%d,%d,%d,%d)", bestD,r,g,b,a),
            tocolor(255,100,100,255))
    else
        line(3, "No markers nearby", tocolor(150,150,150,255))
    end

    -- Строка 4: склад
    line(4, string.format("Storage: %.1f,%.1f set=%s | DestPts: %d",
        STORAGE.x, STORAGE.y, tostring(STORAGE.set), #DEST_POINTS),
        tocolor(200,200,200,255))

    -- Строки 5-12: последние логи
    for i, txt in ipairs(dbgLog) do
        line(4+i, txt, tocolor(180,180,180,220))
    end
end)

-- Расширенный tabakdebug
addCommandHandler("tabakdebug", function()
    local px,py,pz = getElementPosition(localPlayer)
    dbg(string.format("=== DEBUG phase=%s dest=%d ===", BOT.phase, BOT.destIdx))
    dbg(string.format("Pos: %.2f,%.2f,%.2f", px,py,pz))
    dbg(string.format("WP: %d/%d | isPoint=%s", WP.idx, #WP.markers, tostring(WP.isPoint)))

    if BOT.destIdx > 0 then
        local r = routes[BOT.destIdx]
        dbg(string.format("Route[%d]: to=%d back=%d", BOT.destIdx, #r.to, #r.back))
    end

    -- Зелёный маркер
    local gm = getGreenMarker()
    if gm then
        local mx,my,mz = getElementPosition(gm)
        dbg(string.format("GreenMarker: %.1f,%.1f,%.1f", mx,my,mz))
    else
        dbg("GreenMarker: не найден")
    end

    -- Все маркеры в радиусе 30м
    local count = 0
    for _, m in ipairs(getElementsByType("marker", root, true)) do
        local mx,my,mz = getElementPosition(m)
        local d = math.sqrt((mx-px)^2+(my-py)^2+(mz-pz)^2)
        if d < 30 then
            local r,g,b,a = getMarkerColor(m)
            dbg(string.format("Marker dist=%.1f rgb=(%d,%d,%d,%d)", d,r,g,b,a))
            count = count + 1
        end
    end
    dbg(string.format("Markers in 30m: %d | Routes: %d/40", count,
        (function() local c=0; for i=1,40 do if #routes[i].to>0 then c=c+1 end end; return c end)()))
end)

addCommandHandler("tabakdbg", function()
    DBG_ENABLED = not DBG_ENABLED
    outputChatBox("[TABAK] HUD дебаг: "..(DBG_ENABLED and "ВКЛ" or "ВЫКЛ"), 0,200,255,false)
end)

-- =============================================
-- GUI ПРОСМОТРА МАРШРУТОВ
-- =============================================

local GUI = {win=nil, visible=false}
local guiMarkers = {}

local function clearGuiMarkers()
    for _,m in pairs(guiMarkers) do
        if isElement(m) then destroyElement(m) end
    end
    guiMarkers = {}
end

local function showAllDestMarkers()
    clearGuiMarkers()
    if STORAGE.set then
        table.insert(guiMarkers, makeMarker(STORAGE.x,STORAGE.y,STORAGE.z, 0,255,100, 1.0))
    end
    for _,p in ipairs(DEST_POINTS) do
        table.insert(guiMarkers, makeMarker(p.x,p.y,p.z, 255,100,0, 0.8))
    end
end

-- Алиас для GUI (mkm объявлен в блоке движения)
local function makeMarker(x,y,z,r,g,b,size)
    local m=createMarker(x,y,z-1,"cylinder",size or 0.8,r,g,b,180)
    setElementInterior(m,getElementInterior(localPlayer))
    setElementDimension(m,getElementDimension(localPlayer))
    return m
end

local function openTabakRoutes()
    if GUI.win and isElement(GUI.win) then
        GUI.visible = not GUI.visible
        guiSetVisible(GUI.win, GUI.visible)
        showCursor(GUI.visible)
        if not GUI.visible then clearGuiMarkers() end
    end
end

local function showRouteMarkers(destIdx)
    clearGuiMarkers()
    -- Склад
    if STORAGE.set then
        table.insert(guiMarkers, makeMarker(STORAGE.x,STORAGE.y,STORAGE.z, 0,255,100, 1.0))
    end
    -- Все точки назначения
    for i,p in ipairs(DEST_POINTS) do
        local col = (i==destIdx) and {255,255,0} or {255,100,0}
        table.insert(guiMarkers, makeMarker(p.x,p.y,p.z, col[1],col[2],col[3], 0.8))
    end
    -- Маршрут выбранной точки
    if destIdx > 0 and routes[destIdx] then
        for _,p in ipairs(routes[destIdx].to) do
            table.insert(guiMarkers, makeMarker(p[1],p[2],p[3], 0,150,255, 0.5))
        end
        for _,p in ipairs(routes[destIdx].back) do
            table.insert(guiMarkers, makeMarker(p[1],p[2],p[3], 150,0,255, 0.5))
        end
    end
end

addCommandHandler("tabakroutes", function()
    if GUI.win and isElement(GUI.win) then
        GUI.visible = not GUI.visible
        guiSetVisible(GUI.win, GUI.visible)
        showCursor(GUI.visible)
        if not GUI.visible then clearGuiMarkers() end
        return
    end

    -- Большое окно: левая панель + правая панель
    local W, H = 640, 560
    GUI.win = guiCreateWindow((sw-W)/2, (sh-H)/2, W, H, "TabakBot — Редактор маршрутов", false)

    -- ===== ЛЕВАЯ ПАНЕЛЬ (0..300): список точек + склад =====

    guiCreateLabel(10, 28, 280, 16, "Точки назначения:", false, GUI.win)
    local grid = guiCreateGridList(10, 46, 280, 240, false, GUI.win)
    guiGridListAddColumn(grid, "#",        0.10)
    guiGridListAddColumn(grid, "Название", 0.52)
    guiGridListAddColumn(grid, "К точке",  0.19)
    guiGridListAddColumn(grid, "Обратно",  0.19)

    local btnGoto     = guiCreateButton(10,  292, 88, 26, "Телепорт", false, GUI.win)
    local btnShowPath = guiCreateButton(103, 292, 88, 26, "Показать", false, GUI.win)
    local btnDelPoint = guiCreateButton(196, 292, 94, 26, "Удалить", false, GUI.win)

    -- Добавить точку
    guiCreateLabel(10, 326, 280, 14, "Добавить точку (встань на неё):", false, GUI.win)
    local editName    = guiCreateEdit(10, 342, 180, 24, "Название", false, GUI.win)
    local btnAddPoint = guiCreateButton(194, 342, 96, 24, "+ Добавить", false, GUI.win)

    -- Склад
    guiCreateLabel(10, 374, 280, 14, "Склад (место где берёшь ящик):", false, GUI.win)
    local lblStorage  = guiCreateLabel(10, 390, 280, 16,
        STORAGE.set and string.format("%.1f, %.1f, %.1f", STORAGE.x, STORAGE.y, STORAGE.z)
        or "НЕ ЗАДАН", false, GUI.win)
    local btnSetStore = guiCreateButton(10, 408, 280, 26, "Записать мою позицию как склад", false, GUI.win)

    -- ===== ПРАВАЯ ПАНЕЛЬ (310..630): запись + экспорт =====

    guiCreateLabel(310, 28, 320, 16, "Запись маршрута:", false, GUI.win)

    -- Шаг 1
    guiCreateLabel(310, 50, 320, 14, "1. Выбери точку:", false, GUI.win)
    local ddRecDest = guiCreateComboBox(310, 66, 320, 24, false, GUI.win)
    guiComboBoxAddItem(ddRecDest, "-- выбери точку --")

    -- Шаг 2
    guiCreateLabel(310, 96, 320, 14, "2. Встань на СКЛАД, нажми кнопку, иди к точке:", false, GUI.win)
    local btnRecTo = guiCreateButton(310, 112, 155, 28, "Запись: СКЛАД > ТОЧКА", false, GUI.win)
    local btnRecBack = guiCreateButton(470, 112, 160, 28, "Запись: ТОЧКА > СКЛАД", false, GUI.win)

    -- Шаг 3
    guiCreateLabel(310, 148, 320, 14, "3. Дошёл — нажми стоп:", false, GUI.win)
    local btnRecStop   = guiCreateButton(310, 164, 155, 28, "СТОП — сохранить", false, GUI.win)
    local btnRecCancel = guiCreateButton(470, 164, 160, 28, "Отмена", false, GUI.win)

    -- Статус записи
    local lblRecStatus = guiCreateLabel(310, 200, 320, 20, "Запись не активна", false, GUI.win)

    -- Разделитель
    guiCreateLabel(310, 226, 320, 14, "--------------------------------", false, GUI.win)

    -- Экспорт
    guiCreateLabel(310, 242, 320, 14, "Экспорт (Ctrl+A > Ctrl+C для копирования):", false, GUI.win)
    local memo = guiCreateMemo(310, 258, 320, 210, "", false, GUI.win)
    local btnExportOne = guiCreateButton(310, 474, 155, 26, "Экспорт выбранной", false, GUI.win)
    local btnExportAll = guiCreateButton(470, 474, 160, 26, "Экспорт всех", false, GUI.win)

    -- Закрыть
    local btnClose = guiCreateButton(310, 506, 320, 28, "Закрыть", false, GUI.win)

    -- ===== ФУНКЦИИ =====

    local function refreshGrid()
        guiGridListClear(grid)
        while guiComboBoxGetItemCount(ddRecDest) > 0 do
            guiComboBoxRemoveItem(ddRecDest, 0)
        end
        guiComboBoxAddItem(ddRecDest, "-- выбери точку --")

        for i, p in ipairs(DEST_POINTS) do
            local r = routes[i]
            local row = guiGridListAddRow(grid)
            guiGridListSetItemText(grid, row, 1, tostring(i), false, false)
            guiGridListSetItemText(grid, row, 2, p.name, false, false)
            guiGridListSetItemText(grid, row, 3, tostring(r and #r.to or 0), false, false)
            guiGridListSetItemText(grid, row, 4, tostring(r and #r.back or 0), false, false)
            guiComboBoxAddItem(ddRecDest, "#"..i.." "..p.name)
        end

        guiSetText(lblStorage,
            STORAGE.set and string.format("%.1f, %.1f, %.1f", STORAGE.x, STORAGE.y, STORAGE.z)
            or "НЕ ЗАДАН!")

        if REC.active then
            guiSetText(lblRecStatus, string.format(
                "? ЗАПИСЬ: точка #%d [%s] — %d точек",
                REC.destIdx, REC.dir == "to" and "к точке" or "обратно", #REC.points))
        else
            guiSetText(lblRecStatus, "Запись не активна")
        end
    end

    local function getExportText(idx)
        local lines = {}
        if idx then
            local p = DEST_POINTS[idx]
            local r = routes[idx]
            if not p then return "(нет данных)" end
            table.insert(lines, string.format("-- #%d '%s'  %.2f,%.2f,%.2f", idx,p.name,p.x,p.y,p.z))
            table.insert(lines, "routes["..idx.."]={")
            table.insert(lines, "  to={")
            for _,pt in ipairs(r and r.to or {}) do
                table.insert(lines, string.format("    {%.3f,%.3f,%.3f},",pt[1],pt[2],pt[3]))
            end
            table.insert(lines, "  },")
            table.insert(lines, "  back={")
            for _,pt in ipairs(r and r.back or {}) do
                table.insert(lines, string.format("    {%.3f,%.3f,%.3f},",pt[1],pt[2],pt[3]))
            end
            table.insert(lines, "  },")
            table.insert(lines, "}")
        else
            if STORAGE.set then
                table.insert(lines, string.format(
                    "STORAGE={x=%.3f,y=%.3f,z=%.3f,set=true}",STORAGE.x,STORAGE.y,STORAGE.z))
                table.insert(lines, "")
            end
            table.insert(lines, "DEST_POINTS={")
            for i,p in ipairs(DEST_POINTS) do
                table.insert(lines, string.format(
                    "  [%d]={x=%.3f,y=%.3f,z=%.3f,name=%q},",i,p.x,p.y,p.z,p.name))
            end
            table.insert(lines, "}")
            table.insert(lines, "routes={")
            for i,r in pairs(routes) do
                table.insert(lines, "  ["..i.."]={")
                table.insert(lines, "    to={")
                for _,pt in ipairs(r.to or {}) do
                    table.insert(lines, string.format("      {%.3f,%.3f,%.3f},",pt[1],pt[2],pt[3]))
                end
                table.insert(lines, "    },")
                table.insert(lines, "    back={")
                for _,pt in ipairs(r.back or {}) do
                    table.insert(lines, string.format("      {%.3f,%.3f,%.3f},",pt[1],pt[2],pt[3]))
                end
                table.insert(lines, "    },")
                table.insert(lines, "  },")
            end
            table.insert(lines, "}")
        end
        return table.concat(lines, "\n")
    end

    -- ===== ОБРАБОТЧИКИ =====

    addEventHandler("onClientGUIClick", btnSetStore, function()
        local x,y,z = getElementPosition(localPlayer)
        STORAGE.x,STORAGE.y,STORAGE.z,STORAGE.set = x,y,z,true
        refreshGrid()
        log("Склад записан!")
    end, false)

    addEventHandler("onClientGUIClick", btnAddPoint, function()
        local name = guiGetText(editName)
        if name == "" or name == "Название" then name = "Точка #"..(#DEST_POINTS+1) end
        local x,y,z = getElementPosition(localPlayer)
        table.insert(DEST_POINTS, {x=x,y=y,z=z,name=name})
        routes[#DEST_POINTS] = {to={},back={}}
        guiSetText(editName, "Название")
        refreshGrid()
        showAllDestMarkers()
        log("Точка #"..#DEST_POINTS.." '"..name.."' добавлена")
    end, false)

    addEventHandler("onClientGUIClick", btnRecTo, function()
        local si = guiComboBoxGetSelected(ddRecDest)
        if si <= 0 then log("Выбери точку!"); return end
        startRecord(si, "to")
        refreshGrid()
    end, false)

    addEventHandler("onClientGUIClick", btnRecBack, function()
        local si = guiComboBoxGetSelected(ddRecDest)
        if si <= 0 then log("Выбери точку!"); return end
        startRecord(si, "back")
        refreshGrid()
    end, false)

    addEventHandler("onClientGUIClick", btnRecStop, function()
        stopRecord(true); refreshGrid()
    end, false)

    addEventHandler("onClientGUIClick", btnRecCancel, function()
        stopRecord(false); refreshGrid()
    end, false)

    addEventHandler("onClientGUIClick", btnDelPoint, function()
        local row = guiGridListGetSelectedItem(grid)
        if row and row >= 0 then
            table.remove(DEST_POINTS, row+1)
            table.remove(routes, row+1)
            refreshGrid(); clearGuiMarkers()
        end
    end, false)

    addEventHandler("onClientGUIClick", btnGoto, function()
        local row = guiGridListGetSelectedItem(grid)
        if row and row >= 0 then
            local p = DEST_POINTS[row+1]
            if p then setElementPosition(localPlayer,p.x,p.y,p.z+0.5) end
        end
    end, false)

    addEventHandler("onClientGUIClick", btnShowPath, function()
        local row = guiGridListGetSelectedItem(grid)
        showRouteMarkers(row and row >= 0 and row+1 or 0)
    end, false)

    addEventHandler("onClientGUIClick", btnExportOne, function()
        local row = guiGridListGetSelectedItem(grid)
        guiSetText(memo, getExportText(row and row >= 0 and row+1 or nil))
    end, false)

    addEventHandler("onClientGUIClick", btnExportAll, function()
        guiSetText(memo, getExportText(nil))
    end, false)

    addEventHandler("onClientGUIClick", btnClose, function()
        guiSetVisible(GUI.win, false)
        showCursor(false)
        GUI.visible = false
        clearGuiMarkers()
    end, false)

    -- Автообновление статуса записи
    setTimer(function()
        if not GUI.visible or not GUI.win or not isElement(GUI.win) then return end
        if REC.active then
            guiSetText(lblRecStatus, string.format(
                "? ЗАПИСЬ: точка #%d [%s] — %d точек",
                REC.destIdx, REC.dir == "to" and "к точке" or "обратно", #REC.points))
        end
    end, 500, 0)

    refreshGrid()
    showAllDestMarkers()
    guiSetVisible(GUI.win, true)
    showCursor(true)
    GUI.visible = true
end)

-- =============================================
-- HUD
-- =============================================

addEventHandler("onClientRender", root, function()
    -- Панель статистики (справа)
    if BOT.enabled and Settings.showStats then
        local panW, panH = 220, 70
        local px = sw - panW - 10
        local py = 10
        dxDrawRectangle(px, py, panW, panH, tocolor(0,0,0,160))
        dxDrawRectangle(px, py, panW, 22, tocolor(0,150,80,200))
        dxDrawText("TabakBot", px+8, py+3, px+panW-8, py+19,
            tocolor(255,255,255,255), 1.0, "default-bold")
        dxDrawText(string.format("Ящиков: %d", STATS.boxes),
            px+8, py+26, px+panW-8, py+42,
            tocolor(0,255,150,255), 1.0, "default-bold")
        dxDrawText(string.format("Время: %s | %s", getSessionTime(), BOT.phase),
            px+8, py+46, px+panW-8, py+62,
            tocolor(200,200,200,220), 0.85, "default")
    end

    -- Индикатор записи
    if REC.active then        dxDrawRectangle(sw/2-100, 10, 200, 28, tocolor(200,0,0,180))
        dxDrawText(string.format("? ЗАПИСЬ #%d [%s] %d точек",
            REC.destIdx, REC.dir, #REC.points),
            sw/2-98, 14, sw/2+98, 36, tocolor(255,255,255,255), 0.9, "default-bold", "center")
    end

    if not BOT.enabled then return end
    local py = sh-46
    dxDrawRectangle(10,py,300,36,tocolor(0,0,0,150))
    dxDrawText(string.format("TABAKBOT | %s | #%d wp%d/%d",
        BOT.phase, BOT.destIdx, WP.idx, #WP.markers),
        18,py+8,305,py+30,tocolor(0,255,100,220),0.85,"default-bold")
end)

log("Загружен!")
log("/tabakbot — запустить")
log("/tabakoptions — настройки")
log("TG @aleksey_nester")


-- =============================================
-- АВТОСКАНИРОВАНИЕ ТОЧЕК НАЗНАЧЕНИЯ
-- /tabakscan — сканирует все маркеры рядом и добавляет как точки
-- Используй когда стоишь в зале с маркерами
-- =============================================

addCommandHandler("tabakscan", function()
    local px,py,pz = getElementPosition(localPlayer)
    local found = 0
    local skipped = 0

    for _, elem in ipairs(getElementsByType("marker", root, true)) do
        local mx,my,mz = getElementPosition(elem)
        local d = getDistanceBetweenPoints3D(px,py,pz, mx,my,mz)
        if d < 80 then
            -- Проверяем что такой точки ещё нет
            local exists = false
            for _, dp in ipairs(DEST_POINTS) do
                if math.abs(dp.x-mx)<1.0 and math.abs(dp.y-my)<1.0 then
                    exists = true; break
                end
            end
            if not exists then
                found = found + 1
                local name = string.format("T%d", #DEST_POINTS+1)
                table.insert(DEST_POINTS, {x=mx, y=my, z=mz, name=name})
                routes[#DEST_POINTS] = {to={}, back={}}
                log(string.format("  #%d: %.2f, %.2f, %.2f (%.1fм)", #DEST_POINTS, mx,my,mz, d))
            else
                skipped = skipped + 1
            end
        end
    end

    log(string.format("Найдено: %d новых, %d уже есть. Всего точек: %d", found, skipped, #DEST_POINTS))
    if found > 0 then
        log("Теперь /tabakroutes — посмотри список и запиши маршруты")
    end
end)

-- /tabakpoints — вывести все точки с координатами
addCommandHandler("tabakpoints", function()
    if #DEST_POINTS == 0 then
        log("Нет точек. Используй /tabakscan рядом с маркерами")
        return
    end
    log("Все точки назначения ("..#DEST_POINTS.."):")
    for i,p in ipairs(DEST_POINTS) do
        local r = routes[i]
        log(string.format("  #%d '%s': %.2f,%.2f,%.2f | >%d <%d",
            i, p.name, p.x,p.y,p.z,
            r and #r.to or 0, r and #r.back or 0))
    end
end)

-- =============================================
-- TABAK OPTIONS — окно настроек /tabakoptions
-- =============================================

local OPT = {window=nil, visible=false}

-- Дополнительные поля настроек (добавляем к существующей таблице Settings)
Settings.CaptureRadius  = Settings.CaptureRadius  or 1      -- радиус захвата вейпоинта
Settings.SlowRadius     = Settings.SlowRadius     or 1.5    -- радиус захвата последних 2 маркеров
Settings.WaitDestDelay  = Settings.WaitDestDelay  or 1500   -- задержка ожидания маркера сервера (мс)

local bunnyTimer = nil

-- Применяем настройки к движению
local function applySettings()
    -- Обновляем lerpAngle speed
    -- (используется в onClientPreRender через Settings.RotationSpeed)
end

addCommandHandler("tabakoptions", function()
    if OPT.window and isElement(OPT.window) then
        OPT.visible = not OPT.visible
        guiSetVisible(OPT.window, OPT.visible)
        showCursor(OPT.visible)
        if OPT.visible then guiBringToFront(OPT.window) end
        return
    end

    local W, H = 500, 500
    OPT.window = guiCreateWindow((sw-W)/2,(sh-H)/2, W, H,
        "TabakBot — Настройки (/tabakoptions)", false)

    -- СОСТОЯНИЕ (y=25)
    guiCreateLabel(10,25,200,16,"СОСТОЯНИЕ БОТА",false,OPT.window)
    local lblStatus=guiCreateLabel(10,43,480,16,"Бот: "..(BOT.enabled and "ВКЛЮЧЁН | "..BOT.phase or "выключен"),false,OPT.window)
    local btnToggle=guiCreateButton(10,62,150,24,BOT.enabled and "Выключить бота" or "Включить бота",false,OPT.window)
    local btnQuit=guiCreateButton(168,62,160,24,"Уволиться с работы",false,OPT.window)

    -- ДВИЖЕНИЕ (y=100)
    guiCreateLabel(10,96,200,16,"ДВИЖЕНИЕ",false,OPT.window)
    guiCreateLabel(10,114,160,16,"Скорость поворота:",false,OPT.window)
    local sldRot=guiCreateScrollBar(175,115,200,16,true,false,OPT.window)
    guiScrollBarSetScrollPosition(sldRot,Settings.RotationSpeed*5)
    local lblRotVal=guiCreateLabel(385,114,60,16,tostring(Settings.RotationSpeed),false,OPT.window)
    local chkBhop=guiCreateCheckBox(10,134,220,18,"BunnyHop (прыжки при беге)",Settings.AllowBunnyHop,false,OPT.window)
    guiCreateLabel(10,156,130,16,"Задержка (мс):",false,OPT.window)
    local editBhopDelay=guiCreateEdit(145,154,70,20,tostring(Settings.BunnyHopDelay),false,OPT.window)
    guiCreateLabel(230,156,120,16,"Мин. дистанция:",false,OPT.window)
    local editBhopDist=guiCreateEdit(355,154,60,20,tostring(Settings.BunnyHopMinDist),false,OPT.window)

    -- ПОВЕДЕНИЕ (y=190)
    guiCreateLabel(10,186,200,16,"ПОВЕДЕНИЕ",false,OPT.window)
    local chkSprintBack=guiCreateCheckBox(10,204,350,18,"Бежать на склад (по умолч. — шагом)",Settings.sprintToStorage,false,OPT.window)
    local chkSprintBox=guiCreateCheckBox(10,224,350,18,"Бежать с ящиком в руках",Settings.sprintWithBox,false,OPT.window)
    local chkAutoWalk=guiCreateCheckBox(10,244,420,18,"Идти к зелёной метке если стоим > 1 сек",Settings.autoWalkToDest,false,OPT.window)

    -- БЕЗОПАСНОСТЬ (y=278)
    guiCreateLabel(10,270,200,16,"БЕЗОПАСНОСТЬ",false,OPT.window)
    local chkAutoDisable=guiCreateCheckBox(10,288,380,18,"Выключить бота при обнаружении админа",Settings.AutoDisable,false,OPT.window)

    -- ОТОБРАЖЕНИЕ И ОТЛАДКА (y=322)
    guiCreateLabel(10,314,200,16,"ОТОБРАЖЕНИЕ И ОТЛАДКА",false,OPT.window)
    local chkStats=guiCreateCheckBox(10,332,300,18,"Показывать панель статистики",Settings.showStats,false,OPT.window)
    local chkDbg=guiCreateCheckBox(10,352,200,18,"HUD дебага",DBG_ENABLED,false,OPT.window)
    local btnFindMarker=guiCreateButton(220,350,160,22,"Найти зелёный маркер",false,OPT.window)

    -- КНОПКИ (y=390)
    local btnApply=guiCreateButton(10,390,120,26,"Применить",false,OPT.window)
    local btnClose=guiCreateButton(370,390,120,26,"Закрыть",false,OPT.window)

    -- Автообновление
    setTimer(function()
        if not OPT.visible or not OPT.window or not isElement(OPT.window) then return end
        guiSetText(lblStatus,"Бот: "..(BOT.enabled and ("ВКЛЮЧЁН | "..BOT.phase) or "выключен"))
        guiSetText(btnToggle,BOT.enabled and "Выключить бота" or "Включить бота")
    end,500,0)

    -- Обработчики
    addEventHandler("onClientGUIClick",btnToggle,function()
        if BOT.enabled then
            BOT.enabled=false; setPedControlState(localPlayer,"forwards",false)
            setPedControlState(localPlayer,"sprint",false); BOT.phase="idle"; clearWP(); log("Выключён")
        else
            BOT.enabled=true; BOT.phase="at_storage"; BOT.destIdx=0; BOT.isReset=true
            storageTimer=0; STATS.boxes=0; STATS.startTick=getTickCount(); clearWP(); log("Включён!")
        end
    end,false)

    addEventHandler("onClientGUIClick",btnQuit,function()
        local res=getResourceFromName("province_tobacco")
        local rr=res and getResourceRootElement(res)
        if rr then triggerServerEvent("tobacco:onJobCanceled",rr) end
        BOT.enabled=false; BOT.phase="idle"; clearWP(); log("Уволился")
    end,false)

    addEventHandler("onClientGUIScroll",sldRot,function()
        local v=math.max(1,math.min(20,math.floor(guiScrollBarGetScrollPosition(sldRot)/5)))
        Settings.RotationSpeed=v; guiSetText(lblRotVal,tostring(v))
    end,false)

    addEventHandler("onClientGUIClick",chkBhop,function() Settings.AllowBunnyHop=guiCheckBoxGetSelected(chkBhop) end,false)
    addEventHandler("onClientGUIClick",chkSprintBack,function() Settings.sprintToStorage=guiCheckBoxGetSelected(chkSprintBack) end,false)
    addEventHandler("onClientGUIClick",chkSprintBox,function() Settings.sprintWithBox=guiCheckBoxGetSelected(chkSprintBox) end,false)
    addEventHandler("onClientGUIClick",chkAutoWalk,function() Settings.autoWalkToDest=guiCheckBoxGetSelected(chkAutoWalk) end,false)
    addEventHandler("onClientGUIClick",chkAutoDisable,function() Settings.AutoDisable=guiCheckBoxGetSelected(chkAutoDisable) end,false)
    addEventHandler("onClientGUIClick",chkStats,function() Settings.showStats=guiCheckBoxGetSelected(chkStats) end,false)
    addEventHandler("onClientGUIClick",chkDbg,function() DBG_ENABLED=guiCheckBoxGetSelected(chkDbg) end,false)

    addEventHandler("onClientGUIClick",btnFindMarker,function()
        local m=getGreenMarker()
        if m then local mx,my=getElementPosition(m); log(string.format("Маркер: %.1f,%.1f → T%d",mx,my,nearestDest(m)))
        else log("Зелёный маркер не найден") end
    end,false)

    addEventHandler("onClientGUIClick",btnApply,function()
        local bd=tonumber(guiGetText(editBhopDelay)); local bm=tonumber(guiGetText(editBhopDist))
        if bd then Settings.BunnyHopDelay=bd end
        if bm then Settings.BunnyHopMinDist=bm end
        log("Применено")
    end,false)

    addEventHandler("onClientGUIClick",btnClose,function()
        guiSetVisible(OPT.window,false); showCursor(false); OPT.visible=false
    end,false)

    guiSetVisible(OPT.window,true); showCursor(true); OPT.visible=true

    log("Настройки: /tabakoptions")
end)
