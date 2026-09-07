#pragma once
#include "DirectionalLight.h"
#include "LightTypes.h"
#include "PointLightGroup.h"
#include "SpotLightGroup.h"
#include "d3d12.h"
#include "data/DataHandler.h"
#include "wrl.h"
#include <camera/projection/ViewProjection.h>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// シーン内の光源をまとめる入れ物。
///
/// 種類ごとの中身は3つのクラスへ分けてあり、ここはそれらを束ねる役に徹する:
///   ・DirectionalLight … 平行光源（太陽光）1つ
///   ・PointLightGroup  … 点光源。ディファードのStructuredBufferとGPU追記もここが持つ
///   ・SpotLightGroup   … スポットライト
///
/// 種類をまたぐ仕事（名前の一意化・ギズモ登録・一覧の選択状態・カメラ定数バッファ・
/// 描画時のバインド・セーブ/ロードの取りまとめ）だけがこのクラスに残っている。
/// </summary>
class LightGroup
{
  public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static LightGroup *GetInstance()
    {
        static LightGroup instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="viewProjection">カメラのビュープロジェクション</param>
    void Update(const ViewProjection &viewProjection);

    /// <summary>
    /// 描画設定（ルートシグネチャへのバインドなど）
    /// </summary>
    void Draw();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// ライトデータをJSONへ保存
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void SaveLightData(const std::string &fileName);

    /// <summary>
    /// ライトデータをJSONから読み込み
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void LoadLightData(const std::string &fileName);

    /// <summary>
    /// 光源可視化フラグを設定
    /// </summary>
    void SetShowLightVisualization(bool show) { showLightVisualization_ = show; }

    /// ===================================================
    /// 種類ごとのライトへの入口
    /// ===================================================

    DirectionalLight &GetDirectionalLight() { return directionalLight_; }
    const DirectionalLight &GetDirectionalLight() const { return directionalLight_; }
    PointLightGroup &GetPointLights() { return pointLights_; }
    const PointLightGroup &GetPointLights() const { return pointLights_; }
    SpotLightGroup &GetSpotLights() { return spotLights_; }
    const SpotLightGroup &GetSpotLights() const { return spotLights_; }

    /// ===================================================
    /// 動的ポイントライト（毎フレーム登録し直す実行時光源）
    /// ===================================================

    /// <summary>
    /// 実行時に登録する動的ポイントライトのパラメータ（LightTypes.h の別名）
    /// </summary>
    using DynamicPointLightDesc = Hagine::DynamicPointLightDesc;

    /// <summary>
    /// 動的ポイントライトの登録を全て破棄する（フレーム先頭で呼ぶ）
    /// </summary>
    void ClearDynamicPointLights() { pointLights_.ClearDynamic(); }

    /// <summary>
    /// このフレームだけ有効なポイントライトを登録する。
    /// GPUパーティクルの発光など、毎フレーム位置・強さが変わる光源に使う。
    /// オーサリング済みライトと合わせて MAX_POINT_LIGHTS を超えた場合は、
    /// カメラからの距離と明るさで決めた優先度が高いものだけが採用される。
    /// </summary>
    /// <param name="desc">ライトのパラメータ</param>
    void AddDynamicPointLight(const DynamicPointLightDesc &desc) { pointLights_.AddDynamic(desc); }

    /// <summary>
    /// オーサリング済みライトと動的ライトを統合して定数バッファへ転送する。
    /// 動的ライトの登録が済んだ後（描画直前）に呼ぶ。
    /// </summary>
    void CommitPointLights();

    /// <summary>
    /// このフレームに登録された動的ポイントライトの数を取得
    /// </summary>
    size_t GetDynamicPointLightCount() const { return pointLights_.GetDynamicCount(); }

    /// ===================================================
    /// ディファードレンダリング用（多光源）
    /// ===================================================

    /// <summary>
    /// StructuredBufferに格納できるポイントライトの最大数
    /// </summary>
    static constexpr uint32_t kMaxBufferedPointLights = PointLightGroup::kMaxBufferedLights;

    /// <summary>
    /// ディファードのライトカリング／ライティングが読む、ポイントライトの StructuredBuffer
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetPointLightBufferAddress() const { return pointLights_.GetStructuredBufferAddress(); }

    /// <summary>
    /// CPU側（手置き＋動的）が積んだポイントライトの数
    /// </summary>
    uint32_t GetPointLightBufferCount() const { return pointLights_.GetBufferCount(); }

    /// <summary>
    /// CPU側のライトをGPUバッファへ転送し、GPUからの追記を受け付ける状態にする
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void BeginGpuLightAppend(ID3D12GraphicsCommandList *pCommandList) { pointLights_.BeginGpuAppend(pCommandList); }

    /// <summary>
    /// GPUからの追記を締め、カリングCS／ライティングPSが読める状態へ遷移させる
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void EndGpuLightAppend(ID3D12GraphicsCommandList *pCommandList) { pointLights_.EndGpuAppend(pCommandList); }

    /// <summary>
    /// 粒子光源CSの書き込み先（RWStructuredBuffer&lt;PointLightGPU&gt;）のGPUアドレス
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetPointLightUavAddress() const { return pointLights_.GetUavAddress(); }

    /// <summary>
    /// ライト総数カウンタ（先頭1要素）のGPUアドレス
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetLightCounterAddress() const { return pointLights_.GetCounterAddress(); }

    /// <summary>
    /// 読み戻した「GPU側で確定したライト総数」（1〜2フレーム遅延・統計表示用）
    /// </summary>
    uint32_t GetGpuTotalLightCount() const { return pointLights_.GetGpuTotalCount(); }

    /// <summary>
    /// GPU側で追記された粒子光源の数（統計表示用）
    /// </summary>
    uint32_t GetParticleLightCount() const { return pointLights_.GetParticleLightCount(); }

    /// <summary>
    /// 平行光源の定数バッファのGPUアドレス（ディファードのライティングパス用）
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetDirectionalLightAddress() const { return directionalLight_.GetGpuAddress(); }

    /// <summary>
    /// スポットライトの定数バッファのGPUアドレス（ディファードのライティングパス用）
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetSpotLightsAddress() const { return spotLights_.GetConstantBufferAddress(); }

    Vector3 GetDirectionalLightDirection() const { return directionalLight_.GetDirection(); }
    bool IsDirectionalLightActive() const { return directionalLight_.IsEnabled(); }

  private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    LightGroup() = default;
    ~LightGroup() = default;
    LightGroup(LightGroup &) = delete;
    LightGroup &operator=(LightGroup &) = delete;

    /// <summary>
    /// カメラ情報用バッファの生成
    /// </summary>
    void CreateCamera();

    /// <summary>
    /// 光源名を他とかぶらないように調整して返す。
    /// 点光源とスポットライトはギズモの登録名を共有するので、両方を見て一意にする。
    /// </summary>
    /// <param name="desired">希望する名前</param>
    /// <param name="ignorePoint">重複チェックから除外するポイントライトの添字（-1で無し）</param>
    /// <param name="ignoreSpot">重複チェックから除外するスポットライトの添字（-1で無し）</param>
    /// <returns>std::string: 一意な名前</returns>
    std::string MakeUniqueLightName(const std::string &desired, int ignorePoint = -1, int ignoreSpot = -1) const;

    /// <summary>
    /// 読み込み直後など、名前が重複しうる状態を一意な名前へ整える
    /// </summary>
    void EnsureUniqueNames();

    /// ===================================================
    /// ギズモ連携
    /// ===================================================

    /// <summary>
    /// 全ライトをギズモへ登録し直す。
    /// std::vector の再確保でポインタが変わるため、ライトの増減・名前変更のたびに呼ぶこと。
    /// </summary>
    void SyncGizmoTargets();

    /// <summary>
    /// ギズモへ登録済みのライトを全て解除する
    /// </summary>
    void UnregisterGizmoTargets();

    /// <summary>
    /// 種類をまたいだ親子付け（AttachmentManager）へ光源を登録し直す。
    /// ギズモと違い Release でも必要なので、こちらは USE_IMGUI に依存しない。
    /// </summary>
    void SyncAttachTargets();

  public:
    /// <summary>
    /// 光源の親子付け登録名を返す（3Dオブジェクトと衝突しないよう接頭辞が付く）
    /// </summary>
    /// <param name="lightName">光源の名前</param>
    /// <returns>std::string: 親子付けの登録名</returns>
    static std::string AttachName(const std::string &lightName);

  private:

    /// <summary>
    /// ライトのギズモ登録名（ポイントライト本体）
    /// </summary>
    std::string PointGizmoName(int index) const;

    /// <summary>
    /// ライトのギズモ登録名（スポットライト本体 / 向きハンドル）
    /// </summary>
    std::string SpotGizmoName(int index) const;
    std::string SpotAimGizmoName(int index) const;

    /// <summary>
    /// 一覧で選んだライトをギズモ側の選択にも反映する
    /// </summary>
    void SyncSelectionToGizmo();

    /// <summary>
    /// ギズモで掴まれたライトへ一覧の選択を合わせる
    /// </summary>
    void SyncSelectionFromGizmo();

    /// ===================================================
    /// ImGui 描画の細分化
    /// ===================================================

    void DrawStatusHeader();
    void DrawLightListPanel(float height);
    void DrawPropertyPanel(float height);
    void DrawSaveLoadSection();

    /// <summary>
    /// プロパティUIから返ってきた要求（名前変更・複製・削除）を実行する
    /// </summary>
    /// <param name="request">要求</param>
    /// <param name="isSpot">スポットライトへの要求なら true</param>
    /// <param name="index">対象の添字</param>
    void ApplyEditRequest(const LightEditRequest &request, bool isSpot, int index);

    /// <summary>
    /// 選択対象が変わったときだけ名前入力欄へ現在名を流し込む
    /// （毎フレーム上書きすると編集中の入力が消えるため）
    /// </summary>
    /// <param name="owner">対象を表すキー</param>
    /// <param name="currentName">現在の名前</param>
    void SyncNameEditBuffer(const std::string &owner, const std::string &currentName);

    /// <summary>
    /// デバッグ用の光源位置描画
    /// </summary>
    void DrawLightVisualization();

  private:
    /// <summary>
    /// 一覧で選択中の対象の種類
    /// </summary>
    enum class SelectionKind
    {
        None,        // 未選択
        Directional, // 平行光源
        Point,       // ポイントライト
        Spot,        // スポットライト
    };

    // ===================================================
    // メンバ変数
    // ===================================================

    DirectXCommon *pDxCommon_ = nullptr; // DirectX基盤へのポインタ

    // 種類ごとのライト
    DirectionalLight directionalLight_;
    PointLightGroup pointLights_;
    SpotLightGroup spotLights_;

    // カメラ情報（陰影計算で視線ベクトルを求めるのに使う）
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraForGPUResource_;
    CameraForGPU *pCameraForGPUData_ = nullptr;
    Vector3 cameraPosition_{}; // 優先度計算に使うカメラ位置（Updateで更新）

    // ギズモへ登録済みの名前一覧（解除するときに使う）
    std::vector<std::string> gizmoNames_;

    // UI状態
    bool showLightVisualization_ = false;
    bool visualizeSelectedOnly_ = true; // 可視化を選択中のライトだけに絞る（大量配置時の描画負荷対策）
    bool syncGizmoSelection_ = true;    // 一覧の選択をギズモ側へ反映するか

    SelectionKind selectedKind_ = SelectionKind::Directional; // 一覧で選択中の種類
    int selectedIndex_ = -1;                                  // 一覧で選択中の添字（Point/Spot のとき有効）

    // 親子付けの登録を作り直すか判定するための、前回の光源数
    size_t lastAttachPointCount_ = 0;
    size_t lastAttachSpotCount_ = 0;
    char listFilter_[128] = "";                               // 一覧の絞り込み文字列

    // 名前入力欄のバッファ。編集中に外から書き換えると入力が消えるので、
    // 対象が変わったときだけ現在名を流し込む
    std::string nameEditBuffer_; // 名前編集の一時バッファ（imgui_stdlib で std::string を直接編集）
    std::string nameEditOwner_;
};
} // namespace Hagine
