#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// パイプライン／ルートシグネチャのキャッシュと共通設定。個々のPSO生成は PipelineManagerXxx.cpp に分けてある。
namespace Hagine {
void PipelineManager::Finalize()
{
    pipelines_.clear();
    rootSignatures_.clear();
    reflectedRootSignatures_.clear();
}

const ShaderRootSignature *PipelineManager::GetReflectedRootSignature(PipelineType type, ShaderMode shaderMode) const
{
    auto it = reflectedRootSignatures_.find(MakeRootSignatureKey(type, shaderMode));
    return (it != reflectedRootSignatures_.end()) ? &it->second : nullptr;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::BuildReflectedRootSignature(
    PipelineType type, ShaderMode shaderMode,
    const std::vector<ShaderStageFile> &shaders,
    const ShaderRootSignature::BuildOptions &options)
{
    const std::string key = MakeRootSignatureKey(type, shaderMode);

    // 全ステージをコンパイルしてリフレクションを集める。
    // ここで集めた「使われているレジスタの和集合」がそのままルートシグネチャになる
    std::vector<ID3D12ShaderReflection *> reflections;
    std::vector<IDxcBlob *> blobs;
    std::vector<ShaderRootSignature::StageReflection> stages;
    reflections.reserve(shaders.size());
    blobs.reserve(shaders.size());
    stages.reserve(shaders.size());

    for (const ShaderStageFile &shader : shaders)
    {
        ID3D12ShaderReflection *pReflection = nullptr;
        IDxcBlob *pBlob = pDxCommon_->CompileShaderWithReflection(shader.path, shader.profile, &pReflection);
        blobs.push_back(pBlob);
        reflections.push_back(pReflection);
        if (pReflection)
        {
            stages.push_back({pReflection, shader.visibility});
        }
    }

    ShaderRootSignature reflected;
    const bool ok = reflected.Build(pDxCommon_, stages, options, key);

    // リフレクションはルートシグネチャを組むためだけに使うので、ここで解放してよい
    for (ID3D12ShaderReflection *pReflection : reflections)
    {
        if (pReflection)
        {
            pReflection->Release();
        }
    }
    for (IDxcBlob *pBlob : blobs)
    {
        if (pBlob)
        {
            pBlob->Release();
        }
    }

    if (!ok)
    {
        Logger::Error("PipelineManager: リフレクションからのルートシグネチャ生成に失敗: " + key);
        return nullptr;
    }

    // どのレジスタが何番のルートパラメータになったかを残しておく（不整合の切り分け用）
    Logger::Info("RootSignature[" + key + "] " + reflected.DescribeLayout());

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = reflected.Get();
    reflectedRootSignatures_[key] = std::move(reflected);
    return rootSignature;
}

void PipelineManager::AliasReflectedRootSignature(PipelineType from, PipelineType to)
{
    auto it = reflectedRootSignatures_.find(MakeRootSignatureKey(from, ShaderMode::None));
    if (it == reflectedRootSignatures_.end())
    {
        return;
    }
    reflectedRootSignatures_[MakeRootSignatureKey(to, ShaderMode::None)] = it->second;
}

IDxcBlob *PipelineManager::CompileVertexShaderWithLayout(const std::wstring &path, ShaderInputLayout &outLayout)
{
    ID3D12ShaderReflection *pReflection = nullptr;
    IDxcBlob *pBlob = pDxCommon_->CompileShaderWithReflection(path, L"vs_6_0", &pReflection);
    if (pReflection)
    {
        outLayout.BuildFromReflection(pReflection, StringUtility::ConvertString(path));
        pReflection->Release();
    }
    return pBlob;
}

void PipelineManager::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;

    CreateAllPipelines();
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::GetPipeline(PipelineType type, BlendMode blendMode, ShaderMode shaderMode)
{
    // キーを生成して対応するパイプラインを取得
    std::string key = MakePipelineKey(type, blendMode, shaderMode);

    // 対応するパイプラインが存在するか確認
    if (pipelines_.find(key) == pipelines_.end())
    {
        // パイプラインが見つからない場合は警告を出して、デフォルトを返す
        assert(false && "指定されたパイプラインが存在しません");

        // デフォルトのパイプラインを返す (ここではStandard/Normal/Noneを想定)
        return pipelines_[MakePipelineKey(PipelineType::Standard, BlendMode::Normal, ShaderMode::None)];
    }

    return pipelines_[key];
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::GetRootSignature(PipelineType type, ShaderMode shaderMode)
{
    // キーを生成して対応するルートシグネチャを取得
    std::string key = MakeRootSignatureKey(type, shaderMode);

    // 対応するルートシグネチャが存在するか確認
    if (rootSignatures_.find(key) == rootSignatures_.end())
    {
        // ルートシグネチャが見つからない場合は警告を出して、デフォルトを返す
        assert(false && "指定されたルートシグネチャが存在しません");

        // デフォルトのルートシグネチャを返す
        return rootSignatures_[MakeRootSignatureKey(PipelineType::Standard, ShaderMode::None)];
    }

    return rootSignatures_[key];
}

void PipelineManager::DrawCommonSetting(PipelineType type, BlendMode blendMode, ShaderMode shaderMode)
{
    // 指定されたタイプのパイプラインとルートシグネチャを取得
    auto pipeline = GetPipeline(type, blendMode, shaderMode);
    auto rootSignature = GetRootSignature(type, shaderMode);

    // グラフィックスコマンドリストにパイプラインとルートシグネチャを設定
    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pCommandList->SetPipelineState(pipeline.Get());
    pCommandList->SetGraphicsRootSignature(rootSignature.Get());
    // 今バインドしたものを覚えておく。Material や LightGroup のように
    // 複数のパイプラインから共有される描画コードが、ルートパラメータ番号を引くのに使う
    pCurrentRootSignature_ = GetReflectedRootSignature(type, shaderMode);
    if (type == PipelineType::Line3d)
    {
        pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    }
    else
    {
        pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
}

// キー文字列を生成するヘルパー関数
std::string PipelineManager::MakePipelineKey(PipelineType type, BlendMode blendMode, ShaderMode shaderMode) const
{
    return std::format("Pipeline_{}_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(blendMode),
                       static_cast<int>(shaderMode));
}

std::string PipelineManager::MakeRootSignatureKey(PipelineType type, ShaderMode shaderMode) const
{
    return std::format("RootSignature_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(shaderMode));
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateFullScreenPostEffectPipeline(const std::wstring &psPath, Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, DXGI_FORMAT rtvFormat)
{
    IDxcBlob *vs = pDxCommon_->CompileShader(shaderPath + L"shaders/OffScreen/FullScreen.VS.hlsl", L"vs_6_0");
    IDxcBlob *ps = pDxCommon_->CompileShader(psPath.c_str(), L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = {nullptr, 0};
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.NumRenderTargets = 1;
    // 書き込み先の実フォーマットと一致させる必要がある。
    // ポストエフェクトのチェーン内はリニアFP16、バックバッファへの最終合成だけ sRGB。
    desc.RTVFormats[0] = rtvFormat;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    HRESULT hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));
    return pipelineState;
}

void PipelineManager::CreateAllPipelines()
{
    // 各種パイプラインの作成
    CreateStandardPipelines();
    CreateParticlePipelines();
    CreateSpritePipelines();
    CreateRenderPipelines();
    CreateSkinningPipelines();
    CreateLine3dPipelines();
    CreateSkyboxPipelines();
    CreateGPUParticlePipelines();
    CreateShadowMapPipelines();
    CreateDeferredPipelines();
}

} // namespace Hagine
