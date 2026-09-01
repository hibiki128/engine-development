#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// 3Dライン・スカイボックス・シャドウマップのルートシグネチャとPSO。
namespace Hagine {
// 3Dラインパイプラインの作成
void PipelineManager::CreateLine3dPipelines()
{
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateLine3dRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::Line3d, ShaderMode::None)] = rootSignature;

    // パイプラインを作成し、マップに格納
    auto pipeline = CreateLine3dGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::Line3d, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateLine3dRootSignature()
{
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    // 線描画は頂点入力のみを使うので、不要なシェーダーステージのアクセスを明示的に落とす
    options.extraFlags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    // b1: ワールド行列＋乗算色をルート定数で渡す。静的バッチをローカル座標のまま
    //     使い回し、描画のたびに姿勢と色だけ差し替えるための枠。
    //     リフレクションからは「b1 に定数バッファがある」ことしか分からないので、
    //     ルート定数にしたいことはここで明示する。
    options.rootConstants = {{1, 20}}; // float4x4(16) + float4(4)

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/Line/Line3d.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Line/Line3d.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::Line3d, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateLine3dGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // InputLayout（LineVertex = float3 + RGBA8 の16バイト）
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "COLOR";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    // アルファは「書き込み先の値をそのまま保つ」。
    // ソースのアルファで上書きすると、半透明を描いた画素だけレンダーターゲットのアルファが
    // 1未満になり、エディタでシーンを ImGui::Image 表示したときに
    // ImGui がそのアルファで背景と合成してクリア色が透けて見えてしまう。
    // 色の合成は上の DestBlend 側で完結しているので、アルファを書き換える必要はない
    // （パーティクル系のパイプラインは元々この設定になっている）
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
    IDxcBlob *pVertexShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Line/Line3d.VS.hlsl", L"vs_6_0");
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Line/Line3d.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // デバッグ線は最後に重ねるだけなので深度は書かない（後続描画を遮らない・ROPも軽い）
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
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    // どのように画面に色を打ち込むかの設定（気にしなくていい）
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    // 実際に生成
    hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));
    return graphicsPipelineState;
}

void PipelineManager::CreateSkyboxPipelines()
{
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateSkyboxRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::Skybox, ShaderMode::None)] = rootSignature;

    // パイプラインを作成し、マップに格納
    auto pipeline = CreateSkyboxGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::Skybox, BlendMode::Normal, ShaderMode::None)] = pipeline;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateSkyboxRootSignature()
{
    // b0/b1 は頂点シェーダー、t0 (TextureCube) はピクセルシェーダー。
    // 空を貼るのでサンプラーは繰り返し（WRAP）にする
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;
    options.samplerPresets = {ShaderRootSignature::SamplerPreset::LinearWrap};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/SkyBox/Skybox.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/SkyBox/Skybox.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::Skybox, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateSkyboxGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // BlendStateの設定
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    // アルファは「書き込み先の値をそのまま保つ」。
    // ソースのアルファで上書きすると、半透明を描いた画素だけレンダーターゲットのアルファが
    // 1未満になり、エディタでシーンを ImGui::Image 表示したときに
    // ImGui がそのアルファで背景と合成してクリア色が透けて見えてしまう。
    // 色の合成は上の DestBlend 側で完結しているので、アルファを書き換える必要はない
    // （パーティクル系のパイプラインは元々この設定になっている）
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    // RasterizerStateの設定
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // Shaderをコンパイル。入力レイアウトは頂点シェーダーのリフレクションから組む
    ShaderInputLayout inputLayout;
    IDxcBlob *pVertexShaderBlob = CompileVertexShaderWithLayout(shaderPath + L"shaders/Skybox/Skybox.VS.hlsl", inputLayout);
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Skybox/Skybox.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // DepthStencilStateの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // スカイボックスは深度書き込みしない
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // パイプラインステート作成
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
    graphicsPipelineStateDesc.InputLayout = inputLayout.Get();
    graphicsPipelineStateDesc.VS = {pVertexShaderBlob->GetBufferPointer(), pVertexShaderBlob->GetBufferSize()};
    graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
    graphicsPipelineStateDesc.BlendState = blendDesc;
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // スカイボックスは三角形で描画
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // パイプラインステート作成
    hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
                                                             IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    return graphicsPipelineState;
}

// ========== シャドウマップパイプライン ==========

void PipelineManager::CreateShadowMapPipelines()
{
    auto rootSignature = CreateShadowMapRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::ShadowMap, ShaderMode::None)] = rootSignature;

    auto pipeline = CreateShadowMapGraphicsPipeline(rootSignature);
    pipelines_[MakePipelineKey(PipelineType::ShadowMap, BlendMode::Normal, ShaderMode::None)] = pipeline;

    // インスタンシング版（頂点シェーダーだけ差し替え・ルートシグネチャは共有）
    rootSignatures_[MakeRootSignatureKey(PipelineType::ShadowMapInstanced, ShaderMode::None)] = rootSignature;
    AliasReflectedRootSignature(PipelineType::ShadowMap, PipelineType::ShadowMapInstanced);
    pipelines_[MakePipelineKey(PipelineType::ShadowMapInstanced, BlendMode::Normal, ShaderMode::None)] =
        CreateShadowMapGraphicsPipeline(rootSignature, true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateShadowMapRootSignature()
{
    // 通常描画の b0（変換行列）とインスタンシング描画の t0（インスタンスデータ）を
    // 1つのルートシグネチャに同居させる。どちらの頂点シェーダーもこれで動く。
    // 両方のシェーダーを渡さないと、片方でしか使われていないレジスタが欠ける。
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    // インスタンスデータはGPUアドレスで直接差すのでルートSRVにする
    options.rootDescriptorSrvRegisters = {0};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/Shadow/ShadowMap.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Shadow/ShadowMapInstanced.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
    };
    return BuildReflectedRootSignature(PipelineType::ShadowMap, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateShadowMapGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, bool instanced)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

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

    const std::wstring shadowVsPath = instanced ? (shaderPath + L"shaders/Shadow/ShadowMapInstanced.VS.hlsl")
                                                : (shaderPath + L"shaders/Shadow/ShadowMap.VS.hlsl");
    IDxcBlob *vs = pDxCommon_->CompileShader(shadowVsPath, L"vs_6_0");
    assert(vs != nullptr);

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.DepthBias = 100;
    rasterizerDesc.DepthBiasClamp = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = 1.0f;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = inputLayoutDesc;
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0; // カラー書き込みなし
    desc.RasterizerState = rasterizerDesc;
    desc.DepthStencilState = depthStencilDesc;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.NumRenderTargets = 0; // RTVなし（深度のみ）
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    HRESULT hr = pDxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));
    return pipelineState;
}

} // namespace Hagine
