-- [[
-- ReaScript Name: Publish to KutPod
-- Description: Publish rendered master mix and project markers directly to KutPod online library.
-- Author: Antigravity AI (Google DeepMind pair programmer)
-- Version: 1.1
-- About:
--   Opens a Dear ImGui window inside REAPER to fill episode metadata, select a podcast show,
--   auto-extract chapters from REAPER markers, and upload the master audio with cover and transcripts.
-- ]]

local r = reaper

-- Check for ReaImGui extension
if not r.APIExists("ImGui_GetVersion") then
  r.ShowMessageBox("Este script requiere la extensión 'ReaImGui' para dibujar la interfaz.\nPor favor, instálala usando ReaPack e inténtalo de nuevo.", "ReaImGui no encontrada", 0)
  return
end

-- Global State
local State = {
  host = "https://kutpod.com",
  username = "",
  password = "",
  token = "",
  loggedIn = false,
  podcasts = {},
  selected_podcast_idx = 0,
  episode_title = "",
  episode_desc = "",
  season_num = 1,
  episode_num = 1,
  episode_type = 0, -- 0: Full, 1: Trailer, 2: Bonus
  explicit = false,
  schedule = false,
  schedule_date = "",
  audio_path = "",
  cover_path = "",
  transcript_path = "",
  auto_chapters = true,
  chapters = {},
  upload_progress = -1,
  upload_status = "",
  temp_progress_file = "",
  curl_pid = nil
}

-- Load configuration/session if saved
local function load_session()
  State.host = r.GetExtState("KutPodPublisher", "host")
  if State.host == "" then State.host = "https://kutpod.com" end
  State.username = r.GetExtState("KutPodPublisher", "username")
  State.token = r.GetExtState("KutPodPublisher", "token")
  if State.token ~= "" then
    State.loggedIn = true
  end
  State.audio_path = r.GetExtState("KutPodPublisher", "last_audio")
  State.cover_path = r.GetExtState("KutPodPublisher", "last_cover")
  State.transcript_path = r.GetExtState("KutPodPublisher", "last_transcript")

  -- Auto-detect last render settings
  local retval, render_targets = r.GetSetProjectInfo_String(0, "RENDER_TARGETS", "", false)
  if retval and render_targets ~= "" then
    local first_file = render_targets:match("([^;]+)")
    if first_file and first_file ~= "" then
      State.audio_path = first_file
    end
  end

end

-- Save configuration/session
local function save_session()
  r.SetExtState("KutPodPublisher", "host", State.host, true)
  r.SetExtState("KutPodPublisher", "username", State.username, true)
  r.SetExtState("KutPodPublisher", "token", State.token, true)
  r.SetExtState("KutPodPublisher", "last_audio", State.audio_path, true)
  r.SetExtState("KutPodPublisher", "last_cover", State.cover_path, true)
  r.SetExtState("KutPodPublisher", "last_transcript", State.transcript_path, true)
end

-- Helper function to escape shell arguments cross-platform
local function shell_quote(str)
  local platform = r.GetOS()
  if platform:match("Win") then
    local escaped = str:gsub('\\', '\\\\'):gsub('"', '\\"')
    return '"' .. escaped .. '"'
  else
    return "'" .. str:gsub("'", "'\\''") .. "'"
  end
end

-- Synchronous HTTP GET
local function http_get(url, token)
  local cmd = 'curl -s -H "Authorization: Bearer ' .. token .. '" "' .. url .. '"'
  local f = io.popen(cmd)
  if not f then return "" end
  local out = f:read("*all")
  f:close()
  return out
end

-- Synchronous HTTP POST JSON
local function http_post_json(url, json)
  local cmd
  if r.GetOS():match("Win") then
    local escaped_json = json:gsub('"', '\\"')
    cmd = 'curl -s -X POST -H "Content-Type: application/json" -d "' .. escaped_json .. '" "' .. url .. '"'
  else
    cmd = 'curl -s -X POST -H "Content-Type: application/json" -d \'' .. json .. '\' "' .. url .. '"'
  end
  local f = io.popen(cmd)
  if not f then return "" end
  local out = f:read("*all")
  f:close()
  return out
end

-- Execute curl command asynchronously in background
local function run_curl_async(args)
  local platform = r.GetOS()
  local cmd = "curl " .. args
  
  -- Create unique temp file for tracking upload progress
  State.temp_progress_file = r.GetResourcePath() .. "/kutpod_upload_progress.txt"
  -- Redirect stdout and stderr of curl to progress file
  cmd = cmd .. " > \"" .. State.temp_progress_file .. "\" 2>&1"
  
  if platform:match("Win") then
    cmd = 'start "" /b ' .. cmd
  else
    cmd = cmd .. " &"
  end
  
  os.execute(cmd)
