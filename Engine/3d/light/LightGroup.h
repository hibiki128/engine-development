#pragma once
#include "data/DataHandler.h"
#include "d3d12.h"
#include "nlohmann/json.hpp"
#include "wrl.h"
#include <camera/projection/ViewProjection.h>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>

// ---- 前方描画（定数バッファ経由）のライト枠 ----
// ディファードON時はポイントライトを StructuredBuffer（kMaxBufferedPointLights）から読むため、
// この枠は「ディファードOFFのときのフォールバック」でしか使われない。
// ※ EngineAssets/shaders/Object/Object3d.hlsli の同名マクロと必ず一致させること。
#define MAX_POINT_LIGHTS 16
// スポットライトは前方・ディファードともに定数バッファ経由（タイルカリング対象外）なので上限あり。
// ※ Object3d.hlsli の MAX_SPOT_LIGHTS / Deferred.hlsli の MAX_SPOT_LIGHTS_DEFERRED と一致させること。
#define MAX_SPOT_LIGHTS 32

/// <summary>
/// ライトタイプ種別
/// </summary>
namespace Hagine {
enum class LightType
{
    Directional, // 平行光源
    Point,       // ポイントライト
    Spot         // スポットライト
};

class DirectXCommon;

/// <summary>
/// ライトグループクラス
/// シーン内の各種光源（平行・点・スポット）を一括管理し、GPUへ定数データとして転送する
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
    /// 動的ポイントライト（毎フレーム登録し直す実行時光源）
    /// ===================================================

    /// <summary>
    /// 実行時に登録する動的ポイントライトのパラメータ
    /// </summary>
    struct DynamicPointLightDesc
    {
        Vector3 position{};                  // ワールド座標
        Vector4 color = {1.f, 1.f, 1.f, 1.f}; // 色
        float intensity = 1.0f;              // 輝度
        float radius = 5.0f;                 // 影響半径
        float decay = 1.0f;                  // 減衰率
    };

    /// <summary>
    /// 動的ポイントライトの登録を全て破棄する（フレーム先頭で呼ぶ）
    /// </summary>
    void ClearDynamicPointLights() { dynamicPointLights_.clear(); }

    /// <summary>
    /// このフレームだけ有効なポイントライトを登録する。
    /// GPUパーティクルの発光など、毎フレーム位置・強さが変わる光源に使う。
    /// オーサリング済みライトと合わせて MAX_POINT_LIGHTS を超えた場合は、
    /// カメラからの距離と明るさで決めた優先度が高いものだけが採用される。
    /// </summary>
    /// <param name="desc">ライトのパラメータ</param>
    void AddDynamicPointLight(const DynamicPointLightDesc &desc);

    /// <summary>
    /// オーサリング済みライトと動的ライトを統合して定数バッファへ転送する。
    /// 動的ライトの登録が済んだ後（描画直前）に呼ぶ。
    /// </summary>
    void CommitPointLights();

    /// <summary>
    /// このフレームに登録された動的ポイントライトの数を取得
    /// </summary>
    size_t GetDynamicPointLightCount() const { return dynamicPointLights_.size(); }

    /// ===================================================
    /// ディファードレンダリング用（多光源）
    /// ===================================================

    /// <summary>
    /// ディファードのライトカリング／ライティングが読む、ポイントライトの
    /// StructuredBuffer。CBの MAX_POINT_LIGHTS 制限を受けず、
    /// オーサリング済み＋動的ライト＋GPU生成の粒子光源を全て格納する。
    /// </summary>
    /// <returns>StructuredBufferのGPUアドレス</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetPointLightBufferAddress() const;

    /// <summary>
    /// CPU側（手置き＋動的）が積んだポイントライトの数。
    /// GPUが追記する粒子光源はこれに含まれない（総数は GPU 上のカウンタが持つ）
    /// </summary>
    uint32_t GetPointLightBufferCount() const { return pointLightBufferCount_; }

    /// ===================================================
    /// GPU生成ライト（粒子1個1個の光源化）
    /// ===================================================
    ///
    /// ポイントライトのStructuredBufferは DEFAULTヒープ＋UAV で持ち、
    /// 「CPU分をコピーで先頭へ詰める → GPUがその後ろへ追記する」形をとる。
    /// ライト総数はCPUでは確定できないため GPU 上のカウンタバッファに持ち、
    /// ライトカリングCSはそれを読んで走査数を決める。
    ///
    /// フレーム内の呼び出し順:
    ///   CommitPointLights() → BeginGpuLightAppend() → 粒子光源CS → EndGpuLightAppend()

