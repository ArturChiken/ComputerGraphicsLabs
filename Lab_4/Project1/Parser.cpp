#include "Parser.h"
#include "Vertex.h"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <cmath>

using namespace DirectX;

namespace
{
    struct ObjVertexRef
    {
        int Position = 0;
        int Texcoord = 0;
        int Normal = 0;
    };

    ObjVertexRef ParseObjVertexRef(const std::string& token)
    {
        ObjVertexRef ref;
        size_t firstSlash = token.find('/');
        if (firstSlash == std::string::npos)
        {
            ref.Position = std::stoi(token);
            return ref;
        }

        ref.Position = std::stoi(token.substr(0, firstSlash));
        size_t secondSlash = token.find('/', firstSlash + 1);
        if (secondSlash == std::string::npos)
        {
            std::string tex = token.substr(firstSlash + 1);
            if (!tex.empty())
                ref.Texcoord = std::stoi(tex);
            return ref;
        }

        std::string tex = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        std::string norm = token.substr(secondSlash + 1);
        if (!tex.empty())
            ref.Texcoord = std::stoi(tex);
        if (!norm.empty())
            ref.Normal = std::stoi(norm);
        return ref;
    }

    XMFLOAT3 NormalizeFloat3(const XMFLOAT3& v, const XMFLOAT3& fallback)
    {
        XMVECTOR vec = XMLoadFloat3(&v);
        float lenSq = XMVectorGetX(XMVector3LengthSq(vec));
        if (lenSq <= 1e-10f)
            return fallback;

        XMFLOAT3 result;
        XMStoreFloat3(&result, XMVector3Normalize(vec));
        return result;
    }

    void GenerateNormalsTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            Vertex& v0 = vertices[indices[i + 0]];
            Vertex& v1 = vertices[indices[i + 1]];
            Vertex& v2 = vertices[indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.position);
            XMVECTOR p1 = XMLoadFloat3(&v1.position);
            XMVECTOR p2 = XMLoadFloat3(&v2.position);
            XMVECTOR edge1 = p1 - p0;
            XMVECTOR edge2 = p2 - p0;

            XMVECTOR normalVec = XMVector3Normalize(XMVector3Cross(edge1, edge2));
            XMFLOAT3 faceNormal;
            XMStoreFloat3(&faceNormal, normalVec);

            XMFLOAT2 deltaUV1 =
            {
                v1.texcoord.x - v0.texcoord.x,
                v1.texcoord.y - v0.texcoord.y
            };
            XMFLOAT2 deltaUV2 =
            {
                v2.texcoord.x - v0.texcoord.x,
                v2.texcoord.y - v0.texcoord.y
            };

            float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
            XMFLOAT3 tangent = { 1.0f, 0.0f, 0.0f };
            XMFLOAT3 bitangent = { 0.0f, 1.0f, 0.0f };

            if (std::fabs(det) > 1e-8f)
            {
                float f = 1.0f / det;
                XMFLOAT3 e1;
                XMFLOAT3 e2;
                XMStoreFloat3(&e1, edge1);
                XMStoreFloat3(&e2, edge2);

                tangent =
                {
                    f * (deltaUV2.y * e1.x - deltaUV1.y * e2.x),
                    f * (deltaUV2.y * e1.y - deltaUV1.y * e2.y),
                    f * (deltaUV2.y * e1.z - deltaUV1.y * e2.z)
                };

                bitangent =
                {
                    f * (-deltaUV2.x * e1.x + deltaUV1.x * e2.x),
                    f * (-deltaUV2.x * e1.y + deltaUV1.x * e2.y),
                    f * (-deltaUV2.x * e1.z + deltaUV1.x * e2.z)
                };
            }

