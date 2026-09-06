#include "RunningGear.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace RunningGear {
namespace {
constexpr double tau = 6.2831853071795864769;
struct Bounds {
    glm::vec3 low{1e9f}, high{-1e9f};
    glm::vec3 centre() const { return (low+high)*.5f; }
};
Bounds bounds(const ModelLoader::Part& mesh) {
    Bounds b;
    for (const auto& v : mesh.vertices) {
        b.low = glm::min(b.low,v.position);
        b.high = glm::max(b.high,v.position);
    }
    return b;
}
void append(ModelLoader::Part& out, const ModelLoader::Part& in, const glm::mat4& transform) {
    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    out.materialName = in.materialName;
    for (auto v : in.vertices) {
        v.position = glm::vec3(transform*glm::vec4(v.position,1));
        v.normal = glm::normalize(glm::mat3(transform)*v.normal);
        out.vertices.push_back(v);
    }
    for (auto i : in.indices) out.indices.push_back(base+i);
}
}

double wrap(double value, double period) {
    double result = std::fmod(value,period);
    return result < 0 ? result+period : result;
}
double signedTravel(double longitudinalTravel, double yawTravel, double localX) {
    // omega(+Y) cross offset(+X) gives longitudinal velocity toward -Z.
    return longitudinalTravel-yawTravel*localX;
}
Path beltPath(const ModelLoader::Part& belt) {
    std::vector<glm::vec2> candidates;
    for (const auto& v : belt.vertices) candidates.emplace_back(v.position.z,v.position.y);
    auto less = [](glm::vec2 a, glm::vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); };
    std::sort(candidates.begin(),candidates.end(),less);
    candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
    auto cross = [](glm::vec2 a,glm::vec2 b,glm::vec2 c) {
        glm::vec2 u=b-a,v=c-a; return static_cast<double>(u.x)*v.y-static_cast<double>(u.y)*v.x;
    };
    std::vector<glm::vec2> lower,upper;
    auto push = [&](auto& chain,glm::vec2 p) {
        while (chain.size() >= 2 && cross(chain[chain.size()-2],chain.back(),p) <= 0)
            chain.pop_back();
        chain.push_back(p);
    };
    for (auto p : candidates) push(lower,p);
    for (auto it=candidates.rbegin();it!=candidates.rend();++it) push(upper,*it);
    if (lower.size()<2 || upper.size()<2) throw std::runtime_error("Degenerate track belt");
    lower.pop_back(); upper.pop_back();
    Path path;
    path.points=lower;
    path.points.insert(path.points.end(),upper.begin(),upper.end());
    for (size_t i=0;i<path.points.size();++i) {
        double length=glm::length(path.points[(i+1)%path.points.size()]-path.points[i]);
        path.lengths.push_back(length); path.perimeter+=length;
    }
    return path;
}
glm::mat4 Path::frame(double distance,float x) const {
    distance=wrap(distance,perimeter);
    for (size_t i=0;i<points.size();++i) {
        if (distance <= lengths[i] || i+1 == points.size()) {
            glm::vec2 tangent=(points[(i+1)%points.size()]-points[i])/static_cast<float>(lengths[i]);
            glm::vec2 p=points[i]+tangent*static_cast<float>(distance);
            // Local +Y is the outer normal; +Z is the transverse shoe's
            // length axis. This is a proper rotation, including on the top run.
            return glm::mat4(glm::vec4(1,0,0,0),glm::vec4(0,-tangent.x,tangent.y,0),
                             glm::vec4(0,-tangent.y,-tangent.x,0),glm::vec4(x,p.y,p.x,1));
        }
        distance-=lengths[i];
    }
    return glm::mat4(1);
}

