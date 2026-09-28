#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <type/Matrix4x4.h>
#include <unordered_map>
#include <vector>

namespace Hagine {
class DirectXCommon;
class SrvManager;
class Model;

/// <summary>
/// レイトレーシングの加速構造（BLAS / TLAS）を管理するクラス。
///
/// インラインRT（RayQuery）を使うので、**シェーダーテーブルも DXR 用のステートオブジェクトも
/// raygen/hit/miss シェーダーも要らない。** 必要なのは
///   ・BLAS … メッシュの形そのもの。モデル1つにつき1回だけ作って使い回す
///   ・TLAS … そのBLASを「どこに何個置いたか」の一覧。毎フレーム組み直す
/// の2つと、TLAS を指すSRVだけ。
///
/// 使い方（毎フレーム）:
///   ワールド行列が確定したら BeginFrame() → 置きたいオブジェクトのぶん Submit()。
///   スキニングで動くモデルはここで器だけ用意し、スキニングのディスパッチ直後に
///   BuildSkinnedBlas() が今のポーズで中身を作る（Model::Update が呼んでいる）。
///   レイを飛ばす側は、まず RequestUse() で「使う」と伝え、
///   使う直前に EnsureTlas() を呼んでから GetTlasGpuAddress() でSRVを作る。
///   （RequestUse() が無いフレームは何も作らない＝使っていないときは無料）
///
/// **順番が命。** TLAS はスキンのBLASより後に組まないと、TLAS が持つ大まかな範囲が
/// 前のポーズのままになり、動いたキャラの一部が影から欠ける。
///
/// 対応していない環境では IsAvailable() が false を返し、すべての操作が何もしない
/// </summary>
class RaytracingScene
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static RaytracingScene *GetInstance()
    {
        static RaytracingScene instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    /// <param name="pSrvManager">SRVマネージャー</param>
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager);

    /// <summary>
    /// 終了処理（加速構造を破棄する）
    /// </summary>
    void Finalize();

    /// <summary>
    /// レイトレーシングが使えるか（環境が対応していて、初期化も済んでいるか）
    /// </summary>
    /// <returns>bool: 使えるなら true</returns>
    bool IsAvailable() const { return available_; }

    /// <summary>
    /// このフレームのインスタンス集めを始める
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 「この先レイを飛ばすので加速構造を作っておいてほしい」と伝える。
    ///
    /// これが無いフレームは Submit も BLAS の作り直しも丸ごと省く。
    /// レイトレーシングを使っていないのに、スキンのBLASを毎フレーム作り直すのは無駄なため。
    /// 伝えるのは使う側（RtShadowPass など）で、効き始めるのは次のフレームから
    /// （積むのは描画より前なので、このフレームぶんはもう間に合わない）
    /// </summary>
    void RequestUse();

    /// <summary>
    /// このフレームは加速構造を作るか（＝前のフレームに RequestUse があったか）
    /// </summary>
    /// <returns>bool: 作るなら true</returns>
    bool IsActive() const { return available_ && active_; }

    /// <summary>
    /// このフレームのTLASへオブジェクトを1つ積む。
    /// BLAS が未作成ならここで作る（モデル単位で一度だけ）
    /// </summary>
    /// <param name="pModel">対象のモデル</param>
    /// <param name="worldMatrix">ワールド行列</param>
    void Submit(Model *pModel, const Matrix4x4 &worldMatrix);

    /// <summary>
    /// スキニングで動くモデルのBLASを、今のポーズで作り直す。
    ///
    /// 呼ぶ場所は「スキニングのディスパッチ直後」。出力頂点バッファはそこで
    /// UNORDERED_ACCESS 状態なので、加速構造が読める NON_PIXEL_SHADER_RESOURCE へ
    /// 移してから構築する。頂点バッファ状態へ戻すのは呼び出し側（Model::Update）の役目
    /// </summary>
    /// <param name="pModel">対象のモデル</param>
    /// <returns>bool: 構築した（＝出力頂点バッファを NON_PIXEL_SHADER_RESOURCE にした）なら true</returns>
    bool BuildSkinnedBlas(Model *pModel);

    /// <summary>
    /// 積んだ内容から TLAS を構築する（このフレームでまだなら）。
    ///
    /// **スキニングのBLASを作り直した後に呼ぶこと。** 先に組むと TLAS が持つ
    /// 大まかな範囲が前のポーズのままになり、動いたキャラの一部が影から欠ける。
    /// レイを飛ばす側が使う直前に呼べばよく、2回目以降は何もしない
    /// </summary>
    void EnsureTlas();

    /// <summary>
    /// TLAS のGPUアドレス。
    /// 加速構造のSRVは「リソースではなくアドレス」を指すので、
    /// 使う側はこのアドレスから自前でSRVを作る
    /// </summary>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: 未構築なら0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetTlasGpuAddress() const
    {
        return tlasResult_ ? tlasResult_->GetGPUVirtualAddress() : 0;
    }

    /// <summary>
    /// このフレームに TLAS を組めたか（インスタンスが0件だと組めない）
    /// </summary>
    /// <returns>bool: 組めていれば true</returns>
    bool HasValidTlas() const { return tlasBuilt_; }

    /// <summary>
    /// 統計: このフレームに積んだインスタンス数
    /// </summary>
    uint32_t GetInstanceCount() const { return static_cast<uint32_t>(instances_.size()); }

