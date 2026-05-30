-- Allow civilizations to override the primary prerequisite tech for specific units.
CREATE TABLE IF NOT EXISTS Civilization_UnitTechOverrides (
CivilizationType text REFERENCES Civilizations(Type),
UnitType text REFERENCES Units(Type),
PrereqTech text REFERENCES Technologies(Type),
PRIMARY KEY (CivilizationType, UnitType)
);
