#include "Material.h"

#include "fstream"
#include <graphics/srv/SrvManager.h>
#include <graphics/texture/TextureManager.h>

namespace Hagine {
void Material::Initialize() {
    pDxCommon_ = DirectXCommon::GetInstance();
    CreateMaterial();
}

void Material::LoadTexture() {
    // テクスチャの読み込み
    TextureManager::GetInstance()->LoadTexture(materialData_.textureFilePath);
    materialData_.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(materialData_.textureFilePath);
}

void Material::PrimitiveInitialize(const PrimitiveType &type) {
    materialData_.color = PrimitiveModel::GetInstance()->GetPrimitiveData(type).color;
    materialData_.uvTransform = PrimitiveModel::GetInstance()->GetPrimitiveData(type).uvMatrix;
    materialData_.textureFilePath = "debug/uvChecker.png";
}

void Material::Draw(const Vector4 color, bool lighting) {
    pMaterialDataGPU_->color = color;
    pMaterialDataGPU_->enableLighting = lighting ? 1 : 0;

    materialData_.uvTransform = MakeAffineMatrix({materialData_.uvSize.x, materialData_.uvSize.y, 1.0f}, {0.0f, 0.0f, materialData_.uvRotate}, {materialData_.uvPosition.x, materialData_.uvPosition.y, 0.0f});
    // 組み立てた UV 行列は毎フレーム GPU へ送る
    // （送らないと SetUVSize 等の変更が UpdateGPUData を呼ぶまで反映されない）
    pMaterialDataGPU_->uvTransform = materialData_.uvTransform;

    // 法線マッピング関連（ImGui等での変更を毎フレーム反映）
    pMaterialDataGPU_->enableNormalMap = materialData_.enableNormalMap ? 1 : 0;
    pMaterialDataGPU_->enableProceduralNormal = materialData_.enableProceduralNormal ? 1 : 0;
    pMaterialDataGPU_->normalStrength = materialData_.normalStrength;
    pMaterialDataGPU_->proceduralScale = materialData_.proceduralScale;

    ID3D12GraphicsCommandList *commandList = pDxCommon_->GetCommandList().Get();
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, materialData_.textureIndex);
}

void Material::SetTexture(const std::string &texturePath) {
    if (materialData_.textureFilePath == texturePath)
        return;

    // テクスチャを読み込み
    TextureManager::GetInstance()->LoadTexture(texturePath);

    // MaterialDataを更新
    materialData_.textureFilePath = texturePath;
    materialData_.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(texturePath);

    // GPU側のデータも更新（色や他のパラメータも含む）
    UpdateGPUData();
}

void Material::SetEnvironmentCoefficients(float environmentCoefficients) {
    materialData_.environmentCoefficient = environmentCoefficients;
    UpdateGPUData();
}

MaterialData Material::LoadMaterialTemplateFile(const std::string &directoryPath, const std::string &filename) {
    MaterialData materialData;                          // 構築するMaterialData
    std::string line;                                   // ファイルから読んだ1行を格納するもの
    std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
    assert(file.is_open());                             // 開けなかったら止める
    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        // identifierに応じた処理
        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            // textureFilePath は images ルートからの相対パスで保持する。
            // 実パスへの解決は TextureManager 側（AssetPath::Image）が行う。
            materialData.textureFilePath = textureFilename;
        }
    }

    // テクスチャが張られていない場合の処理
    if (materialData.textureFilePath.empty()) {
        materialData.textureFilePath = "debug/white1x1.png";
    }

    return materialData;
}

void Material::CreateMaterial() {
    // GPUバッファの作成
    materialResource_ = pDxCommon_->CreateBufferResource(sizeof(MaterialDataGPU));
    materialResource_->Map(0, nullptr, reinterpret_cast<void **>(&pMaterialDataGPU_));

    // 初期値設定
    UpdateGPUData();
}

void Material::UpdateGPUData() {
    if (pMaterialDataGPU_) {
        pMaterialDataGPU_->color = materialData_.color;
        pMaterialDataGPU_->enableLighting = materialData_.enableLighting ? 1 : 0;
        pMaterialDataGPU_->uvTransform = materialData_.uvTransform;
        pMaterialDataGPU_->shininess = 32.0f;
        pMaterialDataGPU_->environmentCoefficient = materialData_.environmentCoefficient;
        pMaterialDataGPU_->enableNormalMap = materialData_.enableNormalMap ? 1 : 0;
        pMaterialDataGPU_->enableProceduralNormal = materialData_.enableProceduralNormal ? 1 : 0;
        pMaterialDataGPU_->normalStrength = materialData_.normalStrength;
        pMaterialDataGPU_->proceduralScale = materialData_.proceduralScale;
    }
}

void Material::SetProceduralNormal(bool enable, float scale, float strength) {
    materialData_.enableProceduralNormal = enable;
    materialData_.proceduralScale = scale;
    materialData_.normalStrength = strength;
    UpdateGPUData();
}

void Material::SetNormalMap(const std::string &normalMapPath) {
    if (normalMapPath.empty())
        return;

    // 法線マップを読み込み、テクスチャインデックスを解決して有効化する
    TextureManager::GetInstance()->LoadTexture(normalMapPath);
    materialData_.normalMapFilePath = normalMapPath;
    materialData_.normalMapIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(normalMapPath);
    materialData_.hasNormalMapTexture = true;
    materialData_.enableNormalMap = true;
    // 手続き的法線とは排他（PS が procedural を優先するため明示的に切る）
    materialData_.enableProceduralNormal = false;
    UpdateGPUData();
}

void Material::ClearNormalMap() {
    materialData_.normalMapFilePath.clear();
    materialData_.normalMapIndex = 0;
    materialData_.hasNormalMapTexture = false;
    // 画像が無い状態で有効のままだと albedo を法線として読んでしまうため切る
    materialData_.enableNormalMap = false;
    UpdateGPUData();
}
} // namespace Hagine
