#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// パーティクル（CPU版・GPU版）のルートシグネチャとPSO。
namespace Hagine {
void PipelineManager::CreateParticlePipelines()
{
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateParticleRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::Particle, ShaderMode::None)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::Screen); i++)
    {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateParticleGraphicsPipeline(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::Particle, blendMode, ShaderMode::None)] = pipeline;
    }
}

void PipelineManager::CreateGPUParticlePipelines()
{
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateGPUParticleRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::GPUParticle, ShaderMode::None)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::Screen); i++)
    {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateGPUParticleGraphicsPipeline(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::GPUParticle, blendMode, ShaderMode::None)] = pipeline;
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateGPUParticleRootSignature()
{
    // 頂点シェーダーの t0（描画リスト）とピクセルシェーダーの t0（テクスチャ）は
    // レジスタ番号が同じだが別リソースなので、ステージごとに分ける。
    // t2/t3/t4（スロット・可視数・回転）も頂点シェーダー側で別々の場所から差す。
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;
    options.separateResourcesByStage = true;

    // CPUパーティクルと同じく U だけ WRAP・V は CLAMP という軸ごとに違う設定。
    // プリセットに寄せると見た目が変わるので元の指定をそのまま持ってくる
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    options.explicitSamplers = {sampler};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/Particle/CSParticle/ParticleCS.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Particle/CSParticle/ParticleCS.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::GPUParticle, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateGPUParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    // BlendMode = Add
    switch (blendMode)
    {
    case BlendMode::None:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::Normal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::Add:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::Subtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::Multiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::Screen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    // RasterizerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *pVertexShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/ParticleCS.VS.hlsl", L"vs_6_0");
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/CSParticle/ParticleCS.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {pVertexShaderBlob->GetBufferPointer(),
                                    pVertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateParticleRootSignature()
{
    // 頂点シェーダーの t0（インスタンスデータ）とピクセルシェーダーの t0（テクスチャ）は
    // レジスタ番号が同じだが別リソース。ステージごとに別のルートパラメータへ分ける
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;
    options.separateResourcesByStage = true;

    // s0 は U だけ WRAP・V は CLAMP という軸ごとに違う設定なので、プリセットでは表せない。
    // 見た目が変わってしまうため元の指定をそのまま持ってくる
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    options.explicitSamplers = {sampler};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/Particle/Particle.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Particle/Particle.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::Particle, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    // BlendMode = Add
    switch (blendMode)
    {
    case BlendMode::None:
        // ブレンドを無効化する
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case BlendMode::Normal:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::Add:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::Subtract:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::Multiply:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        break;
    case BlendMode::Screen:
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        break;
    default:
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    // RasterizerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    // 裏面（時計回り）を表示しない
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする
    IDxcBlob *pVertexShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;        // InputLayout
    graphicsPipelineStateDesc.VS = {pVertexShaderBlob->GetBufferPointer(),
                                    pVertexShaderBlob->GetBufferSize()}; // vertexShader
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                                    pixelShaderBlob->GetBufferSize()}; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // DepthStencilの設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // 利用するトロポジ（形状）のタイプ、三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}

} // namespace Hagine
