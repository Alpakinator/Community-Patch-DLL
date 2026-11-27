-- Ticket 0175: report how many victory types a loaded game holds valid (read only). tools/release/stages/refsave.py
-- runs it with the two-turn load check of a new reference save, which must have every type off.
local n, valid = 0, 0
for v in GameInfo.Victories() do
  n = n + 1
  if Game.IsVictoryValid(v.ID) then valid = valid + 1 end
end
print(string.format("refsave victories valid: %d of %d", valid, n))
