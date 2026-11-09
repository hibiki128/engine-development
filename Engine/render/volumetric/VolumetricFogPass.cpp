#include "VolumetricFogPass.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <MyMath.h>
#include <camera/projection/ViewProjection.h>
#include <data/DataHandler.h>
#include <debug/log/Logger.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>
#include <light/LightGroup.h>
#include <render/raytracing/RaytracingScene.h>
#include <shadow/ShadowMap.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#endif // USE_IMGUI

#include <algorithm>
#include <cmath>

namespace Hagine {
namespace {
constexpr const char *kInjectShader = "OffScreen/VolumetricFogInject.CS.hlsl";
constexpr const char *kInjectRaytracedShader = "OffScreen/VolumetricFogInjectRt.CS.hlsl";
constexpr const char *kIntegrateShader = "OffScreen/VolumetricFogIntegrate.CS.hlsl";
constexpr const char *kApplyShader = "OffScreen/VolumetricFogApply.CS.hlsl";
constexpr const char *kDataFileName = "VolumetricFogData";
// RayQuery を使うので cs_6_5 以上
constexpr const wchar_t *kRayQueryProfile = L"cs_6_5";

// 注入の入力テーブル（t0=前フレームの注入結果 / t1=シャドウマップ / t2=光源 / t3=加速構造）
constexpr uint32_t kInjectTableSize = 4;
// 合成の入力テーブル（t0=積分結果 / t1=深度）
constexpr uint32_t kApplyTableSize = 2;

// 一番手前のスライスの奥行き。0 に近いほどカメラ直前に格子を使ってしまう
constexpr float kNearDistance = 0.2f;
// 1フレームでこれ以上カメラが動いたら、前フレームの結果を混ぜない
constexpr float kCameraCutDistance = 5.0f;
// ジッタの周期
constexpr uint32_t kJitterPeriod = 16;

/// <summary>
/// Halton 列の値（0〜1）
/// </summary>
float Halton(uint32_t index, uint32_t base)
{
    float fraction = 1.0f;
    float result = 0.0f;
    while (index > 0)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}
} // namespace

void VolumetricFogPass::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();

    LoadData();

    injectCb_ = pDxCommon_->CreateBufferResource(sizeof(InjectConstants));
    injectCb_->Map(0, nullptr, reinterpret_cast<void **>(&pInjectCb_));
    integrateCb_ = pDxCommon_->CreateBufferResource(sizeof(IntegrateConstants));
    integrateCb_->Map(0, nullptr, reinterpret_cast<void **>(&pIntegrateCb_));
    applyCb_ = pDxCommon_->CreateBufferResource(sizeof(ApplyConstants));
    applyCb_->Map(0, nullptr, reinterpret_cast<void **>(&pApplyCb_));

    CreateResources();
}

void VolumetricFogPass::Finalize()
{
    pInjectCb_ = nullptr;
    pIntegrateCb_ = nullptr;
    pApplyCb_ = nullptr;
    pLights_ = nullptr;
    injectCb_.Reset();
    integrateCb_.Reset();
    applyCb_.Reset();
    lightBuffer_.Reset();
    scatterVolumes_[0].Reset();
    scatterVolumes_[1].Reset();
    integratedVolume_.Reset();
    initialized_ = false;
    pDxCommon_ = nullptr;
    pSrvManager_ = nullptr;
}

uint32_t VolumetricFogPass::AllocateTable(uint32_t size)
{
    // SrvManager は Allocate() が返した番号 r に対して r+1 へ書き込む規約
    uint32_t firstAllocated = 0;
    bool contiguous = true;
    for (uint32_t i = 0; i < size; ++i)
    {
        const uint32_t index = pSrvManager_->Allocate();
        if (i == 0)
        {
            firstAllocated = index;
        }
        else if (index != firstAllocated + i)
        {
            contiguous = false;
        }
    }
    return contiguous ? (firstAllocated + 1) : 0;
}

