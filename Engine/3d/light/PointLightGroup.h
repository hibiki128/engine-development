#pragma once
#include "LightTypes.h"
#include "d3d12.h"
#include "wrl.h"
#include <string>
#include <vector>

namespace Hagine {
class DirectXCommon;
class DataHandler;
class LineRenderer;

/// <summary>
/// 点光源（ポイントライト）をまとめて管理するクラス。
///
/// 持っているものは3種類:
///   ・手で置いたライト（entries_）……エディタで編集・保存するもの
///   ・動的ライト（dynamicLights_）……粒子の発光など毎フレーム積み直すもの
///   ・GPU生成ライト……粒子1個1個を光源化したもの。CSがStructuredBufferへ直接追記する
///
/// 転送先も2系統ある:
///   ・定数バッファ（前方描画用）……MAX_POINT_LIGHTS 個までなので寄与の大きい順に間引く
///   ・StructuredBuffer（ディファード用）……kMaxBufferedPointLights まで全部入る
///
/// 一覧の選択やギズモ登録、名前の一意化は LightGroup の担当なので、
/// このクラスは「要求を返すだけ」で自分では実行しない。
/// </summary>
class PointLightGroup
{
  public:
    /// <summary>
    /// StructuredBufferに格納できるポイントライトの最大数
    /// </summary>
    static constexpr uint32_t kMaxBufferedLights = 8192;

    /// <summary>
    /// 手で置いたライト1個ぶんの編集データ。
    /// GPUへ流す実体（gpu）と、エディタ用の情報を分けて持つ。
    /// </summary>
    struct Entry
    {
        PointLightData gpu{}; // 定数バッファ／StructuredBuffer へ転送する実体
        std::string name;     // 一覧表示とギズモ登録に使う名前（ライト間で一意）
    };

    /// <summary>
    /// 定数バッファ・StructuredBuffer・カウンタを生成する
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    // ===================================================
    //  追加・削除
    // ===================================================

    /// <summary>
    /// ライトを追加する
    /// </summary>
    /// <param name="name">付ける名前（呼び出し側で一意にしておくこと）</param>
    /// <returns>int: 追加した要素の添字。上限に達していれば -1</returns>
    int Add(const std::string &name);

    /// <summary>
    /// ライトを削除する
    /// </summary>
    /// <param name="index">対象の添字</param>
    /// <returns>bool: 削除できたら true</returns>
    bool Remove(int index);

    /// <summary>
    /// ライトを複製する
    /// </summary>
    /// <param name="index">複製元の添字</param>
    /// <param name="name">複製先に付ける名前（呼び出し側で一意にしておくこと）</param>
    /// <returns>int: 複製した要素の添字。複製できなければ -1</returns>
    int Duplicate(int index, const std::string &name);

    // ===================================================
    //  参照
    // ===================================================

    std::vector<Entry> &GetEntries() { return entries_; }
    const std::vector<Entry> &GetEntries() const { return entries_; }
    size_t GetCount() const { return entries_.size(); }
    bool IsValidIndex(int index) const { return index >= 0 && index < static_cast<int>(entries_.size()); }

    /// <summary>
    /// 有効になっているライトの数（統計表示用）
    /// </summary>
    int GetActiveCount() const;

    /// <summary>
    /// これ以上追加できるか
    /// </summary>
    bool CanAdd() const { return entries_.size() < kMaxBufferedLights; }

    // ===================================================
    //  動的ライト（毎フレーム登録し直す実行時光源）
    // ===================================================

    /// <summary>
    /// 動的ライトの登録を全て破棄する（フレーム先頭で呼ぶ）
    /// </summary>
    void ClearDynamic() { dynamicLights_.clear(); }

    /// <summary>
    /// このフレームだけ有効なライトを登録する
    /// </summary>
    /// <param name="desc">ライトのパラメータ</param>
    void AddDynamic(const DynamicPointLightDesc &desc);

    /// <summary>
    /// このフレームに登録された動的ライトの数
    /// </summary>
    size_t GetDynamicCount() const { return dynamicLights_.size(); }

    // ===================================================
    //  GPU転送
    // ===================================================

    /// <summary>
    /// 前方描画用の定数バッファを更新する。
    /// MAX_POINT_LIGHTS を超える分は「明るさ×届く範囲÷カメラ距離」の大きい順に採用する。
    /// </summary>
    /// <param name="cameraPosition">優先度計算に使うカメラのワールド座標</param>
    void UpdateConstantBuffer(const Vector3 &cameraPosition);

    /// <summary>
    /// 手置き＋動的ライトをステージングバッファへ書き出す（ディファード用）
    /// </summary>
    void UploadStructuredBuffer();

