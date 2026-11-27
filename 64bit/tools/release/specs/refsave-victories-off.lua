-- Ticket 0175: the huge reference game of tools/release/stages/refsave.py plays with every victory type off, so it
-- cannot end before (or two turns after) its reference turn. Run by server/run_server.py --lua-probe after the game is
-- set up and before the first turn. Game.SetVictoryValid writes the pregame's victory list (CvPreGame::setVictory),
-- which the save carries. A spec's "victories" list cannot do this on the D96 pins: the server only sets the listed
-- types valid, and the DLL's resetGame has already set every type valid (release-driver.md W5.11).
local n, off = 0, 0
for v in GameInfo.Victories() do
  Game.SetVictoryValid(v.ID, false)
  n = n + 1
  if not Game.IsVictoryValid(v.ID) then off = off + 1 end
end
print(string.format("refsave victories off: %d of %d", off, n))