Microsoft::WRL::ComPtr<ID3D12Resource> VolumetricFogPass::CreateVolume(const wchar_t *name)
{
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = kGridX;
    desc.Height = kGridY;
    desc.DepthOrArraySize = static_cast<UINT16>(kGridZ);
    desc.MipLevels = 1;
    desc.Format = kVolumeFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = pDxCommon_->GetDevice()->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(hr))
    {
        return nullptr;
    }
    resource->SetName(name);
    return resource;
}

void VolumetricFogPass::WriteVolumeSrv(uint32_t index, ID3D12Resource *resource)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = kVolumeFormat;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MipLevels = 1;
    pDxCommon_->GetDevice()->CreateShaderResourceView(resource, &desc, pSrvManager_->GetCPUDescriptorHandle(index));
}

void VolumetricFogPass::WriteVolumeUav(uint32_t index, ID3D12Resource *resource)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = kVolumeFormat;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MipSlice = 0;
    desc.Texture3D.FirstWSlice = 0;
    desc.Texture3D.WSize = kGridZ;
    pDxCommon_->GetDevice()->CreateUnorderedAccessView(resource, nullptr, &desc,
                                                        pSrvManager_->GetCPUDescriptorHandle(index));
}

void VolumetricFogPass::CreateResources()
{
    scatterVolumes_[0] = CreateVolume(L"VolumetricFog_Scatter0");
    scatterVolumes_[1] = CreateVolume(L"VolumetricFog_Scatter1");
    integratedVolume_ = CreateVolume(L"VolumetricFog_Integrated");
    if (!scatterVolumes_[0] || !scatterVolumes_[1] || !integratedVolume_)
    {
        Logger::Error("ボリュメトリックフォグの格子テクスチャを作成できませんでした。");
        return;
    }

    lightBuffer_ = pDxCommon_->CreateBufferResource(sizeof(VolumeLight) * kMaxLights);
    lightBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pLights_));

    for (uint32_t i = 0; i < 2; ++i)
    {
        scatterSrvIndex_[i] = pSrvManager_->Allocate() + 1;
        WriteVolumeSrv(scatterSrvIndex_[i], scatterVolumes_[i].Get());
        scatterUavIndex_[i] = pSrvManager_->Allocate() + 1;
        WriteVolumeUav(scatterUavIndex_[i], scatterVolumes_[i].Get());
    }
    integratedUavIndex_ = pSrvManager_->Allocate() + 1;
    WriteVolumeUav(integratedUavIndex_, integratedVolume_.Get());

    injectTableIndex_[0] = AllocateTable(kInjectTableSize);
    injectTableIndex_[1] = AllocateTable(kInjectTableSize);
    applyTableIndex_ = AllocateTable(kApplyTableSize);
    if (injectTableIndex_[0] == 0 || injectTableIndex_[1] == 0 || applyTableIndex_ == 0)
    {
        Logger::Error("ボリュメトリックフォグ用のディスクリプタを連続で確保できませんでした。");
        return;
    }

    // 書き込み先 i のとき、前フレームの結果は i ^ 1
    for (uint32_t i = 0; i < 2; ++i)
    {
        WriteVolumeSrv(injectTableIndex_[i], scatterVolumes_[i ^ 1].Get());
        pSrvManager_->CreateSRVforStructuredBuffer(injectTableIndex_[i] + 2, lightBuffer_.Get(), kMaxLights,
                                                   sizeof(VolumeLight));
    }
    WriteVolumeSrv(applyTableIndex_, integratedVolume_.Get());

    initialized_ = true;
}

