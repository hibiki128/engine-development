#include "PostEffectRenderer.h"
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <algorithm>

namespace Hagine {
void PostEffectRenderer::Initialize(DirectXCommon *pDxCommon,
                                    SrvManager *pSrvManager,
                                    PipelineManager *psoManager)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;
    pPsoManager_ = psoManager;

    renderBuffer_.Initialize(pDxCommon_, pSrvManager_);
    dsvHandle_ = pDxCommon_->GetDSVCPUDescriptorHandle(0);
    finalResultRtvHandle_ = renderBuffer_.GetFinalResultRtvHandle();
    InitializeComputeSrvTable();
}

void PostEffectRenderer::InitializeComputeSrvTable()
{
    // デスクリプタテーブルはヒープ上で連続している必要があるので、
    // テーブル×リング数ぶんの連続領域をまとめて押さえる。
    //
    // SrvManager は「Allocate() が返した番号 r に対して、実際に書き込むのは r+1」という
    // 規約で運用されている（素の r に書くと直前の確保分を上書きしてしまう）。
    // ここでもその規約に合わせ、確保した先頭 +1 をテーブルの先頭として使う。
    const uint32_t totalDescriptors = kComputeSrvTableSize * kComputeSrvTableCount;

    uint32_t firstAllocated = 0;
    for (uint32_t i = 0; i < totalDescriptors; ++i)
    {
        const uint32_t index = pSrvManager_->Allocate();
        if (i == 0)
        {
            firstAllocated = index;
        }
        else if (index != firstAllocated + i)
        {
            // 連続で取れなかった場合はCS版を諦める（PS版で動き続ける）
            assert(false && "CS用SRVテーブルの連続領域を確保できませんでした");
            return;
        }
    }

    computeSrvTableBaseIndex_ = firstAllocated + 1;
    computeSrvTableCursor_ = 0;
    computeSrvTableReady_ = true;
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectRenderer::BuildComputeSrvTable(const std::vector<ComputeInput> &inputs,
                                                                     bool isFirstInput,
                                                                     int inputPingPong,
                                                                     bool readFromScratch)
{
    // ここでデスクリプタを「コピー」してはいけない。
    // SrvManager のヒープはシェーダー可視（SHADER_VISIBLE）で、その手のヒープは
    // CPUからは書き込み専用なので CopyDescriptorsSimple のコピー元にできない
    // （D3D12 ERROR #654 COPY_DESCRIPTORS_WRITE_ONLY_DESCRIPTOR になる）。
    // 一方、シェーダー可視ヒープへ直接デスクリプタを「作る」のは許されているので、
    // テーブルの各スロットへその場で SRV を作り直す。
    //
    // 書き込む先はリングから1つ取る。使い回すと、同じフレーム内で後続のエフェクトが
    // 書き換えたデスクリプタを、先に積んだディスパッチまで読んでしまう。
    const uint32_t tableStart =
        computeSrvTableBaseIndex_ + computeSrvTableCursor_ * kComputeSrvTableSize;
    computeSrvTableCursor_ = (computeSrvTableCursor_ + 1) % kComputeSrvTableCount;

    for (size_t i = 0; i < inputs.size() && i < kComputeSrvTableSize; ++i)
    {
        const uint32_t slotIndex = tableStart + static_cast<uint32_t>(i);

        switch (inputs[i])
        {
        case ComputeInput::SourceColor:
        {
            // 2パス目以降は前パスが書いたスクラッチ、
            // 1パス目はチェーンの先頭ならシーンのオフスクリーン、以降は前段のピンポン
            ID3D12Resource *sourceResource = nullptr;
            if (readFromScratch)
            {
                sourceResource = renderBuffer_.GetComputeScratchResource().Get();
            }
            else if (isFirstInput)
            {
                sourceResource = pDxCommon_->GetOffScreenResource();
            }
            else
            {
                sourceResource = renderBuffer_.GetPingPongResource(inputPingPong).Get();
            }
            // フォーマットはリソース自身のものを使う（オフスクリーンはsRGB、チェーン内はFP16）
            pSrvManager_->CreateSRVforRenderTexture(slotIndex, sourceResource, DXGI_FORMAT_UNKNOWN);
            break;
        }
        case ComputeInput::EffectInput:
        {
            // パスが進んでも中間結果に差し替えず、常にエフェクトへの入力を指す
            ID3D12Resource *originalResource =
                isFirstInput ? pDxCommon_->GetOffScreenResource()
                             : renderBuffer_.GetPingPongResource(inputPingPong).Get();
            pSrvManager_->CreateSRVforRenderTexture(slotIndex, originalResource, DXGI_FORMAT_UNKNOWN);
            break;
        }
        case ComputeInput::SceneDepth:
            pSrvManager_->CreateSRVforDepth(slotIndex, pDxCommon_->GetDepthStencilResource());
            break;
        }
    }

    return pSrvManager_->GetGPUDescriptorHandle(tableStart);
}

void PostEffectRenderer::Draw(PostEffectChain &effectChain, float deltaTime)
{
    const std::vector<int> enabledIndices = effectChain.GetEnabledSlotIndices();

    if (enabledIndices.empty())
    {
        DrawToFinalResult();
        CopyFinalResultToBackBuffer();
        return;
    }

    // 時間パラメータの更新（時間依存エフェクト向け）
    for (int idx : enabledIndices)
    {
        IPostEffectParams *params = effectChain.GetParams(idx);
        if (params)
        {
            params->UpdateTime(deltaTime);
        }
    }

    ApplyEffectChain(effectChain, enabledIndices);
    CopyFinalResultToBackBuffer();
}

void PostEffectRenderer::ApplyEffectChain(PostEffectChain &effectChain, const std::vector<int> &enabledIndices)
{
    // ピンポンバッファで順番にエフェクトを適用する。
    // エフェクトの書き込み先は必ずピンポン（リニアFP16）で、最終結果テクスチャ（sRGB）へは
    // 最後にコピーパスで書き戻す。こうしておくと、
    //   ・エフェクトのPSOはフォーマット1種類（FP16）で済む
    //   ・チェーンの途中で8bitに丸められない
    //   ・最終結果はsRGBのままなのでUI合成やImGui表示に影響しない
    bool isFirstInput = true;
    int currentPingPong = 0;
    int outputPingPong = 0;

    for (size_t i = 0; i < enabledIndices.size(); ++i)
    {
        const int slotIdx = enabledIndices[i];
        const EffectSlot &slot = effectChain.GetSlots()[slotIdx];

        // CS版を持つエフェクトはそちらで処理し、持たない／生成に失敗した場合はPS版で描く
        if (!DispatchComputeEffect(slot, isFirstInput, currentPingPong, outputPingPong))
        {
            DrawSingleEffect(slot, isFirstInput, currentPingPong, outputPingPong);
        }

        currentPingPong = outputPingPong;
        outputPingPong = 1 - outputPingPong;
        isFirstInput = false;
    }

    // チェーンの出口: 最後に書いたピンポン(FP16リニア) → 最終結果(sRGB)
    ResolveChainToFinalResult(currentPingPong);
}

bool PostEffectRenderer::DispatchComputeEffect(const EffectSlot &slot,
                                               bool isFirstInput,
                                               int inputPingPong,
                                               int outputPingPong)
{
    if (!slot.params || !computeSrvTableReady_)
    {
        return false;
    }
    const std::string csFile = slot.params->GetComputeShaderFile();
    if (csFile.empty())
    {
        return false; // このエフェクトはCS版を持たない
    }

    // 入力に深度が含まれるなら、深度は補間してはいけないのでポイントサンプラーを割り当てる。
    // s0 = 画像用の線形クランプ、s1 = 深度用のポイントクランプ、という並びで統一している。
    const std::vector<ShaderRootSignature::SamplerPreset> samplers = {
        ShaderRootSignature::SamplerPreset::LinearClamp,
        ShaderRootSignature::SamplerPreset::PointClamp,
    };

    const ComputeEffectProgram *program = ComputeEffectPipeline::GetInstance()->Get(csFile, samplers);
    if (!program)
    {
        return false; // コンパイル失敗などのときはPS版へフォールバック
    }

    auto *pCommandList = pDxCommon_->GetCommandList().Get();
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    const std::vector<ComputeInput> inputs = slot.params->GetComputeInputs();
    const int passCount = (std::max)(1, slot.params->GetComputePassCount());

    // 中間パスはスクラッチへ、最終パスだけ呼び出し側が期待する outputPingPong へ書く。
    // （ピンポン2枚は入力用と出力用で埋まっているので、往復させると入出力が同じになってしまう）
    //
    // isFirstInput / inputPingPong は「このエフェクトへの入力がどこか」を表すので、
    // パスが進んでも書き換えない。パスが進んだかどうかは readFromScratch だけで表す
    // （ComputeInput::EffectInput が元画像を指し続けられるようにするため）。
    bool readFromScratch = false;

    const UINT groupX = (width + program->threadGroupSizeX - 1) / program->threadGroupSizeX;
    const UINT groupY = (height + program->threadGroupSizeY - 1) / program->threadGroupSizeY;

    for (int pass = 0; pass < passCount; ++pass)
    {
        const bool isLastPass = (pass == passCount - 1);
        ID3D12Resource *dstResource = isLastPass
                                          ? renderBuffer_.GetPingPongResource(outputPingPong).Get()
                                          : renderBuffer_.GetComputeScratchResource().Get();

        pDxCommon_->BarrierTransition(dstResource,
                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCommandList->SetPipelineState(program->pipelineState.Get());
        pCommandList->SetComputeRootSignature(program->rootSignature.Get());

        // 入力（t0..）
        if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(
                program->rootSignature.GetSrvTableIndex(),
                BuildComputeSrvTable(inputs, isFirstInput, inputPingPong, readFromScratch));
        }
        // 出力（u0）
        if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(
                program->rootSignature.GetUavTableIndex(),
                isLastPass ? renderBuffer_.GetPingPongUavHandleGPU(outputPingPong)
                           : renderBuffer_.GetComputeScratchUavHandleGPU());
        }

        // エフェクト固有の定数バッファ（b0）
        slot.params->ApplyCompute(pCommandList,
                                  program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0),
                                  pass, width, height);

        // スレッドグループ数はシェーダーの numthreads から求める（C++側に定数を持たない）
        pCommandList->Dispatch(groupX, groupY, 1);

        pDxCommon_->BarrierTransition(dstResource,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);

        // 次のパスは今書いたスクラッチを入力にする
        readFromScratch = true;
    }

    return true;
}

