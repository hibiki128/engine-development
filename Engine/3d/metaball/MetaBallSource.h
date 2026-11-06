#pragma once
#include "MetaBall.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {
class BaseObject;

/// <summary>
/// メタボールであることを示すモデル名。シーンの JSON に "modelPath" として書かれ、
/// 読み込み時に BaseObjectManager がこれを見てメタボールとして作り直す。
/// </summary>
inline constexpr const char *kMetaBallModelTag = "MetaBall";

/// <summary>
/// グループ名を付けていないメタボールが入る共通グループ。名前なしの物同士はすべて融合する
/// </summary>
inline constexpr const char *kMetaBallDefaultGroup = "Default";

/// <summary>
/// BaseObject をメタボールの発生源にする部品（BaseObject::CreateMetaBallModel で付く）。
/// 要素をワールド空間に置いて MetaBallGroupManager に渡し、同じグループの密度を足し合わせた表面が 1 つのメッシュとして描かれる。
/// </summary>
class MetaBallSource
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 球 1 個を持った状態でグループに登録する
    /// </summary>
    /// <param name="pOwner">持ち主（ワールド行列と名前を使う）</param>
    /// <param name="groupName">融合するグループ名（空なら名前なしの共通グループ）</param>
    /// <param name="loadSavedData">保存済みの要素とグループ設定を読むか（コードで作る物のとき true）</param>
    MetaBallSource(BaseObject *pOwner, const std::string &groupName, bool loadSavedData);

    /// <summary>
    /// グループから登録を解除する
    /// </summary>
    ~MetaBallSource();

    MetaBallSource(const MetaBallSource &) = delete;
    MetaBallSource &operator=(const MetaBallSource &) = delete;

    /// <summary>
    /// 自分の要素をワールド空間に変換して out の末尾に足す（グループマネージャが毎フレーム呼び出し）
    /// </summary>
    /// <param name="out">追記先</param>
    void AppendWorldElements(std::vector<MetaBallElement> &out) const;

    /// <summary>
    /// 複製元の要素を写し、名前なしならそのまま共通グループへ、名前付きなら見た目の設定だけ写した独立グループにする
    /// </summary>
    /// <param name="source">複製元</param>
    void CopyFrom(const MetaBallSource &source);

    /// <summary>
    /// インスペクタの「メタボール」タブの中身を描画する
    /// </summary>
    void DrawImGui();

    /// ===================================================
    /// 要素の操作
    /// ===================================================

    /// <summary>
    /// 要素を追加する
    /// </summary>
    /// <returns>size_t: 追加された要素の添字</returns>
    size_t AddElement(const MetaBallElement &element);

    /// <summary>
    /// ローカル座標に球を足す
    /// </summary>
    /// <returns>size_t: 追加された要素の添字</returns>
    size_t AddBall(const Vector3 &localPosition, float radius = 1.0f);

    /// <summary>
    /// 要素を削除する
    /// </summary>
    void RemoveElement(size_t index);

    /// <summary>
    /// 全要素を消す
    /// </summary>
    void ClearElements();

    /// <summary>
    /// 全要素の影響半径をまとめて設定する
    /// </summary>
    void SetRadius(float radius);

    /// <summary>
    /// 全要素の強さをまとめて設定する
    /// </summary>
    void SetStiffness(float stiffness);

    /// <summary>
    /// 全要素の軸ごとの半径倍率をまとめて設定する（{1,1,1} で真球、それ以外は楕円体）
    /// </summary>
    void SetRadiusScale(const Vector3 &radiusScale);

    /// ===================================================
    /// グループ
    /// ===================================================

    /// <summary>
    /// 融合するグループを変える（同じ名前の物同士だけがくっつき、空なら名前なしの物すべてとくっつく）
    /// </summary>
    void SetGroupName(const std::string &groupName);

    /// <summary>
    /// グループ名を付けていない（名前なしの共通グループにいる）か
    /// </summary>
    bool IsUngrouped() const { return groupName_ == kMetaBallDefaultGroup; }

    /// ===================================================
    /// 保存・読み込み
    /// ===================================================

    /// <summary>
    /// シーンファイル・Undo 用の状態（グループ名・要素・グループ設定）を JSON にする
    /// </summary>
    /// <returns>nlohmann::json: BaseObject::Serialize の "metaBall" に入る値</returns>
    nlohmann::json Serialize() const;

    /// <summary>
    /// Serialize の結果を戻す（書いていないキーは今の値のまま）
    /// </summary>
    void Deserialize(const nlohmann::json &state);

    /// <summary>
    /// 要素を jsons/MetaBallDatas/保存名.json、グループ設定を jsons/MetaBallGroups/グループ名.json へ保存する
    /// </summary>
    /// <param name="dataName">持ち主の保存名</param>
    void SaveToFile(const std::string &dataName) const;

    /// ===================================================
    /// Getter / Setter
    /// ===================================================

    std::vector<MetaBallElement> &GetElements() { return elements_; }
    const std::vector<MetaBallElement> &GetElements() const { return elements_; }
    const std::string &GetGroupName() const { return groupName_; }
    int GetSelectedElementIndex() const { return selectedElement_; }
    void SetSelectedElementIndex(int index) { selectedElement_ = index; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// jsons/MetaBallDatas/保存名.json から要素を読む（無ければ今の要素のまま）
    /// </summary>
    void LoadElements(const std::string &dataName);

    /// <summary>
    /// jsons/MetaBallGroups/グループ名.json から今のグループの設定を読んで反映する
    /// </summary>
    void LoadGroupSettings();

    /// ===================================================
    /// private variables
    /// ===================================================

    BaseObject *pOwner_ = nullptr;                  // 持ち主
    std::vector<MetaBallElement> elements_{};       // ローカル空間の要素
    std::string groupName_ = kMetaBallDefaultGroup; // 融合するグループ
    int selectedElement_ = 0;                       // インスペクタで選択中の要素
    bool loadSavedData_ = false;                    // 初めて入るグループの保存済み設定を読むか
};

} // namespace Hagine
