#include "PipelineManager.h"
#include "ComputePipelineManager.h"
#include <debug/log/Logger.h>
#include <d3dx12.h>

// スプライトのルートシグネチャとPSO。
namespace Hagine {
// スプライトパイプラインの作成
void PipelineManager::CreateSpritePipelines()
{
    // ルートシグネチャを作成し、マップに格納
    auto rootSignature = CreateSpriteRootSignature();
    rootSignatures_[MakeRootSignatureKey(PipelineType::Sprite, ShaderMode::None)] = rootSignature;

    // 各ブレンドモード用のパイプラインを作成し、マップに格納
    for (int i = 0; i <= static_cast<int>(BlendMode::Screen); i++)
    {
        BlendMode blendMode = static_cast<BlendMode>(i);
        auto pipeline = CreateSpriteGraphicsPipeline(rootSignature, blendMode);
        pipelines_[MakePipelineKey(PipelineType::Sprite, blendMode, ShaderMode::None)] = pipeline;
    }
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PipelineManager::CreateSpriteRootSignature()
{
    // 頂点シェーダーの t0（インスタンス行列）とピクセルシェーダーの t1（テクスチャ）は
    // ヒープ上の別々の場所から差すので、テーブルはレジスタごとに分ける
    ShaderRootSignature::BuildOptions options;
    options.allowInputAssembler = true;
    options.srvTableMode = ShaderRootSignature::TableMode::PerRegister;
    options.samplerPresets = {ShaderRootSignature::SamplerPreset::LinearClamp};

    const std::vector<ShaderStageFile> shaders = {
        {shaderPath + L"shaders/Sprite/Sprite.VS.hlsl", L"vs_6_0", D3D12_SHADER_VISIBILITY_VERTEX},
        {shaderPath + L"shaders/Sprite/Sprite.PS.hlsl", L"ps_6_0", D3D12_SHADER_VISIBILITY_PIXEL},
    };
    return BuildReflectedRootSignature(PipelineType::Sprite, ShaderMode::None, shaders, options);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateSpriteGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode)
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    HRESULT hr;

    // BlendStageの設定
    D3D12_BLEND_DESC blendDesc{};
    // すべての色要素を書き込む
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;

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
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    // 三角形の中を塗りつぶす
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    // Shaderをコンパイルする。入力レイアウトは頂点シェーダーのリフレクションから組む
    ShaderInputLayout inputLayout;
    IDxcBlob *pVertexShaderBlob = CompileVertexShaderWithLayout(shaderPath + L"shaders/Sprite/Sprite.VS.hlsl", inputLayout);
    assert(pVertexShaderBlob != nullptr);

    IDxcBlob *pixelShaderBlob = pDxCommon_->CompileShader(shaderPath + L"shaders/Sprite/Sprite.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    ///=========DepthStencilStateの設定==========
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    // Depthの機能を有効化する
    depthStencilDesc.DepthEnable = true;
    // 書き込みします
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // 比較関数はLessEqual。つまり、近ければ描画される
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ///==========================================

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayout.Get();      // InputLayout
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
