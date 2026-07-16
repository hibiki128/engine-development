#pragma once
#include "Windows.h"
#ifdef USE_IMGUI
#include "imgui/imgui.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd_, UINT msg, WPARAM wParam, LPARAM lParam);
#endif //  USE_IMGUI
#include <cstdint>
// WindowsAPI
namespace Hagine {
class WinApp
{
  public:
    WinApp() = default;
    ~WinApp() = default;
    WinApp(const WinApp &) = delete;
    const WinApp &operator=(const WinApp &) = delete;

  public: // 静的メンバ関数
    static LRESULT CALLBACK WindowProc(HWND hwnd_, UINT msg, WPARAM wparam, LPARAM lparam);

  public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update();

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// メッセージの処理
    /// </summary>
    /// <returns></returns>
    bool ProcessMessage();

    /// <summary>
    /// フルスクリーンの切り替え
    /// </summary>
    void ToggleFullScreen();

    void ClosedWindow();

    /// <summary>
    /// クライアント領域のサイズを変更する（ウィンドウモード時のみ有効）
    /// </summary>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void SetClientSize(int32_t width, int32_t height);

    /// <summary>
    /// 仮想解像度（内部レンダリング解像度）を設定する
    /// ウィンドウ・レンダーターゲットが生成される前（Framework::Initialize より前）に呼ぶこと
    /// </summary>
    /// <param name="width">仮想解像度の横幅</param>
    /// <param name="height">仮想解像度の高さ</param>
    static void SetVirtualResolution(int32_t width, int32_t height);

    /// <summary>
    /// 保留中のリサイズを1回だけ取得する（取得後フラグはクリアされる）
    /// </summary>
    /// <param name="width">実クライアント幅の出力先</param>
    /// <param name="height">実クライアント高さの出力先</param>
    /// <returns>bool: リサイズが保留されていれば true</returns>
    bool ConsumeResize(uint32_t &width, uint32_t &height);

    /// <summary>
    /// 仮想解像度のアスペクト比を保って実クライアント領域に収まる表示矩形を計算する（レターボックス）
    /// 最終合成パスのビューポートとマウス座標の逆変換で共用する
    /// </summary>
    /// <param name="clientWidth">実クライアント幅</param>
    /// <param name="clientHeight">実クライアント高さ</param>
    /// <param name="outX">表示矩形の左上X（出力）</param>
    /// <param name="outY">表示矩形の左上Y（出力）</param>
    /// <param name="outWidth">表示矩形の幅（出力）</param>
    /// <param name="outHeight">表示矩形の高さ（出力）</param>
    static void ComputeLetterboxRect(int32_t clientWidth, int32_t clientHeight,
                                     float &outX, float &outY, float &outWidth, float &outHeight);

    /// <summary>
    /// getter
    /// </summary>
    /// <returns></returns>
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return wc_.hInstance; }
    bool &IsFullScreen() { return isFullScreen_; }
    int32_t GetClientWidth() const { return clientWidth_; }        // 実クライアント幅
    int32_t GetClientHeight() const { return clientHeight_; }      // 実クライアント高さ
    static int32_t GetVirtualWidth() { return virtualWidth_; }     // 仮想解像度の横幅
    static int32_t GetVirtualHeight() { return virtualHeight_; }   // 仮想解像度の高さ

  public: // 定数
    // 仮想解像度（内部レンダリング解像度）の既定値
    // ウィンドウの実サイズが変わっても、ゲーム内の描画・スプライト座標・マウス座標は
    // この解像度を基準にした空間で扱われ、最終合成パスで実ウィンドウサイズへ拡縮される。
    // ゲーム層は SetVirtualResolution で起動時にこの値を上書きできる。
    static constexpr int32_t kDefaultVirtualWidth = 1760;  // 横
    static constexpr int32_t kDefaultVirtualHeight = 990;  // 縦
  private:                                                 // メンバ変数
    // 仮想解像度（内部レンダリング解像度）。SetVirtualResolution で起動時に設定される。
    inline static int32_t virtualWidth_ = kDefaultVirtualWidth;
    inline static int32_t virtualHeight_ = kDefaultVirtualHeight;

    HWND hwnd_ = nullptr; // ウィンドウハンドル
    WNDCLASS wc_{};       // ウィンドウクラスの設定
    bool isFullScreen_ = false;
    // ウィンドウモードの復元用の矩形
    RECT windowRect_ = {0, 0, virtualWidth_, virtualHeight_};

    // 実クライアント領域のサイズ（WM_SIZE で更新される）
    int32_t clientWidth_ = virtualWidth_;
    int32_t clientHeight_ = virtualHeight_;
    bool resizeRequested_ = false; // スワップチェーン再構築待ちフラグ
};
} // namespace Hagine