    /// <summary>
    /// CPU側のライトをGPUバッファへ転送し、GPUからの追記を受け付ける状態にする。
    /// 描画コマンドリストで1フレームに1回だけ呼ぶこと。
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void BeginGpuAppend(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>
    /// GPUからの追記を締め、カリングCS／ライティングPSが読める状態へ遷移させる
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void EndGpuAppend(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>前方描画用の定数バッファのGPUアドレス</summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

    /// <summary>ディファードが読む StructuredBuffer のGPUアドレス</summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetStructuredBufferAddress() const;

    /// <summary>粒子光源CSの書き込み先（RWStructuredBuffer）のGPUアドレス</summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetUavAddress() const;

    /// <summary>ライト総数カウンタ（先頭1要素）のGPUアドレス</summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetCounterAddress() const;

    /// <summary>CPU側（手置き＋動的）が積んだライト数（＝GPU追記の開始位置）</summary>
    uint32_t GetBufferCount() const { return bufferCount_; }

    /// <summary>読み戻した「GPU側で確定したライト総数」（1〜2フレーム遅延・統計表示用）</summary>
    uint32_t GetGpuTotalCount() const { return gpuTotalCount_; }

    /// <summary>GPU側で追記された粒子光源の数（統計表示用）</summary>
    uint32_t GetParticleLightCount() const
    {
        return gpuTotalCount_ > bufferCount_ ? gpuTotalCount_ - bufferCount_ : 0;
    }

    // ===================================================
    //  UI・デバッグ描画
    // ===================================================

    /// <summary>
    /// 一覧の行をまとめて描く
    /// </summary>
    /// <param name="filter">名前の絞り込み文字列</param>
    /// <param name="selectedIndex">選択中の添字（このグループが選ばれていないなら -1）</param>
    /// <returns>LightListResult: 押された行（何も無ければ全て -1）</returns>
    LightListResult DrawListRows(const char *filter, int selectedIndex);

    /// <summary>
    /// プロパティ編集UI
    /// </summary>
    /// <param name="index">対象の添字</param>
    /// <param name="nameEditBuffer">名前入力欄のバッファ（呼び出し側が対象ごとに持つ）</param>
    /// <returns>LightEditRequest: 名前変更・複製・削除の要求</returns>
    LightEditRequest DrawProperties(int index, std::string &nameEditBuffer);

    /// <summary>
    /// ギズモのインスペクタに出す簡易表示（ライトの増減をしてはいけない）
    /// </summary>
    /// <param name="index">対象の添字</param>
    void DrawGizmoInspector(int index);

    /// <summary>
    /// デバッグ用の可視化
    /// </summary>
    /// <param name="drawLine">線の描画先</param>
    /// <param name="selectedIndex">選択中の添字（-1で無し）</param>
    /// <param name="selectedOnly">選択中以外は十字マーカーだけにするか</param>
    void DrawVisualization(LineRenderer *drawLine, int selectedIndex, bool selectedOnly) const;

    /// <summary>
    /// JSONへ保存する
    /// </summary>
    /// <param name="handler">保存先</param>
    void Save(DataHandler *handler) const;

    /// <summary>
    /// JSONから読み込む。名前の一意化は呼び出し側で行うこと
    /// </summary>
    /// <param name="handler">読み込み元</param>
    void Load(DataHandler *handler);

  private:
    /// <summary>
    /// StructuredBuffer の状態を遷移させる（現在状態を自分で覚えている）
    /// </summary>
    void TransitionBuffer(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after);

    /// <summary>
    /// ライト総数カウンタの状態を遷移させる
    /// </summary>
    void TransitionCounter(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after);

    /// <summary>前方描画用の定数バッファを作る</summary>
    void CreateConstantBuffer();

    /// <summary>ディファード用 StructuredBuffer（＋カウンタ）を作る</summary>
    void CreateStructuredBuffer();

    DirectXCommon *pDxCommon_ = nullptr; // DirectX基盤

    // 手で置いたライト（kMaxBufferedLights まで置ける）
    std::vector<Entry> entries_;
    // 実行時に登録される動的ライト（毎フレームクリアされる）
    std::vector<DynamicPointLightDesc> dynamicLights_;

    // ---- 前方描画用の定数バッファ ----
    Microsoft::WRL::ComPtr<ID3D12Resource> constantResource_;
    PointLightsCB *pConstantData_ = nullptr;
    // 定数バッファ枠（MAX_POINT_LIGHTS）へ詰めるときの並べ替え用スクラッチ。
    // 毎フレーム確保し直さないようメンバに持つ
    std::vector<const PointLightData *> sortScratch_;

    // ---- ディファード用 StructuredBuffer ----
    // 粒子光源CSがGPUから追記するため DEFAULTヒープ＋UAV で持つ。
    Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource_;
    D3D12_RESOURCE_STATES bufferState_ = D3D12_RESOURCE_STATE_COMMON;
    // CPU（手置き＋動的ライト）が書き込むステージング。毎フレーム上のバッファ先頭へコピーする
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource_;
    PointLightGPU *pBufferData_ = nullptr;
    uint32_t bufferCount_ = 0; // CPU由来のライト数（＝GPU追記の開始位置）

    // ライト総数カウンタ（先頭1要素）。CPU分で初期化し、粒子光源CSが InterlockedAdd で足す
    Microsoft::WRL::ComPtr<ID3D12Resource> counterResource_;
    D3D12_RESOURCE_STATES counterState_ = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> counterUploadResource_;
    uint32_t *pCounterUploadData_ = nullptr;
    // 統計表示用の読み戻し（1〜2フレーム遅延で構わない）
    Microsoft::WRL::ComPtr<ID3D12Resource> counterReadbackResource_;
    uint32_t gpuTotalCount_ = 0;
};
} // namespace Hagine
