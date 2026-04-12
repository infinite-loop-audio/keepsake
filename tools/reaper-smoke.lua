local target_ident = os.getenv("KEEPSAKE_REAPER_SMOKE_PLUGIN_ID") or ""
local log_path = os.getenv("KEEPSAKE_REAPER_SMOKE_LOG") or ""
local status_path = os.getenv("KEEPSAKE_REAPER_SMOKE_STATUS") or ""
local scan_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS") or "15000")
local settle_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_SETTLE_MS") or "1500")
local open_ui = os.getenv("KEEPSAKE_REAPER_SMOKE_OPEN_UI") == "1"
local ui_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS") or "5000")
local run_transport = os.getenv("KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT") == "1"
local play_timeout_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS") or "5000")
local play_hold_ms = tonumber(os.getenv("KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS") or "1000")

local state = {
  started_at = reaper.time_precise(),
  stage = "wait-scan",
  finished = false,
  track = nil,
  fx_index = -1,
}

local function elapsed_ms()
  return math.floor((reaper.time_precise() - state.started_at) * 1000.0 + 0.5)
end

local function append_file(path, line)
  if path == "" then
    return
  end

  local f = io.open(path, "a")
  if not f then
    return
  end

  f:write(line)
  f:close()
end

local function log_line(message)
  local line = string.format("[%06d ms] %s\n", elapsed_ms(), message)
  reaper.ShowConsoleMsg("!SHOW:" .. line)
  append_file(log_path, line)
end

local function write_status(result)
  if status_path == "" then
    return
  end
  local f = io.open(status_path, "w")
  if not f then
    return
  end
  f:write(result)
  f:write("\n")
  f:close()
end

local function finish(ok, detail)
  if state.finished then
    return
  end
  state.finished = true
  log_line(string.format("result=%s detail=%s", ok and "PASS" or "FAIL", detail or ""))
  write_status(ok and "PASS" or "FAIL")
end

local function get_playing()
  return (reaper.GetPlayState() & 1) == 1
end

local function prepare_midi_clip()
  local item = reaper.CreateNewMIDIItemInProj(state.track, 0.0, 1.0, false)
  if not item then
    return false, "midi-item-create-failed"
  end

  local take = reaper.GetMediaItemTake(item, 0)
  if not take then
    return false, "midi-take-missing"
  end

  local ok = reaper.MIDI_InsertNote(take, true, false, 0, 960, 0, 60, 100, false)
  if not ok then
    return false, "midi-note-insert-failed"
  end

  reaper.MIDI_Sort(take)
  log_line("midi-item-ready note=60 len-ppq=960")
  return true, nil
end

local function find_installed_fx(ident)
  local index = 0
  while true do
    local ok, name, found_ident = reaper.EnumInstalledFX(index)
    if not ok then
      return nil, nil
    end
    if found_ident == ident then
      return index, name
    end
    index = index + 1
  end
end

local function step()
  if state.finished then
    return
  end

  if state.stage == "wait-scan" then
    local installed_index, installed_name = find_installed_fx(target_ident)
    if installed_index ~= nil then
      log_line(string.format("scan-found ident=%s installed-index=%d name=%s",
                             target_ident, installed_index, installed_name))
      reaper.InsertTrackAtIndex(0, false)
      state.track = reaper.GetTrack(0, 0)
      if not state.track then
        finish(false, "track-create-failed")
        return
      end
      log_line("track-created index=0")
      state.stage = "add-fx"
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

  if state.stage == "add-fx" then
    log_line(string.format("fx-add-start ident=%s", target_ident))
    state.fx_index = reaper.TrackFX_AddByName(state.track, target_ident, false, 1)
    log_line(string.format("fx-add-finish fx-index=%d", state.fx_index))
    if state.fx_index < 0 then
      finish(false, "fx-add-failed")
      return
    end

    if open_ui then
      log_line(string.format("fx-ui-open-start fx-index=%d", state.fx_index))
      reaper.TrackFX_SetOpen(state.track, state.fx_index, true)
      state.stage = "wait-ui"
      state.ui_started_at = reaper.time_precise()
    else
      state.stage = "settle"
      state.settle_started_at = reaper.time_precise()
    end
    reaper.defer(step)
    return
  end

  if state.stage == "wait-ui" then
    local is_open = reaper.TrackFX_GetOpen(state.track, state.fx_index)
    if is_open then
      log_line(string.format("fx-ui-open-finish fx-index=%d", state.fx_index))
      state.stage = "settle"
      state.settle_started_at = reaper.time_precise()
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
        local ok, detail = prepare_midi_clip()
        if not ok then
          finish(false, detail)
          return
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
        reaper.TrackFX_SetOpen(state.track, state.fx_index, false)
        log_line(string.format("fx-ui-close fx-index=%d", state.fx_index))
      end
      finish(true, open_ui and "fx-added-ui-opened" or "fx-added")
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
    if not get_playing() then
      log_line("transport-stopped")
      if open_ui then
        reaper.TrackFX_SetOpen(state.track, state.fx_index, false)
        log_line(string.format("fx-ui-close fx-index=%d", state.fx_index))
      end
      finish(true, open_ui and "fx-added-ui-opened-played" or "fx-added-played")
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

log_line(string.format("script-start ident=%s", target_ident))
reaper.defer(step)
