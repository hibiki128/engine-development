#define NOMINMAX
#include "Object3d.h"
#include "Debug/Log/Logger.h"
#include "DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Model/Mesh/Mesh.h"
#include "Object3dCommon.h"
#include "Transform/WorldTransform.h"
#include "cassert"
#include <Frame.h>
#include <Shadow/ShadowMap.h>
#include <line/DrawLine3D.h>
#include <myMath.h>
#include <type/Matrix4x4.h>

namespace Hagine {
void Object3d::Initialize() {
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize();

    dxCommon_ = DirectXCommon::GetInstance();

    lightGroup_ = LightGroup::GetInstance();

    CreateTransformationMatrix();
}

void Object3d::CreateModel(const std::string &filePath) {
    modelFilePath_ = filePath;

    // ベースモデルはデフォルトループ ON で登録
    animationLoopFlags_[modelFilePath_] = true;

    ModelManager::GetInstance()->LoadModel(modelFilePath_);

    // モデルを検索してセットする
    model_ = ModelManager::GetInstance()->FindModel(modelFilePath_);

    // マテリアル配列のサイズを調整
    materials_.resize(model_->GetModelData().materials.size());
    color_.resize(model_->GetModelData().materials.size());

    // 各マテリアルの初期化
    for (size_t i = 0; i < model_->GetModelData().materials.size(); ++i) {
        materials_[i] = std::make_unique<Material>();
        materials_[i]->Initialize();
        materials_[i]->GetMaterialData() = model_->GetModelData().materials[i];
        materials_[i]->LoadTexture();
        color_[i].Initialize();
        color_[i].SetColor(model_->GetModelData().materials[i].color);
    }

    if (model_->IsGltf()) {
        currentModelAnimation_ = std::make_unique<ModelAnimation>();
        currentModelAnimation_->SetModelData(model_->GetModelData());
        currentModelAnimation_->Initialize("resources/models/", modelFilePath_);

        model_->SetAnimator(currentModelAnimation_->GetAnimator());
        if (model_->GetModelData().hasBones) {
            model_->SetBone(currentModelAnimation_->GetBone());
            model_->SetSkin(currentModelAnimation_->GetSkin());
        }
    }
}

void Object3d::CreatePrimitiveModel(const PrimitiveType &type, std::string texPath) {
    model_ = ModelManager::GetInstance()->FindModel(ModelManager::GetInstance()->CreatePrimitiveModel(type, texPath));
    isPrimitive_ = true;
    materials_.resize(1);
    color_.resize(1);

    // マテリアルの初期化
    materials_[0] = std::make_unique<Material>();
    materials_[0]->Initialize();
    materials_[0]->PrimitiveInitialize(type);
    materials_[0]->GetMaterialData().textureFilePath = texPath;
    materials_[0]->LoadTexture();
    color_[0].Initialize();
    color_[0].SetColor({1.0f, 1.0f, 1.0f, 1.0f});
}

void Object3d::Update(const WorldTransform &worldTransform, const ViewProjection &viewProjection) {
    if (lightGroup_) {
        lightGroup_->Update(viewProjection);
    }

    // ローカル行列を作成
    Matrix4x4 localMatrix = MakeAffineMatrix(worldTransform.scale_, worldTransform.quateRotation_, worldTransform.translation_);

    // ワールド行列を計算（親がいる場合は親の行列と合成）
    Matrix4x4 worldMatrix = localMatrix;
    if (worldTransform.parent_) {
        worldMatrix = localMatrix * worldTransform.parent_->matWorld_;
    }

    Matrix4x4 worldViewProjectionMatrix;
    const Matrix4x4 &viewProjectionMatrix = viewProjection.matView_ * viewProjection.matProjection_;
    worldViewProjectionMatrix = worldMatrix * viewProjectionMatrix;
    Matrix4x4 worldInverseMatrix = Inverse(worldMatrix);

    if (!model_->GetModelData().hasAnimations) {
        transformationMatrixData_->WVP = worldViewProjectionMatrix;
        transformationMatrixData_->World = worldMatrix;
        transformationMatrixData_->WorldInverseTranspose = Transpose(worldInverseMatrix);
        transformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    } else {
        if (model_->GetModelData().hasBones) {
            transformationMatrixData_->WVP = worldViewProjectionMatrix;
            transformationMatrixData_->World = worldMatrix;
            transformationMatrixData_->WorldInverseTranspose = Transpose(worldInverseMatrix);
            transformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
        } else {
            Matrix4x4 localMat = model_->GetAnimator()->GetLocalMatrix();
            transformationMatrixData_->WVP = localMat * worldViewProjectionMatrix;
            transformationMatrixData_->World = localMat * worldMatrix;
            transformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
            transformationMatrixData_->LightWVP = localMat * worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
        }
    }

    if (model_ && model_->IsGltf()) {
        if (model_->GetModelData().hasAnimations) {
            objectCommon_->computeSkinningDrawCommonSetting();
            model_->Update();
        }
    }

    for (auto &color : color_) {
        color.TransferMatrix();
    }
}

void Object3d::Draw(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool reflect, bool lighting, bool modelDraw) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) {
        DrawShadow(worldTransform);
        return;
    }
    objectCommon_->SetBlendMode(blendMode_);
    Update(worldTransform, viewProjection);

    // アニメーション設定
    if (model_ && model_->IsGltf()) {
        if (model_->GetModelData().hasAnimations) {
            objectCommon_->skinningDrawCommonSetting();
        }
    }

    // 変換行列設定
    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

    // ライティング設定
    if (lightGroup_) {
        lightGroup_->Draw();
    }

    // モデル描画
    if (modelDraw) {
        if (model_) {
            model_->Draw(materials_, color_, lighting, reflect);
        }
    }
}

