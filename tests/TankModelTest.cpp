#include "io/ModelLoader.h"
#include "scene/TankSurface.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    constexpr float turretRise = .08f;
    const auto original = ModelLoader::load(std::string(ASSET_ROOT) + "/assets/models/tank.x");
    require(original.parts.size() >= 4, "Original model remains loadable");
    auto refined = ModelLoader::load(std::string(ASSET_ROOT) + "/assets/models/challenger2.obj");
    std::map<std::string, size_t> triangles;
    glm::vec3 hullMin(1000), hullMax(-1000), barrelMin(1000), barrelMax(-1000);
    size_t objects = 0;
    size_t roadWheels = 0;
    bool turretChecked = false;
    bool skirtChecked = false;
    bool commanderChecked = false;
    size_t drums = 0;
    size_t smokeLaunchers = 0;
    for (auto& part : refined.parts) {
        require(!part.indices.empty() && part.indices.size() % 3 == 0, "Triangulated mesh required");
        require(part.materialName == "Base" || part.materialName == "Turret" ||
                part.materialName == "Barrel" || part.materialName == "Tracks" ||
                part.materialName == "HullFittings" || part.materialName == "TurretDark", "Known material groups");
        ++objects;
        if (part.meshName == "commander_cupola") {
            for (const auto& v : part.vertices)
                require(v.position.x < 0, "Commander cupola on image-left when viewed from +Z");
            commanderChecked = true;
        }
        if (part.meshName.find("rear_drum") != std::string::npos) {
            require(part.materialName == "HullFittings", "Painted drums excluded from hull collider");
            ++drums;
        }
        if (part.meshName.find("smoke_launcher_") != std::string::npos) {
            require(part.materialName == "TurretDark", "Launchers follow turret, not barrel");
            ++smokeLaunchers;
        }
        if (part.meshName.find("road_wheel_") != std::string::npos) ++roadWheels;
        if (part.meshName == "left_skirt_3") {
            float minY = 1000;
            for (const auto& v : part.vertices) minY = std::min(minY,v.position.y);
            require(std::abs(minY-25*4.48f/398.0f) < 1e-5f, "Deep skirts follow profile");
            skirtChecked = true;
        }
        if (part.meshName == "turret_armour") {
            glm::vec3 low(1000), high(-1000);
            for (const auto& v : part.vertices) {
                low = glm::min(low,v.position);
                high = glm::max(high,v.position);
            }
            // User's side profile: turret X=222..466, roof Y=57,
            // ground Y=181; hull X=156..554 spans 4.48 game units.
            constexpr float scale = 4.48f/398.0f;
            require(std::abs((high.z-low.z)-244*scale) < 1e-5f, "Long turret profile");
            require(std::abs(high.y-(124*scale+turretRise)) < 1e-5f, "Raised turret roof profile");
            float frontWidth = 0;
            for (const auto& v : part.vertices)
                if (std::abs(v.position.z-high.z) < 1e-5f)
                    frontWidth = std::max(frontWidth,std::abs(v.position.x)*2);
            require(std::abs(frontWidth-1.48f) < 1e-5f, "Broad frontal armour cheeks");
            turretChecked = true;
        }
        triangles[part.materialName] += part.indices.size() / 3;
        for (const auto& v : part.vertices) {
            require(std::isfinite(v.position.x) && std::isfinite(v.position.y) &&
                    std::isfinite(v.position.z), "Finite positions");
            require(std::abs(glm::length(v.normal) - 1.0f) < 1e-4f, "Unit normals");
            require(v.position.y >= 0.0f, "Track contact sits on ground");
            if (part.materialName == "Base") {
                hullMin = glm::min(hullMin, v.position);
                hullMax = glm::max(hullMax, v.position);
            } else if (part.materialName == "Barrel") {
                barrelMin = glm::min(barrelMin, v.position);
                barrelMax = glm::max(barrelMax, v.position);
            }
        }
        for (size_t i = 0; i < part.indices.size(); i += 3) {
            for (size_t j = 0; j < 3; ++j)
                require(part.indices[i+j] < part.vertices.size(), "Valid vertex indices");
            const auto a = part.vertices[part.indices[i]].position;
            const auto b = part.vertices[part.indices[i+1]].position;
            const auto c = part.vertices[part.indices[i+2]].position;
            require(glm::length(glm::cross(b-a, c-a)) > 1e-8f, "Nondegenerate triangles");
        }
        const size_t indexCount = part.indices.size();
        TankSurface::bakeEdgeDistances(part.vertices, part.indices);
        require(part.indices.size() == indexCount, "Wear bake preserves model triangles");
    }
    require(objects > 100, "Named authoring objects survive import");
    require(roadWheels == 12 && turretChecked && skirtChecked, "Profile landmark objects survive import");
    require(commanderChecked && drums == 2 && smokeLaunchers == 10, "Multi-view fittings survive import");
    require(triangles.size() == 6, "All six authored material categories present");
    size_t total = 0;
    for (const auto& [name, count] : triangles) {
        std::cout << name << ": " << count << " triangles\n";
        total += count;
    }
    require(total > 5000 && total < 14000, "Multi-view triangle budget");
    require(std::abs((hullMax.x-hullMin.x)-2.222f) < .02f, "Original hull width retained");
    require(std::abs((hullMax.z-hullMin.z)-4.48f) < .02f, "Original hull length approximately retained");
    require(std::abs(hullMax.y-1.042f) < 1e-5f, "Deck fittings do not enlarge hull collision bounds");
    constexpr float profileScale = 4.48f/398.0f;
    require(std::abs((barrelMax.y+barrelMin.y)*.5f-(97*profileScale+turretRise)) < 1e-5f,
            "Gun bore follows turret lift");
    require(std::abs(barrelMin.z-.80f) < 1e-5f && std::abs(barrelMax.z-333*profileScale) < 1e-5f,
            "Gun trunnion and muzzle positions");
}
