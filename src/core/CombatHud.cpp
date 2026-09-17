#include "CombatHud.h"

#include "HudCanvas.h"
#include "PauseMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace CombatHud {
using namespace HudStyle;
using Canvas = HudCanvas;

glm::vec2 mapPosition(glm::vec3 position, float boundaryHalfExtent) {
    float half=std::max(boundaryHalfExtent,.001f);
    // +X (east) -> right (u=1); +Z (north) -> up/top of the panel (v=0), so
    // only z flips sign against the texture's top-down v axis.
    return glm::clamp(glm::vec2(position.x,-position.z)/half,-1.f,1.f)*.5f+glm::vec2(.5f);
}

float headingDegrees(glm::vec3 direction) {
    return std::fmod(glm::degrees(std::atan2(direction.x,direction.z))+360.f,360.f);
}

void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& s) {
    if (viewportPixels.x<=0 || viewportPixels.y<=0) return;
    Canvas c{hud,viewportPixels/canvasScale(viewportPixels)};
    float w=c.size.x,h=c.size.y;
    size_t remaining=std::count_if(s.targets.begin(),s.targets.end(),[](const Box& b){return b.alive;});
    size_t destroyed=s.targets.size()-remaining;
    bool complete=!s.targets.empty() && remaining==0;

    // Objective: count plus a slim progress bar; the caption keeps the
    // sandbox goal readable without the old framed banner.
    std::string count=std::to_string(destroyed)+"/"+std::to_string(s.targets.size());
    std::string_view caption=complete ? "CLEARED" : "CRATES";
    float countW=(count.size()*6-1)*2.f, captionW=(caption.size()*6-1)*1.2f, barW=150;
    c.chip(w/2-(std::max(countW+10+captionW,barW)+28)/2,14,std::max(countW+10+captionW,barW)+28,46);
    float x0=w/2-(countW+10+captionW)/2;
    c.text(count,x0,20,2,complete ? accent : white);
    c.text(caption,x0+countW+10,20+7*(2-1.2f),1.2f,grey);
    c.rect(w/2-barW/2,46,barW,2,grey,.3f);
    if (!s.targets.empty()) c.rect(w/2-barW/2,46,barW*float(destroyed)/s.targets.size(),2,accent);

    float camW=std::max((s.camera.size()*6-1)*1.4f,71.5f)+28;
    c.chip(20,20,camW,40);
    c.text(s.camera,34,26,1.4f);
    c.text(s.help ? "H  HIDE" : "H  CONTROLS",34,44,1.1f,grey);

    // Whose turn it is, plus both tanks' health/fuel/shells -- absent
    // entirely outside --match, same gating every other match-only element
    // here already uses. Placed below the reticle/"GUN OUT OF VIEW" status
    // rather than beside it (both centered at w/2) -- during the away
    // camera the player's own aim point is essentially never on screen, so
    // that message is showing almost the entire time this label needs to.
    if (s.matchActive && !s.turnLabel.empty()) {
        float labelW=std::max((s.turnLabel.size()*6-1)*1.3f,60.f)+28;
        c.chip(w/2-labelW/2,96,labelW,26);
        c.centered(s.turnLabel,w/2,102,1.3f,accent);
    }
    // Vitals panel shared by both combatants: chunked health, a fuel gauge
    // and ammo pips, laid out [label|bar|value] normally or mirrored to
    // [value|bar|label] for the opponent -- so the player's panel (bottom
    // left) and the opponent's (top right) read as facing mirror images,
    // beat-em-up style. Returns the panel height so the caller can stack
    // other elements against it without duplicating the layout math.
    constexpr float kPanelPadX=14,kPanelLabelW=42,kPanelValueW=64,kPanelGapCol=8,
                    kPanelRowH=13,kPanelGapY=5,kPanelPadY=9,kPanelHeaderH=15;
    auto combatantPanel=[&](float x,float y,float panelW,bool mirrored,std::string_view header,
                             const State::CombatantHud& cs)->float {
        float barW=panelW-kPanelPadX*2-kPanelLabelW-kPanelValueW-kPanelGapCol*2;
        float headerH=header.empty() ? 0.f : kPanelHeaderH;
        float panelH=headerH+kPanelPadY*2+kPanelRowH*3+kPanelGapY*2;
        c.chip(x,y,panelW,panelH);
        c.rect(x,y,panelW,1.5f,accent,.55f);          // dash-trim top edge
        c.line({x,y+10},{x+10,y},1.2f,accent);          // angled corner cut
        glm::vec3 valueColor=cs.alive?white:warn;
        float ry=y+kPanelPadY;
        if (!header.empty()) { c.text(header,x+kPanelPadX,y+5,1.15f,grey); ry=y+headerH+kPanelPadY; }

        int healthSegments=std::max(1,int(std::round(cs.healthMax)));
        float healthFrac=cs.healthMax>0 ? cs.health/cs.healthMax : 0.f;
        float fuelFrac=cs.fuelCapacity>0 ? cs.fuelRemaining/cs.fuelCapacity : 0.f;
        int shellTotal=std::clamp(std::max(cs.shellsRemaining,cs.shellsPerTurn),1,6);
        glm::vec3 healthColor=healthFrac<=1.f/3.f ? warn : (healthFrac<=2.f/3.f ? amber : accent);
        glm::vec3 fuelColor=fuelFrac<=.25f ? warn : fuelBlue;

        float leftColX=x+kPanelPadX;
        float leftColW=mirrored?kPanelValueW:kPanelLabelW;
        float barX=leftColX+leftColW+kPanelGapCol;
        float rightColX=barX+barW+kPanelGapCol;
        auto label=[&](std::string_view text){ c.text(text,mirrored?rightColX:leftColX,ry+1,1.0f,grey); };
        auto value=[&](std::string text){ c.text(text,mirrored?leftColX:rightColX,ry+1,.95f,valueColor); };

        label("HULL"); value(fixed(cs.health,1)+"/"+fixed(cs.healthMax,1));
        c.chunkedBar(barX,ry-2,barW,kPanelRowH-2,healthSegments,healthFrac,healthColor,mirrored);
        ry+=kPanelRowH+kPanelGapY;

        label("FUEL"); value(fixed(cs.fuelRemaining)+"/"+fixed(cs.fuelCapacity));
        c.chunkedBar(barX,ry-2,barW,kPanelRowH-2,1,fuelFrac,fuelColor,mirrored);
        ry+=kPanelRowH+kPanelGapY;

        label("AMMO"); value(std::to_string(cs.shellsRemaining)+"/"+std::to_string(cs.shellsPerTurn));
        c.pips(barX,ry-2,shellTotal,cs.shellsRemaining,mirrored,accent);

        return panelH;
    };
    constexpr float kVitalsPanelW=320;

    // Big center-screen banner (match start / match over) -- placed well
    // above the reticle/turn-label cluster (which occupies roughly y 14 to
    // 122) so it never overlaps those fixed-position elements; a dynamic
    // aim reticle happening to land nearby is an accepted, pre-existing
    // tolerance in this HUD (it can already overlap other panels too).
    if (s.matchActive && (s.showMatchStartBanner || s.showMatchOverBanner)) {
        std::string_view caption = s.showMatchStartBanner ? "MATCH START" : s.matchOverText;
        float by=h*0.32f;
        float captionW=std::max((caption.size()*6-1)*2.2f,60.f)+32;
        float bannerH = s.showMatchStartBanner ? 44.f : 68.f;
        c.chip(w/2-captionW/2,by,captionW,bannerH,.55f);
        c.centered(caption,w/2,by+13,2.2f,accent);
        if (s.showMatchOverBanner) c.centered("PRESS N TO RESTART",w/2,by+42,1.2f,grey);
    }

    // Split reticle leaves the target visible. The point is the existing
    // unjittered gun-ray projection, not a hit/penetration prediction.
    bool onScreen=s.aimClip.w>.01f;
    glm::vec2 aim(0);
    if (onScreen) {
        aim=glm::vec2(s.aimClip)/s.aimClip.w;
        onScreen=std::abs(aim.x)<.94f && std::abs(aim.y)<.84f;
    }
    if (onScreen) {
        glm::vec2 p((aim.x+1)*w/2,(1-aim.y)*h/2);
        for (glm::vec2 d : {glm::vec2(1,0),glm::vec2(-1,0),glm::vec2(0,1),glm::vec2(0,-1)}) {
            c.line(p+d*8.f,p+d*16.f,2.6f,scrim);
            c.line(p+d*8.5f,p+d*15.5f,1.2f,white);
        }
        c.rect(p.x-1.5f,p.y-1.5f,3,3,scrim);
        c.rect(p.x-1,p.y-1,2,2,accent);
    } else {
        c.chip(w/2-72,66,144,18,.5f);
        c.centered("GUN OUT OF VIEW",w/2,71,1.1f,warn);
    }

    // Aim-assist predicted trajectory (see PLAN.md's "Power-up crates"):
    // connect consecutive projected points that are actually in front of
    // the camera, same clip-space-to-screen conversion the reticle above
    // uses. A point behind the camera (w <= 0) breaks the line rather than
    // producing a garbage segment across the screen.
    std::optional<glm::vec2> previousTrajectoryPoint;
    for (const auto& clip : s.trajectoryClip) {
        if (clip.w <= .01f) { previousTrajectoryPoint.reset(); continue; }
        glm::vec2 ndcPoint = glm::vec2(clip) / clip.w;
        glm::vec2 screenPoint((ndcPoint.x+1)*w/2,(1-ndcPoint.y)*h/2);
        if (previousTrajectoryPoint) c.line(*previousTrajectoryPoint, screenPoint, 1.6f, accent);
        previousTrajectoryPoint = screenPoint;
    }

    // Speed plus gun angles in one compact cluster, centered at the bottom
    // of the screen between the two vitals panels below -- a car-dash-style
    // speedo sitting between the driver's and opponent's gauge clusters. y
    // stays h-80 (unchanged) whether or not the inventory line below it is
    // showing, so free play's HUD height never moves, only its x shifted
    // from the left edge to centered.
    std::string speed=fixed(std::abs(s.speed),1);
    int extraLines=s.inventoryText.empty()?0:1;
    float y=h-80;
    float telemetryX=w/2-104;
    c.chip(telemetryX,y,208,62.f+16.f*extraLines);
    c.text(speed,telemetryX+14,y+7,2.6f,s.speed<-.05f ? warn : white);
    c.text("M/S",telemetryX+14+speed.size()*6*2.6f+4,y+7+7*(2.6f-1.2f),1.2f,grey);
    c.text("EL "+fixed(glm::degrees(s.gunElevation))+"  TRV "+fixed(std::remainder(glm::degrees(s.turretYaw),360.f)),telemetryX+14,y+32,1.1f,grey);
    c.text("PWR "+fixed(s.shotPower*100.f)+"%",telemetryX+14,y+48,1.1f,grey);
    // Power-up inventory (see PLAN.md's "Power-up crates") -- only present
    // under --match with something held; pre-formatted by the caller so
    // this stays a dumb renderer with no MatchState/PowerUpType dependency.
    if (!s.inventoryText.empty()) c.text(std::string(s.inventoryText),telemetryX+14,y+64,0.95f,accent);

    // Both combatants' vitals panels sit flush along the bottom edge --
    // player bottom-left, opponent bottom-right (mirrored), the telemetry
    // cluster above centered between them. Both carry a header ("PLAYER"/
    // "OPPONENT") now, so they're the same height and can share one
    // bottom-anchored y instead of each computing its own.
    if (s.matchActive) {
        constexpr float kVitalsBottom=18.f;
        float panelH=kPanelHeaderH+kPanelPadY*2+kPanelRowH*3+kPanelGapY*2;
        float panelY=h-kVitalsBottom-panelH;
        combatantPanel(20,panelY,kVitalsPanelW,false,"PLAYER",s.playerCombat);
        if (s.opponentPresent) {
            combatantPanel(w-kVitalsPanelW-20,panelY,kVitalsPanelW,true,"OPPONENT",s.opponentCombat);
        }
    }

    // Entire playable area, independent of camera position. The map shows
    // known static crate objectives, not invented enemy/spotting systems.
    // Anchored top-right (freeing up the bottom row for the vitals panels);
    // the diagnostics/FPS chip below reads its own y off this one's bottom.
    float mapSize=150,mapX=w-mapSize-30,mapY=20;
    c.chip(mapX-10,mapY-10,mapSize+20,mapSize+42);
    c.rect(mapX,mapY,mapSize,mapSize,scrim,.5f);
    c.rect(mapX,mapY,mapSize,1,grey,.4f);
    c.rect(mapX,mapY+mapSize-1,mapSize,1,grey,.4f);
    c.rect(mapX,mapY,1,mapSize,grey,.4f);
    c.rect(mapX+mapSize-1,mapY,1,mapSize,grey,.4f);
    c.centered("N",mapX+mapSize/2,mapY+4,1.1f,grey);
    auto point=[&](glm::vec3 p){return glm::vec2(mapX+5,mapY+5)+mapPosition(p,s.boundaryHalfExtent)*(mapSize-10);};
    for (const auto& target:s.targets) if (target.alive) c.diamond(point(target.position),2.5f,warn);
    glm::vec2 player=point(s.position);
    glm::vec2 direction(s.forward.x,-s.forward.z);
    direction/=std::max(glm::length(direction),.001f);
    glm::vec2 right(-direction.y,direction.x);
    c.hud.addTriangle(c.ndc(player+direction*6.f),c.ndc(player-direction*4.f-right*4.f),
                      c.ndc(player-direction*4.f+right*4.f),accent);
    // Same +X east / -Z north convention as mapPosition and the heading
    // arrow above -- an earlier version negated x too, mirroring the gun
    // line east/west so it never lined up with the crate markers.
    glm::vec2 aimDirection(s.aimDirection.x,-s.aimDirection.z);
    aimDirection/=std::max(glm::length(aimDirection),.001f);
    glm::vec2 gunEnd=glm::clamp(player+aimDirection*14.f,{mapX,mapY},{mapX+mapSize,mapY+mapSize});
    c.line(player,gunEnd,1.5f,white);
    int heading=static_cast<int>(std::round(headingDegrees(s.forward)))%360;
    c.text("HDG "+std::to_string(heading),mapX,mapY+mapSize+8,1.1f,grey);

    // FPS stays visible at all times; F3 expands it with the GPU frame time
    // (only measured while diagnostics are on) and the render toggles.
    // Stacked below the minimap (now top-right) with a fixed 10px gap.
    float diagY=mapY+mapSize+42;
    if (s.diagnostics) {
        bool gpu=s.gpuMs>0;
        c.chip(w-206,diagY,186,gpu ? 72 : 58);
        c.text("FPS "+fixed(s.fps),w-192,diagY+7,1.4f);
        if (gpu) c.text("GPU "+fixed(s.gpuMs,1)+" MS",w-192,diagY+25,1.1f,grey);
        float ly=diagY+(gpu ? 39 : 25);
        c.text(s.treeLod ? "F8 TREE LOD ON" : "F8 TREE LOD OFF",w-192,ly,1.1f,grey);
        c.text(s.reflectionRays ? "F9 REFLECTIONS ON" : "F9 REFLECTIONS OFF",w-192,ly+14,1.1f,grey);
    } else {
        c.chip(w-116,diagY,96,20,.35f);
        c.text("FPS "+fixed(s.fps),w-104,diagY+5,1.2f,grey);
    }
    if (s.help) {
        // Same table as the pause menu's Controls page (see
        // PauseMenu::controlLines); free-camera lines are omitted under
        // --match, where C never reaches the free camera.
        auto lines=PauseMenu::controlLines();
        size_t shown=0;
        for (const auto& line : lines) if (!(s.matchActive && line.freeCameraOnly)) ++shown;
        c.chip(20,68,300,44+15.f*shown,.55f);
        c.text("CONTROLS",34,80,1.5f);
        size_t row=0;
        for (const auto& line : lines) {
            if (s.matchActive && line.freeCameraOnly) continue;
            c.text(line.key,34,104+row*15,1.2f,grey);
            c.text(line.action,34+11*6*1.2f,104+row*15,1.2f,grey);
            ++row;
        }
    }
}
}
