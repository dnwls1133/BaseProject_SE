#pragma once
#include "ModelCache.h"
#include <array>
#include <functional>
#include <map>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>

class WorldSimulation;
class RandomLevelMap;
class RpgStats;
struct LevelData;

namespace ContentData
{
class Row;
}

// Stable simulation identities outlive the visible actors that represent them.
class WorldSociety
{
  public:
    enum class Action
    {
        Rest,
        Travel,
        Trade,
        Aid,
        Patrol,
        Research,
        Raid,
        Flee,
        Combat,
        Escort
    };
    enum class QuestState
    {
        Proposed,
        Accepted,
        Progress,
        Held,
        Completed,
        Failed,
        Expired,
        Replaced
    };
    enum class QuestKind
    {
        Supply,
        Heal,
        Escort,
        Investigate,
        Negotiate,
        Defend
    };

    struct Memory
    {
        int region = -1, source = -1;
        float food = 0, infection = 0, confidence = 1;
        std::uint64_t learned = 0;
    };

    struct Npc
    {
        std::string id, name, role, goal;
        int faction = -1, home = 0, region = 0;
        ModelId model = ModelId::Pilgrim;
        // Courage, greed, compassion, suspicion, zeal, curiosity, individuality.
        std::array<float, 7> traits{};
        // Trust, liking, respect, fear, dependence, ideological affinity.
        std::array<float, 6> playerRelation{};
        float x = 0, y = 0, health = 100, food = 5, infection = 0, yaw = 0;
        float attack = 8, speed = 2, phase = 0, movement = 0, facing = 1;
        float windup = 0, cooldown = 0, flash = 0, collapse = 0, farTime = 0;
        Action action = Action::Rest;
        int target = -1, destination = 0;
        std::uint64_t nextDecision = 0, nextMessage = 0, nextFaction = 0;
        std::vector<Memory> memories;
        std::vector<Point> path;
        std::size_t waypoint = 0;
        int pathCell = -1;
        float pathRetry = 0;
        int pathFailures = 0;
        std::string speech;
        float speechTime = 0;
        bool playerThreat = false;
        int wildTarget = -1;
        float wildDecision = 0;

        bool Alive() const
        {
            return health > 0;
        }
    };

    struct Faction
    {
        std::string id, name, motto;
        int home = 0, leader = -1, parent = -1;
        Color color = Color(.7f, .7f, .7f);
        ModelId emblem = ModelId::Pilgrim;
        float ideology = 0, supplies = 5, support = .6f;
        bool active = true;
    };

    struct Frontier
    {
        int first = -1, second = -1, region = -1;
        Point firstRally{0, 0}, secondRally{0, 0};
    };

    struct Campaign
    {
        int attacker = -1, defender = -1, region = -1;
        bool active = false;
        std::uint64_t until = 0;
        int stage = 0;
        std::uint64_t stageSince = 0;
        Point attackRally{0, 0}, defendRally{0, 0};
    };

    struct Region
    {
        std::string id, name;
        float x = 0, y = 0;
        int owner = -1, population = 0;
        float security = .5f, blood = .5f, hive = .2f, fear = .2f, unrest = .2f;
        float hospital = 1, storehouse = 1, wall = 1;
    };

    struct Relation
    {
        int from = -1, to = -1;
        std::array<float, 6> values{};
    };

    struct Message
    {
        std::uint64_t id = 0, cause = 0, due = 0, expires = 0;
        int from = -1, to = -1, region = 0, kind = 0;
        float food = 0, infection = 0, confidence = 1;
    };

    struct Quest
    {
        std::uint64_t id = 0, cause = 0, deadline = 0;
        int issuer = -1, target = -1, region = 0, progress = 0;
        QuestKind kind = QuestKind::Supply;
        QuestState state = QuestState::Proposed;
    };

    struct Change
    {
        std::uint64_t id = 0, cause = 0, minute = 0;
        std::string text;
    };

    struct StoryProfile
    {
        std::string past, motive, boundary, belief, unknown;
        std::array<float, 10> weights{};
    };

    struct StoryFact
    {
        std::string id, text, correction, observation;
        int region = 0;
        float threshold = 0, restraint = 0;
        std::array<float, 10> influence{};
    };

    struct Belief
    {
        int fact = -1, origin = -1, subject = -1, claim = 1;
        float confidence = 0;
        std::uint64_t learned = 0;
    };

    struct StoryLetter
    {
        std::uint64_t id = 0, due = 0, expires = 0;
        int from = -1, to = -1;
        Belief evidence;
    };

    struct StoryIncident
    {
        std::string id, title, kind, outcome;
        int region = 0, actor = -1, partner = -1, trigger = -1, proof = -1;
        int stage = 0, step = 0, patient = -1, supplier = -1;
        std::uint64_t lifetime = 720, deadline = 0, cause = 0;
        float cargo = 0;
    };

