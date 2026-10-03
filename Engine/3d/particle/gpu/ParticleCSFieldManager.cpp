#define NOMINMAX
#include "ParticleCSFieldManager.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <Frame.h>
#include <MyMath.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <icon/IconsFontAwesome5.h>
#include <line/LineRenderer.h>
#include <numbers>

#ifdef USE_IMGUI
#include "../utility/debug/imgui/ImGuizmoManager.h"
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

namespace {
constexpr size_t kGPUOverrideStride = sizeof(GPU_FieldSettingsOverride);
// 保存形式の版。1 = 旧形式（fieldType + 機能ごとの enable 群）/ 2 = 形 + 効果
constexpr int kFieldFormatVersion = 2;

void PackOverrideToGPU(const ParticleFieldSettingsOverride &src, GPU_FieldSettingsOverride &dst)
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

Vector3 SafeNormalize(const Vector3 &v, const Vector3 &fallback)
{
    const float length = v.Length();
    return (length > 1e-5f) ? v * (1.0f / length) : fallback;
}

/// <summary>フィールドの回転（オイラー角）からローカル軸3本を取り出す（ギズモと同じ行列の作り方）</summary>
void FieldAxes(const ParticleField &field, Vector3 &axisX, Vector3 &axisY, Vector3 &axisZ)
{
    const Matrix4x4 rotation = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, field.rotation, {0.0f, 0.0f, 0.0f});
    axisX = SafeNormalize({rotation.m[0][0], rotation.m[0][1], rotation.m[0][2]}, {1.0f, 0.0f, 0.0f});
    axisY = SafeNormalize({rotation.m[1][0], rotation.m[1][1], rotation.m[1][2]}, {0.0f, 1.0f, 0.0f});
    axisZ = SafeNormalize({rotation.m[2][0], rotation.m[2][1], rotation.m[2][2]}, {0.0f, 0.0f, 1.0f});
}

/// <summary>形の半分の大きさ（球: x=半径 / 箱: 各軸の半分 / 円柱: x=半径 y=高さの半分）</summary>
Vector3 FieldHalfExtent(const ParticleField &field)
{
    constexpr float kMin = 0.01f;
    switch (field.shape)
    {
    case ParticleFieldShape::Box:
        return {(std::max)(field.boxSize.x * 0.5f, kMin), (std::max)(field.boxSize.y * 0.5f, kMin), (std::max)(field.boxSize.z * 0.5f, kMin)};
    case ParticleFieldShape::Cylinder:
        return {(std::max)(field.radius, kMin), (std::max)(field.height * 0.5f, kMin), (std::max)(field.radius, kMin)};
    default:
        return {(std::max)(field.radius, kMin), (std::max)(field.radius, kMin), (std::max)(field.radius, kMin)};
    }
}

/// <summary>矢印を1本描く（先端に V 字）</summary>
void DrawArrow(LineRenderer *line, const Vector3 &from, const Vector3 &to, const Vector4 &color)
{
    line->AddLine(from, to, color);
    const Vector3 direction = to - from;
    const float length = direction.Length();
    if (length < 1e-4f)
    {
        return;
    }
    const Vector3 forward = direction * (1.0f / length);
    const Vector3 up = (std::abs(forward.y) > 0.9f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
    const Vector3 side = SafeNormalize(forward.Cross(up), {1.0f, 0.0f, 0.0f});
    const float head = length * 0.25f;
    const Vector3 base = to - forward * head;
    line->AddLine(to, base + side * (head * 0.5f), color);
    line->AddLine(to, base - side * (head * 0.5f), color);
}
} // namespace

// =============================================
// 初期化・終了
// =============================================

void ParticleCSFieldManager::Finalize()
{
#ifdef USE_IMGUI
    for (const std::string &name : gizmoNames_)
    {
        ImGuizmoManager::GetInstance()->RemoveTarget(name);
    }
    gizmoNames_.clear();
    gizmoRegisteredData_ = nullptr;
#endif
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

    fieldsResource_.Reset();
    fieldCountResource_.Reset();
    zeroFieldCountResource_.Reset();
    overrideResource_.Reset();

    fields_.clear();
    uploadedSlots_.clear();
}

void ParticleCSFieldManager::Initialize()
{
    pDxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    pSrvManager_ = SrvManager::GetInstance();
    // 上限ぶん先に確保しておき、追加で並びが作り直されないようにする
    // （ゲーム側が GetField のポインタを持ち続けるため。並べ替え・削除は番号がずれるので注意）
    fields_.reserve(kMaxFields);
    CreateGPUResources();
}

