#pragma once
#include "SpriteStruct.h"
#include "d3d12.h"
#include "string"
#include "wrl.h"
#include <Graphics/Srv/SrvManager.h>

namespace Hagine {
class SpriteCommon;

/// <summary>
/// スプライト描画を管理するクラス
/// 2D画像の描画、トランスフォーム、UV変換などを制御
/// </summary>
class Sprite {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="textureFilePath">テクスチャファイルパス</param>
    /// <param name="position">座標</param>
    /// <param name="color">色</param>
    /// <param name="anchorpoint">アンカーポイント</param>
    /// <param name="isFlipX">左右反転フラグ</param>
    /// <param name="isFlipY">上下反転フラグ</param>
    void Initialize(const std::string &textureFilePath, Vector2 position, Vector4 color = {1, 1, 1, 1}, Vector2 anchorpoint = {0.0f, 0.0f}, bool isFlipX = false, bool isFlipY = false);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="isBackMost">背面フラグ</param>
    void Draw(bool isBackMost = false);

    /// ===================================================
    /// Getter
    /// ===================================================
    const Vector2 &GetPosition() const { return position_; }
    Vector2 &GetPositionRef() { return position_; }
    float GetRotation() const { return rotation_; }
    const Vector2 &GetSize() const { return size_; }
    const Vector4 &GetColor() const { return materialData_->color; }
    const Vector2 &GetAnchorPoint() const { return anchorPoint_; }
    const bool GetFlipX() const { return isFlipX_; }
    const bool GetFilpY() const { return isFlipY_; }
    const Vector2 &GetTexLeftTop() const { return textureLeftTop_; }
    const Vector2 &GetTexSize() const { return textureSize_; }
    uint32_t &GetInstanceCount() { return instanceCount_; }
    Matrix4x4 GetUVTransform() { return materialData_->uvTransform; }
    Vector2 GetUVPosition() { return uvPosition_; }
    Vector2 GetUVSize() { return uvSize_; }
    float GetUVRotate() { return uvRotate_; }

    /// ===================================================
    /// Setter
    /// ===================================================
    void SetPosition(const Vector2 &position) { this->position_ = position; }
    void SetRotation(float rotation_) { this->rotation_ = rotation_; }
    void SetSize(const Vector2 &size_) { this->size_ = size_; }
    void SetColor(const Vector3 &color) { materialData_->color.x = color.x, materialData_->color.y = color.y, materialData_->color.z = color.z; }
    void SetAlpha(const float &alpha) { materialData_->color.w = alpha; }
    void SetTexturePath(std::string textureFilePath);
    void SetAnchorPoint(const Vector2 &anchorPoint) { this->anchorPoint_ = anchorPoint; }
    void SetFlipX(bool isFlipX) { isFlipX_ = isFlipX; }
    void SetFlipY(bool isFlipY) { isFlipY_ = isFlipY; }
    void SetTexLeftTop(const Vector2 &textureLeftTop_) { this->textureLeftTop_ = textureLeftTop_; }
    void SetTexSize(const Vector2 &textureSize_) { this->textureSize_ = textureSize_; }
    void SetUVTransform(const Matrix4x4 &uvTransform) {
        materialData_->uvTransform = uvTransform;
        uvSize_.x = sqrt(uvTransform.m[0][0] * uvTransform.m[0][0] + uvTransform.m[1][0] * uvTransform.m[1][0]);
        uvSize_.y = sqrt(uvTransform.m[0][1] * uvTransform.m[0][1] + uvTransform.m[1][1] * uvTransform.m[1][1]);
        uvRotate_ = atan2(uvTransform.m[1][0], uvTransform.m[0][0]);
        uvPosition_.x = uvTransform.m[3][0];
        uvPosition_.y = uvTransform.m[3][1];
    }
    void SetUVPosition(const Vector2 &position) { uvPosition_ = position; }
    void SetUVSize(const Vector2 &size_) { uvSize_ = size_; }
    void SetUVRotate(const float &rotate) { uvRotate_ = rotate; }
    void SetInstanceCount(uint32_t count);
    void SetInstanceTransform(uint32_t index, const TransformationMatrix &transform);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="isbackmost_">背面フラグ</param>
    void Update(bool isbackmost_);

    /// <summary>
    /// 頂点データを作成
    /// </summary>
    void CreateVartexData();

    /// <summary>
    /// マテリアルデータを作成
    /// </summary>
    void CreateMaterial();

    /// <summary>
    /// 座標変換行列データを作成
    /// </summary>
    void CreateTransformationMatrix();

    /// <summary>
    /// テクスチャサイズを画像に合わせる
    /// </summary>
    void AdjustTextureSize();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    SpriteCommon *spriteCommon_ = nullptr;        // スプライト共通処理
    SrvManager *srvManager_ = nullptr;            // SRV管理

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr; // 頂点リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;  // インデックスリソース
    SpriteVertexData *vertexData_ = nullptr;                         // 頂点データ
    uint32_t *indexData_ = nullptr;                                   // インデックスデータ
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};                     // 頂点バッファビュー
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};                       // インデックスバッファビュー

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr; // マテリアルリソース
    SpriteMaterial *materialData_ = nullptr;                           // マテリアルデータ

    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_; // 変換行列リソース
    TransformationMatrix *transformationMatrixData_ = nullptr;            // 変換行列データ

    Vector2 position_ = {0.0f, 0.0f};           // 座標
    float rotation_ = 0.0f;                      // 回転角度
    Vector2 size_ = {640.0f, 360.0f};            // サイズ

    std::string fullpath_;                       // ファイルパス
    Vector2 anchorPoint_ = {0.0f, 0.0f};        // アンカーポイント

    bool isFlipX_ = false;                      // 左右反転フラグ
    bool isFlipY_ = false;                      // 上下反転フラグ
    bool isbackmost_ = false;                   // 背面フラグ

    Vector2 textureLeftTop_ = {0.0f, 0.0f};      // テクスチャ左上座標
    Vector2 textureSize_ = {512.0f, 512.0f};     // テクスチャサイズ

    uint32_t instanceCount_ = 1;                 // インスタンス数
    uint32_t transformationMatrixSrvIndex_ = 0;  // 変換行列SRVインデックス

    float uvRotate_ = 0.0f;                     // UV回転角度
    Vector2 uvSize_ = {1.0f, 1.0f};             // UVサイズ
    Vector2 uvPosition_ = {0.0f, 0.0f};         // UV座標
};
} // namespace Hagine
