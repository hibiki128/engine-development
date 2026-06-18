#include "ParticleCSGroupManager.h"

namespace Hagine {
void ParticleCSGroupManager::Initialize() {
    const std::string directoryPath = "resources/jsons/ParticleCSGroup/";

    // ディレクトリが存在しない場合は何もしない
    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        return;
    }

    for (const auto &entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            // ファイル名（拡張子なし）をグループ名として使用してDataHandlerを生成
            std::string groupName = entry.path().stem().string();
            DataHandler data("ParticleCSGroup", groupName);

            // グループ名キーが存在しない場合はスキップ
            if (!data.Exists()) {
                continue;
            }

            std::string loadedGroupName = data.Load<std::string>("groupName", "");
            if (loadedGroupName.empty()) {
                continue;
            }

            std::string texturePath = data.Load<std::string>("textureName", "");
            std::string modelPath = data.Load<std::string>("modelfilePath", "");
            uint32_t maxParticleCount = data.Load<uint32_t>("maxParticleCount", 10000);
            BlendMode blendMode = data.Load<BlendMode>("blendMode", BlendMode::kAdd);
            PrimitiveType type = data.Load<PrimitiveType>("primitiveType", PrimitiveType::None);

            if (!modelPath.empty()) {
                CreateParticleCSGroup(loadedGroupName, modelPath, maxParticleCount, texturePath, blendMode);
            } else if (type != PrimitiveType::None) {
                CreatePrimitiveParticleCSGroup(loadedGroupName, type, maxParticleCount, texturePath, blendMode);
            }
        }
    }
}

void ParticleCSGroupManager::Finalize() {
    particleGroups_.clear();
    independentGroups_.clear();
    groupPool_.clear();
}

void ParticleCSGroupManager::CreateParticleCSGroup(const std::string &groupName, const std::string &fileName, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode) {
    auto particleGroup = std::make_unique<ParticleCSGroup>();
    particleGroup->CreateParticleGroup(groupName, fileName, maxParticleCount, texturePath, blendMode);
    AddParticleCSGroup(std::move(particleGroup));
}

void ParticleCSGroupManager::CreatePrimitiveParticleCSGroup(const std::string &groupName, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode) {
    auto particleGroup = std::make_unique<ParticleCSGroup>();
    particleGroup->CreatePrimitiveParticleGroup(groupName, type, maxParticleCount, texturePath, blendMode);
    AddParticleCSGroup(std::move(particleGroup));
}

void ParticleCSGroupManager::AddParticleCSGroup(std::unique_ptr<ParticleCSGroup> particleCSGroup) {
    DataHandler data("ParticleCSGroup", particleCSGroup->GetGroupName());
    data.Save("groupName", particleCSGroup->GetGroupName());
    // materialがvectorになったため、最初のmaterialのtextureFilePathを保存
    const auto &materials = particleCSGroup->GetParticleGroupData().materials;
    std::string textureFilePath = (!materials.empty()) ? materials[0].textureFilePath : "";
    data.Save("textureName", textureFilePath);
    data.Save("modelfilePath", particleCSGroup->GetModelPath());
    data.Save("primitiveType", particleCSGroup->GetPrimitiveType());
    data.Save("maxParticleCount", particleCSGroup->GetSettingsData()->maxParticleCount);
    data.Save("blendMode", particleCSGroup->GetParticleGroupData().blendMode);
    particleGroups_.emplace_back(std::move(particleCSGroup));
}
} // namespace Hagine
