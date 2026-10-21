local ui = ui or {}
local common = common or {}

local tab_val = { enabled = false }
local rotation_speed_val = { val = 6 }
local sprintToStorage_val = { val = false }
local siren_switch_val = { val = false }
local autodisable_switch_val = { val = false }
local autoanswer_switch_val = { val = false }
local autoanswer_delay_slider_val = { val = 50 }

local function makeControl(tbl, field)
    return {
        get = function() return tbl[field] end,
        set = function(_, v) tbl[field] = v end
    }
end

local tab = {
    get = function() return tab_val.enabled end,
    slider = function(_, name, min, max, def)
        if name == "Rotation speed" then return makeControl(rotation_speed_val, "val")
        else return makeControl(autoanswer_delay_slider_val, "val") end
    end,
    switch = function(_, name, def)
        if name == "Sprint to storage" then return makeControl(sprintToStorage_val, "val")
        elseif name == "Sirena" then return makeControl(siren_switch_val, "val")
        elseif name == "Auto-disable" then return makeControl(autodisable_switch_val, "val")
        elseif name == "Auto-answer AI" then return makeControl(autoanswer_switch_val, "val")
        else return makeControl({val = def}, "val") end
    end
}

ui.sidebar = function() return {} end
ui.localize = function() end
ui.get_alpha = function() return 0 end
ui.create = function() return tab end

local rotation_speed = tab:slider("Rotation speed", 5, 10, 6, "%d")
local sprintToStorage = tab:switch("Sprint to storage", false)
local siren_switch = tab:switch("Sirena", false)
local autodisable_switch = tab:switch("Auto-disable", false)
local autoanswer_switch = tab:switch("Auto-answer AI", false)
local autoanswer_delay_slider = tab:slider("Answer delay", 50, 200, 50, "%dms")

if not common.emulate_key then
    common.emulate_key = function(vk, state)
        if vk == 0x57 then
            if setPedControlState then setPedControlState(localPlayer, "forwards", state) end
            if dfEmulateKey then dfEmulateKey("w", state) end
        elseif vk == 0x20 or vk == 0x10 or vk == 0xA0 then
            if setPedControlState then setPedControlState(localPlayer, "sprint", state) end
            if dfEmulateKey then dfEmulateKey("space", state) end
        end
    end
end

if not common.send_message then
    common.send_message = function(text)
        if outputChatBox then outputChatBox("[Ответ ИИ] " .. tostring(text), 220, 220, 220) end
        if executeCommandHandler then executeCommandHandler("say", tostring(text)) end
    end
end

local STORAGE = {x=2173.784, y=-1505.407, z=1397.539}

