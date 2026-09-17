#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "../render/HudGeometry.h"

// Shared palette and layout helper for everything drawn through
// HudGeometry (the combat HUD and the in-game pause menu), so both speak
// the same visual language. Layout is in logical pixels on a canvas that
// is never smaller than 1000x700 (see canvasScale), converted here to the
// renderer's +Y-up NDC.
namespace HudStyle {
inline const glm::vec3 scrim(.03f,.04f,.05f);
inline const glm::vec3 white(.95f,.96f,.97f), grey(.62f,.66f,.70f);
inline const glm::vec3 accent(.38f,.79f,.76f), warn(.96f,.56f,.36f);
// Mid-health amber and a fuel-gauge blue, distinct from accent/warn so the
// vitals panel's three gauges (health/fuel/ammo) stay visually distinct.
inline const glm::vec3 amber(.93f,.78f,.30f), fuelBlue(.45f,.70f,.98f);

// Logical-pixel scale for a viewport: the canvas is vp/scale, which this
// formula pins to at least 1000x700 for any real viewport (x-bound cases
// give exactly 1000 wide, y-bound exactly 700 tall), so fixed layouts that
// fit that box fit everywhere.
inline float canvasScale(glm::vec2 viewportPixels) {
    return std::min({viewportPixels.x/1000.f,viewportPixels.y/700.f,1.5f});
}

inline std::string fixed(float value, int decimals=0) {
    char buffer[32];
    std::snprintf(buffer,sizeof(buffer),"%.*f",decimals,value);
    return buffer;
}

// Width in logical pixels of `characters` glyphs at `pixel` scale -- the
// bitmap font's 6-cell advance, minus the trailing gap.
inline float textWidth(size_t characters, float pixel) { return (characters*6-1)*pixel; }
}

struct HudCanvas {
    HudGeometry& hud;
    glm::vec2 size;
    glm::vec2 ndc(glm::vec2 p) const { return {2*p.x/size.x-1,1-2*p.y/size.y}; }
    void rect(float x, float y, float w, float h, glm::vec3 color, float alpha=1) {
        hud.addQuad(ndc({x+w/2,y+h/2}),{w/size.x,h/size.y},color,alpha);
    }
    void text(std::string_view value, float x, float y, float pixel=1.5f, glm::vec3 color=HudStyle::white) {
        hud.addText(value,ndc({x,y}),{2*pixel/size.x,2*pixel/size.y},color);
    }
    void centered(std::string_view value, float x, float y, float pixel=1.5f, glm::vec3 color=HudStyle::white) {
        text(value,x-HudStyle::textWidth(value.size(),pixel)/2,y,pixel,color);
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
    void chip(float x,float y,float w,float h,float alpha=.42f) { rect(x,y,w,h,HudStyle::scrim,alpha); }
    void diamond(glm::vec2 p, float r, glm::vec3 color) {
        line(p+glm::vec2(0,-r),p+glm::vec2(r,0),1.2f,color);
        line(p+glm::vec2(r,0),p+glm::vec2(0,r),1.2f,color);
        line(p+glm::vec2(0,r),p+glm::vec2(-r,0),1.2f,color);
        line(p+glm::vec2(-r,0),p+glm::vec2(0,-r),1.2f,color);
    }
    // One bar cell: a dim backing plate plus a colored fill anchored to
    // whichever side keeps it losing charge from that side -- the low edge
    // for a normal bar, the opposite edge when mirrored (see chunkedBar).
    void barCell(float x,float y,float w,float h,float fraction,glm::vec3 color,bool mirrored) {
        rect(x,y,w,h,HudStyle::scrim,.65f);
        fraction=glm::clamp(fraction,0.f,1.f);
        if (fraction<=0) return;
        float fw=w*fraction;
        rect(mirrored ? x+w-fw : x,y,fw,h,color);
    }
    // Chunked gauge (health): `segments` equal cells with small gaps, filled
    // by `fraction` of the whole bar left-to-right, or right-to-left when
    // mirrored -- mirroring both the cell order and each cell's own fill
    // anchor so the opponent's bar reads as a true mirror image of the
    // player's. segments==1 degrades to one continuous bar (used for fuel).
    void chunkedBar(float x,float y,float w,float h,int segments,float fraction,glm::vec3 color,bool mirrored) {
        segments=std::max(segments,1);
        float gap=segments>1 ? 2.f : 0.f;
        float cellW=(w-gap*(segments-1))/segments;
        float filledUnits=glm::clamp(fraction,0.f,1.f)*segments;
        for (int i=0;i<segments;++i) {
            int idx=mirrored ? segments-1-i : i;
            float cellFrac=glm::clamp(filledUnits-float(idx),0.f,1.f);
            barCell(x+i*(cellW+gap),y,cellW,h,cellFrac,color,mirrored);
        }
    }
    // Ammo pips, styled like a car dash's shift-light row: `filled` lit
    // cells out of `total`, counting down as shells are spent -- mirrored
    // for the opponent so it lights from the opposite side.
    void pips(float x,float y,int total,int filled,bool mirrored,glm::vec3 color) {
        float pw=10,ph=12,gap=3;
        for (int i=0;i<total;++i) {
            int idx=mirrored ? total-1-i : i;
            float px=x+i*(pw+gap);
            bool lit=idx<filled;
            rect(px,y,pw,ph,HudStyle::scrim,.65f);
            rect(px+1.5f,y+1.5f,pw-3,ph-3,lit?color:HudStyle::grey,lit?1.f:.3f);
        }
    }
};
