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

/// <summary>
/// Blender のメタボールに相当するオブジェクト。
///
/// 要素（球・カプセル）を並べると、その密度場の等値面が 1 つのなめらかな
/// メッシュとして生成される。要素が動いたフレームだけ作り直すので、
/// 触っていない間は普通のメッシュと同じコストで描かれる。
///
/// ドラッグ中は editResolution_、手を離したら params_.resolution で作り直す
/// 二段構えにしている（Blender のビューポート/レンダー解像度と同じ考え方）。
/// 素朴に本解像度で毎フレーム作ると実用解像度で 60fps を割るため、
/// これは最適化ではなく前提の設計。
/// </summary>
class MetaBallObject : public BaseObject
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    ~MetaBallObject() override;

    /// <summary>
    /// 初期化。動的モデルを作り、要素を 1 個置いた状態から始める。
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void Init(const std::string objectName) override;

    /// <summary>
    /// 更新。必要があればメッシュを作り直す。
    /// </summary>
    void Update() override;

    /// <summary>
    /// インスペクタ UI
    /// </summary>
    void DrawImGui() override;

    /// ===================================================
    /// 要素の操作
    /// ===================================================

    /// <summary>要素を追加する</summary>
    /// <returns>size_t: 追加された要素の添字</returns>
    size_t AddElement(const MetaBallElement &element);

    /// <summary>末尾に既定の球を足す</summary>
    size_t AddBall(const Vector3 &position, float radius = 1.0f);

    /// <summary>要素を削除する</summary>
    void RemoveElement(size_t index);

    /// <summary>全要素を消す</summary>
    void ClearElements();

    /// <summary>
    /// 要素を書き換えたら呼ぶ。次の Update でメッシュが作り直される。
    /// </summary>
    /// <param name="interactive">ドラッグ中なら true（低解像度で作り直す）</param>
    void MarkDirty(bool interactive = false);

    /// ===================================================
    /// Getter / Setter
    /// ===================================================

    std::vector<MetaBallElement> &GetElements() { return elements_; }
    const std::vector<MetaBallElement> &GetElements() const { return elements_; }
    const MetaBallBuildParams &GetBuildParams() const { return params_; }
    const MetaBallBuildStats &GetBuildStats() const { return stats_; }
    int GetSelectedElementIndex() const { return selectedElement_; }

    void SetResolution(uint32_t resolution);
    void SetEditResolution(uint32_t resolution);
    void SetThreshold(float threshold);
    void SetUvScale(float scale);
    void SetSelectedElementIndex(int index) { selectedElement_ = index; }

    /// ===================================================
    /// シリアライズ（メッシュではなく要素リストを保存する）
    /// ===================================================

    /// <summary>オブジェクト単体の保存に要素リストを追加する</summary>
    void SaveToJson() override;

    /// <summary>シーン保存に要素リストを追加する</summary>
    void SceneSaveToJson() override;

    /// <summary>読み込み後に要素リストを復元してメッシュを作り直す</summary>
    void LoadFromJson() override;

    /// <summary>要素リストだけを保存する</summary>
    void SaveMetaBallToJson();

    /// <summary>要素リストだけを読み込む</summary>
    void LoadMetaBallFromJson();

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>指定解像度でメッシュを作り直す</summary>
    void RebuildMesh(uint32_t resolution);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::vector<MetaBallElement> elements_{}; // メタボール要素
    MetaBallBuildParams params_{};            // 本解像度での生成パラメータ
    MetaBallBuildStats stats_{};              // 直近の生成結果

    uint32_t editResolution_ = 20; // ドラッグ中に使う低解像度
    int selectedElement_ = 0;      // インスペクタで選択中の要素

    bool isDirty_ = true;                // 次の Update で作り直すか
    bool isInteracting_ = false;         // いま UI をドラッグ中か（ImGui 側が毎フレーム更新）
    bool interactiveHint_ = false;       // MarkDirty(true) の 1 フレーム限りのヒント
    bool builtAtEditResolution_ = false; // 直近の生成が低解像度だったか
};

} // namespace Hagine