local DEST_POINTS = {
    [1]  = {x=2161.478, y=-1531.323, z=1397.539},
    [2]  = {x=2161.842, y=-1509.603, z=1397.539},
    [3]  = {x=2171.658, y=-1490.037, z=1397.539},
    [4]  = {x=2161.824, y=-1521.908, z=1397.539},
    [5]  = {x=2175.782, y=-1486.041, z=1397.539},
    [6]  = {x=2176.459, y=-1493.133, z=1397.539},
    [7]  = {x=2171.537, y=-1493.259, z=1397.539},
    [8]  = {x=2171.919, y=-1482.086, z=1397.539},
    [9]  = {x=2176.459, y=-1494.539, z=1397.539},
    [10] = {x=2168.868, y=-1530.622, z=1397.539},
    [11] = {x=2161.677, y=-1517.109, z=1397.539},
    [12] = {x=2162.508, y=-1513.214, z=1397.539},
    [13] = {x=2165.390, y=-1537.748, z=1397.539},
    [14] = {x=2176.062, y=-1489.817, z=1397.539},
    [15] = {x=2172.179, y=-1486.283, z=1397.539},
    [16] = {x=2165.116, y=-1550.222, z=1397.539},
    [17] = {x=2163.069, y=-1523.925, z=1397.539},
    [18] = {x=2172.181, y=-1549.150, z=1397.539},
    [19] = {x=2172.477, y=-1558.521, z=1397.539},
    [20] = {x=2162.043, y=-1525.976, z=1397.539},
    [21] = {x=2176.077, y=-1482.021, z=1397.539},
    [22] = {x=2166.646, y=-1552.553, z=1397.539},
    [23] = {x=2171.618, y=-1545.852, z=1397.539},
    [24] = {x=2172.756, y=-1481.275, z=1397.539},
    [25] = {x=2173.736, y=-1490.134, z=1397.539},
    [26] = {x=2175.780, y=-1484.102, z=1397.539},
    [27] = {x=2173.127, y=-1491.804, z=1397.539},
    [28] = {x=2162.518, y=-1520.793, z=1397.539},
    [29] = {x=2173.010, y=-1543.625, z=1397.539},
    [30] = {x=2175.232, y=-1491.794, z=1397.539},
    [31] = {x=2162.507, y=-1511.298, z=1397.539},
    [32] = {x=2175.783, y=-1487.707, z=1397.539},
    [33] = {x=2166.103, y=-1548.334, z=1397.539},
    [34] = {x=2175.062, y=-1490.968, z=1397.539},
    [35] = {x=2166.109, y=-1540.959, z=1397.539},
    [36] = {x=2166.340, y=-1543.446, z=1397.539},
    [37] = {x=2169.662, y=-1515.516, z=1397.539},
    [38] = {x=2172.541, y=-1491.030, z=1397.539},
    [39] = {x=2163.267, y=-1528.810, z=1397.539},
    [40] = {x=2169.371, y=-1514.641, z=1397.539},
}

local routes = {}
for i=1,40 do routes[i]={to={},back={}} end

routes[1].to = {{2173.5,-1505.2,1397.539},{2170.6,-1505.4,1397.539},{2170.1,-1506.2,1397.539},{2169.7,-1508.1,1397.539},{2169.1,-1509.7,1397.539},{2167.5,-1509.6,1397.539},{2165.3,-1509.7,1397.539},{2163.6,-1509.7,1397.539},{2163.1,-1510.6,1397.539},{2163.1,-1512.6,1397.539},{2163.1,-1515.7,1397.539},{2162.9,-1519.0,1397.539},{2163.3,-1522.8,1397.539},{2163.4,-1527.5,1397.539},{2163.4,-1531.4,1397.539},{2161.6,-1531.5,1397.539}}
routes[2].to = {{2172.2,-1505.4,1397.539},{2170.5,-1505.5,1397.539},{2169.8,-1506.1,1397.539},{2169.8,-1507.4,1397.539},{2169.4,-1509.0,1397.539},{2168.5,-1509.4,1397.539},{2166.5,-1509.5,1397.539},{2163.5,-1509.4,1397.539},{2161.8,-1509.5,1397.539}}
routes[3].to = {{2172.2,-1505.4,1397.539},{2170.7,-1505.4,1397.539},{2169.8,-1505.1,1397.539},{2169.7,-1504.4,1397.539},{2169.7,-1503.3,1397.539},{2170.8,-1501.8,1397.539},{2171.3,-1500.7,1397.539},{2171.5,-1497.7,1397.539},{2171.1,-1495.0,1397.539},{2171.1,-1492.8,1397.539},{2171.6,-1489.9,1397.539}}
routes[4].to = {{2172.4,-1505.3,1397.539},{2171.4,-1505.3,1397.539},{2170.2,-1505.5,1397.539},{2170.0,-1506.6,1397.539},{2169.8,-1508.1,1397.539},{2169.3,-1509.9,1397.539},{2167.4,-1510.1,1397.539},{2165.5,-1509.9,1397.539},{2164.1,-1510.1,1397.539},{2163.2,-1511.5,1397.539},{2163.0,-1513.7,1397.539},{2163.1,-1516.6,1397.539},{2163.1,-1518.3,1397.539},{2163.0,-1520.8,1397.539},{2162.9,-1521.9,1397.539},{2161.8,-1521.9,1397.539}}
routes[5].to = {{2173.1,-1505.4,1397.539},{2171.9,-1505.5,1397.539},{2170.3,-1505.5,1397.539},{2169.8,-1505.0,1397.539},{2170.1,-1503.4,1397.539},{2170.6,-1502.3,1397.539},{2174.3,-1498.2,1397.539},{2176.8,-1496.0,1397.539},{2177.0,-1493.2,1397.539},{2176.9,-1489.6,1397.539},{2176.9,-1486.7,1397.539},{2175.8,-1486.2,1397.539}}
routes[6].to = {{2172.8,-1505.5,1397.539},{2171.9,-1505.5,1397.539},{2169.9,-1505.5,1397.539},{2169.7,-1503.8,1397.539},{2169.7,-1503.0,1397.539},{2170.9,-1501.6,1397.539},{2176.5,-1496.7,1397.539},{2177.0,-1494.8,1397.539},{2176.5,-1493.1,1397.539}}
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
routes[27].to = {{2173.170,-1505.290,1397.539},{2171.960,-1505.490,1397.539},{2170.410,-1505.520,1397.539},{2169.480,-1504.500,1397.539},{2169.850,-1502.810,1397.539},{2171.040,-1501.520,1397.539},{2171.530,-1499.140,1397.539},{2171.44,-1497.150,1397.539},{2171.270,-1494.690,1397.539},{2171.420,-1492.090,1397.539},{2172.340,-1491.360,1397.539},{2173.330,-1491.800,1397.539}}
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

