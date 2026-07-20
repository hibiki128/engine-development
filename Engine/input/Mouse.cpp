#include "Mouse.h"
#include "WinApp.h"
#include <cmath>
#include "MyMath.h"
#include <assert.h>
namespace Hagine {
void Mouse::Init(Microsoft::WRL::ComPtr<IDirectInput8> directInput, HWND hWnd)
{
    hWnd_ = hWnd;
    // マウスデバイスの生成
    HRESULT result = directInput->CreateDevice(GUID_SysMouse, &devMouse_, NULL);
    assert(SUCCEEDED(result));
    mousePosition_ = {0.0f, 0.0f}; // 初期値の確認

    // 入力データ形式のセット
    result = devMouse_->SetDataFormat(&c_dfDIMouse2);
    assert(SUCCEEDED(result));

    // 排他制御レベルのセット
    result = devMouse_->SetCooperativeLevel(hWnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(result));
}

void Mouse::Update()
{
    // マウスの状態を更新
    mousePre_ = mouse_;
    devMouse_->Acquire();
    devMouse_->GetDeviceState(sizeof(mouse_), &mouse_);
}

// マウス****************************************************************

bool Mouse::IsPressMouse(int32_t buttonNumber) const
{
    return (mouse_.rgbButtons[buttonNumber] & 0x80);
}

bool Mouse::IsTriggerMouse(int32_t buttonNumber) const
{
    return (mouse_.rgbButtons[buttonNumber] & 0x80) && !(mousePre_.rgbButtons[buttonNumber] & 0x80);
}

MouseMove Mouse::GetMouseMove()
{
    MouseMove move;
    move.lX = mouse_.lX;
    move.lY = mouse_.lY;
    move.lZ = mouse_.lZ;
    return move;
}

Vector3 Mouse::GetMousePos3D(const ViewProjection &viewprojection, float depthFactor, float blockSize) const
{
    // 2Dマウス座標を取得（仮想解像度座標系）
    Vector2 mousePos = mousePosition_;

    // 仮想解像度（マウス座標は GetMousePos で仮想解像度へ変換済み）
    float windowWidth = static_cast<float>(WinApp::GetVirtualWidth());
    float windowHeight = static_cast<float>(WinApp::GetVirtualHeight());

    // スクリーン座標を正規化デバイス座標 (NDC) に変換 [-1, 1] の範囲にする
    float ndcX = (2.0f * mousePos.x / windowWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mousePos.y / windowHeight);
    float ndcZ = depthFactor; // Z軸の奥行きを調整するパラメータ

    // NDC座標をVector3に変換（NDCのZ値をdepthFactorで調整）
    Vector3 clipPos = {ndcX, ndcY, ndcZ};

    // 逆射影行列を使ってクリップ空間からビュー空間へ変換
    Matrix4x4 invProj = Inverse(viewprojection.matProjection_);
    Vector3 viewPos = Transformation(clipPos, invProj);

    // ビュー空間からワールド空間へ変換
    Matrix4x4 invView = Inverse(viewprojection.matView_);
    Vector3 worldPos = Transformation(viewPos, invView);

    // ブロックサイズに基づいてスナップ処理を適用
    if (blockSize > 0.0f)
    {
        worldPos.x = std::round(worldPos.x / blockSize) * blockSize;
        worldPos.y = std::round(worldPos.y / blockSize) * blockSize;
        worldPos.z = std::round(worldPos.z / blockSize) * blockSize;
    }

    // ワールド座標を返す
    return worldPos;
}

int32_t Mouse::GetWheel() const
{
    return mouse_.lZ;
}

Vector2 Mouse::GetMousePos()
{
    // マウス座標を取得
    POINT mousePos;
    GetCursorPos(&mousePos);
    // スクリーン座標からウィンドウ内座標に変換
    ScreenToClient(hWnd_, &mousePos); // hWndはウィンドウハンドル

    // 実クライアント座標 → 仮想解像度座標へ逆変換する
    // （ウィンドウサイズ変更・フルスクリーン時もゲーム内座標系は仮想解像度で一定）
    RECT clientRect{};
    GetClientRect(hWnd_, &clientRect);
    const int32_t clientW = clientRect.right - clientRect.left;
    const int32_t clientH = clientRect.bottom - clientRect.top;

    float viewX = 0.0f, viewY = 0.0f, viewW = 0.0f, viewH = 0.0f;
    WinApp::ComputeLetterboxRect(clientW, clientH, viewX, viewY, viewW, viewH);

    float virtualX = (static_cast<float>(mousePos.x) - viewX) * (static_cast<float>(WinApp::GetVirtualWidth()) / viewW);
    float virtualY = (static_cast<float>(mousePos.y) - viewY) * (static_cast<float>(WinApp::GetVirtualHeight()) / viewH);

    mousePosition_ = Vector2(virtualX, virtualY);

    return mousePosition_;
}
} // namespace Hagine