void ParticleCSFieldManager::CreateGPUResources()
{
    // フィールド配列バッファ（StructuredBuffer として使う）
    size_t bufSize = sizeof(ParticleFieldGPU) * kMaxFields;
    fieldsResource_ = pDxCommon_->CreateBufferResource(bufSize);
    fieldsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pFieldsMappedData_));
    ZeroMemory(pFieldsMappedData_, bufSize);

    // フィールド数バッファ（ConstantBuffer）。シェーダーは「受けるか（>0）」の判定にだけ使う
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
    pSrvManager_->CreateSRVforStructuredBuffer(fieldsSrvIndex_, fieldsResource_.Get(), kMaxFields, sizeof(ParticleFieldGPU));

    // 設定上書きバッファ（StructuredBuffer: gFieldsOverride t1）
    size_t overrideBufSize = kGPUOverrideStride * kMaxFields;
    overrideResource_ = pDxCommon_->CreateBufferResource(overrideBufSize);
    overrideResource_->Map(0, nullptr, &pOverrideMappedData_);
    ZeroMemory(pOverrideMappedData_, overrideBufSize);

    overrideSrvIndex_ = pSrvManager_->Allocate() + 1;
    overrideSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(overrideSrvIndex_);
    overrideSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(overrideSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(overrideSrvIndex_, overrideResource_.Get(), kMaxFields, kGPUOverrideStride);
}

// =============================================
// 毎フレーム
// =============================================

void ParticleCSFieldManager::Update()
{
    UpdateSpawnTimers();
    UploadToGPU();
    SyncGizmoTargets();
}

void ParticleCSFieldManager::UpdateSpawnTimers()
{
    // 「この範囲から発生」の間隔管理。出すフレームだけ spawn.burst に数を入れる。
    const float dt = Frame::UnscaledDeltaTime();
    for (ParticleField &field : fields_)
    {
        ParticleField::Spawn &spawn = field.spawn;
        spawn.burst = 0;
        if (!field.enabled || !spawn.enabled || spawn.count == 0)
        {
            spawn.timer = 0.0f;
            continue;
        }
        if (spawn.interval <= 0.0f)
        {
            spawn.burst = spawn.count; // 間隔0 = 毎フレーム
            spawn.timer = 0.0f;
            continue;
        }
        spawn.timer += dt;
        if (spawn.timer >= spawn.interval)
        {
            // 低FPSで複数間隔ぶん経過しても1回に丸める（発生数の暴発防止）
            spawn.timer = (std::min)(spawn.timer - spawn.interval, spawn.interval);
            spawn.burst = spawn.count;
        }
    }
}

ParticleFieldGPU ParticleCSFieldManager::PackField(const ParticleField &field)
{
    ParticleFieldGPU gpu;
    gpu.center = field.position;
    gpu.shape = static_cast<uint32_t>(field.shape);
    FieldAxes(field, gpu.axisX, gpu.axisY, gpu.axisZ);
    gpu.halfExtent = FieldHalfExtent(field);
    switch (field.shape)
    {
    case ParticleFieldShape::Box:
        gpu.boundRadiusSq = gpu.halfExtent.x * gpu.halfExtent.x + gpu.halfExtent.y * gpu.halfExtent.y + gpu.halfExtent.z * gpu.halfExtent.z;
        break;
    case ParticleFieldShape::Cylinder:
        gpu.boundRadiusSq = gpu.halfExtent.x * gpu.halfExtent.x + gpu.halfExtent.y * gpu.halfExtent.y;
        break;
    default:
        gpu.boundRadiusSq = gpu.halfExtent.x * gpu.halfExtent.x;
        break;
    }
    gpu.falloffStart = std::clamp(field.falloffStart, 0.0f, 0.95f);
    gpu.falloffCurve = static_cast<uint32_t>(field.falloff);

    uint32_t flags = 0;
    if (field.wind.enabled)
    {
        flags |= FieldEffectBits::Wind;
        gpu.windVelocity = SafeNormalize(field.wind.direction, {1.0f, 0.0f, 0.0f}) * field.wind.speed;
        gpu.windResponse = (std::max)(field.wind.response, 0.0f);
    }
    if (field.attract.enabled)
    {
        flags |= FieldEffectBits::Attract;
        gpu.attractStrength = field.attract.strength;
        gpu.absorbRadius = (std::max)(field.attract.absorbRadius, 0.0f);
    }
    if (field.vortex.enabled)
    {
        flags |= FieldEffectBits::Vortex;
        gpu.vortexSpeed = field.vortex.speed;
        gpu.vortexResponse = (std::max)(field.vortex.response, 0.0f);
    }
    if (field.drag.enabled)
    {
        flags |= FieldEffectBits::Drag;
        gpu.dragPerSecond = (std::max)(field.drag.perSecond, 0.0f);
    }
    if (field.tint.enabled)
    {
        flags |= FieldEffectBits::Tint;
        gpu.tint = field.tint.color;
    }
    if (field.size.enabled)
    {
        flags |= FieldEffectBits::Size;
        gpu.sizeScale = (std::max)(field.size.scale, 0.0f);
    }
    if (field.life.enabled)
    {
        flags |= field.life.killOnEnter ? FieldEffectBits::Kill : FieldEffectBits::Life;
        gpu.lifeSpeed = (std::max)(field.life.speed, 0.0f);
    }
    if (field.trail.enabled)
    {
        flags |= FieldEffectBits::Trail;
        gpu.trailSpawnDistance = (std::max)(field.trail.spawnDistance, 0.0f);
    }
    if (field.once.enabled && field.once.settings.overrideMask != 0)
    {
        flags |= FieldEffectBits::Once;
    }
    gpu.effectFlags = flags;

    gpu.emitLifeMin = (std::min)(field.spawn.lifeMin, field.spawn.lifeMax);
    gpu.emitLifeMax = field.spawn.lifeMax;
    gpu.emitCount = field.spawn.burst;
    return gpu;
}

