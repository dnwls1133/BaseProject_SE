#pragma once

// All derived values have one source of truth for combat and the stat UI.
class RpgStats
{
  public:
    static constexpr int MaximumLevel = 20;
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
        return 60 + (level - 1) * 35;
    }

    int MaximumHealth() const
    {
        return 100 + (level - 1) * 5 + vitality * 15;
    }

    int Attack() const
    {
        return 18 + (level - 1) * 2 + strength * 4;
    }

    int Defense() const
    {
        return 2 + resilience * 2;
    }

    int AddExperience(int amount)
    {
        if (amount <= 0 || level >= MaximumLevel)
        {
            return 0;
        }

        int previousMaximum = MaximumHealth();
        experience += amount;
        totalExperience += amount;
        int gainedLevels = 0;

        while (level < MaximumLevel && experience >= RequiredExperience())
        {
            experience -= RequiredExperience();
            ++level;
            ++gainedLevels;
            points += 2;
        }

        if (level == MaximumLevel)
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
            health += 15;
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
        health += 40;
        if (health > MaximumHealth())
        {
            health = MaximumHealth();
        }
        return true;
    }
};