void Object3d::AnimationUpdate() {
    if (currentModelAnimation_) {
        // 基本的には modelFilePath_ に紐づくループフラグを使用するが、
        // 切り替え待機中（補間中）は、切り替え先（targetLoop_）の設定を優先する
        bool loop = true;
        if (isAnimationSwitchPending_) {
            loop = targetLoop_;
        } else {
            auto it = animationLoopFlags_.find(modelFilePath_);
            if (it != animationLoopFlags_.end()) {
                loop = it->second;
            }
        }

        currentModelAnimation_->Update(loop);

        // 補間完了後の切り替え処理
        if (isAnimationSwitchPending_) {
            Animator *currentAnimator = currentModelAnimation_->GetAnimator();

            // 補間が完了しているかチェック
            if (!currentAnimator->IsBlending()) {
                // ファイル名の比較（パスを除いた部分のみ比較）
                std::string currentFile = currentAnimator->GetCurrentFilename();
                std::string nextFile = nextAnimationFileName_;

                // アニメーター側のファイル情報がまだ切り替え先と異なる場合のみ更新する
                if (currentFile != nextFile) {
                    currentAnimator->UpdateCurrentFileInfo("resources/models/", nextFile);
                }

                // ループフラグは modelFilePath_ をキーに参照するため、
                // 補間完了後は必ず切り替え先のファイルへ更新しておく
                // （未更新だと常に旧ファイルのループ設定が使われ、全クリップがループしてしまう）
                modelFilePath_ = nextFile;

                // 切り替え完了フラグをリセット
                isAnimationSwitchPending_ = false;
                nextAnimationFileName_.clear();
            }
        }
    }
}

bool Object3d::IsAnimationBlending() const {
    if (currentModelAnimation_ && currentModelAnimation_->GetAnimator()) {
        return currentModelAnimation_->GetAnimator()->IsBlending();
    }
    return false;
}

void Object3d::SetAnimationImmediate(const std::string &fileName) {
    if (fileName == modelFilePath_) {
        return;
    }

    auto it = modelAnimations_.find(fileName);
    assert(it != modelAnimations_.end() && "Error: Animation file not found in modelAnimations_!");

    currentModelAnimation_ = it->second;
    currentModelAnimation_->GetAnimator()->SetAnimationTime(0.0f);
    currentModelAnimation_->GetAnimator()->SetIsAnimation(true);

    model_->SetAnimator(currentModelAnimation_->GetAnimator());
    model_->SetBone(currentModelAnimation_->GetBone());
    model_->SetSkin(currentModelAnimation_->GetSkin());

    modelFilePath_ = fileName;

    // 即時切り替え時は待機フラグを折る
    isAnimationSwitchPending_ = false;
    nextAnimationFileName_.clear();
}

