#pragma once
#include "Windows.h"
#include <functional>
#include <string>
#include <unordered_map>
namespace Hagine {
class Input;

/// <summary>
/// キーボードショートカットを登録・実行するシングルトン
/// 単一キー・複数キー同時押しのコマンドに対応する
/// </summary>
class ShortcutManager {
  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ShortcutManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ShortcutManager() = default;
    ShortcutManager(ShortcutManager &) = delete;
    ShortcutManager &operator=(ShortcutManager &) = delete;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ShortcutManager*: シングルトンインスタンス</returns>
    static ShortcutManager *GetInstance() {
        static ShortcutManager instance;
        return &instance;
    }

    /// <summary>
    /// 単一キーのショートカットを登録
    /// </summary>
    /// <param name="name">ショートカット名</param>
    /// <param name="key">トリガーとなるキー</param>
    /// <param name="callback">実行するコールバック</param>
    void RegisterShortcut(const std::string &name, BYTE key, std::function<void()> callback);

    /// <summary>
    /// 複数キー同時押しのショートカットを登録
    /// </summary>
    /// <param name="name">ショートカット名</param>
    /// <param name="keys">トリガーとなるキーの組み合わせ</param>
    /// <param name="callback">実行するコールバック</param>
    void RegisterShortcut(const std::string &name, const std::vector<BYTE> &keys, std::function<void()> callback);

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="input">入力クラス</param>
    void Initialize(Input *input);

    /// <summary>
    /// 毎フレーム更新処理（Inputからトリガーを取得してコマンド実行）
    /// </summary>
    void Update();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

  private:
    /// <summary>
    /// ショートカット情報（拡張性を持たせるための構造体）
    /// </summary>
    struct Shortcut {
        std::vector<BYTE> keys;        // トリガーとなるキーの組み合わせ
        std::function<void()> callback; // 実行するコールバック
    };

    /// ===================================================
    /// private variants
    /// ===================================================

    std::unordered_map<std::string, Shortcut> shortcuts_; // 登録済みショートカット

    Input *input_ = nullptr; // 入力クラス
};
} // namespace Hagine
