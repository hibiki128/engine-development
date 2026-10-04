#include "TransitionRenderer.h"
#include "DirectXCommon.h"
#include <asset/AssetPath.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>
#include <graphics/texture/TextureManager.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Hagine {
namespace {

constexpr const char *kShaderFile = "Transition/Transition.CS.hlsl";
constexpr const char *kWhiteTexture = "debug/white1x1.png";
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr uint64_t kConstantBufferStride = (sizeof(TransitionGpuData) + 255) & ~255ull;

float ToRadians(float degrees)
{
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

/// <summary>色の欄は画面に見えている色（sRGB）で選ぶので、描く空間（直線）へ直す</summary>
Vector4 SrgbToLinear(const Vector4 &c)
{
    return {std::pow((std::max)(c.x, 0.0f), 2.2f), std::pow((std::max)(c.y, 0.0f), 2.2f),
            std::pow((std::max)(c.z, 0.0f), 2.2f), c.w};
}

} // namespace

void TransitionRenderer::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;

    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    D3D12_CLEAR_VALUE clearValue = pDxCommon_->GetClearColorValue();
    clearValue.Format = kFormat;
    outputResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kFormat, clearValue, /*allowUAV=*/true);
    snapshotResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kFormat, clearValue);

    // 出力の UAV（+1規約: 返った番号の次へ書く）
    outputUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(outputUavIndex_, outputResource_.Get(), kFormat);

    // SRV テーブルはヒープ上で連続している必要があるので、まとめて押さえる
    const uint32_t total = kTableSize * kTableCount;
    uint32_t first = 0;
    tableReady_ = true;
    for (uint32_t i = 0; i < total; ++i)
    {
        const uint32_t index = pSrvManager_->Allocate();
        if (i == 0)
        {
            first = index;
        }
        else if (index != first + i)
        {
            tableReady_ = false; // 連続で取れなかった。遷移の幕は描かない（画面はそのまま出る）
        }
    }
    tableBaseIndex_ = first + 1;

    // 定数バッファも SRV テーブルと同じ数だけ持って順に使う（前のフレームが読んでいる最中に書き換えないため）
    constantBuffer_ = pDxCommon_->CreateBufferResource(kConstantBufferStride * kTableCount);
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pData_));

    TextureManager::GetInstance()->LoadTexture(kWhiteTexture);
}

void TransitionRenderer::Finalize()
{
    if (constantBuffer_ && pData_)
    {
        constantBuffer_->Unmap(0, nullptr);
    }
    pData_ = nullptr;
    constantBuffer_.Reset();
    outputResource_.Reset();
    snapshotResource_.Reset();
}