uint32_t VolumetricFogPass::UploadLights()
{
    if (!pLights_)
    {
        return 0;
    }
    const LightGroup *lightGroup = LightGroup::GetInstance();
    uint32_t count = 0;

    for (const PointLightGroup::Entry &entry : lightGroup->GetPointLights().GetEntries())
    {
        const PointLightData &light = entry.gpu;
        if (count >= kMaxLights)
        {
            break;
        }
        if (!light.active || light.intensity <= 0.0f || light.radius <= 0.0f)
        {
            continue;
        }
        VolumeLight &out = pLights_[count++];
        out.position = light.position;
        out.range = light.radius;
        out.color = Vector3(light.color.x, light.color.y, light.color.z) * light.intensity;
        out.decay = light.decay;
        out.direction = Vector3(0.0f, -1.0f, 0.0f);
        out.cosAngle = -1.0f;
        out.type = 0;
    }

    for (const SpotLightGroup::Entry &entry : lightGroup->GetSpotLights().GetEntries())
    {
        const SpotLightData &light = entry.gpu;
        if (count >= kMaxLights)
        {
            break;
        }
        if (!light.active || light.intensity <= 0.0f || light.distance <= 0.0f)
        {
            continue;
        }
        VolumeLight &out = pLights_[count++];
        out.position = light.position;
        out.range = light.distance;
        out.color = Vector3(light.color.x, light.color.y, light.color.z) * light.intensity;
        out.decay = light.decay;
        out.direction = light.direction.Length() > 1e-6f ? light.direction.Normalize() : Vector3(0.0f, -1.0f, 0.0f);
        out.cosAngle = light.cosAngle;
        out.type = 1;
    }
    return count;
}

