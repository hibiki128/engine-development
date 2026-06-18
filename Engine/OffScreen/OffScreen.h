#pragma once
#include "PostEffect/PostEffectChain.h"
#include "PostEffect/PostEffectDataManager.h"
#include "PostEffect/PostEffectRenderer.h"
#include <string>
#include <type/Matrix4x4.h>

namespace Hagine {

/// <summary>
/// ポストエフェクト全体を管理するファサードクラス
///
/// 使用例:
///   offScreen_.AddEffect(ShaderMode::kVignette, "ビネット");
///   offScreen_.AddEffect(ShaderMode::kBloom,    "ブルーム", 5); // スロット5に配置
///
///   // パラメータを直接取得して変更
///   if (auto* p = offScreen_.GetEffectParams&lt;VignetteParams&gt;(slotIndex)) {
///       p->GetData().strength = 2.0f;
///   }
/// </summary>
class OffScreen {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 描画（バックバッファへのコピーまで含む）
    /// </summary>
    void Draw();

    /// <summary>
    /// エフェクト適用のみ（マルチステージ・フルフレーム合成用）
    /// </summary>
    void DrawWithoutCopy();

    /// <summary>
    /// finalResultへのUI合成を開始
    /// </summary>
    void BeginCompositePass();

    /// <summary>
    /// UI合成を終了
    /// </summary>
    void EndCompositePass();

    /// <summary>
    /// 前ステージの結果をオフスクリーンへ転送
    /// </summary>
    /// <param name="prevFinalResultSrvIndex">前ステージ結果のSRVインデックス</param>
    void BlitToOffScreen(uint32_t prevFinalResultSrvIndex);

    /// <summary>
    /// ImGuiでのポストエフェクト設定UIを表示
    /// </summary>
    void Setting();

    /// <summary>
    /// 投影行列を設定
    /// </summary>
    /// <param name="projectionMatrix">投影行列</param>
    void SetProjection(Matrix4x4 projectionMatrix);

    /// <summary>
    /// 最終結果のSRVインデックスを取得
    /// </summary>
    /// <returns>uint32_t: 最終結果のSRVインデックス</returns>
    uint32_t GetFinalResultSrvIndex() const;

    /// <summary>
    /// 最終結果をバックバッファへコピー
    /// </summary>
    void CopyFinalResultToBackBuffer();

    /// ===================================================
    /// エフェクト管理API
    /// ===================================================

    /// <summary>
    /// エフェクトを追加する
    /// </summary>
    /// <param name="mode">シェーダーモード</param>
    /// <param name="name">識別名（省略可）</param>
    /// <param name="slotIndex">配置スロット（-1で自動）</param>
    /// <returns>int: 実際に配置されたスロット番号。失敗時 -1</returns>
    int AddEffect(ShaderMode mode,
                  const std::string &name = "",
                  int slotIndex = -1);

    /// <summary>
    /// スロット番号を指定してエフェクトを削除する
    /// </summary>
    /// <param name="slotIndex">削除するスロット番号</param>
    /// <returns>bool: 削除に成功すれば true</returns>
    bool RemoveEffect(int slotIndex);

    /// <summary>
    /// 名前で検索して最初にヒットした1つを削除する
    /// </summary>
    /// <param name="name">削除するエフェクト名</param>
    /// <returns>int: 削除されたスロット番号。見つからなければ -1</returns>
    int RemoveEffectByName(const std::string &name);

    /// <summary>
    /// 同名のエフェクトをすべて削除する
    /// </summary>
    /// <param name="name">削除するエフェクト名</param>
    /// <returns>int: 削除した数</returns>
    int RemoveAllEffectsByName(const std::string &name);

    /// <summary>
    /// エフェクトの有効/無効を切り替える
    /// </summary>
    /// <param name="slotIndex">対象スロット番号</param>
    /// <param name="enabled">有効にするなら true</param>
    /// <returns>bool: 切り替えに成功すれば true</returns>
    bool SetEffectEnabled(int slotIndex, bool enabled);

    /// <summary>
    /// エフェクトを上に移動する（描画順変更）
    /// </summary>
    /// <param name="slotIndex">対象スロット番号</param>
    /// <returns>bool: 移動に成功すれば true</returns>
    bool MoveEffectUp(int slotIndex);

    /// <summary>
    /// エフェクトを下に移動する（描画順変更）
    /// </summary>
    /// <param name="slotIndex">対象スロット番号</param>
    /// <returns>bool: 移動に成功すれば true</returns>
    bool MoveEffectDown(int slotIndex);

    /// <summary>
    /// 設定データを読み込む
    /// </summary>
    /// <param name="fileName">読み込むファイル名</param>
    void LoadData(const std::string& fileName);

    /// <summary>
    /// 指定スロットのパラメータを型付きで取得する
    /// </summary>
    /// <typeparam name="T">具体的なパラメータ型（例: VignetteParams）</typeparam>
    /// <param name="slotIndex">対象スロット番号</param>
    /// <returns>T*: パラメータへのポインタ。型不一致またはスロット未使用時は nullptr</returns>
    template <typename T>
    T *GetEffectParams(int slotIndex) {
        return effectChain_.GetParams<T>(slotIndex);
    }

    /// <summary>
    /// 名前からスロット番号を取得する
    /// </summary>
    /// <param name="name">エフェクト名</param>
    /// <returns>int: スロット番号。見つからなければ -1</returns>
    int FindEffectSlotByName(const std::string &name) {
        return effectChain_.FindSlotByName(name);
    }

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    PostEffectChain effectChain_;       // エフェクトの連結チェーン
    PostEffectRenderer renderer_;       // エフェクト描画
    PostEffectDataManager dataManager_; // エフェクト設定の保存/読み込み

    DirectXCommon *dxCommon_ = nullptr; // DirectX共通処理
    Matrix4x4 projectionMatrix_;        // 投影行列

    // セーブ/ロード結果メッセージとその表示タイマー
    std::string saveMessage_;     // 保存結果メッセージ
    int saveMessageTimer_ = 0;    // メッセージ表示タイマー
};
} // namespace Hagine
