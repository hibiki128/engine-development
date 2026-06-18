#include "ParticleStruct.h"

namespace Hagine {
ParticleMaterial ForParticleMaterial(MaterialData material) {
    ParticleMaterial result;
    result.color = material.color;
    result.uvTransform = material.uvTransform;
    result.textureFilePath = material.textureFilePath;
    result.textureIndex = material.textureIndex;
    return result;
}

std::vector<ParticleMaterial> ForParticleMaterials(std::vector<MaterialData> materials) {
    std::vector<ParticleMaterial> result;
    for (const auto &material : materials) {
        result.push_back(ForParticleMaterial(material));
    }
    return result;
}

uint32_t threadsPerGroup_ = 1024;
int threadGroupSize_ = 1024;
} // namespace Hagine
