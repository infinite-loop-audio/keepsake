local plugin_ids_raw = os.getenv("KEEPSAKE_REAPER_SMOKE_PLUGIN_IDS") or ""
local log_path = os.getenv("KEEPSAKE_REAPER_SMOKE_LOG") or ""
local status_path = os.getenv("KEEPSAKE_REAPER_SMOKE_STATUS") or ""
local scan_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS") or "20000")
local settle_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_SETTLE_MS") or "1500")
local open_ui = os.getenv("KEEPSAKE_REAPER_SMOKE_OPEN_UI") == "1"
local ui_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS") or "5000")
local run_transport = os.getenv("KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT") == "1"
local play_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS") or "5000")
local play_hold_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS") or "1000")
local require_audio = os.getenv("KEEPSAKE_REAPER_SMOKE_REQUIRE_AUDIO") == "1"
local min_peak = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_MIN_PEAK") or "0.00001")

local plugin_ids = {}
for ident in string.gmatch(plugin_ids_raw, "([^;]+)") do
  plugin_ids[#plugin_ids + 1] = ident
end

local state = {
  started_at = reaper.time_precise(),
  stage = "wait-scan",
  finished = false,
  tracks = {},
  fx_indexes = {},
  found = {},
  current_index = 1,
  saw_audio = false,
  max_peak = 0.0,
}

local function elapsed_ms()
  return math.floor((reaper.time_precise() - state.started_at) * 1000.0 + 0.5)
end

local function append_file(path, line)
  if path == "" then return end
  local f = io.open(path, "a")
  if not f then return end
  f:write(line)
  f:close()
end

local function log_line(message)
  local line = string.format("[%06d ms] %s\n", elapsed_ms(), message)
  reaper.ShowConsoleMsg("!SHOW:" .. line)
  append_file(log_path, line)
end

local function write_status(result)
  if status_path == "" then return end
  local f = io.open(status_path, "w")
  if not f then return end
  f:write(result)
  f:write("\n")
  f:close()
end

local function finish(ok, detail)
  if state.finished then return end
  state.finished = true
  log_line(string.format("result=%s detail=%s", ok and "PASS" or "FAIL", detail or ""))
  write_status(ok and "PASS" or "FAIL")
end

local function get_playing()
  return (reaper.GetPlayState() & 1) == 1
end

local function update_audio_activity()
  for _, track in ipairs(state.tracks) do
    local left = reaper.Track_GetPeakInfo(track, 0) or 0.0
    local right = reaper.Track_GetPeakInfo(track, 1) or 0.0
    local peak = math.max(left, right)
    if peak > state.max_peak then
      state.max_peak = peak
    end
    if peak >= min_peak then
      state.saw_audio = true
    end
  end
end

local function prepare_midi_clip(track, note)
  local item = reaper.CreateNewMIDIItemInProj(track, 0.0, 1.0, false)
  if not item then
    return false, "midi-item-create-failed"
  end
  local take = reaper.GetMediaItemTake(item, 0)
  if not take then
    return false, "midi-take-missing"
  end
  local ok = reaper.MIDI_InsertNote(take, true, false, 0, 960, 0, note, 100, false)
  if not ok then
    return false, "midi-note-insert-failed"
  end
  reaper.MIDI_Sort(take)
  return true, nil
end

local function find_installed_fx(ident)
  local index = 0
  while true do
    local ok, name, found_ident = reaper.EnumInstalledFX(index)
    if not ok then return nil, nil end
    if found_ident == ident then
      return index, name
    end
    index = index + 1
  end
end

local function all_plugins_found()
  for _, ident in ipairs(plugin_ids) do
    if not state.found[ident] then
      return false
    end
  end
  return true
end

local function step()
  if state.finished then return end

  if #plugin_ids == 0 then
    finish(false, "plugin-list-empty")
    return
  end

  if state.stage == "wait-scan" then
    for _, ident in ipairs(plugin_ids) do
      if not state.found[ident] then
        local installed_index, installed_name = find_installed_fx(ident)
        if installed_index ~= nil then
          state.found[ident] = true
          log_line(string.format("scan-found ident=%s installed-index=%d name=%s",
                                 ident, installed_index, installed_name))
        end
      end
    end

    if all_plugins_found() then
      state.stage = "create-tracks"
      reaper.defer(step)
      return
    end

    if elapsed_ms() >= scan_timeout_ms then
      finish(false, "scan-timeout")
      return
    end

    reaper.defer(step)
    return
  end

  if state.stage == "create-tracks" then
    for i, ident in ipairs(plugin_ids) do
      reaper.InsertTrackAtIndex(i - 1, false)
      local track = reaper.GetTrack(0, i - 1)
      if not track then
        finish(false, "track-create-failed")
        return
      end
      state.tracks[i] = track
      log_line(string.format("track-created index=%d ident=%s", i - 1, ident))
    end
    state.stage = "add-fx"
    state.current_index = 1
    reaper.defer(step)
    return
  end

  if state.stage == "add-fx" then
    local i = state.current_index
    if i > #plugin_ids then
      if open_ui then
        state.stage = "open-ui"
        state.current_index = 1
      else
        state.stage = "settle"
        state.settle_started_at = reaper.time_precise()
      end
      reaper.defer(step)
      return
    end

    local ident = plugin_ids[i]
    local track = state.tracks[i]
    log_line(string.format("fx-add-start ident=%s track=%d", ident, i - 1))
    local fx_index = reaper.TrackFX_AddByName(track, ident, false, 1)
    log_line(string.format("fx-add-finish ident=%s fx-index=%d", ident, fx_index))
    if fx_index < 0 then
      finish(false, "fx-add-failed")
      return
    end
    state.fx_indexes[i] = fx_index
    state.current_index = i + 1
    reaper.defer(step)
    return
  end

  if state.stage == "open-ui" then
    local i = state.current_index
    if i > #plugin_ids then
      state.stage = "settle"
      state.settle_started_at = reaper.time_precise()
      reaper.defer(step)
      return
    end

    if not state.ui_wait_index then
      local ident = plugin_ids[i]
      log_line(string.format("fx-ui-open-start ident=%s fx-index=%d", ident, state.fx_indexes[i]))
      reaper.TrackFX_SetOpen(state.tracks[i], state.fx_indexes[i], true)
      state.ui_wait_index = i
      state.ui_started_at = reaper.time_precise()
      reaper.defer(step)
      return
    end

    local wait_i = state.ui_wait_index
    if reaper.TrackFX_GetOpen(state.tracks[wait_i], state.fx_indexes[wait_i]) then
      log_line(string.format("fx-ui-open-finish ident=%s fx-index=%d",
                             plugin_ids[wait_i], state.fx_indexes[wait_i]))
      state.current_index = wait_i + 1
      state.ui_wait_index = nil
      reaper.defer(step)
      return
    end

    local ui_elapsed = (reaper.time_precise() - state.ui_started_at) * 1000.0
    if ui_elapsed >= ui_timeout_ms then
      finish(false, "ui-open-timeout")
      return
    end

    reaper.defer(step)
    return
  end

  if state.stage == "settle" then
    local settle_elapsed = (reaper.time_precise() - state.settle_started_at) * 1000.0
    if settle_elapsed >= settle_ms then
      if run_transport then
        for i, track in ipairs(state.tracks) do
          local note = 60 + ((i - 1) % 12)
          local ok, detail = prepare_midi_clip(track, note)
          if not ok then
            finish(false, detail)
            return
          end
          log_line(string.format("midi-item-ready ident=%s note=%d", plugin_ids[i], note))
        end
        log_line("transport-play-start")
        reaper.SetEditCurPos(0.0, false, false)
        reaper.OnPlayButton()
        state.stage = "wait-play"
        state.play_started_at = reaper.time_precise()
        reaper.defer(step)
        return
      end

      if open_ui then
        for i, track in ipairs(state.tracks) do
          reaper.TrackFX_SetOpen(track, state.fx_indexes[i], false)
          log_line(string.format("fx-ui-close ident=%s fx-index=%d",
                                 plugin_ids[i], state.fx_indexes[i]))
        end
      end
      finish(true, open_ui and "multi-fx-added-ui-opened" or "multi-fx-added")
      return
    end
    reaper.defer(step)
    return
  end

  if state.stage == "wait-play" then
    if get_playing() then
      log_line("transport-playing")
      state.stage = "hold-play"
      state.play_hold_started_at = reaper.time_precise()
      reaper.defer(step)
      return
    end

    local play_elapsed = (reaper.time_precise() - state.play_started_at) * 1000.0
    if play_elapsed >= play_timeout_ms then
      finish(false, "transport-play-timeout")
      return
    end

    reaper.defer(step)
    return
  end

  if state.stage == "hold-play" then
    update_audio_activity()
    local hold_elapsed = (reaper.time_precise() - state.play_hold_started_at) * 1000.0
    if hold_elapsed >= play_hold_ms then
      log_line("transport-stop-start")
      reaper.OnStopButton()
      state.stage = "wait-stop"
      state.stop_started_at = reaper.time_precise()
      reaper.defer(step)
      return
    end

    reaper.defer(step)
    return
  end

  if state.stage == "wait-stop" then
    update_audio_activity()
    if not get_playing() then
      log_line("transport-stopped")
      log_line(string.format("audio-peak max=%.6f saw_audio=%d",
                             state.max_peak, state.saw_audio and 1 or 0))
      if require_audio and not state.saw_audio then
        finish(false, "audio-silent")
        return
      end
      if open_ui then
        for i, track in ipairs(state.tracks) do
          reaper.TrackFX_SetOpen(track, state.fx_indexes[i], false)
          log_line(string.format("fx-ui-close ident=%s fx-index=%d",
                                 plugin_ids[i], state.fx_indexes[i]))
        end
      end
      finish(true, open_ui and "multi-fx-added-ui-opened-played" or "multi-fx-added-played")
      return
    end

    local stop_elapsed = (reaper.time_precise() - state.stop_started_at) * 1000.0
    if stop_elapsed >= play_timeout_ms then
      finish(false, "transport-stop-timeout")
      return
    end

    reaper.defer(step)
    return
  end

  finish(false, "unexpected-stage")
end

log_line(string.format("script-start count=%d ids=%s", #plugin_ids, plugin_ids_raw))
reaper.defer(step)