Rig extract(const ModelLoader::Result& model) {
    std::map<std::string,const ModelLoader::Part*> parts;
    for (const auto& part : model.parts) parts[part.meshName]=&part;
    if (!parts.contains("right_track_belt") || !parts.contains("left_track_belt")) return {};
    Rig rig;
    auto get = [&](const std::string& name) -> const ModelLoader::Part& {
        auto it=parts.find(name);
        if (it==parts.end()) throw std::runtime_error("Incomplete running gear: "+name);
        return *it->second;
    };
    rig.batches.resize(5); // shoes, road tyres, road discs/hubs, end tyres, end discs/hubs
    rig.batches[0].shoes=true;
    for (unsigned side=0;side<2;++side) {
        std::string prefix=side ? "right_" : "left_";
        const auto& belt=get(prefix+"track_belt");
        rig.paths[side]=beltPath(belt);
        rig.trackX[side]=bounds(belt).centre().x;
        for (unsigned i=0;i<64;++i) {
            std::string name=prefix+"shoe_"+(i<10 ? "0" : "")+std::to_string(i);
            get(name); rig.movingNames.insert(name);
            double phase=(i+.5)*rig.paths[side].perimeter/64;
            rig.batches[0].placements.push_back({side,{rig.trackX[side],0,0},0,phase});
            if (side==1 && i==0)
                append(rig.batches[0].mesh,get(name),glm::inverse(rig.paths[side].frame(phase,rig.trackX[side])));
        }
        auto wheel = [&](const std::string& tyre,const std::string& disc,const std::string& hub,size_t batch) {
            Bounds b=bounds(get(prefix+tyre));
            Placement placement{side,b.centre(),(b.high.y-b.low.y)*.5f,0};
            rig.batches[batch].placements.push_back(placement);
            rig.batches[batch+1].placements.push_back(placement);
            for (const auto& suffix : {tyre,disc,hub}) rig.movingNames.insert(prefix+suffix);
            if (side==1 && rig.batches[batch].mesh.vertices.empty()) {
                auto local=glm::translate(glm::mat4(1),-placement.centre);
                append(rig.batches[batch].mesh,get(prefix+tyre),local);
                append(rig.batches[batch+1].mesh,get(prefix+disc),local);
                append(rig.batches[batch+1].mesh,get(prefix+hub),local);
            } else { get(prefix+disc); get(prefix+hub); }
        };
        for (unsigned i=1;i<=6;++i) {
            auto suffix=std::to_string(i);
            wheel("road_wheel_"+suffix,"wheel_disc_"+suffix,"wheel_hub_"+suffix,1);
        }
        for (const auto& kind : {std::string("idler"),std::string("sprocket")})
            wheel(kind,kind+"_disc",kind+"_hub",3);
    }
    return rig;
}

void Rig::advance(double longitudinalTravel,double yawTravel) {
    if (!enabled()) return;
    for (unsigned side=0;side<2;++side) {
        double travel=signedTravel(longitudinalTravel,yawTravel,trackX[side]);
        // Forward hull travel sends the contacting bottom run backward.
        beltPhase[side]=wrap(beltPhase[side]-travel,paths[side].perimeter);
        roadAngle[side]=wrap(roadAngle[side]+travel/batches[1].placements.front().radius,tau);
        endAngle[side]=wrap(endAngle[side]+travel/batches[3].placements.front().radius,tau);
    }
}
std::vector<std::vector<glm::mat4>> Rig::transforms(const glm::mat4& hull) const {
    std::vector<std::vector<glm::mat4>> result(batches.size());
    for (size_t i=0;i<batches.size();++i) {
        result[i].reserve(batches[i].placements.size());
        for (const auto& p : batches[i].placements) {
            glm::mat4 local;
            if (batches[i].shoes) local=paths[p.side].frame(p.phase+beltPhase[p.side],p.centre.x);
            else {
                float angle=static_cast<float>(i<3 ? roadAngle[p.side] : endAngle[p.side]);
                local=glm::translate(glm::mat4(1),p.centre)*
                      glm::rotate(glm::mat4(1),angle,glm::vec3(1,0,0))*
                      glm::rotate(glm::mat4(1),p.side ? 0.0f : static_cast<float>(tau*.5),glm::vec3(0,1,0));
            }
            result[i].push_back(hull*local);
        }
    }
    return result;
}
} // namespace RunningGear