end

-- API: Login to KutPod
local function kutpod_login()
  if State.username == "" or State.password == "" then
    State.upload_status = "Error: Usuario y contraseña requeridos"
    return false
  end
  
  State.upload_status = "Iniciando sesión..."
  local payload = '{"user":"' .. State.username .. '","password":"' .. State.password .. '"}'
  local url = State.host .. '/api/auth/login'
  
  local output = http_post_json(url, payload)
  if output and output ~= "" then
    local token = output:match('"token"%s*:%s*"([^"]+)"')
    if token then
      State.token = token
      State.loggedIn = true
      State.upload_status = "Sesión iniciada con éxito"
      save_session()
      return true
    else
      local errMsg = output:match('"error"%s*:%s*"([^"]+)"') or "Respuesta inválida"
      State.upload_status = "Error: " .. errMsg
    end
  else
    State.upload_status = "Error: No se pudo conectar al servidor"
  end
  return false
end

-- API: Fetch Last Episode from KutPod to auto-increment season and episode numbers
local function kutpod_fetch_last_episode(podcast_id)
  local url = State.host .. '/api/podcasts/' .. podcast_id .. '/episodes?page=1&per_page=1'
  local output = http_get(url, State.token)
  if output and output ~= "" then
    local maxEp = 0
    local season = 1
    for obj_str in output:gmatch("{([^}]+)}") do
      local epNum = tonumber(obj_str:match('"number"%s*:%s*(%d+)')) or tonumber(obj_str:match('"episode"%s*:%s*(%d+)'))
      local epSeason = tonumber(obj_str:match('"season"%s*:%s*(%d+)'))
      if epNum and epNum > maxEp then
        maxEp = epNum
      end
      if epSeason and epSeason > season then
        season = epSeason
      end
    end
    if maxEp == 0 then
      local total = tonumber(output:match('"total"%s*:%s*(%d+)'))
      if total and total > 0 then
        maxEp = total
      end
    end
    State.episode_num = maxEp + 1
    State.season_num = season
    State.upload_status = string.format("Número de episodio autodetectado: %d (Temporada %d)", State.episode_num, State.season_num)
  end
end

