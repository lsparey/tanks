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
    // Power-up inventory text and the aim-assist trajectory line (see
    // PLAN.md's "Power-up crates"): structural checks only, matching this
    // file's existing style -- not a golden-pixel test.
    state.inventoryText="SHL:1 SPL:2 [ARMED DMG]";
    std::vector<glm::vec4> trajectory={
        {0,0,.5f,1}, {.1f,.05f,.6f,1}, {.2f,-.1f,.7f,1},
        {0,0,-.2f,-1},  // behind the camera -- must break the line, not draw a garbage segment
        {-.1f,.2f,.4f,1}, {-.2f,.3f,.5f,1},
    };
    state.trajectoryClip=trajectory;
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(!hud.vertices().empty() && hud.vertices().size()%3==0,"Trajectory geometry stays in complete triangles");
    for (auto& v:hud.vertices()) {
        require(std::isfinite(v.position.x) && std::isfinite(v.position.y),"Finite trajectory geometry");
        require(std::abs(v.position.x)<=1.001f && std::abs(v.position.y)<=1.001f,"Trajectory stays in viewport");
    }
    state.inventoryText={};
    state.trajectoryClip={};

    state.help=false;
    state.diagnostics=false;
    state.aimClip={0,0,0,1};
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    auto aliveVertices=hud.vertices().size();

    // Turn/health/fuel/shells panel (see PLAN.md's "Turn camera and match
    // HUD"): gated entirely behind matchActive, same pattern as
    // inventoryText, so free play's HUD must be byte-identical whether or
    // not these fields happen to hold stale values.
    require(!state.matchActive,"matchActive defaults to false");
    std::vector<HudGeometry::Vertex> baselineVertices(hud.vertices().begin(),hud.vertices().end());
    state.matchActive=true;
    state.opponentPresent=true;
    state.turnLabel="OPPONENT TURN";
    state.playerCombat={true,2.0f,3.0f,14.0f,20.0f,1,1};
    state.opponentCombat={false,0.0f,3.0f,0.0f,20.0f,0,1};
    // Both an armed inventory line and the match line at once is the
    // panel's tallest case (regression check for the anchor fix that keeps
    // its bottom edge from pushing past the canvas).
    state.inventoryText="SHL:1 DMG:1 SPL:2 AIM:1 [ARMED AIM]";
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(hud.vertices().size()>aliveVertices,"Match panel adds geometry when active");
    for (auto& v:hud.vertices()) {
        require(std::isfinite(v.position.x) && std::isfinite(v.position.y),"Finite match-panel geometry");
        require(std::abs(v.position.x)<=1.001f && std::abs(v.position.y)<=1.001f,"Match panel stays in viewport");
    }
    // Match start/over banners (see PLAN.md's "Match flow and deterministic
    // replay"): bigger, separate from turnLabel, shown briefly at match
    // start or persistently once isGameOver() -- same structural checks.
    state.showMatchStartBanner=true;
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(hud.vertices().size()>aliveVertices,"Match-start banner adds geometry when active");
    for (auto& v:hud.vertices()) {
        require(std::isfinite(v.position.x) && std::isfinite(v.position.y),"Finite match-start banner geometry");
        require(std::abs(v.position.x)<=1.001f && std::abs(v.position.y)<=1.001f,"Match-start banner stays in viewport");
    }
    state.showMatchStartBanner=false;
    state.showMatchOverBanner=true;
    state.matchOverText="OPPONENT WINS";
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(hud.vertices().size()>aliveVertices,"Match-over banner adds geometry when active");
    for (auto& v:hud.vertices()) {
        require(std::isfinite(v.position.x) && std::isfinite(v.position.y),"Finite match-over banner geometry");
        require(std::abs(v.position.x)<=1.001f && std::abs(v.position.y)<=1.001f,"Match-over banner stays in viewport");
    }

    state.matchActive=false;
    state.opponentPresent=false;
    state.turnLabel={};
    state.inventoryText={};
    state.showMatchStartBanner=false;
    state.showMatchOverBanner=false;
    state.matchOverText={};
    hud.begin();
    CombatHud::draw(hud,{1280,720},state);
    require(hud.vertices().size()==baselineVertices.size(),"Free play vertex count unaffected by match fields");
    for (size_t i=0;i<baselineVertices.size();++i) {
        require(baselineVertices[i].position==hud.vertices()[i].position &&
                baselineVertices[i].color==hud.vertices()[i].color,
                "Free play HUD geometry byte-identical with match fields present but inactive");
    }
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
