#include "UnitBehaviorService.h"
#include <rwe/GameScene.h>
#include <rwe/cob/CobExecutionContext.h>
#include <rwe/geometry/Circle2f.h>
#include <rwe/math/rwe_math.h>

namespace rwe
{
    Vector2f Vector2fFromLengthAndAngle(float length, float angle)
    {
        auto v = Matrix4f::rotationY(angle) * Vector3f(0.0f, 0.0f, length);
        return Vector2f(v.x, v.z);
    }

    bool isWithinTurningCircle(const Vector3f& dest, float speed, float turnRate, float currentDirection)
    {
        auto turnRadius = speed / turnRate;

        auto anticlockwiseCircleAngle = currentDirection + (Pif / 2.0f);
        auto clockwiseCircleAngle = currentDirection - (Pif / 2.0f);
        auto anticlockwiseCircle = Circle2f(turnRadius, Vector2fFromLengthAndAngle(turnRadius, anticlockwiseCircleAngle));
        auto clockwiseCircle = Circle2f(turnRadius, Vector2fFromLengthAndAngle(turnRadius, clockwiseCircleAngle));

        return anticlockwiseCircle.contains(Vector2f(dest.x, dest.z)) || clockwiseCircle.contains(Vector2f(dest.x, dest.z));
    }

    UnitBehaviorService::UnitBehaviorService(GameScene* scene, PathFindingService* pathFindingService, MovementClassCollisionService* collisionService)
        : scene(scene), pathFindingService(pathFindingService), collisionService(collisionService)
    {
    }

    class GetTargetPositionVisitor : public boost::static_visitor<std::optional<Vector3f>>
    {
    private:
        UnitBehaviorService* service;

    public:
        explicit GetTargetPositionVisitor(UnitBehaviorService* service) : service(service) {}

        std::optional<Vector3f> operator()(const Vector3f& target) { return target; }

        std::optional<Vector3f> operator()(UnitId id)
        {
            return service->tryGetSweetSpot(id);
        }
    };

    class AttackTargetToMovingStateGoalVisitor : public boost::static_visitor<MovingStateGoal>
    {
    private:
        const GameScene* scene;

    public:
        explicit AttackTargetToMovingStateGoalVisitor(const GameScene* scene) : scene(scene) {}

        MovingStateGoal operator()(const Vector3f& target) const { return target; }
        MovingStateGoal operator()(UnitId unitId) const
        {
            const auto& targetUnit = scene->getSimulation().getUnit(unitId);
            return scene->computeFootprintRegion(targetUnit.position, targetUnit.footprintX, targetUnit.footprintZ);
        }
    };