void PostEffectRenderer::ResolveChainToFinalResult(int srcPingPong)
{
    auto *pCommandList = pDxCommon_->GetCommandList().Get();

    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommandList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, nullptr);

    // 書き込み先が sRGB なので、チェーン内用(FP16)とは別のPSOを使う。
    // リニア→sRGB の変換はレンダーターゲット側のフォーマットが行う。
    pPsoManager_->DrawCommonSetting(PipelineType::PresentCopy, BlendMode::Normal, ShaderMode::None);
    pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), renderBuffer_.GetPingPongSrvHandleGPU(srcPingPong));
    pCommandList->DrawInstanced(3, 1, 0, 0);

    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostEffectRenderer::DrawWithoutCopy(PostEffectChain &effectChain, float deltaTime)
{
    const std::vector<int> enabledIndices = effectChain.GetEnabledSlotIndices();
    if (enabledIndices.empty())
    {
        DrawToFinalResult();
        return;
    }
    for (int idx : enabledIndices)
    {
        if (IPostEffectParams *p = effectChain.GetParams(idx))
        {
            p->UpdateTime(deltaTime);
        }
    }
    ApplyEffectChain(effectChain, enabledIndices);
    // CopyFinalResultToBackBuffer は呼ばない
}

void PostEffectRenderer::BeginCompositePass()
{
    auto *pCommandList = pDxCommon_->GetCommandList().Get();
    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                 D3D12_RESOURCE_STATE_GENERIC_READ,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommandList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, &dsvHandle_);
}

