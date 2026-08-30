#pragma once

#include <memory>

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
#include "../scene/DebrisParticle.h"
#include "../scene/ImpactEffect.h"
#include "../scene/InputManager.h"
#include "../scene/Projectile.h"
#include "../scene/Tank.h"
#include "../scene/Terrain.h"
#include "../scene/TrackMark.h"
#include "../scene/TreeInstance.h"
#include "../scene/WaterGenerator.h"

class Application {
public:
    Application();
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
    bool followTank_ = true;
    bool prevFKeyDown_ = false;
    bool prevFireDown_ = false;
    glm::mat4 prevViewProj_{1.0f};
    glm::vec3 prevCameraPos_{0.0f};
    bool firstFrame_ = true;
    uint32_t frameCounter_ = 0;

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<CommandContext> commands_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Terrain> terrain_;
    std::unique_ptr<Tank> tank_;
    std::unique_ptr<InputManager> input_;
    Camera camera_;

    std::unique_ptr<HudRenderer> hud_;
    std::unique_ptr<Texture> grassTexture_;
    std::unique_ptr<Texture> rockTexture_;
    std::unique_ptr<Texture> trackTexture_;
    std::unique_ptr<Texture> whiteTexture_;
    VkDescriptorSet terrainMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet trackMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet whiteMaterialSet_ = VK_NULL_HANDLE;
    std::unique_ptr<Mesh> boxMesh_;
    std::unique_ptr<Mesh> shellMesh_;
    std::unique_ptr<Mesh> flashMesh_;
    std::unique_ptr<Mesh> treeMesh_;
    std::unique_ptr<Mesh> debrisChunkMesh_;
    std::unique_ptr<Mesh> debrisEmberMesh_;
    std::unique_ptr<Mesh> trackMarkMesh_;
    std::unique_ptr<Mesh> waterMesh_;  // null if no qualifying low-lying basin exists this run
    std::vector<Box> boxes_;
    std::vector<Projectile> projectiles_;
    std::vector<ImpactEffect> impactEffects_;
    std::vector<DebrisParticle> debris_;
    std::vector<TrackMark> trackMarks_;
    glm::vec3 lastTrackMarkPosition_{0.0f};
    bool hasTrackMarkAnchor_ = false;
    std::vector<TreeInstance> trees_;

    // Ray tracing: one BLAS per shared mesh (built once), plus a TLAS
    // rebuilt every frame from the current scene state (see
    // gatherRayTracingInstances). Tank's own BLAS per part live on Tank
    // itself since it owns those meshes.
    std::unique_ptr<AccelerationStructure> treeBLAS_;
    std::unique_ptr<AccelerationStructure> boxBLAS_;
    std::unique_ptr<AccelerationStructure> shellBLAS_;
    std::unique_ptr<SceneAccelerationStructure> sceneAS_;
    std::unique_ptr<HistoryBuffer> historyBuffer_;

    void spawnBoxes();
    void spawnTrees(const WaterGenerator::FloodField& waterField);
    void spawnExplosion(glm::vec3 position);
    void updateTrackMarks(float deltaTime);
    void fireProjectile();
    void updateProjectilesAndCollisions(float deltaTime);
    void buildAccelerationStructures();
    std::vector<AccelerationStructure::Instance> gatherRayTracingInstances() const;
    void recreateSwapchainDependentResources();
};
