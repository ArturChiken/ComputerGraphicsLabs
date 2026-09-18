#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Submesh.h"
#include "Vertex.h"

bool LoadModelWithAssimp(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes,
    std::string& outError);
