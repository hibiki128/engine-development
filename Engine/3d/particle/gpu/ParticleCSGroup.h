#pragma once
#include "particle/ParticleStruct.h"
#include <camera/projection/ViewProjection.h>
#include <graphics/srv/SrvManager.h>
#include <graphics/texture/TextureManager.h>
#include <model/Model.h>
#include <model/ModelStructs.h>
#include <particle/ParticleCommon.h>
#include <primitive/PrimitiveModel.h>
#include <d3d12.h>
#include <utility>
#include <wrl.h>

namespace Hagine {

/// <summary>
/// GPUパーティクル1グループ分のリソースとコンピュート処理を管理するクラス
/// パーティクルバッファ・生存リスト・各種定数バッファを保持し、
/// 更新コンピュートのディスパッチ（indirect対応）と描画用データの提供を行う
/// </summary>
class ParticleCSGroup
{
  public:
    // 軽量 Update バリアントのスレッドグループサイズ。
    // フル版(threadsPerGroup_=1024)と異なり、Ampere の1SM常駐1536スレッド上限で
    // 占有率を上げるため256にする。UpdateParticleLite.CS.hlsl の [numthreads] と一致必須。
    static constexpr uint32_t kLiteUpdateThreadsPerGroup = 256;

    // フル版 Update のスレッドグループサイズ。演出多用でレジスタが多くオキュパンシが
    // 低い(1024だと1SMに1ブロック=67%頭打ち)ため、Lite同様に縮小して常駐ブロック数を増やす。
    // フル版は Wave 単位集約(ブロックサイズ非依存)なので縮小しても正しさ不変。
    // UpdateParticle.CS.hlsl の [numthreads] と一致必須。他の全N系(Init/Count)は threadsPerGroup_ のまま。
    static constexpr uint32_t kFullUpdateThreadsPerGroup = 256;

    /// ===================================
    /// public method
    /// ===================================

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ParticleCSGroup();
    ParticleCSGroupData CreateParticleGroup(const std::string &groupName, const std::string &filename, uint32_t maxParticleCount = 10000, const std::string &texturePath = {}, BlendMode blendMode = BlendMode::Add);
    ParticleCSGroupData CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, uint32_t maxParticleCount = 10000, const std::string &texturePath = {}, BlendMode blendMode = BlendMode::Add);
    void Update(const ViewProjection &vp);
    void DrawImGui();
    int CalculateOptimalEmitCount() const;
    // fieldsActive: このグループがフィールドの影響を受けるか（receiveFields_ かつ有効フィールド>0）。
    //   演出設定が全 OFF かつ fieldsActive==false のとき軽量 Update PSO を使う。
    void UpdateParticleCSDisPatch(
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> fieldsSrvHandle,
        Microsoft::WRL::ComPtr<ID3D12Resource> fieldCountResource,
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> overrideSrvHandle,
        bool fieldsActive,
        ID3D12GraphicsCommandList *pCommandList = nullptr);

    // 軽量 Update バリアントを使えるグループか判定する。
    // 重い演出(curl/vortex/gather/turbulence/trail/rotation)が全 OFF かつ
    // フィールドの影響を受けない場合のみ true。
    bool CanUseLiteUpdate(bool fieldsActive) const;
    ParticleCSGroupData GetParticleGroupData() { return particleGroupData_; }

    /// ===================================
    /// Getter
    /// ===================================

