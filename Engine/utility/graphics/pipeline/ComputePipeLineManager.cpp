#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>

namespace Hagine {
void ComputePipelineManager::Finalize()
{
    pipelines_.clear();
    rootSignatures_.clear();
}

void ComputePipelineManager::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;

    CreateAllPipelines();
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::GetPipeline(ComputePipelineType type, BlendMode blendMode, ShaderMode shaderMode)
{
    std::string key = MakePipelineKey(type, blendMode, shaderMode);

    if (pipelines_.find(key) == pipelines_.end())
    {
        assert(false && "指定されたパイプラインが存在しません");
        return pipelines_[MakePipelineKey(ComputePipelineType::Skinning, BlendMode::Normal, ShaderMode::None)];
    }

    return pipelines_[key];
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::GetRootSignature(ComputePipelineType type, ShaderMode shaderMode)
{
    std::string key = MakeRootSignatureKey(type, shaderMode);

    if (rootSignatures_.find(key) == rootSignatures_.end())
    {
        assert(false && "指定されたルートシグネチャが存在しません");
        return rootSignatures_[MakeRootSignatureKey(ComputePipelineType::Skinning, ShaderMode::None)];
    }

    return rootSignatures_[key];
}

void ComputePipelineManager::DrawCommonSetting(ComputePipelineType type, BlendMode blendMode, ShaderMode shaderMode, ID3D12GraphicsCommandList *pCommandList)
{
    auto pipeline = GetPipeline(type, blendMode, shaderMode);
    auto rootSignature = GetRootSignature(type, shaderMode);

    // 引数が省略された場合はメインのコマンドリストを使う
    if (pCommandList == nullptr)
    {
        pCommandList = pDxCommon_->GetCommandList().Get();
    }
    pCommandList->SetPipelineState(pipeline.Get());
    pCommandList->SetComputeRootSignature(rootSignature.Get());
}

void ComputePipelineManager::CreateSkinningPipelines()
{
    auto rootSignature = CreateSkinningRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::Skinning, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateSkinningGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::Skinning, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

void ComputePipelineManager::CreateInitParticlePipelines()
{
    auto rootSignature = CreateInitParticleRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::InitParticle, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateInitParticleGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::InitParticle, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

void ComputePipelineManager::CreateEmitterPipelines()
{
    auto rootSignature = CreateEmitterRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::Emitter, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateEmitterGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::Emitter, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

void ComputePipelineManager::CreateUpdateEmitterPipelines()
{
    auto rootSignature = CreateUpdateEmitterRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::UpdateEmitter, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateUpdateEmitterGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::UpdateEmitter, BlendMode::Normal, ShaderMode::None)] = pipeline;

    // 演出なし専用の軽量 Update。シェーダは触るレジスタが部分集合なので
    // ルートシグネチャはフル版と同一オブジェクトを共有する（両キーへ登録）。
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::UpdateEmitterLite, ShaderMode::None)] = rootSignature;
    auto litePipeline = CreateUpdateEmitterLiteGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::UpdateEmitterLite, BlendMode::Normal, ShaderMode::None)] = litePipeline;
}