void TransitionRenderer::CaptureSnapshot(ID3D12Resource *pTarget)
{
    if (!pTarget || !snapshotResource_)
    {
        return;
    }
    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pDxCommon_->BarrierTransition(pTarget, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
    pDxCommon_->BarrierTransition(snapshotResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
    pCommandList->CopyResource(snapshotResource_.Get(), pTarget);
    pDxCommon_->BarrierTransition(snapshotResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    pDxCommon_->BarrierTransition(pTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_GENERIC_READ);
    hasSnapshot_ = true;
}

bool TransitionRenderer::BindTexture(uint32_t slot, const std::string &relativePath)
{
    TextureManager *pTextureManager = TextureManager::GetInstance();
    std::string path = relativePath.empty() ? kWhiteTexture : relativePath;
    pTextureManager->LoadTexture(path);
    ID3D12Resource *pResource = pTextureManager->GetTextureResource(AssetPath::Image(path));
    const bool found = (pResource != nullptr) && !relativePath.empty();
    if (!pResource)
    {
        path = kWhiteTexture;
        pResource = pTextureManager->GetTextureResource(AssetPath::Image(path));
    }
    if (!pResource)
    {
        return false;
    }
    const DirectX::TexMetadata &metaData = pTextureManager->GetMetaData(AssetPath::Image(path));
    pSrvManager_->CreateSRVforTexture2D(currentTable_ + slot, pResource, metaData, static_cast<UINT>(metaData.mipLevels));
    return found;
}

void TransitionRenderer::FillLayer(TransitionGpuLayer &out, const TransitionLayer &layer, float progress, bool flipInvert, int index)
{
    out.color = SrgbToLinear(layer.color);
    out.color2 = SrgbToLinear(layer.color2);
    Vector4 edge = SrgbToLinear(layer.edgeColor);
    edge.w = layer.edgeColor.w * layer.edgeIntensity;
    out.edgeColor = edge;
    for (int i = 0; i < 4; ++i)
    {
        out.palette[i] = SrgbToLinear(layer.palette[static_cast<size_t>(i)]);
    }
    out.center = layer.center;
    out.angle = ToRadians(layer.angle);
    out.count = layer.count;
    out.progress = progress;
    out.softness = layer.softness;
    out.edgeWidth = layer.edgeWidth;
    out.opacity = layer.enabled ? layer.opacity : 0.0f;
    out.shape = static_cast<int>(layer.shape);
    out.fill = static_cast<int>(layer.fill);
    out.invert = (layer.invert != flipInvert) ? 1 : 0;
    out.order = static_cast<int>(layer.order);
    out.cellSpread = layer.cellSpread;
    out.amplitude = layer.amplitude;
    out.seed = layer.seed;
    out.gradientAngle = ToRadians(layer.gradientAngle);
    out.imageScale = layer.imageScale;
    out.paletteCount = std::clamp(layer.paletteCount, 1, 4);
    // t2+i にルール画像、t6+i に塗りの画像
    out.hasRule = (layer.shape == TransitionShape::RuleImage && BindTexture(2 + static_cast<uint32_t>(index), layer.ruleImage)) ? 1 : 0;
    if (layer.shape != TransitionShape::RuleImage)
    {
        BindTexture(2 + static_cast<uint32_t>(index), std::string());
    }
    out.hasImage = (layer.fill == TransitionFill::Image && BindTexture(6 + static_cast<uint32_t>(index), layer.image)) ? 1 : 0;
    if (layer.fill != TransitionFill::Image)
    {
        BindTexture(6 + static_cast<uint32_t>(index), std::string());
    }
}

void TransitionRenderer::Render(ID3D12Resource *pTarget, const TransitionFrame &frame, float time)
{
    if (!pTarget || !tableReady_ || !pData_)
    {
        return;
    }
    const ComputeEffectProgram *program = ComputeEffectPipeline::GetInstance()->Get(
        kShaderFile, {ShaderRootSignature::SamplerPreset::LinearClamp, ShaderRootSignature::SamplerPreset::LinearWrap});
    if (!program)
    {
        return; // コンパイルに失敗したときは幕を描かない（画面はそのまま出る）
    }

    // このフレームで使うテーブルと定数バッファ
    currentTable_ = tableBaseIndex_ + tableCursor_ * kTableSize;
    const uint32_t bufferSlot = tableCursor_;
    tableCursor_ = (tableCursor_ + 1) % kTableCount;
    TransitionGpuData *pData =
        reinterpret_cast<TransitionGpuData *>(reinterpret_cast<char *>(pData_) + kConstantBufferStride * bufferSlot);

    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    TransitionGpuData data{};
    data.textureSize[0] = static_cast<int>(width);
    data.textureSize[1] = static_cast<int>(height);
    data.time = time;
    data.hasSnapshot = hasSnapshot_ ? 1 : 0;
    const TransitionSceneFx &fx = frame.sceneFx;
    data.mosaic = fx.mosaic;
    data.blur = fx.blur;
    data.swirl = ToRadians(fx.swirl);
    data.zoom = fx.zoom;
    data.zoomBlur = fx.zoomBlur;
    data.rotate = ToRadians(fx.rotate);
    data.chroma = fx.chroma;
    data.desaturate = fx.desaturate;
    data.brightness = fx.brightness;
    data.shake = fx.shake;
    data.wave = fx.wave;

    // t0 = 画面、t1 = 前の画面
    pSrvManager_->CreateSRVforRenderTexture(currentTable_ + 0, pTarget, DXGI_FORMAT_UNKNOWN);
    pSrvManager_->CreateSRVforRenderTexture(currentTable_ + 1, snapshotResource_.Get(), DXGI_FORMAT_UNKNOWN);

    int layerCount = 0;
    if (frame.layers)
    {
        for (const TransitionLayer &layer : *frame.layers)
        {
            if (layerCount >= TransitionPreset::kMaxLayers)
            {
                break;
            }
            FillLayer(data.layers[layerCount], layer, frame.layerProgress[layerCount], frame.flipInvert, layerCount);
            ++layerCount;
        }
    }
    // 使わない枠にも何か差しておく（テーブルに穴を空けない）
    for (int i = layerCount; i < TransitionPreset::kMaxLayers; ++i)
    {
        BindTexture(2 + static_cast<uint32_t>(i), std::string());
        BindTexture(6 + static_cast<uint32_t>(i), std::string());
    }
    data.layerCount = layerCount;
    *pData = data;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pSrvManager_->SetDescriptorHeap();
    pDxCommon_->BarrierTransition(outputResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    pCommandList->SetPipelineState(program->pipelineState.Get());
    pCommandList->SetComputeRootSignature(program->rootSignature.Get());
    if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
    {
        pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetSrvTableIndex(),
                                                    pSrvManager_->GetGPUDescriptorHandle(currentTable_));
    }
    if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
    {
        pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetUavTableIndex(),
                                                    pSrvManager_->GetGPUDescriptorHandle(outputUavIndex_));
    }
    const UINT cbvIndex = program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
    if (cbvIndex != UINT_MAX)
    {
        pCommandList->SetComputeRootConstantBufferView(cbvIndex, constantBuffer_->GetGPUVirtualAddress() +
                                                                     kConstantBufferStride * bufferSlot);
    }
    pCommandList->Dispatch((width + program->threadGroupSizeX - 1) / program->threadGroupSizeX,
                           (height + program->threadGroupSizeY - 1) / program->threadGroupSizeY, 1);

    // 結果を画面へ書き戻す
    pDxCommon_->BarrierTransition(outputResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    pDxCommon_->BarrierTransition(pTarget, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
    pCommandList->CopyResource(pTarget, outputResource_.Get());
    pDxCommon_->BarrierTransition(pTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    pDxCommon_->BarrierTransition(outputResource_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

} // namespace Hagine
