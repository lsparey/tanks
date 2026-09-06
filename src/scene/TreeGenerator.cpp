#include "TreeGenerator.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace TreeGenerator {
namespace {
constexpr float kPi = 3.14159265359f;
struct Builder {
    Tree tree;
    std::mt19937 structure, foliage;
    Species species;
    float density;
    uint32_t group = 0;
    float random(float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(structure);
    }
    void branch(glm::vec3 a, glm::vec3 b, float radius, float endRadius) {
        tree.branches.push_back({a, b, radius, endRadius});
    }
    void spray(glm::vec3 center, glm::vec3 direction, float vigor) {
        auto unit = [&] { return std::uniform_real_distribution<float>(0, 1)(foliage); };
        float keep = unit(), size = 0.82f + unit() * 0.36f;
        glm::vec3 u = glm::normalize(direction);
        glm::vec3 v = glm::normalize(glm::cross(u, glm::vec3(0, 1, 0)));
        glm::vec3 w = glm::normalize(glm::cross(u, v));
        float roll = (unit() - .5f) * (species == Species::Oak ? 2.6f : .9f);
        glm::vec3 rolledV = v * std::cos(roll) + w * std::sin(roll);
        glm::vec3 rolledW = w * std::cos(roll) - v * std::sin(roll);
        // Flattened sprays, not balls: ash has narrow leaflet fans, pine
        // elongated needle-bearing shoots, oak broader overlapping foliage.
        glm::vec3 radii = species == Species::Pine ? glm::vec3(.29f, .12f, .07f)
                        : species == Species::Ash ? glm::vec3(.32f, .13f, .065f)
                                                  : glm::vec3(.30f, .23f, .095f);
        // Split the fan into small, angled leaf/needle groups instead of
        // rendering its entire envelope as one paddle. Petioles are kept in
        // the skeleton even when density removes their foliage.
        for (int pair = 0; pair < 3; ++pair) for (int sign : {-1, 1}) {
            float spacing = species == Species::Oak ? .85f : .55f;
            glm::vec3 local((pair - 1) * radii.x * spacing,
                            sign * radii.y * spacing, (unit() - .5f) * radii.z * .3f);
            glm::vec3 position = center + glm::mat3(u, rolledV, rolledW) * local * size;
            glm::vec3 leafU = glm::normalize(u * .8f + rolledV * (sign * .6f));
            glm::vec3 leafV = glm::normalize(glm::cross(rolledW, leafU));
            glm::vec3 leafRadii = radii * glm::vec3(.42f,.40f,.50f) * size;
            leafRadii.z = std::max(leafRadii.z, .035f);
            if (species == Species::Oak) {
                // Individual laminae: the former thick, wide ellipsoids
                // fused adjacent fans into one solid canopy surface.
                leafRadii = radii * glm::vec3(.40f, .28f, .065f) * size;
            }
            branch(center, position, .003f, .001f);
            float opening = unit();
            uint32_t noiseSeed = foliage();
            if (keep < density * vigor && opening > .035f)
                tree.sprays.push_back({position, glm::mat3(leafU, leafV, rolledW), leafRadii, noiseSeed, group});
        }
    }
    // Foliage grows along a shoot, not just at its tip. Several differently
    // tilted fans overlap in depth while their individual leaf groups retain
    // small sky holes. The woody layout stays independent of density.
    void leafyShoot(glm::vec3 root, glm::vec3 end, float vigor) {
        glm::vec3 along = glm::normalize(end - root);
        glm::vec3 side = glm::normalize(glm::cross(along, glm::vec3(0, 1, 0)));
        int shoots = species == Species::Oak ? 3 : 2;
        for (int j = 0; j < shoots; ++j) {
            float t = .24f + j * (.66f / (shoots - 1));
            float sign = j % 2 == 0 ? -1.0f : 1.0f;
            glm::vec3 a = glm::mix(root, end, t);
            glm::vec3 direction = glm::normalize(along * .55f + side * sign
                + glm::vec3(0, random(.15f, .85f), 0));
            glm::vec3 b = a + direction * random(.10f, .19f);
            branch(a, b, .004f, .002f);
            spray(b, direction, vigor);
        }
        spray(end, along, vigor);
    }
    // Side shoots spread foliage over the outer bough and into the crown,
    // instead of leaving long bare limbs with a single row of terminal leaves.
    void fan(glm::vec3 a, glm::vec3 b, float width, int pairs, float vigor) {
        glm::vec3 along = glm::normalize(b - a);
        glm::vec3 side = glm::normalize(glm::cross(along, glm::vec3(0, 1, 0)));
        for (int j = 0; j < pairs; ++j) {
            float t = .15f + .75f * static_cast<float>(j) / std::max(pairs - 1, 1);
            for (int sign : {-1, 1}) {
                glm::vec3 root = glm::mix(a, b, t);
                glm::vec3 end = root + along * random(.18f, .32f)
                    + side * (static_cast<float>(sign) * width * random(.65f, 1.1f))
                    + glm::vec3(0, random(-.13f, .25f), 0);
                branch(root, end, .009f, .003f);
                leafyShoot(root, end, vigor);
            }
        }
        leafyShoot(glm::mix(a, b, .65f), b, vigor);
    }
    void pine() {
        float height = random(4.2f, 4.6f);
        branch(glm::vec3(0), glm::vec3(.035f, height, -.025f), .115f, .009f);
        float phase = random(0, 2 * kPi);
        for (int tier = 0; tier < 8; ++tier) {
            float y = .85f + tier * .43f;
            float reach = (1.0f - y / height) * 1.85f;
            for (int i = 0; i < 5; ++i) {
                ++group;
                float angle = phase + tier * 1.7f + i * kPi * .4f + random(-.22f, .22f);
                glm::vec3 direction(std::cos(angle), 0, std::sin(angle));
                glm::vec3 base(.035f * y / height, y + random(-.09f, .09f), -.025f * y / height);
                glm::vec3 elbow = base + direction * reach * .58f + glm::vec3(0, -.10f, 0);
                glm::vec3 end = base + direction * reach * random(.85f, 1.1f) + glm::vec3(0, .18f, 0);
                float baseRadius = .034f * (1 - y / height) + .01f;
                float midRadius = baseRadius * .6f;
                branch(base, elbow, baseRadius, midRadius);
                branch(elbow, end, midRadius, .005f);
                fan(glm::mix(base, elbow, .4f), end, reach * .32f, 2, random(.88f, 1.0f));
            }
        }
        // A narrow tuft around the leader rather than a solid conical cap.
        for (int i = 0; i < 4; ++i) {
            ++group;
            float angle = phase + i * kPi * .5f;
            glm::vec3 a(.025f, height - .3f, -.02f);
            glm::vec3 b = a + glm::vec3(std::cos(angle) * .16f, .22f, std::sin(angle) * .16f);
            branch(a, b, .008f, .003f); leafyShoot(a, b, 1.0f);
        }
    }
    void ash() {
        branch(glm::vec3(0), glm::vec3(.10f, 3.8f, -.08f), .105f, .008f);
        float phase = random(0, 2 * kPi);
        for (int tier = 0; tier < 6; ++tier) for (int i = 0; i < 2; ++i) {
            ++group;
            float angle = phase + tier * 1.35f + i * kPi + random(-.15f, .15f);
            float y = 1.0f + tier * .43f;
            glm::vec3 base(.10f * y / 3.8f, y, -.08f * y / 3.8f);
            float reach = 1.35f - tier * .14f + .22f * std::sin(tier * kPi / 5);
            glm::vec3 end = base + glm::vec3(std::cos(angle) * reach, random(.65f, .95f), std::sin(angle) * reach);
            branch(base, end, .032f, .009f);
            // Opposite secondary boughs give ash a feathery but substantial
            // crown; foliage occupies several heights along each main limb.
            glm::vec3 side(-std::sin(angle), 0, std::cos(angle));
            for (int j = 0; j < 2; ++j) for (int sign : {-1, 1}) {
                glm::vec3 a = glm::mix(base, end, .35f + j * .40f);
                glm::vec3 b = a + side * (sign * random(.30f, .52f))
                    + glm::normalize(end - base) * .24f + glm::vec3(0, random(.12f, .30f), 0);
                branch(a, b, .014f, .005f);
                fan(a, b, .23f, 2, random(.85f, 1.0f));
            }
            leafyShoot(glm::mix(base, end, .8f), end, 1.0f);
        }
    }
    void oakFork(glm::vec3 base, glm::vec3 direction, float length, float radius, int depth) {
        glm::vec3 elbow = base + direction * length * .55f;
        glm::vec3 turn = glm::normalize(direction + glm::vec3(random(-.25f,.25f), .13f, random(-.25f,.25f)));
        glm::vec3 end = elbow + turn * length * .45f;
        branch(base, elbow, radius, radius * .8f);
        branch(elbow, end, radius * .8f, radius * .55f);
        if (depth == 0) {
            fan(glm::mix(base, end, .4f), end, .44f, 2, random(.9f, 1.0f));
            return;
        }
        // Keep the main scaffold exposed between dense terminal boughs.
        float heading = std::atan2(turn.z, turn.x);
        for (int i = 0; i < 3; ++i) {
            float angle = heading + (i - 1) * .85f + random(-.3f,.3f);
            glm::vec3 next = glm::normalize(glm::vec3(std::cos(angle), random(.30f,1.4f), std::sin(angle)));
            oakFork(end, next, length * random(.57f,.72f), radius * .55f, depth - 1);
        }
    }
    void oak() {
        glm::vec3 fork(.08f, 1.0f, -.04f);
        branch(glm::vec3(0), fork, .155f, .105f);
        glm::vec3 leader(.02f, 3.65f, .06f);
        branch(fork, leader, .105f, .025f);
        float phase = random(0, 2 * kPi);
        // Stagger large boughs throughout a deep crown. Dense branch-end
        // foliage is separated by windows onto the scaffold, as in a mature
        // oak, instead of every limb terminating on one umbrella surface.
        for (int tier = 0; tier < 4; ++tier) {
            float y = 1.15f + tier * .66f;
            glm::vec3 base = glm::mix(fork, leader, (y - fork.y) / (leader.y - fork.y));
            for (int i = 0; i < 3; ++i) {
                ++group;
                float angle = phase + tier * 1.35f + i * 2 * kPi / 3 + random(-.25f, .25f);
                glm::vec3 direction = glm::normalize(glm::vec3(std::cos(angle),
                    random(.15f, .5f) + tier * .13f, std::sin(angle)));
                constexpr float reaches[] = {.95f, 1.12f, 1.0f, .55f};
                float length = reaches[tier] * random(.9f, 1.1f);
                oakFork(base, direction, length, .058f - tier * .009f, 1);
            }
        }
        ++group;
        oakFork(leader, glm::normalize(glm::vec3(.22f, 1.f, -.14f)), .42f, .025f, 1);
    }

};
} // namespace
Tree generate(uint32_t seed, Species species, float density) {
    Builder b{{}, std::mt19937(seed), std::mt19937(seed ^ 0x9e3779b9u), species,
              std::clamp(density, 0.0f, 1.0f)};
    b.tree.species = species;
    switch (species) {
        case Species::Pine: b.pine(); break;
        case Species::Ash: b.ash(); break;
        case Species::Oak: b.oak(); break;
    }
    return std::move(b.tree);
}
} // namespace TreeGenerator
