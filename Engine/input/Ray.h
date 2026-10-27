#pragma once
#include "type/Vector2.h"
#include "type/Vector3.h"
#include <string>

namespace Hagine {
/// <summary>
/// レイ
/// </summary>
struct Ray {
    Vector3 origin;    // レイの開始点
    Vector3 direction; // レイの方向（正規化済み）
    float length;      // レイの最大長
};

/// <summary>
/// レイのヒット情報
/// </summary>
struct RayHitInfo {
    bool hit;               // ヒットしたかどうか
    Vector3 hitPoint;       // ヒット点
    Vector3 hitNormal;      // ヒット面の法線
    float distance;         // レイの開始点からヒット点までの距離
    std::string objectName; // ヒットしたオブジェクト名
};

/// <summary>
/// ImGuiシーン描画領域情報
/// </summary>
struct SceneViewport {
    Vector2 position; // シーンウィンドウの左上座標
    Vector2 size;     // シーンウィンドウのサイズ
};
} // namespace Hagine