-- API: Fetch Podcasts
local function kutpod_fetch_podcasts()
  if not State.loggedIn or State.token == "" then return end
  
  State.upload_status = "Cargando podcasts..."
  local url = State.host .. '/api/podcasts'
  local output = http_get(url, State.token)
  
  if output and output ~= "" then
    State.podcasts = {}
    -- Parse list from json (robust objects matching)
    for obj_str in output:gmatch("{([^}]+)}") do
      local id = obj_str:match('"id"%s*:%s*"([^"]+)"') or obj_str:match('"id"%s*:%s*(%d+)')
      local name = obj_str:match('"name"%s*:%s*"([^"]+)"') or obj_str:match('"title"%s*:%s*"([^"]+)"')
      if id and name then
        table.insert(State.podcasts, { id = tostring(id), name = name })
      end
    end
    State.upload_status = "Podcasts cargados: " .. tostring(#State.podcasts)
    if #State.podcasts > 0 then
      local current_podcast = State.podcasts[State.selected_podcast_idx + 1] or State.podcasts[1]
      if current_podcast then
        kutpod_fetch_last_episode(current_podcast.id)
      end
    end
  else
    State.upload_status = "Error al obtener podcasts"
  end
end

-- Auto-extract markers as chapters
local function update_chapters_from_markers()
  State.chapters = {}
  if not State.auto_chapters then return end
  
  local num_markers = r.CountProjectMarkers(0)
  for i = 0, num_markers - 1 do
    local retval, isrgn, pos, rgnend, name, id = r.EnumProjectMarkers(i)
    if not isrgn then
      table.insert(State.chapters, {
        startTime = pos,
        title = name ~= "" and name or ("Capítulo " .. tostring(#State.chapters + 1))
      })
    end
  end
end

-- API: Publish Episode
local function kutpod_publish()
  if #State.podcasts == 0 then
    State.upload_status = "Error: No hay podcast seleccionado"
    return
  end
  if State.audio_path == "" then
    State.upload_status = "Error: Debes seleccionar el archivo de audio"
    return
  end
  
  local podcast = State.podcasts[State.selected_podcast_idx + 1]
  if not podcast then return end
  
  State.upload_status = "Iniciando subida..."
  State.upload_progress = 0
  save_session()
  
  -- Build curl command arguments
  local args = '-# -X POST -H ' .. shell_quote('Authorization: Bearer ' .. State.token)
  args = args .. ' -F ' .. shell_quote('title=' .. State.episode_title)
  args = args .. ' -F ' .. shell_quote('description=' .. State.episode_desc)
  args = args .. ' -F ' .. shell_quote('season=' .. tostring(State.season_num))
  args = args .. ' -F ' .. shell_quote('episode=' .. tostring(State.episode_num))
  
  local t_str = "full"
  if State.episode_type == 1 then t_str = "trailer"
  elseif State.episode_type == 2 then t_str = "bonus" end
  args = args .. ' -F ' .. shell_quote('episode_type=' .. t_str)
  args = args .. ' -F ' .. shell_quote('explicit=' .. (State.explicit and "1" or "0"))
  
  if State.schedule and State.schedule_date ~= "" then
    args = args .. ' -F ' .. shell_quote('publish_at=' .. State.schedule_date)
  end
  
  -- Files
  args = args .. ' -F ' .. shell_quote('audio=@' .. State.audio_path)
  if State.cover_path ~= "" then
    args = args .. ' -F ' .. shell_quote('cover=@' .. State.cover_path)
  end
  if State.transcript_path ~= "" then
    args = args .. ' -F ' .. shell_quote('transcript=@' .. State.transcript_path)
  end
  
  -- Chapters as JSON
  if #State.chapters > 0 then
    local chapters_json = "["
    for i, ch in ipairs(State.chapters) do
      chapters_json = chapters_json .. '{"startTime":' .. tostring(ch.startTime) .. ',"title":"' .. ch.title .. '"}'
      if i < #State.chapters then chapters_json = chapters_json .. "," end
    end
    chapters_json = chapters_json .. "]"
    args = args .. ' -F ' .. shell_quote('chapters=' .. chapters_json)
  end
  
  -- Server Endpoint
  local url = State.host .. '/api/podcasts/' .. podcast.id .. '/episodes'
  args = args .. ' ' .. shell_quote(url)
  
  -- Launch upload
  run_curl_async(args)
end

-- Read curl progress file periodically
local function monitor_upload_progress()
  if State.temp_progress_file == "" or State.upload_progress == -1 then return end
  
  local f = io.open(State.temp_progress_file, "r")
  if not f then return end
  
  local content = f:read("*all")
  f:close()
  
  -- Curl -# output displays progress as hashes: ############ 100.0% or similar
  local last_pct = nil
  for pct in content:gmatch("([%d%.]+)%%") do
    last_pct = tonumber(pct)
  end
  
  if last_pct then
    State.upload_progress = last_pct / 100
    State.upload_status = string.format("Subiendo audio: %d%%", math.floor(last_pct))
    
    if last_pct >= 100 then
      State.upload_progress = -1
      State.upload_status = "Subida completa. Procesando en KutPod..."
      os.remove(State.temp_progress_file)
      State.temp_progress_file = ""
    end
  else
    -- Check if curl exited or completed (file is no longer written to)
    if content:match("Error") or content:match("error") or content:match("curl:") then
      local errMsg = content:match('"error"%s*:%s*"([^"]+)"') or content:match("curl:%s*%d*%s*([^\n\r]+)") or "Conexión interrumpida"
      State.upload_status = "Error en la subida: " .. errMsg
      State.upload_progress = -1
      os.remove(State.temp_progress_file)
      State.temp_progress_file = ""
    elseif content:match('"id"') then
      State.upload_status = "Publicado con éxito en KutPod!"
      State.upload_progress = -1
      os.remove(State.temp_progress_file)
      State.temp_progress_file = ""
    end
  end
end

-- Main ImGui UI Context
local ctx = r.ImGui_CreateContext('Publish to KutPod')
load_session()

local function show_ui()
  -- Periodic polling
  monitor_upload_progress()
  if State.auto_chapters then update_chapters_from_markers() end

  r.ImGui_SetNextWindowSize(ctx, 550, 680, r.ImGui_Cond_FirstUseEver())
  local visible, open = r.ImGui_Begin(ctx, 'Publicar Episodio en KutPod', true, r.ImGui_WindowFlags_MenuBar())
  
  if visible then
    -- Session menu
    if r.ImGui_BeginMenuBar(ctx) then
      if r.ImGui_BeginMenu(ctx, 'Configuración') then
        if State.loggedIn then
          if r.ImGui_MenuItem(ctx, 'Cerrar Sesión (' .. State.username .. ')') then
            State.token = ""
            State.loggedIn = false
            State.podcasts = {}
            save_session()
          end
        else
          r.ImGui_MenuItem(ctx, 'Sin sesión activa')
        end
        r.ImGui_EndMenu(ctx)
      end
      r.ImGui_EndMenuBar(ctx)
    end

    if not State.loggedIn then
      -- Login Screen
      r.ImGui_Text(ctx, "Servidor KutPod:")
      local host_changed, new_host = r.ImGui_InputText(ctx, "##host", State.host)
      if host_changed then State.host = new_host end
      
      r.ImGui_Text(ctx, "Correo / Usuario:")
      local user_changed, new_user = r.ImGui_InputText(ctx, "##username", State.username)
      if user_changed then State.username = new_user end
      
      r.ImGui_Text(ctx, "Contraseña:")
      local pass_changed, new_pass = r.ImGui_InputText(ctx, "##password", State.password, r.ImGui_InputTextFlags_Password())
      if pass_changed then State.password = new_pass end
      
      r.ImGui_Spacing(ctx)
      if r.ImGui_Button(ctx, "Iniciar Sesión") then
        if kutpod_login() then
          kutpod_fetch_podcasts()
        end
      end
      
      if State.upload_status ~= "" then
        r.ImGui_Spacing(ctx)
        r.ImGui_TextWrapped(ctx, State.upload_status)
      end
    else
      -- Logged In Form
      r.ImGui_Text(ctx, "Servidor: " .. State.host)
      r.ImGui_Text(ctx, "Usuario: " .. State.username)
      r.ImGui_Separator(ctx)

      -- Podcast selection
      if #State.podcasts > 0 then
        r.ImGui_Text(ctx, "Selecciona el Podcast Show:")
        local p_names = {}
        for _, p in ipairs(State.podcasts) do table.insert(p_names, p.name) end
        local p_csv = table.concat(p_names, "\0") .. "\0"
        local c_changed, new_idx = r.ImGui_Combo(ctx, "##podcast_combo", State.selected_podcast_idx, p_csv)
        if c_changed then
          State.selected_podcast_idx = new_idx
          local current_podcast = State.podcasts[new_idx + 1]
          if current_podcast then
            kutpod_fetch_last_episode(current_podcast.id)
          end
        end
      else
        r.ImGui_Text(ctx, "Cargando o sin podcasts. Pulsa Recargar:")
        r.ImGui_SameLine(ctx)
        if r.ImGui_Button(ctx, "Recargar") then kutpod_fetch_podcasts() end
      end
      
      r.ImGui_Separator(ctx)

      -- Episode details
      r.ImGui_Text(ctx, "Título del Episodio:")
      local t_changed, new_title = r.ImGui_InputText(ctx, "##title", State.episode_title)
      if t_changed then State.episode_title = new_title end
      
      r.ImGui_Text(ctx, "Descripción del Episodio:")
      local d_changed, new_desc = r.ImGui_InputTextMultiline(ctx, "##desc", State.episode_desc, -1, 80)
      if d_changed then State.episode_desc = new_desc end

      -- Episode attributes
      r.ImGui_Text(ctx, "Temporada:")
      r.ImGui_SameLine(ctx, 160)
      r.ImGui_Text(ctx, "Nº Episodio:")
      r.ImGui_SameLine(ctx, 320)
      r.ImGui_Text(ctx, "Tipo:")

      r.ImGui_SetNextItemWidth(ctx, 130)
      local s_changed, new_s = r.ImGui_InputInt(ctx, "##season", State.season_num)
      if s_changed then State.season_num = math.max(1, new_s) end
      
      r.ImGui_SameLine(ctx, 160)
      r.ImGui_SetNextItemWidth(ctx, 130)
      local e_changed, new_e = r.ImGui_InputInt(ctx, "##episode", State.episode_num)
      if e_changed then State.episode_num = math.max(1, new_e) end
      
      r.ImGui_SameLine(ctx, 320)
      r.ImGui_SetNextItemWidth(ctx, 150)
      local types_csv = "Completo\0Trailer\0Bonus\0"
      local ty_changed, new_ty = r.ImGui_Combo(ctx, "##type", State.episode_type, types_csv)
      if ty_changed then State.episode_type = new_ty end

      -- Explicit and Schedule
      local exp_changed, new_exp = r.ImGui_Checkbox(ctx, "Contenido Explícito", State.explicit)
      if exp_changed then State.explicit = new_exp end
      
      r.ImGui_SameLine(ctx)
      local sch_changed, new_sch = r.ImGui_Checkbox(ctx, "Programar Publicación", State.schedule)
      if sch_changed then State.schedule = new_sch end
      
      if State.schedule then
        r.ImGui_Text(ctx, "Fecha de Publicación (ISO: YYYY-MM-DD HH:MM:SS):")
        local date_changed, new_date = r.ImGui_InputText(ctx, "##schedule_date", State.schedule_date)
        if date_changed then State.schedule_date = new_date end
      end

      r.ImGui_Separator(ctx)

      -- File Paths (Audio, Cover, Transcripts)
      r.ImGui_Text(ctx, "Archivo de Audio (MP3/WAV/AAC):")
      r.ImGui_SetNextItemWidth(ctx, 350)
      local audio_changed, new_audio = r.ImGui_InputText(ctx, "##audio", State.audio_path)
      if audio_changed then State.audio_path = new_audio end
      r.ImGui_SameLine(ctx)
      if r.ImGui_Button(ctx, "Buscar...##audio") then
        local ok, file = r.GetUserFileNameForRead("", "Buscar Audio", ".mp3;.wav;.m4a")
        if ok then State.audio_path = file end
      end
      r.ImGui_SameLine(ctx)
      if r.ImGui_Button(ctx, "Autodetectar##audio") then
        local retval, render_targets = r.GetSetProjectInfo_String(0, "RENDER_TARGETS", "", false)
        if retval and render_targets ~= "" then
          local first_file = render_targets:match("([^;]+)")
          if first_file and first_file ~= "" then
            State.audio_path = first_file
            State.upload_status = "Audio detectado desde la configuración de renderizado: " .. first_file
          end
        else
          State.upload_status = "No se encontraron configuraciones de renderizado en este proyecto."
        end
      end

      r.ImGui_Text(ctx, "Imagen de Portada (Opcional - PNG/JPG):")
      local cover_changed, new_cover = r.ImGui_InputText(ctx, "##cover", State.cover_path)
      if cover_changed then State.cover_path = new_cover end
      r.ImGui_SameLine(ctx)
      if r.ImGui_Button(ctx, "Buscar...##cover") then
        local ok, file = r.GetUserFileNameForRead("", "Buscar Portada", ".jpg;.jpeg;.png")
        if ok then State.cover_path = file end
      end

      r.ImGui_Text(ctx, "Transcripción (Opcional - SRT/VTT):")
      local trans_changed, new_trans = r.ImGui_InputText(ctx, "##transcript", State.transcript_path)
      if trans_changed then State.transcript_path = new_trans end
      r.ImGui_SameLine(ctx)
      if r.ImGui_Button(ctx, "Buscar...##transcript") then
        local ok, file = r.GetUserFileNameForRead("", "Buscar Transcripción", ".srt;.vtt")
        if ok then State.transcript_path = file end
      end

      r.ImGui_Separator(ctx)

      -- Chapters
      local auto_ch_changed, new_auto_ch = r.ImGui_Checkbox(ctx, "Extraer Capítulos Automáticamente desde los Marcadores de Reaper", State.auto_chapters)
      if auto_ch_changed then State.auto_chapters = new_auto_ch end
      
      if #State.chapters > 0 then
        r.ImGui_Text(ctx, string.format("Marcadores detectados como capítulos (%d):", #State.chapters))
        r.ImGui_BeginChild(ctx, "chapters_scroll", 0, 100, r.ImGui_ChildFlags_Borders())
        for _, ch in ipairs(State.chapters) do
          local time_str = r.format_timestr(ch.startTime, 0)
          r.ImGui_Text(ctx, string.format("[%s] %s", time_str, ch.title))
        end
        r.ImGui_EndChild(ctx)
      end

      r.ImGui_Separator(ctx)

      -- Upload Progress Bar & Action Button
      if State.upload_progress >= 0 then
        r.ImGui_ProgressBar(ctx, State.upload_progress, 0, 0)
      end
      
      if State.upload_status ~= "" then
        r.ImGui_TextWrapped(ctx, State.upload_status)
      end

      r.ImGui_Spacing(ctx)
      if State.upload_progress == -1 then
        if r.ImGui_Button(ctx, "Publicar en KutPod", 150, 30) then
          kutpod_publish()
        end
      end
    end

  end
  r.ImGui_End(ctx)

  if open then
    r.defer(show_ui)
  end
end

r.defer(show_ui)
