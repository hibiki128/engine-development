#pragma once
#include "MetaBall.h"
#include "object/base/BaseObject.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// メタボールであることを示すモデル名。シーンの JSON に "modelName" として書かれ、
/// 読み込み時に BaseObjectManager がこれを見て MetaBallObject を作り直す。
/// </summary>
inline constexpr const char *kMetaBallModelTag = "MetaBall";

/// <summary>既定のグループ名。同じグループ名同士が融合する</summary>
inline constexpr const char *kMetaBallDefaultGroup = "Default";

/// <summary>
/// メタボールの「発生源」オブジェクト。
///
/// このオブジェクト自身はメッシュを持たない。要素をワールド空間に置いて
/// MetaBallGroupManager に渡し、同じグループ名のオブジェクトすべての密度を
/// 足し合わせた表面が 1 つのメッシュとして描かれる。
/// つまり **オブジェクトを近づけると勝手にくっつく**。
///
/// 弾 1 発 = このオブジェクト 1 個、という使い方を想定している。
/// 既定では球 1 個だけを持つので、生成してそのまま飛ばせばよい。
///
/// 離れた要素は別のかたまりとして切られるので、弾が世界中に散らばっても
/// 格子がワールド全体に広がって潰れることはない。
/// </summary>
class MetaBallObject : public BaseObject
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    ~MetaBallObject() override;

    /// <summary>
    /// 初期化。球 1 個を持った状態でグループに登録される。
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void Init(const std::string objectName) override;

    /// <summary>
    /// インスペクタのメタボール専用タブの中身。
    /// タブ自体が枠になるので、ここで折りたたみヘッダーは作らない。
    /// </summary>
    void DrawImGuiExtension() override;

    /// <summary>インスペクタに「メタボール」タブを出す</summary>
    const char *GetImGuiExtensionName() const override { return "メタボール"; }

    /// <summary>複製元の要素リストとグループ名も写す</summary>
    void CopyPropertiesFrom(const BaseObject &source) override;

    /// <summary>
    /// 自分の要素をワールド空間に変換して out の末尾に足す。
    /// グループマネージャが毎フレーム呼ぶ。
    /// </summary>
    /// <param name="out">追記先</param>
    void AppendWorldElements(std::vector<MetaBallElement> &out) const;

    /// ===================================================
    /// 要素の操作
    /// ===================================================

    /// <summary>要素を追加する</summary>
    /// <returns>size_t: 追加された要素の添字</returns>
    size_t AddElement(const MetaBallElement &element);

    /// <summary>ローカル座標に球を足す</summary>
    size_t AddBall(const Vector3 &localPosition, float radius = 1.0f);

    /// <summary>要素を削除する</summary>
    void RemoveElement(size_t index);

    /// <summary>全要素を消す</summary>
    void ClearElements();

    /// ===================================================
    /// Getter / Setter
    /// ===================================================

    std::vector<MetaBallElement> &GetElements() { return elements_; }
    const std::vector<MetaBallElement> &GetElements() const { return elements_; }
    const std::string &GetGroupName() const { return groupName_; }
    int GetSelectedElementIndex() const { return selectedElement_; }

    /// <summary>
    /// 融合するグループを変える。同じ名前のオブジェクト同士だけがくっつく。
    /// </summary>
    void SetGroupName(const std::string &groupName);

    void SetSelectedElementIndex(int index) { selectedElement_ = index; }

    /// <summary>
    /// 要素 1 個だけを持つ単純なメタボール（弾など）の半径をまとめて設定する
    /// </summary>
    void SetRadius(float radius);

    /// <summary>要素 1 個だけを持つメタボールの強さを設定する</summary>
    void SetStiffness(float stiffness);

    /// ===================================================
    /// シリアライズ（メッシュではなく要素リストを保存する）
    /// ===================================================

    /// <summary>オブジェクト単体の保存に要素リストを追加する</summary>
    void SaveToJson() override;

    /// <summary>シーン保存に要素リストを追加する</summary>
    void SceneSaveToJson() override;

    /// <summary>読み込み後に要素リストを復元する</summary>
    void LoadFromJson() override;

    /// <summary>要素リストだけを保存する</summary>
    void SaveMetaBallToJson();

    /// <summary>要素リストだけを読み込む</summary>
    void LoadMetaBallFromJson();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::vector<MetaBallElement> elements_{};        // ローカル空間の要素
    std::string groupName_ = kMetaBallDefaultGroup;  // 融合するグループ
    int selectedElement_ = 0;                        // インスペクタで選択中の要素
};

} // namespace Hagine
