-- ultrabot.lua - MAME counterpart of ki1-port's KI1_ULTRA_BOT: start a 2P
-- game (P1 Jago, idle P2), put P2 in ultra range and enter Jago's ultra
-- (DF D DB + LQ) until it runs. Logs one line per frame while the ultra
-- flag 0x88089F12 is set to $KIU_LOG (default ultrabot.log).
--
--   mame kinst -bios v1.5d -rompath "DIR_WITH_kinst/ki-l15d.u98;ROMS" \
--        -video none -sound none -nothrottle -seconds_to_run 60 \
--        -autoboot_script mame/ultrabot.lua
--
-- Frame 0 is the first frame with the game unpacked in DRAM (seg0 word 0).
-- MAME's startup runs longer than the port's, so coins and starts repeat
-- until both players are in, and character select is timed from there.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local out = assert(io.open(os.getenv("KIU_LOG") or "ultrabot.log", "w"))
local P1, P2 = 0x0808BC00, 0x0808BD00

local fields = {}
for _, p in pairs(manager.machine.ioport.ports) do
  for name, f in pairs(p.fields) do fields[name] = f end
end
local function F(n) return assert(fields[n], n) end
local c1, c2, s1, s2 = F("Coin 1"), F("Coin 2"), F("1 Player Start"), F("2 Players Start")
local up, down, left, right = F("P1 Up"), F("P1 Down"), F("P1 Left"), F("P1 Right")
local hf, lq, hf2 = F("P1 High Attack - Fierce"), F("P1 Low Attack - Quick"), F("P2 High Attack - Fierce")
local all = {c1, c2, s1, s2, up, down, left, right, hf, lq, hf2}

-- $KIU_SNAP=N: snapshot every N frames (MAME's snapshot directory)
local snap_every = tonumber(os.getenv("KIU_SNAP") or "0")
local t0, frame, step, after, tries, joined = nil, 0, 0, 0, 0, nil
sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  if not t0 then
    if sp:read_u32(0x08000000) == 0x0A00006E then t0 = frame end
    return
  end
  local t = frame - t0
  local press = {}
  local function hold(f) press[f] = true end
  -- coin up and start until both players are in (s16 0x8808F326 / F38E:
  -- 1 = human), then character select relative to that, as in the port
  local in1, in2 = sp:read_u16(0x0808F326) == 1, sp:read_u16(0x0808F38E) == 1
  if not joined then
    local k = t % 240
    if (k >= 0 and k < 8) or (k >= 30 and k < 38) then hold(c1) end
    if (k >= 60 and k < 68) or (k >= 90 and k < 98) then hold(c2) end
    if k == 150 and not in1 then hold(s1) end
    if k == 200 and in1 and not in2 then hold(s2) end
    if in1 and in2 then
      joined = t
      out:write(string.format("JOINED frame %d\n", t))
    end
  else
    local j = t - joined
    if j >= 70 and j < 74 then hold(right) end
    if j == 150 then hold(hf) end
    if j == 190 then hold(hf2) end
  end
  local flag = sp:read_u8(0x08089F12)
  local fight = joined and t > joined + 750 and sp:read_u8(P1) ~= 0 and sp:read_u8(P2) ~= 0
  if fight and flag == 0 and after == 0 then
    sp:write_u8(P2 + 0x8F, 1)
    sp:write_u16(P2 + 0x54, 0x1800)
    sp:write_u16(P1 + 0x54, 0x7800)
    local facing_left = sp:read_u8(P1 + 0x24) >= 0x80
    local fwd, back = right, left
    if facing_left then fwd, back = left, right end
    if step < 60 then hold(fwd)
    elseif step < 62 then hold(down); hold(fwd)          -- DF
    elseif step < 64 then hold(down)                     -- D
    elseif step < 66 then hold(down); hold(back)         -- DB
    elseif step < 69 then
      hold(down); hold(back); hold(lq)
      sp:write_u8(P1 + 0x8D, 2)
      sp:write_u8(P2 + 0x2C, sp:read_u8(P2 + 0x2C) | 0x10)
    end
    if step == 60 then
      tries = tries + 1
      out:write(string.format("TRY %d frame %d chars %d %d\n", tries, t, sp:read_u8(P1), sp:read_u8(P2)))
    end
    step = (step + 1) % 100
  end
  if flag ~= 0 then after = 120 end
  if flag ~= 0 or after > 0 then
    out:write(string.format("ULTRA %d flag %d bank %d frm %d combo %d p2hp %04X\n", t, flag,
      sp:read_u8(P1 + 0x34), sp:read_u16(P1 + 0x14), sp:read_u8(P1 + 0x8D), sp:read_u16(P2 + 0x54)))
    out:flush()
    if flag == 0 then after = after - 1 end
  end
  for _, f in ipairs(all) do f:set_value(press[f] and 1 or 0) end
  if snap_every > 0 and t % snap_every == 0 then manager.machine.video:snapshot() end
end)