    struct Rules
    {
        int decisionPeriod = 6, factionPeriod = 60, messageBudget = 16;
        int messageLifetime = 180, questLifetime = 720, maxFactions = 32;
        float talkRange = 14, foodPerDay = 3, combatRange = 1.5f;
        float attackWindup = .8f, attackCooldown = 2, hostility = -45;
        std::array<float, 10> weights{{.15f, .2f, .6f, .8f, .4f, .5f, .3f, .9f, .5f, .8f}};
    };

    enum class LifeTask
    {
        None,
        Rest,
        Eat,
        Work,
        Collect,
        Deliver,
        Meet,
        Patrol,
        Research
    };

    struct LifePlace
    {
        std::string id, name, kind, model;
        int region = 0, capacity = 2;
        Point position{0, 0};
        float medicine = 0;
        std::vector<Point> slots;
    };

    struct LifeState
    {
        LifeTask task = LifeTask::None;
        int home = -1, work = -1, place = -1, partner = -1, delivery = -1, slot = -1;
        std::uint64_t agreement = 0, until = 0, nextSocial = 0, cause = 0, nextTrade = 0;
        float fatigue = 0, social = 0, progress = 0, cargo = 0, medicine = 0, satiation = 60;
        float lastDistance = 1e6f, stalled = 0;
        int helped = 0, broken = 0;
        std::string reason, lastMemory;
    };

    struct Agreement
    {
        std::uint64_t id = 0, due = 0, deadline = 0;
        int requester = -1, supplier = -1, kind = 0, stage = 0, payer = -1, payee = -1;
        float cargo = 0, payment = 0;
    };

    const std::vector<LifePlace>& LifePlaces() const
    {
        return places;
    }

    int LifeActivity(int npc) const;
    std::string LifeLabel(int npc) const;
    std::vector<std::string> LifeJournal() const;

    void Load(const std::string& root, const std::string& file, const LevelData& level,
              const ModelCache& models, std::uint32_t seed, std::uint64_t minute);
    void Update(float dt, WorldSimulation& world, const RandomLevelMap& map, Point player);
    void TickActor(std::size_t index, float dt, WorldSimulation& world, const RandomLevelMap& map,
                   Point player, const std::function<bool(Npc&, float)>& wildlife);
    void ReconcilePositions(const RandomLevelMap& map);
    void ResetWildlifeTargets();
    void ObserveBroodConflict(Point point, const RandomLevelMap& map, Point player);
    float StoryCargo(int npc) const;

    std::string Interact(int npc, int option, WorldSimulation& world, RpgStats& stats);
    std::vector<std::string> Dialogue(int npc) const;
    bool BeginConversation(int npc);
    void EndConversation();
    bool IsConversationActive(int npc) const;
    std::vector<std::pair<int, std::string>> ConversationChoices(int npc) const;
    std::string ConversationTerms(int npc, int option) const;
    std::vector<std::string> Journal(int tab) const;
    void PlayerAttack(int npc, int damage, WorldSimulation& world);
    void MonsterKilled(const std::string& region, bool playerCredit = true);
    float WildlifeAffinity(int faction, const std::string& species) const;
    void WildlifeDamage(const std::string& source, int target, float amount,
                        WorldSimulation& world);
    void WildlifeRecord(const std::string& source, const std::string& target,
                        const std::string& reason);
    void WildlifeMove(Npc& npc, Point goal, float dt, const RandomLevelMap& map);

    int TakePlayerDamage()
    {
        int damage = playerDamage;
        playerDamage = 0;
        return damage;
    }

    std::uint64_t Minute() const
    {
        return minute;
    }

    void ToggleQuestHold();
    void Write(std::ostream& stream) const;
    void BeginRestore();
    bool ReadRow(const std::string& command, ContentData::Row& row);
    void FinishRestore(const RandomLevelMap& map);
    int NpcIndex(const std::string& id) const;
    int FactionIndex(const std::string& id) const;
    int RegionIndex(const std::string& id) const;

    const std::vector<Npc>& Npcs() const
    {
        return npcs;
    }

    const std::vector<Faction>& Factions() const
    {
        return factions;
    }

    const std::vector<Region>& Regions() const
    {
        return regions;
    }

    const std::deque<Change>& History() const
    {
        return history;
    }

    std::uint32_t Signature() const
    {
        return signature;
    }

    float AttackWindupDuration() const
    {
        return rules.attackWindup;
    }

    float AttackCooldownDuration() const
    {
        return rules.attackCooldown;
    }

    float CombatRange() const
    {
        return rules.combatRange;
    }

    int PlayerFaction() const
    {
        return playerFaction;
    }

    static const char* ActionName(Action action);
    static const char* RoleName(const std::string& role);
    static const char* QuestName(QuestKind kind);
    static const char* QuestStatus(QuestState state);

  private:
    std::vector<Npc> npcs;
    std::vector<Faction> factions;
    std::vector<Region> regions;
    std::vector<Relation> relations;
    std::vector<std::vector<float>> diplomacy;
    std::deque<Message> messages;
    std::vector<Quest> quests;
    std::deque<Change> history;
    std::unordered_map<int, std::vector<int>> neighborhood;
    Rules rules;
    std::vector<LifePlace> places;
    std::vector<LifeState> lives;
    std::vector<Agreement> agreements;
    std::vector<std::pair<int, int>> tradeRoutes;
    std::map<std::string, std::vector<std::string>> lifeLines;
    float lifeWorkSeconds = 10, lifeRestSeconds = 16, lifeFoodPerDay = 9;
    int lifeSocialMinutes = 90;

