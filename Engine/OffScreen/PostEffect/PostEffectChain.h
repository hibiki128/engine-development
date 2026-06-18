#pragma once
#include "IPostEffectParams.h"
#include "PostEffectParamsFactory.h"
#include <array>
#include <string>
#include <optional>

/// @brief ポストエフェクトの1スロット分のデータ
namespace Hagine {
struct EffectSlot {
    bool occupied = false;   ///< このスロットが使用中かどうか
    bool enabled  = false;   ///< エフェクトが有効かどうか
    std::string name;        ///< 識別用の名前（任意）
    std::unique_ptr<IPostEffectParams> params; ///< エフェクト固有パラメータ（所有権あり）
};

/// @brief スロットベースのポストエフェクトチェーン
///
/// kMaxSlots個のスロットをあらかじめ確保し、AddEffectでスロットを占有する。
/// 描画順 = スロットインデックス順（0が最初に適用される）
class PostEffectChain {
  public:
    /// スロットの最大数（空枠として事前確保される）
    static constexpr int kMaxSlots = 10;

    // -------------------------------------------------------
    //  追加・削除
    // -------------------------------------------------------

    /// @brief エフェクトを追加する
    /// @param mode        シェーダーモード
    /// @param name        識別用の名前
    /// @param dxCommon    GPUバッファ生成のためのDirectXCommon
    /// @param slotIndex   追加先スロット番号(-1で空きスロットに自動配置)
    /// @return 実際に配置されたスロット番号。失敗時は-1
    int AddEffect(ShaderMode mode,
                  const std::string &name,
                  DirectXCommon *dxCommon,
                  int slotIndex = -1);

    /// @brief スロット番号を指定して削除する
    /// @return 成功したかどうか
    bool RemoveEffect(int slotIndex);

    /// @brief 名前で検索して最初にヒットした1つを削除する
    /// @return 削除されたスロット番号。見つからなければ-1
    int RemoveEffectByName(const std::string &name);

    /// @brief 同名のエフェクトをすべて削除する
    /// @return 削除した数
    int RemoveAllEffectsByName(const std::string &name);

    /// @brief 全スロットをクリアする
    void Clear(DirectXCommon *dxCommon = nullptr);

    // -------------------------------------------------------
    //  スロット操作
    // -------------------------------------------------------

    /// @brief エフェクトの有効/無効を設定する
    bool SetEnabled(int slotIndex, bool enabled);

    /// @brief エフェクトの名前を変更する
    bool SetName(int slotIndex, const std::string &name);

    /// @brief 2つのスロットの内容を入れ替える（描画順の変更に使用）
    bool SwapSlots(int slotA, int slotB);

    /// @brief スロットを1つ上（小さいインデックス方向）に移動する
    bool MoveUp(int slotIndex);

    /// @brief スロットを1つ下（大きいインデックス方向）に移動する
    bool MoveDown(int slotIndex);

    // -------------------------------------------------------
    //  参照・クエリ
    // -------------------------------------------------------

    /// @brief 使用中のスロットかどうか
    bool IsOccupied(int slotIndex) const;

    /// @brief 指定スロットのパラメータを取得する（型キャスト版）
    template <typename T>
    T *GetParams(int slotIndex) {
        if (!IsValidIndex(slotIndex) || !slots_[slotIndex].occupied) { return nullptr; }
        return dynamic_cast<T *>(slots_[slotIndex].params.get());
    }

    /// @brief 指定スロットのパラメータを取得する（基底クラス版）
    IPostEffectParams *GetParams(int slotIndex);

    /// @brief 有効エフェクトが存在するスロットのインデックスリストを取得する
    std::vector<int> GetEnabledSlotIndices() const;

    /// @brief 全スロット（const参照）
    const std::array<EffectSlot, kMaxSlots> &GetSlots() const { return slots_; }

    /// @brief 有効なエフェクトが1つでもあるか
    bool HasEnabledEffects() const;

    /// @brief 使用中スロットが0個か
    bool IsEmpty() const;

    /// @brief 空きスロットの数
    int GetFreeSlotCount() const;

    /// @brief 名前でスロット番号を検索する（最初のヒット）
    /// @return スロット番号。見つからなければ-1
    int FindSlotByName(const std::string &name) const;

    /// @brief 名前でエフェクトの有効/無効を設定する（最初のヒット）
    bool SetEnabledByName(const std::string &name, bool enabled);

  private:
    bool IsValidIndex(int index) const { return index >= 0 && index < kMaxSlots; }

    /// @brief 最初の空きスロットインデックスを返す（なければ-1）
    int FindFirstFreeSlot() const;

    std::array<EffectSlot, kMaxSlots> slots_;
};
} // namespace Hagine
