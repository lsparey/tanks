#pragma once

#include <memory>

#include <vector>

#include "../render/CommandContext.h"
#include "../render/HudRenderer.h"
#include "../render/Mesh.h"
#include "../render/Pipeline.h"
#include "../render/Swapchain.h"
#include "../render/Texture.h"
#include "../render/VulkanContext.h"
#include "../scene/Box.h"
#include "../scene/Camera.h"
#include "../scene/ImpactEffect.h"
#include "../scene/InputManager.h"
#include "../scene/Projectile.h"
#include "../scene/Tank.h"
#include "../scene/Terrain.h"

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
    std::unique_ptr<Texture> whiteTexture_;
    VkDescriptorSet grassMaterialSet_ = VK_NULL_HANDLE;
    VkDescriptorSet whiteMaterialSet_ = VK_NULL_HANDLE;
    std::unique_ptr<Mesh> boxMesh_;
    std::unique_ptr<Mesh> shellMesh_;
    std::unique_ptr<Mesh> flashMesh_;
    std::vector<Box> boxes_;
    std::vector<Projectile> projectiles_;
    std::vector<ImpactEffect> impactEffects_;

    void spawnBoxes();
    void fireProjectile();
    void updateProjectilesAndCollisions(float deltaTime);
};
