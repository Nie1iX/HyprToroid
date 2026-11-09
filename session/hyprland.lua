-- HyprToroid on the system Hyprland. Does not import CachyOS's default binds/gestures.
local root = os.getenv("HYPRTOROID_DATA_DIR") or "/usr/share/hyprtoroid"
local lib = os.getenv("HYPRTOROID_LIB_DIR") or "/usr/lib/hyprtoroid"
local config_dir = (os.getenv("XDG_CONFIG_HOME") or (os.getenv("HOME") .. "/.config")) .. "/hyprtoroid"
local session = os.getenv("HYPRTOROID_SESSION") == "1"
local uwsm = session and os.getenv("HYPRTOROID_UWSM") == "1"
hl.env("HYPRTOROID_UWSM", uwsm and "1" or "0")
local mod = session and "SUPER" or "CTRL + ALT"
local grid_mod = session and (mod .. " + CTRL") or mod
local session_restore_cmd = 'env HYPRSESSION_PATH="${XDG_STATE_HOME:-$HOME/.local/state}/hyprtoroid/hyprsession" ' ..
    "'" .. (lib .. "/hyprsession"):gsub("'", "'\\''") .. "'"

-- This standalone session does not load CachyOS's Qt integration settings.
hl.env("QT_QPA_PLATFORMTHEME", "kde")

local plugin = os.getenv("HYPRTOROID_PLUGIN")
if not plugin then
    local selection = io.open(config_dir .. "/plugin-path", "r")
    if selection then
        plugin = assert(selection:read("*l"), "HyprToroid plugin-path is empty")
        selection:close()
    else
        plugin = lib .. "/libhyprtoroid.so"
    end
end
assert(plugin:sub(1, 1) == "/", "HyprToroid plugin path must be absolute")
hl.plugin.load(plugin)
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })
hl.config({
    debug = { disable_logs = false, enable_stdout_logs = not session },
    dwindle = { force_split = 2 }, -- New windows go right/bottom, independent of the mouse.
    input = {
        kb_layout = "us",
        kb_options = "",
        touchpad = { natural_scroll = true },
    },
    general = {
        gaps_in = 4,
        gaps_out = 8,
        border_size = 2,
        resize_on_border = false,
        col = { active_border = "rgb(51afef)", inactive_border = "rgb(444854)" },
    },
    decoration = { dim_special = 0.6 },
    misc = { disable_hyprland_logo = true, background_color = "rgb(242730)" },
    plugin = {
        hyprtasking = {
            layout = "grid",
            bg_color = 0xff242730,
            gap_size = 10,
            grid = { rows = 3, cols = 3, layers = 1, loop = true, center_current = false },
            gestures = {
                enabled = true,
                move_fingers = 3,
                move_distance = 300,
                open_fingers = 4,
                open_distance = 300,
                open_positive = true,
            },
            jump = { enabled = true },
        },
    },
})
hl.animation({ leaf = "specialWorkspaceIn", enabled = true, speed = 2.5, bezier = "default", style = "slide bottom" })
-- Exit directions are reversed by Hyprland: "top" sends the workspace down.
hl.animation({ leaf = "specialWorkspaceOut", enabled = true, speed = 2.5, bezier = "default", style = "slide top" })
for i = 1, 9 do
    hl.workspace_rule({ workspace = tostring(i), persistent = true, default = i == 1 })
    -- Physical digit keys remain usable in both US and Russian layouts.
    local key = "code:" .. (9 + i)
    hl.bind(mod .. " + " .. key, hl.dsp.focus({ workspace = tostring(i) }))
    hl.bind(mod .. " + SHIFT + " .. key, hl.dsp.window.move({ workspace = tostring(i), follow = false }))
end

hl.bind(mod .. " + Return", hl.dsp.exec_cmd("kitty"))
hl.bind(mod .. " + Space", hl.dsp.exec_cmd(session and "noctalia msg panel-toggle launcher" or "fuzzel"))
hl.bind(mod .. " + E", hl.dsp.exec_cmd("dolphin"))
-- Physical slash key keeps the help shortcut available in both US and RU.
hl.bind(mod .. " + code:61", function()
    for _, layer in ipairs(hl.get_layers({ namespace = "hyprtoroid-help" })) do
        if layer.mapped and layer.pid > 1 then
            hl.exec_cmd("kill -TERM " .. layer.pid)
            return
        end
    end
    local help_path = config_dir .. "/keybindings.txt"
    local help_file = io.open(help_path, "r")
    if help_file then
        help_file:close()
    else
        help_path = root .. "/keybindings.txt"
    end
    hl.exec_cmd("fuzzel --namespace=hyprtoroid-help --keyboard-focus=on-demand --no-exit-on-keyboard-focus-loss --dmenu --no-sort --lines=20 --width=85 --prompt='HyprToroid: ' < '" .. help_path:gsub("'", "'\\''") .. "'")
end)
hl.bind(mod .. " + Tab", function() hl.plugin.hyprtasking.toggle("cursor") end)
for _, direction in ipairs({ "Left", "Right", "Up", "Down" }) do
    local name = direction:lower()
    hl.bind(grid_mod .. " + " .. direction, function() hl.plugin.hyprtasking.move(name) end)
    hl.bind(grid_mod .. " + SHIFT + " .. direction, function() hl.plugin.hyprtasking.movewindow(name) end)