void PostEffectRenderer::EndCompositePass()
{
    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostEffectRenderer::BlitToOffScreen(D3D12_GPU_DESCRIPTOR_HANDLE srcSrv)
{
    auto *pCommandList = pDxCommon_->GetCommandList().Get();
    // PreRenderTexture() によりオフスクリーンは既に RENDER_TARGET 状態
    D3D12_CPU_DESCRIPTOR_HANDLE offScreenRtv = pDxCommon_->GetRTVCPUDescriptorHandle(2);
    pCommandList->OMSetRenderTargets(1, &offScreenRtv, false, &dsvHandle_);
    // オフスクリーンは sRGB なので、チェーン内用(FP16)ではなく PresentCopy を使う
    pPsoManager_->DrawCommonSetting(PipelineType::PresentCopy, BlendMode::Normal, ShaderMode::None);
    pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), srcSrv);
    pCommandList->DrawInstanced(3, 1, 0, 0);
    // オフスクリーンは RENDER_TARGET のまま（以降の3D描画のため）
}

void PostEffectRenderer::DrawToFinalResult()
{
    auto *pCommandList = pDxCommon_->GetCommandList().Get();

    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                 D3D12_RESOURCE_STATE_GENERIC_READ,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);

    pCommandList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, &dsvHandle_);

    D3D12_CLEAR_VALUE cv = pDxCommon_->GetClearColorValue();
    const float clearColor[4] = {cv.Color[0], cv.Color[1], cv.Color[2], cv.Color[3]};
    pCommandList->ClearRenderTargetView(finalResultRtvHandle_, clearColor, 0, nullptr);

    // 書き込み先が sRGB の最終結果テクスチャなので、チェーン内用(FP16)ではなく PresentCopy を使う
    pPsoManager_->DrawCommonSetting(PipelineType::PresentCopy, BlendMode::Normal, ShaderMode::None);
    pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), pDxCommon_->GetOffScreenGPUHandle());
    pCommandList->DrawInstanced(3, 1, 0, 0);

    pDxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostEffectRenderer::CopyFinalResultToBackBuffer()
{
    auto *pCommandList = pDxCommon_->GetCommandList().Get();

    UINT backBufferIndex = pDxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pDxCommon_->GetRTVCPUDescriptorHandle(backBufferIndex);

    // バックバッファは実ウィンドウサイズなので、仮想解像度の深度バッファは束縛しない
    // （このパスは深度不使用。サイズ不一致の検証エラーも防ぐ）
    pCommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    // レターボックス込みの最終合成用ビューポートで仮想解像度→実ウィンドウサイズへ拡縮する
    pCommandList->RSSetViewports(1, &pDxCommon_->GetPresentViewport());
    pCommandList->RSSetScissorRects(1, &pDxCommon_->GetPresentScissorRect());

    // チェーン内はリニアFP16、バックバッファは sRGB とフォーマットが違うため、
    // ここだけ専用のパイプライン（PresentCopy）を使う。sRGBへのエンコードはRTVが行う。
    pPsoManager_->DrawCommonSetting(PipelineType::PresentCopy, BlendMode::Normal, ShaderMode::None);
    pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), renderBuffer_.GetFinalResultSrvHandleGPU());
    pCommandList->DrawInstanced(3, 1, 0, 0);

    // 後続の描画（次フレームのオフスクリーンパス等）のためにレンダリング用ビューポートへ戻す
    pCommandList->RSSetViewports(1, &pDxCommon_->GetRenderViewport());
    pCommandList->RSSetScissorRects(1, &pDxCommon_->GetRenderScissorRect());
}

