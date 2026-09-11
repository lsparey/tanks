#include "CombatHud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace CombatHud {
namespace {
const glm::vec3 ink(.045f,.065f,.075f), border(.24f,.30f,.32f);
const glm::vec3 white(.91f,.94f,.91f), muted(.60f,.69f,.69f);
const glm::vec3 green(.58f,.83f,.53f), amber(.98f,.72f,.36f);

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
    void panel(float x,float y,float w,float h) {
        rect(x,y,w,h,ink,.86f);
        rect(x,y,w,1,border);
        rect(x,y,2,h,border);
    }
    void diamond(glm::vec2 p, float r, glm::vec3 color) {
        line(p+glm::vec2(0,-r),p+glm::vec2(r,0),1.5f,color);
        line(p+glm::vec2(r,0),p+glm::vec2(0,r),1.5f,color);
        line(p+glm::vec2(0,r),p+glm::vec2(-r,0),1.5f,color);
        line(p+glm::vec2(-r,0),p+glm::vec2(0,-r),1.5f,color);
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

    // The actual sandbox objective, with counts rather than an unlabelled row
    // of squares that could be mistaken for ammunition or health.
    c.panel(w/2-154,18,308,64);
    c.centered(complete ? "ALL TARGETS CLEARED" : "DESTROY THE CRATES",w/2,29,1.6f,complete ? green : muted);
    c.centered(std::to_string(destroyed)+" / "+std::to_string(s.targets.size())+" DESTROYED",w/2,49,2);
    c.rect(w/2-140,75,280,3,border);
    if (!s.targets.empty()) c.rect(w/2-140,75,280*float(destroyed)/s.targets.size(),3,green);

    c.panel(18,18,258,64);
    c.text("C  CAMERA",32,27,1.3f,muted);
    c.text(s.camera,32,43,1.8f);
    c.text(s.help ? "H  CLOSE CONTROLS" : "H  CONTROLS",32,66,1.3f,muted);

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
            c.line(p+d*9.f,p+d*19.f,4,ink);
            c.line(p+d*10.f,p+d*18.f,2,white);
        }
        c.rect(p.x-2,p.y-2,4,4,ink);
        c.rect(p.x-1,p.y-1,2,2,green);
    } else {
        c.panel(w/2-114,92,228,25);
        c.centered("GUN OUT OF VIEW",w/2,100,1.3f,amber);
    }

    // Hull-relative orientation makes Q/E traversal visible even when the
    // model is obscured by terrain or the player is using the free camera.
    float y=h-171;
    c.panel(18,y,258,135);
    c.text("VEHICLE",32,y+12,1.4f,muted);
    c.text("SPEED",32,y+38,1.3f,muted);
    c.text(fixed(std::abs(s.speed),1),32,y+55,3.2f);
    c.text(std::abs(s.speed)<.05f ? "STOPPED" : (s.speed<0 ? "REVERSE" : "FORWARD"),32,y+88,1.3f,green);
    c.text("ELEV "+fixed(glm::degrees(s.gunElevation))+" DEG",32,y+112,1.3f,muted);
    glm::vec2 tank(220,y+67);
    c.rect(tank.x-20,tank.y-23,8,45,border);
    c.rect(tank.x+12,tank.y-23,8,45,border);
    c.rect(tank.x-10,tank.y-26,20,48,muted);
    c.rect(tank.x-7,tank.y-8,14,16,ink);
    glm::vec2 gun(std::sin(s.turretYaw),-std::cos(s.turretYaw));
    c.line(tank,tank+gun*34.f,7,ink);
    c.line(tank,tank+gun*34.f,3,green);
    c.centered(fixed(std::remainder(glm::degrees(s.turretYaw),360.f))+" DEG",220,y+112,1.3f,green);

    c.panel(w/2-140,h-115,280,79);
    c.text("MAIN GUN",w/2-125,h-103,1.4f,muted);
    c.text("SINGLE SHOT",w/2-125,h-82,2,green);
    c.text("LMB / SPACE  FIRE",w/2-125,h-57,1.4f);
    c.centered("Q/E TRAVERSE   R/F ELEVATE",w/2,h-22,1.3f,muted);

    // Entire playable area, independent of camera position. The map shows
    // known static crate objectives, not invented enemy/spotting systems.
    float mapX=w-244,mapY=h-294,mapSize=202;
    c.panel(mapX,mapY,226,258);
    c.text("TACTICAL MAP",mapX+12,mapY+12,1.4f,muted);
    c.text("N",mapX+204,mapY+12,1.4f,green);
    glm::vec2 origin(mapX+12,mapY+34);
    c.rect(origin.x,origin.y,mapSize,mapSize,ink,.75f);
    for (int i=0;i<=4;++i) {
        float step=mapSize*i/4;
        c.line(origin+glm::vec2(step,0),origin+glm::vec2(step,mapSize),1,border);
        c.line(origin+glm::vec2(0,step),origin+glm::vec2(mapSize,step),1,border);
    }
    auto point=[&](glm::vec3 p){return origin+glm::vec2(7)+mapPosition(p,s.boundaryHalfExtent)*(mapSize-14);};
    for (const auto& target:s.targets) if (target.alive) c.diamond(point(target.position),3,amber);
    glm::vec2 player=point(s.position);
    glm::vec2 direction(s.forward.x,-s.forward.z);
    direction/=std::max(glm::length(direction),.001f);
    glm::vec2 right(-direction.y,direction.x);
    glm::vec2 tip=player+direction*6.f,left=player-direction*4.f-right*4.f,r=player-direction*4.f+right*4.f;
    c.hud.addTriangle(c.ndc(tip),c.ndc(left),c.ndc(r),green);
    glm::vec2 aimDirection(s.aimDirection.x,-s.aimDirection.z);
    aimDirection/=std::max(glm::length(aimDirection),.001f);
    glm::vec2 gunEnd=glm::clamp(player+aimDirection*18.f,origin,origin+glm::vec2(mapSize));
    c.line(player,gunEnd,2,white);
    c.diamond({mapX+16,mapY+247},3,amber);
    c.text(std::to_string(remaining)+" LEFT",mapX+25,mapY+242,1.2f,muted);
    int heading=static_cast<int>(std::round(headingDegrees(s.forward)))%360;
    c.text("HDG "+std::to_string(heading),mapX+135,mapY+242,1.2f,muted);

    if (s.diagnostics) {
        c.panel(w-274,18,256,80);
        c.text("F3  "+fixed(s.fps)+" FPS",w-260,29,1.5f);
        c.text(s.treeLod ? "F8 TREE LOD ON" : "F8 TREE LOD OFF",w-260,53,1.3f,muted);
        c.text(s.reflectionRays ? "F9 REFLECTIONS ON" : "F9 REFLECTIONS OFF",w-260,75,1.3f,muted);
    }
    if (s.help) {
        c.panel(18,100,312,235);
        c.text("CONTROLS",32,114,1.8f,green);
        const char* lines[]={"W/S       DRIVE / REVERSE","A/D       STEER","Q/E       TRAVERSE TURRET",
            "R/F       GUN UP / DOWN","LMB/SPACE FIRE SINGLE SHOT","C         CYCLE CAMERA",
            "ARROWS    MOVE FREE CAMERA","SPACE/CTRL CAMERA UP / DOWN","MOUSE     LOOK IN FREE CAMERA",
            "F3        DIAGNOSTICS","F12       SCREENSHOT","ESC       QUIT"};
        for (size_t i=0;i<std::size(lines);++i) c.text(lines[i],32,141+i*15,1.3f,i==0 ? white : muted);
    }
}
}
