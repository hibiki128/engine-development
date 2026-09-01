#pragma once
#include "camera/projection/ViewProjection.h"
#include "object/Object3dCommon.h"
#include "animation/ModelAnimation.h"
#include "light/LightGroup.h"
#include "string"
#include "type/Matrix4x4.h"
#include "type/Vector2.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include "vector"
#include <graphics/pipeline/PipelineManager.h>
#include <model/material/Material.h>
#include <model/Model.h>
#include <transform/ObjColor.h>

namespace Hagine {
class ModelCommon;
class Object3d
{
  private: // メンバ変数
    struct Transform
    {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    // 座標変換行列データ
    struct TransformationMatrix
    {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Matrix4x4 WorldInverseTranspose;
        Matrix4x4 LightWVP;
    };

    DirectXCommon *pDxCommon_ = nullptr;

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    // バッファリソース内のデータを指すポインタ
    TransformationMatrix *pTransformationMatrixData_ = nullptr;

    Transform transform_;

    Model *pModel_ = nullptr;
    std::shared_ptr<ModelAnimation> currentModelAnimation_ = nullptr;
    std::map<std::string, std::shared_ptr<ModelAnimation>> modelAnimations_;
    std::vector<std::unique_ptr<Material>> materials_;
    std::vector<ObjColor> color_;
    // インスタンシング描画でマテリアル色を白に固定するための使い回しバッファ
    // （個体色はインスタンスバッファ側で乗算するので、ここで二重に掛けない）
    std::vector<ObjColor> instanceWhiteColors_;
    ModelCommon *pModelCommon_ = nullptr;
    LightGroup *pLightGroup_ = nullptr;

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
    // 動的モデル（メタボールなど）を作ったときの ModelManager 上のキー
    std::string dynamicModelKey_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    BlendMode blendMode_ = BlendMode::None;
    bool useDeferred_ = true; // ディファードのG-Bufferに載せてよいか

    float animationSpeed_ = 1.0f;
    float blendDuration_ = 0.5f;

    // 1フレーム内でGPUスキニングを二重実行しないためのガード。
    // 影パスと本描画パスの両方で pModel_->Update()（スキニングDispatch）が
    // 呼ばれるが、同一フレームのポーズは不変なので最初の1回だけ実行すればよい。
    // AnimationUpdate()（フレーム先頭）で false に戻す。
    bool skinnedThisFrame_ = false;

  public: // メンバ関数
    void Initialize();

    /// <summary>
    /// 初期化
    /// </summary>
    void CreateModel(const std::string &filePath);

    void CreatePrimitiveModel(const PrimitiveType &type, std::string texPath);

    /// <summary>
    /// 動的メッシュのモデルを作る（メタボールなど、オブジェクトごとに形が変わるもの用）。
    /// 中身は RebuildDynamicMesh() で入れるまで空。
    /// </summary>
    /// <param name="texPath">貼るテクスチャのパス</param>
    void CreateDynamicModel(std::string texPath);

    /// <summary>
    /// 動的メッシュの中身を差し替える。1 フレームに 1 回まで。
    /// </summary>
    void RebuildDynamicMesh(MeshData &&data);

    /// <summary>
    /// 動的モデルのキー（ModelManager から破棄するのに使う）
    /// </summary>
    const std::string &GetDynamicModelKey() const { return dynamicModelKey_; }

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
    /// 体の一部だけを別モーションで上書きするレイヤー再生を開始する
    /// （下半身は通常のアニメーション、指定ジョイント以下だけこのモーションになる）
    /// </summary>
    /// <param name="animationFileName">アニメーションファイルパス</param>
    /// <param name="maskRootJoint">上書き範囲の根になるジョイント名</param>
    /// <param name="loop">ループ再生するか（false なら再生終了で自動解除）</param>
    /// <param name="fadeDuration">レイヤーの出入りにかける時間（秒）</param>
    void PlayLayerAnimation(const std::string &animationFileName, const std::string &maskRootJoint,
                            bool loop = false, float fadeDuration = 0.1f);

    /// <summary>
    /// レイヤー再生を解除する
    /// </summary>
    /// <param name="fadeDuration">フェードアウトにかける時間（秒）</param>
    void StopLayerAnimation(float fadeDuration = 0.1f);

    /// <summary>
    /// レイヤー再生中かを取得
    /// </summary>
    /// <returns>bool: 再生中なら true</returns>
    bool IsLayerAnimationPlaying() const;

    /// <summary>
    /// アニメーションの有無
    /// </summary>
    /// <param name="anime"></param>
    void SetStopAnimation(bool anime) { currentModelAnimation_->SetIsAnimation(anime); }

    void DrawWireframe(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool isRainbow = false);

    /// ===================================
    /// インスタンシング描画（Object3dInstancing から使う）
    /// ===================================

    /// <summary>
    /// 同じモデルを参照する他のオブジェクトとまとめて描けるかを返す。
    /// スキニング（アニメーション付き）は頂点バッファがモデル共有なのでまとめられない。
    /// </summary>
    /// <returns>bool: インスタンシング描画の対象にしてよいか</returns>
    bool CanBatchInstanced() const;

    /// <summary>
    /// 今のパス（影 / G-Buffer / 前方描画）で描かれる対象かを返す。
    /// Draw() の冒頭にあるディファードの振り分けと同じ判定。
    /// </summary>
    /// <param name="lighting">ライティング有効フラグ</param>
    /// <returns>bool: このパスで描くなら true</returns>
    bool ShouldDrawInCurrentPass(bool lighting) const;