void Object3d::SetAnimation(const std::string &animationFileName) {
    if (!currentModelAnimation_) {
        return;
    }

    Animator *animator = currentModelAnimation_->GetAnimator();
    if (!animator) {
        return;
    }

    // 現在のファイル名と比較
    std::string currentFile = animator->GetCurrentFilename();

    // 同じアニメーションの場合は何もしない
    if (currentFile == animationFileName && !isAnimationSwitchPending_) {
        return;
    }

    // 既に同じアニメーションへの切り替えが待機中の場合は何もしない
    if (isAnimationSwitchPending_ && nextAnimationFileName_ == animationFileName) {
        return;
    }

    // 新しいアニメーションのループ設定を予約しておく
    targetLoop_ = GetAnimationLoop(animationFileName);

    // 新しいアニメーションへの補間開始
    animator->BlendToAnimation("resources/models/", animationFileName, blendDuration_);

    // 切り替え待機状態にする
    isAnimationSwitchPending_ = true;
    nextAnimationFileName_ = animationFileName;
}

void Object3d::AddAnimation(const std::string &fileName, bool loop) {
    // ループフラグを登録（既存エントリも上書き更新）
    animationLoopFlags_[fileName] = loop;

    if (modelAnimations_.count(fileName) > 0) {
        return;
    }

    auto animation = std::make_unique<ModelAnimation>();

    animation->SetModelData(model_->GetModelData());
    animation->Initialize("resources/models/", fileName);
    animation->GetAnimator()->SetAnimationTime(0.0f);
    animation->SetSpeed(animationSpeed_);

    modelAnimations_.emplace(fileName, std::move(animation));
}

void Object3d::SetAnimationSpeed(float speed) {
    animationSpeed_ = speed;
    if (currentModelAnimation_) {
        currentModelAnimation_->SetSpeed(animationSpeed_);
    }
    // 全てのアニメーションに適用する場合
    for (auto &animation : modelAnimations_) {
        animation.second->SetSpeed(animationSpeed_);
    }
}

void Object3d::SetAnimationBlendDuration(float duration) {
    blendDuration_ = duration;
    if (currentModelAnimation_) {
        currentModelAnimation_->SetBlendDuration(blendDuration_);
    }
    // 全てのアニメーションに適用する場合
    for (auto &animation : modelAnimations_) {
        animation.second->SetBlendDuration(blendDuration_);
    }
}

void Object3d::SetAnimationLoop(const std::string &fileName, bool loop) {
    animationLoopFlags_[fileName] = loop;
}

bool Object3d::GetAnimationLoop(const std::string &fileName) {
    auto it = animationLoopFlags_.find(fileName);
    if (it != animationLoopFlags_.end()) {
        return it->second;
    }
    return true; // デフォルトはループあり
}