    // SoA: Compute(Emit/Update) が u0-u5 にバインドする UAV の GPU ハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetLifeUavGpu() const { return soaLife_.uavHandle.second; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetDrawCoreUavGpu() const { return soaDrawCore_.uavHandle.second; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSimCoreUavGpu() const { return soaSimCore_.uavHandle.second; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetTrailUavGpu() const { return soaTrail_.uavHandle.second; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetRotationUavGpu() const { return soaRotation_.uavHandle.second; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOverrideUavGpu() const { return soaOverride_.uavHandle.second; }
    // 描画コンパクション: Update が u11 に書く UAV / 描画VS が t0 で読む SRV
    D3D12_GPU_DESCRIPTOR_HANDLE GetRenderCompactUavGpu() const { return soaRenderCompact_.uavHandle.second; }
    uint32_t GetRenderCompactSrvForVSIndex() const { return soaRenderCompact_.srvForVSIndex; }
    // 可視カウンタ (u12): 視錐台カリングを通った粒子の数＝ExecuteIndirect の instanceCount
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetVisibleCounterUavHandle() const { return visibleCounterUavHandle_; }
    uint32_t GetVisibleCounterSrvForVSIndex() const { return visibleCounterSrvForVSIndex_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetVisibleCounterGpuAddress() const
    {
        return visibleCounterResource_ ? visibleCounterResource_->GetGPUVirtualAddress() : 0;
    }
    // 描画リストの slot index (u13 / VS t2): 回転グループが gRotation を引くために使う
    D3D12_GPU_DESCRIPTOR_HANDLE GetRenderSlotUavGpu() const { return soaRenderSlot_.uavHandle.second; }
    uint32_t GetRenderSlotSrvForVSIndex() const { return soaRenderSlot_.srvForVSIndex; }
    // 粒子光源生成CSがルートSRVで直接読むためのGPUアドレス（詰めた描画データ／生存数）
    D3D12_GPU_VIRTUAL_ADDRESS GetRenderCompactGpuAddress() const
    {
        return soaRenderCompact_.resource ? soaRenderCompact_.resource->GetGPUVirtualAddress() : 0;
    }
    D3D12_GPU_VIRTUAL_ADDRESS GetAliveCounterGpuAddress() const
    {
        return aliveCounterResource_[alivePhase_] ? aliveCounterResource_[alivePhase_]->GetGPUVirtualAddress() : 0;
    }
    // SoA: 描画VS が回転グループのみ読む SRV インデックス（t4:Rotation）
    uint32_t GetRotationSrvForVSIndex() const { return soaRotation_.srvForVSIndex; }
    // 生存コンパクション用ハンドル/インデックス（out フェーズ＝今フレームの書込先を返す）
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetAliveListUavHandle() const { return aliveListUavHandle_[alivePhase_]; }
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetAliveCounterUavHandle() const { return aliveCounterUavHandle_[alivePhase_]; }
    // 描画(VS)が読む生存リスト/カウンタの SRV インデックス（out フェーズ）。
    uint32_t GetAliveListSrvForVSIndex() const { return aliveListSrvForVSIndex_[alivePhase_]; }
    uint32_t GetAliveCounterSrvForVSIndex() const { return aliveCounterSrvForVSIndex_[alivePhase_]; }
    // 直近フレームに読み戻した生存数(描画 instanceCount のヒント)
    uint32_t GetAliveDrawCount() const { return aliveDrawCount_; }
    // 生存リスト ping-pong のフェーズを反転する（毎フレーム先頭で呼び、out/in を入れ替える）。
    void AdvanceAliveFrame() { alivePhase_ ^= 1u; }
    // 生存コンパクションカウンタ／可視カウンタを 0 にリセットする 1スレッドパス
    void ResetAliveCounterDispatch(ID3D12GraphicsCommandList *pCommandList);
    // 視錐台カリングを一時的に無効化する（プレビュー窓は別カメラで描くのでカリングできない）。
    // Update(vp) が定数バッファへ書き込む enableFrustumCull に効く。
    void SetFrustumCullSuppressed(bool suppressed) { frustumCullSuppressed_ = suppressed; }
    // 視錐台カリングのグループ設定（ImGui / セーブロード用）
    bool IsFrustumCullEnabled() const { return frustumCullEnabled_; }
    void SetFrustumCullEnabled(bool enabled) { frustumCullEnabled_ = enabled; }
    // 生存数を readback バッファへコピーする（compute キュー上で記録すること）
    // 併せて GPU駆動描画(ExecuteIndirect)の引数バッファへ生存数を書き込む。
    void RecordAliveCountReadback(ID3D12GraphicsCommandList *computeCmdList);
    // ===== GPU駆動描画 (DrawInstanceIndirect) =====
    // 「今何個描くか」を CPU 読み戻しではなく VRAM 上のカウンタで決めるための引数バッファ。
    // 1メッシュにつき D3D12_DRAW_INDEXED_ARGUMENTS 1件を並べる。
    static constexpr uint32_t kDrawArgsStride = 5 * sizeof(uint32_t); // sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
    ID3D12Resource *GetDrawArgsResource() const { return drawArgsResource_.Get(); }
    // 引数バッファに一度でも値が書かれたか（未初期化のままの ExecuteIndirect を防ぐガード）
    bool IsDrawArgsReady() const { return drawArgsReady_; }
    uint32_t GetDrawArgsCount() const { return drawArgsCount_; }
    // readback 済みの生存数を CPU へ取り込む（aliveDrawCount_ を更新）
    void FetchAliveDrawCount();
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetFreeListIndexSrvHandle() const { return freeListIndexSrvHandle_; }
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetFreeListTrailIndexSrvHandle() const { return freeListTrailIndexSrvHandle_; }
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetFreeListSrvHandle() const { return freeListSrvHandle_; }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetPerFrameResource() const { return perFrameResource_; }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetMaterialResource() const { return materialResource_; }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetPerViewResource() const { return perViewResource_; }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetSettingsResource() const { return settingsResource_; }
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const { return indexBufferView_; }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const { return vertexBufferView_; }
    // ModelData は頂点配列を抱えるので値返しにしない（参照するたびにフルコピーが走る）
    const ModelData &GetModelData() const { return modelData_; }
    PerFrame *GetPerFrameData() const { return pPerFrameData_; }
    uint32_t GetMaxParticleCount() const { return pSettingsData_->maxParticleCount; }
    ParticleCSSettings *GetSettingsData() const { return pSettingsData_; }
    // カラーグラデーション(N段)のストップ列。ImGui エディタ/セーブロードが参照・編集する。
    // 編集後は MarkColorStopsDirty() を呼ぶと次フレームの Update(vp) で 256段 LUT を再ベイクする。
    std::vector<GradientStop> &GetColorStops() { return colorStops_; }
    void MarkColorStopsDirty() { colorStopsDirty_ = true; }
    // 寿命カーブ(サイズ/アルファ倍率)の制御点。ImCurveEdit エディタ/セーブロードが参照・編集する。
    // 編集後は MarkLifeCurvesDirty() で次フレームの Update(vp) が 256段 LUT を再ベイクする。
    std::vector<CurvePoint> &GetSizeCurvePoints() { return sizeCurvePoints_; }
    std::vector<CurvePoint> &GetAlphaCurvePoints() { return alphaCurvePoints_; }
    void MarkLifeCurvesDirty() { lifeCurvesDirty_ = true; }
    std::string GetGroupName() { return particleGroupData_.groupName; }
    PrimitiveType GetPrimitiveType() { return type_; }
    std::string GetModelPath() { return modelFilePath_; }
    uint32_t GetAliveParticleCount();
    PerView *GetPerView() { return pPerViewData_; }

    /// ===================================
    /// Setter
    /// ===================================

    void SetFrequency(float frequency) { frequency_ = frequency; }
    void SetSettingData(const ParticleCSSettings &settings) { *pSettingsData_ = settings; }
    void SetBlendMode(BlendMode blendMode) { particleGroupData_.blendMode = blendMode; }
    /// <summary>
    /// 全マテリアルのテクスチャを差し替える（ロード＋パス/インデックス更新）。
    /// エディタのD&Dや設定ロード時のテクスチャ適用に使う。空パスは無視。
    /// </summary>
    void SetTexture(const std::string &path);
    void SetPerView(PerView *perViewData) { pPerViewData_ = perViewData; }
    void SetBillboard(bool flag) { pPerViewData_->enableBillboard = flag; }

    // カウント処理を実行
    void CountAliveParticles();

    // プール再利用時に GPU 上のパーティクル状態とフリーリストを初期化し直す。
    // （新規生成時の InitParticle と同等。バッファ/SRV は再確保しない）
    void ResetForReuse() { InitParticle(); }

    // Trail/Rotation/Override を「使うグループだけ」本確保する（演出なしは 1要素ダミーのまま）。
    // Emit CS もこれらのバッファへ書き込むため、Emit のバインドより前に呼ぶこと。
    // （Update 内の確保だけだと、初回 Emit がダミーバッファへ書いて OOB で破棄され、
    //   その粒子のトレイル状態が未初期化のまま残る＝花火のトレイル欠落の原因）
    void EnsureUpdateOptionalBuffers(bool fieldsActive);

  private:
    /// ===================================
    /// private types
    /// ===================================
    // GPUパーティクル SoA バッファ1本分のリソースとディスクリプタ。
    // 各バッファは Compute(Emit/Update) 用 UAV を持つ。描画VSが読む Rotation/RenderCompact のみ
    // 追加で SRV(srvForVSIndex) を持つ。Trail/Rotation/Override は使うグループだけ本確保する。
    struct SoABuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> uavHandle{};
        uint32_t uavIndex = 0;
        uint32_t srvForVSIndex = 0;
        uint32_t stride = 0;         // 要素サイズ（再確保時に使う）
        bool withSrvForVS = false;   // 描画VS用SRVも作るか
        uint32_t allocatedCount = 0; // 現在の確保要素数（1=未使用ダミー / maxParticleCount=本確保）
    };

