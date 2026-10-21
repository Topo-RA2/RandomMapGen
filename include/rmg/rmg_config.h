#pragma once

#include <string>
#include <vector>

namespace rmg {

struct RmgConfig {
    int minimumTiberium = 900;
    int maximumTiberium = 1050;
    int maxTrees = 600;
    std::vector<int> vegetationMinimums{0, 20, 60, 100, 160};
    std::vector<int> vegetationMaximums{0, 80, 180, 280, 420};
    std::vector<int> levelLightSettings{3, 3, 3, 3, 0};
    std::vector<int> temperateAmbientLight{75, 100, 75, 35, 0};
    std::vector<int> snowAmbientLight{75, 100, 75, 55, 0};
    std::vector<int> temperateAmbientRed{109, 108, 71, 51, 0};
    std::vector<int> temperateAmbientGreen{80, 94, 81, 46, 0};
    std::vector<int> temperateAmbientBlue{49, 68, 119, 141, 0};
    std::vector<int> snowAmbientRed{112, 99, 106, 76, 0};
    std::vector<int> snowAmbientGreen{80, 104, 107, 74, 0};
    std::vector<int> snowAmbientBlue{133, 107, 154, 118, 0};
    std::vector<std::string> techBuildings{"CAOILD", "CAPOWR", "CAMACH", "CAAIRP"};
    std::vector<std::string> civilianBuildings{"CABHUT", "CAHOSP", "CAPOWR", "CAMACH", "CAOILD", "CAARAY", "CACHIG04", "CACRCK"};
    std::vector<std::string> neutralUnits{"CIV1", "CIV2", "CIV3", "CTECH", "CIVX1"};
    std::vector<std::string> terrainChainObjects{"ROCK01", "ROCK02", "TREE02", "TREE01"};
    std::vector<std::string> temperateOrePatchLamps{"INGALITE"};
    std::vector<std::string> snowOrePatchLamps{"INGALITE"};
};

} // namespace rmg
