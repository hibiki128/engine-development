#include "PostEffectRenderer.h"

namespace Hagine {
void PostEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                     SrvManager *srvManager,
                                     PipeLineManager *psoManager) {
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;
    psoManager_ = psoManager;

    renderBuffer_.Initialize(dxCommon_, srvManager_);
    dsvHandle_            = dxCommon_->GetDSVCPUDescriptorHandle(0);
    finalResultRtvHandle_ = renderBuffer_.GetFinalResultRtvHandle();
}

void PostEffectRenderer::Draw(PostEffectChain &effectChain, float deltaTime) {
    const std::vector<int> enabledIndices = effectChain.GetEnabledSlotIndices();

    if (enabledIndices.empty()) {
        DrawToFinalResult();
        CopyFinalResultToBackBuffer();
        return;
    }

    // 時間パラメータの更新（時間依存エフェクト向け）
    for (int idx : enabledIndices) {
        IPostEffectParams *params = effectChain.GetParams(idx);
        if (params) { params->UpdateTime(deltaTime); }
    }

    // ピンポンバッファで順番にエフェクトを適用
    bool isFirstInput       = true;
    int currentPingPong     = 0;
    int outputPingPong      = 0;

    for (size_t i = 0; i < enabledIndices.size(); ++i) {
        const int slotIdx    = enabledIndices[i];
        const bool isLast    = (i == enabledIndices.size() - 1);
        const EffectSlot &slot = effectChain.GetSlots()[slotIdx];

        // 最後のエフェクトは最終結果テクスチャへ、それ以外はピンポンへ
        const int outputTarget = isLast ? -2 : outputPingPong;

        DrawSingleEffect(slot, isFirstInput, currentPingPong, outputTarget);

        if (!isLast) {
            currentPingPong = outputPingPong;
            outputPingPong  = 1 - outputPingPong;
            isFirstInput    = false;
        }
    }

    CopyFinalResultToBackBuffer();
}

void PostEffectRenderer::DrawWithoutCopy(PostEffectChain &effectChain, float deltaTime) {
    const std::vector<int> enabledIndices = effectChain.GetEnabledSlotIndices();
    if (enabledIndices.empty()) {
        DrawToFinalResult();
        return;
    }
    for (int idx : enabledIndices) {
        if (IPostEffectParams *p = effectChain.GetParams(idx)) { p->UpdateTime(deltaTime); }
    }
    bool isFirstInput = true;
    int currentPingPong = 0;
    int outputPingPong = 0;
    for (size_t i = 0; i < enabledIndices.size(); ++i) {
        const int slotIdx = enabledIndices[i];
        const bool isLast = (i == enabledIndices.size() - 1);
        const EffectSlot &slot = effectChain.GetSlots()[slotIdx];
        DrawSingleEffect(slot, isFirstInput, currentPingPong, isLast ? -2 : outputPingPong);
        if (!isLast) {
            currentPingPong = outputPingPong;
            outputPingPong = 1 - outputPingPong;
            isFirstInput = false;
        }
    }
    // CopyFinalResultToBackBuffer は呼ばない
}

void PostEffectRenderer::BeginCompositePass() {
    auto *cmdList = dxCommon_->GetCommandList().Get();
    dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, &dsvHandle_);
}

