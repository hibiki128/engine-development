#pragma once
#include "camera/projection/ViewProjection.h"
#include "object/Object3d.h"
#include "type/Matrix4x4.h"
#include "type/Vector4.h"
#include <cstdint>
#include <d3d12.h>
#include <unordered_map>
#include <vector>
#include <wrl.h>

namespace Hagine {
class DirectXCommon;
class WorldTransform;

/// <summary>
/// GPUへ送るインスタンス1個ぶんのデータ
/// Object3dInstanced.VS.hlsl / ShadowMapInstanced.VS.hlsl の ObjectInstanceData と一致させること
/// </summary>
struct ObjectInstanceData
{
    Matrix4x4 wvp;                   // ワールドビュープロジェクション行列
    Matrix4x4 world;                 // ワールド行列
    Matrix4x4 worldInverseTranspose; // ワールド逆転置行列（法線用）
    Matrix4x4 lightWVP;              // ライト空間のWVP行列（シャドウ用）
    Vector4 color;                   // 個体ごとの色（マテリアル色に乗算される）
};
static_assert(sizeof(ObjectInstanceData) == 272, "ObjectInstanceData は272B。HLSL の ObjectInstanceData と一致させること");

/// <summary>
/// 同じモデルを参照するオブジェクトを1回の描画にまとめるインスタンシング描画のバッチャ
///
/// BaseObjectManager::Draw が Begin() 〜 Flush() で囲み、その間の BaseObject::Draw が
/// TrySubmit() で積む。まとめられなかったものは従来どおり1体ずつ描く。
/// 影パス / G-Buffer パス / 前方描画のどれで呼ばれたかは Flush() 時に判定するので、
/// 呼び出し側はパスを意識しなくてよい。
/// </summary>
class Object3dInstancing
{
  private:
    /// ===================================================
    /// private method
    /// ===================================================

    Object3dInstancing() = default;
    ~Object3dInstancing() = default;
    Object3dInstancing(Object3dInstancing &) = delete;
    Object3dInstancing &operator=(Object3dInstancing &) = delete;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>Object3dInstancing*: シングルトンインスタンス</returns>
    static Object3dInstancing *GetInstance()
    {
        static Object3dInstancing instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// フレーム先頭で1回だけ呼ぶ（インスタンスバッファの書き込み位置を先頭へ戻す）。
    /// 影パス / G-Buffer パス / 前方描画で内容が違うため、1フレーム内では同じ領域を
    /// 二度使わない（後のパスの書き込みで前のパスの描画内容が化けるのを防ぐ）。
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 収集開始（このパスぶんのバッチをリセットする）
    /// </summary>
    void Begin();

    /// <summary>
    /// 描画対象をバッチへ積む。積めなかった場合は呼び出し側が従来どおり描画すること。
    /// </summary>
    /// <param name="pObject3d">対象のObject3d</param>
    /// <param name="worldTransform">ワールド変換（描画用オフセット適用後の値）</param>
    /// <param name="viewProjection">ビュープロジェクション</param>
    /// <param name="reflect">反射有効フラグ</param>
    /// <param name="lighting">ライティング有効フラグ</param>
    /// <returns>bool: 積めたら true（呼び出し側は描画しない）</returns>
    bool TrySubmit(Object3d *pObject3d, const WorldTransform &worldTransform,
                   const ViewProjection &viewProjection, bool reflect, bool lighting);

    /// <summary>
    /// 積んだバッチをまとめて描画する
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void Flush(const ViewProjection &viewProjection);

    /// <summary>
    /// インスタンシングのON/OFF（デバッグ用。OFFにすると全て従来の1体ずつ描画になる）
    /// </summary>
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    /// <summary>直近フレームの統計を取得（ImGui表示用）</summary>
    uint32_t GetLastBatchCount() const { return lastBatchCount_; }
    uint32_t GetLastInstanceCount() const { return lastInstanceCount_; }
    uint32_t GetLastMergedDrawCount() const { return lastMergedDrawCount_; }

  private:
    /// ===================================================
    /// private types
    /// ===================================================

    /// <summary>同じ描画設定でまとめられるオブジェクト群</summary>
    struct Batch
    {
        Object3d *pRepresentative = nullptr; // バインドに使う代表オブジェクト
        bool reflect = false;
        bool lighting = true;
        std::vector<ObjectInstanceData> instances;
    };

    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>インスタンスバッファを必要な要素数まで確保し直す（永続マップ）</summary>
    /// <param name="requiredCount">必要な要素数</param>
    void EnsureInstanceBuffer(size_t requiredCount);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;

    // フレーム（パス）ごとに作り直すバッチ。キーは Object3d::ComputeBatchSignature の値。
    std::unordered_map<size_t, Batch> batches_;
    bool collecting_ = false;
    bool enabled_ = true;

    // 全バッチのインスタンスを連結して置く1本のアップロードバッファ。
    // バッチごとに「先頭要素のGPUアドレス」をルートSRVへ渡すのでディスクリプタは要らない。
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
    ObjectInstanceData *pInstanceData_ = nullptr;
    size_t instanceCapacity_ = 0;
    // このフレームの書き込み位置（パスをまたいで前進し、BeginFrame で 0 に戻す）
    size_t writeCursor_ = 0;
    // 確保し直した古いバッファ（GPU が参照中の可能性があるので即解放しない）
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredInstanceResources_;

    uint32_t lastBatchCount_ = 0;       // まとめて描いたバッチ数
    uint32_t lastInstanceCount_ = 0;    // まとめて描いたインスタンス総数
    uint32_t lastMergedDrawCount_ = 0;  // 減らせた描画コール数（インスタンス総数 - バッチ数）
};
} // namespace Hagine