            Vertex* tri[3] = { &v0, &v1, &v2 };
            for (Vertex* v : tri)
            {
                XMFLOAT3 normal = NormalizeFloat3(v->normal, faceNormal);
                XMVECTOR nVec = XMLoadFloat3(&normal);
                XMVECTOR tVec = XMLoadFloat3(&tangent);
                tVec = XMVector3Normalize(tVec - XMVector3Dot(tVec, nVec) * nVec);
                XMVECTOR bVec = XMVector3Normalize(XMVector3Cross(nVec, tVec));

                v->normal = normal;
                XMStoreFloat3(&v->tangent, tVec);
                XMStoreFloat3(&v->bitangent, bVec);
            }
        }
    }

    std::string TrimLeft(const std::string& line)
    {
        size_t first = line.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} : line.substr(first);
    }
}

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes)
{
    outVertices.clear();
    outIndices.clear();
    outSubmeshes.clear();
    std::vector<Submesh> submeshes;
    std::string currentMaterial = "";
    uint32_t currentStartIndex = 0;

    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    constexpr float OBJ_SCALE = 0.01f;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;

    std::string line;

    while (std::getline(file, line))
    {
        if (line.rfind("v ", 0) == 0)
        {
            XMFLOAT3 p;
            sscanf_s(line.c_str(), "v %f %f %f", &p.x, &p.y, &p.z);

            p.x *= OBJ_SCALE;
            p.y *= OBJ_SCALE;
            p.z *= OBJ_SCALE;

            positions.push_back(p);
        }
        else if (line.rfind("vt ", 0) == 0)
        {
            XMFLOAT2 uv;
            sscanf_s(line.c_str(), "vt %f %f", &uv.x, &uv.y);
            //uv.y = 1.0f - uv.y;
            texcoords.push_back(uv);
        }
        else if (line.rfind("vn ", 0) == 0)
        {
            XMFLOAT3 n;
            sscanf_s(line.c_str(), "vn %f %f %f", &n.x, &n.y, &n.z);
            normals.push_back(n);
        }

        else if (line.rfind("usemtl ", 0) == 0)
        {
            // если уже был материал — закрываем предыдущий submesh
            if (!currentMaterial.empty() &&
                outIndices.size() > currentStartIndex)
            {
                Submesh sm;
                sm.MaterialName = currentMaterial;
                sm.IndexStart = currentStartIndex;
                sm.IndexCount = (uint32_t)outIndices.size() - currentStartIndex;
                outSubmeshes.push_back(sm);
            }

            currentMaterial = line.substr(7);
            currentStartIndex = (uint32_t)outIndices.size();
        }

        else if (line.rfind("f ", 0) == 0)
        {
            std::vector<ObjVertexRef> refs;

            std::stringstream ss(line.substr(2));
            std::string vert;

            while (ss >> vert)
            {
                refs.push_back(ParseObjVertexRef(vert));
            }

            // Триангуляция fan способом
            for (size_t i = 1; i + 1 < refs.size(); ++i)
            {
                int ids[3] = { 0, (int)i, (int)i + 1 };

                for (int k = 0; k < 3; ++k)
                {
                    Vertex v{};

                    const ObjVertexRef& ref = refs[ids[k]];
                    int posIndex = ref.Position - 1;
                    int texIndex = ref.Texcoord - 1;
                    int normIndex = ref.Normal - 1;

                    v.position = positions[posIndex];
                    v.normal = (normIndex >= 0 && normIndex < static_cast<int>(normals.size()))
                        ? normals[normIndex]
                        : XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                    v.texcoord = (texIndex >= 0 && texIndex < static_cast<int>(texcoords.size()))
                        ? texcoords[texIndex]
                        : XMFLOAT2{ 0.0f, 0.0f };

                    outVertices.push_back(v);
                    outIndices.push_back((uint32_t)outVertices.size() - 1);
                }
            }
        }
    }

    if (outVertices.empty())
        return false;

    GenerateNormalsTangents(outVertices, outIndices);

    // ==============================
    //        CENTER MODEL
    // ==============================

    XMFLOAT3 minP = outVertices[0].position;
    XMFLOAT3 maxP = outVertices[0].position;

    for (const auto& v : outVertices)
    {
        minP.x = std::min(minP.x, v.position.x);
        minP.y = std::min(minP.y, v.position.y);
        minP.z = std::min(minP.z, v.position.z);

        maxP.x = std::max(maxP.x, v.position.x);
        maxP.y = std::max(maxP.y, v.position.y);
        maxP.z = std::max(maxP.z, v.position.z);
    }

    XMFLOAT3 center =
    {
        (minP.x + maxP.x) * 0.5f,
        (minP.y + maxP.y) * 0.5f,
        (minP.z + maxP.z) * 0.5f
    };

    for (auto& v : outVertices)
    {
        v.position.x -= center.x;
        v.position.y -= center.y;
        v.position.z -= center.z;
    }

    if (!currentMaterial.empty() &&
        outIndices.size() > currentStartIndex)
    {
        Submesh sm;
        sm.MaterialName = currentMaterial;
        sm.IndexStart = currentStartIndex;
        sm.IndexCount = (uint32_t)outIndices.size() - currentStartIndex;
        outSubmeshes.push_back(sm);
    }

    return true;
}

bool LoadMTL(
    const std::string& filename,
    std::vector<ParsedMaterial>& materials)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    std::string line;
    ParsedMaterial current;

    while (std::getline(file, line))
    {
        line = TrimLeft(line);
        if (line.rfind("newmtl ", 0) == 0)
        {
            if (!current.Name.empty())
                materials.push_back(current);

            current = ParsedMaterial{};
            current.Name = line.substr(7);
        }
        else if (line.rfind("map_Kd ", 0) == 0)
        {
            current.DiffuseMap = line.substr(7);
        }
        else if (line.rfind("map_bump ", 0) == 0)
        {
            // Bump maps perturb normals; they are not reliable geometric displacement maps.
            if (current.NormalMap.empty())
                current.NormalMap = line.substr(9);
        }
        else if (line.rfind("bump ", 0) == 0)
        {
            // Keep bump as a normal-map fallback, but reserve displacement for disp/map_disp.
            if (current.NormalMap.empty())
                current.NormalMap = line.substr(5);
        }
        else if (line.rfind("norm ", 0) == 0 || line.rfind("map_norm ", 0) == 0)
        {
            size_t space = line.find(' ');
            current.NormalMap = line.substr(space + 1);
        }
        else if (line.rfind("disp ", 0) == 0 || line.rfind("map_disp ", 0) == 0)
        {
            size_t space = line.find(' ');
            current.DisplacementMap = line.substr(space + 1);
        }
        else if (line.rfind("Kd ", 0) == 0)
        {
            std::stringstream ss(line.substr(3));
            ss >> current.Kd.x >> current.Kd.y >> current.Kd.z;
        }
    }

    if (!current.Name.empty())
        materials.push_back(current);

    return true;
}