void PostEffectRenderer::EndCompositePass() {
    dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostEffectRenderer::BlitToOffScreen(D3D12_GPU_DESCRIPTOR_HANDLE srcSrv) {
    auto *cmdList = dxCommon_->GetCommandList().Get();
    // PreRenderTexture() によりオフスクリーンは既に RENDER_TARGET 状態
    D3D12_CPU_DESCRIPTOR_HANDLE offScreenRtv = dxCommon_->GetRTVCPUDescriptorHandle(2);
    cmdList->OMSetRenderTargets(1, &offScreenRtv, false, &dsvHandle_);
    psoManager_->DrawCommonSetting(PipelineType::kRender, BlendMode::kNormal, ShaderMode::kNone);
    cmdList->SetGraphicsRootDescriptorTable(0, srcSrv);
    cmdList->DrawInstanced(3, 1, 0, 0);
    // オフスクリーンは RENDER_TARGET のまま（以降の3D描画のため）
}

void PostEffectRenderer::DrawToFinalResult() {
    auto *cmdList = dxCommon_->GetCommandList().Get();

    dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmdList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, &dsvHandle_);

    D3D12_CLEAR_VALUE cv = dxCommon_->GetClearColorValue();
    const float clearColor[4] = {cv.Color[0], cv.Color[1], cv.Color[2], cv.Color[3]};
    cmdList->ClearRenderTargetView(finalResultRtvHandle_, clearColor, 0, nullptr);

    psoManager_->DrawCommonSetting(PipelineType::kRender, BlendMode::kNormal, ShaderMode::kNone);
    cmdList->SetGraphicsRootDescriptorTable(0, dxCommon_->GetOffScreenGPUHandle());
    cmdList->DrawInstanced(3, 1, 0, 0);

    dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostEffectRenderer::CopyFinalResultToBackBuffer() {
    auto *cmdList = dxCommon_->GetCommandList().Get();

    UINT backBufferIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetRTVCPUDescriptorHandle(backBufferIndex);

    cmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle_);
    psoManager_->DrawCommonSetting(PipelineType::kRender, BlendMode::kNormal, ShaderMode::kNone);
    cmdList->SetGraphicsRootDescriptorTable(0, renderBuffer_.GetFinalResultSrvHandleGPU());
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostEffectRenderer::DrawSingleEffect(const EffectSlot &slot,
                                           bool isFirstInput,
                                           int inputPingPong,
                                           int outputRtvIndex) {
    assert(slot.occupied && slot.params);
    auto *cmdList = dxCommon_->GetCommandList().Get();

    D3D12_CLEAR_VALUE cv = dxCommon_->GetClearColorValue();
    const float clearColor[4] = {cv.Color[0], cv.Color[1], cv.Color[2], cv.Color[3]};

    // --- 出力先の設定 ---
    if (outputRtvIndex == -2) {
        // 最終結果テクスチャ
        dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &finalResultRtvHandle_, false, &dsvHandle_);
        cmdList->ClearRenderTargetView(finalResultRtvHandle_, clearColor, 0, nullptr);
    } else {
        // ピンポンバッファ
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = renderBuffer_.GetPingPongRtvHandle(outputRtvIndex);
        dxCommon_->BarrierTransition(renderBuffer_.GetPingPongResource(outputRtvIndex).Get(),
                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle_);
        cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    }

    // --- パイプライン & シェーダーパラメータ設定 ---
    ShaderMode mode = slot.params->GetMode();
    psoManager_->DrawCommonSetting(PipelineType::kRender, BlendMode::kNormal, mode);
    slot.params->Apply(cmdList, srvManager_, dxCommon_);

    // --- 入力テクスチャの設定 ---
    if (isFirstInput) {
        cmdList->SetGraphicsRootDescriptorTable(0, dxCommon_->GetOffScreenGPUHandle());
    } else {
        if (inputPingPong >= 0 && inputPingPong < renderBuffer_.GetPingPongBufferCount()) {
            cmdList->SetGraphicsRootDescriptorTable(0, renderBuffer_.GetPingPongSrvHandleGPU(inputPingPong));
        } else {
            // フォールバック
            cmdList->SetGraphicsRootDescriptorTable(0, dxCommon_->GetOffScreenGPUHandle());
        }
    }

    // --- 描画 ---
    cmdList->DrawInstanced(3, 1, 0, 0);

    // --- バリア遷移（読み取り可能状態へ戻す）---
    if (outputRtvIndex == -2) {
        dxCommon_->BarrierTransition(renderBuffer_.GetFinalResultResource().Get(),
                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    } else {
        dxCommon_->BarrierTransition(renderBuffer_.GetPingPongResource(outputRtvIndex).Get(),
                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }
}
} // namespace Hagine
