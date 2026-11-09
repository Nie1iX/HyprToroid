local map = require("./grid.luau")
local output = barWidget.outputName()
local vertical = barWidget.isVertical()

local function render(snapshot)
    barWidget.render(map.tree(snapshot, output, vertical and 6 or 14,
        vertical and 14 or 6, false, false))
    barWidget.setTooltip(snapshot and snapshot.error or "HyprToroid · 3×3")
end

noctalia.state.watch("grid", render)
render(noctalia.state.get("grid"))
