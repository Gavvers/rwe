#ifndef RWE_PLAYERCOMMAND_H
#define RWE_PLAYERCOMMAND_H

#include <boost/variant.hpp>
#include <rwe/SceneTime.h>
#include <rwe/UnitId.h>
#include <rwe/UnitOrder.h>
#include <rwe/UnitFireOrders.h>

namespace rwe
{
    struct PlayerUnitCommand
    {
        struct IssueOrder
        {
            enum IssueKind
            {
                Immediate,
                Queued
            };
            UnitOrder order;
            IssueKind issueKind;

            IssueOrder(const UnitOrder& order, IssueKind issueKind) : order(order), issueKind(issueKind)
            {
            }
        };

        struct Stop
        {
        };

        struct SetFireOrders
        {
            UnitFireOrders orders;
        };

        using Command = boost::variant<IssueOrder, Stop, SetFireOrders>;

        UnitId unit;
        Command command;

        PlayerUnitCommand(const UnitId& unit, const Command& command) : unit(unit), command(command)
        {
        }
    };

    struct PlayerPauseGameCommand
    {
    };

    struct PlayerUnpauseGameCommand
    {
    };

    using PlayerCommand = boost::variant<PlayerUnitCommand, PlayerPauseGameCommand, PlayerUnpauseGameCommand>;
}

#endif