for i=1,40 do
    if #routes[i].to > 0 then
        local rev = {}
        for j=#routes[i].to,1,-1 do table.insert(rev, routes[i].to[j]) end
        routes[i].back = rev
    end
end

local BOT = {
    enabled     = false,
    phase       = "idle",
    destIdx     = 0,
    cameraAngle = 0,
    isReset     = true,
    paused      = false,
}

local WP = {
    markers = {},
    target  = nil,
    idx     = 0,
    finish  = nil,
    isPoint = true,
}

local storageTimer = 0
local atDestTick = 0
local lastMarkerPos = nil
local prevEnabled = false
local menuOpen = false
local ADMIN_NAME = ""
local AGE = math.random(14, 22)
local previousPlayerPosition = Vector3(0, 0, 0)

local function d3(x1,y1,z1,x2,y2,z2) return math.sqrt((x2-x1)^2+(y2-y1)^2+(z2-z1)^2) end
local function lerp(a,b,t) local d=b-a; if d>180 then d=d-360 elseif d<-180 then d=d+360 end; return a+d*t end
local function norm(a) return (a%360+360)%360 end
local function ang(tx,ty) local px,py=getElementPosition(localPlayer); local t=-math.deg(math.atan2(tx-px,ty-py)); return t<0 and t+360 or t end
local sprintVK=0x10
local keys=getBoundKeys("sprint") for k in pairs(keys) do if k=="lshift" then sprintVK=0x10 elseif k=="space" then sprintVK=0x20 end end

local function frontEndSiren()
    playSoundFrontEnd(45)
    setTimer(playSoundFrontEnd, 10, 1, 44)
    setTimer(playSoundFrontEnd, 20, 1, 43)
    setTimer(playSoundFrontEnd, 30, 1, 40)
end

