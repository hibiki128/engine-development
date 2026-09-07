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
/// スポットライトをまとめて管理するクラス。
///
/// 前方描画・ディファードともに定数バッファ経由（タイルカリングの対象外）なので、
/// 点光源と違って MAX_SPOT_LIGHTS 個までの固定枠しか持たない。
///
/// 向きは回転ギズモではなく「照射先の点（aimPoint）」を掴んで決める。
/// ImGuizmoManager は平行移動しか各ターゲットへ反映しないため、この形が確実。
///
/// 一覧の選択やギズモ登録、名前の一意化は LightGroup の担当なので、
/// このクラスは「要求を返すだけ」で自分では実行しない。
/// </summary>
class SpotLightGroup
{
  public:
    /// <summary>
    /// スポットライト1個ぶんの編集データ
    /// </summary>
    struct Entry
    {
        SpotLightData gpu{};  // 転送する実体
        std::string name;     // 一覧表示とギズモ登録に使う名前
        Vector3 aimPoint{};   // 向きを操作するためのギズモハンドル位置（照射先）
        Vector3 prevAim{};    // 前フレームのハンドル位置。ギズモで動かされたかの判定に使う
    };

    /// <summary>
    /// 定数バッファを生成する
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
    bool CanAdd() const { return entries_.size() < MAX_SPOT_LIGHTS; }

    // ===================================================
    //  更新・GPU転送
    // ===================================================

    /// <summary>
    /// ギズモで動かされた向きハンドルを向きへ反映し、ハンドル位置を張り直す（毎フレーム呼ぶ）
    /// </summary>
    void UpdateAimPoints();

    /// <summary>
    /// 定数バッファへ転送する
    /// </summary>
    void UpdateConstantBuffer();

    /// <summary>
    /// 定数バッファのGPUアドレス
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;

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
    /// <param name="aimHandleSuffix">向きハンドルの名前につく接尾辞（説明文に出す）</param>
    /// <returns>LightEditRequest: 名前変更・複製・削除の要求</returns>
    LightEditRequest DrawProperties(int index, std::string &nameEditBuffer, const char *aimHandleSuffix);

    /// <summary>
    /// ギズモのインスペクタに出す簡易表示（ライトの増減をしてはいけない）
    /// </summary>
    /// <param name="index">対象の添字</param>
    void DrawGizmoInspector(int index);

    /// <summary>
    /// デバッグ用の可視化（コーン形状と向きハンドル）
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
    DirectXCommon *pDxCommon_ = nullptr;              // DirectX基盤
    std::vector<Entry> entries_;                      // 手で置いたライト
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_; // 定数バッファ
    SpotLightsCB *pData_ = nullptr;                   // 書き込み先
};
} // namespace Hagine
