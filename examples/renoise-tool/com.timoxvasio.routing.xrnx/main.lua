-- Tool Renoise de démonstration. Le compagnon bridge.mjs applique les routes
-- via l'API TimoxVasio et transmet les commandes MIDI Clock à SuperCollider.
local HOST, PORT = "127.0.0.1", 52999
local selected = { low = nil, mid = nil, high = nil }
local labels = {}
local report
local preview_valid = false
local preview_pairs = nil
local selected_song = nil

local function encode(value)
  return tostring(value):gsub("[^%w%-_%.~]", function(char)
    return string.format("%%%02X", string.byte(char))
  end)
end

local function http_request(method, path)
  local receive_timeout = path == "/sc/start" and 25000 or
    ((path == "/apply" or path == "/clock/prepare") and 20000 or 5000)
  local client, error_message = renoise.Socket.create_client(
    HOST, PORT, renoise.Socket.PROTOCOL_TCP, 2000)
  if error_message then return nil, "Compagnon absent : " .. error_message end
  local request = method .. " " .. path .. " HTTP/1.0\r\nHost: " .. HOST ..
    "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"
  local success, send_error = client:send(request)
  if not success then client:close(); return nil, send_error or "Envoi HTTP impossible" end
  local status, receive_error = client:receive("*line", receive_timeout)
  if not status then client:close(); return nil, receive_error or "Réponse HTTP absente" end
  local code = tonumber(status:match("HTTP/%d%.%d%s+(%d+)"))
  if not code then client:close(); return nil, "Réponse HTTP invalide" end
  while true do
    local line, line_error = client:receive("*line", receive_timeout)
    if not line then client:close(); return nil, line_error or "En-têtes HTTP incomplets" end
    if line == "" or line == "\r" then break end
  end
  local lines = {}
  for _ = 1, 100 do
    local line, line_error = client:receive("*line", receive_timeout)
    if line then
      lines[#lines + 1] = line
    elseif line_error == "disconnected" then
      break
    else
      client:close()
      return nil, line_error or "Lecture HTTP interrompue"
    end
  end
  if client.is_open then client:close() end
  local body = table.concat(lines, "\n")
  if code < 200 or code >= 300 then return nil, body end
  return body
end

local function show(text)
  report.text = text
  print("[TimoxVasio Routing] " .. text)
end

local function current_track(label)
  local song = renoise.song()
  if not rawequal(song, selected_song) then
    show("La chanson a changé : rouvrir la fenêtre du Tool.")
    return nil
  end
  local index = song.selected_track_index
  if not index or not song.tracks[index] then
    show("Sélectionner une piste Renoise pour " .. label .. ".")
    return nil
  end
  return index
end

local function assign(band, label)
  local index = current_track(label)
  if not index then return end
  selected[band] = index
  labels[band].text = label .. " : " .. renoise.song().tracks[index].name
  preview_valid = false
end

local function pair_for_track(index, label)
  local track = renoise.song().tracks[index]
  if not track then error(label .. " : piste disparue") end
  local routing = track.output_routing
  if not routing:lower():find("timoxvasio", 1, true) then
    error(label .. " : choisir une sortie TimoxVasio sur la piste " .. track.name)
  end
  local left, right = routing:lower():match("out%s+(%d+)%s*&%s*(%d+)")
  left, right = tonumber(left), tonumber(right)
  if not left or not right or left % 2 ~= 1 or right ~= left + 1 then
    error(label .. " : paire stéréo illisible dans « " .. routing .. " »")
  end
  return left
end

local function guarded(action)
  local okay, result = pcall(action)
  if not okay then show("Erreur : " .. tostring(result)) end
end

local function open_dialog()
  selected_song = renoise.song()
  selected = { low = nil, mid = nil, high = nil }
  preview_valid = false
  preview_pairs = nil
  local vb = renoise.ViewBuilder()
  local function invalidate_preview() preview_valid = false end
  local renoise_name = vb:textfield { text = "Renoise.exe", width = 175, notifier = invalidate_preview }
  local sc_name = vb:textfield { text = "scsynth.exe", width = 175, notifier = invalidate_preview }
  local ableton_name = vb:textfield { text = "Ableton Live 12 Suite.exe", width = 215, notifier = invalidate_preview }
  local stereo_pairs = { "1-2", "3-4", "5-6", "7-8" }
  local ableton_master = vb:popup { items = stereo_pairs, value = 1, width = 75, notifier = invalidate_preview }
  local physical_output = vb:popup { items = stereo_pairs, value = 1, width = 75, notifier = invalidate_preview }
  local replace_physical = vb:checkbox { value = false, notifier = invalidate_preview }
  local bpm = vb:textfield { text = "120", width = 55 }
  local renoise_osc_host = vb:textfield { text = "127.0.0.1", width = 110 }
  local renoise_osc_port = vb:textfield { text = "8000", width = 55 }
  local ableton_osc_host = vb:textfield { text = "127.0.0.1", width = 110 }
  local ableton_osc_port = vb:textfield { text = "11000", width = 55 }
  labels.low = vb:text { text = "Grave : aucune piste", width = 340 }
  labels.mid = vb:text { text = "Médium : aucune piste", width = 340 }
  labels.high = vb:text { text = "Aigu : aucune piste", width = 340 }
  report = vb:multiline_text { text = "Démarrer bridge.mjs, puis cliquer État.", width = 630, height = 195 }

  local function band_row(band, label)
    return vb:row { spacing = 8, views = {
      vb:button { text = "Prendre la piste sélectionnée", width = 190,
        notifier = function() assign(band, label) end },
      labels[band]
    } }
  end

  local function plan()
    if not rawequal(renoise.song(), selected_song) then error("La chanson a changé : rouvrir la fenêtre du Tool") end
    for _, band in ipairs({ "low", "mid", "high" }) do
      if not selected[band] then error("Choisir les trois pistes de traitement Renoise") end
    end
    local low = pair_for_track(selected.low, "Grave")
    local mid = pair_for_track(selected.mid, "Médium")
    local high = pair_for_track(selected.high, "Aigu")
    preview_pairs = { low = low, mid = mid, high = high }
    local values = {
      renoise = renoise_name.text, sc = sc_name.text, ableton = ableton_name.text,
      low = low, mid = mid, high = high,
      abletonMaster = (ableton_master.value * 2) - 1,
      physicalOutput = (physical_output.value * 2) - 1,
      replacePhysical = replace_physical.value and "1" or "0"
    }
    local keys = { "renoise", "sc", "ableton", "low", "mid", "high", "abletonMaster", "physicalOutput", "replacePhysical" }
    local query = {}
    for _, key in ipairs(keys) do query[#query + 1] = key .. "=" .. encode(values[key]) end
    local text, err = http_request("POST", "/plan?" .. table.concat(query, "&"))
    if not text then error(err) end
    preview_valid = true
    show(text)
  end

  local function apply()
    if not preview_valid then error("Afficher un aperçu avant d’appliquer") end
    if not rawequal(renoise.song(), selected_song) then error("La chanson a changé : rouvrir la fenêtre du Tool") end
    for band, label in pairs({ low = "Grave", mid = "Médium", high = "Aigu" }) do
      if pair_for_track(selected[band], label) ~= preview_pairs[band] then
        preview_valid = false
        error("La sortie d’une piste a changé : refaire l’aperçu")
      end
    end
    preview_valid = false
    local text, err = http_request("POST", "/apply")
    if not text then error(err) end
    show(text)
  end

  local function clock(action)
    local path = "/clock/" .. action
    if action == "bpm" then path = path .. "?value=" .. encode(bpm.text) end
    local text, err = http_request("POST", path)
    if not text then error(err) end
    show(text)
  end

  local function prepare_midi()
    local text, err = http_request("POST", "/clock/prepare")
    if not text then error(err) end
    show(text)
  end

  local function osc_transport(action)
    local fields = {
      { "renoiseHost", renoise_osc_host.text },
      { "renoisePort", renoise_osc_port.text },
      { "abletonHost", ableton_osc_host.text },
      { "abletonPort", ableton_osc_port.text }
    }
    local query = {}
    for _, field in ipairs(fields) do
      query[#query + 1] = field[1] .. "=" .. encode(field[2])
    end
    local text, err = http_request("POST", "/osc/transport/" .. action .. "?" .. table.concat(query, "&"))
    if not text then error(err) end
    show(text)
  end

  local content = vb:column { margin = 12, spacing = 8, views = {
    vb:text { text = "Renoise → SuperCollider → Ableton → SSL", font = "bold" },
    vb:text { text = "Choisir dans Renoise les trois pistes déjà routées vers TimoxVasio." },
    band_row("low", "Grave"), band_row("mid", "Médium"), band_row("high", "Aigu"),
    vb:row { spacing = 6, views = {
      vb:text { text = "Renoise .exe" }, renoise_name,
      vb:text { text = "SC .exe" }, sc_name } },
    vb:row { spacing = 6, views = {
      vb:text { text = "Ableton .exe" }, ableton_name,
      vb:text { text = "Master paire" }, ableton_master,
      vb:text { text = "SSL paire" }, physical_output } },
    vb:row { spacing = 6, views = {
      replace_physical,
      vb:text { text = "Remplacer les routes existantes vers cette paire SSL" } } },
    vb:row { spacing = 8, views = {
      vb:button { text = "État", notifier = function() guarded(function()
        local text, err = http_request("GET", "/state")
        if not text then error(err) end
        show(text)
      end) end },
      vb:button { text = "Aperçu des routes", notifier = function() guarded(plan) end },
      vb:button { text = "Appliquer", notifier = function() guarded(apply) end } } },
    vb:row { spacing = 8, views = {
      vb:button { text = "Démarrer SC", notifier = function() guarded(function()
        local text, err = http_request("POST", "/sc/start")
        if not text then error(err) end
        show(text)
      end) end },
      vb:button { text = "Arrêter SC", notifier = function() guarded(function()
        local text, err = http_request("POST", "/sc/stop")
        if not text then error(err) end
        show(text)
      end) end } } },
    vb:text { text = "Transport OSC (Start/Stop seulement ; horloge MIDI ci-dessous)" },
    vb:row { spacing = 8, views = {
      vb:text { text = "Renoise TCP" }, renoise_osc_host, renoise_osc_port,
      vb:text { text = "AbletonOSC UDP" }, ableton_osc_host, ableton_osc_port,
      vb:button { text = "Start OSC", notifier = function() guarded(function() osc_transport("start") end) end },
      vb:button { text = "Stop OSC", notifier = function() guarded(function() osc_transport("stop") end) end } } },
    vb:row { spacing = 8, views = {
      vb:text { text = "MIDI Clock BPM" }, bpm,
      vb:button { text = "Régler BPM", notifier = function() guarded(function() clock("bpm") end) end },
      vb:button { text = "Start", notifier = function() guarded(function() clock("start") end) end },
      vb:button { text = "Stop", notifier = function() guarded(function() clock("stop") end) end } } },
    vb:row { spacing = 8, views = {
      vb:button { text = "Préparer ports MIDI", notifier = function() guarded(prepare_midi) end },
      vb:text { text = "Crée deux ports virtuels et configure SC" } } },
    report
  } }
  renoise.app():show_custom_dialog("TimoxVasio Routing Demo", content)
end

renoise.tool():add_menu_entry {
  name = "Main Menu:Tools:TimoxVasio Routing Demo",
  invoke = open_dialog
}
