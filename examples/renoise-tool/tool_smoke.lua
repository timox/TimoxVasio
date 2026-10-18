-- Exécuter avec l'interpréteur Lua livré avec Renoise. Reproduit le clic
-- « Prendre la piste sélectionnée » avec un Song qui refuse l'opérateur ==.
local buttons = {}
local popups = {}
local entry
local song = setmetatable({
  selected_track_index = 1,
  tracks = {
    { name = "Grave", output_routing = "TimoxVasio Out 1 & 2" },
    { name = "Médium", output_routing = "TimoxVasio Out 3 & 4" },
    { name = "Aigu", output_routing = "TimoxVasio Out 5 & 6" }
  }
}, {
  __eq = function() error("Song ne prend pas en charge ==") end
})
local request
local client = {
  is_open = true,
  send = function(_, value) request = value; return true end,
  receive = function(self)
    self.line = (self.line or 0) + 1
    local lines = { "HTTP/1.0 200 OK", "Content-Type: text/plain", "", "Aperçu disponible" }
    if lines[self.line] then return lines[self.line] end
    return nil, "disconnected"
  end,
  close = function(self) self.is_open = false end
}

renoise = {
  Socket = {
    PROTOCOL_TCP = 1,
    create_client = function() client.line = 0; client.is_open = true; return client end
  },
  song = function() return song end,
  tool = function() return {
    add_menu_entry = function(_, item) entry = item end
  } end,
  app = function() return {
    show_custom_dialog = function() end
  } end,
  ViewBuilder = function()
    return setmetatable({}, { __index = function(_, kind)
      return function(_, properties)
        if kind == "button" then buttons[#buttons + 1] = properties end
        if kind == "popup" then popups[#popups + 1] = properties end
        return properties
      end
    end })
  end
}

local source = debug.getinfo(1, "S").source:sub(2)
local directory = assert(source:match("^(.*)[/\\]"))
assert(loadfile(directory .. "/com.timoxvasio.routing.xrnx/main.lua"))()
assert(entry and entry.invoke)
entry.invoke()
assert(buttons[1] and buttons[1].notifier)
buttons[1].notifier()
song.selected_track_index = 2
buttons[2].notifier()
song.selected_track_index = 3
buttons[3].notifier()
assert(popups[1] and popups[2])
popups[2].value = 2 -- SSL 3-4
buttons[5].notifier() -- Aperçu des routes
assert(request and request:find("physicalOutput=3", 1, true), request)
buttons[9].notifier() -- Transport OSC Start
assert(request and request:find("renoiseHost=127.0.0.1", 1, true), request)
assert(request:find("renoisePort=8000", 1, true), request)
assert(request:find("abletonPort=11000", 1, true), request)
buttons[14].notifier() -- Préparer ports MIDI
assert(request and request:find("POST /clock/prepare", 1, true), request)
print("TOOL_CLICK_PASS")
