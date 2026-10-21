#include "rmg/map_evaluation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace rmg {
namespace {

constexpr int kScoreMaximum = 100;
constexpr int kResourceContestMargin = 4;
int clampScore(int value)
{
    return std::clamp(value, 0, kScoreMaximum);
}

int scaledScore(int value, int floor, int target)
{
    if (value <= floor) {
        return 0;
    }
    if (value >= target || target <= floor) {
        return kScoreMaximum;
    }
    return clampScore((value - floor) * kScoreMaximum / (target - floor));
}

int weightedScore(std::initializer_list<std::pair<int, int>> values)
{
    int weighted = 0;
    int totalWeight = 0;
    for (const auto [score, weight] : values) {
        if (weight <= 0) {
            continue;
        }
        weighted += clampScore(score) * weight;
        totalWeight += weight;
    }
    return totalWeight > 0 ? weighted / totalWeight : 0;
}

int minimumAwareAggregate(const std::vector<std::pair<int, int>>& values)
{
    int weighted = 0;
    int totalWeight = 0;
    int minimum = kScoreMaximum;
    for (const auto [score, weight] : values) {
        if (weight <= 0) {
            continue;
        }
        const int clamped = clampScore(score);
        weighted += clamped * weight;
        totalWeight += weight;
        minimum = std::min(minimum, clamped);
    }
    if (totalWeight == 0) {
        return 0;
    }
    const int average = weighted / totalWeight;
    return (average * 7 + minimum * 3) / 10;
}

int parityScore(const std::vector<int>& values)
{
    if (values.size() < 2) {
        return kScoreMaximum;
    }

    const auto [minimumIt, maximumIt] = std::minmax_element(values.begin(), values.end());
    const int minimum = *minimumIt;
    const int maximum = *maximumIt;
    const std::int64_t sum = std::accumulate(values.begin(), values.end(), std::int64_t{0});
    if (maximum <= 0 || sum <= 0) {
        return kScoreMaximum;
    }

    const int worstRatio = static_cast<int>(static_cast<std::int64_t>(minimum) * kScoreMaximum / maximum);
    std::int64_t scaledDeviation = 0;
    const std::int64_t count = static_cast<std::int64_t>(values.size());
    for (const int value : values) {
        scaledDeviation += std::llabs(static_cast<std::int64_t>(value) * count - sum);
    }
    const int deviationPercent = static_cast<int>(
        std::min<std::int64_t>(kScoreMaximum, scaledDeviation * kScoreMaximum / (count * sum)));
    const int distribution = kScoreMaximum - deviationPercent;
    return clampScore((worstRatio * 6 + distribution * 4) / 10);
}

int distanceParityScore(const std::vector<int>& distances)
{
    const int missing = static_cast<int>(std::count(distances.begin(), distances.end(), -1));
    if (missing == static_cast<int>(distances.size())) {
        return kScoreMaximum;
    }
    if (missing > 0) {
        return 0;
    }
    return parityScore(distances);
}

int resourceValue(const Cell& cell)
{
    if (cell.overlay != Overlay::Ore && cell.overlay != Overlay::Gems) {
        return 0;
    }
    return std::max(1, cell.overlayValue);
}

std::size_t cellIndex(const MapGrid& map, Point point)
{
    return static_cast<std::size_t>(point.y * map.width() + point.x);
}

std::vector<int> buildDistanceMap(const MapGrid& map, Point start)
{
    std::vector<int> distances(static_cast<std::size_t>(map.width() * map.height()), -1);
    if (!map.inBounds(start) || !isPassable(map.at(start))) {
        return distances;
    }

    std::queue<Point> queue;
    distances[cellIndex(map, start)] = 0;
    queue.push(start);
    while (!queue.empty()) {
        const Point current = queue.front();
        queue.pop();
        const int currentDistance = distances[cellIndex(map, current)];
        for (const Point next : map.neighbors4(current)) {
            const std::size_t index = cellIndex(map, next);
            if (distances[index] >= 0 || !isPassable(map.at(next))) {
                continue;
            }
            distances[index] = currentDistance + 1;
            queue.push(next);
        }
    }
    return distances;
}

struct ComponentSummary {
    int passableCells = 0;
    int buildableCells = 0;
    int startingPlayers = 0;
};

std::vector<ComponentSummary> labelComponents(const MapGrid& map, std::vector<int>& componentByCell)
{
    componentByCell.assign(static_cast<std::size_t>(map.width() * map.height()), -1);
    std::vector<ComponentSummary> summaries;
    for (const Cell& candidate : map.cells()) {
        const Point start{candidate.x, candidate.y};
        const std::size_t startIndex = cellIndex(map, start);
        if (componentByCell[startIndex] >= 0 || !isPassable(candidate)) {
            continue;
        }

        const int componentId = static_cast<int>(summaries.size());
        summaries.push_back({});
        std::queue<Point> queue;
        componentByCell[startIndex] = componentId;
        queue.push(start);
        while (!queue.empty()) {
            const Point current = queue.front();
            queue.pop();
            ComponentSummary& summary = summaries[static_cast<std::size_t>(componentId)];
            ++summary.passableCells;
            if (isBuildable(map.at(current))) {
                ++summary.buildableCells;
            }
            for (const Point next : map.neighbors4(current)) {
                const std::size_t index = cellIndex(map, next);
                if (componentByCell[index] >= 0 || !isPassable(map.at(next))) {
                    continue;
                }
                componentByCell[index] = componentId;
                queue.push(next);
            }
        }
    }
    return summaries;
}

std::string topologyName(const std::vector<ComponentSummary>& components, const std::vector<PlayerMapEvaluation>& players)
{
    if (players.empty()) {
        return "unknown";
    }
    std::unordered_set<int> startComponents;
    bool sharedComponent = false;
    bool isolatedComponent = false;
    for (const auto& player : players) {
        startComponents.insert(player.componentId);
    }
    if (startComponents.size() == 1) {
        return "single_land";
    }
    if (startComponents.size() == players.size()) {
        return "islands";
    }
    for (const int componentId : startComponents) {
        if (componentId < 0 || componentId >= static_cast<int>(components.size())) {
            continue;
        }
        const int starts = components[static_cast<std::size_t>(componentId)].startingPlayers;
        sharedComponent = sharedComponent || starts > 1;
        isolatedComponent = isolatedComponent || starts == 1;
    }
    return sharedComponent && isolatedComponent ? "mixed" : "split_land";
}

void addObservation(
    MapEvaluationReport& report,
    std::string code,
    std::string impact,
    std::vector<int> players = {})
{
    report.observations.push_back({std::move(code), std::move(impact), std::move(players)});
}

nlohmann::json domainJson(const EvaluationDomainScore& domain)
{
    return {
        {"availability", domain.availabilityApplicable ? nlohmann::json(domain.availability) : nlohmann::json(nullptr)},
        {"parity", domain.parityApplicable ? nlohmann::json(domain.parity) : nlohmann::json(nullptr)},
        {"confidence", domain.confidence},
    };
}

} // namespace