    /// <summary>
    /// モデルの実体が捨てられる直前に呼ぶ。そのモデルのBLASをキャッシュから外す。
    ///
    /// BLASは Model* をキーに覚えている。実体を捨てたあと、後から作られた別のモデルが
    /// 同じアドレスに乗ることがあり、そのとき古い形のBLASが引き当てられてしまう
    /// （影や反射だけ前のモデルの形になる）。時間切れの追い出し（EvictUnusedBlas）
    /// だけでは、アドレスが使い回されるほうが早い場合に間に合わない
    /// </summary>
    /// <param name="pModel">これから捨てるモデル</param>
    void OnModelDestroyed(const Model *pModel);

    /// <summary>
    /// 統計: 作成済みのBLAS数（＝形の種類）
    /// </summary>
    uint32_t GetBlasCount() const { return static_cast<uint32_t>(blasCache_.size()); }

    /// <summary>
    /// 統計: このフレームに作り直したスキニングのBLAS数
    /// </summary>
    uint32_t GetSkinnedBlasCount() const { return skinnedBuiltThisFrame_; }

  private:
    RaytracingScene() = default;
    ~RaytracingScene() = default;
    RaytracingScene(const RaytracingScene &) = delete;
    RaytracingScene &operator=(const RaytracingScene &) = delete;

    /// <summary>
    /// 加速構造を置くためのバッファを作る（UAV可・専用の初期状態）
    /// </summary>
    /// <param name="sizeInBytes">必要なバイト数</param>
    /// <param name="initialState">初期のリソース状態</param>
    /// <returns>作成したバッファ</returns>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(UINT64 sizeInBytes,
                                                        D3D12_RESOURCE_STATES initialState);

    /// <summary>
    /// モデルからBLASを作る（作成済みならそれを返す）
    /// </summary>
    /// <param name="pModel">対象のモデル</param>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: BLASのアドレス。作れなければ0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS AcquireBlas(Model *pModel);

    /// <summary>
    /// スキニングで動くモデルのBLASを確保する（形は作らず器だけ用意する）。
    /// 実際の構築はスキニングの直後に BuildSkinnedBlas が行う
    /// </summary>
    /// <param name="pModel">対象のモデル</param>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: BLASのアドレス。用意できなければ0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS AcquireSkinnedBlas(Model *pModel);

    /// <summary>
    /// 積んだインスタンスから TLAS を組む（EnsureTlas の本体）
    /// </summary>
    void BuildTlas();

    /// <summary>
    /// しばらく積まれていないBLASを捨てる。
    /// シーンを作り直すとモデルごと消えるので、置きっぱなしにしない
    /// </summary>
    void EvictUnusedBlas();

    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;   // DirectX共通処理
    SrvManager *pSrvManager_ = nullptr;    // SRVマネージャー
    bool available_ = false;               // レイトレーシングが使えるか
    /// <summary>BLAS 1つぶん</summary>
    struct Blas
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> result;  // 加速構造の本体
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch; // 構築中の作業領域
        uint64_t lastUsedFrame = 0;                     // 最後に積まれたフレーム番号
    };
    // モデル → BLAS。同じモデルを何体置いても形は1つで済む
    std::unordered_map<Model *, Blas> blasCache_;

    /// <summary>スキニングで動くモデルのBLAS 1つぶん（毎フレーム作り直す）</summary>
    struct SkinnedBlas
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> result;  // 加速構造の本体
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch; // 構築中の作業領域
        // このフレームの入力。スキニング結果のバッファを直に指すので毎フレーム作り直す
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        ID3D12Resource *pVertexResource = nullptr; // 状態を遷移させる出力頂点バッファ
        bool needsBuild = false;                   // このフレームまだ構築していない
        bool builtOnce = false;                    // 一度でも構築できたか
        uint64_t lastUsedFrame = 0;                // 最後に積まれたフレーム番号
    };
    // モデル → BLAS。こちらは形が毎フレーム変わるので、体ごとに1つ要る
    std::unordered_map<Model *, SkinnedBlas> skinnedBlasCache_;

    /// <summary>このフレームに積まれたオブジェクト1つぶん</summary>
    struct Submission
    {
        D3D12_RAYTRACING_INSTANCE_DESC desc{}; // TLAS へ入れる記述
        Model *pModel = nullptr;               // 積んだモデル
        bool skinned = false;                  // スキニングで動くか
    };
    // 積まれた順の一覧。TLAS を組むときに「形が出来ている物」だけ instances_ へ移す
    std::vector<Submission> submissions_;

    // 実際に TLAS へ入れたインスタンス一覧（GPUへ転送する中身そのもの）
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances_;

    Microsoft::WRL::ComPtr<ID3D12Resource> tlasResult_;    // TLAS本体
    Microsoft::WRL::ComPtr<ID3D12Resource> tlasScratch_;   // TLAS構築の作業領域
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer_; // インスタンス記述の転送先
    UINT64 instanceBufferCapacity_ = 0;                    // instanceBuffer_ が入る件数

    bool tlasBuilt_ = false;              // このフレームにTLASを組めたか
    bool tlasAttemptedThisFrame_ = false; // このフレームに EnsureTlas を通したか
    bool active_ = false;                 // このフレームは加速構造を作るか
    bool useRequested_ = false;           // このフレームに RequestUse があったか
    uint64_t frameIndex_ = 0;             // BeginFrame ごとに進むフレーム番号
    uint32_t skinnedBuiltThisFrame_ = 0;  // このフレームに作り直したスキニングBLAS数
};
} // namespace Hagine