    void UnitBehaviorService::update(UnitId unitId)
    {
        auto& unit = scene->getSimulation().getUnit(unitId);

        float previousSpeed = unit.currentSpeed;

        // Clear steering targets.
        unit.targetAngle = unit.rotation;
        unit.targetSpeed = 0.0f;

        // check our orders
        if (!unit.orders.empty())
        {
            const auto& order = unit.orders.front();

            // process move orders
            if (auto moveOrder = boost::get<MoveOrder>(&order); moveOrder != nullptr)
            {
                if (auto idleState = boost::get<IdleState>(&unit.behaviourState); idleState != nullptr)
                {
                    // request a path to follow
                    scene->getSimulation().requestPath(unitId);
                    const auto& destination = moveOrder->destination;
                    unit.behaviourState = MovingState{destination, std::nullopt, true};
                }
                else if (auto movingState = boost::get<MovingState>(&unit.behaviourState); movingState != nullptr)
                {
                    // if we are colliding, request a new path
                    if (unit.inCollision && !movingState->pathRequested)
                    {
                        auto& sim = scene->getSimulation();

                        // only request a new path if we don't have one yet,
                        // or we've already had our current one for a bit
                        if (!movingState->path || (sim.gameTime - movingState->path->pathCreationTime) >= GameTimeDelta(60))
                        {
                            sim.requestPath(unitId);
                            movingState->pathRequested = true;
                        }
                    }

                    // if a path is available, attempt to follow it
                    auto& pathToFollow = movingState->path;
                    if (pathToFollow)
                    {
                        if (followPath(unit, *pathToFollow))
                        {
                            // we finished following the path,
                            // order complete
                            unit.orders.pop_front();
                            unit.behaviourState = IdleState();

                            if (unit.arrivedSound)
                            {
                                scene->playSoundOnSelectChannel(*unit.arrivedSound);
                            }
                        }
                    }
                }
            }
            else if (auto attackOrder = boost::get<AttackOrder>(&order); attackOrder != nullptr)
            {
                if (handleAttackOrder(unitId, *attackOrder))
                {
                    unit.orders.pop_front();
                }
            }
        }

        for (unsigned int i = 0; i < unit.weapons.size(); ++i)
        {
            updateWeapon(unitId, i);
        }

        applyUnitSteering(unitId);

        if (unit.currentSpeed > 0.0f && previousSpeed == 0.0f)
        {
            unit.cobEnvironment->createThread("StartMoving");
        }
        else if (unit.currentSpeed == 0.0f && previousSpeed > 0.0f)
        {
            unit.cobEnvironment->createThread("StopMoving");
        }

        updateUnitPosition(unitId);
    }

    std::pair<float, float> UnitBehaviorService::computeHeadingAndPitch(float rotation, const Vector3f& from, const Vector3f& to)
    {
        auto aimVector = to - from;
        Vector3f aimVectorXZ(aimVector.x, 0.0f, aimVector.z);

        auto heading = Unit::toRotation(aimVectorXZ);
        heading = wrap(-Pif, Pif, heading - rotation);

        auto pitchNormal = aimVectorXZ.cross(Vector3f(0.0f, 1.0f, 0.0f));
        auto pitch = aimVectorXZ.angleTo(aimVector, pitchNormal);

        return {heading, pitch};
    }

    bool UnitBehaviorService::followPath(Unit& unit, PathFollowingInfo& path)
    {
        const auto& destination = *path.currentWaypoint;
        Vector3f xzPosition(unit.position.x, 0.0f, unit.position.z);
        Vector3f xzDestination(destination.x, 0.0f, destination.z);
        auto distanceSquared = xzPosition.distanceSquared(xzDestination);

        auto isFinalDestination = path.currentWaypoint == (path.path.waypoints.end() - 1);

        if (distanceSquared < (8.0f * 8.0f))
        {
            if (isFinalDestination)
            {
                return true;
            }
            else
            {
                ++path.currentWaypoint;
            }
        }
        else
        {
            // steer towards the goal
            auto xzDirection = xzDestination - xzPosition;
            unit.targetAngle = Unit::toRotation(xzDirection);

            // drive at full speed until we need to brake
            // to turn or to arrive at the goal
            auto brakingDistance = (unit.currentSpeed * unit.currentSpeed) / (2.0f * unit.brakeRate);

            if (isWithinTurningCircle(xzDirection, unit.currentSpeed, unit.turnRate, unit.rotation))
            {
                unit.targetSpeed = 0.0f;
            }
            else if (isFinalDestination && distanceSquared <= (brakingDistance * brakingDistance))
            {
                unit.targetSpeed = 0.0f;
            }
            else
            {
                unit.targetSpeed = unit.maxSpeed;
            }
        }

        return false;
    }

