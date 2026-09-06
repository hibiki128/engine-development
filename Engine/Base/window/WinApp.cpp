#include "WinApp.h"
#ifdef USE_IMGUI
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd_, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // USE_IMGUI

#pragma comment(lib, "winmm.lib")

namespace Hagine {

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd_, UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef USE_IMGUI
    // ImGui用ウィンドウプロシージャ呼び出し
    if (ImGui_ImplWin32_WndProcHandler(hwnd_, msg, wparam, lparam))
    {
        return true;
    }
#endif // USE_IMGUI
       // メッセージに応じてゲーム固有の処理を行う
    switch (msg)
    {
        // ウィンドウが破棄された
    case WM_DESTROY:
        // OSに対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;

        // クライアント領域のサイズが変わった（メニューからの変更・フルスクリーン切替・ドラッグ）
    case WM_SIZE: {
        // CreateWindow 中のメッセージは GWLP_USERDATA 未設定なのでスキップ
        WinApp *self = reinterpret_cast<WinApp *>(GetWindowLongPtr(hwnd_, GWLP_USERDATA));
        if (self && wparam != SIZE_MINIMIZED)
        {
            int32_t width = static_cast<int32_t>(LOWORD(lparam));
            int32_t height = static_cast<int32_t>(HIWORD(lparam));
            if (width > 0 && height > 0 &&
                (width != self->clientWidth_ || height != self->clientHeight_))
            {
                self->clientWidth_ = width;
                self->clientHeight_ = height;
                self->resizeRequested_ = true;
            }
        }
        return 0;
    }
    }
    // 標準のメッセージ処理を行う
    return DefWindowProc(hwnd_, msg, wparam, lparam);
}

void WinApp::Initialize()
{
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);

    // ウィンドウプロシージャ
    wc_.lpfnWndProc = WindowProc;
    // ウィンドウクラス名
    wc_.lpszClassName = L"WindowClass";
    // インスタンスハンドル
    wc_.hInstance = GetModuleHandle(nullptr);
    // カーソル
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // ウィンドウクラスを登録する
    RegisterClass(&wc_);

    // ウィンドウサイズを表す構造体にクライアント領域を入れる
    RECT wrc = {0, 0, virtualWidth_, virtualHeight_};

    // クライアント領域を元に実際のサイズにwrcを変更してもらう
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    // システムタイマーの分解能を上げる
    timeBeginPeriod(1);

    // ウィンドウの生成
    hwnd_ = CreateWindow(
        wc_.lpszClassName,                               // 利用するクラス名
        L"4027_からぽっぷ", // タイトルバーの文字
        WS_OVERLAPPEDWINDOW,                             // よく見るウィンドウスタイル
        CW_USEDEFAULT,                                   // 表示X座標
        CW_USEDEFAULT,                                   // 表示Y座標
        wrc.right - wrc.left,                            // ウィンドウ横幅
        wrc.bottom - wrc.top,                            // ウィンドウ縦幅
        nullptr,                                         // 親ウィンドウハンドル
        nullptr,                                         // メニューハンドル
        wc_.hInstance,                                   // インスタンスハンドル
        nullptr);                                        // オプション

    // WM_SIZE から実クライアントサイズを更新できるよう、インスタンスを関連付ける
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // ウィンドウを表示する
    ShowWindow(hwnd_, SW_SHOW);
}

void WinApp::Update()
{
}

void WinApp::Finalize()
{
    CoUninitialize();
    CloseWindow(hwnd_);
}

bool WinApp::ProcessMessage()
{
    MSG msg{};

    // Windowにメッセージが来てたら最優先で処理させる
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ウィンドウの×ボタンが押されるまでループ
    if (msg.message == WM_QUIT)
    {
        return true;
    }

    return false;
}

void WinApp::ToggleFullScreen()
{

    // 現在のフルスクリーン状態を確認
    if (!isFullScreen_)
    {
        // ウィンドウモードからフルスクリーンに変更

        // 現在のウィンドウの位置とサイズを保存
        GetWindowRect(hwnd_, &windowRect_);

        // ディスプレイの解像度を取得
        uint32_t screenWidth = GetSystemMetrics(SM_CXSCREEN);
        uint32_t screenHeight = GetSystemMetrics(SM_CYSCREEN);

        // フルスクリーンに設定
        SetWindowLongPtr(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, screenWidth, screenHeight, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
        ShowWindow(hwnd_, SW_MAXIMIZE);

        // フルスクリーンフラグを設定
        isFullScreen_ = true;
    }
    else
    {
        // フルスクリーンからウィンドウモードに変更

        // ウィンドウスタイルを元に戻す
        SetWindowLongPtr(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, windowRect_.left, windowRect_.top,
                     windowRect_.right - windowRect_.left, windowRect_.bottom - windowRect_.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
        ShowWindow(hwnd_, SW_RESTORE);

        // フルスクリーンフラグを解除
        isFullScreen_ = false;
    }
}

void WinApp::SetClientSize(int32_t width, int32_t height)
{
    // フルスクリーン中はウィンドウサイズを直接変更しない
    if (isFullScreen_ || hwnd_ == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    // クライアント領域が指定サイズになるようウィンドウ全体の矩形を計算する
    RECT wrc = {0, 0, width, height};
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    // 位置は維持したままサイズだけ変更（WM_SIZE 経由で resizeRequested_ が立つ）
    SetWindowPos(hwnd_, nullptr, 0, 0,
                 wrc.right - wrc.left, wrc.bottom - wrc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

void WinApp::SetVirtualResolution(int32_t width, int32_t height)
{
    if (width > 0 && height > 0)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }
}

void WinApp::ComputeLetterboxRect(int32_t clientWidth, int32_t clientHeight,
                                  float &outX, float &outY, float &outWidth, float &outHeight)
{
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        outX = 0.0f;
        outY = 0.0f;
        outWidth = static_cast<float>(virtualWidth_);
        outHeight = static_cast<float>(virtualHeight_);
        return;
    }

    const float targetAspect = static_cast<float>(virtualWidth_) / static_cast<float>(virtualHeight_);
    const float windowAspect = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);

    if (windowAspect > targetAspect)
    {
        // ウィンドウが横長 → 左右に黒帯
        outHeight = static_cast<float>(clientHeight);
        outWidth = outHeight * targetAspect;
        outX = (static_cast<float>(clientWidth) - outWidth) * 0.5f;
        outY = 0.0f;
    }
    else
    {
        // ウィンドウが縦長 → 上下に黒帯
        outWidth = static_cast<float>(clientWidth);
        outHeight = outWidth / targetAspect;
        outX = 0.0f;
        outY = (static_cast<float>(clientHeight) - outHeight) * 0.5f;
    }
}

bool WinApp::ConsumeResize(uint32_t &width, uint32_t &height)
{
    if (!resizeRequested_)
    {
        return false;
    }
    resizeRequested_ = false;
    width = static_cast<uint32_t>(clientWidth_);
    height = static_cast<uint32_t>(clientHeight_);
    return true;
}

void WinApp::ClosedWindow()
{
    // ウィンドウハンドルが有効な場合のみ処理
    if (hwnd_ != nullptr)
    {
        // WM_CLOSEメッセージを送信してウィンドウを終了
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }
}
} // namespace Hagine