    /// ===================================
    /// private method
    /// ===================================
    void Initialize(uint32_t maxParticleCount = 10000);
    void InitParticle();
    void CreateParticleSoABuffers();
    // SoA バッファを count 要素で（再）確保しディスクリプタを作る。
    // 再確保時は in-flight 参照中の旧リソース／ディスクリプタを上書きせず、
    // 旧リソースは retiredSoABuffers_ へ退避し新しいディスクリプタ枠に作り直す（ハザード回避）。
    void AllocateSoABuffer(SoABuffer &buf, uint32_t count);
    void CreatePerViewResource();
    void CreateMaterialResource();
    void CreateIndexResource();
    void CreateVertexResource();
    void CreatePerFrameResource();
    void CreateFreeListIndexResource();
    void CreateFreeListTrailIndexResource();
    void CopyDebugDataToReadback();
    void CreateFreeListResource();
    void CreateSettingsResource();
    void CreateAliveCountResource();
    void CreateAliveListResources();
    // GPU駆動描画の引数バッファを作る（頂点/インデックスバッファ確定後に呼ぶこと）
    void CreateDrawArgsResources();
    // viewProjection から視錐台6平面(left/right/bottom/top/near/far)を抽出して正規化する
    static void ExtractFrustumPlanes(const Matrix4x4 &viewProjection, Vector4 outPlanes[6]);
    // colorStops_ を位置でソートし 256段 RGBA8 LUT を pSettingsData_->colorLUT へベイクする。
    // enableColorGradient 有効時、Update(vp) が colorStopsDirty_ のとき呼ぶ。
    void BakeColorLUT();
    // sizeCurvePoints_ / alphaCurvePoints_ を 256段 float LUT へベイクする（有効時 dirty で呼ぶ）。
    void BakeLifetimeCurveLUTs();