    /// <summary>
    /// CPU側のライトをGPUバッファへ転送し、GPUからの追記を受け付ける状態にする。
    /// 描画コマンドリストで1フレームに1回だけ呼ぶこと。
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void BeginGpuLightAppend(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>
    /// GPUからの追記を締め、カリングCS／ライティングPSが読める状態へ遷移させる。
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    void EndGpuLightAppend(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>
    /// 粒子光源CSの書き込み先（RWStructuredBuffer&lt;PointLightGPU&gt;）のGPUアドレス
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetPointLightUavAddress() const;

    /// <summary>
    /// ライト総数カウンタ（先頭1要素）のGPUアドレス。
    /// BeginGpuLightAppend でCPU分の個数に初期化され、粒子光源CSが InterlockedAdd で足す。
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetLightCounterAddress() const;

    /// <summary>
    /// 読み戻した「GPU側で確定したライト総数」（1〜2フレーム遅延・統計表示用）。
    /// kMaxBufferedPointLights を超えていれば粒子光源が切り捨てられている。
    /// </summary>
    uint32_t GetGpuTotalLightCount() const { return gpuTotalLightCount_; }

    /// <summary>
    /// GPU側で追記された粒子光源の数（総数からCPU分を引いた値・統計表示用）
    /// </summary>
    uint32_t GetParticleLightCount() const
    {
        return gpuTotalLightCount_ > pointLightBufferCount_ ? gpuTotalLightCount_ - pointLightBufferCount_ : 0;
    }

    /// <summary>
    /// StructuredBufferに格納できるポイントライトの最大数
    /// </summary>
    static constexpr uint32_t kMaxBufferedPointLights = 8192;

    /// <summary>
    /// 平行光源の定数バッファのGPUアドレス（ディファードのライティングパス用）
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetDirectionalLightAddress() const;

    /// <summary>
    /// スポットライトの定数バッファのGPUアドレス（ディファードのライティングパス用）
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetSpotLightsAddress() const;

    Vector3 GetDirectionalLightDirection() const
    {
        if (pDirectionalLightData_)
            return pDirectionalLightData_->direction;
        return {0.f, -1.f, 0.f};
    }
    bool IsDirectionalLightActive() const
    {
        return pDirectionalLightData_ && pDirectionalLightData_->active != 0;
    }

  private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    LightGroup() = default;
    ~LightGroup() = default;
    LightGroup(LightGroup &) = delete;
    LightGroup &operator=(LightGroup &) = delete;

    /// <summary>
    /// 平行光源用バッファの生成
    /// </summary>
    void CreateDirectionLight();

    /// <summary>
    /// ポイントライト用バッファの生成
    /// </summary>
    void CreatePointLights();

    /// <summary>
    /// スポットライト用バッファの生成
    /// </summary>
    void CreateSpotLights();

    /// <summary>
    /// カメラ情報用バッファの生成
    /// </summary>
    void CreateCamera();

    /// <summary>
    /// ポイントライトを追加
    /// </summary>
    /// <returns>int: 追加した要素のインデックス（追加できなければ -1）</returns>
    int AddPointLight();

    /// <summary>
    /// ポイントライトを削除
    /// </summary>
    /// <param name="index">対象インデックス</param>
    void RemovePointLight(int index);

    /// <summary>
    /// ポイントライトを複製する
    /// </summary>
    /// <param name="index">複製元インデックス</param>
    /// <returns>int: 複製した要素のインデックス（複製できなければ -1）</returns>
    int DuplicatePointLight(int index);

    /// <summary>
    /// スポットライトを追加
    /// </summary>
    /// <returns>int: 追加した要素のインデックス（追加できなければ -1）</returns>
    int AddSpotLight();

    /// <summary>
    /// スポットライトを削除
    /// </summary>
    /// <param name="index">対象インデックス</param>
    void RemoveSpotLight(int index);

    /// <summary>
    /// スポットライトを複製する
    /// </summary>
    /// <param name="index">複製元インデックス</param>
    /// <returns>int: 複製した要素のインデックス（複製できなければ -1）</returns>
    int DuplicateSpotLight(int index);

    /// <summary>
    /// 光源名を他とかぶらないように調整して返す
    /// </summary>
    /// <param name="desired">希望する名前</param>
    /// <param name="ignorePoint">重複チェックから除外するポイントライトの添字（-1で無し）</param>
    /// <param name="ignoreSpot">重複チェックから除外するスポットライトの添字（-1で無し）</param>
    /// <returns>std::string: 一意な名前</returns>
    std::string MakeUniqueLightName(const std::string &desired, int ignorePoint = -1, int ignoreSpot = -1) const;

    /// <summary>
    /// スポットライトの向きハンドル位置を更新し、動かされていれば向きへ反映する
    /// </summary>
    void UpdateSpotAimPoints();

    /// ===================================================
    /// ギズモ連携（_DEBUG のみ実体を持つ）
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
    /// ライトのギズモ登録名（ポイントライト本体）
    /// </summary>
    std::string PointGizmoName(int index) const;

    /// <summary>
    /// ライトのギズモ登録名（スポットライト本体 / 向きハンドル）
    /// </summary>
    std::string SpotGizmoName(int index) const;
    std::string SpotAimGizmoName(int index) const;

    /// ===================================================
    /// ImGui 描画の細分化
    /// ===================================================

    void DrawStatusHeader();
    void DrawLightListPanel(float height);
    void DrawPropertyPanel(float height);
    void DrawDirectionalProperties();
    void DrawPointProperties(int index);
    void DrawSpotProperties(int index);
    void DrawSaveLoadSection();

    /// <summary>
    /// ギズモのインスペクタに出す簡易表示。
    /// ImGuizmoManager がターゲットを走査している最中に呼ばれるため、
    /// ここでライトの増減や登録し直しをしてはいけない（自分自身を破棄してしまう）。
    /// </summary>
    /// <param name="isSpot">スポットライトなら true</param>
    /// <param name="index">対象インデックス</param>
    void DrawGizmoInspector(bool isSpot, int index);

    /// <summary>
    /// 一覧で選んだライトをギズモ側の選択にも反映する
    /// </summary>
    void SyncSelectionToGizmo();

    /// <summary>
    /// ポイントライトバッファの同期
    /// </summary>
    void UpdatePointLightBuffer();

    /// <summary>
    /// スポットライトバッファの同期
    /// </summary>
    void UpdateSpotLightBuffer();

    /// <summary>
    /// デバッグ用の光源位置描画
    /// </summary>
    void DrawLightVisualization();

    /// <summary>
    /// ディファード用ポイントライトStructuredBuffer（＋ライト総数カウンタ）の生成
    /// </summary>
    void CreatePointLightBuffer();

    /// <summary>
    /// オーサリング済み＋動的ライトをステージングバッファへ書き出す
    /// </summary>
    void UploadPointLightBuffer();

    /// <summary>
    /// ポイントライトバッファの状態を遷移させる（現在状態を自分で覚えている）
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト</param>
    /// <param name="after">遷移先の状態</param>
    void TransitionPointLightBuffer(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after);

    /// <summary>
    /// ライト総数カウンタの状態を遷移させる
    /// </summary>
    /// <param name="pCommandList">記録先のコマンドリスト</param>
    /// <param name="after">遷移先の状態</param>
    void TransitionLightCounter(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after);

  private:
    // ===================================================
    // 構造体定義
    // ===================================================

    /// <summary>
    /// 平行光源データ
    /// </summary>
    struct DirectionLight
    {
        Vector4 color;       // 色 (RGBA)
        Vector3 direction;   // 方向
        float intensity;     // 輝度
        int32_t active;      // 有効フラグ
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
    };

    /// <summary>
    /// ポイントライトデータ
    /// </summary>
    struct PointLight
    {
        Vector4 color;       // 色 (RGBA)
        Vector3 position;    // 位置
        float intensity;     // 輝度
        int32_t active;      // 有効フラグ
        float radius;        // 影響半径
        float decay;         // 減衰率
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
        float padding[3];
    };

    /// <summary>
    /// ポイントライト群
    /// </summary>
    struct PointLights
    {
        alignas(16) PointLight lights[MAX_POINT_LIGHTS];
        int32_t count; // 有効数
        float padding[3];
    };

    /// <summary>
    /// スポットライトデータ
    /// </summary>
    struct SpotLight
    {
        Vector4 color;       // 色 (RGBA)
        Vector3 position;    // 位置
        float intensity;     // 輝度
        Vector3 direction;   // 方向
        float distance;      // 照射距離
        float decay;         // 減衰率
        float cosAngle;      // コーン角度の余弦
        int32_t active;      // 有効フラグ
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
        float padding[3];
    };

    /// <summary>
    /// スポットライト群
    /// </summary>
    struct SpotLights
    {
        SpotLight lights[MAX_SPOT_LIGHTS];
        int32_t count; // 有効数
        float padding[3];
    };

    /// <summary>
    /// GPU転送用カメラデータ
    /// </summary>
    struct CameraForGPU
    {
        Vector3 worldPosition; // ワールド座標
    };

    /// <summary>
    /// オーサリング済みポイントライト1個ぶんの編集データ。
    /// GPUへ流す実体（gpu）と、エディタ用の情報を分けて持つ。
    /// </summary>
    struct PointLightEntry
    {
        PointLight gpu{};  // 定数バッファ／StructuredBuffer へ転送する実体
        std::string name;  // 一覧表示とギズモ登録に使う名前（ライト間で一意）
    };

    /// <summary>
    /// オーサリング済みスポットライト1個ぶんの編集データ
    /// </summary>
    struct SpotLightEntry
    {
        SpotLight gpu{};      // 転送する実体
        std::string name;     // 一覧表示とギズモ登録に使う名前
        Vector3 aimPoint{};   // 向きを操作するためのギズモハンドル位置（照射先）
        Vector3 prevAim{};    // 前フレームのハンドル位置。ギズモで動かされたかの判定に使う
    };

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

  public:
    /// <summary>
    /// ディファード用ポイントライト（StructuredBuffer要素・32バイト）
    /// HLSL側の PointLightGPU と必ず一致させること
    /// </summary>
    struct PointLightGPU
    {
        Vector3 position{};  // ワールド座標
        float radius = 0.0f; // 影響半径
        Vector3 color{};     // 色（RGB）
        float intensity = 0.0f; // 輝度
        float decay = 1.0f;  // 減衰率
        uint32_t flags = 0;  // bit0: HalfLambert / bit1: BlinnPhong
        float padding[2]{};  // 16バイト境界合わせ
    };
    static_assert(sizeof(PointLightGPU) == 48, "PointLightGPUはHLSL側と一致させること");

  private:

  private:
    // ===================================================
    // メンバ変数
    // ===================================================

    DirectXCommon *pDxCommon_ = nullptr; // DirectX基盤へのポインタ

    // リソース管理
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_; // 平行光源バッファ
    DirectionLight *pDirectionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightsResource_; // ポイントライトバッファ
    PointLights *pPointLightsData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightsResource_; // スポットライトバッファ
    SpotLights *pSpotLightsData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraForGPUResource_; // カメラデータバッファ
    CameraForGPU *pCameraForGPUData_ = nullptr;

    // ライトリスト管理（オーサリング済み。ポイントライトは kMaxBufferedPointLights まで置ける）
    std::vector<PointLightEntry> pointLights_;
    std::vector<SpotLightEntry> spotLights_;

    // 定数バッファ枠（MAX_POINT_LIGHTS）へ詰めるときの並べ替え用スクラッチ。
    // 毎フレーム確保し直さないようメンバに持つ
    std::vector<const PointLight *> cbSortScratch_;

    // 実行時に登録される動的ポイントライト（毎フレームクリアされる）
    std::vector<DynamicPointLightDesc> dynamicPointLights_;
    // 優先度計算に使うカメラ位置（Updateで更新）
    Vector3 cameraPosition_{};

    // ディファード用ポイントライトのStructuredBuffer（CBの上限を受けない全ライト）。
    // 粒子光源CSがGPUから追記するため DEFAULTヒープ＋UAV で持つ。
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightBufferResource_;
    D3D12_RESOURCE_STATES pointLightBufferState_ = D3D12_RESOURCE_STATE_COMMON;
    // CPU（手置き＋動的ライト）が書き込むステージング。毎フレーム上のバッファ先頭へコピーする
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightUploadResource_;
    PointLightGPU *pPointLightBufferData_ = nullptr;
    uint32_t pointLightBufferCount_ = 0; // CPU由来のライト数（＝GPU追記の開始位置）

    // ライト総数カウンタ（先頭1要素）。CPU分で初期化し、粒子光源CSが InterlockedAdd で足す
    Microsoft::WRL::ComPtr<ID3D12Resource> lightCounterResource_;
    D3D12_RESOURCE_STATES lightCounterState_ = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightCounterUploadResource_;
    uint32_t *pLightCounterUploadData_ = nullptr;
    // 統計表示用の読み戻し（1〜2フレーム遅延で構わない）
    Microsoft::WRL::ComPtr<ID3D12Resource> lightCounterReadbackResource_;
    uint32_t gpuTotalLightCount_ = 0;

    // ギズモへ登録済みの名前一覧（解除するときに使う）
    std::vector<std::string> gizmoNames_;

    // UI状態
    std::string saveMessage_;
    int saveMessageTimer_ = 0;
    bool isDirectionalLight_ = true; // 平行光源の全体有効フラグ
    bool showLightVisualization_ = false;
    bool visualizeSelectedOnly_ = true; // 可視化を選択中のライトだけに絞る（大量配置時の描画負荷対策）
    bool syncGizmoSelection_ = true;    // 一覧の選択をギズモ側へ反映するか

    SelectionKind selectedKind_ = SelectionKind::Directional; // 一覧で選択中の種類
    int selectedIndex_ = -1;                                  // 一覧で選択中の添字（Point/Spot のとき有効）
    char listFilter_[128] = "";                               // 一覧の絞り込み文字列

    // 名前入力欄のバッファ。編集中に外から書き換えると入力が消えるので、
    // 対象が変わったときだけ現在名を流し込む
    std::string nameEditBuffer_; // 名前編集の一時バッファ（imgui_stdlib で std::string を直接編集）
    std::string nameEditOwner_;

    std::unique_ptr<DataHandler> lightDataHandler_ = nullptr; // JSONハンドラー
};
} // namespace Hagine
