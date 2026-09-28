#include "RtShadowPass.h"
#include "RaytracingScene.h"
#include <DirectXCommon.h>
#include <WinApp.h>
#include <debug/log/Logger.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#endif

namespace Hagine {
namespace {

// 影マスクのフォーマット。0〜1 しか入らないので8bitで足りる
constexpr DXGI_FORMAT kShadowMaskFormat = DXGI_FORMAT_R8_UNORM;
// t0=TLAS, t1=深度, t2=法線 の3枚
constexpr uint32_t kSrvTableSize = 3;
// RayQuery を使うので cs_6_5 以上でないとコンパイルが通らない
constexpr const wchar_t *kRayQueryProfile = L"cs_6_5";
const std::string kShaderFile = "Raytracing/RtShadow.CS.hlsl";
} // namespace

void RtShadowPass::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;

    if (!pDxCommon_->IsRaytracingSupported())
    {
        Logger::Log("RtShadowPass: レイトレーシング非対応のため無効です\n");
        return;
    }

    constantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(Constants));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstants_));

    CreateResources(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    // 入力テーブルぶんの連続領域を押さえる。
    // SrvManager は「Allocate() が返した番号 r に対して実際に書くのは r+1」という規約
    {
        uint32_t firstAllocated = 0;
        bool contiguous = true;
        for (uint32_t i = 0; i < kSrvTableSize; ++i)
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
        if (contiguous)
        {
            inputTableIndex_ = firstAllocated + 1;
        }
        else
        {
            Logger::Error("RtShadowPass: ディスクリプタを連続で確保できませんでした。RT影は無効になります。");
        }
    }

    initialized_ = (maskResource_ != nullptr && inputTableIndex_ != UINT32_MAX);
    if (initialized_)
    {
        Logger::Log("RtShadowPass: 初期化しました\n");
    }
}

void RtShadowPass::Finalize()
{
    pConstants_ = nullptr;
    constantBuffer_.Reset();
    maskResource_.Reset();
    initialized_ = false;
    maskReady_ = false;
}

void RtShadowPass::CreateResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }
    width_ = width;
    height_ = height;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kShadowMaskFormat;
    clearValue.Color[0] = 1.0f; // 遮蔽なし＝1.0（日向）

    maskResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kShadowMaskFormat,
                                                            clearValue, /*allowUAV=*/true);
    if (!maskResource_)
    {
        Logger::Error("RtShadowPass: 影マスクのテクスチャを作成できませんでした。");
        return;
    }

    maskSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(maskSrvIndex_, maskResource_.Get(), kShadowMaskFormat);
    maskUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(maskUavIndex_, maskResource_.Get(), kShadowMaskFormat);
}

void RtShadowPass::EnsureResolution()
{
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    if (width == width_ && height == height_)
    {
        return;
    }
    CreateResources(width, height);
}

