#include "render/Mesh.h"
#include "scene/TreeGenerator.h"

#include <array>
#include <cmath>
#include <future>
#include <map>
#include <stdexcept>

namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
using Geometry = std::array<Mesh::Geometry, 7>;
Geometry build(const TreeGenerator::Tree& tree) {
    Geometry result;
    for (int lod = 0; lod < 3; ++lod) {
        result[lod*2] = Mesh::treeBarkGeometry(glm::vec3(1), tree, lod);
        result[lod*2+1] = Mesh::treeLeafGeometry(glm::vec3(1), tree, lod);
    }
    result[6] = Mesh::treeLeafGeometry(glm::vec3(1), tree, 3);
    return result;
}
bool inside(glm::vec3 p, const Mesh::Geometry& mesh) {
    const glm::vec3 ray = glm::normalize(glm::vec3(.173f,.317f,1));
    int hits = 0;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
        glm::vec3 e1 = mesh.vertices[mesh.indices[i+1]].position - a;
        glm::vec3 e2 = mesh.vertices[mesh.indices[i+2]].position - a;
        glm::vec3 h = glm::cross(ray,e2);
        float d = glm::dot(e1,h);
        if (std::abs(d) < 1e-10f) continue;
        glm::vec3 s = p-a;
        float u = glm::dot(s,h)/d;
        glm::vec3 q = glm::cross(s,e1);
        float v = glm::dot(ray,q)/d;
        float t = glm::dot(e2,q)/d;
        if (u>=0 && v>=0 && u+v<=1 && t>1e-7f) hits += d<0 ? 1 : -1;
    }
    return hits != 0;
}
}
int main() {
    std::array<TreeGenerator::Tree,3> trees;
    std::array<std::future<Geometry>,3> jobs;
    for (int i=0;i<3;++i) {
        trees[i] = TreeGenerator::generate(i+1,static_cast<TreeGenerator::Species>(i),1);
        // Exercise all mesh paths with a bounded sample of actual leaf
        // orientations; the full skeleton and its fine twigs remain present.
        trees[i].sprays.resize(64);
        jobs[i] = std::async(std::launch::async,[&,i]{ return build(trees[i]); });
    }
    for (int i=0;i<3;++i) {
        auto serial = build(trees[i]);
        auto parallel = jobs[i].get();
        for (int mesh=0;mesh<7;++mesh) {
            const auto& a=serial[mesh]; const auto& b=parallel[mesh];
            require(a.indices==b.indices && a.vertices.size()==b.vertices.size(), "Parallel build changed topology");
            for (size_t j=0;j<a.vertices.size();++j) {
                const auto& v=a.vertices[j]; const auto& w=b.vertices[j];
                require(v.position==w.position && v.normal==w.normal && v.color==w.color && v.uv==w.uv, "Parallel build changed a vertex");
                require(std::isfinite(glm::dot(v.position,v.position)) && std::abs(glm::length(v.normal)-1)<1e-4f, "Invalid mesh vertex");
            }
            for (auto index:a.indices) require(index<a.vertices.size(), "Invalid index");
        }
    }
    for (int species=0;species<3;++species) {
        auto tree=TreeGenerator::generate(7,static_cast<TreeGenerator::Species>(species),1);
        // Sample every bough, including the crown leader, without a full-size
        // asset build in the test suite.
        std::map<uint32_t,TreeGenerator::Tree> boughs;
        for (const auto& spray:tree.sprays) {
            auto& bough=boughs[spray.group];
            bough.species=tree.species;
            if (bough.sprays.size()<2) bough.sprays.push_back(spray);
        }
        tree.sprays.clear();
        for (const auto& [id,bough]:boughs)
            tree.sprays.insert(tree.sprays.end(),bough.sprays.begin(),bough.sprays.end());
        auto job=std::async(std::launch::async,[&]{return Mesh::treeFoliageGeometry(glm::vec3(1),tree);});
        auto grouped=Mesh::treeFoliageGeometry(glm::vec3(1),tree);
        auto parallel=job.get();
        require(grouped.groups.size()==boughs.size(), "Lost a bough");
        require(grouped.mesh.indices==parallel.mesh.indices, "Non-deterministic grouped topology");
        require(grouped.mesh.vertices.size()==parallel.mesh.vertices.size(), "Non-deterministic grouped vertices");
        for (size_t i=0;i<grouped.mesh.vertices.size();++i) {
            const auto& a=grouped.mesh.vertices[i]; const auto& b=parallel.mesh.vertices[i];
            require(a.position==b.position && a.normal==b.normal && a.uv==b.uv && a.color==b.color,
                    "Parallel grouped build changed a vertex");
        }
        uint32_t nextIndex=0;
        for (size_t i=0;i<grouped.groups.size();++i) {
            const auto& group=grouped.groups[i];
            const auto& other=parallel.groups[i];
            require(group.center==other.center && group.radius==other.radius && group.seed==other.seed,
                    "Non-deterministic bough metadata");
            for (int lod=0;lod<3;++lod) {
                const auto& range=group.levels[lod];
                auto standalone=Mesh::treeLeafGeometry(glm::vec3(1),boughs.at(group.seed),lod);
                require(range.firstIndex==nextIndex && range.indexCount==standalone.indices.size(),
                        "Overlapping or incomplete bough range");
                require(range.vertexOffset==0 && range.instanceCount==0 && range.firstInstance==0,
                        "Asset contains runtime draw state");
                for (uint32_t j=0;j<range.indexCount;++j) {
                    auto index=grouped.mesh.indices.at(range.firstIndex+j);
                    const auto& vertex=grouped.mesh.vertices.at(index);
                    require(vertex.position==standalone.vertices[standalone.indices[j]].position,
                            "Grouping moved foliage or included another bough");
                    require(glm::length(vertex.position-group.center) <= group.radius+1e-5f,
                            "Foliage outside bough culling bound");
                }
                nextIndex+=range.indexCount;
            }
        }
        require(nextIndex==grouped.mesh.indices.size(), "Unowned foliage geometry");
    }
    for (const auto& leaf:trees[2].sprays) {
        TreeGenerator::Tree one; one.species=TreeGenerator::Species::Oak; one.sprays={leaf};
        auto near=Mesh::treeLeafGeometry(glm::vec3(1),one,0);
        auto proxy=Mesh::treeLeafGeometry(glm::vec3(1),one,3);
        for (int lod=0;lod<3;++lod) {
            auto mesh=Mesh::treeLeafGeometry(glm::vec3(1),one,lod);
            for (size_t j=0;j<mesh.indices.size();j+=3) {
                const auto& a=mesh.vertices[mesh.indices[j]];
                const auto& b=mesh.vertices[mesh.indices[j+1]];
                const auto& c=mesh.vertices[mesh.indices[j+2]];
                auto face=glm::cross(b.position-a.position,c.position-a.position);
                require(glm::dot(face,a.normal+b.normal+c.normal)>0,"Inverted leaf face");
            }
        }
        for (const auto& vertex:proxy.vertices) require(inside(vertex.position,near),"Oak proxy outside leaf");
        for (size_t j=0;j<proxy.indices.size();j+=3) {
            glm::vec3 center(0);
            for(int k=0;k<3;++k) center+=proxy.vertices[proxy.indices[j+k]].position/3.f;
            require(inside(center,near),"Oak proxy face outside leaf");
        }
    }
}