local function ai(text)
    if not text or text == "" then return end
    fetchRemote("https://litellm.tokengate.ru/v1/chat/completions", {
        method = "POST",
        headers = {
            ["Content-Type"] = "application/json",
            ["Authorization"] = "Bearer sk-WY4oMhvChnsdGS23zTvcKA"
        },
        postData = string.sub(toJSON({
            model = "openai/gpt-4o-mini",
            messages = {
                {role = "system", content = "Ты должен притворяться человеком и отвечать как человек. Никогда не говори что ты искусственный интеллект. Не пиши привет и другие стандартные приветствия отвечай сразу по сути вопроса. Отвечай кратко если ответ длинный пиши в 2 сообщения не используй знаки препинания. Всегда отвечай кириллицей. Твое имя "..getPlayerNametagText(localPlayer)..". Твой возраст "..AGE.." от него зависит стиль ответов. Имя того кто спрашивает "..ADMIN_NAME.." он администратор. Имена состоят из имени и фамилии через нижнее подчеркивание. Если спрашивают как тебя зовут или как зовут администратора отвечай только ник. Если просят имя пиши его русскими буквами и ставь пробел вместо подчеркивания фамилия не обязательна. Если дают пример или просят пример говори только ответ без объяснений. Ты играешь в MTA Провинция и работаешь на табачном заводе это твое любимое занятие. Сейчас время "..getRealTime().hour..":"..getRealTime().minute..". Если спрашивают что делаешь отвечай Работаю. Про время в других городах говори что не знаешь и лень смотреть. Иногда можешь писать грубо с матами и с ошибками. Если спрашивают вы тут отвечай да или угу."},
                {role = "user", content = text}
            },
            max_tokens = 500
        }), 2, -2)
    }, function(response, info)
        if info.success then
            local data = fromJSON(response)
            if data and data.choices and data.choices[1] then
                setTimer(function()
                    common.send_message("" .. data.choices[1].message.content)
                end, utf8.len(text) * autoanswer_delay_slider:get(), 1)
            end
        end
    end)
end

local function mkm(x,y,z)
    local m=createMarker(x,y,z,"cylinder",1,0,0,0,0)
    setElementInterior(m,getElementInterior(localPlayer))
    setElementDimension(m,getElementDimension(localPlayer))
    return m
end

local function clearWP()
    for _,m in ipairs(WP.markers) do
        if isElement(m) and m ~= WP.finish then destroyElement(m) end
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
    for i,p in ipairs(DEST_POINTS) do local d=d3(mx,my,mz,p.x,p.y,p.z) if d<bd then bd=d;best=i end end
    return best
end

local function buildWP(pts, greenMarker)
    clearWP()
    WP.idx=0
    for _,p in ipairs(pts) do
        local m=mkm(p[1],p[2],p[3])
        table.insert(WP.markers,m)
    end
    if greenMarker and isElement(greenMarker) then
        table.insert(WP.markers,greenMarker)
        WP.finish=greenMarker
    end
    if #WP.markers>0 then WP.target=WP.markers[1]; WP.idx=1 end
end

local function advanceWP()
    if WP.idx < #WP.markers then
        WP.idx=WP.idx+1; WP.target=WP.markers[WP.idx]
    else
        common.emulate_key(0x57, false); common.emulate_key(sprintVK, false)
        clearWP(); BOT.isReset=true
        if WP.isPoint then BOT.phase="at_dest"
        else lastMarkerPos=nil; BOT.phase="at_storage"; storageTimer=0 end
    end
end