void ParticleCSFieldManager::UploadToGPU()
{
    uploadedSlots_.clear();
    uint32_t count = 0;
    for (int index = 0; index < static_cast<int>(fields_.size()); ++index)
    {
        const ParticleField &field = fields_[index];
        if (!field.enabled)
            continue;
        // ソロ中はそのフィールドだけを効かせる（どれが効いているかを切り分ける用）
        if (soloIndex_ >= 0 && index != soloIndex_)
            continue;
        if (count >= kMaxFields)
            break;

        pFieldsMappedData_[count] = PackField(field);
        auto *dst = reinterpret_cast<GPU_FieldSettingsOverride *>(static_cast<uint8_t *>(pOverrideMappedData_) + count * kGPUOverrideStride);
        PackOverrideToGPU(field.once.settings, *dst);

        UploadedSlot slot;
        slot.layers = field.layers;
        slot.affectsParticles = (pFieldsMappedData_[count].effectFlags & FieldEffectBits::UpdateMask) != 0;
        slot.emitCount = field.spawn.enabled ? field.spawn.burst : 0;
        uploadedSlots_.push_back(slot);
        ++count;
    }
    *pFieldCountMappedData_ = count;
}

uint32_t ParticleCSFieldManager::GetUpdateMask(uint32_t receiveLayers) const
{
    uint32_t mask = 0;
    for (size_t i = 0; i < uploadedSlots_.size(); ++i)
    {
        if (uploadedSlots_[i].affectsParticles && (uploadedSlots_[i].layers & receiveLayers) != 0)
        {
            mask |= 1u << i;
        }
    }
    return mask;
}

uint32_t ParticleCSFieldManager::GetEmitMask(uint32_t receiveLayers) const
{
    uint32_t mask = 0;
    for (size_t i = 0; i < uploadedSlots_.size(); ++i)
    {
        if (uploadedSlots_[i].emitCount > 0 && (uploadedSlots_[i].layers & receiveLayers) != 0)
        {
            mask |= 1u << i;
        }
    }
    return mask;
}

uint32_t ParticleCSFieldManager::GetEmitBurstTotal(uint32_t receiveLayers) const
{
    uint32_t total = 0;
    for (const UploadedSlot &slot : uploadedSlots_)
    {
        if (slot.emitCount > 0 && (slot.layers & receiveLayers) != 0)
        {
            total += slot.emitCount;
        }
    }
    return total;
}

// =============================================
// ギズモ登録
// =============================================

