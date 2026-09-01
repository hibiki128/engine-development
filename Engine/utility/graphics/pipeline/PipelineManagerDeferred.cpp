#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// ディファードレンダリング（G-Buffer書き込み・全画面ライティング）のPSO。
namespace Hagine {
// ディファード（G-Buffer 書き込み＋全画面ライティング）パイプラインの作成
void PipelineManager::CreateDeferredPipelines()
{
    // G-Buffer 書き込みは Standard / Skinning とルートシグネチャを共有する。
    // これにより Object3d 側のバインド処理（マテリアル・行列・法線マップ等）を
    // 前方描画とまったく同じまま使い回せる。
    auto standardRootSignature = GetRootSignature(PipelineType::Standard, ShaderMode::None);
    rootSignatures_[MakeRootSignatureKey(PipelineType::GBuffer, ShaderMode::None)] = standardRootSignature;
    AliasReflectedRootSignature(PipelineType::Standard, PipelineType::GBuffer);
    pipelines_[MakePipelineKey(PipelineType::GBuffer, BlendMode::Normal, ShaderMode::None)] =
        CreateGBufferGraphicsPipeline(standardRootSignature, false);

    // インスタンシング版の G-Buffer 書き込み（頂点シェーダーだけ差し替え）
    rootSignatures_[MakeRootSignatureKey(PipelineType::GBufferInstanced, ShaderMode::None)] = standardRootSignature;
    AliasReflectedRootSignature(PipelineType::Standard, PipelineType::GBufferInstanced);
    pipelines_[MakePipelineKey(PipelineType::GBufferInstanced, BlendMode::Normal, ShaderMode::None)] =
        CreateGBufferGraphicsPipeline(standardRootSignature, false, true);

    auto skinningRootSignature = GetRootSignature(PipelineType::Skinning, ShaderMode::None);
    rootSignatures_[MakeRootSignatureKey(PipelineType::GBufferSkinning, ShaderMode::None)] = skinningRootSignature;
    AliasReflectedRootSignature(PipelineType::Skinning, PipelineType::GBufferSkinning);
    pipelines_[MakePipelineKey(PipelineType::GBufferSkinning, BlendMode::Normal, ShaderMode::None)] =
        CreateGBufferGraphicsPipeline(skinningRootSignature, true);

    // ライティングパス
    auto lightingRootSignature = CreateDeferredLightingRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::DeferredLighting, ShaderMode::None)] = lightingRootSignature;
    pipelines_[MakePipelineKey(PipelineType::DeferredLighting, BlendMode::Normal, ShaderMode::None)] =
        CreateDeferredLightingGraphicsPipeline(lightingRootSignature);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateGBufferGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, bool skinned, bool instanced)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    // InputLayout（Standard / Skinning と同じ頂点形式）
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

    // G-Buffer は不透明専用なのでブレンドしない（3枚とも全成分を書く）
    D3D12_BLEND_DESC blendDesc{};
    for (int i = 0; i < 3; ++i)
    {
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        blendDesc.RenderTarget[i].BlendEnable = FALSE;
    }
    blendDesc.IndependentBlendEnable = FALSE;

    // 前方描画（Standard / Skinning）と同じカリング設定にする
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    const std::wstring vsPath = skinned    ? (shaderPath + L"shaders/Skinning/Skinning.VS.hlsl")
                                : instanced ? (shaderPath + L"shaders/Object/Object3dInstanced.VS.hlsl")
                                            : (shaderPath + L"shaders/Object/Object3d.VS.hlsl");
    IDxcBlob *pVertexShaderBlob = pDxCommon_->CompileShader(vsPath, L"vs_6_0");
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Deferred/GBuffer.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // 深度は通常どおり書き込む（このあとライティングパスが深度を読む）
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = inputLayoutDesc;
    desc.VS = {pVertexShaderBlob->GetBufferPointer(), pVertexShaderBlob->GetBufferSize()};
    desc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
    desc.BlendState = blendDesc;
    desc.RasterizerState = rasterizerDesc;
    // G-Buffer 3枚（DeferredRenderer::kGBufferFormats と一致させること）
    desc.NumRenderTargets = 3;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;    // アルベド
    desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;     // 法線＋光沢度
    desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;         // マテリアル種別
    desc.DepthStencilState = depthStencilDesc;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateDeferredLightingRootSignature()
{
    // 全画面ライティングパス。リソースはすべてピクセルシェーダー側。
    // t0..t5 は G-Buffer・深度・シャドウ・環境マップで、それぞれ別の場所から差すのでテーブルを分ける。
    // t6/t7（ポイントライト・タイルインデックス）は GPU アドレスで直接差すのでルートSRV。
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;
    options.rootDescriptorSrvRegisters = {6, 7};

    // s0 は全画面用のクランプ、s1 はシャドウの比較サンプラー。
    // s1 のフィルタは MIP_POINT で、汎用プリセットとは別物なのでそのまま指定する
    D3D12_STATIC_SAMPLER_DESC linearClamp{};
    linearClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearClamp.AddressU = linearClamp.AddressV = linearClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    linearClamp.MaxLOD = D3D12_FLOAT32_MAX;
    linearClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC shadowCompare{};
    shadowCompare.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowCompare.AddressU = shadowCompare.AddressV = shadowCompare.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowCompare.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowCompare.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowCompare.MaxLOD = D3D12_FLOAT32_MAX;
    shadowCompare.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    options.explicitSamplers = {linearClamp, shadowCompare};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/OffScreen/FullScreen.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Deferred/DeferredLighting.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::DeferredLighting, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateDeferredLightingGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;

    // 全画面三角形は頂点バッファ不要（SV_VertexID から生成する）
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    IDxcBlob *pVertexShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/OffScreen/FullScreen.VS.hlsl", L"vs_6_0");
    assert(pVertexShaderBlob != nullptr);
    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Deferred/DeferredLighting.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // 深度は G-Buffer のものを SRV として読むため、ここでは深度バッファを使わない
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = {nullptr, 0};
    desc.VS = {pVertexShaderBlob->GetBufferPointer(), pVertexShaderBlob->GetBufferSize()};
    desc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
    desc.BlendState = blendDesc;
    desc.RasterizerState = rasterizerDesc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // オフスクリーンRTと同じ
    desc.DepthStencilState = depthStencilDesc;
    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

} // namespace Hagine
