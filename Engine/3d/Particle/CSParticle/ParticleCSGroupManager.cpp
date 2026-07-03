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

            // ここではメタデータの登録のみ行い、実体(GPUバッファ)は確保しない。
            // 実際に確保するのはエミッター等が名前で要求した時（GetParticleCSGroup での遅延生成）。
            // これにより起動時に全 .json 分の maxParticleCount バッファを確保する無駄を無くす。
            GroupDesc desc;
            desc.groupName = loadedGroupName;
            desc.texturePath = data.Load<std::string>("textureName", "");
            desc.modelPath = data.Load<std::string>("modelfilePath", "");
            desc.maxParticleCount = data.Load<uint32_t>("maxParticleCount", 10000);
            desc.blendMode = data.Load<BlendMode>("blendMode", BlendMode::kAdd);
            desc.primitiveType = data.Load<PrimitiveType>("primitiveType", PrimitiveType::None);
            groupDescs_[loadedGroupName] = std::move(desc);
        }
    }
}

void ParticleCSGroupManager::Finalize() {
    particleGroups_.clear();
    independentGroups_.clear();
    groupPool_.clear();
    groupDescs_.clear();
}

ParticleCSGroup *ParticleCSGroupManager::GetParticleCSGroup(const std::string &name) {
    // 既に実体化済みなら返す
    for (const auto &group : particleGroups_) {
        if (group->GetGroupName() == name) {
            return group.get();
        }
    }
    // 未生成: メタデータ登録簿にあれば、この時点で初めて遅延生成する
    auto it = groupDescs_.find(name);
    if (it != groupDescs_.end()) {
        return LoadGroupFromDesc(it->second);
    }
    return nullptr;
}

ParticleCSGroup *ParticleCSGroupManager::LoadGroupFromDesc(const GroupDesc &desc) {
    auto particleGroup = std::make_unique<ParticleCSGroup>();
    if (!desc.modelPath.empty()) {
        particleGroup->CreateParticleGroup(desc.groupName, desc.modelPath, desc.maxParticleCount, desc.texturePath, desc.blendMode);
    } else if (desc.primitiveType != PrimitiveType::None) {
        particleGroup->CreatePrimitiveParticleGroup(desc.groupName, desc.primitiveType, desc.maxParticleCount, desc.texturePath, desc.blendMode);
    } else {
        return nullptr;
    }
    ParticleCSGroup *groupPtr = particleGroup.get();
    particleGroups_.emplace_back(std::move(particleGroup));
    return groupPtr;
}

std::vector<std::string> ParticleCSGroupManager::GetAllGroupNames() const {
    std::vector<std::string> names;
    names.reserve(groupDescs_.size());
    for (const auto &[name, desc] : groupDescs_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
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

    // エディタ等で新規生成されたグループも登録簿へ登録し、一覧表示と
    // （破棄後の）遅延再生成の対象に含める。
    GroupDesc desc;
    desc.groupName = particleCSGroup->GetGroupName();
    desc.texturePath = textureFilePath;
    desc.modelPath = particleCSGroup->GetModelPath();
    desc.maxParticleCount = particleCSGroup->GetSettingsData()->maxParticleCount;
    desc.blendMode = particleCSGroup->GetParticleGroupData().blendMode;
    desc.primitiveType = particleCSGroup->GetPrimitiveType();
    groupDescs_[desc.groupName] = std::move(desc);

    particleGroups_.emplace_back(std::move(particleCSGroup));
}
} // namespace Hagine