MapEvaluationReport evaluateMap(
    const MapGrid& map,
    std::span<const Point> starts,
    const MapEvaluationContext& context)
{
    MapEvaluationReport report;
    report.seed = context.seed;
    report.width = map.width();
    report.height = map.height();
    report.requestedPlayers = context.requestedPlayers;
    report.evaluatedPlayers = static_cast<int>(starts.size());
    report.players.resize(starts.size());

    std::vector<int> componentByCell;
    std::vector<ComponentSummary> components = labelComponents(map, componentByCell);
    const int totalCells = std::max(1, map.width() * map.height());
    const int totalPassableCells = std::accumulate(
        components.begin(), components.end(), 0, [](int total, const ComponentSummary& component) {
            return total + component.passableCells;
        });
    report.landFragmentation = static_cast<int>(components.size());
    report.mapUtilization = totalPassableCells * kScoreMaximum / totalCells;

    std::vector<std::vector<int>> distanceMaps;
    distanceMaps.reserve(starts.size());
    for (const Point start : starts) {
        distanceMaps.push_back(buildDistanceMap(map, start));
    }

    for (std::size_t i = 0; i < starts.size(); ++i) {
        PlayerMapEvaluation& player = report.players[i];
        player.player = static_cast<int>(i) + 1;
        player.start = starts[i];
        player.distanceToEdge = map.inBounds(starts[i])
            ? std::min({starts[i].x, starts[i].y, map.width() - 1 - starts[i].x, map.height() - 1 - starts[i].y})
            : 0;
        if (map.inBounds(starts[i])) {
            player.componentId = componentByCell[cellIndex(map, starts[i])];
        }
        if (player.componentId >= 0 && player.componentId < static_cast<int>(components.size())) {
            ComponentSummary& component = components[static_cast<std::size_t>(player.componentId)];
            ++component.startingPlayers;
            player.componentPassableCells = component.passableCells;
            player.componentBuildableCells = component.buildableCells;
        }

        int resourceDistanceSum = 0;
        int resourceValueSum = 0;
        for (const Cell& cell : map.cells()) {
            const Point point{cell.x, cell.y};
            const int pathDistance = distanceMaps[i][cellIndex(map, point)];
            if (pathDistance >= 0 && pathDistance <= 12 && isBuildable(cell)) {
                ++player.connectedBuildableCells;
            }
            if (manhattan(point, starts[i]) <= 7 && isBuildable(cell)) {
                ++player.localBuildableCells;
            }

            const int value = resourceValue(cell);
            if (value <= 0 || pathDistance < 0) {
                continue;
            }
            if (player.nearestResourceDistance < 0 || pathDistance < player.nearestResourceDistance) {
                player.nearestResourceDistance = pathDistance;
            }
            if (pathDistance <= 12) {
                player.resourceValue12 += value;
            }
            if (pathDistance <= 24) {
                player.resourceValue24 += value;
            }
            if (pathDistance <= 34) {
                player.resourceValue34 += value;
                resourceDistanceSum += value * pathDistance;
                resourceValueSum += value;
            }
        }
        if (resourceValueSum > 0) {
            player.averageResourceDistance = (resourceDistanceSum + resourceValueSum / 2) / resourceValueSum;
        }
    }

    int connectedPairs = 0;
    const int totalPairs = starts.size() > 1
        ? static_cast<int>(starts.size() * (starts.size() - 1) / 2)
        : 0;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        for (std::size_t j = i + 1; j < starts.size(); ++j) {
            const int directDistance = manhattan(starts[i], starts[j]);
            const int groundDistance = map.inBounds(starts[j])
                ? distanceMaps[i][cellIndex(map, starts[j])]
                : -1;
            PlayerMapEvaluation& first = report.players[i];
            PlayerMapEvaluation& second = report.players[j];
            if (first.nearestStartDistance < 0 || directDistance < first.nearestStartDistance) {
                first.nearestStartDistance = directDistance;
            }
            if (second.nearestStartDistance < 0 || directDistance < second.nearestStartDistance) {
                second.nearestStartDistance = directDistance;
            }
            if (groundDistance >= 0) {
                ++connectedPairs;
                if (first.nearestGroundStartDistance < 0 || groundDistance < first.nearestGroundStartDistance) {
                    first.nearestGroundStartDistance = groundDistance;
                }
                if (second.nearestGroundStartDistance < 0 || groundDistance < second.nearestGroundStartDistance) {
                    second.nearestGroundStartDistance = groundDistance;
                }
            }
        }
    }
    report.groundContactRatio = totalPairs > 0 ? connectedPairs * kScoreMaximum / totalPairs : kScoreMaximum;

    int centralResourceValue = 0;
    int totalResourceValue = 0;
    for (const Cell& cell : map.cells()) {
        const Point point{cell.x, cell.y};
        const int value = resourceValue(cell);
        if (value <= 0) {
            continue;
        }
        totalResourceValue += value;
        if (point.x * 4 >= map.width()
            && point.x * 4 < map.width() * 3
            && point.y * 4 >= map.height()
            && point.y * 4 < map.height() * 3) {
            centralResourceValue += value;
        }

        int nearestDistance = std::numeric_limits<int>::max();
        int secondDistance = std::numeric_limits<int>::max();
        std::vector<std::size_t> nearestPlayers;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const int distance = distanceMaps[i][cellIndex(map, point)];
            if (distance < 0 || distance > 34) {
                continue;
            }
            if (distance < nearestDistance) {
                secondDistance = nearestDistance;
                nearestDistance = distance;
                nearestPlayers.assign(1, i);
            } else if (distance == nearestDistance) {
                nearestPlayers.push_back(i);
            } else if (distance < secondDistance) {
                secondDistance = distance;
            }
        }
        if (nearestPlayers.empty()) {
            continue;
        }
        const bool contested = nearestPlayers.size() > 1
            || (secondDistance != std::numeric_limits<int>::max()
                && secondDistance - nearestDistance <= kResourceContestMargin);
        if (contested) {
            for (std::size_t i = 0; i < starts.size(); ++i) {
                const int distance = distanceMaps[i][cellIndex(map, point)];
                if (distance >= 0 && distance <= 34 && distance - nearestDistance <= kResourceContestMargin) {
                    report.players[i].contestedResourceValue += value;
                }
            }
        } else {
            report.players[nearestPlayers.front()].exclusiveResourceValue += value;
        }
    }
    report.resourceCentralization = totalResourceValue > 0
        ? centralResourceValue * kScoreMaximum / totalResourceValue
        : 0;

    for (const Cell& cell : map.cells()) {
        if (!isPassable(cell) || starts.empty()) {
            continue;
        }
        const Point point{cell.x, cell.y};
        int nearestDistance = std::numeric_limits<int>::max();
        std::size_t nearestPlayer = 0;
        bool tied = false;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const int distance = distanceMaps[i][cellIndex(map, point)];
            if (distance < 0) {
                continue;
            }
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestPlayer = i;
                tied = false;
            } else if (distance == nearestDistance) {
                tied = true;
            }
        }
        if (nearestDistance == std::numeric_limits<int>::max()) {
            continue;
        }
        if (tied) {
            ++report.contestedTerritoryCells;
        } else {
            ++report.players[nearestPlayer].territoryCells;
        }
    }

    std::vector<int> localBuildable;
    std::vector<int> componentBuildable;
    std::vector<int> resourceValues;
    std::vector<int> resourceDistances;
    std::vector<int> nearestStarts;
    std::vector<int> componentSizes;
    std::vector<int> territories;
    std::vector<int> baseAvailability;
    std::vector<int> economyAvailability;
    for (const PlayerMapEvaluation& player : report.players) {
        localBuildable.push_back(player.localBuildableCells);
        componentBuildable.push_back(player.componentBuildableCells);
        resourceValues.push_back(player.resourceValue34);
        resourceDistances.push_back(player.averageResourceDistance);
        nearestStarts.push_back(player.nearestStartDistance);
        componentSizes.push_back(player.componentPassableCells);
        territories.push_back(player.territoryCells);

        const int componentTarget = std::max(160, totalPassableCells / std::max(2, context.requestedPlayers * 2));
        baseAvailability.push_back(weightedScore({
            {scaledScore(player.localBuildableCells, 36, 80), 65},
            {scaledScore(player.componentBuildableCells, 80, componentTarget), 35},
        }));
        if (context.tiberium > 0) {
            const int distanceScore = player.nearestResourceDistance < 0
                ? 0
                : scaledScore(35 - std::min(35, player.nearestResourceDistance), 0, 23);
            economyAvailability.push_back(weightedScore({
                {scaledScore(player.resourceValue34, 0, 60), 70},
                {distanceScore, 30},
            }));
        }
    }

    report.baseSpace = {
        true,
        baseAvailability.empty() ? 0 : *std::min_element(baseAvailability.begin(), baseAvailability.end()),
        true,
        weightedScore({{parityScore(localBuildable), 75}, {parityScore(componentBuildable), 25}}),
        95,
    };
    report.startingEconomy = {
        context.tiberium > 0,
        economyAvailability.empty() ? 0 : *std::min_element(economyAvailability.begin(), economyAvailability.end()),
        context.tiberium > 0,
        context.tiberium > 0
            ? weightedScore({{parityScore(resourceValues), 70}, {distanceParityScore(resourceDistances), 30}})
            : 0,
        90,
    };
    report.spawnGeometry = {
        false,
        0,
        starts.size() > 1,
        starts.size() > 1
            ? weightedScore({{distanceParityScore(nearestStarts), 65}, {parityScore(territories), 35}})
            : 0,
        95,
    };
    report.groundMobility = {
        false,
        0,
        !starts.empty(),
        !starts.empty()
            ? weightedScore({{parityScore(componentSizes), 60}, {parityScore(territories), 40}})
            : 0,
        80,
    };

    std::vector<std::pair<int, int>> playabilityScores{{report.baseSpace.availability, 60}};
    if (report.startingEconomy.availabilityApplicable) {
        playabilityScores.push_back({report.startingEconomy.availability, 40});
    }
    report.playability = minimumAwareAggregate(playabilityScores);

    std::vector<std::pair<int, int>> fairnessScores{{report.baseSpace.parity, 30}};
    if (report.startingEconomy.parityApplicable) {
        fairnessScores.push_back({report.startingEconomy.parity, 35});
    }
    if (report.spawnGeometry.parityApplicable) {
        fairnessScores.push_back({report.spawnGeometry.parity, 20});
    }
    if (report.groundMobility.parityApplicable) {
        fairnessScores.push_back({report.groundMobility.parity, 15});
    }
    report.fairness = minimumAwareAggregate(fairnessScores);
    report.confidence = weightedScore({
        {report.baseSpace.confidence, 30},
        {report.startingEconomy.confidence, context.tiberium > 0 ? 30 : 0},
        {report.spawnGeometry.confidence, 20},
        {report.groundMobility.confidence, 20},
    });

    report.topology = topologyName(components, report.players);
    const int averageNearestStart = nearestStarts.empty()
        ? 0
        : std::accumulate(nearestStarts.begin(), nearestStarts.end(), 0) / static_cast<int>(nearestStarts.size());
    const int normalizedRushDistance = averageNearestStart * kScoreMaximum / std::max(1, std::min(map.width(), map.height()));
    report.pace = normalizedRushDistance < 45 ? "fast" : (normalizedRushDistance < 75 ? "standard" : "long");

    if (context.tiberium <= 0) {
        addObservation(report, "economy.resource_free", "info");
    } else if (report.startingEconomy.availability == 0) {
        addObservation(report, "economy.starting_resources_missing", "high");
    } else if (report.startingEconomy.parity < 70) {
        addObservation(report, "economy.starting_resource_skew", "medium");
    }
    if (report.baseSpace.parity < 75) {
        addObservation(report, "base.buildable_space_skew", "medium");
    }
    if (report.groundContactRatio == 0 && starts.size() > 1) {
        addObservation(report, "mobility.no_ground_contact", "info");
    } else if (report.groundContactRatio < kScoreMaximum) {
        addObservation(report, "mobility.partial_ground_contact", "info");
    }
    if (report.resourceCentralization >= 65) {
        addObservation(report, "economy.centralized_resources", "info");
    }
    if (report.pace == "fast") {
        addObservation(report, "spawn.fast_contact", "info");
    }

    return report;
}

