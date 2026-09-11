#include "core/CombatHud.h"

#include <cmath>
#include <stdexcept>
#include <vector>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(glm::vec2 a, glm::vec2 b) { return glm::length(a-b)<1e-5f; }

int main() {
    require(close(CombatHud::mapPosition({0,0,0},100),{.5,.5}),"Map origin centered");
    require(close(CombatHud::mapPosition({100,0,100},100),{1,0}),"Map north/east convention");
    require(close(CombatHud::mapPosition({-200,8,-200},100),{0,1}),"Boundary markers clamp");
    require(close(CombatHud::mapPosition({0,0,0},0),{.5,.5}),"Empty boundary is safe");
    require(std::abs(CombatHud::headingDegrees({0,0,1}))<1e-5f,"North heading");
    require(std::abs(CombatHud::headingDegrees({-1,0,0})-270)<1e-5f,"West heading wraps");

    std::vector<Box> targets;
    for (int i=0;i<128;++i) {
        Box b;
        b.position={float(i-64),0,float(i%7)*10};
        b.size=1;
        targets.push_back(b);
    }
    CombatHud::State state;
    state.targets=targets;
    state.boundaryHalfExtent=100;
    state.position={100,0,-100};
    state.help=true;
    state.diagnostics=true;
    state.turretYaw=2.5f;
    state.speed=-3.7f;
    state.gunElevation=glm::radians(-10.f);
    state.forward={1,0,0};
    state.camera="FREE CAMERA";
    HudGeometry hud;
    for (auto extent : {glm::vec2(640,480),glm::vec2(1280,720),glm::vec2(3440,1440),glm::vec2(720,1280)}) {
        for (glm::vec4 clip : {glm::vec4(0,0,.5,1),glm::vec4(8,-9,1,1),glm::vec4(0,0,1,-1)}) {
            state.aimClip=clip;
            hud.begin();
            CombatHud::draw(hud,extent,state);
            require(!hud.vertices().empty() && hud.vertices().size()%3==0,"Complete HUD triangles");
            require(hud.vertices().size()<HudGeometry::kMaxVertices,"HUD fits upload capacity");
            for (auto& v:hud.vertices()) {
                require(std::isfinite(v.position.x) && std::isfinite(v.position.y),"Finite resized geometry");
                require(std::abs(v.position.x)<=1.001f && std::abs(v.position.y)<=1.001f,"HUD stays in viewport");
                require(v.color.a>=0 && v.color.a<=1,"Valid blend opacity");
            }
        }
    }
    state.help=false;
    state.diagnostics=false;
    state.aimClip={0,0,0,1};
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    auto aliveVertices=hud.vertices().size();
    for (auto& b:targets) b.alive=false;
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(hud.vertices().size()<aliveVertices,"Destroyed targets disappear from map");
    state.targets={};
    state.boundaryHalfExtent=0;
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(!hud.vertices().empty(),"No-target scene remains usable");
    hud.begin();
    CombatHud::draw(hud,{0,0},state);
    require(hud.vertices().empty(),"Minimized viewport emits no geometry");
    hud.addQuad({0,0},{.1,.1},{1,1,1});
    require(hud.vertices().front().color.a==1,"Existing menu quads remain opaque");
}
