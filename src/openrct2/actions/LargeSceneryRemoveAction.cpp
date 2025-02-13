/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "LargeSceneryRemoveAction.h"

#include "../Cheats.h"
#include "../OpenRCT2.h"
#include "../common.h"
#include "../core/MemoryStream.h"
#include "../interface/Window.h"
#include "../localisation/Localisation.h"
#include "../localisation/StringIds.h"
#include "../management/Finance.h"
#include "../ride/Ride.h"
#include "../world/Park.h"
#include "../world/SmallScenery.h"
#include "../world/Sprite.h"

LargeSceneryRemoveAction::LargeSceneryRemoveAction(const CoordsXYZD& location, uint16_t tileIndex)
    : _loc(location)
    , _tileIndex(tileIndex)
{
}

void LargeSceneryRemoveAction::AcceptParameters(GameActionParameterVisitor& visitor)
{
    visitor.Visit(_loc);
    visitor.Visit("tileIndex", _tileIndex);
}

uint16_t LargeSceneryRemoveAction::GetActionFlags() const
{
    return GameAction::GetActionFlags();
}

void LargeSceneryRemoveAction::Serialise(DataSerialiser& stream)
{
    GameAction::Serialise(stream);

    stream << DS_TAG(_loc) << DS_TAG(_tileIndex);
}

GameActions::Result::Ptr LargeSceneryRemoveAction::Query() const
{
    GameActions::Result::Ptr res = std::make_unique<GameActions::Result>();

    const uint32_t flags = GetFlags();

    int32_t z = tile_element_height(_loc);
    res->Position.x = _loc.x + 16;
    res->Position.y = _loc.y + 16;
    res->Position.z = z;
    res->Expenditure = ExpenditureType::Landscaping;
    res->Cost = 0;

    TileElement* tileElement = FindLargeSceneryElement();
    if (tileElement == nullptr)
    {
        log_warning("Invalid game command for scenery removal, x = %d, y = %d", _loc.x, _loc.y);
        return MakeResult(GameActions::Status::InvalidParameters, STR_INVALID_SELECTION_OF_OBJECTS);
    }

    rct_scenery_entry* scenery_entry = tileElement->AsLargeScenery()->GetEntry();
    // If we have a bugged scenery entry, do not touch the tile element.
    if (scenery_entry == nullptr)
        return MakeResult(GameActions::Status::Unknown, STR_CANT_REMOVE_THIS);

    auto rotatedOffsets = CoordsXYZ{ CoordsXY{ scenery_entry->large_scenery.tiles[_tileIndex].x_offset,
                                               scenery_entry->large_scenery.tiles[_tileIndex].y_offset }
                                         .Rotate(_loc.direction),
                                     scenery_entry->large_scenery.tiles[_tileIndex].z_offset };

    auto firstTile = CoordsXYZ{ _loc.x, _loc.y, _loc.z } - rotatedOffsets;

    bool calculate_cost = true;
    for (int32_t i = 0; scenery_entry->large_scenery.tiles[i].x_offset != -1; i++)
    {
        auto currentTileRotatedOffset = CoordsXYZ{
            CoordsXY{ scenery_entry->large_scenery.tiles[i].x_offset, scenery_entry->large_scenery.tiles[i].y_offset }.Rotate(
                _loc.direction),
            scenery_entry->large_scenery.tiles[i].z_offset
        };

        auto currentTile = CoordsXYZ{ firstTile.x, firstTile.y, firstTile.z } + currentTileRotatedOffset;

        if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !gCheatsSandboxMode)
        {
            if (!map_is_location_owned({ currentTile.x, currentTile.y, currentTile.z }))
            {
                return MakeResult(GameActions::Status::NoClearance, STR_CANT_REMOVE_THIS, STR_LAND_NOT_OWNED_BY_PARK);
            }
        }

        if (!LocationValid(currentTile))
        {
            return MakeResult(GameActions::Status::NoClearance, STR_CANT_REMOVE_THIS, STR_LAND_NOT_OWNED_BY_PARK);
        }
        // Prevent duplicate costs when using the clear scenery tool that overlaps multiple large
        // scenery tile elements.
        if (flags & GAME_COMMAND_FLAG_PATH_SCENERY)
        {
            if (tileElement->AsLargeScenery()->IsAccounted())
                calculate_cost = false;

            // Sets the flag to prevent this being counted in additional calls
            tileElement->AsLargeScenery()->SetIsAccounted(true);
        }
    }

    if (calculate_cost)
        res->Cost = scenery_entry->large_scenery.removal_price * 10;

    return res;
}