void Object3d::DrawWireframe(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool isRainbow) {
    // worldTransformを更新
    Update(worldTransform, viewProjection);
    if (!model_) {
        return;
    }

    const ModelData &modelData = model_->GetModelData();

    // ====== フラグで切り替え可能 ======
    bool gamingMode = isRainbow;

    // ====== 時間カウンター（時間ベースで変化）======
    static float timeCounter = 0.0f;
    timeCounter += Frame::DeltaTime() / 7.0f;
    if (timeCounter > 100.0f)
        timeCounter = 0.0f;

    // ====== HSV -> RGB変換関数 ======
    auto HSVtoRGB = [](float h, float s, float v) -> Vector4 {
        float c = v * s;
        float x = c * (1.0f - abs(fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;
        float r, g, b;

        if (h < 1.0f / 6.0f) {
            r = c;
            g = x;
            b = 0;
        } else if (h < 2.0f / 6.0f) {
            r = x;
            g = c;
            b = 0;
        } else if (h < 3.0f / 6.0f) {
            r = 0;
            g = c;
            b = x;
        } else if (h < 4.0f / 6.0f) {
            r = 0;
            g = x;
            b = c;
        } else if (h < 5.0f / 6.0f) {
            r = x;
            g = 0;
            b = c;
        } else {
            r = c;
            g = 0;
            b = x;
        }

        return {r + m, g + m, b + m, 1.0f};
    };

    // ====== グラデーション用関数（時間ベース） ======
    auto GetTimeGradientColor = [&](const Vector3 &worldPos) -> Vector4 {
        Vector4 clipPos = Transformation(Vector4{worldPos.x, worldPos.y, worldPos.z, 1.0f},
                                         viewProjection.matView_ * viewProjection.matProjection_);

        Vector2 ndc = {clipPos.x / clipPos.w, clipPos.y / clipPos.w};
        Vector2 screenUV = {(ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f};

        float distance = (screenUV.x + screenUV.y) / 2.0f;
        float hue = fmod(timeCounter + distance * 0.5f, 1.0f);
        return HSVtoRGB(hue, 1.0f, 1.0f);
    };

    for (const auto &mesh : modelData.meshes) {
        const std::vector<VertexData> &vertices = mesh.vertices;
        const std::vector<uint32_t> &indices = mesh.indices;

        auto drawTriangle = [&](const Vector3 &v0, const Vector3 &v1, const Vector3 &v2) {
            if (gamingMode) {
                Vector4 c0 = GetTimeGradientColor(v0);
                Vector4 c1 = GetTimeGradientColor(v1);
                Vector4 c2 = GetTimeGradientColor(v2);
                DrawLine3D::GetInstance()->SetPoints(v0, v1, c0);
                DrawLine3D::GetInstance()->SetPoints(v1, v2, c1);
                DrawLine3D::GetInstance()->SetPoints(v2, v0, c2);
            } else {
                Vector4 wireframeColor = {1.0f, 1.0f, 1.0f, 1.0f};
                DrawLine3D::GetInstance()->SetPoints(v0, v1, wireframeColor);
                DrawLine3D::GetInstance()->SetPoints(v1, v2, wireframeColor);
                DrawLine3D::GetInstance()->SetPoints(v2, v0, wireframeColor);
            }
        };

        if (indices.empty()) {
            for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                // 頂点をワールド行列で変換
                Vector4 v0_4 = Transformation(Vector4{vertices[i].position.x, vertices[i].position.y, vertices[i].position.z, 1.0f}, transformationMatrixData_->World);
                Vector4 v1_4 = Transformation(Vector4{vertices[i + 1].position.x, vertices[i + 1].position.y, vertices[i + 1].position.z, 1.0f}, transformationMatrixData_->World);
                Vector4 v2_4 = Transformation(Vector4{vertices[i + 2].position.x, vertices[i + 2].position.y, vertices[i + 2].position.z, 1.0f}, transformationMatrixData_->World);

                drawTriangle({v0_4.x, v0_4.y, v0_4.z}, {v1_4.x, v1_4.y, v1_4.z}, {v2_4.x, v2_4.y, v2_4.z});
            }
        } else {
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                uint32_t idx0 = indices[i];
                uint32_t idx1 = indices[i + 1];
                uint32_t idx2 = indices[i + 2];

                if (idx0 >= vertices.size() || idx1 >= vertices.size() || idx2 >= vertices.size())
                    continue;

                // 頂点をワールド行列で変換
                Vector4 v0_4 = Transformation(Vector4{vertices[idx0].position.x, vertices[idx0].position.y, vertices[idx0].position.z, 1.0f}, transformationMatrixData_->World);
                Vector4 v1_4 = Transformation(Vector4{vertices[idx1].position.x, vertices[idx1].position.y, vertices[idx1].position.z, 1.0f}, transformationMatrixData_->World);
                Vector4 v2_4 = Transformation(Vector4{vertices[idx2].position.x, vertices[idx2].position.y, vertices[idx2].position.z, 1.0f}, transformationMatrixData_->World);

                drawTriangle({v0_4.x, v0_4.y, v0_4.z}, {v1_4.x, v1_4.y, v1_4.z}, {v2_4.x, v2_4.y, v2_4.z});
            }
        }
    }
}

void Object3d::DrawSkeleton(const WorldTransform &worldTransform, const ViewProjection &viewProjection) {
    const Skeleton &skeleton = currentModelAnimation_->GetSkeletonData();

    // モデルに適用されているワールド変換を生成
    Matrix4x4 worldMatrix = MakeAffineMatrix(
        worldTransform.scale_,
        worldTransform.quateRotation_,
        worldTransform.translation_);

    if (worldTransform.parent_) {
        worldMatrix *= worldTransform.parent_->matWorld_;
    }

    for (const auto &joint : skeleton.joints) {
        // ローカル座標 → ワールド座標に変換
        Matrix4x4 jointWorldMat = joint.skeletonSpaceMatrix * worldMatrix;
        Vector3 jointPosition = ExtractTranslation(jointWorldMat);

        // Jointの大きさに応じて半径を決定
        float jointRadius = 0.03f * worldTransform.scale_.x;

        Vector4 jointColor = {0.8f, 0.2f, 0.2f, 1.0f};
        DrawLine3D::GetInstance()->DrawSphere(jointPosition, jointColor, jointRadius, 8);

        if (!joint.parent.has_value()) {
            continue;
        }

        const auto &parentJoint = skeleton.joints[*joint.parent];
        Matrix4x4 parentWorldMat = parentJoint.skeletonSpaceMatrix * worldMatrix;
        Vector3 parentPosition = ExtractTranslation(parentWorldMat);

        // アーマチュアの描画
        DrawBoneArmature(parentPosition, jointPosition, worldTransform.scale_.x);
    }
}

void Object3d::DrawBoneArmature(const Vector3 &parentPos, const Vector3 &childPos, float scale) {
    // ボーンの方向ベクトル
    Vector3 boneDirection = childPos - parentPos;
    float boneLength = boneDirection.Length();

    if (boneLength < 0.001f)
        return; // 長さが短すぎる場合はスキップ

    // ボーンの太さ（長さに応じてスケーリング）
    float baseWidth = boneLength * 0.1f * scale; // 基部の太さ
    float tipWidth = boneLength * 0.02f * scale; // 先端の太さ

    // 最小・最大の太さを制限
    baseWidth = std::max(0.02f, std::min(baseWidth, 0.15f * scale / 5.0f));
    tipWidth = std::max(0.005f, std::min(tipWidth, 0.05f * scale / 5.0f));

    // ボーンの色
    Vector4 boneColor = {0.2f, 0.6f, 1.0f, 1.0f}; // 青系の色

    // アーマチュア形状を描画
    DrawArmatureShape(parentPos, childPos, baseWidth, tipWidth, boneColor);
}

void Object3d::DrawArmatureShape(const Vector3 &startPos, const Vector3 &endPos,
                                 float baseWidth, float tipWidth, const Vector4 &color) {
    // ボーンの方向ベクトル
    Vector3 direction = endPos - startPos;
    float length = direction.Length();

    if (length < 0.001f)
        return;

    Vector3 normalizedDir = direction.Normalize();

    // 垂直なベクトルを2つ作成（ボーンの断面用）
    Vector3 up = {0.0f, 1.0f, 0.0f};
    if (std::abs(normalizedDir.Dot(up)) > 0.9f) {
        up = {1.0f, 0.0f, 0.0f}; // 方向がY軸に近い場合はX軸を使用
    }

    Vector3 right = (normalizedDir.Cross(up)).Normalize();
    up = (right.Cross(normalizedDir)).Normalize();

    // 分割数
    const int segments = 8;       // 断面の分割数
    const int lengthSegments = 4; // 長さ方向の分割数

    // 各セグメントでボーンを描画
    for (int i = 0; i < lengthSegments; i++) {
        float t1 = (float)i / lengthSegments;
        float t2 = (float)(i + 1) / lengthSegments;

        // 現在の位置と次の位置
        Vector3 pos1 = startPos + direction * t1;
        Vector3 pos2 = startPos + direction * t2;

        // 現在の太さ（線形補間で基部から先端に向かって細くなる）
        float width1 = baseWidth * (1.0f - t1) + tipWidth * t1;
        float width2 = baseWidth * (1.0f - t2) + tipWidth * t2;

        // 断面の円を描画
        for (int j = 0; j < segments; j++) {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            // 現在のセグメントの円周上の点
            Vector3 p1_1 = pos1 + (right * cosf(angle1) + up * sinf(angle1)) * width1;
            Vector3 p1_2 = pos1 + (right * cosf(angle2) + up * sinf(angle2)) * width1;
            Vector3 p2_1 = pos2 + (right * cosf(angle1) + up * sinf(angle1)) * width2;
            Vector3 p2_2 = pos2 + (right * cosf(angle2) + up * sinf(angle2)) * width2;

            // 円周の線
            DrawLine3D::GetInstance()->SetPoints(p1_1, p1_2, color);
            DrawLine3D::GetInstance()->SetPoints(p2_1, p2_2, color);

            // 縦の線（長さ方向）
            DrawLine3D::GetInstance()->SetPoints(p1_1, p2_1, color);

            // 最後のセグメントの場合、先端を中心点に収束させる
            if (i == lengthSegments - 1) {
                DrawLine3D::GetInstance()->SetPoints(p2_1, endPos, color);
            }
        }
    }

    // 基部から中心軸への線も描画（強調用）
    DrawLine3D::GetInstance()->SetPoints(startPos, endPos, {color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, color.w});
}

void Object3d::SetModel(const std::string &filePath) {
    // モデルを検索してセットする
    ModelManager::GetInstance()->LoadModel(filePath);
    model_ = ModelManager::GetInstance()->FindModel(filePath);

    if (model_->IsGltf()) {
        currentModelAnimation_->SetModelData(model_->GetModelData());
        currentModelAnimation_->Initialize("resources/models/", filePath);

        model_->SetAnimator(currentModelAnimation_->GetAnimator());
        model_->SetBone(currentModelAnimation_->GetBone());
        model_->SetSkin(currentModelAnimation_->GetSkin());
    }
}

void Object3d::DrawShadow(const WorldTransform &worldTransform) {
    if (!model_) return;

    Matrix4x4 localMatrix = MakeAffineMatrix(worldTransform.scale_, worldTransform.quateRotation_, worldTransform.translation_);
    Matrix4x4 worldMatrix = localMatrix;
    if (worldTransform.parent_) {
        worldMatrix = localMatrix * worldTransform.parent_->matWorld_;
    }

    if (!model_->GetModelData().hasAnimations) {
        transformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    } else if (model_->GetModelData().hasBones) {
        transformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    } else {
        transformationMatrixData_->LightWVP = model_->GetAnimator()->GetLocalMatrix() * worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    }

    PipeLineManager::GetInstance()->DrawCommonSetting(PipelineType::kShadowMap);
    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(0, transformationMatrixResource_->GetGPUVirtualAddress());

    if (model_) {
        model_->DrawShadow();
    }
}

void Object3d::CreateTransformationMatrix() {

    transformationMatrixResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    // 書き込むかめのアドレスを取得
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void **>(&transformationMatrixData_));
    // 単位行列を書き込んでおく
    transformationMatrixData_->WVP = MakeIdentity4x4();
    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
    transformationMatrixData_->LightWVP = MakeIdentity4x4();
}

void Object3d::CreateIndependentMaterials() {
    if (!model_)
        return;

    size_t materialCount = materials_.size();
    materials_.clear();
    materials_.resize(materialCount);

    for (size_t i = 0; i < materialCount; ++i) {
        materials_[i] = std::make_unique<Material>();
        materials_[i]->Initialize();

        // 元のマテリアルデータをコピー
        const Material *originalMaterial = GetMaterial(uint32_t(i));
        if (originalMaterial) {
            materials_[i]->GetMaterialData() = originalMaterial->GetMaterialData();
            materials_[i]->LoadTexture();
        }
    }
}

void Object3d::SetTexture(const std::string &filePath, uint32_t materialIndex) {
    if (materialIndex < materials_.size()) {
        materials_[materialIndex]->SetTexture(filePath);
    }
}

void Object3d::SetEnvironmentCoefficients(float value) {
    for (auto &material : materials_) {
        material->SetEnvironmentCoefficients(value);
    }
}
} // namespace Hagine
