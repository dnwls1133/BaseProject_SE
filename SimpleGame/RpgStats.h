#pragma once
#include "LevelData.h"

// All derived values have one source of truth for combat and the stat UI.
class RpgStats
{
  public:
    RpgRules rules;
    int level = 1;
    int experience = 0;
    int totalExperience = 0;
    int points = 0;
    int strength = 0;
    int vitality = 0;
    int resilience = 0;
    int allocated = 0;
    int health = 100;
    int potions = 3;
    int fragments = 0;

    int RequiredExperience() const
    {
        return rules.experienceBase + (level - 1) * rules.experienceStep;
    }

    int MaximumHealth() const
    {
        return rules.healthBase + (level - 1) * rules.healthPerLevel +
               vitality * rules.healthPerVitality;
    }

    int Attack() const
    {
        return rules.attackBase + (level - 1) * rules.attackPerLevel +
               strength * rules.attackPerStrength;
    }

    int Defense() const
    {
        return rules.defenseBase + resilience * rules.defensePerResilience;
    }

    int AddExperience(int amount)
    {
        if (amount <= 0 || level >= rules.maximumLevel)
        {
            return 0;
        }

        int previousMaximum = MaximumHealth();
        experience += amount;
        totalExperience += amount;
        int gainedLevels = 0;

        while (level < rules.maximumLevel && experience >= RequiredExperience())
        {
            experience -= RequiredExperience();
            ++level;
            ++gainedLevels;
            points += rules.pointsPerLevel;
        }

        if (level == rules.maximumLevel)
        {
            experience = 0;
        }

        health += MaximumHealth() - previousMaximum;
        return gainedLevels;
    }

    bool Allocate(int attribute)
    {
        if (points == 0 || attribute < 1 || attribute > 3)
        {
            return false;
        }

        if (attribute == 1)
        {
            ++strength;
        }
        else if (attribute == 2)
        {
            ++vitality;
            health += rules.healthPerVitality;
        }
        else
        {
            ++resilience;
        }

        --points;
        ++allocated;
        return true;
    }

    bool Heal()
    {
        if (potions <= 0 || health <= 0 || health >= MaximumHealth())
        {
            return false;
        }

        --potions;
        health += rules.healing;
        if (health > MaximumHealth())
        {
            health = MaximumHealth();
        }
        return true;
    }
};
