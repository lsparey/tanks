#include "CombatHud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace CombatHud {
namespace {
const glm::vec3 scrim(.03f,.04f,.05f);
const glm::vec3 white(.95f,.96f,.97f), grey(.62f,.66f,.70f);
const glm::vec3 accent(.38f,.79f,.76f), warn(.96f,.56f,.36f);

// Layout in logical pixels, converted once here to the renderer's +Y-up NDC.
struct Canvas {
    HudGeometry& hud;
    glm::vec2 size;
    glm::vec2 ndc(glm::vec2 p) const { return {2*p.x/size.x-1,1-2*p.y/size.y}; }
    void rect(float x, float y, float w, float h, glm::vec3 color, float alpha=1) {
        hud.addQuad(ndc({x+w/2,y+h/2}),{w/size.x,h/size.y},color,alpha);
    }
    void text(std::string_view value, float x, float y, float pixel=1.5f, glm::vec3 color=white) {
        hud.addText(value,ndc({x,y}),{2*pixel/size.x,2*pixel/size.y},color);
    }
    void centered(std::string_view value, float x, float y, float pixel=1.5f, glm::vec3 color=white) {
        text(value,x-(value.size()*6-1)*pixel/2,y,pixel,color);
    }
    void line(glm::vec2 a, glm::vec2 b, float width, glm::vec3 color) {
        glm::vec2 d=b-a;
        float length=glm::length(d);
        if (length<.001f) return;
        glm::vec2 n=glm::vec2(-d.y,d.x)*(width*.5f/length);
        hud.addTriangle(ndc(a+n),ndc(a-n),ndc(b-n),color);
        hud.addTriangle(ndc(a+n),ndc(b-n),ndc(b+n),color);
    }
    // Borderless translucent backing keeps text legible over bright terrain.
    void chip(float x,float y,float w,float h,float alpha=.42f) { rect(x,y,w,h,scrim,alpha); }
    void diamond(glm::vec2 p, float r, glm::vec3 color) {
        line(p+glm::vec2(0,-r),p+glm::vec2(r,0),1.2f,color);
        line(p+glm::vec2(r,0),p+glm::vec2(0,r),1.2f,color);
        line(p+glm::vec2(0,r),p+glm::vec2(-r,0),1.2f,color);
        line(p+glm::vec2(-r,0),p+glm::vec2(0,-r),1.2f,color);
    }
};

std::string fixed(float value, int decimals=0) {
    char buffer[32];
    std::snprintf(buffer,sizeof(buffer),"%.*f",decimals,value);
    return buffer;
}
}

glm::vec2 mapPosition(glm::vec3 position, float boundaryHalfExtent) {
    float half=std::max(boundaryHalfExtent,.001f);
    return glm::clamp(glm::vec2(position.x,-position.z)/half,-1.f,1.f)*.5f+glm::vec2(.5f);
}

float headingDegrees(glm::vec3 direction) {
    return std::fmod(glm::degrees(std::atan2(direction.x,direction.z))+360.f,360.f);
}

void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& s) {
    if (viewportPixels.x<=0 || viewportPixels.y<=0) return;
    float scale=std::min({viewportPixels.x/1000.f,viewportPixels.y/700.f,1.5f});
    Canvas c{hud,viewportPixels/scale};
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

    // Speed plus gun angles in one compact cluster; turret state also shows
    // on the minimap aim line, so no vehicle diagram is needed.
    float y=h-64;
    std::string speed=fixed(std::abs(s.speed),1);
    c.chip(20,y,168,46);
    c.text(speed,34,y+7,2.6f,s.speed<-.05f ? warn : white);
    c.text("M/S",34+speed.size()*6*2.6f+4,y+7+7*(2.6f-1.2f),1.2f,grey);
    c.text("EL "+fixed(glm::degrees(s.gunElevation))+"  TRV "+fixed(std::remainder(glm::degrees(s.turretYaw),360.f)),34,y+32,1.1f,grey);

    // Entire playable area, independent of camera position. The map shows
    // known static crate objectives, not invented enemy/spotting systems.
    float mapSize=150,mapX=w-mapSize-30,mapY=h-mapSize-56;
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
    glm::vec2 aimDirection(s.aimDirection.x,-s.aimDirection.z);
    aimDirection/=std::max(glm::length(aimDirection),.001f);
    glm::vec2 gunEnd=glm::clamp(player+aimDirection*14.f,{mapX,mapY},{mapX+mapSize,mapY+mapSize});
    c.line(player,gunEnd,1.5f,white);
    int heading=static_cast<int>(std::round(headingDegrees(s.forward)))%360;
    c.text("HDG "+std::to_string(heading),mapX,mapY+mapSize+8,1.1f,grey);

    // FPS stays visible at all times; F3 expands it with the GPU frame time
    // (only measured while diagnostics are on) and the render toggles.
    if (s.diagnostics) {
        bool gpu=s.gpuMs>0;
        c.chip(w-206,20,186,gpu ? 72 : 58);
        c.text("FPS "+fixed(s.fps),w-192,27,1.4f);
        if (gpu) c.text("GPU "+fixed(s.gpuMs,1)+" MS",w-192,45,1.1f,grey);
        float ly=gpu ? 59 : 45;
        c.text(s.treeLod ? "F8 TREE LOD ON" : "F8 TREE LOD OFF",w-192,ly,1.1f,grey);
        c.text(s.reflectionRays ? "F9 REFLECTIONS ON" : "F9 REFLECTIONS OFF",w-192,ly+14,1.1f,grey);
    } else {
        c.chip(w-116,20,96,20,.35f);
        c.text("FPS "+fixed(s.fps),w-104,25,1.2f,grey);
    }
    if (s.help) {
        c.chip(20,68,300,224,.55f);
        c.text("CONTROLS",34,80,1.5f);
        const char* lines[]={"W/S       DRIVE / REVERSE","A/D       STEER","Q/E       TRAVERSE TURRET",
            "R/F       GUN UP / DOWN","LMB/SPACE FIRE SINGLE SHOT","C         CYCLE CAMERA",
            "ARROWS    MOVE FREE CAMERA","SPACE/CTRL CAMERA UP / DOWN","MOUSE     LOOK IN FREE CAMERA",
            "F3        DIAGNOSTICS","F12       SCREENSHOT","ESC       QUIT"};
        for (size_t i=0;i<std::size(lines);++i) c.text(lines[i],34,104+i*15,1.2f,grey);
    }
}
}
