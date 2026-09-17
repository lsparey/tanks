#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include <vector>

#include "FrameProfiler.h"

#include "../audio/AudioEngine.h"
#include "../render/AccelerationStructure.h"
#include "../render/CommandContext.h"
#include "../render/HistoryBuffer.h"
#include "../render/HudRenderer.h"
#include "../render/Mesh.h"
#include "../render/Pipeline.h"
#include "../render/ResolveTarget.h"
#include "../render/SceneAccelerationStructure.h"
#include "../render/Swapchain.h"
#include "../render/TaaBlendPass.h"
#include "../render/Texture.h"
#include "../render/TonemapPass.h"
#include "../render/TreeShadowMap.h"
#include "../render/VulkanContext.h"
#include "../scene/Box.h"
#include "../scene/Camera.h"
#include "../scene/CollisionSystem.h"
#include "../scene/DebrisParticle.h"
#include "../scene/DynamicLight.h"
#include "../scene/ImpactEffect.h"
#include "../scene/InputManager.h"
#include "../scene/MatchState.h"
#include "../scene/OpponentAI.h"
#include "../scene/Projectile.h"
#include "../scene/RockInstance.h"
#include "../scene/GrassClumpInstance.h"
#include "../scene/ShrubInstance.h"
#include "../scene/SmokePuff.h"
#include "../scene/WaterRipple.h"
#include "../scene/WeaponEffects.h"
#include "../scene/Tank.h"
#include "../scene/Terrain.h"
#include "../scene/TrackMark.h"
#include "../scene/TreeInstance.h"
#include "../scene/WaterGenerator.h"

class Application {
public:
    // Requests that the app save a screenshot and (usually) exit, rather
    // than running interactively -- see main.cpp's --screenshot flag. This
    // reads the rendered image straight out of GPU memory (see drawFrame's
    // capture block), so it works identically on any system that can run
    // the app at all, unlike an OS-level screenshot tool: those depend on
    // the window manager/compositor's own capture path, which can silently
    // return a black frame for a window it doesn't actually composite the
    // normal way (e.g. an XWayland surface under a Wayland compositor).
    struct ScreenshotRequest {
        std::string path;
        // Captured on this frameCounter_ value rather than frame 0 so ray
        // tracing's temporal accumulation (shadows/AO) has time to converge
        // past its initial noisy first frame -- see basic.frag's history
        // blending.
        uint32_t atFrame = 60;
        bool exitAfter = true;
    };

    explicit Application(std::optional<ScreenshotRequest> screenshotRequest = std::nullopt,
                         bool performanceReporting = false,
                         std::optional<uint32_t> worldSeed = std::nullopt,
                         std::string referenceView = {}, bool originalTankModel = false,
                         bool animateTracks = true, bool weaponPreview = false,
                         bool valleyTerrain = false, uint32_t terrainAttempts = 1,
                         MacroTerrain::Landform landform = MacroTerrain::Landform::Mixed,
                         int terrainResolution = 257, int refinementPasses = 0, bool showTerrainMenu = false);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();
    enum class TreeLodMode { Reduced, Previous, Far, Hidden, Full };
    void setTreeLodMode(TreeLodMode mode);
    void beginTreeLodBenchmark();
    // 0: legacy rays, 1: stable PCF maps, 2: contact-hardening PCSS maps.
    void setTreeShadowMode(int mode) { treeShadowMode_ = mode; shadowHistoryReset_ = true; }
    // App-local deterministic driving check (fixed timestep, forward drive
    // held between fixed frames) for inspecting temporal artifacts --
    // ghosting/trails behind the moving tank -- in automated captures,
    // where desktop key injection is unavailable/unreliable.
    void setDrivePreview(bool enabled) { drivePreview_ = enabled; }
    // Opt-in match rules (fuel-limited movement so far -- see MatchState).
    // Off by default: turn-passing isn't wired yet (no opponent AI exists to
    // ever hand a turn back), so this stays off the default interactive path
    // until enough of the roadmap exists to make it a complete experience.
    void setMatchEnabled(bool enabled) { matchEnabled_ = enabled; }
    // Higher is easier (less opponent aim error) -- see OpponentAI::aimErrorRadians.
    void setAiDifficulty(float difficulty) { aiDifficulty_ = difficulty; }
    // Deterministic full-match check in the style of setDrivePreview/
    // weaponPreview_: both tanks are AI-driven (see driveTankWithAI) with a
    // fixed timestep, for scripted regression/reproducibility captures.
    void setMatchPreview(bool enabled) { matchPreview_ = enabled; }
    void setShadowPreview(bool enabled, bool freezeWind) {
        shadowPreview_ = enabled; freezeWind_ = freezeWind;
    }

private:
    void advanceTreeLodBenchmark();
    TreeLodMode treeLodMode_ = TreeLodMode::Reduced;
    TreeLodMode treeLodResumeMode_ = TreeLodMode::Reduced;
    bool prevTreeLodKeyDown_ = false;
    bool treeLodBenchmark_ = false;
    uint32_t treeLodBenchmarkStage_ = 0, treeLodBenchmarkFrames_ = 0;
    enum class CameraMode {
        HullFollow,
        TurretAim,
        Free,
    };

