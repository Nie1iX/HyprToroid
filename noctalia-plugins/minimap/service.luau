local function quote(value)
    return "'" .. string.gsub(value, "'", "'\\''") .. "'"
end

local command = "exec python3 -u " .. quote(noctalia.pluginDir() .. "/hyprland.py")
local started = noctalia.runStream(command, function(line)
    local snapshot, err = noctalia.json.decode(line)
    if snapshot then
        noctalia.state.set("grid", snapshot)
    else
        noctalia.state.set("grid", {error = "Invalid Hyprland state: " .. tostring(err)})
    end
end)
if not started then
    noctalia.state.set("grid", {error = "Could not start the Hyprland connection"})
end
