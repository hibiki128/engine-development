#include "Material.h"

#include "fstream"
#include <cstring>
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

size_t Material::ComputeDrawSignature() const {
    // Draw() が定数バッファへ書き込む値と、バインドするテクスチャを混ぜ込む。
    // color / enableLighting は呼び出し側（色はインスタンスごと・ライティングはバッチキー）で
    // 扱うのでここには含めない。environmentCoefficient も reflect フラグから決まるので除外。
    size_t hash = 0;
    auto mix = [&hash](size_t value) {
        // boost::hash_combine と同じ混ぜ方
        hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    };
    auto mixFloat = [&mix](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mix(static_cast<size_t>(bits));
    };

    mix(materialData_.textureIndex);
    mix(GetNormalMapIndex());
    mixFloat(materialData_.shininess);
    mixFloat(materialData_.uvPosition.x);
    mixFloat(materialData_.uvPosition.y);
    mixFloat(materialData_.uvSize.x);
    mixFloat(materialData_.uvSize.y);
    mixFloat(materialData_.uvRotate);
    mix(materialData_.enableNormalMap ? 1u : 0u);
    mix(materialData_.enableProceduralNormal ? 1u : 0u);
    mixFloat(materialData_.normalStrength);
    mixFloat(materialData_.proceduralScale);
    mix(materialData_.enableToon ? 1u : 0u);
    return hash;
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
    pMaterialDataGPU_->enableToon = materialData_.enableToon ? 1 : 0;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    // 通常描画・スキニング・G-Buffer など複数のパイプラインから呼ばれるので、
    // 今バインドされているルートシグネチャからレジスタ番号で引く
    const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
    assert(rootSignature && "マテリアルを使うパイプラインのルートシグネチャが未生成です");

    pCommandList->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(0, D3D12_SHADER_VISIBILITY_PIXEL), materialResource_->GetGPUVirtualAddress());
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(
        rootSignature->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_PIXEL), materialData_.textureIndex);
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
        pMaterialDataGPU_->enableToon = materialData_.enableToon ? 1 : 0;
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
    // enableNormalMap は落とさない。画像未指定のまま有効なら albedo を法線として流用する
    UpdateGPUData();
}
} // namespace Hagine
