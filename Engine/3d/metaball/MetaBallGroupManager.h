#pragma once
#include "MetaBall.h"
#include "camera/projection/ViewProjection.h"
#include "object/Object3d.h"
#include "transform/WorldTransform.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {
class MetaBallObject;

/// <summary>
/// グループ 1 つ分の生成設定。グループに属する全オブジェクトで共有される。
/// </summary>
struct MetaBallGroupSettings
{
    // セル 1 辺の長さ（ワールド単位）。小さいほど滑らかで重い。
    // 弾の半径のおよそ 1/6 〜 1/10 が目安
    float voxelSize = 0.15f;
    float threshold = 0.5f;                        // 等値面のしきい値
    float uvScale = 1.0f;                          // 平面投影 UV のスケール
    std::string texturePath = "debug/uvChecker.png"; // 貼るテクスチャ
    bool enabled = true;                           // 描画するか
};

/// <summary>
/// メタボールの「場」をグループ単位で持ち、融合した表面を 1 つのメッシュとして描く。
///
/// MetaBallObject は自分ではメッシュを持たず、ワールド空間の要素をこのマネージャに
/// 提供する「発生源」として振る舞う。同じグループ名のオブジェクト同士は密度が
/// 足し合わされるので、近づけると自然にくっつく。
/// 弾のように 1 個ずつのオブジェクトとして扱いたい用途を想定している。
///
/// 離れた要素は別々のかたまりとして切られる（MetaBallBuilder::BuildClustered）ので、
/// 弾が世界中に散らばっても格子が world 全体に広がることはない。
/// </summary>
class MetaBallGroupManager
{
  private:
    MetaBallGroupManager() = default;
    ~MetaBallGroupManager() = default;
    MetaBallGroupManager(MetaBallGroupManager &) = delete;
    MetaBallGroupManager &operator=(MetaBallGroupManager &) = delete;

  public:
    /// <summary>シングルトンインスタンスの取得</summary>
    static MetaBallGroupManager *GetInstance()
    {
        static MetaBallGroupManager instance;
        return &instance;
    }

    /// <summary>終了処理（保持しているモデルを解放する）</summary>
    void Finalize();

    /// <summary>オブジェクトをグループに登録する</summary>
    void Register(MetaBallObject *object);

    /// <summary>オブジェクトの登録を解除する</summary>
    void Unregister(MetaBallObject *object);

    /// <summary>
    /// グループ名が変わったときに呼ぶ（登録し直す）
    /// </summary>
    void Rebind(MetaBallObject *object, const std::string &oldGroupName);

    /// <summary>
    /// 全グループの要素を集め直し、変化していればメッシュを作り直す。
    /// BaseObjectManager::Update のあとに呼ぶこと（ワールド行列が確定している必要がある）。
    /// </summary>
    void Update();

    /// <summary>融合した表面を描画する</summary>
    void Draw(const ViewProjection &viewProjection);

    /// <summary>グループの設定を取得する（無ければ作る）</summary>
    MetaBallGroupSettings &GetSettings(const std::string &groupName);

    /// <summary>直近の生成結果を取得する</summary>
    const MetaBallBuildStats &GetStats(const std::string &groupName);

    /// <summary>グループに属するオブジェクトの数</summary>
    size_t GetMemberCount(const std::string &groupName);

    /// <summary>次の Update で必ず作り直す（設定を変えたときに呼ぶ）</summary>
    void MarkDirty(const std::string &groupName);

    /// <summary>
    /// settings.texturePath をグループのマテリアルへ反映する。
    /// 融合表面はグループが 1 枚のメッシュで描くので、テクスチャもグループ単位になる。
    /// </summary>
    void ApplyTexture(const std::string &groupName);

  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    struct Group
    {
        MetaBallGroupSettings settings{};        // 生成設定
        std::vector<MetaBallObject *> members{}; // 寄与するオブジェクト
        std::unique_ptr<Object3d> obj3d{};       // 融合結果を描くためのモデル
        std::unique_ptr<WorldTransform> transform{}; // 単位行列（メッシュがワールド空間なので）
        MetaBallBuildStats stats{};              // 直近の生成結果
        size_t lastHash = 0;                     // 前回の要素の状態
        bool hasMesh = false;                    // 描けるメッシュがあるか
        bool forceRebuild = true;                // 次の Update で必ず作り直すか
    };

    /// <summary>グループを取得する（無ければ作る）</summary>
    Group &GetOrCreateGroup(const std::string &groupName);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::map<std::string, Group> groups_{};
    // 毎フレームの一時バッファ。確保を使い回して割り当てを避ける
    std::vector<MetaBallElement> scratch_{};
    MetaBallBuildStats emptyStats_{};
};

} // namespace Hagine
