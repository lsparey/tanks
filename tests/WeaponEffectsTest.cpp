#include "scene/WeaponEffects.h"
#include <stdexcept>

void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void finite(glm::mat4 m) {
    for (int i=0;i<4;++i) for (int j=0;j<4;++j)
        require(std::isfinite(m[i][j]),"Finite card transform including axial views");
}
int main() {
    using namespace WeaponEffects;
    Flash flash;
    finite(flash.matrix(glm::vec3(0,0,8)));
    flash.direction=glm::vec3(0,1,0);
    finite(flash.matrix(glm::vec3(0,8,0)));
    flash.update(.1f);
    require(flash.opacity()==0,"Flash expires quickly");
    Smoke smoke;
    smoke.velocity=glm::vec3(2,3,4);
    auto split=smoke;
    smoke.update(.3f);
    for (int i=0;i<30;++i) split.update(.01f);
    require(glm::length(smoke.position-split.position)<1e-5f,"Smoke drag is timestep independent");
    require(smoke.opacity()>0 && smoke.opacity()<.42f,"Bounded smoke alpha");
    finite(smoke.matrix(smoke.position));
    finite(smoke.matrix(smoke.position+glm::vec3(0,10,0)));
    Scorch scorch;
    scorch.update(30);
    require(std::abs(scorch.opacity()-.78f)<1e-6f,"Scorch persists after explosion");
    scorch.update(10);
    require(scorch.opacity()>0 && scorch.opacity()<.78f,"Scorch fades late in lifetime");
    std::vector<Scorch> marks;
    for (int i=0;i<100;++i) addBounded(marks,Scorch{glm::vec3(i,0,0)},kMaxScorches);
    require(marks.size()==16 && marks.front().position.x==84,"Oldest scorch evicted at capacity");
    update(marks,46);
    require(marks.empty(),"Expired marks removed");
    std::vector<Smoke> clouds;
    for (int i=0;i<100;++i) addBounded(clouds,Smoke{},kMaxSmoke);
    require(clouds.size()==48,"Smoke population bounded");
    update(clouds,2);
    require(clouds.empty(),"Smoke cleanup");
}