    void initialize(bool originalTankModel, bool animateTracks, bool weaponPreview,
                    bool valleyTerrain, uint32_t terrainAttempts, MacroTerrain::Landform landform,
                    int terrainResolution, int refinementPasses, bool showTerrainMenu);
    bool selectTerrain(bool& valleyTerrain, MacroTerrain::Landform& landform,
                       int& terrainResolution, int& refinementPasses);
    std::string menuTextInput_;
    void initWindow();
    void cleanup() noexcept;
    void mainLoop();
    void drawFrame();
    void reportPerformance();
    void applyReferenceCamera();

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    uint32_t worldSeed_ = 0;
    std::string referenceView_;
    glm::vec3 waterReferenceTarget_{0.0f};
    bool framebufferResized_ = false;
    size_t currentFrame_ = 0;
    double lastFrameTime_ = 0.0;
    double windTime_ = 0.0;
    float prevWindTime_ = 0.0f;
    float displayedFps_ = 0.0f;
    double fpsWindowStart_ = 0.0;
    uint32_t fpsWindowFrames_ = 0;
    FrameProfiler profiler_;
    FrameProfiler::Sample performanceSample_{};
    bool performanceReporting_ = false;
    bool prevPerformanceKeyDown_ = false;
    bool prevPerformanceResetKeyDown_ = false;
    bool performanceFrameValid_ = false;
    uint32_t performanceWarmup_ = 60;
    double lastPerformanceReport_ = 0.0;
    CameraMode cameraMode_ = CameraMode::HullFollow;
    bool prevCameraToggleKeyDown_ = false;
    bool prevFireDown_ = false;
    // Turn camera (see mainLoop): during the opponent's turn the normal
    // cameraMode_ view is overridden to follow the opponent tank, then its
    // fired shell, blending smoothly rather than cutting so the player
    // never regains control mid-motion (cameraTransitioning_ feeds
    // playerTurnActive). AwayCameraTarget::None means "show cameraMode_'s
    // own view" -- the normal, in-turn behavior, unchanged from before this
    // item.
    enum class AwayCameraTarget { None, OpponentTank, Shell };
    AwayCameraTarget awayCameraTarget_ = AwayCameraTarget::None;
    bool cameraTransitioning_ = false;
    float cameraBlend_ = 1.0f;
    glm::vec3 blendFromEye_{0.0f};
    glm::vec3 blendFromFront_{0.0f, 0.0f, -1.0f};
    bool showHudHelp_ = false;
    bool prevHudHelpKeyDown_ = false;
    bool prevScreenshotKeyDown_ = false;
    // F5 toggles sun shadows/AO off entirely (see basic.frag's
    // rawShadow/rawAO) -- lets a perf/quality comparison run without
    // restarting, and doubles as the terrain menu's SHADOWS button default.
    bool shadowsEnabled_ = true;
    // Sound effects are opt-in from the terrain menu's SOUND toggle and
    // default off -- there is no in-game key for this, so with the menu
    // skipped (no --menu flag) the audio device is never opened at all.
    bool soundEnabled_ = false;
    int treeShadowMode_ = 2;
    bool prevTreeShadowKeyDown_ = false;
    bool aoEnabled_ = true;
    bool prevAoKeyDown_ = false;
    bool reflectionRaysEnabled_ = true;
    bool prevReflectionKeyDown_ = false;
    bool prevVelocityDebugKeyDown_ = false;
    bool shadowHistoryReset_ = true;
    bool shadowPreview_ = false;
    bool freezeWind_ = false;
    bool drivePreview_ = false;
    bool matchPreview_ = false;
    bool prevShadowsKeyDown_ = false;
    std::optional<ScreenshotRequest> screenshotRequest_;
    int screenshotCounter_ = 0;  // suffixes F12-triggered screenshot filenames
    glm::mat4 prevViewProj_{1.0f};
    glm::vec3 prevCameraPos_{0.0f};
    bool firstFrame_ = true;
    uint32_t frameCounter_ = 0;