void ParticleCSFieldManager::SyncGizmoTargets()
{
#ifdef USE_IMGUI
    // 毎フレーム登録し直すと選択が外れるので、増減・改名・並べ替え・再確保があったときだけ作り直す
    std::vector<std::string> names;
    names.reserve(fields_.size());
    for (const ParticleField &field : fields_)
    {
        names.push_back(GizmoName(field.name));
    }
    if (names == gizmoNames_ && gizmoRegisteredData_ == fields_.data())
    {
        return;
    }

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    // 同じ番号のまま名前だけ変わった（名前の入力中など）ものは、選択を新しい名前へ引き継ぐ
    std::vector<std::string> keepSelected;
    for (size_t i = 0; i < gizmoNames_.size() && i < names.size(); ++i)
    {
        if (gizmoNames_[i] != names[i] && gizmo->IsSelected(gizmoNames_[i]))
        {
            keepSelected.push_back(names[i]);
        }
    }
    for (const std::string &old : gizmoNames_)
    {
        if (std::find(names.begin(), names.end(), old) == names.end())
        {
            gizmo->RemoveTarget(old);
        }
    }
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i)
    {
        // 位置と回転をギズモで動かせる（大きさは形ごとに意味が違うので数値で）
        gizmo->AddTarget(names[i], &fields_[i].position, &fields_[i].rotation, nullptr, true, [this, i]() { DrawFieldDetail(i); });
        // シーンのアイコンは効果ごとの絵で別に描くので、エミッターの★は出さない
        gizmo->SetSceneIcon(names[i], false);
    }
    for (const std::string &name : keepSelected)
    {
        gizmo->AddToSelection(name);
    }
    gizmoNames_ = std::move(names);
    gizmoRegisteredData_ = fields_.data();
#endif
}

int ParticleCSFieldManager::FindFieldByGizmoName(const std::string &gizmoName) const
{
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i)
    {
        if (GizmoName(fields_[i].name) == gizmoName)
        {
            return i;
        }
    }
    return -1;
}

// =============================================
// 表示用（代表の効果で色・絵・名前を決める）
// =============================================

namespace {
enum class PrimaryEffect
{
    None,
    Vortex,
    Pull,
    Push,
    Wind,
    Drag,
    Kill,
    Life,
    Tint,
    Size,
    Trail,
    Once,
    Spawn,
};

PrimaryEffect GetPrimaryEffect(const ParticleField &field)
{
    if (field.vortex.enabled)
        return PrimaryEffect::Vortex;
    if (field.attract.enabled)
        return field.attract.strength >= 0.0f ? PrimaryEffect::Pull : PrimaryEffect::Push;
    if (field.wind.enabled)
        return PrimaryEffect::Wind;
    if (field.drag.enabled)
        return PrimaryEffect::Drag;
    if (field.life.enabled)
        return field.life.killOnEnter ? PrimaryEffect::Kill : PrimaryEffect::Life;
    if (field.tint.enabled)
        return PrimaryEffect::Tint;
    if (field.size.enabled)
        return PrimaryEffect::Size;
    if (field.trail.enabled)
        return PrimaryEffect::Trail;
    if (field.once.enabled)
        return PrimaryEffect::Once;
    if (field.spawn.enabled)
        return PrimaryEffect::Spawn;
    return PrimaryEffect::None;
}
} // namespace

Vector4 ParticleCSFieldManager::FieldColor(const ParticleField &field)
{
    switch (GetPrimaryEffect(field))
    {
    case PrimaryEffect::Vortex:
        return {0.2f, 1.0f, 0.6f, 1.0f};
    case PrimaryEffect::Pull:
        return {0.8f, 0.3f, 1.0f, 1.0f};
    case PrimaryEffect::Push:
        return {1.0f, 0.5f, 0.1f, 1.0f};
    case PrimaryEffect::Wind:
        return {0.3f, 0.7f, 1.0f, 1.0f};
    case PrimaryEffect::Drag:
        return {0.55f, 0.65f, 0.80f, 1.0f};
    case PrimaryEffect::Kill:
    case PrimaryEffect::Life:
        return {0.95f, 0.35f, 0.35f, 1.0f};
    case PrimaryEffect::Tint:
        return {1.0f, 0.85f, 0.35f, 1.0f};
    case PrimaryEffect::Size:
        return {0.95f, 0.65f, 0.85f, 1.0f};
    case PrimaryEffect::Trail:
    case PrimaryEffect::Once:
        return {0.60f, 0.85f, 0.85f, 1.0f};
    case PrimaryEffect::Spawn:
        return {0.55f, 0.90f, 0.45f, 1.0f};
    default:
        return {0.6f, 0.6f, 0.6f, 1.0f};
    }
}