void PostEffectRenderer::DrawSingleEffect(const EffectSlot &slot,
                                          bool isFirstInput,
                                          int inputPingPong,
                                          int outputRtvIndex)
{
    assert(slot.occupied && slot.params);
    auto *pCommandList = pDxCommon_->GetCommandList().Get();

    D3D12_CLEAR_VALUE cv = pDxCommon_->GetClearColorValue();
    const float clearColor[4] = {cv.Color[0], cv.Color[1], cv.Color[2], cv.Color[3]};

    // --- 出力先の設定 ---
    // エフェクトの書き込み先は必ずピンポンバッファ（リニアFP16）。
    // 最終結果テクスチャ(sRGB)へはチェーンの最後に ResolveChainToFinalResult でまとめて書き戻す。
    assert(outputRtvIndex >= 0 && outputRtvIndex < renderBuffer_.GetPingPongBufferCount());
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = renderBuffer_.GetPingPongRtvHandle(outputRtvIndex);
    pDxCommon_->BarrierTransition(renderBuffer_.GetPingPongResource(outputRtvIndex).Get(),
                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle_);
    pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // --- パイプライン & シェーダーパラメータ設定 ---
    ShaderMode mode = slot.params->GetMode();
    pPsoManager_->DrawCommonSetting(PipelineType::Render, BlendMode::Normal, mode);
    slot.params->Apply(pCommandList, pSrvManager_, pDxCommon_);

    // --- 入力テクスチャの設定 ---
    if (isFirstInput)
    {
        pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), pDxCommon_->GetOffScreenGPUHandle());
    }
    else
    {
        if (inputPingPong >= 0 && inputPingPong < renderBuffer_.GetPingPongBufferCount())
        {
            pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), renderBuffer_.GetPingPongSrvHandleGPU(inputPingPong));
        }
        else
        {
            // フォールバック
            pCommandList->SetGraphicsRootDescriptorTable(pPsoManager_->GetCurrentRootSignature()->GetSrvIndex(0), pDxCommon_->GetOffScreenGPUHandle());
        }
    }

    // --- 描画 ---
    pCommandList->DrawInstanced(3, 1, 0, 0);

    // --- バリア遷移（読み取り可能状態へ戻す）---
    pDxCommon_->BarrierTransition(renderBuffer_.GetPingPongResource(outputRtvIndex).Get(),
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
}
} // namespace Hagine