void ComputePipelineManager::CreateCountPipelines()
{
    auto rootSignature = CreateCountRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::Count, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateCountGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::Count, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

void ComputePipelineManager::CreateResetArgsPipelines()
{
    auto rootSignature = CreateResetArgsRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::ResetArgs, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateResetArgsGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::ResetArgs, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

void ComputePipelineManager::CreateAllPipelines()
{
    CreateSkinningPipelines();
    CreateInitParticlePipelines();
    CreateEmitterPipelines();
    CreateUpdateEmitterPipelines();
    CreateCountPipelines();
    CreateResetArgsPipelines();
    CreateLightCullingPipelines();
    CreateParticleLightGenPipelines();
}

// ディファードのライトカリングパイプライン
void ComputePipelineManager::CreateLightCullingPipelines()
{
    auto rootSignature = CreateLightCullingRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::LightCulling, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateLightCullingGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::LightCulling, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateLightCullingRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // t0: 深度テクスチャ（テクスチャなのでディスクリプタテーブル経由）
    D3D12_DESCRIPTOR_RANGE depthRange[1] = {};
    depthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange[0].NumDescriptors = 1;
    depthRange[0].BaseShaderRegister = 0;
    depthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // u0: タイルごとのライトインデックス出力
    D3D12_DESCRIPTOR_RANGE uavRange[1] = {};
    uavRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange[0].NumDescriptors = 1;
    uavRange[0].BaseShaderRegister = 0;
    uavRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    // b0: ディファード共通定数
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    // t0: 深度
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = depthRange;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(depthRange);
    // t1: ポイントライト配列（StructuredBuffer はルートSRVで直接渡せる）
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    // u0: 出力
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].DescriptorTable.pDescriptorRanges = uavRange;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(uavRange);
    // t2: ライト総数カウンタ（粒子光源のぶんまで含んだ総数はGPUしか知らない）
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 2;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateLightCullingGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *computeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Deferred/LightCulling.CS.hlsl", L"cs_6_0");
    assert(computeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = computeShaderBlob->GetBufferPointer(),
        .BytecodeLength = computeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// GPUパーティクルの粒子から動的ポイントライトを生成するパイプライン
void ComputePipelineManager::CreateParticleLightGenPipelines()
{
    auto rootSignature = CreateParticleLightGenRootSignature();
    rootSignatures_[MakeRootSignatureKey(ComputePipelineType::ParticleLightGen, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateParticleLightGenGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(ComputePipelineType::ParticleLightGen, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateParticleLightGenRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // 入出力はすべて StructuredBuffer なので、ディスクリプタテーブルを使わず
    // ルートSRV/ルートUAVで直接GPUアドレスを渡す（ディスクリプタ枠を消費しない）。
    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    // b0: 生成パラメータ
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    // t0: 描画コンパクション済みの生存粒子
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    // t1: 生存数カウンタ
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    // u0: ライト配列（追記先）
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].Descriptor.ShaderRegister = 0;
    // u1: ライト総数カウンタ（InterlockedAdd で取り合う）
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateParticleLightGenGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *computeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Deferred/ParticleLightGen.CS.hlsl", L"cs_6_0");
    assert(computeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = computeShaderBlob->GetBufferPointer(),
        .BytecodeLength = computeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

std::string ComputePipelineManager::MakePipelineKey(ComputePipelineType type, BlendMode blendMode, ShaderMode shaderMode)
{
    return std::format("Pipeline_{}_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(blendMode),
                       static_cast<int>(shaderMode));
}

std::string ComputePipelineManager::MakeRootSignatureKey(ComputePipelineType type, ShaderMode shaderMode)
{
    return std::format("RootSignature_{}_{}",
                       static_cast<int>(type),
                       static_cast<int>(shaderMode));
}

// =============================================
// Skinning
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateSkinningRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE srvRange0[1] = {};
    srvRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0[0].NumDescriptors = 1;
    srvRange0[0].BaseShaderRegister = 0;
    srvRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvRange1[1] = {};
    srvRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1[0].NumDescriptors = 1;
    srvRange1[0].BaseShaderRegister = 1;
    srvRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvRange2[1] = {};
    srvRange2[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange2[0].NumDescriptors = 1;
    srvRange2[0].BaseShaderRegister = 2;
    srvRange2[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange[1] = {};
    uavRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange[0].NumDescriptors = 1;
    uavRange[0].BaseShaderRegister = 0;
    uavRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(srvRange0);
    rootParameters[0].DescriptorTable.pDescriptorRanges = srvRange0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(srvRange1);
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvRange1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(srvRange2);
    rootParameters[2].DescriptorTable.pDescriptorRanges = srvRange2;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(uavRange);
    rootParameters[3].DescriptorTable.pDescriptorRanges = uavRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].Descriptor.ShaderRegister = 0;
    rootParameters[4].Descriptor.RegisterSpace = 0;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateSkinningGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Skinning/Skinning.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// =============================================
// InitParticle
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateInitParticleRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE uavRange0[1] = {};
    uavRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange0[0].NumDescriptors = 1;
    uavRange0[0].BaseShaderRegister = 0;
    uavRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange1[1] = {};
    uavRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange1[0].NumDescriptors = 1;
    uavRange1[0].BaseShaderRegister = 1;
    uavRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange2[1] = {};
    uavRange2[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange2[0].NumDescriptors = 1;
    uavRange2[0].BaseShaderRegister = 2;
    uavRange2[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange3[1] = {};
    uavRange3[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange3[0].NumDescriptors = 1;
    uavRange3[0].BaseShaderRegister = 3;
    uavRange3[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.pDescriptorRanges = uavRange0;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(uavRange0);
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.pDescriptorRanges = uavRange1;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(uavRange1);
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.pDescriptorRanges = uavRange2;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(uavRange2);
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.pDescriptorRanges = uavRange3;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(uavRange3);
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].Descriptor.ShaderRegister = 0;
    rootParameters[4].Descriptor.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateInitParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/InitParticle.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// =============================================
// Emitter
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateEmitterRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // SoA UAV (u0-u5: Life/DrawCore/SimCore/Trail/Rotation/Override)
    //   + フリーリスト UAV (u6-u8)
    //   + 生存リスト間接ディスパッチ UAV (u9:AliveList out / u10:AliveCounter out / u11:RenderCompact)
    //   + GPU駆動カリング UAV (u12:VisibleCounter / u13:RenderSlot) = 計14本。
    D3D12_DESCRIPTOR_RANGE uavRanges[14] = {};
    for (UINT i = 0; i < 14; ++i)
    {
        uavRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRanges[i].NumDescriptors = 1;
        uavRanges[i].BaseShaderRegister = i; // u0..u13
        uavRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    // SRV (t0:TriangleInfo / t1:TriangleCDF / t2:EdgeInfo / t3:ParticleField)
    D3D12_DESCRIPTOR_RANGE srvRanges[4] = {};
    for (UINT i = 0; i < 4; ++i)
    {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = i; // t0..t3
        srvRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    // スロット対応（既存 param 番号は不変。新規は末尾へ追加）:
    //   [0..8]   u0..u8 (SoA6本 + フリーリスト3本)
    //   [9..12]  b0..b3 (EmitterMesh / PerFrame / Settings / FieldCB)
    //   [13..16] t0..t3 (TriangleInfo / TriangleCDF / EdgeInfo / ParticleField)
    //   [17..19] u9..u11 (AliveList out / AliveCounter out / RenderCompact) ★生存リスト間接ディスパッチ
    //   [20..21] u12..u13 (VisibleCounter / RenderSlot) ★GPU駆動の視錐台カリング
    D3D12_ROOT_PARAMETER rootParameters[22] = {};
    for (UINT i = 0; i < 9; ++i)
    {
        rootParameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[i].DescriptorTable.pDescriptorRanges = &uavRanges[i];
        rootParameters[i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (UINT i = 0; i < 4; ++i)
    {
        rootParameters[9 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[9 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[9 + i].Descriptor.ShaderRegister = i; // b0..b3
        rootParameters[9 + i].Descriptor.RegisterSpace = 0;
    }
    for (UINT i = 0; i < 4; ++i)
    {
        rootParameters[13 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[13 + i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        rootParameters[13 + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[13 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (UINT i = 0; i < 5; ++i)
    {
        rootParameters[17 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[17 + i].DescriptorTable.pDescriptorRanges = &uavRanges[9 + i]; // u9..u13
        rootParameters[17 + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[17 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateEmitterGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/EmitParticle.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// =============================================
// =============================================
// UpdateEmitter（SoA）
//
// スロット対応表:
//   [0]  u0  : gLife          (UAV) ★SoA
//   [1]  u1  : gDrawCore      (UAV) ★SoA
//   [2]  u2  : gSimCore       (UAV) ★SoA
//   [3]  u3  : gTrail         (UAV) ★SoA
//   [4]  u4  : gRotation      (UAV) ★SoA
//   [5]  u5  : gOverride      (UAV) ★SoA
//   [6]  u6  : gFreeListIndex     (UAV)
//   [7]  u7  : gFreeList          (UAV)
//   [8]  u8  : gFreeListTailIndex (UAV)
//   [9]  u9  : gAliveList         (UAV) ★生存コンパクション
//   [10] u10 : gAliveCounter      (UAV) ★生存コンパクション
//   [11] u11 : gRenderCompact     (UAV) ★描画コンパクション(詰めた描画データ)
//   [12] b0  : gPerFrame      (CBV)
//   [13] b1  : gSettings      (CBV)
//   [14] b2  : gFieldCB       (CBV)
//   [15] t0  : gFields        (SRV)
//   [16] t1  : gFieldsOverride(SRV)
//   [17] t2  : gAliveListIn    (SRV) ★生存リスト間接ディスパッチの入力(前フレーム out リスト)
//   [18] t3  : gAliveCounterIn (SRV) ★生存リスト間接ディスパッチの入力(リスト長)
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateUpdateEmitterRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    // SoA UAV (u0-u5: Life/DrawCore/SimCore/Trail/Rotation/Override)
    //   + フリーリスト UAV (u6-u8) + 生存コンパクション UAV (u9-u10)
    //   + 描画コンパクション UAV (u11: gRenderCompact)
    //   + GPU駆動カリング UAV (u12: VisibleCounter / u13: RenderSlot) = 計14本。
    D3D12_DESCRIPTOR_RANGE uavRanges[14] = {};
    for (UINT i = 0; i < 14; ++i)
    {
        uavRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRanges[i].NumDescriptors = 1;
        uavRanges[i].BaseShaderRegister = i; // u0..u13
        uavRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    // SRV (t0:gFields / t1:gFieldsOverride / t2:gAliveListIn / t3:gAliveCounterIn)
    //   t2/t3 = 生存リスト間接ディスパッチの in（前フレームの out リスト/カウンタ）。
    D3D12_DESCRIPTOR_RANGE srvRanges[4] = {};
    for (UINT i = 0; i < 4; ++i)
    {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = i; // t0..t3
        srvRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    // スロット対応:
    //   [0..11]  u0..u11 (SoA6本 + フリーリスト3本 + 生存コンパクション2本(out) + 描画コンパクション1本)
    //   [12..14] b0..b2  (PerFrame / Settings / FieldCB)
    //   [15..16] t0..t1  (Fields / FieldsOverride)
    //   [17..18] t2..t3  (AliveListIn / AliveCounterIn) ★生存リスト間接ディスパッチの入力
    //   [19..20] u12..u13 (VisibleCounter / RenderSlot) ★GPU駆動の視錐台カリング
    D3D12_ROOT_PARAMETER rootParameters[21] = {};
    for (UINT i = 0; i < 12; ++i)
    {
        rootParameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[i].DescriptorTable.pDescriptorRanges = &uavRanges[i];
        rootParameters[i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (UINT i = 0; i < 3; ++i)
    {
        rootParameters[12 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[12 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[12 + i].Descriptor.ShaderRegister = i; // b0..b2
        rootParameters[12 + i].Descriptor.RegisterSpace = 0;
    }
    for (UINT i = 0; i < 4; ++i)
    {
        rootParameters[15 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[15 + i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        rootParameters[15 + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[15 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (UINT i = 0; i < 2; ++i)
    {
        rootParameters[19 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[19 + i].DescriptorTable.pDescriptorRanges = &uavRanges[12 + i]; // u12..u13
        rootParameters[19 + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[19 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateUpdateEmitterGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/UpdateParticle.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// 演出なし専用の軽量 Update PSO（root sig はフル版 UpdateEmitter と共有）。
Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateUpdateEmitterLiteGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/UpdateParticleLite.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// =============================================
// ResetArgs
//   生存コンパクションカウンタ(u0)を毎フレーム 0 にリセットする 1スレッドパス
//   スロット対応表:
//     [0] u0 : gAliveCounter (UAV)
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateResetArgsRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE uavRange0[1] = {};
    uavRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange0[0].NumDescriptors = 1;
    uavRange0[0].BaseShaderRegister = 0;
    uavRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.pDescriptorRanges = uavRange0;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(uavRange0);
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateResetArgsGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/ResetArgs.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

// =============================================
// Count
// =============================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> ComputePipelineManager::CreateCountRootSignature()
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE uavRange0[1] = {};
    uavRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange0[0].NumDescriptors = 1;
    uavRange0[0].BaseShaderRegister = 0;
    uavRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange1[1] = {};
    uavRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange1[0].NumDescriptors = 1;
    uavRange1[0].BaseShaderRegister = 1;
    uavRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.pDescriptorRanges = uavRange0;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(uavRange0);
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.pDescriptorRanges = uavRange1;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(uavRange1);
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature = {};
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumStaticSamplers = 0;
    descriptionRootSignature.pStaticSamplers = nullptr;
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob *signatureBlob = nullptr;
    ID3DBlob *pErrorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        Logger::Log(reinterpret_cast<char *>(pErrorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = pDxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                     signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    return rootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineManager::CreateCountGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    IDxcBlob *pComputeShaderBlob = nullptr;
    pComputeShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/CountParticle.CS.hlsl", L"cs_6_0");
    assert(pComputeShaderBlob != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
    computePipelineStateDesc.CS = {
        .pShaderBytecode = pComputeShaderBlob->GetBufferPointer(),
        .BytecodeLength = pComputeShaderBlob->GetBufferSize(),
    };
    computePipelineStateDesc.pRootSignature = rootSignature.Get();
    HRESULT hr = pDxCommon_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

} // namespace Hagine