std::string serializeMapEvaluation(const MapEvaluationReport& report)
{
    nlohmann::json players = nlohmann::json::array();
    for (const PlayerMapEvaluation& player : report.players) {
        players.push_back({
            {"player", player.player},
            {"start", {{"x", player.start.x}, {"y", player.start.y}}},
            {"localBuildableCells", player.localBuildableCells},
            {"connectedBuildableCells", player.connectedBuildableCells},
            {"componentPassableCells", player.componentPassableCells},
            {"componentBuildableCells", player.componentBuildableCells},
            {"territoryCells", player.territoryCells},
            {"distanceToEdge", player.distanceToEdge},
            {"nearestStartDistance", player.nearestStartDistance},
            {"nearestGroundStartDistance", player.nearestGroundStartDistance},
            {"nearestResourceDistance", player.nearestResourceDistance},
            {"averageResourceDistance", player.averageResourceDistance},
            {"resourceValue12", player.resourceValue12},
            {"resourceValue24", player.resourceValue24},
            {"resourceValue34", player.resourceValue34},
            {"exclusiveResourceValue", player.exclusiveResourceValue},
            {"contestedResourceValue", player.contestedResourceValue},
            {"componentId", player.componentId},
        });
    }

    nlohmann::json observations = nlohmann::json::array();
    for (const MapEvaluationObservation& observation : report.observations) {
        observations.push_back({
            {"code", observation.code},
            {"impact", observation.impact},
            {"players", observation.players},
        });
    }

    const nlohmann::json root = {
        {"schemaVersion", report.schemaVersion},
        {"evaluatorVersion", report.evaluatorVersion},
        {"map", {
            {"seed", report.seed},
            {"width", report.width},
            {"height", report.height},
            {"requestedPlayers", report.requestedPlayers},
            {"evaluatedPlayers", report.evaluatedPlayers},
        }},
        {"headline", {
            {"playability", report.playability},
            {"fairness", report.fairness},
            {"confidence", report.confidence},
        }},
        {"domains", {
            {"baseSpace", domainJson(report.baseSpace)},
            {"startingEconomy", domainJson(report.startingEconomy)},
            {"spawnGeometry", domainJson(report.spawnGeometry)},
            {"groundMobility", domainJson(report.groundMobility)},
        }},
        {"descriptors", {
            {"topology", report.topology},
            {"pace", report.pace},
            {"groundContactRatio", report.groundContactRatio},
            {"landFragmentation", report.landFragmentation},
            {"resourceCentralization", report.resourceCentralization},
            {"mapUtilization", report.mapUtilization},
            {"contestedTerritoryCells", report.contestedTerritoryCells},
        }},
        {"perPlayer", std::move(players)},
        {"observations", std::move(observations)},
    };
    return root.dump(2) + '\n';
}

