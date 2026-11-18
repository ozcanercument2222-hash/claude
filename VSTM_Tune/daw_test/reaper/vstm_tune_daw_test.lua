-- VSTM Tune - REAPER test project builder and renderer (ReaScript, Lua)
--
-- NOT TESTED IN REAPER: written without a REAPER installation (the build machine has none).
-- If a step fails, the console says which one; README.md lists the same steps for doing them by hand.
--
-- Use: open an EMPTY project (48 kHz), Actions > Show action list > New action > Load ReaScript...,
-- pick this file, run it, choose the folder that make_test_signals.py wrote. The script adds one
-- track per test (named like the render file it must give), puts VSTM Tune on it, sets the
-- parameters, draws the automation of R09 and renders every track as a stem (WAV) next to the
-- signals in "renders/". Then: python3 analyze_daw_renders.py <signals> <signals>/renders
--
-- Parameters are set by index (the plugin's own order, see Parameters.h). The script checks the
-- names of the ones it uses and stops if the order is not the expected one.

local P = { key = 0, scale = 1, correction = 2, retune = 3, humanize = 4, robotism = 5, vibrato = 6,
            formant = 7, transpose = 8, mix = 9, bypass = 10, realtime = 11, range = 12, adaptive = 26 }

local function msg (s) reaper.ShowConsoleMsg (tostring (s) .. "\n") end

local ok, dir = reaper.GetUserInputs ("VSTM Tune DAW test", 1, "Test signal folder:,extrawidth=300",
                                      reaper.GetProjectPath ("") )
if not ok then return end
dir = dir:gsub ("[/\\]$", "")
local sep = package.config:sub (1, 1)
local function file (name) return dir .. sep .. name end

local tests = {
  { name = "R01_hard_t1",          src = "t1_a3_plus40.wav",   hard = true },
  { name = "R02_pdc_realtime_t3",  src = "t3_pdc_clicks.wav" },
  { name = "R03_pdc_hq_t3",        src = "t3_pdc_clicks.wav",  set = { [P.realtime] = 0.0 } },
  { name = "R04_transpose+12_t5",  src = "t5_level.wav",       hard = true, set = { [P.transpose] = 1.0 } },
  { name = "R05_formant-12_t5",    src = "t5_level.wav",       hard = true, set = { [P.formant] = 0.0 } },
  { name = "R06_formant+12_t5",    src = "t5_level.wav",       hard = true, set = { [P.formant] = 1.0 } },
  { name = "R07_transpose-12_t5",  src = "t5_level.wav",       hard = true, set = { [P.transpose] = 0.0 } },
  { name = "R08_octave_t4",        src = "t4_octave_leap.wav", hard = true },
  { name = "R09_automation_t2",    src = "t2_melody.wav",      automate = true },
  { name = "R14_stereo_t6",        src = "t6_stereo_lr.wav",   hard = true },
}

local function setHard (tr, fx)
  reaper.TrackFX_SetParamNormalized (tr, fx, P.key, 1 / 12)       -- C
  reaper.TrackFX_SetParamNormalized (tr, fx, P.scale, 0.0)        -- Chromatic
  reaper.TrackFX_SetParamNormalized (tr, fx, P.robotism, 0.0)     -- Robotic 100 %
  reaper.TrackFX_SetParamNormalized (tr, fx, P.retune, 0.0)       -- 0 ms
  reaper.TrackFX_SetParamNormalized (tr, fx, P.humanize, 0.0)
end

local function expectName (tr, fx, idx, words)
  local _, name = reaper.TrackFX_GetParamName (tr, fx, idx, "")
  for _, w in ipairs (words) do
    if name:lower():find (w:lower(), 1, true) then return true end
  end
  msg ("STOP: parameter " .. idx .. " is \"" .. name .. "\", expected one of: " .. table.concat (words, ", "))
  return false
end

reaper.Undo_BeginBlock ()
local made = {}
for i, t in ipairs (tests) do
  local idx = reaper.CountTracks (0)
  reaper.InsertTrackAtIndex (idx, true)
  local tr = reaper.GetTrack (0, idx)
  reaper.GetSetMediaTrackInfo_String (tr, "P_NAME", t.name, true)
  reaper.SetOnlyTrackSelected (tr)
  reaper.SetEditCurPos (0, false, false)
  if reaper.InsertMedia (file (t.src), 0) == 0 then msg ("STOP: cannot import " .. file (t.src)) return end
  local fx = reaper.TrackFX_AddByName (tr, "VSTM Tune", false, -1)
  if fx < 0 then msg ("STOP: VSTM Tune (VST3) not found - rescan plugins (Options > Preferences > VST)") return end
  if i == 1 then
    msg ("VSTM Tune: " .. reaper.TrackFX_GetNumParams (tr, fx) .. " parameters")
    for k = 0, reaper.TrackFX_GetNumParams (tr, fx) - 1 do
      local _, n = reaper.TrackFX_GetParamName (tr, fx, k, "")
      msg ("  " .. k .. "  " .. n)
    end
    -- ROBOTISM has the same name in every UI language; the others are checked in the console list above
    if not expectName (tr, fx, P.robotism, { "Robotism" }) then return end
  end
  if t.hard then setHard (tr, fx) end
  for p, v in pairs (t.set or {}) do reaper.TrackFX_SetParamNormalized (tr, fx, p, v) end
  if t.automate then
    -- ROBOTISM 50 -> 0 -> 100 -> 50 %, AUTOTUNE off 7-8 s, PITCH RANGE +5 st 9-11 s
    local env = reaper.GetFXEnvelope (tr, fx, P.robotism, true)
    for _, pt in ipairs ({ { 0, 0.5 }, { 2, 0.0 }, { 4, 1.0 }, { 6, 0.5 } }) do
      reaper.InsertEnvelopePoint (env, pt[1], pt[2], 0, 0, false, true)
    end
    reaper.Envelope_SortPoints (env)
    env = reaper.GetFXEnvelope (tr, fx, P.bypass, true)
    for _, pt in ipairs ({ { 0, 0.0 }, { 7, 1.0 }, { 8, 0.0 } }) do
      reaper.InsertEnvelopePoint (env, pt[1], pt[2], 1, 0, false, true)        -- 1 = square
    end
    reaper.Envelope_SortPoints (env)
    env = reaper.GetFXEnvelope (tr, fx, P.transpose, true)
    for _, pt in ipairs ({ { 0, 0.5 }, { 9, 17 / 24 }, { 11, 0.5 } }) do
      reaper.InsertEnvelopePoint (env, pt[1], pt[2], 1, 0, false, true)
    end
    reaper.Envelope_SortPoints (env)
  end
  made[#made + 1] = tr
end
reaper.Undo_EndBlock ("VSTM Tune DAW test tracks", -1)

-- render every test track as a stem: <signals>/renders/<track name>.wav, whole project, 48 kHz stereo
for _, tr in ipairs (made) do reaper.SetTrackSelected (tr, true) end
reaper.GetSetProjectInfo_String (0, "RENDER_FILE", dir .. sep .. "renders", true)
reaper.GetSetProjectInfo_String (0, "RENDER_PATTERN", "$track", true)
reaper.GetSetProjectInfo (0, "RENDER_SETTINGS", 2, true)        -- stems (selected tracks) only
reaper.GetSetProjectInfo (0, "RENDER_BOUNDSFLAG", 1, true)      -- entire project
reaper.GetSetProjectInfo (0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo (0, "RENDER_SRATE", 48000, true)
msg ("Rendering (WAV: set 24-bit or 32-bit float in File > Render once if the format is different) ...")
reaper.Main_OnCommand (42230, 0)                                -- render with these settings, close the dialog
msg ("Done. Next: save the project, close and reopen it, render R09 again as R10 (README.md).")
