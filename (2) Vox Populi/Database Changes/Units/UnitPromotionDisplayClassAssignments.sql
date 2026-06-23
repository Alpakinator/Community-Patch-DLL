-- PromotionDisplayClass: overrides the combat class used by the Promotion Tree UI only.
-- Units NOT listed here use their actual UnitCombatType for display purposes.
-- The DLL reads this column and exposes it via unit:GetPromotionDisplayClass().

-- Column added by VP (not CP) since the Promotion Tree is VP-only
ALTER TABLE Units ADD PromotionDisplayClass text DEFAULT NULL;

-- ============================================================================
-- Archery Units → split into Infantry Archer / Mounted Archer / Helicopter
-- ============================================================================

-- Mounted Archer units (can't take MinimumRangeRequired promotions)
UPDATE Units SET PromotionDisplayClass = 'MOUNTED_ARCHER' WHERE Type IN (
	'UNIT_CHARIOT_ARCHER',
	'UNIT_MONGOLIAN_KESHIK',
	'UNIT_ARABIAN_CAMELARCHER',
	'UNIT_HUN_HORSE_ARCHER'
);

-- Helicopter Gunship (uses HOVERING_UNIT-based promotion tree)
UPDATE Units SET PromotionDisplayClass = 'HELICOPTER' WHERE Type IN (
	'UNIT_HELICOPTER_GUNSHIP'
);

-- ============================================================================
-- Gunpowder Units → split into Gunpowder / Anti-Air
-- ============================================================================

-- Anti-Air units (get Interceptor/Sky Sweeper promotions)
UPDATE Units SET PromotionDisplayClass = 'AA' WHERE Type IN (
	'UNIT_ANTI_AIRCRAFT_GUN',
	'UNIT_MOBILE_SAM'
);
