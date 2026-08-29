#include "ModelLoader.h"

#include <stdexcept>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

glm::vec3 toGlm(const aiVector3D& v) { return {v.x, v.y, v.z}; }

glm::mat4 toGlm(const aiMatrix4x4& m) {
    // Assimp matrices are row-major; glm::mat4's constructor here takes
    // column vectors, so this is the transpose of Assimp's layout.
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4,
                      m.b4, m.c4, m.d4);
}

glm::vec3 materialDiffuseColor(const aiScene* scene, unsigned int materialIndex) {
    if (materialIndex >= scene->mNumMaterials) return {0.6f, 0.6f, 0.6f};
    aiColor3D color(0.6f, 0.6f, 0.6f);
    scene->mMaterials[materialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, color);
    return {color.r, color.g, color.b};
}

void processNode(const aiScene* scene, const aiNode* node, const glm::mat4& parentTransform,
                  ModelLoader::Result& result) {
    glm::mat4 nodeTransform = parentTransform * toGlm(node->mTransformation);
    glm::mat3 normalMatrix = glm::mat3(nodeTransform);

    for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];
        glm::vec3 color = materialDiffuseColor(scene, mesh->mMaterialIndex);

        uint32_t baseVertex = static_cast<uint32_t>(result.vertices.size());
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            glm::vec3 position = glm::vec3(nodeTransform * glm::vec4(toGlm(mesh->mVertices[v]), 1.0f));
            glm::vec3 normal = mesh->HasNormals()
                                    ? glm::normalize(normalMatrix * toGlm(mesh->mNormals[v]))
                                    : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec2 uv = mesh->HasTextureCoords(0)
                               ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                               : glm::vec2(0.0f);
            result.vertices.push_back({position, normal, color, uv});
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; ++idx) {
                result.indices.push_back(baseVertex + face.mIndices[idx]);
            }
        }
    }

    for (unsigned int c = 0; c < node->mNumChildren; ++c) {
        processNode(scene, node->mChildren[c], nodeTransform, result);
    }
}

}  // namespace

ModelLoader::Result ModelLoader::load(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error("failed to load model '" + path + "': " + importer.GetErrorString());
    }

    Result result;
    processNode(scene, scene->mRootNode, glm::mat4(1.0f), result);
    return result;
}