GameActions::Result::Ptr LargeSceneryRemoveAction::Execute() const
{
    GameActions::Result::Ptr res = std::make_unique<GameActions::Result>();

    const uint32_t flags = GetFlags();

    int32_t z = tile_element_height(_loc);
    res->Position.x = _loc.x + 16;
    res->Position.y = _loc.y + 16;
    res->Position.z = z;
    res->Expenditure = ExpenditureType::Landscaping;
    res->Cost = 0;

    TileElement* tileElement = FindLargeSceneryElement();
    if (tileElement == nullptr)
    {
        log_warning("Invalid game command for scenery removal, x = %d, y = %d", _loc.x, _loc.y);
        return MakeResult(GameActions::Status::InvalidParameters, STR_INVALID_SELECTION_OF_OBJECTS);
    }

    rct_scenery_entry* scenery_entry = tileElement->AsLargeScenery()->GetEntry();
    // If we have a bugged scenery entry, do not touch the tile element.
    if (scenery_entry == nullptr)
        return MakeResult(GameActions::Status::Unknown, STR_CANT_REMOVE_THIS);

    tileElement->RemoveBannerEntry();

    auto rotatedFirstTile = CoordsXYZ{ CoordsXY{ scenery_entry->large_scenery.tiles[_tileIndex].x_offset,
                                                 scenery_entry->large_scenery.tiles[_tileIndex].y_offset }
                                           .Rotate(_loc.direction),
                                       scenery_entry->large_scenery.tiles[_tileIndex].z_offset };

    auto firstTile = CoordsXYZ{ _loc.x, _loc.y, _loc.z } - rotatedFirstTile;

    for (int32_t i = 0; scenery_entry->large_scenery.tiles[i].x_offset != -1; i++)
    {
        auto rotatedCurrentTile = CoordsXYZ{
            CoordsXY{ scenery_entry->large_scenery.tiles[i].x_offset, scenery_entry->large_scenery.tiles[i].y_offset }.Rotate(
                _loc.direction),
            scenery_entry->large_scenery.tiles[i].z_offset
        };

        auto currentTile = CoordsXYZ{ firstTile.x, firstTile.y, firstTile.z } + rotatedCurrentTile;

        if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !gCheatsSandboxMode)
        {
            if (!map_is_location_owned({ currentTile.x, currentTile.y, currentTile.z }))
            {
                return MakeResult(GameActions::Status::NoClearance, STR_CANT_REMOVE_THIS, STR_LAND_NOT_OWNED_BY_PARK);
            }
        }

        TileElement* sceneryElement = map_get_first_element_at(currentTile);
        bool element_found = false;
        if (sceneryElement != nullptr)
        {
            do
            {
                if (sceneryElement->GetType() != TILE_ELEMENT_TYPE_LARGE_SCENERY)
                    continue;

                if (sceneryElement->GetDirection() != _loc.direction)
                    continue;

                if (sceneryElement->AsLargeScenery()->GetSequenceIndex() != i)
                    continue;

                if (sceneryElement->GetBaseZ() != currentTile.z)
                    continue;

                // If we are removing ghost elements
                if ((flags & GAME_COMMAND_FLAG_GHOST) && sceneryElement->IsGhost() == false)
                    continue;

                map_invalidate_tile_full(currentTile);
                tile_element_remove(sceneryElement);

                element_found = true;
                break;
            } while (!(sceneryElement++)->IsLastForTile());
        }

        if (element_found == false)
        {
            log_error("Tile not found when trying to remove element!");
        }
    }

    res->Cost = scenery_entry->large_scenery.removal_price * 10;

    return res;
}

TileElement* LargeSceneryRemoveAction::FindLargeSceneryElement() const
{
    TileElement* tileElement = map_get_first_element_at(_loc);
    if (tileElement == nullptr)
        return nullptr;

    do
    {
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_LARGE_SCENERY)
            continue;

        if (tileElement->GetBaseZ() != _loc.z)
            continue;

        if (tileElement->AsLargeScenery()->GetSequenceIndex() != _tileIndex)
            continue;

        if (tileElement->GetDirection() != _loc.direction)
            continue;

        // If we are removing ghost elements
        if ((GetFlags() & GAME_COMMAND_FLAG_GHOST) && tileElement->IsGhost() == false)
            continue;

        return tileElement;

    } while (!(tileElement++)->IsLastForTile());

    return nullptr;
}