const char *ParticleCSFieldManager::FieldIcon(const ParticleField &field)
{
    switch (GetPrimaryEffect(field))
    {
    case PrimaryEffect::Vortex:
        return ICON_FA_SYNC_ALT;
    case PrimaryEffect::Pull:
        return ICON_FA_MAGNET;
    case PrimaryEffect::Push:
        return ICON_FA_BOMB;
    case PrimaryEffect::Wind:
        return ICON_FA_WIND;
    case PrimaryEffect::Drag:
        return ICON_FA_TACHOMETER_ALT;
    case PrimaryEffect::Kill:
    case PrimaryEffect::Life:
        return ICON_FA_HOURGLASS_HALF;
    case PrimaryEffect::Tint:
        return ICON_FA_TINT;
    case PrimaryEffect::Size:
        return ICON_FA_EXPAND_ARROWS_ALT;
    case PrimaryEffect::Trail:
        return ICON_FA_METEOR;
    case PrimaryEffect::Once:
        return ICON_FA_BOLT;
    case PrimaryEffect::Spawn:
        return ICON_FA_SEEDLING;
    default:
        return ICON_FA_CIRCLE_NOTCH;
    }
}

const char *ParticleCSFieldManager::FieldSummary(const ParticleField &field)
{
    switch (GetPrimaryEffect(field))
    {
    case PrimaryEffect::Vortex:
        return "渦";
    case PrimaryEffect::Pull:
        return "引き寄せ";
    case PrimaryEffect::Push:
        return "押し出し";
    case PrimaryEffect::Wind:
        return "風";
    case PrimaryEffect::Drag:
        return "抵抗";
    case PrimaryEffect::Kill:
        return "消す";
    case PrimaryEffect::Life:
        return "寿命";
    case PrimaryEffect::Tint:
        return "色";
    case PrimaryEffect::Size:
        return "大きさ";
    case PrimaryEffect::Trail:
        return "トレイル";
    case PrimaryEffect::Once:
        return "入った瞬間";
    case PrimaryEffect::Spawn:
        return "発生";
    default:
        return "効果なし";
    }
}

// =============================================
// 追加・削除
// =============================================

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
    // 旧形式のキーが残ると読み分けを誤るので、書く前に全部消す
    data.RemoveByPrefix("");
    data.Save("version", kFieldFormatVersion);
    data.Save("name", field.name);
    data.Save("enabled", field.enabled);

    // 形と範囲
    data.Save("shape", static_cast<int>(field.shape));
    data.Save<Vector3>("position", field.position);
    data.Save<Vector3>("rotation", field.rotation);
    data.Save("radius", field.radius);
    data.Save<Vector3>("boxSize", field.boxSize);
    data.Save("height", field.height);
    data.Save("falloff", static_cast<int>(field.falloff));
    data.Save("falloffStart", field.falloffStart);
    data.Save("layers", field.layers);

    // 効果（有効なものだけ書く。読むときは無ければ無効）
    if (field.wind.enabled)
    {
        data.Save<Vector3>("wind_direction", field.wind.direction);
        data.Save("wind_speed", field.wind.speed);
        data.Save("wind_response", field.wind.response);
    }
    if (field.attract.enabled)
    {
        data.Save("attract_strength", field.attract.strength);
        data.Save("attract_absorbRadius", field.attract.absorbRadius);
    }
    if (field.vortex.enabled)
    {
        data.Save("vortex_speed", field.vortex.speed);
        data.Save("vortex_response", field.vortex.response);
    }
    if (field.drag.enabled)
    {
        data.Save("drag_perSecond", field.drag.perSecond);
    }
    if (field.tint.enabled)
    {
        data.Save<Vector4>("tint_color", field.tint.color);
    }
    if (field.size.enabled)
    {
        data.Save("size_scale", field.size.scale);
    }
    if (field.life.enabled)
    {
        data.Save("life_speed", field.life.speed);
        data.Save("life_killOnEnter", field.life.killOnEnter);
    }
    if (field.trail.enabled)
    {
        data.Save("trail_spawnDistance", field.trail.spawnDistance);
    }
    if (field.once.enabled)
    {
        data.Save("once_enabled", true);
        SaveOverrideData(data, field.once.settings);
    }
    if (field.spawn.enabled)
    {
        data.Save("spawn_count", static_cast<int>(field.spawn.count));
        data.Save("spawn_interval", field.spawn.interval);
        data.Save("spawn_lifeMin", field.spawn.lifeMin);
        data.Save("spawn_lifeMax", field.spawn.lifeMax);
    }
}

