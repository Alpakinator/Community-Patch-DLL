CREATE TEMP TABLE TechCosts (
	X INTEGER,
	TechCost INTEGER
);

INSERT INTO TechCosts
VALUES
	(0, 20),
	(1, 60),
	(2, 100),
	(3, 130),
	(4, 275),
	(5, 500),
	(6, 700),
	(7, 1750),
	(8, 2400),
	(9, 3600),
	(10, 5500),
	(11, 9000),
	(12, 11000),
	(13, 14000),
	(14, 17000),
	(15, 20000),
	(16, 24000),
	(17, 29000),
	(18, 36000);

UPDATE Technologies
SET Cost = (SELECT TechCost FROM TechCosts WHERE X = GridX)
WHERE EXISTS (SELECT 1 FROM TechCosts WHERE X = GridX);

DROP TABLE TechCosts;
