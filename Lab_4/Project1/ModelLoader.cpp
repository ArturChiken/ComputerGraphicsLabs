#include "ModelLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace
{
    XMMATRIX AiToXm(const aiMatrix4x4& matrix)
    {
        return XMMATRIX(
            matrix.a1, matrix.a2, matrix.a3, matrix.a4,
            matrix.b1, matrix.b2, matrix.b3, matrix.b4,
            matrix.c1, matrix.c2, matrix.c3, matrix.c4,
            matrix.d1, matrix.d2, matrix.d3, matrix.d4);
    }

    void ParseNode(
        const aiScene* scene,
        const aiNode* node,
        const XMMATRIX& parentTransform,
        std::vector<Vertex>& vertices,
        std::vector<uint32_t>& indices,
        std::vector<Submesh>& submeshes)
    {
        const XMMATRIX nodeTransform = AiToXm(node->mTransformation) * parentTransform;
        const XMMATRIX normalTransform = XMMatrixTranspose(XMMatrixInverse(nullptr, nodeTransform));

        for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            const uint32_t startIndex = static_cast<uint32_t>(indices.size());

            vertices.reserve(vertices.size() + mesh->mNumVertices);
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
            {
                Vertex vertex{};

                XMVECTOR position = XMVectorSet(
                    mesh->mVertices[i].x,
                    mesh->mVertices[i].y,
                    mesh->mVertices[i].z,
                    1.0f);
                XMStoreFloat3(&vertex.position, XMVector3TransformCoord(position, nodeTransform));

                if (mesh->HasNormals())
                {
                    XMVECTOR normal = XMVectorSet(
                        mesh->mNormals[i].x,
                        mesh->mNormals[i].y,
                        mesh->mNormals[i].z,
                        0.0f);
                    XMStoreFloat3(
                        &vertex.normal,
                        XMVector3Normalize(XMVector3TransformNormal(normal, normalTransform)));
                }

                if (mesh->HasTangentsAndBitangents())
                {
                    XMVECTOR tangent = XMVectorSet(
                        mesh->mTangents[i].x,
                        mesh->mTangents[i].y,
                        mesh->mTangents[i].z,
                        0.0f);
                    XMVECTOR bitangent = XMVectorSet(
                        mesh->mBitangents[i].x,
                        mesh->mBitangents[i].y,
                        mesh->mBitangents[i].z,
                        0.0f);
                    XMStoreFloat3(
                        &vertex.tangent,
                        XMVector3Normalize(XMVector3TransformNormal(tangent, normalTransform)));
                    XMStoreFloat3(
                        &vertex.bitangent,
                        XMVector3Normalize(XMVector3TransformNormal(bitangent, normalTransform)));
                }

                if (mesh->HasTextureCoords(0))
                {
                    vertex.texcoord.x = mesh->mTextureCoords[0][i].x;
                    vertex.texcoord.y = 1.0f - mesh->mTextureCoords[0][i].y;
                }

                vertices.push_back(vertex);
            }

            for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
            {
                const aiFace& face = mesh->mFaces[faceIndex];
                for (unsigned int i = 0; i < face.mNumIndices; ++i)
                    indices.push_back(baseVertex + face.mIndices[i]);
            }

            Submesh submesh;
            submesh.IndexStart = startIndex;
            submesh.IndexCount = static_cast<uint32_t>(indices.size()) - startIndex;
            submesh.MaterialName = "Earth";
            submeshes.push_back(submesh);
        }

        for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        {
            ParseNode(
                scene,
                node->mChildren[childIndex],
                nodeTransform,
                vertices,
                indices,
                submeshes);
        }
    }

    void NormalizeEarth(std::vector<Vertex>& vertices)
    {
        if (vertices.empty())
            return;

        XMFLOAT3 minimum(
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)());
        XMFLOAT3 maximum(
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)());

        for (const Vertex& vertex : vertices)
        {
            minimum.x = (std::min)(minimum.x, vertex.position.x);
            minimum.y = (std::min)(minimum.y, vertex.position.y);
            minimum.z = (std::min)(minimum.z, vertex.position.z);
            maximum.x = (std::max)(maximum.x, vertex.position.x);
            maximum.y = (std::max)(maximum.y, vertex.position.y);
            maximum.z = (std::max)(maximum.z, vertex.position.z);
        }

        const XMFLOAT3 center(
            0.5f * (minimum.x + maximum.x),
            0.5f * (minimum.y + maximum.y),
            0.5f * (minimum.z + maximum.z));

        float radius = 0.0f;
        for (const Vertex& vertex : vertices)
        {
            const float x = vertex.position.x - center.x;
            const float y = vertex.position.y - center.y;
            const float z = vertex.position.z - center.z;
            radius = (std::max)(radius, std::sqrt(x * x + y * y + z * z));
        }

        constexpr float targetRadius = 3.5f;
        const float scale = radius > 1e-4f ? targetRadius / radius : 1.0f;
        for (Vertex& vertex : vertices)
        {
            vertex.position.x = (vertex.position.x - center.x) * scale;
            vertex.position.y = (vertex.position.y - center.y) * scale + targetRadius * 0.15f;
            vertex.position.z = (vertex.position.z - center.z) * scale;
        }
    }
}

bool LoadModelWithAssimp(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes,
    std::string& outError)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filename,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->mRootNode)
    {
        outError = importer.GetErrorString();
        return false;
    }

    outVertices.clear();
    outIndices.clear();
    outSubmeshes.clear();
    ParseNode(
        scene,
        scene->mRootNode,
        XMMatrixIdentity(),
        outVertices,
        outIndices,
        outSubmeshes);
    NormalizeEarth(outVertices);
    return !outVertices.empty() && !outIndices.empty();
}