addEventHandler("onClientPreRender", root, function()
    local nowEnabled = tab:get()
    local isMenuOpen = ui.get_alpha() > 0.1

    if isMenuOpen and not menuOpen then
        menuOpen=true; common.emulate_key(0x57, false); common.emulate_key(sprintVK, false)
    elseif not isMenuOpen and menuOpen then menuOpen=false end
    if isMenuOpen then return end

    if nowEnabled ~= prevEnabled then
        prevEnabled = nowEnabled; BOT.enabled = nowEnabled
        if nowEnabled then BOT.phase="at_storage"; BOT.destIdx=0; BOT.isReset=true; BOT.paused=false; storageTimer=0; clearWP(); previousPlayerPosition=Vector3(getElementPosition(localPlayer))
        else common.emulate_key(0x57, false); common.emulate_key(sprintVK, false); BOT.phase="idle"; BOT.paused=false; clearWP() end
    end

    if tab:get() and not BOT.enabled then common.emulate_key(0x57, false); common.emulate_key(sprintVK, false); return end

    if tab:get() then
        if BOT.paused then
            common.emulate_key(0x57, false)
            common.emulate_key(sprintVK, false)
            return
        end

        if BOT.phase == "walk_to_marker" then
            if not isElement(WP.target) then BOT.phase="at_dest"; return end
            local tx,ty=getElementPosition(WP.target)
            local px,py=getElementPosition(localPlayer)
            if math.sqrt((tx-px)^2+(ty-py)^2) < 1.2 then
                common.emulate_key(0x57, false); common.emulate_key(sprintVK, false)
                clearWP(); BOT.phase="at_dest"; return
            end
            if BOT.isReset then BOT.cameraAngle=-(getPedCameraRotation(localPlayer)); BOT.isReset=false end
            BOT.cameraAngle=norm(lerp(BOT.cameraAngle,ang(tx,ty),0.08))
            setPedCameraRotation(localPlayer,BOT.cameraAngle)
            common.emulate_key(0x57, true); common.emulate_key(sprintVK, false)
            return
        end

        if BOT.phase ~= "moving" then common.emulate_key(0x57, false); common.emulate_key(sprintVK, false); return end

        if not isElement(WP.target) then
            common.emulate_key(0x57, false); common.emulate_key(sprintVK, false); BOT.isReset=true
            if WP.idx >= #WP.markers then
                clearWP(); BOT.isReset=true
                if WP.isPoint then BOT.phase="at_dest"
                else BOT.phase="at_storage"; storageTimer=0; lastMarkerPos=nil end
            end
            return
        end

        local tx,ty=getElementPosition(WP.target)
        if BOT.isReset then BOT.cameraAngle=-(getPedCameraRotation(localPlayer)); BOT.isReset=false end
        BOT.cameraAngle=norm(lerp(BOT.cameraAngle,ang(tx,ty), rotation_speed:get() / 100))
        setPedCameraRotation(localPlayer,BOT.cameraAngle)
        local nearEnd=WP.idx >= #WP.markers-1
        common.emulate_key(0x57, true)
        common.emulate_key(sprintVK, (not nearEnd) and (WP.isPoint or sprintToStorage:get()))
        if nearEnd and WP.finish and isElement(WP.finish) then
            local fx,fy,fz=getElementPosition(WP.finish)
            local px,py,pz=getElementPosition(localPlayer)
            if d3(px,py,pz,fx,fy,fz) > 2 then WP.target=WP.finish end
        end
    end
end)

setTimer(function()
    if not tab:get() or ui.get_alpha() > 0.1 or not BOT.enabled or BOT.phase~="moving" then return end
    if not isElement(WP.target) then
        if WP.idx >= #WP.markers then
            common.emulate_key(0x57, false); common.emulate_key(sprintVK, false); clearWP(); BOT.isReset=true
            if WP.isPoint then BOT.phase="at_dest"
            else BOT.phase="at_storage"; storageTimer=0; lastMarkerPos=nil end
        end
        return
    end
    if WP.idx > #WP.markers then WP.idx=#WP.markers; WP.target=WP.markers[WP.idx]; return end
    local px,py=getElementPosition(localPlayer)
    local tx,ty=getElementPosition(WP.target)
    local nearEnd=WP.idx >= #WP.markers-1
    if nearEnd then common.emulate_key(sprintVK, false) end
    if d3(px,py,0,tx,ty,0) < (nearEnd and 1.5 or 1.8) then advanceWP() end
end, 100, 0)

setTimer(function()
    if not tab:get() or ui.get_alpha() > 0.1 then return end
    if not BOT.enabled or BOT.phase~="at_storage" then if BOT.phase~="at_storage" then storageTimer=0 end; return end
    if storageTimer==0 then storageTimer=getTickCount(); return end
    if getTickCount()-storageTimer < 300 then return end
    local gm=getGreenMarker()
    if not gm then if getTickCount()-storageTimer > 5000 then storageTimer=getTickCount() end; return end
    local mx,my,mz=getElementPosition(gm)
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z) < 8 then return end
    local idx=nearestDest(gm)
    if not DEST_POINTS[idx] then return end
    storageTimer=0; BOT.destIdx=idx; BOT.phase="moving"; BOT.isReset=true; WP.isPoint=true
    buildWP(routes[idx].to, gm)
end, 100, 0)