    // GPU timestamps divide each frame into TLAS, terrain, foreground,
    // repeated scenery, effects, and HUD regions. Results are read only
    // after that slot's fence signals, so profiling never stalls the active
    // GPU submission.
    VkQueryPool gpuTimestampPool_ = VK_NULL_HANDLE;
    std::array<bool, CommandContext::kFramesInFlight> gpuTimestampsReady_{};
    bool gpuTimingInitialized_ = false;
    float gpuTimestampPeriodNs_ = 1.0f;
    float gpuTreeShadowMs_ = 0.0f;
    float gpuTlasMs_ = 0.0f;
    float gpuTerrainMs_ = 0.0f;
    float gpuForegroundMs_ = 0.0f;
    float gpuSceneryMs_ = 0.0f;
    float gpuTreeBarkMs_ = 0.0f;
    float gpuFoliageDepthMs_ = 0.0f;
    float gpuFoliageLightingMs_ = 0.0f;
    float gpuOtherSceneryMs_ = 0.0f;
    float gpuEffectsMs_ = 0.0f;
    float gpuHudMs_ = 0.0f;
    float gpuTotalMs_ = 0.0f;

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<CommandContext> commands_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Terrain> terrain_;
    std::unique_ptr<Tank> tank_;
    // The opponent combatant (see MatchState::CombatantId::Opponent). Placed
    // once at a valid, separated TerrainPlayability::secondarySpawn and
    // never updated -- it does not move until AI/turn-driven input exists
    // (later roadmap items), so it uses the static-object motion-vector
    // path (see drawTankParts's `dynamic` parameter) rather than the
    // player's moving-rigid-body one.
    std::unique_ptr<Tank> opponentTank_;
    std::unique_ptr<InputManager> input_;
    std::unique_ptr<AudioEngine> audio_;
    Camera camera_;

