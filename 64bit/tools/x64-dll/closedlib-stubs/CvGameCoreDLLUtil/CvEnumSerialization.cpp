// CvGameCoreDLLUtil enum serializers and the two FSerialization::toString enum overloads:
// real replacements (CvGameCoreDLLUtilWin32.lib member CvEnumSerialization.obj, x86 disassembly
// 2026-11-10, 162 serializer functions grouped by instruction shape).
//
// Byte format [C]:
// - every enum except GameTypes: copied to an int and written with FDataStream::Write(const int&)
//   (4 bytes); read with FDataStream::Read(int&) and stored as the enum (80 writers / 80 readers
//   in the library share one instruction shape each);
// - GameTypes: copied to a char and written with FDataStream::Write(const char&) (1 byte); read
//   with Read(char&) and sign-extended (movsbl), so GAME_TYPE_NONE (-1) round-trips.
// The DLL references 60 writers and 61 readers (MinorCivTypes only as a reader); all are defined.
#include "../FireWorks/fw_prelude.h"
#include "CvEnums.h"
#include "CvEnumSerialization.h"

#define ENUM_SERIALIZE_INT(ENUM_TYPE) \
	FDataStream& operator<<(FDataStream& saveTo, const ENUM_TYPE& readFrom) \
	{ \
		const int iValue = static_cast<int>(readFrom); \
		saveTo << iValue; \
		return saveTo; \
	} \
	FDataStream& operator>>(FDataStream& loadFrom, ENUM_TYPE& writeTo) \
	{ \
		int iValue; \
		loadFrom >> iValue; \
		writeTo = static_cast<ENUM_TYPE>(iValue); \
		return loadFrom; \
	}

#define ENUM_DESERIALIZE_INT(ENUM_TYPE) \
	FDataStream& operator>>(FDataStream& loadFrom, ENUM_TYPE& writeTo) \
	{ \
		int iValue; \
		loadFrom >> iValue; \
		writeTo = static_cast<ENUM_TYPE>(iValue); \
		return loadFrom; \
	}

ENUM_SERIALIZE_INT(ActivityTypes)
ENUM_SERIALIZE_INT(ArtStyleTypes)
ENUM_SERIALIZE_INT(AutomateTypes)
ENUM_SERIALIZE_INT(BandwidthType)
ENUM_SERIALIZE_INT(BuildTypes)
ENUM_SERIALIZE_INT(BuildingClassTypes)
ENUM_SERIALIZE_INT(BuildingTypes)
ENUM_SERIALIZE_INT(ButtonPopupTypes)
ENUM_SERIALIZE_INT(CalendarTypes)
ENUM_SERIALIZE_INT(CityAIFocusTypes)
ENUM_SERIALIZE_INT(CitySpecializationTypes)
ENUM_SERIALIZE_INT(CivilizationTypes)
ENUM_SERIALIZE_INT(ClimateTypes)
ENUM_SERIALIZE_INT(DiploUIStateTypes)
ENUM_SERIALIZE_INT(DirectionTypes)
ENUM_SERIALIZE_INT(EndTurnBlockingTypes)
ENUM_SERIALIZE_INT(EraTypes)
ENUM_SERIALIZE_INT(GameMapTypes)
ENUM_SERIALIZE_INT(GameMode)
ENUM_SERIALIZE_INT(GameOptionTypes)
ENUM_SERIALIZE_INT(GameSpeedTypes)
ENUM_SERIALIZE_INT(GameStateTypes)
ENUM_SERIALIZE_INT(GreatPeopleDirectiveTypes)
ENUM_SERIALIZE_INT(HandicapTypes)
ENUM_SERIALIZE_INT(ImprovementTypes)
ENUM_SERIALIZE_INT(InvisibleTypes)
ENUM_SERIALIZE_INT(LeaderHeadTypes)
ENUM_SERIALIZE_INT(LeaderheadAnimationTypes)
ENUM_SERIALIZE_INT(LeagueNameTypes)
ENUM_SERIALIZE_INT(LeagueProjectTypes)
ENUM_SERIALIZE_INT(LeagueSpecialSessionTypes)
ENUM_SERIALIZE_INT(LeagueTypes)
ENUM_SERIALIZE_INT(MissionAITypes)
ENUM_SERIALIZE_INT(MissionTypes)
ENUM_SERIALIZE_INT(NotificationTypes)
ENUM_SERIALIZE_INT(OrderTypes)
ENUM_SERIALIZE_INT(PlayerColorTypes)
ENUM_SERIALIZE_INT(PlayerTypes)
ENUM_SERIALIZE_INT(PolicyBranchTypes)
ENUM_SERIALIZE_INT(ReligionTypes)
ENUM_SERIALIZE_INT(ReplayMessageTypes)
ENUM_SERIALIZE_INT(ResolutionDecisionTypes)
ENUM_SERIALIZE_INT(ResolutionTypes)
ENUM_SERIALIZE_INT(ResourceTypes)
ENUM_SERIALIZE_INT(RouteTypes)
ENUM_SERIALIZE_INT(SeaLevelTypes)
ENUM_SERIALIZE_INT(SlotClaim)
ENUM_SERIALIZE_INT(SlotStatus)
ENUM_SERIALIZE_INT(StorageLocation)
ENUM_SERIALIZE_INT(TeamTypes)
ENUM_SERIALIZE_INT(TechTypes)
ENUM_SERIALIZE_INT(TurnTimerTypes)
ENUM_SERIALIZE_INT(UnitAITypes)
ENUM_SERIALIZE_INT(UnitClassTypes)
ENUM_SERIALIZE_INT(UnitTypes)
ENUM_SERIALIZE_INT(VictoryTypes)
ENUM_SERIALIZE_INT(VoteSourceTypes)
ENUM_SERIALIZE_INT(VoteTypes)
ENUM_SERIALIZE_INT(WorldSizeTypes)
ENUM_DESERIALIZE_INT(MinorCivTypes)

