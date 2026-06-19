-- All civs start with an extra Pathfinder
UPDATE Eras SET StartingExploreUnits = 1 WHERE Type = 'ERA_ANCIENT';

-- Enable Vassalage
UPDATE Eras SET VassalageEnabled = 1 WHERE Type = 'ERA_MEDIEVAL';

-- Specialist Food Costs
UPDATE Eras SET SpecialistExtraFoodCost = 1 WHERE Type = 'ERA_ANCIENT';
UPDATE Eras SET SpecialistExtraFoodCost = 2 WHERE Type = 'ERA_CLASSICAL';
UPDATE Eras SET SpecialistExtraFoodCost = 3 WHERE Type = 'ERA_MEDIEVAL';
UPDATE Eras SET SpecialistExtraFoodCost = 4 WHERE Type = 'ERA_RENAISSANCE';
UPDATE Eras SET SpecialistExtraFoodCost = 5 WHERE Type = 'ERA_INDUSTRIAL';
UPDATE Eras SET SpecialistExtraFoodCost = 6 WHERE Type = 'ERA_MODERN';
UPDATE Eras SET SpecialistExtraFoodCost = 7 WHERE Type = 'ERA_POSTMODERN';
UPDATE Eras SET SpecialistExtraFoodCost = 8 WHERE Type = 'ERA_FUTURE';

-- Spies are not gained from Era advancements anymore
UPDATE Eras SET SpiesGrantedForPlayer = 0, SpiesGrantedForEveryone = 0;

-- Culture Blast Modifiers for Writers and Artists (flatten late-game curve)
-- Early game eras (full strength)
UPDATE Eras SET CultureBlastModifier = 400 WHERE Type = 'ERA_ANCIENT';
UPDATE Eras SET CultureBlastModifier = 300 WHERE Type = 'ERA_CLASSICAL';
UPDATE Eras SET CultureBlastModifier = 200 WHERE Type = 'ERA_MEDIEVAL';

-- Progressive reduction in later eras
UPDATE Eras SET CultureBlastModifier = 120 WHERE Type = 'ERA_RENAISSANCE';
UPDATE Eras SET CultureBlastModifier = 90 WHERE Type = 'ERA_INDUSTRIAL';
UPDATE Eras SET CultureBlastModifier = 80 WHERE Type = 'ERA_MODERN';
UPDATE Eras SET CultureBlastModifier = 70 WHERE Type = 'ERA_ATOMIC';
UPDATE Eras SET CultureBlastModifier = 65 WHERE Type = 'ERA_INFORMATION';

-- Fallback for mods with custom eras
UPDATE Eras SET CultureBlastModifier = 70 WHERE Type = 'ERA_POSTMODERN';
UPDATE Eras SET CultureBlastModifier = 65 WHERE Type = 'ERA_FUTURE';