void writeMapEvaluationFile(const std::filesystem::path& path, const MapEvaluationReport& report)
{
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const std::string text = serializeMapEvaluation(report);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to open map evaluation output: " + path.string());
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        throw std::runtime_error("failed to write map evaluation output: " + path.string());
    }
}

void verifyMapEvaluation()
{
    constexpr int width = 80;
    constexpr int height = 24;
    const std::array<Point, 2> starts{{{10, 12}, {69, 12}}};

    MapGrid symmetric(width, height);
    symmetric.fill(Terrain::Land);
    for (const Point start : starts) {
        symmetric.at(start).overlay = Overlay::Start;
    }
    symmetric.at({14, 12}).overlay = Overlay::Ore;
    symmetric.at({14, 12}).overlayValue = 60;
    symmetric.at({65, 12}).overlay = Overlay::Ore;
    symmetric.at({65, 12}).overlayValue = 60;

    const MapEvaluationContext resourceContext{42, 2, 50};
    const MapEvaluationReport symmetricReport = evaluateMap(symmetric, starts, resourceContext);
    if (symmetricReport.fairness < 95 || symmetricReport.playability < 80) {
        throw std::runtime_error("map evaluation self-test rejected a symmetric playable map");
    }
    if (serializeMapEvaluation(symmetricReport)
        != serializeMapEvaluation(evaluateMap(symmetric, starts, resourceContext))) {
        throw std::runtime_error("map evaluation self-test is not deterministic");
    }

    MapGrid skewed = symmetric;
    skewed.at({65, 12}).overlay = Overlay::None;
    skewed.at({65, 12}).overlayValue = 0;
    const MapEvaluationReport skewedReport = evaluateMap(skewed, starts, resourceContext);
    if (skewedReport.startingEconomy.parity >= symmetricReport.startingEconomy.parity
        || skewedReport.fairness >= symmetricReport.fairness) {
        throw std::runtime_error("map evaluation self-test did not detect starting resource skew");
    }

    const MapEvaluationReport resourceFreeReport = evaluateMap(symmetric, starts, {42, 2, 0});
    if (resourceFreeReport.startingEconomy.availabilityApplicable
        || resourceFreeReport.startingEconomy.parityApplicable) {
        throw std::runtime_error("map evaluation self-test scored disabled resources");
    }
    MapGrid islands(40, 20);
    islands.fill(Terrain::Water);
    for (Cell& cell : islands.cells()) {
        if ((cell.x >= 2 && cell.x <= 15) || (cell.x >= 24 && cell.x <= 37)) {
            cell.terrain = Terrain::Land;
        }
    }
    const std::array<Point, 2> islandStarts{{{8, 10}, {31, 10}}};
    for (const Point start : islandStarts) {
        islands.at(start).overlay = Overlay::Start;
    }
    const MapEvaluationReport islandReport = evaluateMap(islands, islandStarts, {7, 2, 0});
    if (islandReport.topology != "islands" || islandReport.groundContactRatio != 0) {
        throw std::runtime_error("map evaluation self-test did not describe isolated starts");
    }
}

} // namespace rmg
