#pragma once
#include "Camera.h"
#include "math/Easing.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {

/// <summary>
/// カメラを名前で管理するシングルトン
///
/// 使い方:
///   auto* manager = CameraManager::GetInstance();
///   Camera* main = manager->Create("メイン");   // 生成（所有はマネージャ）
///   main->SetPosition({0, 5, -20});
///   manager->SetActive("メイン");                // このカメラで描画される
///   manager->BlendTo("イベント", 1.5f);          // 1.5秒かけて別カメラへ寄せる（カメラワーク）
///
/// アクティブカメラ（ブレンド中はその補間結果）は SceneManager が毎フレーム
/// シーンの ViewProjection へ流し込むので、ゲーム側で行列をコピーする必要はない。
/// カメラを1つも作っていないシーンは従来どおり自前の ViewProjection がそのまま使われる。
/// </summary>
class CameraManager
{
  private:
    /// ===================================================
    /// private method
    /// ===================================================

    CameraManager() = default;
    ~CameraManager() = default;
    CameraManager(CameraManager &) = delete;
    CameraManager &operator=(CameraManager &) = delete;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>CameraManager*: シングルトンインスタンス</returns>
    static CameraManager *GetInstance()
    {
        static CameraManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理（全カメラを破棄する）
    /// </summary>
    void Finalize();

    /// <summary>
    /// カメラを生成して登録する。同じ名前が既にあればそれを返す。
    /// 最初に作ったカメラは自動的にアクティブになる。
    /// </summary>
    /// <param name="name">カメラ名</param>
    /// <returns>Camera*: 生成したカメラ（所有はマネージャ）</returns>
    Camera *Create(const std::string &name);

    /// <summary>
    /// 名前でカメラを取得する
    /// </summary>
    /// <param name="name">カメラ名</param>
    /// <returns>Camera*: 見つからなければ nullptr</returns>
    Camera *Find(const std::string &name);

    /// <summary>
    /// 名前を指定してカメラを破棄する
    /// </summary>
    /// <param name="name">カメラ名</param>
    void Remove(const std::string &name);

    /// <summary>
    /// 全カメラを破棄する（シーン遷移時に呼ばれる）
    /// </summary>
    void Clear();

    /// <summary>
    /// 描画に使うカメラを即座に切り替える
    /// </summary>
    /// <param name="name">カメラ名</param>
    void SetActive(const std::string &name);

    /// <summary>
    /// 描画に使うカメラを即座に切り替える
    /// </summary>
    /// <param name="pCamera">対象カメラ</param>
    void SetActive(Camera *pCamera);

    /// <summary>
    /// 今のカメラから指定カメラへ、時間をかけて滑らかに乗り換える（カメラワーク）
    /// </summary>
    /// <param name="name">切り替え先のカメラ名</param>
    /// <param name="duration">かける時間(秒)</param>
    /// <param name="easing">補間のしかた</param>
    void BlendTo(const std::string &name, float duration, EasingType easing = EasingType::InOutSine);

    /// <summary>
    /// 今のカメラから指定カメラへ、時間をかけて滑らかに乗り換える（カメラワーク）
    /// </summary>
    /// <param name="pCamera">切り替え先のカメラ</param>
    /// <param name="duration">かける時間(秒)</param>
    /// <param name="easing">補間のしかた</param>
    void BlendTo(Camera *pCamera, float duration, EasingType easing = EasingType::InOutSine);

    /// <summary>
    /// 全カメラの更新とカメラ切り替えの補間を進める（SceneManager が毎フレーム呼ぶ）
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiでカメラ一覧と選択中カメラの設定を表示する
    /// </summary>
    void DrawImGui();

    /// ===================================================
    /// Getter
    /// ===================================================

    /// <summary>今アクティブなカメラを取得する（未設定なら nullptr）</summary>
    Camera *GetActive() const { return pActiveCamera_; }

    /// <summary>
    /// 描画に使うビュープロジェクションを取得する。
    /// アクティブカメラ（切り替え補間中はその補間結果）の状態が毎フレーム書き込まれる
    /// 出力用カメラのものを返すので、**アドレスはカメラを切り替えても変わらない**
    /// （ポインタを持ち回す側が差し替えを気にしなくてよい）。カメラが1つも無ければ nullptr。
    /// </summary>
    /// <returns>const ViewProjection*: 描画に使うビュープロジェクション</returns>
    const ViewProjection *GetActiveViewProjection() const;

    /// <summary>カメラ切り替えの補間中か</summary>
    bool IsBlending() const { return blending_; }

    /// <summary>登録されているカメラ名の一覧を取得する</summary>
    std::vector<std::string> GetCameraNames() const;

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 出力用カメラへ「今描画に使うべき状態」を書き込む（アクティブ or 切り替え補間の結果）
    /// </summary>
    void RefreshOutput();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    // 名前 → カメラ（所有）
    std::unordered_map<std::string, std::unique_ptr<Camera>> cameras_;
    Camera *pActiveCamera_ = nullptr;

    // カメラ切り替えの補間
    bool blending_ = false;
    Camera *pBlendFromCamera_ = nullptr;             // 切り替え元
    Camera *pBlendToCamera_ = nullptr;               // 切り替え先
    float blendTime_ = 0.0f;                         // 経過時間
    float blendDuration_ = 0.0f;                     // 全体時間
    EasingType blendEasing_ = EasingType::InOutSine; // 補間のしかた
    // 補間結果を入れるだけの内部カメラ（登録簿には載せない）
    std::unique_ptr<Camera> blendedCamera_;
    // 描画へ渡す最終結果を持つ内部カメラ。アドレスを固定したいのでシーンを跨いでも作り直さない
    // （ViewProjection のポインタを持ち回している箇所が差し替えを気にしなくて済む）。
    std::unique_ptr<Camera> outputCamera_;
    // 切り替え開始時点の元カメラの状態（元カメラが動き続けても始点がぶれないように固定する）
    std::unique_ptr<Camera> blendFromSnapshot_;

    std::string selectedName_; // ImGui で選択中のカメラ名
};
} // namespace Hagine
