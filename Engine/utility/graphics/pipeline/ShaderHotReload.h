#pragma once
#include <string>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// HLSL を保存したら、その場でパイプラインを作り直す仕組み。
///
/// これまでシェーダーを1行直すたびに再起動が必要で、絵作りの試行回数がそこで削られていた。
/// shaders/ 配下の更新時刻を定期的に見て、変わっていたら作り直す。
///
/// **書きかけのHLSLを保存しても落ちないこと** を最優先にしてある:
/// パイプラインを捨てる前に、変更されたシェーダーが実際にコンパイルを通るか確かめ、
/// 通らなければ何も壊さずエラーだけを知らせる。
/// （.hlsli は単体でコンパイルできないので、変わったときは .hlsl を全部確かめる）
///
/// エディタ専用（USE_IMGUI）。配布ビルドでは何もしない
/// </summary>
class ShaderHotReload
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static ShaderHotReload *GetInstance();

    /// <summary>
    /// 初期化（現在の更新時刻を基準として控える）
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// 毎フレーム呼ぶ。一定間隔で更新時刻を見て、変わっていれば作り直す
    /// </summary>
    void Update();

    /// <summary>
    /// 変更の有無にかかわらず、今すぐ作り直す
    /// </summary>
    /// <returns>bool: 作り直せたら true。コンパイルが通らず中止したら false</returns>
    bool ReloadNow();

    /// <summary>
    /// 自動での作り直しを行うか
    /// </summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetAutoReloadEnabled(bool enabled) { autoReloadEnabled_ = enabled; }

    /// <summary>
    /// 自動での作り直しが有効か
    /// </summary>
    /// <returns>bool: 有効なら true</returns>
    bool IsAutoReloadEnabled() const { return autoReloadEnabled_; }

    /// <summary>
    /// 直近の作り直しの結果メッセージ（失敗時はDXCのエラー）
    /// </summary>
    /// <returns>const std::string&: 結果メッセージ</returns>
    const std::string &GetLastMessage() const { return lastMessage_; }

    /// <summary>
    /// 直近の作り直しが成功したか
    /// </summary>
    /// <returns>bool: 成功していれば true</returns>
    bool WasLastReloadSuccessful() const { return lastReloadSucceeded_; }

    /// <summary>
    /// ImGuiでの設定UIと状態表示
    /// </summary>
    void DrawImGui();

  private:
    ShaderHotReload() = default;
    ~ShaderHotReload() = default;
    ShaderHotReload(const ShaderHotReload &) = delete;
    ShaderHotReload &operator=(const ShaderHotReload &) = delete;

    /// <summary>
    /// 実際に作り直す本体
    /// </summary>
    /// <param name="verifyAll">全ての .hlsl のコンパイルを確かめるか（.hlsli が変わったときと手動実行）</param>
    /// <param name="changedShaders">verifyAll が false のときに確かめる .hlsl（改行区切り）</param>
    /// <returns>bool: 作り直せたら true</returns>
    bool RunReload(bool verifyAll, const std::string &changedShaders);

    /// <summary>
    /// shaders/ 配下を走査し、更新時刻の控えを取り直す
    /// </summary>
    /// <param name="outChangedAnyInclude">.hlsli が変わっていたら true が入る</param>
    /// <param name="outChangedShaders">変わった .hlsl の相対パス（改行区切り）</param>
    /// <returns>bool: 何か変わっていれば true</returns>
    bool CollectChanges(bool *outChangedAnyInclude, std::string *outChangedShaders);

    /// <summary>
    /// 指定した .hlsl がコンパイルを通るか確かめる
    /// </summary>
    /// <param name="relativePath">shaders ルートからの相対パス</param>
    /// <param name="outError">失敗したときのDXCのメッセージ</param>
    /// <returns>bool: 通れば true。プロファイルを判定できないファイルも true（触らない）</returns>
    bool VerifyShader(const std::string &relativePath, std::string *outError);

    DirectXCommon *pDxCommon_ = nullptr; // DirectX共通処理

    bool autoReloadEnabled_ = true;   // 保存を検知して自動で作り直すか
    bool lastReloadSucceeded_ = true; // 直近の作り直しが成功したか
    std::string lastMessage_;         // 直近の結果メッセージ
    int reloadCount_ = 0;             // 作り直した回数（動いていることの確認用）
    float pollTimer_ = 0.0f;          // 次に更新時刻を見るまでの残り時間
};
} // namespace Hagine