    std::unique_ptr<HudRenderer> hud_;
    std::unique_ptr<Texture> grassTextureA_;
    std::unique_ptr<Texture> grassTextureB_;
    std::unique_ptr<Texture> rockTextureA_;
    std::unique_ptr<Texture> rockTextureB_;
    // Whole-map RGBA lookup for terrain UV warp and material patch masks;
    // generated once so the fragment shader does not rebuild those six
    // procedural noise values for every covered pixel.
    std::unique_ptr<Texture> terrainControlTexture_;
    std::unique_ptr<Texture> terrainFieldTexture_;
    std::unique_ptr<Texture> trackTexture_;
    std::unique_ptr<Texture> cloudTexture_;
    std::unique_ptr<Texture> crateTexture_;
    std::unique_ptr<Texture> whiteTexture_;
    std::unique_ptr<Texture> camoTexture_;
    std::unique_ptr<Texture> opponentCamoTexture_;
    std::unique_ptr<Texture> metalTexture_;
    std::unique_ptr<Texture> boundaryLineTexture_;
    std::unique_ptr<Texture> boundaryWallTexture_;
    // One texture (and one material set below) per mesh variant -- see
    // treeBarkMeshes_/treeFoliageMeshes_/rockMeshes_ -- rather than a single
    // shared texture, so the small pool of tree/rock shapes also looks
    // materially different from one instance to the next, not just tinted.
    std::vector<std::unique_ptr<Texture>> barkTextures_;
    std::vector<std::unique_ptr<Texture>> leafTextures_;
    std::vector<std::unique_ptr<Texture>> rockStandaloneTextures_;
    VkDescriptorSet terrainMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet trackMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet cloudMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet crateMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet whiteMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet camoMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet opponentCamoMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet metalMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet boundaryLineMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet boundaryWallMaterialSet_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> barkMaterialSets_;  // one per treeBarkMeshes_ variant
    std::vector<VkDescriptorSet> leafMaterialSets_;  // one per treeFoliageMeshes_ variant
    std::vector<VkDescriptorSet> rockMaterialSets_;  // one per rockMeshes_ variant
    std::unique_ptr<Mesh> boxMesh_;
    std::unique_ptr<Mesh> shellMesh_;
    std::unique_ptr<Mesh> flashMesh_;
    // Small pool of distinct shard shapes (see Mesh::shard) so a debris
    // burst reads as varied fragments rather than copies of one chunk;
    // DebrisParticle::meshVariant indexes into this.
    std::vector<std::unique_ptr<Mesh>> debrisChunkMeshes_;
    std::unique_ptr<Mesh> debrisEmberMesh_;
    std::unique_ptr<Mesh> smokePuffMesh_;
    std::unique_ptr<Mesh> dustPuffMesh_;
    std::unique_ptr<Mesh> trackMarkMesh_;
    std::unique_ptr<Mesh> waterMesh_;  // null if no qualifying low-lying basin exists this run
    std::unique_ptr<Mesh> boundaryLineMesh_;
    std::unique_ptr<Mesh> boundaryWallMesh_;
    // Half-extent of the square play-area boundary (see BoundaryGenerator)
    // -- also used by Tank::update to keep the hull from driving through
    // the boundary's wall of light.
    float boundaryHalfExtent_ = 0.0f;
    std::vector<std::unique_ptr<Mesh>> rockMeshes_;        // LOD 0: 1280 faces
    std::vector<std::unique_ptr<Mesh>> mediumRockMeshes_;  // LOD 1: 320 faces
    // LOD 2: 80 faces, shared by far boulders, decorative scree, and
    // ordinary far-distance rasterization.
    std::vector<std::unique_ptr<Mesh>> smallRockMeshes_;
    // Inset copies of LOD 2 used only for ray queries. Keeping these inside
    // the visible boulders prevents the proxy from self-shadowing them.
    std::vector<std::unique_ptr<Mesh>> rockProxyMeshes_;
    // Matching variants share one generated skeleton. Foliage packs all
    // bough/LOD ranges into one mesh; bark retains three whole-tree meshes.
    std::vector<std::unique_ptr<Mesh>> treeBarkMeshes_;
    std::vector<std::unique_ptr<Mesh>> treeFoliageMeshes_;
    std::vector<std::vector<Mesh::FoliageGroup>> treeFoliageGroups_;
    // CPU staging lists retain capacity across frames and LOD changes.
    std::vector<std::vector<RasterInstance>> treeInstanceGroups_;
    std::vector<std::vector<std::vector<RasterInstance>>> foliageInstanceGroups_;
    std::vector<glm::vec4> treeShadowBounds_; // local center/radius, all raster levels
    std::vector<std::unique_ptr<Mesh>> mediumTreeBarkMeshes_;
    // Far raster LOD and simplified ray-tracing proxy geometry.
    std::vector<std::unique_ptr<Mesh>> farTreeBarkMeshes_;
    std::vector<std::unique_ptr<Mesh>> distantTreeBarkMeshes_; // visible low-cost LOD 2
    std::vector<std::unique_ptr<Mesh>> treeLeafProxyMeshes_;
    std::array<std::vector<std::unique_ptr<Mesh>>,2> treeReflectionMeshes_;
    std::vector<std::unique_ptr<Mesh>> shrubMeshes_;  // small pool of distinct bush shapes
    std::vector<std::unique_ptr<Mesh>> grassClumpMeshes_;  // grass tufts, then waterside reed clumps
    int grassTuftVariants_ = 0;  // pool indices below this are tufts, at/after it reeds
    std::unique_ptr<Mesh> cloudDomeMesh_;
    std::vector<Box> boxes_;
    std::vector<Projectile> projectiles_;
    std::vector<ImpactEffect> impactEffects_;
    std::vector<DebrisParticle> debris_;
    // Muzzle-flash/explosion point lights -- see DynamicLight.h. Capped at
    // kMaxDynamicLights by spawnDynamicLight rather than left to grow like
    // impactEffects_/debris_, since each one has to round-trip through a
    // fixed-size FrameUBO array every frame.
    std::vector<DynamicLight> dynamicLights_;
    std::vector<SmokePuff> smokePuffs_;
    std::vector<WaterRipple> waterRipples_;
    std::vector<WeaponEffects::Flash> muzzleFlashes_;
    std::vector<WeaponEffects::Smoke> blastSmoke_;
    std::vector<WeaponEffects::Scorch> scorches_;
    WaterGenerator::FloodField legacyWaterField_;
    glm::vec2 spawnXZ_{0.0f};
    glm::vec2 opponentSpawnXZ_{0.0f};
    // Full spawn pose (position+forward), cached alongside the XZ-only
    // fields above -- restartMatch() re-places both tanks here without
    // needing to regenerate terrain (see PLAN.md's "Match flow and
    // deterministic replay").
    glm::vec3 playerSpawnPosition_{0.0f};
    glm::vec2 playerSpawnForward_{0.0f, 1.0f};
    glm::vec3 opponentSpawnPosition_{0.0f};
    glm::vec2 opponentSpawnForward_{0.0f, 1.0f};
    // opponentTank_ is always constructed (see initialize()) but only
    // placed/drawn/collided when the advanced terrain generator produced a
    // navigation result -- `--terrain legacy` has no playability/route
    // system to derive a second spawn from, so it gets no opponent.
    bool hasOpponent_ = false;
    // Off by default; see setMatchEnabled. matchState_ itself is always
    // constructed (a plain value type, no Vulkan/heap resources) but only
    // consulted/advanced when matchEnabled_ is set.
    MatchState matchState_;
    bool matchEnabled_ = false;
    bool prevEndMoveKeyDown_ = false;
    bool prevRestartKeyDown_ = false;
    // frameCounter_ this match (re)started on -- drives the brief
    // "MATCH START" HUD banner; see restartMatch().
    uint32_t matchStartFrame_ = 0;
    bool prevArmPowerUpKeyDown_ = false;
    // Power-up type on crate collection, and shot dispersion (see
    // MatchState::dispersionDegrees) -- seeded from worldSeed_ once in
    // initialize() so a fixed seed stays reproducible under --match,
    // unlike this codebase's purely-visual randomness (std::random_device
    // elsewhere) which doesn't need to be.
    std::mt19937 powerUpRng_;
    // AI turn state (see driveTankWithAI/OpponentAI.h) -- one instance per
    // combatant so either tank can be AI-driven (the opponent always, the
    // player too under --match-preview). aiDifficulty_ is higher = easier
    // (less aim error); see --ai-difficulty. The two "solved" flags make
    // chooseMoveTarget/the fire solution get computed once per Move/AimFire
    // phase entry, not re-rolled every frame, so the point a tank visibly
    // steers/aims toward doesn't jitter.
    float aiDifficulty_ = 1.0f;
    struct AiTurnState {
        bool moveTargetSet = false;
        glm::vec2 moveTarget{0.0f};
        bool aimSolved = false;
        float targetTurretYaw = 0.0f;
        float targetPower = 0.0f;
    };
    std::array<AiTurnState, 2> aiTurnState_;
    bool weaponPreview_ = false;
    std::vector<TrackMark> trackMarks_;
    struct TrackTrailState {
        glm::vec3 previousPosition{0.0f};
        float distanceSinceMark = 0.0f;
        float distanceSinceDust = 0.0f;
        bool initialized = false;
    };
    std::array<TrackTrailState, 2> trackTrails_;
    std::vector<TreeInstance> trees_;
    std::vector<RockInstance> rocks_;
    std::vector<ShrubInstance> shrubs_;
    // Small decorative scree/pebbles, biased toward the terrain's visible
    // gravel areas -- see spawnSmallRocks. Reuses RockInstance
    // and the same rockMeshes_/rockMaterialSets_ pool as rocks_ (just many
    // more, much smaller, and never added to the ray-traced TLAS -- see
    // gatherRayTracingInstances).
    std::vector<RockInstance> smallRocks_;
    // Near-field ground vegetation -- baked once over the whole terrain
    // (see spawnGrassClumps), like smallRocks_ above, but culled per-frame
    // to a short draw radius rather than smallRocks_'s longer one, since
    // individual tufts are only meant to read up close (see PLAN.md's
    // "Near-field ground vegetation"). Reuses leafMaterialSets_/materialType
    // 2 (foliage) rather than a texture/pipeline of its own.
    std::vector<GrassClumpInstance> grassClumps_;
    // Static collision circles for trees/rocks, built once after spawning
    // both -- see Tank::update.
    std::vector<CollisionSystem::CircleObstacle> obstacles_;

