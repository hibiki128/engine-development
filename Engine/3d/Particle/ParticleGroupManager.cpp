#include "ParticleGroupManager.h"

namespace Hagine {
void ParticleGroupManager::Initialize() {
    const std::string directoryPath = "resources/jsons/ParticleGroup/";

    // ディレクトリが存在しない場合は何もしない
    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        return;
    }

    for (const auto &entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            // ファイル名（拡張子なし）をグループ名として使用してDataHandlerを生成
            std::string groupName = entry.path().stem().string();
            DataHandler data("ParticleGroup", groupName);

            // グループ名キーが存在しない場合はスキップ
            if (!data.Exists()) {
                continue;
            }

            std::string loadedGroupName = data.Load<std::string>("groupName", "");
            if (loadedGroupName.empty()) {
                continue;
            }

            std::string texturePath = data.Load<std::string>("textrueName", "");
            std::string modelPath = data.Load<std::string>("modelfilePath", "");
            PrimitiveType type = data.Load<PrimitiveType>("primitiveType", PrimitiveType::None);

            if (!modelPath.empty()) {
                CreateParticleGroup(loadedGroupName, modelPath, texturePath);
            } else if (type != PrimitiveType::None) {
                CreatePrimitiveParticleGroup(loadedGroupName, type, texturePath);
            }
        }
    }
}

void ParticleGroupManager::Finalize() {
    particleGroups_.clear();
    independentGroups_.clear();
}

void ParticleGroupManager::AddParticleGroup(std::unique_ptr<ParticleGroup> particleGroup) {
    DataHandler data("ParticleGroup", particleGroup->GetGroupName());
    data.Save("groupName", particleGroup->GetGroupName());
    // materialがvectorになったため、最初のmaterialのtextureFilePathを保存
    const auto &materials = particleGroup->GetParticleGroupData().materials;
    std::string textureFilePath = (!materials.empty()) ? materials[0].textureFilePath : "";
    data.Save("textrueName", textureFilePath);
    data.Save("modelfilePath", particleGroup->GetModelPath());
    data.Save("primitiveType", particleGroup->GetPrimitiveType());
    particleGroups_.emplace_back(std::move(particleGroup));
}

void ParticleGroupManager::CreateParticleGroup(const std::string &groupName, const std::string &filename, const std::string &texturePath) {
    auto particleGroup = std::make_unique<ParticleGroup>();
    particleGroup->CreateParticleGroup(groupName, filename, texturePath);
    AddParticleGroup(std::move(particleGroup));
}

void ParticleGroupManager::CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, const std::string &texturePath) {
    auto particleGroup = std::make_unique<ParticleGroup>();
    particleGroup->CreatePrimitiveParticleGroup(groupName, type, texturePath);
    AddParticleGroup(std::move(particleGroup));
}
} // namespace Hagine