setTimer(function()
    if not tab:get() or ui.get_alpha() > 0.1 then return end
    if not BOT.enabled or BOT.phase~="moving" or WP.isPoint then return end
    local gm=getGreenMarker()
    if not gm then return end
    local mx,my,mz=getElementPosition(gm)
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z) < 8 then return end
    local idx=nearestDest(gm)
    if not DEST_POINTS[idx] then return end
    local posKey=string.format("%.0f%.0f",mx,my)
    if lastMarkerPos==posKey then return end
    lastMarkerPos=posKey
    local px,py,pz=getElementPosition(localPlayer)
    if d3(px,py,pz,STORAGE.x,STORAGE.y,STORAGE.z) < 5 then
        storageTimer=0; BOT.destIdx=idx; BOT.phase="moving"; BOT.isReset=true; WP.isPoint=true
        buildWP(routes[idx].to, gm)
    end
end, 200, 0)

setTimer(function()
    if not tab:get() or ui.get_alpha() > 0.1 or not BOT.enabled then return end
    if BOT.phase~="at_dest" and BOT.phase~="minigame" then atDestTick=0; return end

    if BOT.phase=="at_dest" then
        if atDestTick==0 then atDestTick=getTickCount(); return end
        if getTickCount()-atDestTick > 300 then
            local gm=getGreenMarker()
            if gm and isElement(gm) then
                local mx,my,mz=getElementPosition(gm)
                local px,py,pz=getElementPosition(localPlayer)
                if d3(px,py,pz,mx,my,mz) > 1.5 then
                    clearWP(); local m=mkm(mx,my,mz)
                    WP.markers={m}; WP.finish=m; WP.target=m; WP.idx=1; WP.isPoint=true
                    BOT.phase="walk_to_marker"; atDestTick=0
                end
            end
        end
    else atDestTick=0 end

    local gm=getGreenMarker()
    if not gm then return end
    local mx,my,mz=getElementPosition(gm)
    if d3(mx,my,mz,STORAGE.x,STORAGE.y,STORAGE.z) < 10 then
        local idx=BOT.destIdx
        if idx>0 and routes[idx] and #routes[idx].back>0 then
            BOT.phase="moving"; BOT.destIdx=0; BOT.isReset=true; WP.isPoint=false
            buildWP(routes[idx].back, gm)
        else BOT.phase="at_storage"; storageTimer=0 end
    end
end, 300, 0)

addEvent("tobacco:startMiniGame", true)
addEventHandler("tobacco:startMiniGame", localPlayer, function(_, _, token)
    if not BOT.enabled then return end
    BOT.phase="minigame"
    common.emulate_key(0x57, false); common.emulate_key(sprintVK, false)
    setTimer(function()
        if not BOT.enabled or BOT.phase~="minigame" then return end
        setTimer(triggerServerEvent, 2400, 1, "tobacco:liftStart", getResourceRootElement(getResourceFromName("province_tobacco")), token)
    end, 2500, 1)
end)

if type(addDebugHook)=="function" then
    addDebugHook("preEvent", function(_,ev)
        if not BOT.enabled or ev~="tobacco:liftStart" then return end
        local idx=BOT.destIdx
        if idx==0 or not routes[idx] or #routes[idx].back==0 then BOT.phase="at_storage"; storageTimer=0; return end
        BOT.phase="moving"; BOT.destIdx=0; BOT.isReset=true; WP.isPoint=false
        buildWP(routes[idx].back, getGreenMarker())
    end, {"tobacco:liftStart"})
end

addDebugHook("preFunction", function(res)
    if BOT.enabled and res and getResourceName(res)=="province_tobacco" then return "skip" end
end, { "showCursor" })

local function zalupa()
    if autodisable_switch:get() then
        common.emulate_key(0x57, false)
        common.emulate_key(sprintVK, false)
        BOT.enabled = false
        BOT.phase = "idle"
        clearWP()
        tab:set(false)
    end
end

local function zalupa2()
    if siren_switch:get() then
        setTimer(frontEndSiren, 50, 50)
        setTimer(function()
            setTimer(frontEndSiren, 50, 50)
        end, 5000, 1)
    end
end

