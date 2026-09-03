#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <vector>

#include "../render/AccelerationStructure.h"
#include "../render/CommandContext.h"
#include "../render/HistoryBuffer.h"
#include "../render/HudRenderer.h"
#include "../render/Mesh.h"
#include "../render/Pipeline.h"
#include "../render/SceneAccelerationStructure.h"
#include "../render/Swapchain.h"
#include "../render/Texture.h"
#include "../render/VulkanContext.h"
#include "../scene/Box.h"
#include "../scene/Camera.h"
#include "../scene/CollisionSystem.h"
#include "../scene/DebrisParticle.h"
#include "../scene/DynamicLight.h"
#include "../scene/ImpactEffect.h"
#include "../scene/InputManager.h"
#include "../scene/Projectile.h"
#include "../scene/RockInstance.h"
#include "../scene/ShrubInstance.h"
#include "../scene/SmokePuff.h"
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

    explicit Application(std::optional<ScreenshotRequest> screenshotRequest = std::nullopt);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void initWindow();
    void mainLoop();
    void drawFrame();

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    bool framebufferResized_ = false;
    size_t currentFrame_ = 0;
    double lastFrameTime_ = 0.0;
    float fpsSmoothed_ = 60.0f;
    bool followTank_ = true;
    bool prevFKeyDown_ = false;
    bool prevFireDown_ = false;
    bool prevScreenshotKeyDown_ = false;
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
    float gpuTlasMs_ = 0.0f;
    float gpuTerrainMs_ = 0.0f;
    float gpuForegroundMs_ = 0.0f;
    float gpuSceneryMs_ = 0.0f;
    float gpuEffectsMs_ = 0.0f;
    float gpuHudMs_ = 0.0f;
    float gpuTotalMs_ = 0.0f;

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<CommandContext> commands_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Terrain> terrain_;
    std::unique_ptr<Tank> tank_;
    std::unique_ptr<InputManager> input_;
    Camera camera_;

    std::unique_ptr<HudRenderer> hud_;
    std::unique_ptr<Texture> grassTextureA_;
    std::unique_ptr<Texture> grassTextureB_;
    std::unique_ptr<Texture> rockTextureA_;
    std::unique_ptr<Texture> rockTextureB_;
    std::unique_ptr<Texture> trackTexture_;
    std::unique_ptr<Texture> cloudTexture_;
    std::unique_ptr<Texture> crateTexture_;
    std::unique_ptr<Texture> whiteTexture_;
    std::unique_ptr<Texture> camoTexture_;
    std::unique_ptr<Texture> metalTexture_;
    std::unique_ptr<Texture> boundaryLineTexture_;
    std::unique_ptr<Texture> boundaryWallTexture_;
    // One texture (and one material set below) per mesh variant -- see
    // treeBarkMeshes_/treeLeafMeshes_/rockMeshes_ -- rather than a single
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
    VkDescriptorSet metalMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet boundaryLineMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet boundaryWallMaterialSet_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> barkMaterialSets_;  // one per treeBarkMeshes_ variant
    std::vector<VkDescriptorSet> leafMaterialSets_;  // one per treeLeafMeshes_ variant
    std::vector<VkDescriptorSet> rockMaterialSets_;  // one per rockMeshes_ variant
    std::unique_ptr<Mesh> boxMesh_;
    std::unique_ptr<Mesh> shellMesh_;
    std::unique_ptr<Mesh> flashMesh_;
    std::unique_ptr<Mesh> debrisChunkMesh_;
    std::unique_ptr<Mesh> debrisEmberMesh_;
    std::unique_ptr<Mesh> smokePuffMesh_;
    std::unique_ptr<Mesh> trackMarkMesh_;
    std::unique_ptr<Mesh> waterMesh_;  // null if no qualifying low-lying basin exists this run
    std::unique_ptr<Mesh> boundaryLineMesh_;
    std::unique_ptr<Mesh> boundaryWallMesh_;
    std::unique_ptr<Mesh> sedimentaryCliffMesh_;
    std::unique_ptr<Mesh> sedimentaryCliffGrassMesh_;
    // Half-extent of the square play-area boundary (see BoundaryGenerator)
    // -- also used by Tank::update to keep the hull from driving through
    // the boundary's wall of light.
    float boundaryHalfExtent_ = 0.0f;
    std::vector<std::unique_ptr<Mesh>> rockMeshes_;  // small pool of distinct rock shapes
    // 80-face versions for decorative scree that only covers a few pixels;
    // never used for collision or ray tracing.
    std::vector<std::unique_ptr<Mesh>> smallRockMeshes_;
    // Small pool of distinct fractal branch structures, one bark + one
    // leaves mesh per variant (see Mesh::treeBark/treeLeaves) -- matching
    // indices in each vector share the same seed, so their branch tips line
    // up.
    std::vector<std::unique_ptr<Mesh>> treeBarkMeshes_;
    std::vector<std::unique_ptr<Mesh>> treeLeafMeshes_;
    std::vector<std::unique_ptr<Mesh>> shrubMeshes_;  // small pool of distinct bush shapes
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
    std::vector<TrackMark> trackMarks_;
    glm::vec3 lastTrackMarkPosition_{0.0f};
    bool hasTrackMarkAnchor_ = false;
    std::vector<TreeInstance> trees_;
    std::vector<RockInstance> rocks_;
    std::vector<RockInstance> sedimentaryCliffs_;
    std::vector<ShrubInstance> shrubs_;
    // Small decorative scree/pebbles, biased toward the terrain's visible
    // gravel areas -- see spawnSmallRocks. Reuses RockInstance
    // and the same rockMeshes_/rockMaterialSets_ pool as rocks_ (just many
    // more, much smaller, and never added to the ray-traced TLAS -- see
    // gatherRayTracingInstances).
    std::vector<RockInstance> smallRocks_;
    // Static collision circles for trees/rocks, built once after spawning
    // both -- see Tank::update.
    std::vector<CollisionSystem::CircleObstacle> obstacles_;

    // Ray tracing: one BLAS per shared mesh (built once), plus a TLAS
    // rebuilt every frame from the current scene state (see
    // gatherRayTracingInstances). Tank's own BLAS per part live on Tank
    // itself since it owns those meshes.
    std::unique_ptr<AccelerationStructure> boxBLAS_;
    std::unique_ptr<AccelerationStructure> shellBLAS_;
    std::vector<std::unique_ptr<AccelerationStructure>> rockBLAS_;      // one per rockMeshes_ variant
    std::unique_ptr<AccelerationStructure> sedimentaryCliffBLAS_;
    std::vector<std::unique_ptr<AccelerationStructure>> treeBarkBLAS_;  // one per treeBarkMeshes_ variant
    std::vector<std::unique_ptr<AccelerationStructure>> treeLeafBLAS_;  // one per treeLeafMeshes_ variant
    std::unique_ptr<SceneAccelerationStructure> sceneAS_;
    std::unique_ptr<HistoryBuffer> historyBuffer_;

    void spawnBoxes();
    void spawnTrees(const WaterGenerator::FloodField& waterField);
    void spawnRocks(const WaterGenerator::FloodField& waterField);
    void spawnSedimentaryCliffs(const WaterGenerator::FloodField& waterField);
    void spawnShrubs(const WaterGenerator::FloodField& waterField);
    void spawnSmallRocks(const WaterGenerator::FloodField& waterField);
    void spawnExplosion(glm::vec3 position);
    void spawnDynamicLight(glm::vec3 position, glm::vec3 color, float radius, float intensity,
                            float lifetime);
    void spawnSmokePuff(glm::vec3 position, glm::vec3 velocity, float initialScale, float finalScale,
                         float lifetime);
    void destroyBox(Box& box);
    void updateTrackMarks(float deltaTime);
    void fireProjectile();
    void updateProjectilesAndCollisions(float deltaTime);
    void buildAccelerationStructures();
    std::vector<AccelerationStructure::Instance> gatherRayTracingInstances() const;
    void recreateSwapchainDependentResources();
    std::string nextScreenshotPath();
};