    void UnitBehaviorService::updateWeapon(UnitId id, unsigned int weaponIndex)
    {
        auto& unit = scene->getSimulation().getUnit(id);
        auto& weapon = unit.weapons[weaponIndex];
        if (!weapon)
        {
            return;
        }

        if (auto idleState = boost::get<UnitWeaponStateIdle>(&weapon->state); idleState != nullptr)
        {
            // attempt to acquire a target
            if (!weapon->commandFire)
            {
                for (const auto& entry : scene->getSimulation().units)
                {
                    auto otherUnitId = entry.first;
                    const auto& otherUnit = entry.second;

                    if (otherUnit.isOwnedBy(unit.owner))
                    {
                        continue;
                    }

                    if (unit.position.distanceSquared(otherUnit.position) > weapon->maxRange * weapon->maxRange)
                    {
                        continue;
                    }

                    weapon->state = UnitWeaponStateAttacking(otherUnitId);
                    break;
                }
            }
        }
        else if (auto aimingState = boost::get<UnitWeaponStateAttacking>(&weapon->state); aimingState != nullptr)
        {
            GetTargetPositionVisitor targetPositionVisitor(this);
            auto targetPosition = boost::apply_visitor(targetPositionVisitor, aimingState->target);

            if (!targetPosition || unit.position.distanceSquared(*targetPosition) > weapon->maxRange * weapon->maxRange)
            {
                unit.clearWeaponTarget(weaponIndex);
            }
            else if (!aimingState->aimInfo)
            {
                auto aimFromPosition = getAimingPoint(id, weaponIndex);

                auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                auto threadId = unit.cobEnvironment->createThread(getAimScriptName(weaponIndex), {toTaAngle(RadiansAngle(heading)).value, toTaAngle(RadiansAngle(pitch)).value});

                if (threadId)
                {
                    aimingState->aimInfo = UnitWeaponStateAttacking::AimInfo{*threadId, heading, pitch};
                }
                else
                {
                    // We couldn't launch an aiming script (there isn't one),
                    // just go straight to firing.
                    tryFireWeapon(id, weaponIndex, *targetPosition);
                }
            }
            else
            {
                const auto aimInfo = *aimingState->aimInfo;
                auto returnValue = unit.cobEnvironment->tryReapThread(aimInfo.thread);
                if (returnValue)
                {
                    // we successfully reaped, clear the thread.
                    aimingState->aimInfo = std::nullopt;

                    if (*returnValue)
                    {
                        // aiming was successful, check the target again for drift
                        GetTargetPositionVisitor targetPositionVisitor(this);
                        auto targetPosition = boost::apply_visitor(targetPositionVisitor, aimingState->target);
                        auto aimFromPosition = getAimingPoint(id, weaponIndex);

                        auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition);
                        auto heading = headingAndPitch.first;
                        auto pitch = headingAndPitch.second;

                        // if the target is close enough, try to fire
                        if (std::abs(heading - aimInfo.lastHeading) <= weapon->tolerance && std::abs(pitch - aimInfo.lastPitch) <= weapon->pitchTolerance)
                        {
                            tryFireWeapon(id, weaponIndex, *targetPosition);
                        }
                    }
                }
            }
        }
    }

    void UnitBehaviorService::tryFireWeapon(UnitId id, unsigned int weaponIndex, const Vector3f& targetPosition)
    {
        auto& unit = scene->getSimulation().getUnit(id);
        auto& weapon = unit.weapons[weaponIndex];

        if (!weapon)
        {
            return;
        }

        // wait for the weapon to reload
        auto gameTime = scene->getGameTime();
        if (gameTime < weapon->readyTime)
        {
            return;
        }

        // spawn a projectile from the firing point
        auto firingPoint = getFiringPoint(id, weaponIndex);
        auto targetVector = targetPosition - firingPoint;
        if (weapon->startSmoke)
        {
            scene->createLightSmoke(firingPoint);
        }
        scene->getSimulation().spawnLaser(unit.owner, *weapon, firingPoint, targetVector.normalized());

        if (weapon->soundStart)
        {
            scene->playUnitSound(id, *weapon->soundStart);
        }
        unit.cobEnvironment->createThread(getFireScriptName(weaponIndex));

        // we are reloading now
        weapon->readyTime = gameTime + deltaSecondsToTicks(weapon->reloadTime);
    }

    void UnitBehaviorService::applyUnitSteering(UnitId id)
    {
        updateUnitRotation(id);
        updateUnitSpeed(id);
    }

    void UnitBehaviorService::updateUnitRotation(UnitId id)
    {
        auto& unit = scene->getSimulation().getUnit(id);

        auto angleDelta = wrap(-Pif, Pif, unit.targetAngle - unit.rotation);

        auto turnRateThisFrame = unit.turnRate;
        if (std::abs(angleDelta) <= turnRateThisFrame)
        {
            unit.rotation = unit.targetAngle;
        }
        else
        {
            unit.rotation = wrap(-Pif, Pif, unit.rotation + (turnRateThisFrame * (angleDelta > 0.0f ? 1.0f : -1.0f)));
        }
    }

    void UnitBehaviorService::updateUnitSpeed(UnitId id)
    {
        auto& unit = scene->getSimulation().getUnit(id);

        if (unit.targetSpeed > unit.currentSpeed)
        {
            // accelerate to target speed
            if (unit.targetSpeed - unit.currentSpeed <= unit.acceleration)
            {
                unit.currentSpeed = unit.targetSpeed;
            }
            else
            {
                unit.currentSpeed += unit.acceleration;
            }
        }
        else
        {
            // brake to target speed
            if (unit.currentSpeed - unit.targetSpeed <= unit.brakeRate)
            {
                unit.currentSpeed = unit.targetSpeed;
            }
            else
            {
                unit.currentSpeed -= unit.brakeRate;
            }
        }

        auto effectiveMaxSpeed = unit.maxSpeed;
        if (unit.position.y < scene->getTerrain().getSeaLevel())
        {
            effectiveMaxSpeed /= 2.0f;
        }
        unit.currentSpeed = std::clamp(unit.currentSpeed, 0.0f, effectiveMaxSpeed);
    }

    void UnitBehaviorService::updateUnitPosition(UnitId unitId)
    {
        auto& unit = scene->getSimulation().getUnit(unitId);

        auto direction = Unit::toDirection(unit.rotation);

        unit.inCollision = false;

        if (unit.currentSpeed > 0.0f)
        {
            auto newPosition = unit.position + (direction * unit.currentSpeed);
            newPosition.y = scene->getTerrain().getHeightAt(newPosition.x, newPosition.z);

            if (!tryApplyMovementToPosition(unitId, newPosition))
            {
                unit.inCollision = true;

                // if we failed to move, try in each axis separately
                // to see if we can complete a "partial" movement
                const Vector3f maskX(0.0f, 1.0f, 1.0f);
                const Vector3f maskZ(1.0f, 1.0f, 0.0f);

                Vector3f newPos1;
                Vector3f newPos2;
                if (direction.x > direction.z)
                {
                    newPos1 = unit.position + (direction * maskZ * unit.currentSpeed);
                    newPos2 = unit.position + (direction * maskX * unit.currentSpeed);
                }
                else
                {
                    newPos1 = unit.position + (direction * maskX * unit.currentSpeed);
                    newPos2 = unit.position + (direction * maskZ * unit.currentSpeed);
                }
                newPos1.y = scene->getTerrain().getHeightAt(newPos1.x, newPos1.z);
                newPos2.y = scene->getTerrain().getHeightAt(newPos2.x, newPos2.z);

                if (!tryApplyMovementToPosition(unitId, newPos1))
                {
                    tryApplyMovementToPosition(unitId, newPos2);
                }
            }
        }
    }

    bool UnitBehaviorService::tryApplyMovementToPosition(UnitId id, const Vector3f& newPosition)
    {
        auto& sim = scene->getSimulation();
        auto& unit = sim.getUnit(id);

        // check for collision at the new position
        auto newFootprintRegion = scene->computeFootprintRegion(newPosition, unit.footprintX, unit.footprintZ);

        // Unlike for pathfinding, TA doesn't care about the unit's actual movement class for collision checks,
        // it only cares about the attributes defined directly on the unit.
        // Jam these into an ad-hoc movement class to pass into our walkability check.
        MovementClass mc;
        mc.minWaterDepth = unit.minWaterDepth;
        mc.maxWaterDepth = unit.maxWaterDepth;
        mc.maxSlope = unit.maxSlope;
        mc.maxWaterSlope = unit.maxWaterSlope;
        mc.footprintX = unit.footprintX;
        mc.footprintZ = unit.footprintZ;

        if (!isGridPointWalkable(sim.terrain, mc, newFootprintRegion.x, newFootprintRegion.y))
        {
            return false;
        }

        if (scene->isCollisionAt(newFootprintRegion, id))
        {
            return false;
        }

        // we passed all collision checks, update accordingly
        auto footprintRegion = scene->computeFootprintRegion(unit.position, unit.footprintX, unit.footprintZ);
        scene->moveUnitOccupiedArea(footprintRegion, newFootprintRegion, id);
        unit.position = newPosition;
        return true;
    }

    std::string UnitBehaviorService::getAimScriptName(unsigned int weaponIndex) const
    {
        switch (weaponIndex)
        {
            case 0:
                return "AimPrimary";
            case 1:
                return "AimSecondary";
            case 2:
                return "AimTertiary";
            default:
                throw std::logic_error("Invalid wepaon index: " + std::to_string(weaponIndex));
        }
    }

    std::string UnitBehaviorService::getAimFromScriptName(unsigned int weaponIndex) const
    {
        switch (weaponIndex)
        {
            case 0:
                return "AimFromPrimary";
            case 1:
                return "AimFromSecondary";
            case 2:
                return "AimFromTertiary";
            default:
                throw std::logic_error("Invalid weapon index: " + std::to_string(weaponIndex));
        }
    }

    std::string UnitBehaviorService::getFireScriptName(unsigned int weaponIndex) const
    {
        switch (weaponIndex)
        {
            case 0:
                return "FirePrimary";
            case 1:
                return "FireSecondary";
            case 2:
                return "FireTertiary";
            default:
                throw std::logic_error("Invalid weapon index: " + std::to_string(weaponIndex));
        }
    }

    std::string UnitBehaviorService::getQueryScriptName(unsigned int weaponIndex) const
    {
        switch (weaponIndex)
        {
            case 0:
                return "QueryPrimary";
            case 1:
                return "QuerySecondary";
            case 2:
                return "QueryTertiary";
            default:
                throw std::logic_error("Invalid wepaon index: " + std::to_string(weaponIndex));
        }
    }

    std::optional<int> UnitBehaviorService::runCobQuery(UnitId id, const std::string& name)
    {
        auto& unit = scene->getSimulation().getUnit(id);
        auto thread = unit.cobEnvironment->createNonScheduledThread(name, {0});
        if (!thread)
        {
            return std::nullopt;
        }
        CobExecutionContext context(&scene->getSimulation(), unit.cobEnvironment.get(), &*thread, id);
        auto status = context.execute();
        if (boost::get<CobEnvironment::FinishedStatus>(&status) == nullptr)
        {
            throw std::runtime_error("Synchronous cob query thread blocked before completion");
        }

        auto result = thread->returnLocals[0];
        return result;
    }

    Vector3f UnitBehaviorService::getAimingPoint(UnitId id, unsigned int weaponIndex)
    {
        auto scriptName = getAimFromScriptName(weaponIndex);
        auto pieceId = runCobQuery(id, scriptName);
        if (!pieceId)
        {
            return getFiringPoint(id, weaponIndex);
        }

        return getPiecePosition(id, *pieceId);
    }

    Vector3f UnitBehaviorService::getFiringPoint(UnitId id, unsigned int weaponIndex)
    {

        auto scriptName = getQueryScriptName(weaponIndex);
        auto pieceId = runCobQuery(id, scriptName);
        if (!pieceId)
        {
            return scene->getSimulation().getUnit(id).position;
        }

        return getPiecePosition(id, *pieceId);
    }

    Vector3f UnitBehaviorService::getSweetSpot(UnitId id)
    {
        auto pieceId = runCobQuery(id, "SweetSpot");
        if (!pieceId)
        {
            return scene->getSimulation().getUnit(id).position;
        }

        return getPiecePosition(id, *pieceId);
    }

    std::optional<Vector3f> UnitBehaviorService::tryGetSweetSpot(UnitId id)
    {
        if (!scene->getSimulation().unitExists(id))
        {
            return std::nullopt;
        }

        return getSweetSpot(id);
    }

    bool UnitBehaviorService::handleAttackOrder(UnitId unitId, const AttackOrder& attackOrder)
    {
        auto& unit = scene->getSimulation().getUnit(unitId);

        if (!unit.weapons[0])
        {
            return true;
        }
        else
        {
            GetTargetPositionVisitor targetPositionVisitor(this);
            auto targetPosition = boost::apply_visitor(targetPositionVisitor, attackOrder.target);
            if (!targetPosition)
            {
                // target has gone away, throw away this order
                return true;
            }

            auto maxRangeSquared = unit.weapons[0]->maxRange * unit.weapons[0]->maxRange;
            if (auto idleState = boost::get<IdleState>(&unit.behaviourState); idleState != nullptr)
            {
                // if we're out of range, drive into range
                if (unit.position.distanceSquared(*targetPosition) > maxRangeSquared)
                {
                    // request a path to follow
                    scene->getSimulation().requestPath(unitId);
                    auto destination = boost::apply_visitor(AttackTargetToMovingStateGoalVisitor(scene), attackOrder.target);
                    unit.behaviourState = MovingState{destination, std::nullopt, true};
                }
                else
                {
                    // we're in range, aim weapons
                    for (unsigned int i = 0; i < 2; ++i)
                    {
                        unit.setWeaponTarget(i, *targetPosition);
                    }
                }
            }
            else if (auto movingState = boost::get<MovingState>(&unit.behaviourState); movingState != nullptr)
            {
                if (unit.position.distanceSquared(*targetPosition) <= maxRangeSquared)
                {
                    unit.behaviourState = IdleState();
                }
                else
                {
                    // TODO: consider requesting a new path if the target unit has moved significantly

                    // if we are colliding, request a new path
                    if (unit.inCollision && !movingState->pathRequested)
                    {
                        auto& sim = scene->getSimulation();

                        // only request a new path if we don't have one yet,
                        // or we've already had our current one for a bit
                        if (!movingState->path || (sim.gameTime - movingState->path->pathCreationTime) >= GameTimeDelta(60))
                        {
                            sim.requestPath(unitId);
                            movingState->pathRequested = true;
                        }
                    }

                    // if a path is available, attempt to follow it
                    auto& pathToFollow = movingState->path;
                    if (pathToFollow)
                    {
                        if (followPath(unit, *pathToFollow))
                        {
                            // we finished following the path,
                            // go back to idle
                            unit.behaviourState = IdleState();
                        }
                    }
                }
            }
        }

        return false;
    }

    Vector3f UnitBehaviorService::getPiecePosition(UnitId id, unsigned int pieceId)
    {
        auto& unit = scene->getSimulation().getUnit(id);

        const auto& pieceName = unit.cobEnvironment->_script->pieces.at(pieceId);
        auto pieceTransform = unit.mesh.getPieceTransform(pieceName);
        if (!pieceTransform)
        {
            throw std::logic_error("Failed to find piece offset");
        }

        return unit.getTransform() * (*pieceTransform) * Vector3f(0.0f, 0.0f, 0.0f);
    }
}
