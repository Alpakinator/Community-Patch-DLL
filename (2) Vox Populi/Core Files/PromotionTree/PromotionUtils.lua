--
-- Promotion Utility functions
--

--
-- Unit promotion functions
--

function HasPromotion(pUnit, iPromotion)
  return (pUnit and pUnit:IsHasPromotion(iPromotion))
end

function CanAcquirePromotion(pUnit, iPromotion)
  if (pUnit and pUnit:CanAcquirePromotion(iPromotion)) then
    local iAction = GetActionForPromotion(iPromotion)
	return (iAction and Game.CanHandleAction(iAction))
  end

  return false
end


--
-- Action promotion functions
--

function GetActionForPromotion(iPromotion)
  local promotion = GameInfo.UnitPromotions[iPromotion]

  for iAction, action in pairs(GameInfoActions) do
    if (action.SubType == ActionSubTypes.ACTIONSUBTYPE_PROMOTION and action.Type == promotion.Type) then
	  return iAction
    end
  end

  return nil
end


--
-- Promotions database lookup functions
--
local sMatchRank = "_[0-9IV]+$"

-- Display class configuration: maps display class to real query class + filters
-- When the DLL returns a PromotionDisplayClass for a unit, this table defines
-- which real UnitCombatType to query and what filters to apply.
local g_DisplayClassConfig = {
	MOUNTED_ARCHER = {
		queryClass = "UNITCOMBAT_ARCHER",
		-- Mounted archers cannot use promotions with MinimumRangeRequired
		baseFilter = "AND IFNULL(p.MinimumRangeRequired, 0) = 0",
		-- Dependent promos for mounted archers: query UNITCOMBAT_ARCHER, exclude range-restricted
		depClass = "UNITCOMBAT_ARCHER",
		depFilter = "AND IFNULL(p2.MinimumRangeRequired, 0) = 0",
	},
	HELICOPTER = {
		queryClass = "UNITCOMBAT_ARCHER",
		-- Helicopter promos hook on HOVERING_UNIT or have no prereq
    baseFilter = "AND (p.PromotionPrereqOr1 IS NULL OR p.PromotionPrereqOr1 = 'PROMOTION_HOVERING_UNIT')",
    depClass = "UNITCOMBAT_ARCHER",
		depFilter = "",
	},
	AA = {
		queryClass = "UNITCOMBAT_GUN",
		-- AA units: base promos are the standard gun ones plus hard-coded additions
		baseFilter = "",
		extraBasePromos = {"PROMOTION_ANTI_AIR", "PROMOTION_INTERCEPTION_I"},
		depClass = "UNITCOMBAT_GUN",
		depFilter = "",
	},
}

-- Promotions without rank suffixes that still need chain-following (GetNextPromotion)
local g_RankedExceptions = {
	PROMOTION_MEDIC = true,
	PROMOTION_CHARGE = true,
	PROMOTION_ANTI_AIR = true,
	PROMOTION_SPLASH = true,
	PROMOTION_NAVIGATOR = true,
}

function IsRankedPromotion(sPromotion)
  if g_RankedExceptions[sPromotion] then
    return true
  end

  return (sPromotion:match(sMatchRank) ~= nil)
end

function GetPromotionBase(sPromotion)
  local sRank = sPromotion:match(sMatchRank) or ""
  return sPromotion:sub(1, sPromotion:len()-sRank:len())
end

function GetNextPromotion(sPromotion, sCombatClass)
  if (not IsRankedPromotion(sPromotion)) then
    return nil
  end

  local sBase = GetPromotionBase(sPromotion)

  local promotions = {}
  local sPrereqs = "p1.Type = p2.PromotionPrereqOr1 OR p1.Type = p2.PromotionPrereqOr2 OR p1.Type = p2.PromotionPrereqOr3 OR p1.Type = p2.PromotionPrereqOr4 OR p1.Type = p2.PromotionPrereqOr5 OR p1.Type = p2.PromotionPrereqOr6 OR p1.Type = p2.PromotionPrereqOr7 OR p1.Type = p2.PromotionPrereqOr8 OR p1.Type = p2.PromotionPrereqOr9"
  local sQuery = "SELECT p2.Type FROM UnitPromotions p1, UnitPromotions p2, UnitPromotions_UnitCombats c WHERE p1.Type = ? AND (" .. sPrereqs .. ") AND p2.Type = c.PromotionType AND c.UnitCombatType = ? AND p2.Type LIKE ?"
  for row in DB.Query(sQuery, sPromotion, sCombatClass, sBase .. "%") do
    table.insert(promotions, row.Type)
  end
  
  return promotions[1]
