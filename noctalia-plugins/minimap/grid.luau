local map = {}

function map.activate(number)
    local signature = noctalia.getenv("HYPRLAND_INSTANCE_SIGNATURE")
    if not signature then
        noctalia.notifyError("HyprToroid", "No Hyprland session")
        return
    end
    local code = 'hl.dispatch(hl.dsp.focus({workspace="' .. tostring(number) .. '"}))'
    noctalia.runAsync({"hyprctl", "-i", signature, "eval", code}, function(result)
        if result.exitCode ~= 0 then
            noctalia.notifyError("HyprToroid", result.stderr .. result.stdout)
        end
    end)
end

function map.tree(snapshot, output, width, height, labels, interactive)
    if not snapshot or snapshot.error then
        if labels then
            return ui.label({text = snapshot and snapshot.error or "Connecting to Hyprland…",
                color = "on_surface", fontSize = 12, maxWidth = 170, maxLines = 3})
        end
        return ui.glyph({name = snapshot and "alert-triangle" or "layout-grid", size = 16,
            color = snapshot and "error" or "on_surface/0.5"})
    end

    local active = snapshot.focused
    if output then
        active = snapshot.outputs[output]
    end
    local gap = labels and 4 or 2
    local rows = {}
    for y = 0, 2 do
        local cells = {}
        for x = 0, 2 do
            local number = y * 3 + x + 1
            local count = snapshot.counts[number]
            local current = number == active
            local fill = current and "primary" or (count > 0 and "primary/0.3" or "surface_container")
            local children = {}
            if labels then
                children = {ui.label({text = tostring(number) .. (count > 0 and " · " .. tostring(count) or ""),
                    fontSize = 13, fontWeight = current and "bold" or "regular",
                    color = current and "on_primary" or "on_surface"})}
            end
            table.insert(cells, ui.row({key = "workspace-" .. tostring(number),
                width = width, height = height, align = "center", justify = "center",
                fill = fill, radius = labels and 4 or 2, borderWidth = 1,
                border = current and "primary" or (count > 0 and "primary/0.6" or "outline_variant"),
                tooltip = interactive ~= false
                    and "Workspace " .. tostring(number) .. " · windows: " .. tostring(count) or nil,
                onClick = interactive ~= false and function() map.activate(number) end or nil}, children))
        end
        table.insert(rows, ui.row({key = "row-" .. tostring(y), gap = gap}, cells))
    end
    return ui.column({gap = gap, align = "center", justify = "center"}, rows)
end

return map
