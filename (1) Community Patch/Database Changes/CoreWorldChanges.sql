UPDATE Worlds
SET
	MinDistanceCities = 3,
	MinDistanceCityStates = 3;

-- Store city-cost modifiers as percent * 100 (fixed-point).
UPDATE Worlds
SET
	NumCitiesPolicyCostMod = 1000,
	NumCitiesTechCostMod = 500
WHERE Type IN ('WORLDSIZE_DUEL', 'WORLDSIZE_TINY', 'WORLDSIZE_SMALL', 'WORLDSIZE_STANDARD');

UPDATE Worlds
SET
	NumCitiesPolicyCostMod = 750,
	NumCitiesTechCostMod = 375
WHERE Type = 'WORLDSIZE_LARGE';

UPDATE Worlds
SET
	NumCitiesPolicyCostMod = 500,
	NumCitiesTechCostMod = 250
WHERE Type = 'WORLDSIZE_HUGE';
