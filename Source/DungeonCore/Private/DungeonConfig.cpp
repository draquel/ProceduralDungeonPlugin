// DungeonConfig.cpp — UDungeonConfiguration constructor with default RoomTypeRules
#include "DungeonConfig.h"

UDungeonConfiguration::UDungeonConfiguration()
{
	// Boss: 1, farthest from entrance, prefer main path
	FDungeonRoomTypeRule BossRule;
	BossRule.RoomType = EDungeonRoomType::Boss;
	BossRule.Count = 1;
	BossRule.Priority = 100;
	BossRule.MinGraphDistanceFromEntrance = 0.7f;
	BossRule.bPreferMainPath = true;
	RoomTypeRules.Add(BossRule);

	// Treasure: 1, prefer leaf nodes
	FDungeonRoomTypeRule TreasureRule;
	TreasureRule.RoomType = EDungeonRoomType::Treasure;
	TreasureRule.Count = 1;
	TreasureRule.Priority = 50;
	TreasureRule.bPreferLeafNodes = true;
	RoomTypeRules.Add(TreasureRule);
}