end
hl.bind(mod .. " + code:49", hl.dsp.workspace.toggle_special("scratchpad"))
hl.bind(mod .. " + SHIFT + code:49", function()
    local window = hl.get_active_window()
    if not window or not window.workspace then return end
    local target = window.workspace.name == "special:scratchpad"
        and hl.get_active_workspace() or "special:scratchpad"
    hl.dispatch(hl.dsp.window.move({ workspace = target, follow = false, window = window }))
end)
hl.bind(mod .. " + G", hl.dsp.group.toggle())
hl.bind(mod .. " + bracketright", hl.dsp.group.next())
hl.bind(mod .. " + bracketleft", hl.dsp.group.prev())
hl.bind(mod .. " + F", hl.dsp.window.fullscreen())
hl.bind(mod .. " + SHIFT + Space", hl.dsp.window.float({ action = "toggle" }))
hl.bind(mod .. " + code:33", hl.dsp.window.pseudo())
hl.bind(mod .. " + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind(mod .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })
hl.bind(mod .. " + Q", hl.dsp.window.close())
hl.bind(mod .. " + SHIFT + code:54", hl.dsp.reload_config())
hl.bind(mod .. " + SHIFT + Escape", session and hl.dsp.exec_cmd(session_restore_cmd .. " logout --dialect lua") or hl.dsp.exit())

if session then
    if os.getenv("AQ_BACKEND") ~= "wayland" then
        hl.on("hyprland.start", function()
            local prepare_env = uwsm
                and "uwsm finalize QT_QPA_PLATFORMTHEME HYPRLAND_INSTANCE_SIGNATURE HYPRTOROID_UWSM HYPRTOROID_SESSION HYPRTOROID_DATA_DIR HYPRTOROID_LIB_DIR"
                or "dbus-update-activation-environment --systemd QT_QPA_PLATFORMTHEME WAYLAND_DISPLAY XDG_CURRENT_DESKTOP HYPRLAND_INSTANCE_SIGNATURE HYPRTOROID_SESSION HYPRTOROID_DATA_DIR HYPRTOROID_LIB_DIR HYPRTOROID_UWSM"
            local shell_scope = uwsm and "uwsm app -s s -t scope -- " or ""
            local restore_scope = uwsm and "uwsm app -s b -t scope -- " or ""
            hl.exec_cmd(prepare_env .. "; /usr/lib/pam_kwallet_init; " ..
                shell_scope .. "noctalia --daemon; " .. restore_scope .. "systemd-cat --identifier=hyprtoroid-session " ..
                session_restore_cmd .. " default --dialect lua --save-interval 5")
        end)
    end
    hl.bind("SUPER + L", hl.dsp.exec_cmd("noctalia msg session lock"))
    hl.bind("SUPER + V", hl.dsp.exec_cmd("noctalia msg panel-toggle clipboard"))
    hl.bind("ALT + Tab", hl.dsp.window.cycle_next())
    hl.bind("ALT + SHIFT + Tab", hl.dsp.window.cycle_next({ next = false }))
    for direction, short in pairs({ Left = "l", Right = "r", Up = "u", Down = "d" }) do
        hl.bind("SUPER + " .. direction, hl.dsp.focus({ direction = direction:lower() }))
        hl.bind("SUPER + SHIFT + " .. direction, hl.dsp.window.move({ direction = short }))
        hl.bind("SUPER + ALT + " .. direction, hl.dsp.window.resize({
            x = short == "r" and 40 or short == "l" and -40 or 0,
            y = short == "d" and 40 or short == "u" and -40 or 0,
            relative = true,
        }), { repeating = true })
    end
    for key, command in pairs({
        XF86AudioRaiseVolume = "volume-up",
        XF86AudioLowerVolume = "volume-down",
        XF86MonBrightnessUp = "brightness-up",
        XF86MonBrightnessDown = "brightness-down",
    }) do
        hl.bind(key, hl.dsp.exec_cmd("noctalia msg " .. command), { locked = true, repeating = true })
    end
    for key, command in pairs({
        XF86AudioMute = "volume-mute",
        XF86AudioMicMute = "mic-mute",
    }) do
        hl.bind(key, hl.dsp.exec_cmd("noctalia msg " .. command), { locked = true })
    end
    hl.bind("SUPER + SHIFT + code:39", hl.dsp.exec_cmd("noctalia msg screenshot-region"))
    hl.bind("Print", hl.dsp.exec_cmd("noctalia msg screenshot-region"))
    hl.bind("SUPER + Print", hl.dsp.exec_cmd("noctalia msg screenshot-fullscreen"))
else
    hl.on("hyprland.start", function()
        hl.exec_cmd("kitty --title 'HyprToroid preview'")
    end)
end

local settings = config_dir .. "/config.lua"
local settings_file = io.open(settings, "r")
if settings_file then
    settings_file:close()
    dofile(settings)
end
