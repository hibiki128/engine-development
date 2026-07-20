#define NOMINMAX
#include "ParticleCSFieldManager.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <Frame.h>
#include <algorithm>
#include <cassert>
#include <cmath>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

#pragma pack(push, 1)
namespace Hagine {
// 【重要】HLSL 側 ParticleFieldSettingsOverrideData（Particle.hlsli）と
// バイト単位で一致させること（合計112バイト）。
struct GPU_FieldSettingsOverride
{
    uint32_t overrideMask; // FieldOverrideBits の組み合わせ
    float lifeTimeMin;
    float lifeTimeMax;
    float scaleMin;
    float scaleMax;
    float velocityMultiplier;
    float trailSpawnDistance;
    float pad0;
    float velocityMin[3];
    float pad1;
    float velocityMax[3];
    float pad2;
    float accelImpulse[3];
    float pad3;
    float color[4];
    float gatherTarget[3];
    float pad4;
};
#pragma pack(pop)

static_assert(sizeof(GPU_FieldSettingsOverride) == 112,
              "GPU_FieldSettingsOverride のサイズが変化。HLSL ParticleFieldSettingsOverrideData と一致させること");

static constexpr size_t kGPUOverrideStride = sizeof(GPU_FieldSettingsOverride);

static void PackOverrideToGPU(const ParticleFieldSettingsOverride &src, GPU_FieldSettingsOverride &dst)
{
    dst.overrideMask = src.overrideMask;
    dst.lifeTimeMin = src.lifeTimeMin;
    dst.lifeTimeMax = src.lifeTimeMax;
    dst.scaleMin = src.scaleMin;
    dst.scaleMax = src.scaleMax;
    dst.velocityMultiplier = src.velocityMultiplier;
    dst.trailSpawnDistance = src.trailSpawnDistance;
    dst.pad0 = 0.0f;
    dst.velocityMin[0] = src.velocityMin.x;
    dst.velocityMin[1] = src.velocityMin.y;
    dst.velocityMin[2] = src.velocityMin.z;
    dst.pad1 = 0.0f;
    dst.velocityMax[0] = src.velocityMax.x;
    dst.velocityMax[1] = src.velocityMax.y;
    dst.velocityMax[2] = src.velocityMax.z;
    dst.pad2 = 0.0f;
    dst.accelImpulse[0] = src.accelImpulse.x;
    dst.accelImpulse[1] = src.accelImpulse.y;
    dst.accelImpulse[2] = src.accelImpulse.z;
    dst.pad3 = 0.0f;
    dst.color[0] = src.color.x;
    dst.color[1] = src.color.y;
    dst.color[2] = src.color.z;
    dst.color[3] = src.color.w;
    dst.gatherTarget[0] = src.gatherTarget.x;
    dst.gatherTarget[1] = src.gatherTarget.y;
    dst.gatherTarget[2] = src.gatherTarget.z;
    dst.pad4 = 0.0f;
}

void ParticleCSFieldManager::Finalize()
{
    // マップ中のリソースはアンマップしてから解放する
    if (fieldsResource_)
    {
        fieldsResource_->Unmap(0, nullptr);
        pFieldsMappedData_ = nullptr;
    }
    if (fieldCountResource_)
    {
        fieldCountResource_->Unmap(0, nullptr);
        pFieldCountMappedData_ = nullptr;
    }
    if (overrideResource_)
    {
        overrideResource_->Unmap(0, nullptr);
        pOverrideMappedData_ = nullptr;
    }

    // ComPtr は Reset() で明示的に解放（デストラクタでも自動解放される）
    fieldsResource_.Reset();
    fieldCountResource_.Reset();
    zeroFieldCountResource_.Reset();
    overrideResource_.Reset();

    fields_.clear();
}

void ParticleCSFieldManager::Initialize()
{
    pDxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    pSrvManager_ = SrvManager::GetInstance();
    CreateGPUResources();
}

void ParticleCSFieldManager::CreateGPUResources()
{
    // フィールド配列バッファ（StructuredBuffer として使う）
    size_t bufSize = sizeof(ParticleFieldData) * kMaxFields;
    fieldsResource_ = pDxCommon_->CreateBufferResource(bufSize);
    fieldsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pFieldsMappedData_));
    ZeroMemory(pFieldsMappedData_, bufSize);

    // フィールド数バッファ（ConstantBuffer）
    fieldCountResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * 4); // アライメント
    fieldCountResource_->Map(0, nullptr, reinterpret_cast<void **>(&pFieldCountMappedData_));
    *pFieldCountMappedData_ = 0;

    zeroFieldCountResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * 4);
    uint32_t *zeroPtr = nullptr;
    zeroFieldCountResource_->Map(0, nullptr, reinterpret_cast<void **>(&zeroPtr));
    *zeroPtr = 0;
    zeroFieldCountResource_->Unmap(0, nullptr);

    // SRV 登録 (fields)
    fieldsSrvIndex_ = pSrvManager_->Allocate() + 1;
    fieldsSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(fieldsSrvIndex_);
    fieldsSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(fieldsSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(
        fieldsSrvIndex_,
        fieldsResource_.Get(),
        kMaxFields,
        sizeof(ParticleFieldData));

    // 設定上書きバッファ（StructuredBuffer: gFieldsOverride t1）
    // HLSL の ParticleFieldSettingsOverrideData と同じレイアウトを
    // GPU_FieldSettingsOverride として扱う（サイズだけ合わせる）
    size_t overrideBufSize = kGPUOverrideStride * kMaxFields;
    overrideResource_ = pDxCommon_->CreateBufferResource(overrideBufSize);
    overrideResource_->Map(0, nullptr, &pOverrideMappedData_);
    ZeroMemory(pOverrideMappedData_, overrideBufSize);

    overrideSrvIndex_ = pSrvManager_->Allocate() + 1;
    overrideSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(overrideSrvIndex_);
    overrideSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(overrideSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(
        overrideSrvIndex_,
        overrideResource_.Get(),
        kMaxFields,
        kGPUOverrideStride);
}

void ParticleCSFieldManager::Update()
{
    UpdateEmitSpawnTimers();
    UploadToGPU();
}

void ParticleCSFieldManager::UpdateEmitSpawnTimers()
{
    // 接触Emitのバースト管理。
    // 各フィールドの間隔タイマーを進め、バーストするフレームだけ
    // data.emitSpawnCount（GPU通信スロット）に発生数を書き込む。
    // エミッター側（EmitterDisPatch）はこの値の合計をディスパッチ数に使う。
    const float dt = Frame::DeltaTime();
    for (auto &f : fields_)
    {
        uint32_t burst = 0;
        if (f.enabled && f.data.enableEmitSpawn != 0 && f.emitSpawnCount > 0)
        {
            if (f.emitSpawnInterval <= 0.0f)
            {
                // 間隔0 = 毎フレーム発生
                burst = f.emitSpawnCount;
                f.emitSpawnTimer = 0.0f;
            }
            else
            {
                f.emitSpawnTimer += dt;
                if (f.emitSpawnTimer >= f.emitSpawnInterval)
                {
                    f.emitSpawnTimer -= f.emitSpawnInterval;
                    // 低FPSで複数間隔ぶん経過しても1バーストに丸める（発生数の暴発防止）
                    f.emitSpawnTimer = std::min(f.emitSpawnTimer, f.emitSpawnInterval);
                    burst = f.emitSpawnCount;
                }
            }
        }
        else
        {
            f.emitSpawnTimer = 0.0f;
        }
        f.data.emitSpawnCount = burst;
    }
}

void ParticleCSFieldManager::UploadToGPU()
{
    uint32_t count = 0;
    for (auto &f : fields_)
    {
        if (!f.enabled)
            continue;
        if (count >= kMaxFields)
            break;

        ParticleFieldData gpuData = f.data;

        // Wind/Vortex の方向はシェーダ側で正規化しない契約のため、ここで正規化して転送する。
        // （長さが強さに紛れ込む・(0,0,0)で無反応になる、という不安定さの元だった）
        const float dirLen = std::sqrt(gpuData.direction.x * gpuData.direction.x +
                                       gpuData.direction.y * gpuData.direction.y +
                                       gpuData.direction.z * gpuData.direction.z);
        if (dirLen > 1e-5f)
        {
            gpuData.direction.x /= dirLen;
            gpuData.direction.y /= dirLen;
            gpuData.direction.z /= dirLen;
        }

        pFieldsMappedData_[count] = gpuData;

        // 設定上書きデータを GPU レイアウト構造体へパック
        auto *dst = reinterpret_cast<GPU_FieldSettingsOverride *>(
            static_cast<uint8_t *>(pOverrideMappedData_) + count * kGPUOverrideStride);
        PackOverrideToGPU(f.override_, *dst);

        count++;
    }
    *pFieldCountMappedData_ = count;
}

void ParticleCSFieldManager::AddField(const ParticleField &field)
{
    if (static_cast<uint32_t>(fields_.size()) >= kMaxFields)
        return;
    fields_.push_back(field);
}

void ParticleCSFieldManager::RemoveField(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        return;
    fields_.erase(fields_.begin() + index);
}

ParticleField *ParticleCSFieldManager::GetField(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        return nullptr;
    return &fields_[index];
}

// =============================================
// セーブ / ロード
// =============================================

void ParticleCSFieldManager::SaveFieldData(DataHandler &data, const ParticleField &field)
{
    // 基本情報
    data.Save("name", field.name);
    data.Save("enabled", field.enabled);

    // フィールドデータ
    data.Save("fieldType", field.data.fieldType);
    data.Save<Vector3>("position", field.data.position);
    data.Save("radius", field.data.radius);
    data.Save<Vector3>("direction", field.data.direction);
    data.Save("strength", field.data.strength);
    data.Save("falloff", field.data.falloff);

    // 寿命ドレイン
    data.Save("enableLifeDrain", field.data.enableLifeDrain);
    data.Save("lifeTimeDrain", field.data.lifeTimeDrain);

    // トレイル強制生成
    data.Save("enableForceTrail", field.data.enableForceTrail);
    data.Save("trailSpawnDistanceOverride", field.data.trailSpawnDistanceOverride);

    // カラー乗算
    data.Save("enableColorMultiply", field.data.enableColorMultiply);
    data.Save<Vector4>("colorMultiplier", field.data.colorMultiplier);

    // 一度きり設定上書き
    data.Save("enableSettingsOverride", field.data.enableSettingsOverride);
    if (field.data.enableSettingsOverride)
    {
        SaveOverrideData(data, field.override_);
    }

    // 接触Emit（発生数・間隔はフィールド側が唯一の設定場所）
    data.Save("enableEmitSpawn", field.data.enableEmitSpawn);
    if (field.data.enableEmitSpawn)
    {
        data.Save("emitSpawnLifeTimeMin", field.data.emitSpawnLifeTimeMin);
        data.Save("emitSpawnLifeTimeMax", field.data.emitSpawnLifeTimeMax);
        data.Save("emitSpawnCount", static_cast<int>(field.emitSpawnCount));
        data.Save("emitSpawnInterval", field.emitSpawnInterval);
    }

    // グループID
    data.Save("groupId", field.data.groupId);
}

void ParticleCSFieldManager::LoadFieldData(DataHandler &data, ParticleField &field)
{
    // 基本情報
    field.name = data.Load("name", field.name);
    field.enabled = data.Load("enabled", field.enabled);

    // フィールドデータ
    field.data.fieldType = data.Load("fieldType", field.data.fieldType);
    field.data.position = data.Load<Vector3>("position", field.data.position);
    field.data.radius = data.Load("radius", field.data.radius);
    field.data.direction = data.Load<Vector3>("direction", field.data.direction);
    field.data.strength = data.Load("strength", field.data.strength);
    field.data.falloff = data.Load("falloff", field.data.falloff);

    // 寿命ドレイン
    field.data.enableLifeDrain = data.Load("enableLifeDrain", field.data.enableLifeDrain);
    field.data.lifeTimeDrain = data.Load("lifeTimeDrain", field.data.lifeTimeDrain);

    // トレイル強制生成
    field.data.enableForceTrail = data.Load("enableForceTrail", field.data.enableForceTrail);
    field.data.trailSpawnDistanceOverride = data.Load("trailSpawnDistanceOverride", field.data.trailSpawnDistanceOverride);

    // カラー乗算
    field.data.enableColorMultiply = data.Load("enableColorMultiply", field.data.enableColorMultiply);
    field.data.colorMultiplier = data.Load<Vector4>("colorMultiplier", field.data.colorMultiplier);

    // 一度きり設定上書き
    field.data.enableSettingsOverride = data.Load("enableSettingsOverride", field.data.enableSettingsOverride);
    if (field.data.enableSettingsOverride)
    {
        LoadOverrideData(data, field.override_);
    }

    // 接触Emit（発生数・間隔はフィールド側が唯一の設定場所）
    field.data.enableEmitSpawn = data.Load("enableEmitSpawn", field.data.enableEmitSpawn);
    if (field.data.enableEmitSpawn)
    {
        field.data.emitSpawnLifeTimeMin = data.Load("emitSpawnLifeTimeMin", field.data.emitSpawnLifeTimeMin);
        field.data.emitSpawnLifeTimeMax = data.Load("emitSpawnLifeTimeMax", field.data.emitSpawnLifeTimeMax);
        field.emitSpawnCount = static_cast<uint32_t>(
            std::max(0, data.Load("emitSpawnCount", static_cast<int>(field.emitSpawnCount))));
        field.emitSpawnInterval = data.Load("emitSpawnInterval", field.emitSpawnInterval);
    }
    // data.emitSpawnCount はGPU通信専用（毎フレーム算出）なのでロードしない
    field.data.emitSpawnCount = 0;
    field.emitSpawnTimer = 0.0f;

    // グループID
    field.data.groupId = data.Load("groupId", field.data.groupId);
}

void ParticleCSFieldManager::SaveOverrideData(DataHandler &data, const ParticleFieldSettingsOverride &ov)
{
    data.Save("ov_mask", ov.overrideMask);
    data.Save("ov_lifeTimeMin", ov.lifeTimeMin);
    data.Save("ov_lifeTimeMax", ov.lifeTimeMax);
    data.Save("ov_scaleMin", ov.scaleMin);
    data.Save("ov_scaleMax", ov.scaleMax);
    data.Save<Vector3>("ov_velocityMin", ov.velocityMin);
    data.Save<Vector3>("ov_velocityMax", ov.velocityMax);
    data.Save("ov_velocityMultiplier", ov.velocityMultiplier);
    data.Save<Vector3>("ov_accelImpulse", ov.accelImpulse);
    data.Save<Vector4>("ov_color", ov.color);
    data.Save("ov_trailSpawnDistance", ov.trailSpawnDistance);
    data.Save<Vector3>("ov_gatherTarget", ov.gatherTarget);
}

void ParticleCSFieldManager::LoadOverrideData(DataHandler &data, ParticleFieldSettingsOverride &ov)
{
    // 新フォーマット（8項目）。旧フォーマット(ov_maskLo/Hi + 45項目)は
    // ビット意味が異なり安全に変換できないため読み込まない（実質未使用だった）。
    ov.overrideMask = data.Load("ov_mask", ov.overrideMask);
    ov.lifeTimeMin = data.Load("ov_lifeTimeMin", ov.lifeTimeMin);
    ov.lifeTimeMax = data.Load("ov_lifeTimeMax", ov.lifeTimeMax);
    ov.scaleMin = data.Load("ov_scaleMin", ov.scaleMin);
    ov.scaleMax = data.Load("ov_scaleMax", ov.scaleMax);
    ov.velocityMin = data.Load<Vector3>("ov_velocityMin", ov.velocityMin);
    ov.velocityMax = data.Load<Vector3>("ov_velocityMax", ov.velocityMax);
    ov.velocityMultiplier = data.Load("ov_velocityMultiplier", ov.velocityMultiplier);
    ov.accelImpulse = data.Load<Vector3>("ov_accelImpulse", ov.accelImpulse);
    ov.color = data.Load<Vector4>("ov_color", ov.color);
    ov.trailSpawnDistance = data.Load("ov_trailSpawnDistance", ov.trailSpawnDistance);
    ov.gatherTarget = data.Load<Vector3>("ov_gatherTarget", ov.gatherTarget);
}

void ParticleCSFieldManager::SaveField(const ParticleField &field)
{
    // フォルダ: jsons/ParticleField/  ファイル名: field.name.json
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleField", field.name);
    SaveFieldData(*data, field);
    ImGuiNotification::Post("パーティクルフィールドを保存しました: " + field.name, {0.2f, 0.8f, 0.2f, 1.0f});
}

ParticleField ParticleCSFieldManager::LoadField(const std::string &fileName, const ParticleField &defaultField)
{
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleField", fileName);
    if (!data->Exists())
    {
        return defaultField;
    }
    ParticleField field = defaultField;
    LoadFieldData(*data, field);
    ImGuiNotification::Post("パーティクルフィールドを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
    return field;
}

// =============================================
// CreateField
// =============================================

ParticleField *ParticleCSFieldManager::CreateField(const std::string &name, const std::string &templateName)
{
    // 上限チェック
    if (static_cast<uint32_t>(fields_.size()) >= kMaxFields)
    {
        return nullptr;
    }

    ParticleField newField;

    if (!templateName.empty())
    {
        // ★ テンプレートjsonが指定されていれば、そのデータを複製して土台にする
        newField = LoadField(templateName, ParticleField{});
    }

    // 名前は引数で上書き（テンプレートの名前ではなく指定名を使う）
    newField.name = name;

    // 自身のjsonが既に存在すれば、それをロードして上書きする
    // （再起動後の復元など、name.json が保存済みの場合に対応）
    {
        std::unique_ptr<DataHandler> selfData = std::make_unique<DataHandler>("ParticleField", name);
        if (selfData->Exists())
        {
            LoadFieldData(*selfData, newField);
            newField.name = name; // name だけは引数を優先
        }
    }

    fields_.push_back(newField);
    ImGuiNotification::Post("パーティクルフィールドを作成しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    return &fields_.back();
}

// =============================================
// ImGui
// =============================================
void ParticleCSFieldManager::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

    bool show = true;

    if (!ImGui::Begin("パーティクルフィールド管理", &show, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }
    ImGui::PopStyleColor();

    // ヘッダー情報
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::Text("フィールド数: %d / %d", static_cast<int>(fields_.size()), kMaxFields);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // フィールド追加ボタン
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.30f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.38f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.58f, 0.44f, 1.0f));
    if (ImGui::Button("フィールドを追加", ImVec2(-1, 30)))
    {
        ParticleField newField;
        newField.name = "Field_" + std::to_string(fields_.size());
        newField.enabled = true;
        AddField(newField);
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    // フィールドリスト
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i)
    {
        auto &f = fields_[i];

        // フィールドタイプ別の色
        ImVec4 headerColor;
        const char *typeLabel;
        switch (static_cast<ParticleFieldType>(f.data.fieldType))
        {
        case ParticleFieldType::Wind:
            headerColor = ImVec4(0.30f, 0.40f, 0.52f, 0.55f);
            typeLabel = "[風]";
            break;
        case ParticleFieldType::Attract:
            headerColor = ImVec4(0.42f, 0.34f, 0.50f, 0.55f);
            typeLabel = "[引力]";
            break;
        case ParticleFieldType::Repel:
            headerColor = ImVec4(0.52f, 0.40f, 0.28f, 0.55f);
            typeLabel = "[斥力]";
            break;
        case ParticleFieldType::Vortex:
            headerColor = ImVec4(0.30f, 0.46f, 0.44f, 0.55f);
            typeLabel = "[渦巻き]";
            break;
        default:
            headerColor = ImVec4(0.32f, 0.33f, 0.36f, 0.55f);
            typeLabel = "[不明]";
            break;
        }

        // 無効時はグレーアウト
        if (!f.enabled)
        {
            headerColor = ImVec4(0.28f, 0.28f, 0.30f, 0.55f);
        }

        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(headerColor.x + 0.1f, headerColor.y + 0.1f, headerColor.z + 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(headerColor.x + 0.2f, headerColor.y + 0.2f, headerColor.z + 0.2f, 1.0f));

        std::string label = std::string(typeLabel) + " " + f.name + "##field" + std::to_string(i);
        bool open = ImGui::CollapsingHeader(label.c_str());
        ImGui::PopStyleColor(3);

        if (open)
        {
            ImGui::Indent();
            ImGui::PushItemWidth(200.0f);

            // 有効/無効チェック
            ImGui::Checkbox(("有効##en" + std::to_string(i)).c_str(), &f.enabled);
            ImGui::SameLine();

            // 保存ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.52f, 0.70f, 1.0f));
            if (ImGui::Button(("保存##save" + std::to_string(i)).c_str(), ImVec2(50, 0)))
            {
                SaveField(f);
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();

            // 削除ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.46f, 0.24f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.30f, 0.30f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.66f, 0.36f, 0.36f, 1.0f));
            if (ImGui::Button(("削除##del" + std::to_string(i)).c_str(), ImVec2(60, 0)))
            {
                removeIndex = i;
            }
            ImGui::PopStyleColor(3);

            ImGui::Spacing();

            // 名前
            char nameBuf[128];
            strncpy_s(nameBuf, f.name.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText(("名前##nm" + std::to_string(i)).c_str(), nameBuf, sizeof(nameBuf)))
            {
                f.name = nameBuf;
            }

            ImGui::Spacing();
            ImGui::Separator();

            // フィールドタイプ選択
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("フィールド種類");
            ImGui::PopStyleColor();

            const char *typeItems[] = {"風 (Wind)", "引力 (Attract)", "斥力 (Repel)", "渦巻き (Vortex)"};
            int typeIdx = static_cast<int>(f.data.fieldType);
            if (ImGui::Combo(("##type" + std::to_string(i)).c_str(), &typeIdx, typeItems, 4))
            {
                f.data.fieldType = static_cast<uint32_t>(typeIdx);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // 位置・範囲
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("位置・影響範囲");
            ImGui::PopStyleColor();

            ImGui::DragFloat3(("位置##pos" + std::to_string(i)).c_str(), &f.data.position.x, 0.1f, -9999.0f, 9999.0f, "%.2f");
            ImGui::DragFloat(("影響半径##rad" + std::to_string(i)).c_str(), &f.data.radius, 0.1f, 0.01f, 9999.0f, "%.2f");
            ImGui::DragFloat(("減衰指数##fal" + std::to_string(i)).c_str(), &f.data.falloff, 0.05f, 0.1f, 4.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("中心=1 端=0 の減衰カーブの指数\n1.0=線形 / 2.0=二乗（端で急激に弱く） / 0.5=平方根（広範囲で強い）");

            ImGui::Spacing();
            ImGui::Separator();

            // タイプ別パラメータ
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("フィールドパラメータ");
            ImGui::PopStyleColor();

            ImGui::DragFloat(("強さ##str" + std::to_string(i)).c_str(), &f.data.strength, 0.05f, -999.0f, 999.0f, "%.3f");

            auto ft = static_cast<ParticleFieldType>(f.data.fieldType);
            if (ft == ParticleFieldType::Wind)
            {
                ImGui::DragFloat3(("方向##dir" + std::to_string(i)).c_str(), &f.data.direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("正規化しなくてもシェーダー側で正規化されます");
            }
            else if (ft == ParticleFieldType::Vortex)
            {
                ImGui::DragFloat3(("回転軸##dir" + std::to_string(i)).c_str(), &f.data.direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("渦の回転軸（例: 0,1,0 = Y軸回り）");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // 寿命ドレイン
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("寿命ドレイン");
            ImGui::PopStyleColor();

            bool lifeDrainEnabled = (f.data.enableLifeDrain != 0);
            if (ImGui::Checkbox(("有効##ld" + std::to_string(i)).c_str(), &lifeDrainEnabled))
            {
                f.data.enableLifeDrain = lifeDrainEnabled ? 1u : 0u;
            }
            if (lifeDrainEnabled)
            {
                ImGui::DragFloat(("ドレイン量(秒/秒)##ldr" + std::to_string(i)).c_str(),
                                 &f.data.lifeTimeDrain, 0.05f, 0.0f, 100.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("フィールド中心(influence=1)で毎秒この量だけ寿命を消費します");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // トレイル強制生成
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("トレイル強制生成");
            ImGui::PopStyleColor();

            bool forceTrail = (f.data.enableForceTrail != 0);
            if (ImGui::Checkbox(("有効##ft" + std::to_string(i)).c_str(), &forceTrail))
            {
                f.data.enableForceTrail = forceTrail ? 1u : 0u;
            }
            if (forceTrail)
            {
                ImGui::DragFloat(("生成間隔上書き##tdo" + std::to_string(i)).c_str(),
                                 &f.data.trailSpawnDistanceOverride, 0.01f, 0.0f, 10.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 のときはグループ設定の trailSpawnDistance を使用します");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // カラー乗算
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("カラー乗算");
            ImGui::PopStyleColor();

            bool colorMul = (f.data.enableColorMultiply != 0);
            if (ImGui::Checkbox(("有効##cm" + std::to_string(i)).c_str(), &colorMul))
            {
                f.data.enableColorMultiply = colorMul ? 1u : 0u;
            }
            if (colorMul)
            {
                ImGui::ColorEdit4(("乗算色##clr" + std::to_string(i)).c_str(),
                                  &f.data.colorMultiplier.x,
                                  ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("フィールド影響度(influence)でブレンドされます\n白(1,1,1,1)=変化なし");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // 一度きり設定上書き
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("一度きり設定上書き");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("フィールドに最初に入ったとき、チェックした項目を\n粒子ごとに一度だけ書き換えます。\n一度書き換えた粒子は再度入っても変化しません。");

            bool settingsOvEnabled = (f.data.enableSettingsOverride != 0);
            if (ImGui::Checkbox(("有効##so" + std::to_string(i)).c_str(), &settingsOvEnabled))
            {
                f.data.enableSettingsOverride = settingsOvEnabled ? 1u : 0u;
            }
            if (settingsOvEnabled)
            {
                DrawOverrideImGui(f.override_, i);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // --- 接触Emit ---
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("接触Emit");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "「フィールド接触部分にのみ発生」を有効にしたエミッターが、\n"
                    "このフィールドと接触している表面にだけパーティクルを発生させます。\n"
                    "発生数・間隔・寿命はここ（フィールド側）が唯一の設定場所です。\n"
                    "エミッター側はトグルとグループIDのみ持ちます。");

            bool emitSpawn = (f.data.enableEmitSpawn != 0);
            if (ImGui::Checkbox(("有効##es" + std::to_string(i)).c_str(), &emitSpawn))
            {
                f.data.enableEmitSpawn = emitSpawn ? 1u : 0u;
            }
            if (emitSpawn)
            {
                ImGui::Indent();
                ImGui::PushItemWidth(180.0f);

                int spawnCount = static_cast<int>(f.emitSpawnCount);
                if (ImGui::DragInt(("発生数/バースト##esCount" + std::to_string(i)).c_str(), &spawnCount, 10, 0, 50000))
                {
                    f.emitSpawnCount = static_cast<uint32_t>(std::max(0, spawnCount));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "1回のバーストで発生させる粒子数（対象エミッターごと）。\n"
                        "全スレッドが接触点にEmitするため 500〜3000 程度で十分密になります。");

                ImGui::DragFloat(("発生間隔##esInterval" + std::to_string(i)).c_str(),
                                 &f.emitSpawnInterval, 0.005f, 0.0f, 10.0f, "%.3f s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("バーストの間隔（秒）。0 = 毎フレーム発生します。");

                ImGui::DragFloat(("寿命 Min##esLTMin" + std::to_string(i)).c_str(),
                                 &f.data.emitSpawnLifeTimeMin, 0.01f, 0.0f, 60.0f, "%.2f s");
                ImGui::DragFloat(("寿命 Max##esLTMax" + std::to_string(i)).c_str(),
                                 &f.data.emitSpawnLifeTimeMax, 0.01f, 0.0f, 60.0f, "%.2f s");
                // Min > Max にならないよう補正
                if (f.data.emitSpawnLifeTimeMin > f.data.emitSpawnLifeTimeMax)
                    f.data.emitSpawnLifeTimeMin = f.data.emitSpawnLifeTimeMax;

                // ライブ状態（間隔タイマーの進行と今フレームのバースト）
                if (f.emitSpawnInterval > 0.0f)
                {
                    const float ratio = std::clamp(f.emitSpawnTimer / f.emitSpawnInterval, 0.0f, 1.0f);
                    ImGui::ProgressBar(ratio, ImVec2(180.0f, 0.0f), "");
                    ImGui::SameLine();
                    ImGui::TextDisabled("次バーストまで %.2fs",
                                        std::max(0.0f, f.emitSpawnInterval - f.emitSpawnTimer));
                }
                else
                {
                    ImGui::TextDisabled("毎フレーム %u 個発生中", f.emitSpawnCount);
                }

                ImGui::PopItemWidth();
                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // --- グループID ---
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("グループID");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "-1 = 全エミッターに影響する（デフォルト）\n"
                    "0以上 = 同じIDを持つエミッターにのみ影響する\n"
                    "エミッター側は SetFieldGroupId() で設定します。");
            ImGui::PushItemWidth(120.0f);
            int gid = f.data.groupId;
            if (ImGui::DragInt(("##groupId" + std::to_string(i)).c_str(), &gid, 1, -1, 255))
            {
                f.data.groupId = std::max(-1, gid);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (f.data.groupId == -1)
            {
                ImGui::TextDisabled("(全エミッター対象)");
            }
            else
            {
                ImGui::Text("(ID: %d のエミッターのみ)", f.data.groupId);
            }

            ImGui::PopItemWidth();
            ImGui::Unindent();
        }

        ImGui::Spacing();
    }

    if (removeIndex >= 0)
    {
        RemoveField(removeIndex);
    }

    // -----------------------------------------------
    // ギズモ表示トグル & 即時描画
    // -----------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("デバッグ表示");
    ImGui::PopStyleColor();
    ImGui::Checkbox("ギズモ表示##gizmo", &showGizmos_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("各フィールドの影響範囲・方向をワイヤーフレームで表示します");

    if (showGizmos_)
    {
        DrawFieldGizmos();
    }

    ImGui::End();
#endif
}

// =============================================
// DrawOverrideImGui
// =============================================
void ParticleCSFieldManager::DrawOverrideImGui(ParticleFieldSettingsOverride &ov, int idx)
{
#ifdef USE_IMGUI
    using namespace FieldOverrideBits;
    const std::string s = std::to_string(idx);

    // 項目ヘルパー: 先頭のチェックボックスで上書きON/OFFを切り替え、
    // ONのときだけ右隣の値エディタを操作できるようにする
    auto BitCheckbox = [&](const char *id, uint32_t bit) -> bool {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + id + s).c_str(), &checked))
        {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        return checked;
    };

    ImGui::Indent(12.0f);
    ImGui::TextDisabled("チェックした項目だけ、入った粒子へ一度だけ適用されます");

    // ---- 寿命（Min/Max乱数で上書き） ----
    {
        const bool on = BitCheckbox("life", LifeTime);
        if (!on)
            ImGui::BeginDisabled();
        float v[2] = {ov.lifeTimeMin, ov.lifeTimeMax};
        if (ImGui::DragFloat2(("寿命 Min/Max##ov" + s).c_str(), v, 0.01f, 0.0f, 60.0f, "%.2f s"))
        {
            ov.lifeTimeMin = v[0];
            ov.lifeTimeMax = std::max(v[0], v[1]);
        }
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命を Min〜Max の乱数で上書きします。\n短くすると入った粒子が早く消えます。");
    }

    // ---- スケール（Min/Max乱数で上書き） ----
    {
        const bool on = BitCheckbox("scale", Scale);
        if (!on)
            ImGui::BeginDisabled();
        float v[2] = {ov.scaleMin, ov.scaleMax};
        if (ImGui::DragFloat2(("スケール Min/Max##ov" + s).c_str(), v, 0.01f, 0.0f, 99.0f, "%.2f"))
        {
            ov.scaleMin = v[0];
            ov.scaleMax = std::max(v[0], v[1]);
        }
        if (!on)
            ImGui::EndDisabled();
    }

    // ---- 速度（Min/Max乱数で置換） ----
    {
        const bool on = BitCheckbox("vel", Velocity);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("速度 Min##ov" + s).c_str(), &ov.velocityMin.x, 0.01f, -999.0f, 999.0f, "%.2f");
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("速度を成分ごとの Min〜Max 乱数で置き換えます");
        const float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(checkboxWidth);
        ImGui::DragFloat3(("速度 Max##ov" + s).c_str(), &ov.velocityMax.x, 0.01f, -999.0f, 999.0f, "%.2f");
        ImGui::Unindent(checkboxWidth);
        if (!on)
            ImGui::EndDisabled();
    }

    // ---- 速度倍率（一度だけ乗算） ----
    {
        const bool on = BitCheckbox("velmul", VelocityMul);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat(("速度倍率##ov" + s).c_str(), &ov.velocityMultiplier, 0.01f, -10.0f, 10.0f, "%.2f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間に速度へ一度だけ乗算します。\n0=停止 / 0.5=減速 / 負=反転");
    }

    // ---- 加速度インパルス（一度だけ加算） ----
    {
        const bool on = BitCheckbox("impulse", AccelImpulse);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("加速インパルス##ov" + s).c_str(), &ov.accelImpulse.x, 0.01f, -999.0f, 999.0f, "%.2f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間に速度へ一度だけ加算します（吹き飛ばし等）");
    }

    // ---- 色（RGBを上書きして固定） ----
    {
        const bool on = BitCheckbox("color", Color);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::ColorEdit4(("色上書き##ov" + s).c_str(), &ov.color.x, ImGuiColorEditFlags_Float);
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った粒子のRGBをこの色に固定します。\nアルファのフェードは通常どおり継続します。");
    }

    // ---- トレイル生成間隔の上書き ----
    {
        const bool on = BitCheckbox("traildist", TrailDistance);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat(("トレイル生成間隔##ov" + s).c_str(), &ov.trailSpawnDistance, 0.005f, 0.001f, 10.0f, "%.3f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("トレイルの生成間隔（距離）を上書きします。\n小さいほど濃く出ます。");
    }

    // ---- 向け替え（速さを保ったままターゲット方向へ） ----
    {
        const bool on = BitCheckbox("redirect", GatherRedirect);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("向け替え先##ov" + s).c_str(), &ov.gatherTarget.x, 0.1f, -9999.0f, 9999.0f, "%.1f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間、速さを保ったままこの座標の方向へ向け替えます");
    }

    ImGui::Unindent(12.0f);
#endif
}

// =============================================
// ギズモ描画
// =============================================

void ParticleCSFieldManager::DrawFieldGizmos()
{
    for (const auto &f : fields_)
    {
        if (!f.enabled)
            continue;

        // フィールドタイプ別に色を決定
        // strength の絶対値を alpha に反映して強さを視覚化（0.4〜1.0 にクランプ）
        float alpha = std::min(1.0f, 0.4f + std::abs(f.data.strength) * 0.06f);

        Vector4 color;
        auto ft = static_cast<ParticleFieldType>(f.data.fieldType);
        switch (ft)
        {
        case ParticleFieldType::Wind:
            // 風 → 水色
            color = {0.3f, 0.7f, 1.0f, alpha};
            break;
        case ParticleFieldType::Attract:
            // 引力 → 紫
            color = {0.8f, 0.3f, 1.0f, alpha};
            break;
        case ParticleFieldType::Repel:
            // 斥力 → オレンジ
            color = {1.0f, 0.5f, 0.1f, alpha};
            break;
        case ParticleFieldType::Vortex:
            // 渦巻き → 緑
            color = {0.2f, 1.0f, 0.6f, alpha};
            break;
        default:
            color = {0.6f, 0.6f, 0.6f, alpha};
            break;
        }

        // 影響範囲球（全タイプ共通）
        DrawFieldSphere(f, color);

        // タイプ別の方向・強さ表示
        switch (ft)
        {
        case ParticleFieldType::Wind:
            DrawWindArrows(f, color);
            break;
        case ParticleFieldType::Attract:
            // inward = true（外→中心向き）
            DrawRadialLines(f, color, true);
            break;
        case ParticleFieldType::Repel:
            // inward = false（中心→外向き）
            DrawRadialLines(f, color, false);
            break;
        case ParticleFieldType::Vortex:
            DrawVortexArcs(f, color);
            break;
        default:
            break;
        }
    }
}

// --- 影響範囲球 ---
void ParticleCSFieldManager::DrawFieldSphere(const ParticleField &field, const Vector4 &color)
{
    DrawLine3D::GetInstance()->DrawSphere(field.data.position, color, field.data.radius, 16);
}

// --- Wind：球内に等間隔で方向矢印を描く ---
void ParticleCSFieldManager::DrawWindArrows(const ParticleField &field, const Vector4 &color)
{
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // 方向を正規化
    Vector3 dir = field.data.direction;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-5f)
        return;
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;

    // strength の絶対値で矢印の長さを決定（最大 radius の 0.6 倍）
    float arrowLen = std::min(r * 0.6f, std::abs(field.data.strength) * 0.5f + r * 0.15f);
    // 矢頭サイズ
    float headLen = arrowLen * 0.25f;

    // 球内に 3×3×3 グリッドで矢印を配置
    const int grid = 3;
    float step = r * 1.6f / (grid - 1);
    for (int ix = 0; ix < grid; ++ix)
    {
        for (int iy = 0; iy < grid; ++iy)
        {
            for (int iz = 0; iz < grid; ++iz)
            {
                Vector3 offset = {
                    -r * 0.8f + ix * step,
                    -r * 0.8f + iy * step,
                    -r * 0.8f + iz * step,
                };
                // 球の外側は除外
                float d2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
                if (d2 > r * r)
                    continue;

                Vector3 from = {center.x + offset.x, center.y + offset.y, center.z + offset.z};
                Vector3 to = {from.x + dir.x * arrowLen, from.y + dir.y * arrowLen, from.z + dir.z * arrowLen};
                DrawLine3D::GetInstance()->SetPoints(from, to, color);

                // 矢頭：dirに垂直な軸で小さな V 字を描く
                // dir に直交するベクトルを求める
                Vector3 up = {0.0f, 1.0f, 0.0f};
                if (std::abs(dir.y) > 0.9f)
                    up = {1.0f, 0.0f, 0.0f};
                // cross(dir, up)
                Vector3 side = {
                    dir.y * up.z - dir.z * up.y,
                    dir.z * up.x - dir.x * up.z,
                    dir.x * up.y - dir.y * up.x,
                };
                float sLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
                if (sLen > 1e-5f)
                {
                    side.x /= sLen;
                    side.y /= sLen;
                    side.z /= sLen;
                }
                Vector3 headBase = {to.x - dir.x * headLen, to.y - dir.y * headLen, to.z - dir.z * headLen};
                Vector3 h1 = {headBase.x + side.x * headLen * 0.5f, headBase.y + side.y * headLen * 0.5f, headBase.z + side.z * headLen * 0.5f};
                Vector3 h2 = {headBase.x - side.x * headLen * 0.5f, headBase.y - side.y * headLen * 0.5f, headBase.z - side.z * headLen * 0.5f};
                DrawLine3D::GetInstance()->SetPoints(to, h1, color);
                DrawLine3D::GetInstance()->SetPoints(to, h2, color);
            }
        }
    }
}

// --- Attract / Repel：球面から中心、または中心から球面へ向かう放射線 ---
void ParticleCSFieldManager::DrawRadialLines(const ParticleField &field, const Vector4 &color, bool inward)
{
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // strength の絶対値で線の長さ割合を決定（0.3〜1.0）
    float ratio = std::min(1.0f, 0.3f + std::abs(field.data.strength) * 0.07f);

    // 正二十面体の頂点方向（12方向）を均一配置の代わりに球面上を均等サンプル
    const int stacks = 4;
    const int slices = 8;
    const float PI = 3.1415926535f;
    for (int si = 0; si < stacks; ++si)
    {
        float theta = PI * (si + 0.5f) / stacks; // 0 〜 π
        for (int sj = 0; sj < slices; ++sj)
        {
            float phi = 2.0f * PI * sj / slices;
            Vector3 dir = {
                std::sin(theta) * std::cos(phi),
                std::cos(theta),
                std::sin(theta) * std::sin(phi),
            };
            Vector3 surface = {center.x + dir.x * r, center.y + dir.y * r, center.z + dir.z * r};
            // 線の長さを ratio で縮める（途中まで）
            Vector3 inner = {
                center.x + dir.x * r * (1.0f - ratio),
                center.y + dir.y * r * (1.0f - ratio),
                center.z + dir.z * r * (1.0f - ratio),
            };
            if (inward)
            {
                // 球面 → 中心方向へ（Attract）
                DrawLine3D::GetInstance()->SetPoints(surface, inner, color);
            }
            else
            {
                // 中心 → 球面方向へ（Repel）
                DrawLine3D::GetInstance()->SetPoints(inner, surface, color);
            }
        }
    }
}

// --- Vortex：回転軸周りに螺旋状の円弧を描く ---
void ParticleCSFieldManager::DrawVortexArcs(const ParticleField &field, const Vector4 &color)
{
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // 回転軸を正規化
    Vector3 axis = field.data.direction;
    float axLen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axLen < 1e-5f)
        return;
    axis.x /= axLen;
    axis.y /= axLen;
    axis.z /= axLen;

    // strength の符号で回転方向を決定、絶対値で螺旋の巻き数を決定
    float sign = (field.data.strength >= 0.0f) ? 1.0f : -1.0f;
    float turns = std::min(1.5f, 0.5f + std::abs(field.data.strength) * 0.1f);

    // 軸に直交するベクトルを生成
    Vector3 up = {0.0f, 1.0f, 0.0f};
    if (std::abs(axis.y) > 0.9f)
        up = {1.0f, 0.0f, 0.0f};
    // right = cross(axis, up)
    Vector3 right = {
        axis.y * up.z - axis.z * up.y,
        axis.z * up.x - axis.x * up.z,
        axis.x * up.y - axis.y * up.x,
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    right.x /= rLen;
    right.y /= rLen;
    right.z /= rLen;
    // forward = cross(right, axis)
    Vector3 forward = {
        right.y * axis.z - right.z * axis.y,
        right.z * axis.x - right.x * axis.z,
        right.x * axis.y - right.y * axis.x,
    };

    // 高さ方向の異なる3段に円弧を描く
    const int arcLayers = 3;
    const int arcSegments = 24;
    const float PI = 3.1415926535f;
    for (int layer = 0; layer < arcLayers; ++layer)
    {
        // 各段を軸方向にオフセット（-r*0.5 〜 r*0.5）
        float heightOffset = -r * 0.5f + r * layer / (arcLayers - 1);
        Vector3 layerCenter = {
            center.x + axis.x * heightOffset,
            center.y + axis.y * heightOffset,
            center.z + axis.z * heightOffset,
        };
        // 段ごとに半径を変えて円錐状に見せる
        float layerRadius = r * (0.5f + 0.5f * std::sin(PI * layer / (arcLayers - 1)));

        for (int seg = 0; seg < arcSegments; ++seg)
        {
            float t1 = sign * 2.0f * PI * turns * seg / arcSegments;
            float t2 = sign * 2.0f * PI * turns * (seg + 1) / arcSegments;

            Vector3 p1 = {
                layerCenter.x + layerRadius * (right.x * std::cos(t1) + forward.x * std::sin(t1)),
                layerCenter.y + layerRadius * (right.y * std::cos(t1) + forward.y * std::sin(t1)),
                layerCenter.z + layerRadius * (right.z * std::cos(t1) + forward.z * std::sin(t1)),
            };
            Vector3 p2 = {
                layerCenter.x + layerRadius * (right.x * std::cos(t2) + forward.x * std::sin(t2)),
                layerCenter.y + layerRadius * (right.y * std::cos(t2) + forward.y * std::sin(t2)),
                layerCenter.z + layerRadius * (right.z * std::cos(t2) + forward.z * std::sin(t2)),
            };
            DrawLine3D::GetInstance()->SetPoints(p1, p2, color);
        }
    }

    // 回転軸そのものを細い線で表示（軸の方向が分かるように）
    Vector3 axisTop = {center.x + axis.x * r * 0.6f, center.y + axis.y * r * 0.6f, center.z + axis.z * r * 0.6f};
    Vector3 axisBot = {center.x - axis.x * r * 0.6f, center.y - axis.y * r * 0.6f, center.z - axis.z * r * 0.6f};
    DrawLine3D::GetInstance()->SetPoints(axisBot, axisTop, color);
}
} // namespace Hagine
