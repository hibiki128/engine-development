#pragma once
#ifdef USE_IMGUI
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// エンジンの HLSL をエディタ上で読み書きする窓。
///
/// これまでシェーダーを直すには Visual Studio 側でファイルを開き直す必要があったが、
/// 「今どのシェーダーがどう書かれているか」を確認するだけでも往復が要って面倒だった。
/// ここでは shaders/ 配下を一覧して、構文色付き（ImGuiColorTextEdit）で表示・編集する。
///
/// 注意: シェーダーは起動時にコンパイルされるので、保存しても反映は次回起動から。
/// </summary>
class ShaderEditorWindow {
  public:
    /// <summary>インスタンスを取得</summary>
    static ShaderEditorWindow *GetInstance();

    /// <summary>窓を描画する</summary>
    /// <param name="open">表示フラグ（閉じるボタンで false になる）</param>
    void Draw(bool *open);

  private:
    ShaderEditorWindow() = default;
    ~ShaderEditorWindow() = default;
    ShaderEditorWindow(const ShaderEditorWindow &) = delete;
    ShaderEditorWindow &operator=(const ShaderEditorWindow &) = delete;

    /// <summary>shaders/ 配下の .hlsl を集め直す</summary>
    void RefreshFileList();

    /// <summary>ファイルを読み込んでエディタへ流し込む</summary>
    /// <param name="relativePath">shaders/ からの相対パス</param>
    void LoadFile(const std::string &relativePath);

    /// <summary>編集内容をファイルへ書き戻す</summary>
    /// <returns>bool: 書けたら true</returns>
    bool SaveCurrentFile();

    /// <summary>左ペイン（ファイル一覧）を描く</summary>
    void DrawFileList();

    /// <summary>右ペイン（エディタ本体）を描く</summary>
    void DrawEditor();

    // shaders/ からの相対パス一覧（Deferred/GBuffer.PS.hlsl のような形）
    std::vector<std::string> files_;
    std::string filter_;             // 一覧の絞り込み
    std::string currentFile_;        // 開いているファイル（相対パス）
    std::string statusMessage_;      // 保存結果などの一言
    bool initialized_ = false;       // 一覧の初回取得を済ませたか
    bool dirty_ = false;             // 未保存の変更があるか
};

} // namespace Hagine
#endif // USE_IMGUI