void ParticleCSFieldManager::LoadFieldData(DataHandler &data, ParticleField &field)
{
    if (data.Load("version", 1) < kFieldFormatVersion)
    {
        LoadLegacyFieldData(data, field);
        return;
    }
    field.name = data.Load("name", field.name);
    field.enabled = data.Load("enabled", field.enabled);

    field.shape = static_cast<ParticleFieldShape>(std::clamp(data.Load("shape", 0), 0, 2));
    field.position = data.Load<Vector3>("position", field.position);
    field.rotation = data.Load<Vector3>("rotation", field.rotation);
    field.radius = data.Load("radius", field.radius);
    field.boxSize = data.Load<Vector3>("boxSize", field.boxSize);
    field.height = data.Load("height", field.height);
    field.falloff = static_cast<ParticleFieldFalloff>(std::clamp(data.Load("falloff", 2), 0, 2));
    field.falloffStart = data.Load("falloffStart", field.falloffStart);
    field.layers = data.Load<uint32_t>("layers", field.layers);

    field.wind.enabled = data.Contains("wind_speed");
    field.wind.direction = data.Load<Vector3>("wind_direction", field.wind.direction);
    field.wind.speed = data.Load("wind_speed", field.wind.speed);
    field.wind.response = data.Load("wind_response", field.wind.response);

    field.attract.enabled = data.Contains("attract_strength");
    field.attract.strength = data.Load("attract_strength", field.attract.strength);
    field.attract.absorbRadius = data.Load("attract_absorbRadius", field.attract.absorbRadius);

    field.vortex.enabled = data.Contains("vortex_speed");
    field.vortex.speed = data.Load("vortex_speed", field.vortex.speed);
    field.vortex.response = data.Load("vortex_response", field.vortex.response);

    field.drag.enabled = data.Contains("drag_perSecond");
    field.drag.perSecond = data.Load("drag_perSecond", field.drag.perSecond);

    field.tint.enabled = data.Contains("tint_color");
    field.tint.color = data.Load<Vector4>("tint_color", field.tint.color);

    field.size.enabled = data.Contains("size_scale");
    field.size.scale = data.Load("size_scale", field.size.scale);

    field.life.enabled = data.Contains("life_speed");
    field.life.speed = data.Load("life_speed", field.life.speed);
    field.life.killOnEnter = data.Load("life_killOnEnter", field.life.killOnEnter);

    field.trail.enabled = data.Contains("trail_spawnDistance");
    field.trail.spawnDistance = data.Load("trail_spawnDistance", field.trail.spawnDistance);

    field.once.enabled = data.Load("once_enabled", false);
    if (field.once.enabled)
    {
        LoadOverrideData(data, field.once.settings);
    }

    field.spawn.enabled = data.Contains("spawn_count");
    field.spawn.count = static_cast<uint32_t>((std::max)(0, data.Load("spawn_count", static_cast<int>(field.spawn.count))));
    field.spawn.interval = data.Load("spawn_interval", field.spawn.interval);
    field.spawn.lifeMin = data.Load("spawn_lifeMin", field.spawn.lifeMin);
    field.spawn.lifeMax = data.Load("spawn_lifeMax", field.spawn.lifeMax);
    field.spawn.timer = 0.0f;
    field.spawn.burst = 0;
}

