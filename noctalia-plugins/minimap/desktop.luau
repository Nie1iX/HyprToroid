local map = require("./grid.luau")
-- The SDK output getter shares the desktop instance's placement snapshot.
local output = barWidget.outputName()

local function render(snapshot)
    desktopWidget.render(map.tree(snapshot, output, 48, 27, true))
end

noctalia.state.watch("grid", render)
render(noctalia.state.get("grid"))