    bool HasLife() const
    {
        return !places.empty();
    }

    void LoadLife(const std::string& root, const std::string& file);
    void LifeTick(std::uint64_t elapsed, WorldSimulation& world, const RandomLevelMap& map);
    void ChooseLife(int npc, WorldSimulation& world, const RandomLevelMap& map);
    bool TickLife(int npc, float dt, WorldSimulation& world, const RandomLevelMap& map);
    void OfferLife(int requester, int supplier, int kind);
    void EndAgreement(Agreement& agreement, bool success, const std::string& reason);
    void SayLife(int npc, const std::string& key, int other = -1, int place = -1,
                 const std::string& detail = "");
    int LifeSite(int npc, const std::string& kind) const;
    int LifeSlot(int npc, int place) const;
    void LifeEvent(int npc, const std::string& kind, int region, WorldSimulation& world,
                   int other = -1);
    void WriteLife(std::ostream& out) const;
    bool ReadLife(const std::string& command, ContentData::Row& row);
    void FinishLifeRestore();
    std::vector<StoryProfile> storyProfiles;
    std::vector<StoryFact> storyFacts;
    std::vector<std::vector<Belief>> storyBeliefs;
    std::vector<Belief> playerBeliefs;
    std::deque<StoryLetter> storyLetters;
    std::vector<StoryIncident> storyIncidents;
    std::uint64_t nextStoryTick = 0, nextStoryShare = 0;
    void LoadStory(const std::string& root, const std::string& file);
    int StoryFactIndex(const std::string& id) const;
    const Belief* KnownStory(int npc, int fact) const;
    bool LearnStory(int npc, const Belief& evidence, int sender);
    void SendStory(int from, int to, int fact);
    void StoryTick(WorldSimulation& world, const RandomLevelMap& map, Point player);
    void AdvanceStory(StoryIncident& incident, WorldSimulation& world, const RandomLevelMap& map);
    void FinishStory(StoryIncident& incident, int stage, const std::string& reason,
                     WorldSimulation& world);
    bool StoryOrder(int npc);
    float PersonalRegard(int from, int to) const;
    float StoryRestraint(int npc) const;
    void StoryInfluence(int npc, std::array<float, 10>& score) const;
    std::string StoryNews(int npc);
    std::string GiveStoryEvidence(int npc);
    std::vector<std::string> StoryJournal(int tab) const;
    void WriteStory(std::ostream& out) const;
    bool ReadStory(const std::string& command, ContentData::Row& row);
    void FinishStoryRestore();

    std::map<std::pair<std::string, std::string>, float> ecology;
    std::vector<Campaign> campaigns;
    std::vector<Frontier> frontiers;
    float warThreshold = -60, warReserve = 2, warCost = .35f;
    std::uint64_t truceMinutes = 1440;
    void CampaignTick(WorldSimulation& world);
    bool CampaignOrder(int npc);
    bool CampaignGoal(int npc, Point& goal) const;

    std::array<float, 6> karma{};
    int playerFaction = -1, playerFood = 6;
    std::uint32_t randomState = 1, signature = 0;
    std::uint64_t minute = 0, serial = 1, lastFactionTick = 0, nextQuestTick = 0;
    int pathBudget = 0, pathNodeBudget = 0, playerDamage = 0;
    std::vector<int> pathParents, pathCosts;
    std::vector<std::uint32_t> pathStamps;
    std::uint32_t pathStamp = 0;
    std::string source;
    float factionTimer = 0;
    int conversationNpc = -1;
    float conversationTime = 0;
    std::vector<bool> restoredNpcs, restoredRegions;
    bool restoredState = false;
    std::set<std::string> restoreSeen;
    std::uint32_t Random();
    std::uint64_t Record(const std::string& kind, const std::string& actor,
                         const std::string& target, const std::string& before,
                         const std::string& after, const std::string& reason,
                         std::uint64_t cause = 0);
    std::vector<int> Nearby(const Npc& npc) const;
    float Trust(int from, int to) const;
    void Relate(int from, int to, float trust, float fear, const std::string& reason,
                std::uint64_t cause = 0);
    void SetFaction(int npc, int faction, const std::string& reason);
    void Observe(int npc, WorldSimulation& world);
    void Send(int from, int to, int kind, int region, WorldSimulation& world,
              std::uint64_t cause = 0);
    void Deliver(WorldSimulation& world);
    void Decide(int npc, WorldSimulation& world, const RandomLevelMap& map);
    void FactionTick(WorldSimulation& world);
    void QuestTick(WorldSimulation& world, Point player);
    void ChangeQuest(Quest& quest, QuestState state, const std::string& reason);
    void Damage(int attacker, int target, float amount, WorldSimulation& world,
                const std::string& wildlife = "");
    void MoveTo(Npc& npc, Point target, float dt, const RandomLevelMap& map);
};