    // Ray tracing: one BLAS per shared mesh (built once), plus a TLAS
    // rebuilt every frame from the current scene state (see
    // gatherRayTracingInstances). Tank's own BLAS per part live on Tank
    // itself since it owns those meshes.
    std::unique_ptr<AccelerationStructure> boxBLAS_;
    std::unique_ptr<AccelerationStructure> shellBLAS_;
    std::vector<std::unique_ptr<AccelerationStructure>> rockBLAS_;  // one inset proxy per rock variant
    std::vector<std::unique_ptr<AccelerationStructure>> treeBarkBLAS_;  // one per treeBarkMeshes_ variant
    std::array<std::vector<std::unique_ptr<AccelerationStructure>>,2> treeReflectionBLAS_;
    std::vector<std::unique_ptr<AccelerationStructure>> treeLeafBLAS_;  // one per treeFoliageMeshes_ variant
    std::unique_ptr<SceneAccelerationStructure> sceneAS_;
    std::unique_ptr<TreeShadowMap> treeShadowMap_;
    // The light-space matrix each shadow cascade was last actually rendered
    // with. Cascades refresh round-robin (see drawFrame's cascadeUpdates),
    // so the UBO must keep serving a skipped cascade's rendered matrix --
    // sampling a stale layer through this frame's freshly snapped matrix
    // would shift every shadow by the camera's motion since that render.
    std::array<glm::mat4, TreeShadowCascades::kCount> treeShadowRenderedMatrices_{};
    std::unique_ptr<HistoryBuffer> historyBuffer_;
    // Single-channel (R16_SFLOAT) sibling buffer for the independently,
    // fixed-alpha smoothed foliage-transmission factor -- see basic.frag's
    // comment on why that value can't share historyBuffer_'s adaptive blend.
    std::unique_ptr<HistoryBuffer> foliageHistoryBuffer_;
    // Linear HDR scene-color target the main pass resolves into, and the
    // single tonemap pass that maps it down to the swapchain's sRGB image
    // right before the HUD draws on top -- see ResolveTarget/TonemapPass.
    std::unique_ptr<ResolveTarget> hdrTarget_;
    // Resolved UV-space motion-vector buffer -- see shaders/basic.frag and
    // PLAN.md's "Linear HDR and temporal image stability". Consumed by
    // TaaBlendPass's reprojection; F10 also toggles visualizing it directly
    // via TonemapPass for verification.
    std::unique_ptr<ResolveTarget> velocityTarget_;
    std::unique_ptr<TonemapPass> tonemapPass_;
    bool showVelocityDebug_ = false;
    // Ping-ponged TAA color history -- reuses HistoryBuffer verbatim (its
    // MSAA scratch image goes unused here; see TaaBlendPass's comment for
    // why that's an accepted tradeoff). TaaBlendPass writes this frame's
    // blended result into slot(currentFrame_) and TonemapPass reads it
    // straight from there afterward.
    std::unique_ptr<HistoryBuffer> taaHistory_;
    std::unique_ptr<TaaBlendPass> taaBlendPass_;
    bool taaEnabled_ = true;
    bool prevTaaKeyDown_ = false;
    // Both ping-pong slots need one real write before either holds a
    // meaningful "previous frame" -- unlike firstFrame_'s single-frame case.
    uint32_t taaHistoryPrimedFrames_ = 0;