  private:
    /// ===================================
    /// private variables
    /// ===================================
    // ===== GPUパーティクル SoA バッファ（旧 outputParticleResource_ を機能別に分割） =====
    // 各バッファは Compute(Emit/Update) 用 UAV を持つ。描画VSが読む DrawCore/Rotation のみ
    // 追加で SRV(srvForVSIndex) を持つ。
    SoABuffer soaLife_;     // float            (生存判定。早期returnで4Bのみ読む)
    SoABuffer soaDrawCore_; // CSParticleDrawCore (translate/scale/velocity/color)
    SoABuffer soaSimCore_;  // CSParticleSimCore  (currentTime/initialScale/isTrailParticle)
    SoABuffer soaTrail_;    // CSParticleTrail    (parentIndex/lastTrailPosition/trailSpawnDistance)
    SoABuffer soaRotation_; // CSParticleRotation (rotation/angularVelocity)
    SoABuffer soaOverride_; // CSParticleOverride (settingsOverrideFlags uint2)
    // 描画コンパクション: Update が生存パーティクルの描画データ(DrawCore形式)を
    // 詰めた順(instanceId順)に書き出す。描画VSはこれを順次読みして散乱gatherを排除する。
    SoABuffer soaRenderCompact_; // CSParticleDrawCore (詰めた描画データ)
    // 描画リストの slot index。視錐台カリングで描画リストが生存リストと別順序になるため、
    // 回転グループが gRotation[slot] を引くのに使う（回転を使うグループだけ本確保）。
    SoABuffer soaRenderSlot_; // uint (描画順 -> 実 slot index)
    // 条件付き確保で本確保へ作り直したときの旧リソース退避先。
    // in-flight のコマンドリストが旧リソース/旧ディスクリプタを参照中でも安全なように、
    // 即解放せずグループ破棄まで生かす（要素1個のダミーなので極小）。
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredSoABuffers_;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
    uint32_t *indexData_{};
    // バッファリソースの使い道を補足するバッファビュー
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_{};
    PerView *pPerViewData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
    ParticleMaterial *pMaterialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    VertexData *pVertexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_ = nullptr;
    PerFrame *pPerFrameData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> freeListIndexSrvHandle_{};
    uint32_t freeListIndexSrvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListTrailIndexResource_{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> freeListTrailIndexSrvHandle_{};
    uint32_t freeListTrailIndexSrvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> freeListSrvHandle_{};
    uint32_t freeListSrvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> settingsResource_{};
    ParticleCSSettings *pSettingsData_ = nullptr;

    // 音声振動の実行時状態（保存しない）。onset(音量の立ち上がり)で跳ね、時間で指数減衰させる
    // エンベロープを毎フレーム計算し pSettingsData_->audioAmplitude へ注入する（GPU が振動の駆動に使う）。
    float audioEnvelope_ = 0.0f; // 現在のエンベロープ値[0,1]（ビートで跳ね、releaseRate で減衰）
    float audioPrevPeak_ = 0.0f; // 前フレームのピーク（onset=増加分の算出用）

    // カラーグラデーション(N段)のストップ列（CPU保持）。256段 RGBA8 LUT へベイクして CB に積む。
    std::vector<GradientStop> colorStops_;
    bool colorStopsDirty_ = true; // true のとき次の Update(vp) で LUT を再ベイク
    // 寿命カーブ(サイズ/アルファ倍率)の制御点（CPU保持）。256段 float LUT へベイクして CB に積む。
    std::vector<CurvePoint> sizeCurvePoints_;
    std::vector<CurvePoint> alphaCurvePoints_;
    bool lifeCurvesDirty_ = true;

    // ===== 生存コンパクション（生存リスト間接ディスパッチの ping-pong 基盤）=====
    // listBuf[2]/counterBuf[2] を毎フレーム ping-pong する。
    //   out = alivePhase_      : 今フレームの生存リスト書込先（Reset/Update/Readback/Draw が参照）
    //   in  = 1 - alivePhase_  : 前フレームの生存リスト（Step3 で Update の入力に使う・現状未使用）
    // 現状は Update が全スロット走査で out を毎フレーム作り直すため、どちらの物理バッファに
    // 書いても見た目は不変（ping-pong は将来 listIn 読みを入れるための土台）。
    static constexpr uint32_t kAlivePingPong = 2;
    // 生存パーティクルの slot index を詰めるバッファ (UAV: compute u9 / SRV: VS t2)
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveListResource_[kAlivePingPong]{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> aliveListUavHandle_[kAlivePingPong]{};
    uint32_t aliveListUavIndex_[kAlivePingPong] = {};
    uint32_t aliveListSrvForVSIndex_[kAlivePingPong] = {};
    // 生存数アトミックカウンタ (UAV: compute u10 / SRV: VS t3)
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveCounterResource_[kAlivePingPong]{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> aliveCounterUavHandle_[kAlivePingPong]{};
    uint32_t aliveCounterUavIndex_[kAlivePingPong] = {};
    uint32_t aliveCounterSrvForVSIndex_[kAlivePingPong] = {};
    // 読み戻しは out からコピーする共有 1個（1〜2F遅延は従来どおり許容）。
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveCounterReadbackResource_{};
    uint32_t alivePhase_ = 0; // out インデックス。AdvanceAliveFrame で毎フレーム反転
    uint32_t aliveDrawCount_ = 0;

    // ===== 描画リスト（視錐台カリング後）のカウンタ =====
    // 生存リストと違い ping-pong しない（毎フレーム作り直して当フレームの描画だけに使う）。
    // Emit/Update が u12 へ append し、描画VS が t3 で読み、ExecuteIndirect の
    // instanceCount へコピーされる。CPU は読み戻さない（＝GPU駆動）。
    Microsoft::WRL::ComPtr<ID3D12Resource> visibleCounterResource_{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> visibleCounterUavHandle_{};
    uint32_t visibleCounterUavIndex_ = 0;
    uint32_t visibleCounterSrvForVSIndex_ = 0;
    bool frustumCullSuppressed_ = false; // プレビュー窓など別カメラで描く間は true
    bool frustumCullEnabled_ = true;     // グループ設定(ImGui/Json)。false で視錐台カリングを切る
    // 粒子モデルのローカル境界半径（頂点の最大距離）。粒子半径 = max(scale)×これ。
    float modelLocalRadius_ = 1.0f;

    // ===== GPU駆動描画 (ExecuteIndirect) の引数バッファ =====
    // drawArgsResource_ は状態遷移を明示せず COMMON のままにする（バッファは COMMON から
    // 任意の状態へ暗黙昇格し、ExecuteCommandLists 完了で COMMON へ減衰する）。
    //   compute キュー: COPY_DEST へ昇格して InstanceCount を書き込み
    //   direct  キュー: INDIRECT_ARGUMENT へ昇格して ExecuteIndirect が読む
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsResource_{};
    // IndexCountPerInstance など CPU が決める固定部の種（初回に1回だけコピーする）
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsUploadResource_{};
    uint32_t drawArgsCount_ = 0;             // メッシュ数（引数の件数）
    bool drawArgsStaticUploaded_ = false;    // 固定部のコピーを記録済みか
    bool drawArgsReady_ = false;             // 一度でも引数を書いたか（未初期化描画の防止）

    Microsoft::WRL::ComPtr<ID3D12Resource> aliveCountResource_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveCountReadbackResource_{};
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> aliveCountSrvHandle_{};
    uint32_t aliveCountSrvIndex_ = 0;
    uint32_t cachedAliveCount_ = 0;

    ID3D12GraphicsCommandList *pCommandList_{};
    ID3D12GraphicsCommandList *computeCommandList_{};

    ParticleCommon *particleCommon_{};
    DirectXCommon *pDxCommon_{};
    SrvManager *pSrvManager_{};
    TextureManager *pTextureManager_{};
    Model *pModel_{};
    ModelData modelData_{};
    std::string modelFilePath_{};

    PrimitiveType type_ = PrimitiveType::None;
    ParticleCSGroupData particleGroupData_{};

    std::string texPath_ = "debug/circle2.png";

    float frequency_ = 0.1f;
    bool isRandomColor_ = false;
    bool isInitialized_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexReadbackBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListTrailIndexReadbackBuffer_;

    int32_t debugFreeListCount_ = 0;
    int32_t debugFreeListTailCount_ = 0;
};
} // namespace Hagine