    /// <summary>
    /// 同一バッチにまとめてよいかを表すシグネチャを計算する。
    /// モデル・全マテリアル・ライティング/反射/ブレンド設定が一致するもの同士だけが同じ値になる。
    /// </summary>
    /// <param name="reflect">反射有効フラグ</param>
    /// <param name="lighting">ライティング有効フラグ</param>
    /// <returns>size_t: バッチキー</returns>
    size_t ComputeBatchSignature(bool reflect, bool lighting) const;

    /// <summary>
    /// ワールド変換からインスタンス1個ぶんの行列を計算する（定数バッファには書かない）。
    /// </summary>
    /// <param name="worldTransform">ワールド変換</param>
    /// <param name="viewProjection">ビュープロジェクション</param>
    /// <param name="outWVP">WVP行列の書き込み先</param>
    /// <param name="outWorld">ワールド行列の書き込み先</param>
    /// <param name="outWorldInverseTranspose">ワールド逆転置行列の書き込み先</param>
    /// <param name="outLightWVP">ライト空間WVP行列の書き込み先</param>
    void BuildInstanceMatrices(const WorldTransform &worldTransform, const ViewProjection &viewProjection,
                               Matrix4x4 &outWVP, Matrix4x4 &outWorld,
                               Matrix4x4 &outWorldInverseTranspose, Matrix4x4 &outLightWVP) const;

    /// <summary>
    /// インスタンシングでまとめた1バッチぶんを描画する。
    /// 変換行列はインスタンスバッファから引くので、このオブジェクトの定数バッファは使わない。
    /// マテリアル色は白で送り、個体ごとの色はインスタンスバッファ側で乗算させる。
    /// </summary>
    /// <param name="instanceBufferAddress">このバッチの先頭インスタンスのGPUアドレス</param>
    /// <param name="instanceCount">インスタンス数</param>
    /// <param name="viewProjection">ビュープロジェクション（ライト更新用）</param>
    /// <param name="reflect">反射有効フラグ</param>
    /// <param name="lighting">ライティング有効フラグ</param>
    void DrawInstancedBatch(D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress, uint32_t instanceCount,
                            const ViewProjection &viewProjection, bool reflect, bool lighting);

    /// <summary>
    /// インスタンシングでまとめた1バッチぶんをシャドウマップへ描画する
    /// </summary>
    /// <param name="instanceBufferAddress">このバッチの先頭インスタンスのGPUアドレス</param>
    /// <param name="instanceCount">インスタンス数</param>
    void DrawShadowInstancedBatch(D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress, uint32_t instanceCount);

    /// <summary>ブレンドモードを取得（バッチキーに使う）</summary>
    BlendMode GetBlendMode() const { return blendMode_; }

    /// <summary>
    /// 個体色をインスタンスバッファ経由で渡せるか。
    /// 頂点シェーダーの instanceColor は「個体単位」でマテリアル単位ではないため、
    /// マテリアルが1つのときだけ使える（複数マテリアルなら色もバッチキーに含めて一致させる）。
    /// </summary>
    /// <returns>bool: インスタンスごとに色を変えられるか</returns>
    bool UsesInstanceColor() const { return materials_.size() == 1; }

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
    std::string GetTextureFilePath(uint32_t materialIndex) const
    {
        return materials_[materialIndex]->GetMaterialData().textureFilePath;
    }
    std::vector<std::string> GetAllTexturePath()
    {
        std::vector<std::string> texturePaths = {};
        for (int i = 0; i < GetMaterialCount(); i++)
        {
            texturePaths.push_back(materials_[i]->GetMaterialData().textureFilePath);
        }
        return texturePaths;
    }
    ModelAnimation *GetCurrentModelAnimation() const
    {
        return currentModelAnimation_.get();
    }

    const bool GetHaveAnimation() const { return pModel_->GetModelData().hasAnimations; }
    bool IsFinish() { return currentModelAnimation_->IsFinish(); }
    Model *GetModel() const { return pModel_; }

    Material *GetMaterial(uint32_t index)
    {
        return (index < materials_.size()) ? materials_[index].get() : nullptr;
    }
    Vector4 GetColor(int index = 0) { return color_[index].GetColor(); }

    /// <summary>
    /// setter
    /// </summary>
    /// <param name="position"></param>
    void SetModel(Model *pModel_) { this->pModel_ = pModel_; }
    void SetPosition(const Vector3 &position_) { this->position_ = position_; }
    void SetRotation(const Vector3 &rotation_) { this->rotation_ = rotation_; }
    void SetSize(const Vector3 &size_) { this->size_ = size_; }
    void SetModel(const std::string &filePath);
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

    /// <summary>
    /// ディファードのG-Bufferに載せてよいか。既定true。
    /// 半透明として見せたいオブジェクト（ブレンドはNormalのままアルファを下げて
    /// 透かしているものなど）は false にして前方描画へ逃がす。
    /// </summary>
    void SetUseDeferred(bool use) { useDeferred_ = use; }
    bool GetUseDeferred() const { return useDeferred_; }
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

    Vector3 ExtractTranslation(const Matrix4x4 &matrix)
    {
        return Vector3(matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]);
    }
};
} // namespace Hagine
