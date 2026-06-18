#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Object/Object3dCommon.h"
#include "animation/ModelAnimation.h"
#include "light/LightGroup.h"
#include "string"
#include "type/Matrix4x4.h"
#include "type/Vector2.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include "vector"
#include <Graphics/PipeLine/PipeLineManager.h>
#include <Model/Material/Material.h>
#include <Model/Model.h>
#include <Transform/ObjColor.h>

namespace Hagine {
class ModelCommon;
class Object3d {
  private: // メンバ変数
    struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    // 座標変換行列データ
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Matrix4x4 WorldInverseTranspose;
        Matrix4x4 LightWVP;
    };

    DirectXCommon *dxCommon_ = nullptr;

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    // バッファリソース内のデータを指すポインタ
    TransformationMatrix *transformationMatrixData_ = nullptr;

    Transform transform_;

    Model *model_ = nullptr;
    std::shared_ptr<ModelAnimation> currentModelAnimation_ = nullptr;
    std::map<std::string, std::shared_ptr<ModelAnimation>> modelAnimations_;
    std::vector<std::unique_ptr<Material>> materials_;
    std::vector<ObjColor> color_;
    ModelCommon *modelCommon_ = nullptr;
    LightGroup *lightGroup_ = nullptr;

    // 移動させる用各SRT
    Vector3 position_ = {0.0f, 0.0f, 0.0f};
    Vector3 rotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 size_ = {1.0f, 1.0f, 1.0f};
    bool isPrimitive_ = false;
    bool isAnimationSwitchPending_ = false;
    std::string nextAnimationFileName_;
    bool targetLoop_ = true;

    // アニメーションファイルパスごとのループフラグ
    // AddAnimation() で登録し、AnimationUpdate() が modelFilePath_ をキーに参照する
    std::map<std::string, bool> animationLoopFlags_;

    std::string modelFilePath_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    BlendMode blendMode_ = BlendMode::kNone;

    float animationSpeed_ = 1.0f;
    float blendDuration_ = 0.5f;

  public: // メンバ関数
    void Initialize();

    /// <summary>
    /// 初期化
    /// </summary>
    void CreateModel(const std::string &filePath);

    void CreatePrimitiveModel(const PrimitiveType &type, std::string texPath);

    /// <summary>
    /// 更新
    /// </summary>
    void Update(const WorldTransform &worldTransform, const ViewProjection &viewProjection);

    /// <summary>
    /// アニメーションの更新（ループ設定はアニメーションごとに自動解決）
    /// </summary>
    void AnimationUpdate();

    /// <summary>
    /// 補間状態を取得
    /// </summary>
    bool IsAnimationBlending() const;

    /// <summary>
    /// 即座にアニメーション切り替え（補間なし、デバッグ用）
    /// </summary>
    void SetAnimationImmediate(const std::string &fileName);

    void SetAnimation(const std::string &animationFileName);

    /// <summary>
    /// アニメーションの有無
    /// </summary>
    /// <param name="anime"></param>
    void SetStopAnimation(bool anime) { currentModelAnimation_->SetIsAnimation(anime); }

    void DrawWireframe(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool isRainbow = false);

    /// <summary>
    /// シャドウマップパスへの描画（深度のみ）
    /// </summary>
    void DrawShadow(const WorldTransform &worldTransform);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool reflect, bool Lighting = true, bool modelDraw = true);

    /// <summary>
    /// スケルトン描画
    /// </summary>
    void DrawSkeleton(const WorldTransform &worldTransform, const ViewProjection &viewProjection);

    void PlayAnimation() { currentModelAnimation_->PlayAnimation(); }

    /// <summary>
    /// getter
    /// </summary>
    /// <returns></returns>
    const Vector3 &GetPosition() const { return position_; }
    const Vector3 &GetRotation() const { return rotation_; }
    const Vector3 &GetSize() const { return size_; }
    size_t GetMaterialCount() const { return materials_.size(); }
    std::string GetModelFilePath() const { return modelFilePath_; }
    std::string GetTextureFilePath(uint32_t materialIndex) const {
        return materials_[materialIndex]->GetMaterialData().textureFilePath;
    }
    std::vector<std::string> GetAllTextruePath() {
        std::vector<std::string> texturePaths = {};
        for (int i = 0; i < GetMaterialCount(); i++) {
            texturePaths.push_back(materials_[i]->GetMaterialData().textureFilePath);
        }
        return texturePaths;
    }
    ModelAnimation *GetCurrentModelAnimation() const {
        return currentModelAnimation_.get();
    }

    const bool GetHaveAnimation() const { return model_->GetModelData().hasAnimations; }
    bool IsFinish() { return currentModelAnimation_->IsFinish(); }
    Model *GetModel() const { return model_; }

    Material *GetMaterial(uint32_t index) {
        return (index < materials_.size()) ? materials_[index].get() : nullptr;
    }
    Vector4 GetColor(int index = 0) { return color_[index].GetColor(); }

    /// <summary>
    /// setter
    /// </summary>
    /// <param name="position"></param>
    void SetModel(Model *model_) { this->model_ = model_; }
    void SetPosition(const Vector3 &position_) { this->position_ = position_; }
    void SetRotation(const Vector3 &rotation_) { this->rotation_ = rotation_; }
    void SetSize(const Vector3 &size_) { this->size_ = size_; }
    void SetModel(const std::string &filePath);
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    void SetColor(Vector4 color, int index = 0) { color_[index].SetColor(color); }

    // マルチマテリアル用のsetter
    void SetTexture(const std::string &filePath, uint32_t materialIndex);

    void SetEnvironmentCoefficients(float value);

    /// <summary>
    /// アニメーション速度設定
    /// </summary>
    void SetAnimationSpeed(float speed);
    float GetAnimationSpeed() const { return animationSpeed_; }

    /// <summary>
    /// アニメーション補間時間設定
    /// </summary>
    void SetAnimationBlendDuration(float duration);
    float GetAnimationBlendDuration() const { return blendDuration_; }

    /// <summary>
    /// アニメーションのループ設定
    /// </summary>
    void SetAnimationLoop(const std::string &fileName, bool loop);
    bool GetAnimationLoop(const std::string &fileName);

    /// <summary>
    /// アニメーション追加
    /// </summary>
    /// <param name="fileName">アニメーションファイルパス</param>
    /// <param name="loop">ループ再生するか（デフォルト true）</param>
    void AddAnimation(const std::string &fileName, bool loop = true);

  private: // メンバ関数
    /// <summary>
    /// 座標変換行列データ作成
    /// </summary>
    void CreateTransformationMatrix();

    void CreateIndependentMaterials();

    void DrawBoneArmature(const Vector3 &parentPos, const Vector3 &childPos, float scale);

    void DrawArmatureShape(const Vector3 &startPos, const Vector3 &endPos, float baseWidth, float tipWidth, const Vector4 &color);

    Vector3 ExtractTranslation(const Matrix4x4 &matrix) {
        return Vector3(matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]);
    }
};
} // namespace Hagine