void VolumetricFogPass::Render(const ViewProjection &viewProjection, ID3D12Resource *sceneResource)
{
    if (!initialized_ || !enabled_ || sceneResource == nullptr)
    {
        historyValid_ = false;
        return;
    }

    const std::vector<ShaderRootSignature::SamplerPreset> samplers = {
        ShaderRootSignature::SamplerPreset::LinearClamp,
        ShaderRootSignature::SamplerPreset::PointClamp,
    };
    ComputeEffectPipeline *pipeline = ComputeEffectPipeline::GetInstance();
    const ComputeEffectProgram *inject = pipeline->Get(kInjectShader, samplers);
    const ComputeEffectProgram *integrate = pipeline->Get(kIntegrateShader, samplers);
    const ComputeEffectProgram *apply = pipeline->Get(kApplyShader, samplers);
    if (!inject || !inject->IsValid() || !integrate || !integrate->IsValid() || !apply || !apply->IsValid())
    {
        historyValid_ = false;
        return;
    }

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pSrvManager_->SetDescriptorHeap();

    // ディスパッチ1回ぶん。SRV/UAV/CBV をバインドして投げる
    auto dispatch = [&](const ComputeEffectProgram *program, uint32_t srvTable, uint32_t uavIndex, ID3D12Resource *cb,
                        uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ) {
        pCommandList->SetPipelineState(program->pipelineState.Get());
        pCommandList->SetComputeRootSignature(program->rootSignature.Get());
        if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(srvTable));
        }
        if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetUavTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(uavIndex));
        }
        const UINT cbvIndex = program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
        {
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, cb->GetGPUVirtualAddress());
        }
        const UINT groupX = (sizeX + program->threadGroupSizeX - 1) / program->threadGroupSizeX;
        const UINT groupY = (sizeY + program->threadGroupSizeY - 1) / program->threadGroupSizeY;
        const UINT groupZ = (sizeZ + program->threadGroupSizeZ - 1) / program->threadGroupSizeZ;
        pCommandList->Dispatch(groupX, groupY, groupZ);
    };

    // ---- カメラ ----
    const Matrix4x4 &view = viewProjection.matView_;
    const Matrix4x4 &projection = viewProjection.matProjection_;
    const Matrix4x4 viewProjectionMatrix = view * projection;
    const Vector3 cameraPosition = viewProjection.translation_;
    const float farDistance = std::max(farDistance_, kNearDistance + 1.0f);

    // カメラが瞬間移動したら前フレームの結果は使えない
    if ((cameraPosition - previousCameraPosition_).Length() > kCameraCutDistance)
    {
        historyValid_ = false;
    }

    const uint32_t writeIndex = writeIndex_;
    const uint32_t jitterIndex = (frameIndex_ % kJitterPeriod) + 1;

    // ---- 1. 注入（セルごとの霧の濃さと届く光）----
    {
        const LightGroup *lightGroup = LightGroup::GetInstance();
        const DirectionalLight &directional = lightGroup->GetDirectionalLight();
        const Vector4 directionalColor = directional.GetColor();
        const float directionalIntensity = directional.IsEnabled() ? directional.GetIntensity() * directionalScale_ : 0.0f;
        ShadowMap *shadowMap = ShadowMap::GetInstance();

        InjectConstants &cb = *pInjectCb_;
        cb.inverseView = Inverse(view);
        cb.previousView = previousView_;
        cb.previousViewProjection = previousViewProjection_;
        cb.lightViewProjection = shadowMap->GetLightViewProjection();
        cb.cameraPosition = cameraPosition;
        cb.nearDistance = kNearDistance;
        cb.albedo = albedo_;
        cb.farDistance = farDistance;
        cb.directionalColor = Vector3(directionalColor.x, directionalColor.y, directionalColor.z) * directionalIntensity;
        cb.density = density_;
        cb.directionalDirection = directional.GetDirection();
        cb.heightFalloff = heightFalloff_;
        cb.ambientColor = ambientColor_;
        cb.baseHeight = baseHeight_;
        cb.jitter = Vector3(Halton(jitterIndex, 2) - 0.5f, Halton(jitterIndex, 3) - 0.5f, Halton(jitterIndex, 5));
        cb.historyBlend = std::clamp(historyBlend_, 0.0f, 0.98f);
        cb.projectionScaleX = projection.m[0][0];
        cb.projectionScaleY = projection.m[1][1];
        cb.anisotropy = anisotropy_;
        cb.shadowBias = shadowBias_;
        cb.lightCount = UploadLights();
        cb.shadowEnabled = (shadowMap->IsEnabled() && shadowMap->GetDepthResource()) ? 1u : 0u;
        cb.historyValid = historyValid_ ? 1u : 0u;
        cb.localLightScale = localLightScale_;
        cb.gridSize[0] = static_cast<int32_t>(kGridX);
        cb.gridSize[1] = static_cast<int32_t>(kGridY);
        cb.gridSize[2] = static_cast<int32_t>(kGridZ);

        // シャドウマップは作り直されることがあるので毎フレーム差し直す
        pSrvManager_->CreateSRVforShadowDepth(injectTableIndex_[writeIndex] + 1, shadowMap->GetDepthResource());

        // 点光源・スポットライトの影は、加速構造があるときだけレイトレで調べる（無ければ影なしで注入）
        const ComputeEffectProgram *injectProgram = inject;
        raytracedShadowActive_ = false;
        if (localLightShadow_ && cb.lightCount > 0)
        {
            RaytracingScene *raytracingScene = RaytracingScene::GetInstance();
            if (raytracingScene->IsAvailable())
            {
                raytracingScene->RequestUse();
                raytracingScene->EnsureTlas();
                const ComputeEffectProgram *raytraced =
                    raytracingScene->HasValidTlas() ? pipeline->Get(kInjectRaytracedShader, samplers, kRayQueryProfile) : nullptr;
                if (raytraced && raytraced->IsValid())
                {
                    pSrvManager_->CreateSRVforTlas(injectTableIndex_[writeIndex] + 3, raytracingScene->GetTlasGpuAddress());
                    injectProgram = raytraced;
                    raytracedShadowActive_ = true;
                }
            }
        }
        cb.localLightShadow = raytracedShadowActive_ ? 1u : 0u;
        cb.lightShadowOffset = std::max(lightShadowOffset_, 0.0f);

        pDxCommon_->BarrierTransition(scatterVolumes_[writeIndex].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(injectProgram, injectTableIndex_[writeIndex], scatterUavIndex_[writeIndex], injectCb_.Get(), kGridX,
                 kGridY, kGridZ);
        pDxCommon_->BarrierTransition(scatterVolumes_[writeIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // ---- 2. 積分（手前から奥へ散乱と透過率を溜める）----
    {
        IntegrateConstants &cb = *pIntegrateCb_;
        cb.projectionScaleX = projection.m[0][0];
        cb.projectionScaleY = projection.m[1][1];
        cb.nearDistance = kNearDistance;
        cb.farDistance = farDistance;
        cb.gridSize[0] = static_cast<int32_t>(kGridX);
        cb.gridSize[1] = static_cast<int32_t>(kGridY);
        cb.gridSize[2] = static_cast<int32_t>(kGridZ);

        pDxCommon_->BarrierTransition(integratedVolume_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(integrate, scatterSrvIndex_[writeIndex], integratedUavIndex_, integrateCb_.Get(), kGridX, kGridY, 1);
        pDxCommon_->BarrierTransition(integratedVolume_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // ---- 3. 合成（画素の奥行きで積分結果を引いてシーンへ掛ける）----
    {
        const uint32_t width = WinApp::GetVirtualWidth();
        const uint32_t height = WinApp::GetVirtualHeight();

        ApplyConstants &cb = *pApplyCb_;
        cb.depthScale = projection.m[3][2];
        cb.depthOffset = projection.m[2][2];
        cb.nearDistance = kNearDistance;
        cb.farDistance = farDistance;
        cb.textureSize[0] = static_cast<int32_t>(width);
        cb.textureSize[1] = static_cast<int32_t>(height);
        cb.gridDepth = static_cast<int32_t>(kGridZ);
        cb.skyStrength = std::clamp(skyStrength_, 0.0f, 1.0f);

        ID3D12Resource *depthResource = pDxCommon_->GetDepthStencilResource();
        pSrvManager_->CreateSRVforDepth(applyTableIndex_ + 1, depthResource);

        pDxCommon_->BarrierTransition(depthResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, DirectXCommon::kDepthReadState);
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(apply, applyTableIndex_, pDxCommon_->GetOffScreenUavIndex(), applyCb_.Get(), width, height, 1);
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
        pDxCommon_->BarrierTransition(depthResource, DirectXCommon::kDepthReadState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    // ---- 次フレームへの引き継ぎ ----
    previousView_ = view;
    previousViewProjection_ = viewProjectionMatrix;
    previousCameraPosition_ = cameraPosition;
    historyValid_ = true;
    writeIndex_ ^= 1u;
    ++frameIndex_;
}

void VolumetricFogPass::LoadData()
{
    DataHandler data("PostEffect", kDataFileName);
    enabled_ = data.Load<bool>("enabled", false);
    density_ = data.Load<float>("density", 0.02f);
    heightFalloff_ = data.Load<float>("heightFalloff", 0.1f);
    baseHeight_ = data.Load<float>("baseHeight", 0.0f);
    albedo_ = data.Load<Vector3>("albedo", {1.0f, 1.0f, 1.0f});
    anisotropy_ = data.Load<float>("anisotropy", 0.6f);
    farDistance_ = data.Load<float>("farDistance", 64.0f);
    directionalScale_ = data.Load<float>("directionalScale", 1.0f);
    localLightScale_ = data.Load<float>("localLightScale", 1.0f);
    ambientColor_ = data.Load<Vector3>("ambientColor", {0.02f, 0.025f, 0.03f});
    historyBlend_ = data.Load<float>("historyBlend", 0.9f);
    shadowBias_ = data.Load<float>("shadowBias", 0.002f);
    skyStrength_ = data.Load<float>("skyStrength", 1.0f);
    localLightShadow_ = data.Load<bool>("localLightShadow", true);
    lightShadowOffset_ = data.Load<float>("lightShadowOffset", 0.3f);
}

void VolumetricFogPass::SaveData()
{
    DataHandler data("PostEffect", kDataFileName);
    data.Save<bool>("enabled", enabled_);
    data.Save<float>("density", density_);
    data.Save<float>("heightFalloff", heightFalloff_);
    data.Save<float>("baseHeight", baseHeight_);
    data.Save<Vector3>("albedo", albedo_);
    data.Save<float>("anisotropy", anisotropy_);
    data.Save<float>("farDistance", farDistance_);
    data.Save<float>("directionalScale", directionalScale_);
    data.Save<float>("localLightScale", localLightScale_);
    data.Save<Vector3>("ambientColor", ambientColor_);
    data.Save<float>("historyBlend", historyBlend_);
    data.Save<float>("shadowBias", shadowBias_);
    data.Save<float>("skyStrength", skyStrength_);
    data.Save<bool>("localLightShadow", localLightShadow_);
    data.Save<float>("lightShadowOffset", lightShadowOffset_);
}

void VolumetricFogPass::DrawImGui()
{
#ifdef USE_IMGUI
    if (!ThemedHeader("ボリュメトリックフォグ", DebugTheme::kAccentBlue, true))
    {
        return;
    }
    ImGui::Indent();

    AccentCheckbox("有効##volumetricFog", &enabled_, DebugTheme::kAccentGreen);

    ImGui::SeparatorText("霧");
    ImGui::DragFloat("濃さ##vfDensity", &density_, 0.001f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("高さによる減衰##vfFalloff", &heightFalloff_, 0.005f, 0.0f, 2.0f, "%.3f");
    ImGui::DragFloat("基準の高さ##vfBaseHeight", &baseHeight_, 0.1f, -1000.0f, 1000.0f, "%.1f");
    ImGui::ColorEdit3("色##vfAlbedo", &albedo_.x);
    ImGui::DragFloat("最大距離##vfFar", &farDistance_, 0.5f, 4.0f, 500.0f, "%.1f m");
    ImGui::DragFloat("空への影響##vfSky", &skyStrength_, 0.01f, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("光");
    ImGui::DragFloat("散乱の偏り##vfAnisotropy", &anisotropy_, 0.01f, -0.9f, 0.9f, "%.2f");
    ImGui::DragFloat("平行光源##vfDirectional", &directionalScale_, 0.01f, 0.0f, 20.0f, "%.2f");
    ImGui::DragFloat("点光源・スポット##vfLocal", &localLightScale_, 0.01f, 0.0f, 20.0f, "%.2f");
    ImGui::ColorEdit3("環境光##vfAmbient", &ambientColor_.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

    ImGui::SeparatorText("品質");
    ImGui::DragFloat("前フレームの割合##vfHistory", &historyBlend_, 0.005f, 0.0f, 0.98f, "%.3f");
    ImGui::DragFloat("影のバイアス##vfShadowBias", &shadowBias_, 0.0001f, 0.0f, 0.05f, "%.4f");

    ImGui::SeparatorText("点光源・スポットの影");
    AccentCheckbox("レイトレで影を出す##vfLocalShadow", &localLightShadow_, DebugTheme::kAccentGreen);
    ImGui::DragFloat("光源からのオフセット##vfShadowOffset", &lightShadowOffset_, 0.01f, 0.0f, 5.0f, "%.2f m");
    if (localLightShadow_ && !RaytracingScene::GetInstance()->IsAvailable())
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, "レイトレ非対応のため影なし");
    }

    ImGui::TextDisabled("格子 %u x %u x %u", kGridX, kGridY, kGridZ);

    if (ConfirmButton("保存##volumetricFog", ImVec2(-1.0f, 0.0f)))
    {
        SaveData();
    }

    ImGui::Unindent();
#endif // USE_IMGUI
}
} // namespace Hagine
