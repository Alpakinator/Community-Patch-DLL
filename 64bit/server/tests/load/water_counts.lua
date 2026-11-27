-- Issue 0452: count the plots whose cached water flags are set after a load (CvPlot::m_bIsAdjacentToLand, m_bIsLake,
-- m_bIsFreshwater, read with bUseCachedValue = true, so nothing is recomputed). Taken over from ticket 4655's
-- probe-water.lua (perf/evidence/4655/, branch epic-0437-turn-study). Prints one line with the prefix "0452W|".
local n, water, adjLand, lake, fresh = Map.GetNumPlots(), 0, 0, 0, 0
for i = 0, n - 1 do
  local p = Map.GetPlotByIndex(i)
  if p:IsWater() then water = water + 1 end
  if p:IsAdjacentToLand(true) then adjLand = adjLand + 1 end
  if p:IsLake(true) then lake = lake + 1 end
  if p:IsFreshWater(true) then fresh = fresh + 1 end
end
local l = string.format("0452W|cached|turn %d|plots %d|water %d|adjacentToLand %d|lake %d|freshwater %d",
  Game.GetGameTurn(), n, water, adjLand, lake, fresh)
print(l)
return l