#undef ENUM_SERIALIZE_INT
#undef ENUM_DESERIALIZE_INT

// GameTypes: one byte (see the header comment).
FDataStream& operator<<(FDataStream& saveTo, const GameTypes& readFrom)
{
	const char chValue = static_cast<char>(readFrom);
	saveTo << chValue;
	return saveTo;
}

FDataStream& operator>>(FDataStream& loadFrom, GameTypes& writeTo)
{
	char chValue;
	loadFrom >> chValue;
	writeTo = static_cast<GameTypes>(static_cast<int>(static_cast<signed char>(chValue)));
	return loadFrom;
}

// ---------------------------------------------------------------------------------------------
// FSerialization::toString for ActivityTypes and DirectionTypes (debug text for OOS dumps).
// The x86 jump tables map the numeric values below; they were built from the original Firaxis
// enums. Vox Populi's CvEnums.h moved NO_DIRECTION to 7, which x86 already prints as
// "INVALID ENUM VALUE"; kept as is.

namespace FSerialization
{
	std::string toString(const ActivityTypes& source)
	{
		switch(static_cast<int>(source))
		{
		case -1: return std::string("NO_ACTIVITY");
		case 0: return std::string("ACTIVITY_AWAKE");
		case 1: return std::string("ACTIVITY_HOLD");
		case 2: return std::string("ACTIVITY_SLEEP");
		case 3: return std::string("ACTIVITY_HEAL");
		case 4: return std::string("ACTIVITY_SENTRY");
		case 5: return std::string("ACTIVITY_INTERCEPT");
		case 6: return std::string("ACTIVITY_MISSION");
		case 7: return std::string("NUM_ACTIVITY_TYPES");
		default: return std::string("INVALID ENUM VALUE");
		}
	}

	std::string toString(const DirectionTypes& source)
	{
		switch(static_cast<int>(source))
		{
		case -1: return std::string("NO_DIRECTION");
		case 0: return std::string("DIRECTION_NORTHEAST");
		case 1: return std::string("DIRECTION_EAST");
		case 2: return std::string("DIRECTION_SOUTHEAST");
		case 3: return std::string("DIRECTION_SOUTHWEST");
		case 4: return std::string("DIRECTION_WEST");
		case 5: return std::string("DIRECTION_NORTHWEST");
		case 6: return std::string("NUM_DIRECTION_TYPES");
		default: return std::string("INVALID ENUM VALUE");
		}
	}
}
