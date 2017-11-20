#include "UnitDatabase.h"

namespace rwe
{

    const UnitFbi& UnitDatabase::getUnitInfo(const std::string& unitName) const
    {
        auto it = map.find(unitName);
        if (it == map.end())
        {
            throw std::runtime_error("No FBI data found for unit " + unitName);
        }

        return it->second;
    }

    void UnitDatabase::addUnitInfo(const std::string& unitName, const UnitFbi& info)
    {
        map.insert({unitName, info});
    }

    const CobScript& UnitDatabase::getUnitScript(const std::string& unitName) const
    {
        auto it = cobMap.find(unitName);
        if (it == cobMap.end())
        {
            throw std::runtime_error("No script data found for unit " + unitName);
        }

        return it->second;
    }

    void UnitDatabase::addUnitScript(const std::string& unitName, CobScript&& cob)
    {
        cobMap.insert({unitName, std::move(cob)});
    }

    const SoundClass& UnitDatabase::getSoundClass(const std::string& className) const
    {
        auto it = soundClassMap.find(className);
        if (it == soundClassMap.end())
        {
            throw std::runtime_error("No sound class found with name " + className);
        }

        return it->second;
    }

    void UnitDatabase::addSoundClass(const std::string& className, SoundClass&& soundClass)
    {
        soundClassMap.insert({className, std::move(soundClass)});
    }

    const AudioService::SoundHandle& UnitDatabase::getSoundHandle(const std::string sound) const
    {
        auto it = soundMap.find(sound);
        if (it == soundMap.end())
        {
            throw std::runtime_error("No sound class found with name " + sound);
        }

        return it->second;
    }

    void UnitDatabase::addSound(const std::string& soundName, const AudioService::SoundHandle& sound)
    {
        soundMap.insert({soundName, sound});
    }

    const MovementClass& UnitDatabase::getMovementClass(const std::string& className) const
    {
        auto it = movementClassMap.find(className);
        if (it == movementClassMap.end())
        {
            throw std::runtime_error("No movement class found with name " + className);
        }

        return it->second;
    }

    void UnitDatabase::addMovementClass(const std::string& className, MovementClass&& movementClass)
    {
        movementClassMap.insert({className, std::move(movementClass)});
    }
}