    bool isUnderwater(float x, float z) const;
    // Standing-water surface height wherever (x, z) is wet, empty where dry
    // -- one query over whichever water system this run generated (the
    // TerrainWater surface or the legacy flood field). Drives shell
    // splashes, tank wading drag, and track wash/wake placement.
    std::optional<float> waterLevelAt(float x, float z) const;
    bool allowsScenery(glm::vec2 center, float radius) const;
    // True when (x, z)'s navigation cell is in the same connected component
    // as the accepted spawn -- i.e. actually reachable by driving, not just
    // clear of hazards. Always true when there's no navigation data
    // (`--terrain legacy`), matching allowsScenery's own no-op-when-absent
    // convention.
    bool sameComponentAsSpawn(glm::vec2 xz) const;
    // Rejection-samples one candidate position: clear of both spawns
    // (`minDistanceFromSpawns`), clear of `placed`'s existing points
    // (`minDistanceBetween`), passing `allowsScenery(., allowsSceneryRadius)`,
    // and (when navigation data exists) in the spawns' connected component.
    // Shared by spawnBoxes' initial placement and Application::collectBox's
    // top-up so the two can't drift apart. Always returns a position (the
    // last attempted candidate on failure, matching spawnBoxes' original
    // fallback-to-legacy-terrain behavior) and reports success via `found`.
    glm::vec2 findScenerySpot(std::mt19937& rng, const std::vector<glm::vec2>& placed,
                               float minDistanceFromSpawns, float minDistanceBetween,
                               float allowsSceneryRadius, int maxAttempts, bool* found);
    void spawnBoxes();
    void spawnTrees();
    void spawnRocks();
    void spawnShrubs();
    void spawnSmallRocks();
    void spawnGrassClumps();
    void spawnReeds();
    void spawnExplosion(glm::vec3 position);
    void spawnDynamicLight(glm::vec3 position, glm::vec3 color, float radius, float intensity,
                            float lifetime);
    void spawnSmokePuff(glm::vec3 position, glm::vec3 velocity, float initialScale, float finalScale,
                        float lifetime, bool dust = false);
    void spawnWaterSplash(glm::vec3 point);
    void spawnWaterRipple(glm::vec3 position, float initialRadius, float growthRate, float lifetime,
                          float waveAmplitude);
    void destroyBox(Box& box);
    // --match only (see the tank-vs-box overlap check in
    // updateProjectilesAndCollisions): grants the player a random power-up
    // (see MatchState::collectPowerUp) and relocates this same Box to a
    // fresh valid spot instead of destroying it -- "top up as they are
    // collected" (PLAN.md). Free play keeps destroyBox unchanged.
    void collectBox(Box& box, CombatantId collector);
    void updateTrackMarks(float deltaTime);
    // Drives one tank for one frame with AI (see OpponentAI.h and PLAN.md's
    // "Opponent AI"/"Match flow and deterministic replay"): a no-op unless
    // matchEnabled_ && hasOpponent_. Calls self.update() exactly once
    // regardless of phase -- with an AI-decided Tank::Controls during
    // selfId's own Move/AimFire phases, or a neutral one otherwise so it
    // stays grounded/settled the same way a manually-driven tank always
    // does. Always used for the opponent; also used for the player under
    // --match-preview (see matchPreview_).
    void driveTankWithAI(Tank& self, Tank& opponent, CombatantId selfId, float deltaTime);
    // Resets matchState_, re-places both tanks at their cached spawn poses
    // and gives them a fresh crate layout -- no terrain regeneration needed
    // (see PLAN.md's "Match flow and deterministic replay"). Only reachable
    // once matchState_.isGameOver(), via the N key in mainLoop.
    void restartMatch();
    // Player call site is fireProjectile(*tank_, CombatantId::Player);
    // opponent AI calls fireProjectile(*opponentTank_, CombatantId::Opponent).
    void fireProjectile(Tank& firingTank, CombatantId firer);
    void spawnGroundScorch(glm::vec3 point);
    void updateProjectilesAndCollisions(float deltaTime);
    void buildAccelerationStructures();
    void appendTankRayInstances(const Tank& tank, std::vector<AccelerationStructure::Instance>& instances);
    std::vector<AccelerationStructure::Instance> gatherRayTracingInstances();
    void recreateSwapchainDependentResources();
    std::string nextScreenshotPath();
    void presentLoadingProgress(float fraction, const std::function<void()>& drawMenu = {});
};
