// Shared by visible geometry and the sun shadow pass.
vec3 bendTreePosition(vec3 position, vec3 bend) {
    float height = max(position.y, 0.0);
    return position + bend * height * height;
}
