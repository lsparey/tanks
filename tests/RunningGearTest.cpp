#include "scene/RunningGear.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(glm::mat4 a,glm::mat4 b,float tolerance=1e-4f) {
    for (int i=0;i<4;++i) for (int j=0;j<4;++j)
        if (std::abs(a[i][j]-b[i][j])>tolerance) return false;
    return true;
}
void checkTrackClearance(const ModelLoader::Result& model,const RunningGear::Rig& rig) {
    struct Bounds { glm::vec3 low{1000},high{-1000}; };
    std::map<std::string,Bounds> bounds;
    std::map<float,float> hullUnderside; // longitudinal station -> lowest hull vertex
    for (const auto& part : model.parts) for (const auto& v : part.vertices) {
        auto& b=bounds[part.meshName];
        b.low=glm::min(b.low,v.position); b.high=glm::max(b.high,v.position);
        if (part.meshName=="upper_hull") {
            auto [it,added]=hullUnderside.emplace(v.position.z,v.position.y);
            if (!added) it->second=std::min(it->second,v.position.y);
        }
    }
    // Sweep a full belt circuit, including phases between authored shoes.
    // Check imported geometry so regeneration cannot silently reintroduce
    // intersections, and retain the same path used by animated raster/RT.
    for (unsigned side=0;side<2;++side) {
        std::string prefix=side ? "right_" : "left_";
        const auto& path=rig.paths[side];
        for (int step=0;step<2048;++step) {
            auto frame=path.frame(step*path.perimeter/2048,rig.trackX[side]);
            Bounds shoe;
            for (const auto& v : rig.batches[0].mesh.vertices) {
                auto p=glm::vec3(frame*glm::vec4(v.position,1));
                shoe.low=glm::min(shoe.low,p); shoe.high=glm::max(shoe.high,p);
                auto next=hullUnderside.upper_bound(p.z);
                if (next!=hullUnderside.begin() && next!=hullUnderside.end()) {
                    auto previous=std::prev(next);
                    float t=(p.z-previous->first)/(next->first-previous->first);
                    float underside=glm::mix(previous->second,next->second,t);
                    require(p.y+.01f<underside,"Moving shoes clear upper hull shoulders");
                }
            }
            for (const auto& end : {"front_","rear_"}) {
                const auto& fender=bounds.at(prefix+end+"fender");
                if (shoe.high.z>=fender.low.z && shoe.low.z<=fender.high.z)
                    require(shoe.high.y+.01f<fender.low.y,"Moving shoes clear fender undersides");
                const auto& flap=bounds.at(prefix+end+"mudflap");
                require(shoe.high.z+.005f<flap.low.z || shoe.low.z-.005f>flap.high.z ||
                        shoe.high.y+.005f<flap.low.y,"Mud flaps clear the full track turn");
            }
        }
        float skirtOutside=std::max(std::abs(bounds.at(prefix+"skirt_3").low.x),
                                    std::abs(bounds.at(prefix+"skirt_3").high.x));
        for (const auto& [name,b] : bounds) {
            if (name.starts_with(prefix) && name.find("hub")!=std::string::npos)
                require(std::max(std::abs(b.low.x),std::abs(b.high.x))+.015f<skirtOutside,
                        "Wheel hubs are recessed behind the outer skirt surface");
        }
    }
}
int main() {
    auto model=ModelLoader::load(std::string(ASSET_ROOT)+"/assets/models/challenger2.obj");
    auto rig=RunningGear::extract(model);
    checkTrackClearance(model,rig);
    require(rig.batches.size()==5,"Five instanced gear draws");
    require(rig.movingNames.size()==176,"128 shoes plus 48 wheel components removed from static meshes");
    auto initial=rig.transforms(glm::mat4(1));
    size_t count=0;
    for (const auto& batch : initial) count+=batch.size();
    require(count==160,"160 matching raster/ray instances");
    // At rest every shared shoe must reconstruct the authored geometry,
    // including the mirrored side and shoes rounding the raised ends.
    for (size_t side=0;side<2;++side) for (size_t i=0;i<64;++i) {
        auto name=std::string(side ? "right_" : "left_")+"shoe_"+(i<10 ? "0" : "")+std::to_string(i);
        const ModelLoader::Part* authored=nullptr;
        for (const auto& part : model.parts) if (part.meshName==name) authored=&part;
        require(authored!=nullptr,"Authored shoe exists");
        for (const auto& v : rig.batches[0].mesh.vertices) {
            auto p=glm::vec3(initial[0][side*64+i]*glm::vec4(v.position,1));
            float nearest=1000;
            for (const auto& a : authored->vertices) nearest=std::min(nearest,glm::length(p-a.position));
            require(nearest<2e-4f,"Shared shoe fits authored belt at rest");
        }
    }
    for (unsigned s=0;s<2;++s) {
        const auto& path=rig.paths[s];
        require(path.perimeter>8 && path.perimeter<10,"Authored belt perimeter");
        require(close(path.frame(-.02,0),path.frame(path.perimeter-.02,0)),"Reverse wraps around seam");
        require(close(path.frame(.02,0),path.frame(path.perimeter+.02,0)),"Forward wraps around seam");
        for (int i=0;i<100;++i) {
            auto m=path.frame(i*path.perimeter/100,0);
            require(std::abs(glm::determinant(glm::mat3(m))-1)<1e-4f,"Shoe frames are proper rotations");
        }
    }
    // On the long lower run, forward drive moves a shoe backward relative
    // to the hull; rotating wheel contact points have the same sign.
    const auto& path=rig.paths[0];
    auto bottom=path.frame(1,0),back=path.frame(.99,0);
    require(back[3].z < bottom[3].z,"Bottom shoes oppose forward hull travel");
    auto wheelContact=glm::rotate(glm::mat4(1),.01f,glm::vec3(1,0,0))*glm::vec4(0,-.222f,0,1);
    require(wheelContact.z<0,"Forward wheels roll, not skate");
    rig.advance(.5,0);
    require(std::abs(rig.roadAngle[0]-rig.roadAngle[1])<1e-6,"Straight travel spins both sides equally");
    rig.advance(-.5,0);
    auto restored=rig.transforms(glm::mat4(1));
    for (size_t i=0;i<initial.size();++i) for (size_t j=0;j<initial[i].size();++j)
        require(close(initial[i][j],restored[i][j]),"Reverse retraces all animation phases");
    require(RunningGear::signedTravel(0,.2,rig.trackX[0])>0 &&
            RunningGear::signedTravel(0,.2,rig.trackX[1])<0,"Pivot produces opposing motion");
    require(RunningGear::signedTravel(1,.2,rig.trackX[0])>
            RunningGear::signedTravel(1,.2,rig.trackX[1]),"Outside track travels farther in turns");
    auto split=rig;
    rig.advance(1.2,.4);
    for (int i=0;i<120;++i) split.advance(.01,.4/120);
    auto one=rig.transforms(glm::mat4(1)),many=split.transforms(glm::mat4(1));
    for (size_t i=0;i<one.size();++i) for (size_t j=0;j<one[i].size();++j)
        require(close(one[i][j],many[i][j]),"Animation is distance-driven, not render-FPS driven");
    rig.advance(0,0);
    auto idle=rig.transforms(glm::mat4(1));
    for (size_t i=0;i<one.size();++i) for (size_t j=0;j<one[i].size();++j)
        require(close(one[i][j],idle[i][j]),"Stationary gear stays stationary");
    auto hull=glm::translate(glm::mat4(1),glm::vec3(3,4,5))*
              glm::rotate(glm::mat4(1),.3f,glm::vec3(0,0,1));
    auto world=rig.transforms(hull);
    require(close(world[0][0],hull*one[0][0]),"Gear follows suspended hull transform");
    for (int i=0;i<1000;++i) rig.advance(10000,-123);
    for (unsigned s=0;s<2;++s) {
        require(rig.beltPhase[s]>=0 && rig.beltPhase[s]<rig.paths[s].perimeter,"Long-run bounded belt phase");
        require(std::isfinite(rig.roadAngle[s]),"Long-run finite wheel angle");
    }
    require(!RunningGear::extract(ModelLoader::load(std::string(ASSET_ROOT)+"/assets/models/tank.x")).enabled(),
            "Legacy model remains a rigid fallback");
    std::erase_if(model.parts,[](const auto& part) { return part.meshName=="left_wheel_hub_2"; });
    bool failed=false;
    try { RunningGear::extract(model); } catch (const std::runtime_error&) { failed=true; }
    require(failed,"Incomplete named rigs fail instead of losing geometry");
}