end

function GetPromotionChain(sPromotion, sCombatClass)
  local promotions = {}

  repeat
	table.insert(promotions, sPromotion)

	sPromotion = GetNextPromotion(sPromotion, sCombatClass)
  until (sPromotion == nil)

  return promotions
end

-- Generic: find base (no PrereqOr1) promotions for a real combat class
function GetBasicPromotions(sCombatClass)
  local promotions = {}

  local sQuery = "SELECT p.Type FROM UnitPromotions p, UnitPromotions_UnitCombats c WHERE c.UnitCombatType = ? AND c.PromotionType = p.Type AND p.PromotionPrereqOr1 IS NULL AND NOT p.CannotBeChosen"
  for row in DB.Query(sQuery, sCombatClass) do
    if (IsRankedPromotion(row.Type)) then
      table.insert(promotions, row.Type)
	end
  end

  return promotions
end

-- Data-driven base promotions with display class or combat class configuration
function GetBasePromotionsForDisplayClass(sCombatClass, sConfigKey)
  local config = g_DisplayClassConfig[sConfigKey] or g_CombatClassOverrides[sConfigKey]
  local promotions = {}
  
  if config then
    -- Use the configured query class and filters
    local sQuery = "SELECT p.Type FROM UnitPromotions p, UnitPromotions_UnitCombats c WHERE c.UnitCombatType = ? AND c.PromotionType = p.Type AND p.PromotionPrereqOr1 IS NULL AND NOT p.CannotBeChosen " .. config.baseFilter
    for row in DB.Query(sQuery, config.queryClass) do
      if (IsRankedPromotion(row.Type)) then
        table.insert(promotions, row.Type)
      end
    end
    
    -- Add any extra base promotions
    if config.extraBasePromos then
      for _, sPromo in ipairs(config.extraBasePromos) do
        table.insert(promotions, sPromo)
      end
    end
    
    -- Build chains using the display class's depClass (or queryClass as fallback)
    local chainClass = config.depClass or config.queryClass
    local result = {}
    for _, sPromotion in ipairs(promotions) do
      table.insert(result, GetPromotionChain(sPromotion, chainClass))
    end
    return result
  else
    -- No special config: use generic path with the combat class as-is
    for _, sPromotion in ipairs(GetBasicPromotions(sCombatClass)) do
      table.insert(promotions, GetPromotionChain(sPromotion, sCombatClass))
    end
    return promotions
  end
end

-- Base promotion overrides for real combat classes that need special filtering
-- Key = real UnitCombatType string, Value = config for GetBasePromotionsForDisplayClass
local g_CombatClassOverrides = {
	UNITCOMBAT_ARCHER = {
		queryClass = "UNITCOMBAT_ARCHER",
		baseFilter = "AND IFNULL(p.MountedOnly, 0) = 0",
		depClass = "UNITCOMBAT_ARCHER",
		depFilter = "AND IFNULL(p2.MountedOnly, 0) = 0",
	},
	UNITCOMBAT_SIEGE = {
		queryClass = "UNITCOMBAT_SIEGE",
		baseFilter = "",
		extraBasePromos = {"PROMOTION_COVER_1"},
		depClass = "UNITCOMBAT_SIEGE",
		depFilter = "",
	},
}

-- Main entry point: get all base promotion chains for a combat class
-- sCombatClass may be a real UnitCombatType or a display-only class
function GetBasePromotions(sCombatClass)
  -- Check if this is a display-only class (in our config)
  if g_DisplayClassConfig[sCombatClass] then
    return GetBasePromotionsForDisplayClass(sCombatClass, sCombatClass)
  end
  
  -- Check if this is a real combat class that needs special handling
  if g_CombatClassOverrides[sCombatClass] then
    return GetBasePromotionsForDisplayClass(sCombatClass, sCombatClass)
  end
  
  -- Generic path
  local promotions = {}
  for _, sPromotion in ipairs(GetBasicPromotions(sCombatClass)) do
    table.insert(promotions, GetPromotionChain(sPromotion, sCombatClass))
  end
  return promotions
end