void RtShadowPass::Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                          const Matrix4x4 &inverseViewProjection, const Vector3 &lightDirection)
{
    maskReady_ = false;

    RaytracingScene *pScene = RaytracingScene::GetInstance();
    if (!initialized_ || !enabled_ || depthResource == nullptr)
    {
        return;
    }
    // 加速構造が無いとレイの飛ばしようがない。
    // TLAS はここで組む。このパスは G-Buffer の後＝スキニングとそのBLAS作り直しが
    // 済んだ後なので、キャラも今のポーズで入る
    if (!pScene->IsAvailable())
    {
        return;
    }
    // 使うと伝えておく。伝えていないフレームは加速構造を組まないので、
    // このパスをONにした直後の1フレームだけはシャドウマップのまま出る
    pScene->RequestUse();
    pScene->EnsureTlas();
    if (!pScene->HasValidTlas())
    {
        return;
    }

    EnsureResolution();
    if (!maskResource_)
    {
        return;
    }

    const ComputeEffectProgram *program =
        ComputeEffectPipeline::GetInstance()->Get(kShaderFile, {}, kRayQueryProfile);
    if (!program)
    {
        return;
    }

    // ── 入力テーブルを組み直す ──
    // シェーダー可視ヒープはコピー元にできないので、その場で作り直す
    ID3D12Device *pDevice = pDxCommon_->GetDevice().Get();
    {
        // t0: TLAS。加速構造のSRVは「リソースではなくアドレス」を指すので pResource は nullptr
        D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc{};
        tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        tlasDesc.RaytracingAccelerationStructure.Location = pScene->GetTlasGpuAddress();
        pDevice->CreateShaderResourceView(nullptr, &tlasDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_));

        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc{};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texDesc.Texture2D.MipLevels = 1;

        // t1: 深度。D24_UNORM_S8_UINT は読むとき R24_UNORM_X8_TYPELESS にする
        texDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        pDevice->CreateShaderResourceView(depthResource, &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_ + 1));

        // t2: 法線。ディファードが無効なときは無いので、深度で埋めておく
        //     （シェーダー側は法線の長さが0かどうかで判断する）
        texDesc.Format = normalResource ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        pDevice->CreateShaderResourceView(normalResource ? normalResource : depthResource, &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_ + 2));
    }

    // ── 定数 ──
    settings_.inverseViewProjection = inverseViewProjection;
    settings_.lightDirection = lightDirection;
    settings_.textureSize[0] = static_cast<int>(width_);
    settings_.textureSize[1] = static_cast<int>(height_);
    *pConstants_ = settings_;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    pDxCommon_->BarrierTransition(maskResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    pCommandList->SetPipelineState(program->pipelineState.Get());
    pCommandList->SetComputeRootSignature(program->rootSignature.Get());

    if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
    {
        pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetSrvTableIndex(),
                                                    pSrvManager_->GetGPUDescriptorHandle(inputTableIndex_));
    }
    if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
    {
        pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetUavTableIndex(),
                                                    pSrvManager_->GetGPUDescriptorHandle(maskUavIndex_));
    }
    const UINT cbvIndex = program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
    if (cbvIndex != UINT_MAX)
    {
        pCommandList->SetComputeRootConstantBufferView(cbvIndex, constantBuffer_->GetGPUVirtualAddress());
    }

    const UINT groupX = (width_ + program->threadGroupSizeX - 1) / program->threadGroupSizeX;
    const UINT groupY = (height_ + program->threadGroupSizeY - 1) / program->threadGroupSizeY;
    pCommandList->Dispatch(groupX, groupY, 1);

    pDxCommon_->BarrierTransition(maskResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);

    maskReady_ = true;
}

void RtShadowPass::DrawImGui()
{
#ifdef USE_IMGUI
    if (!pDxCommon_ || !pDxCommon_->IsRaytracingSupported())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("この環境ではレイトレーシングが使えません");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Checkbox("RTの影を使う##rtshadow", &enabled_);
    ImGui::SetItemTooltip("シャドウマップの代わりにレイトレーシングで影を求めます。\n"
                          "解像度によるギザギザや、接地の影の浮き／めり込みが無くなります");

    ImGui::DragFloat("法線バイアス##rtshadow", &settings_.normalBias, 0.001f, 0.0f, 1.0f, "%.4f");
    ImGui::SetItemTooltip("レイの始点を面から浮かせる量。\n"
                          "面が自分自身を遮ってシマシマになるときに上げます");
    ImGui::DragFloat("最大距離##rtshadow", &settings_.maxDistance, 1.0f, 1.0f, 5000.0f);
    ImGui::SetItemTooltip("この距離までに遮る物があれば影にします");
    ImGui::DragFloat("柔らかさ##rtshadow", &settings_.softness, 0.001f, 0.0f, 0.2f, "%.4f");
    ImGui::SetItemTooltip("太陽の見かけの大きさです。0で硬い影。\n"
                          "上げるとぼけますが、1画素1本しか飛ばしていないのでざらつきます");

    const RaytracingScene *pScene = RaytracingScene::GetInstance();
    ImGui::Text("形の種類(BLAS): %u  /  配置数(TLAS): %u  /  スキン再構築: %u",
                pScene->GetBlasCount(), pScene->GetInstanceCount(), pScene->GetSkinnedBlasCount());

    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("スキニングで動くメッシュも今のポーズで影を落とします"
                       "（そのぶんBLASを毎フレーム作り直すので、体数ぶんの負荷が乗ります）。"
                       "「スキン再構築」はこのフレームに作り直した体数です");
    ImGui::PopStyleColor();
#endif
}
} // namespace Hagine