void ParticleCSFieldManager::LoadLegacyFieldData(DataHandler &data, ParticleField &field)
{
    // 旧形式: 球だけ・fieldType で力を1つ選ぶ・機能ごとに enableXxx。
    // 力は「加速度」だったので、風と渦は「その値の速さへ1秒ほどでなじむ」に読み替える。
    field.name = data.Load("name", field.name);
    field.enabled = data.Load("enabled", field.enabled);
    field.shape = ParticleFieldShape::Sphere;
    field.position = data.Load<Vector3>("position", field.position);
    field.radius = data.Load("radius", field.radius);

    // 減衰指数 → 弱まり方（1 付近 = 直線 / 小さい = ほぼ一定 / 大きい = なめらか）
    const float exponent = data.Load("falloff", 1.0f);
    field.falloffStart = 0.0f;
    field.falloff = (exponent < 0.5f) ? ParticleFieldFalloff::Constant
                                      : ((exponent <= 1.5f) ? ParticleFieldFalloff::Linear : ParticleFieldFalloff::Smooth);

    const uint32_t fieldType = data.Load<uint32_t>("fieldType", 0u);
    const Vector3 direction = data.Load<Vector3>("direction", {1.0f, 0.0f, 0.0f});
    const float strength = data.Load("strength", 1.0f);
    const bool hasDirection = direction.Length() > 1e-4f;
    switch (fieldType)
    {
    case 0: // 風（向きが 0 のものは力を持たない。発生範囲だけに使われていた）
        field.wind.enabled = hasDirection && std::abs(strength) > 1e-4f;
        field.wind.direction = hasDirection ? direction : Vector3{1.0f, 0.0f, 0.0f};
        field.wind.speed = strength;
        field.wind.response = 1.0f;
        break;
    case 1: // 引力
        field.attract.enabled = true;
        field.attract.strength = strength;
        break;
    case 2: // 斥力
        field.attract.enabled = true;
        field.attract.strength = -strength;
        break;
    case 3: // 渦（旧 direction を回転軸に。形の Y 軸をそちらへ向ける）
    {
        field.vortex.enabled = true;
        field.vortex.speed = strength;
        field.vortex.response = 1.0f;
        const Vector3 axis = SafeNormalize(direction, {0.0f, 1.0f, 0.0f});
        const float pitch = std::acos(std::clamp(axis.y, -1.0f, 1.0f));
        const float yaw = std::atan2(axis.x, axis.z);
        field.rotation = {pitch, yaw, 0.0f};
        break;
    }
    default:
        break;
    }

    if (data.Load<uint32_t>("enableLifeDrain", 0u) != 0)
    {
        field.life.enabled = true;
        field.life.speed = 1.0f + data.Load("lifeTimeDrain", 0.0f);
    }
    if (data.Load<uint32_t>("enableForceTrail", 0u) != 0)
    {
        field.trail.enabled = true;
        field.trail.spawnDistance = data.Load("trailSpawnDistanceOverride", 0.0f);
    }
    if (data.Load<uint32_t>("enableColorMultiply", 0u) != 0)
    {
        field.tint.enabled = true;
        field.tint.color = data.Load<Vector4>("colorMultiplier", field.tint.color);
    }
    if (data.Load<uint32_t>("enableSettingsOverride", 0u) != 0)
    {
        field.once.enabled = true;
        LoadOverrideData(data, field.once.settings);
    }
    if (data.Load<uint32_t>("enableEmitSpawn", 0u) != 0)
    {
        field.spawn.enabled = true;
        field.spawn.count = static_cast<uint32_t>((std::max)(0, data.Load("emitSpawnCount", 1000)));
        field.spawn.interval = data.Load("emitSpawnInterval", 0.0f);
        field.spawn.lifeMin = data.Load("emitSpawnLifeTimeMin", 0.25f);
        field.spawn.lifeMax = data.Load("emitSpawnLifeTimeMax", 0.25f);
    }

    // 旧 groupId: -1 = 全部 / n = レイヤー n+1 だけ（エミッター側も同じ規則で読み替えるので意味は変わらない）
    const int groupId = data.Load("groupId", -1);
    field.layers = (groupId < 0 || groupId >= 32) ? 0xFFFFFFFFu : (1u << groupId);
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
    return field;
}

ParticleField *ParticleCSFieldManager::CreateField(const std::string &name, const std::string &templateName)
{
    if (static_cast<uint32_t>(fields_.size()) >= kMaxFields)
    {
        return nullptr;
    }

    ParticleField newField;
    if (!templateName.empty())
    {
        // テンプレートjsonが指定されていれば、そのデータを複製して土台にする
        newField = LoadField(templateName, ParticleField{});
    }
    // 名前は引数で上書き（テンプレートの名前ではなく指定名を使う）
    newField.name = name;

    // 自身のjsonが既に存在すれば、それをロードして上書きする（再起動後の復元など）
    {
        std::unique_ptr<DataHandler> selfData = std::make_unique<DataHandler>("ParticleField", name);
        if (selfData->Exists())
        {
            LoadFieldData(*selfData, newField);
            newField.name = name;
        }
    }

    fields_.push_back(newField);
    return &fields_.back();
}

// =============================================
// ギズモ描画
// =============================================

void ParticleCSFieldManager::DrawFieldGizmos()
{
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i)
    {
        if (fields_[i].enabled)
        {
            DrawFieldGizmo(i);
        }
    }
}

