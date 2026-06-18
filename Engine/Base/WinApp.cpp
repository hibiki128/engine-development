#include "WinApp.h"
#ifdef USE_IMGUI
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd_, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // USE_IMGUI

#pragma comment(lib, "winmm.lib")

namespace Hagine {

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd_, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef _DEBUG
    // ImGui用ウィンドウプロシージャ呼び出し
    if (ImGui_ImplWin32_WndProcHandler(hwnd_, msg, wparam, lparam)) {
        return true;
    }
#endif // _DEBUG
       // メッセージに応じてゲーム固有の処理を行う
    switch (msg) {
        // ウィンドウが破棄された
    case WM_DESTROY:
        // OSに対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;
    }
    // 標準のメッセージ処理を行う
    return DefWindowProc(hwnd_, msg, wparam, lparam);
}

void WinApp::Initialize() {
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
    RECT wrc = {0, 0, kClientWidth, kClientHeight};

    // クライアント領域を元に実際のサイズにwrcを変更してもらう
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    // システムタイマーの分解能を上げる
    timeBeginPeriod(1);

    // ウィンドウの生成
    hwnd_ = CreateWindow(
        wc_.lpszClassName,     // 利用するクラス名
        L"0000_タイトル",            // タイトルバーの文字
        WS_OVERLAPPEDWINDOW,  // よく見るウィンドウスタイル
        CW_USEDEFAULT,        // 表示X座標
        CW_USEDEFAULT,        // 表示Y座標
        wrc.right - wrc.left, // ウィンドウ横幅
        wrc.bottom - wrc.top, // ウィンドウ縦幅
        nullptr,              // 親ウィンドウハンドル
        nullptr,              // メニューハンドル
        wc_.hInstance,         // インスタンスハンドル
        nullptr);             // オプション

    // ウィンドウを表示する
    ShowWindow(hwnd_, SW_SHOW);
}

void WinApp::Update() {
}

void WinApp::Finalize() {
    CoUninitialize();
    CloseWindow(hwnd_);
}

bool WinApp::ProcessMessage() {
    MSG msg{};

    // Windowにメッセージが来てたら最優先で処理させる
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ウィンドウの×ボタンが押されるまでループ
    if (msg.message == WM_QUIT) {
        return true;
    }

    return false;
}

void WinApp::ToggleFullScreen() {

    // 現在のフルスクリーン状態を確認
    if (!isFullScreen_) {
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
    } else {
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

void WinApp::ClosedWindow() {
    // ウィンドウハンドルが有効な場合のみ処理
    if (hwnd_ != nullptr) {
        // WM_CLOSEメッセージを送信してウィンドウを終了
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }
}
} // namespace Hagine