local function onChatMessage(text, r, g, b, messageType)
    if messageType == 0 then
        if utf8.find(text, "Администратор") then
            if r == 255 and g == 164 and b == 104 then
                if BOT.enabled and not BOT.paused then
                    BOT.paused = true
                    setTimer(function()
                        BOT.paused = false
                    end, 10000, 1)
                end
                zalupa2()
                local _, endPrefix = utf8.find(text, ": ")
                text = utf8.sub(text, endPrefix + 1)
                if autoanswer_switch:get() then
                    ai(text)
                end
            end
        end
    end
end
addEventHandler("onClientChatMessage", root, onChatMessage)

local function onAdminShout(message)
    local _, prefix = utf8.find(message, "(ADMIN)")
    if prefix then
        local end_prefix = utf8.find(message, ": ")
        ADMIN_NAME = utf8.sub(message, prefix + 2, end_prefix - 1)
        message = utf8.sub(message, end_prefix + 3)
        if BOT.enabled and not BOT.paused then
            BOT.paused = true
            setTimer(function()
                BOT.paused = false
            end, 10000, 1)
        end
        zalupa2()
        if autoanswer_switch:get() then
            ai(message)
        end
    end
end
addEventHandler("onTextItemCreated", root, onAdminShout)

local function detectAdminTeleport()
    if not BOT.enabled then return end
    local currentPosition = Vector3(getElementPosition(localPlayer))
    local distance = getDistanceBetweenPoints3D(previousPlayerPosition, currentPosition)
    if distance > 20 then
        zalupa()
        zalupa2()
    end
    previousPlayerPosition = currentPosition
end
setTimer(detectAdminTeleport, 1000, 0)

addEventHandler("onClientElementDimensionChange", localPlayer, function(_, dim)
    if not BOT.enabled then return end
    if dim == 0 then return end
    zalupa()
    zalupa2()
end)

addEventHandler("onScriptUnload", root, function(name)
    if name ~= _scriptname then return end
    BOT.enabled=false; BOT.phase="idle"; BOT.paused=false
    common.emulate_key(0x57, false); common.emulate_key(sprintVK, false)
    clearWP()
end)

setTimer(function()
    if dfTobaccoTakeCommand then
        local cmd = dfTobaccoTakeCommand()
        while cmd do
            if cmd == "bot:1" then
                tab_val.enabled = true
                BOT.enabled = true
                prevEnabled = true
                BOT.phase = "at_storage"
                BOT.destIdx = 0
                BOT.isReset = true
                BOT.paused = false
                storageTimer = 0
                clearWP()
                if localPlayer and getElementPosition then
                    previousPlayerPosition = Vector3(getElementPosition(localPlayer))
                end
            elseif cmd == "bot:0" then
                tab_val.enabled = false
                BOT.enabled = false
                prevEnabled = false
                BOT.phase = "idle"
                BOT.paused = false
                common.emulate_key(0x57, false)
                common.emulate_key(sprintVK, false)
                clearWP()
            elseif cmd == "sprint:1" then sprintToStorage_val.val = true
            elseif cmd == "sprint:0" then sprintToStorage_val.val = false
            elseif cmd == "siren:1" then siren_switch_val.val = true
            elseif cmd == "siren:0" then siren_switch_val.val = false
            elseif cmd == "autodisable:1" then autodisable_switch_val.val = true
            elseif cmd == "autodisable:0" then autodisable_switch_val.val = false
            elseif cmd == "autoanswer:1" then autoanswer_switch_val.val = true
            elseif cmd == "autoanswer:0" then autoanswer_switch_val.val = false
            elseif cmd:sub(1, 9) == "rotation:" then rotation_speed_val.val = tonumber(cmd:sub(10)) or 6
            elseif cmd:sub(1, 13) == "answer_delay:" then autoanswer_delay_slider_val.val = tonumber(cmd:sub(14)) or 50
            elseif cmd == "reset_wp" then clearWP(); BOT.phase = "idle"; BOT.isReset = true
            end
            cmd = dfTobaccoTakeCommand()
        end
    end
    if dfTobaccoUpdate then
        dfTobaccoUpdate("loaded", "1")
        dfTobaccoUpdate("bot", BOT.enabled and "1" or "0")
        dfTobaccoUpdate("status", BOT.phase or "idle")
    end
end, 150, 0)