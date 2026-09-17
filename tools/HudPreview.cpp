// Standalone SVG of the same CPU geometry used in game; illustrative state.
#include "core/CombatHud.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    if (argc<2) { std::cerr<<"Usage: hud_preview output.svg [width height] [help]\n"; return 1; }
    float width=argc>2 ? std::stof(argv[2]) : 1280;
    float height=argc>3 ? std::stof(argv[3]) : 720;
    if (width<=0 || height<=0) return 1;
    std::vector<Box> boxes;
    for (int i=0;i<12;++i) {
        Box b;
        b.position={float(i%4)*38-60,0,float(i/4)*44-40};
        b.alive=i>3;
        b.size=1;
        boxes.push_back(b);
    }
    CombatHud::State state;
    state.targets=boxes;
    state.boundaryHalfExtent=100;
    state.speed=4.2f;
    state.turretYaw=.4f;
    state.gunElevation=.07f;
    state.aimDirection={.39f,.07f,.92f};
    state.aimClip={0,.1f,0,1};
    state.fps=60;
    state.gpuMs=7.3f;
    state.help=argc>4;
    state.diagnostics=state.help;
    // Illustrative --match state (see PLAN.md's "Turn camera and match
    // HUD") so this preview also covers the turn/health/fuel/shells panel.
    state.matchActive=true;
    state.opponentPresent=true;
    state.turnLabel="OPPONENT TURN";
    state.playerCombat={true,2.0f,3.0f,14.0f,20.0f,1,1};
    state.opponentCombat={true,3.0f,3.0f,20.0f,20.0f,1,1};
    state.inventoryText="SHL:1 DMG:1 SPL:2 AIM:1 [ARMED AIM]";
    std::string banner=argc>5 ? argv[5] : "";
    if (banner=="start") state.showMatchStartBanner=true;
    if (banner=="over") { state.showMatchOverBanner=true; state.matchOverText="YOU WIN"; }
    HudGeometry hud;
    CombatHud::draw(hud,{width,height},state);
    std::ofstream out(argv[1]);
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<width<<"\" height=\""<<height<<"\">\n"
       <<"<rect width=\"100%\" height=\"100%\" fill=\"#47564c\"/>\n";
    const auto& vertices=hud.vertices();
    for (size_t i=0;i<vertices.size();i+=3) {
        auto color=vertices[i].color;
        out<<"<polygon fill=\"rgb("<<int(color.r*255)<<","<<int(color.g*255)<<","<<int(color.b*255)
           <<")\" fill-opacity=\""<<color.a<<"\" points=\"";
        for (int j=0;j<3;++j) {
            auto p=vertices[i+j].position;
            out<<(p.x+1)*width/2<<","<<(1-p.y)*height/2<<" ";
        }
        out<<"\"/>\n";
    }
    out<<"</svg>\n";
    return out ? 0 : 1;
}
