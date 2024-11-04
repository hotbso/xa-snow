local drefs = {
    { dref = "sim/private/controls/wxr/snow_now", default = nil, max = 1.2 },
    { dref = "sim/private/controls/snow/luma_a", default = nil, max = 5 },
    { dref = "sim/private/controls/snow/luma_r", default = nil, max = 5 },
    { dref = "sim/private/controls/snow/luma_g", default = nil, max = 5 },
    { dref = "sim/private/controls/snow/luma_b", default = nil, max = 5 },
    { dref = "sim/private/controls/twxr/snow_area_scale", default = nil, max = 5 },
    { dref = "sim/private/controls/twxr/snow_area_width", default = nil, max = 5 }
}

function win_build(wnd, x, y)
    if imgui.Button("Reset") then
        for i, dr in pairs(drefs) do
            set(dr.dref, dr.default)
    end
    
    for i, dr in pairs(drefs) do
        --imgui.TextUnformatted(dr.dref)
        local val = get(dr.dref)
        local changed, new_val = imgui.SliderFloat(dr.dref, val, 0.0, dr.max, "%0.3f")
        if changed then
            set(dr.dref, new_val)
        end
    end

end

function win_close(wnd)
end

for i, dr in pairs(drefs) do
    dr.default = get(dr.dref)
end

win = float_wnd_create(900, 280, 1, true)

local left, height, width = XPLMGetScreenBoundsGlobal()

--float_wnd_set_position(win, width / 2 - 375, height / 2)
float_wnd_set_imgui_builder(win, "win_update")
float_wnd_set_onclose(win, "win_close")