void ParticleCSFieldManager::DrawFieldGizmo(int index)
{
    LineCategoryScope lineScope(LineCategory::Particle);
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        return;
    const ParticleField &field = fields_[index];
    LineRenderer *line = LineRenderer::GetInstance();

    Vector4 color = FieldColor(field);
    color.w = field.enabled ? 0.9f : 0.3f;
    Vector4 dim = color;
    dim.w *= 0.4f;

    Vector3 axisX;
    Vector3 axisY;
    Vector3 axisZ;
    FieldAxes(field, axisX, axisY, axisZ);
    const Vector3 half = FieldHalfExtent(field);
    const Vector3 &center = field.position;
    const float core = (field.falloff == ParticleFieldFalloff::Constant) ? 0.0f : std::clamp(field.falloffStart, 0.0f, 0.95f);

    // ---- 形（外側の境界と、100% で効く芯）----
    auto drawShape = [&](float scale, const Vector4 &shapeColor) {
        switch (field.shape)
        {
        case ParticleFieldShape::Box:
        {
            const Vector3 x = axisX * (half.x * scale);
            const Vector3 y = axisY * (half.y * scale);
            const Vector3 z = axisZ * (half.z * scale);
            // 0-3 が手前面（-Z 側）、4-7 が奥面
            const Vector3 corners[8] = {
                center - x - y - z, center + x - y - z, center + x + y - z, center - x + y - z,
                center - x - y + z, center + x - y + z, center + x + y + z, center - x + y + z,
            };
            line->AddBoxCorners(corners, shapeColor);
            break;
        }
        case ParticleFieldShape::Cylinder:
        {
            const Vector3 u = axisX * (half.x * scale);
            const Vector3 v = axisZ * (half.x * scale);
            const Vector3 top = center + axisY * (half.y * scale);
            const Vector3 bottom = center - axisY * (half.y * scale);
            line->AddCircle(top, u, v, shapeColor, 32);
            line->AddCircle(bottom, u, v, shapeColor, 32);
            for (const Vector3 &side : {u, u * -1.0f, v, v * -1.0f})
            {
                line->AddLine(bottom + side, top + side, shapeColor);
            }
            break;
        }
        default:
            line->AddSphere(center, half.x * scale, shapeColor, 24);
            break;
        }
    };
    drawShape(1.0f, color);
    if (core > 0.05f)
    {
        drawShape(core, dim);
    }

    const float reach = (std::max)({half.x, half.y, half.z});

    // ---- 風: 中心を通る矢印（長さは風速に合わせて最大で範囲の 8 割）----
    if (field.wind.enabled)
    {
        const Vector3 direction = SafeNormalize(field.wind.direction, {1.0f, 0.0f, 0.0f}) * (field.wind.speed >= 0.0f ? 1.0f : -1.0f);
        const float length = (std::min)(reach * 0.8f, 0.5f + std::abs(field.wind.speed) * 0.2f);
        const Vector3 side = SafeNormalize(
            direction.Cross((std::abs(direction.y) > 0.9f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
        for (float offset : {-0.4f, 0.0f, 0.4f})
        {
            const Vector3 from = center + side * (reach * offset) - direction * (length * 0.5f);
            DrawArrow(line, from, from + direction * length, color);
        }
    }

    // ---- 渦: 形の Y 軸まわりに、回る向きの矢印付きの輪 ----
    if (field.vortex.enabled)
    {
        const float ringRadius = half.x * 0.6f;
        const float sign = (field.vortex.speed >= 0.0f) ? 1.0f : -1.0f;
        line->AddCircle(center, axisX * ringRadius, axisZ * ringRadius, color, 32);
        line->AddLine(center - axisY * half.y, center + axisY * half.y, dim);
        constexpr int kArrows = 4;
        for (int a = 0; a < kArrows; ++a)
        {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(a) / kArrows;
            const Vector3 radial = axisX * std::cos(angle) + axisZ * std::sin(angle);
            const Vector3 point = center + radial * ringRadius;
            // 接線 = Y 軸 × 半径方向（シェーダーの cross(axisY, radial) と同じ向き）
            const Vector3 tangent = axisY.Cross(radial) * sign;
            DrawArrow(line, point - tangent * (ringRadius * 0.3f), point + tangent * (ringRadius * 0.3f), color);
        }
    }

    // ---- 引き寄せ / 押し出し: 6方向の矢印（内向き / 外向き）と、消える範囲 ----
    if (field.attract.enabled)
    {
        const bool pull = field.attract.strength >= 0.0f;
        for (const Vector3 &axis : {axisX, axisX * -1.0f, axisY, axisY * -1.0f, axisZ, axisZ * -1.0f})
        {
            const Vector3 outer = center + axis * (reach * 0.9f);
            const Vector3 inner = center + axis * (reach * 0.45f);
            DrawArrow(line, pull ? outer : inner, pull ? inner : outer, color);
        }
        if (field.attract.absorbRadius > 0.0f)
        {
            line->AddSphere(center, field.attract.absorbRadius, dim, 12);
        }
    }
}
} // namespace Hagine