function GetDependentPromotions(sCombatClass, sPromotion)
  local promotions = {}

  -- NOTE: Removed IsRankedPromotion guard — non-ranked promotions (like MOBILITY,
  -- INDOMITABLE) can also have dependents (e.g., LOGISTICS depends on INDOMITABLE).
  local sBase = GetPromotionBase(sPromotion)
  local sPrereqs = "p1.Type = p2.PromotionPrereqOr1 OR p1.Type = p2.PromotionPrereqOr2 OR p1.Type = p2.PromotionPrereqOr3 OR p1.Type = p2.PromotionPrereqOr4 OR p1.Type = p2.PromotionPrereqOr5 OR p1.Type = p2.PromotionPrereqOr6 OR p1.Type = p2.PromotionPrereqOr7 OR p1.Type = p2.PromotionPrereqOr8 OR p1.Type = p2.PromotionPrereqOr9"
  
  -- Determine the real combat class to query against
  local queryClass = sCombatClass
  
  -- Resolve display-only classes to their real UnitCombatType
  local displayConfig = g_DisplayClassConfig[sCombatClass]
  if displayConfig then
    queryClass = displayConfig.depClass or displayConfig.queryClass
  end
  
  -- Also check combat class overrides for dependent filtering
  local combatOverride = g_CombatClassOverrides[sCombatClass]
  
  -- Special case overrides for dependent filtering
  if (sCombatClass == "UNITCOMBAT_ARCHER" or displayConfig or combatOverride) then
    -- For archer display classes and combat overrides, apply appropriate filters
    local filterClause = ""
    local overrideConfig = displayConfig or combatOverride
    if overrideConfig and overrideConfig.depFilter and overrideConfig.depFilter ~= "" then
      filterClause = " " .. overrideConfig.depFilter
    end
    
    -- For standard UNITCOMBAT_ARCHER without explicit config, exclude MountedOnly promotions
    if sCombatClass == "UNITCOMBAT_ARCHER" and not overrideConfig then
      filterClause = " AND IFNULL(p2.MountedOnly, 0) = 0"
    end
    
    local sQuery = "SELECT p2.Type FROM UnitPromotions p1, UnitPromotions p2, UnitPromotions_UnitCombats c WHERE p1.Type = ? AND (" .. sPrereqs .. ") AND p2.Type = c.PromotionType AND c.UnitCombatType = ? AND p2.Type NOT LIKE ?" .. filterClause
    for row in DB.Query(sQuery, sPromotion, queryClass, sBase .. "%") do
      table.insert(promotions, GetPromotionChain(row.Type, queryClass))
    end
  else
    local sQuery = "SELECT p2.Type FROM UnitPromotions p1, UnitPromotions p2, UnitPromotions_UnitCombats c WHERE p1.Type = ? AND (" .. sPrereqs .. ") AND p2.Type = c.PromotionType AND c.UnitCombatType = ? AND p2.Type NOT LIKE ?"
    for row in DB.Query(sQuery, sPromotion, queryClass, sBase .. "%") do
      table.insert(promotions, GetPromotionChain(row.Type, queryClass))
    end
  end

  return promotions
end

-- Find promotions that depend on any of the already-drawn promotions
-- (second-level dependents, e.g., LOGISTICS depends on INDOMITABLE which depends on TARGETING_3)
function GetSecondLevelDependents(sCombatClass, drawnPromotions)
  local promotions = {}
  local queryClass = sCombatClass
  
  local displayConfig = g_DisplayClassConfig[sCombatClass]
  local combatOverride = g_CombatClassOverrides[sCombatClass]
  local overrideConfig = displayConfig or combatOverride
  if overrideConfig then
    queryClass = overrideConfig.depClass or overrideConfig.queryClass
  end
  
  local sPrereqs = "p2.PromotionPrereqOr1 = p1.Type OR p2.PromotionPrereqOr2 = p1.Type OR p2.PromotionPrereqOr3 = p1.Type OR p2.PromotionPrereqOr4 = p1.Type OR p2.PromotionPrereqOr5 = p1.Type OR p2.PromotionPrereqOr6 = p1.Type OR p2.PromotionPrereqOr7 = p1.Type OR p2.PromotionPrereqOr8 = p1.Type OR p2.PromotionPrereqOr9 = p1.Type"
  
  for sDrawnPromo, _ in pairs(drawnPromotions) do
    local sQuery = "SELECT p2.Type FROM UnitPromotions p1, UnitPromotions p2, UnitPromotions_UnitCombats c WHERE p1.Type = ? AND (" .. sPrereqs .. ") AND p2.Type = c.PromotionType AND c.UnitCombatType = ? AND NOT p2.CannotBeChosen"
    for row in DB.Query(sQuery, sDrawnPromo, queryClass) do
      if not drawnPromotions[row.Type] then
        promotions[row.Type] = true
      end
    end
  end
  
  -- Convert to array of chains
  local result = {}
  for sPromo, _ in pairs(promotions) do
    table.insert(result, GetPromotionChain(sPromo, queryClass))
  end
  return result
end
