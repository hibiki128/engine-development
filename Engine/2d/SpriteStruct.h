#pragma once
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <type/Vector2.h>
#include <type/Matrix4x4.h>

/// <summary>
/// オブジェクトの変形情報を保持する構造体
/// </summary>
namespace Hagine {
struct Transform {
    Vector3 scale;     // スケール
    Vector3 rotate;    // 回転
    Vector3 translate; // 移動
};

/// <summary>
/// スプライトの頂点情報を保持する構造体
/// </summary>
struct SpriteVertexData {
    Vector4 position; // 頂点座標
    Vector2 texcoord; // テクスチャ座標
};

/// <summary>
/// スプライトのマテリアル情報を保持する構造体
/// </summary>
struct SpriteMaterial {
    Vector4 color;             // カラー
    Matrix4x4 uvTransform;     // UV変換行列
    float padding[3];          // パディング
};

/// <summary>
/// スプライトの座標変換行列情報を保持する構造体
/// </summary>
struct TransformationMatrix {
    Matrix4x4 WVP;   // WVP行列
    Matrix4x4 World; // ワールド行列
};
} // namespace Hagine
