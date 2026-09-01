#define NOMINMAX
#include "Object3d.h"
#include <asset/AssetPath.h>
#include "debug/log/Logger.h"
#include "DirectXCommon.h"
#include "graphics/model/ModelManager.h"
#include "model/mesh/Mesh.h"
#include "Object3dCommon.h"
#include "transform/WorldTransform.h"
#include "cassert"
#include <cstring>
#include <Frame.h>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>
#include <line/LineRenderer.h>
#include <MyMath.h>
#include <type/Matrix4x4.h>

namespace Hagine {
void Object3d::Initialize()
{
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize();

    pDxCommon_ = DirectXCommon::GetInstance();

    pLightGroup_ = LightGroup::GetInstance();

    CreateTransformationMatrix();
}

void Object3d::CreateModel(const std::string &filePath)
{
    modelFilePath_ = filePath;

    // ベースモデルはデフォルトループ ON で登録
    animationLoopFlags_[modelFilePath_] = true;

    ModelManager::GetInstance()->LoadModel(modelFilePath_);

    // モデルを検索してセットする
    pModel_ = ModelManager::GetInstance()->FindModel(modelFilePath_);

    // マテリアル配列のサイズを調整
    materials_.resize(pModel_->GetModelData().materials.size());
    color_.resize(pModel_->GetModelData().materials.size());

    // 各マテリアルの初期化
    for (size_t i = 0; i < pModel_->GetModelData().materials.size(); ++i)
    {
        materials_[i] = std::make_unique<Material>();
        materials_[i]->Initialize();
        materials_[i]->GetMaterialData() = pModel_->GetModelData().materials[i];
        materials_[i]->LoadTexture();
        color_[i].Initialize();
        color_[i].SetColor(pModel_->GetModelData().materials[i].color);
    }

    if (pModel_->IsGltf())
    {
        currentModelAnimation_ = std::make_unique<ModelAnimation>();
        currentModelAnimation_->SetModelData(pModel_->GetModelData());
        currentModelAnimation_->Initialize(AssetPath::ModelsRoot(modelFilePath_), modelFilePath_);

        pModel_->SetAnimator(currentModelAnimation_->GetAnimator());
        if (pModel_->GetModelData().hasBones)
        {
            pModel_->SetBone(currentModelAnimation_->GetBone());
            pModel_->SetSkin(currentModelAnimation_->GetSkin());
        }
    }
}

void Object3d::CreatePrimitiveModel(const PrimitiveType &type, std::string texPath)
{
    pModel_ = ModelManager::GetInstance()->FindModel(ModelManager::GetInstance()->CreatePrimitiveModel(type, texPath));
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

void Object3d::Update(const WorldTransform &worldTransform, const ViewProjection &viewProjection)
{
    if (pLightGroup_)
    {
        pLightGroup_->Update(viewProjection);
    }

    // ローカル行列を作成
    Matrix4x4 localMatrix = MakeAffineMatrix(worldTransform.scale_, worldTransform.quaternionRotation_, worldTransform.translation_);

    // ワールド行列を計算（親がいる場合は親の行列と合成）
    Matrix4x4 worldMatrix = localMatrix;
    if (worldTransform.pParent_)
    {
        worldMatrix = localMatrix * worldTransform.pParent_->matWorld_;
    }

    Matrix4x4 worldViewProjectionMatrix;
    const Matrix4x4 &viewProjectionMatrix = viewProjection.matView_ * viewProjection.matProjection_;
    worldViewProjectionMatrix = worldMatrix * viewProjectionMatrix;
    Matrix4x4 worldInverseMatrix = Inverse(worldMatrix);

    if (!pModel_->GetModelData().hasAnimations)
    {
        pTransformationMatrixData_->WVP = worldViewProjectionMatrix;
        pTransformationMatrixData_->World = worldMatrix;
        pTransformationMatrixData_->WorldInverseTranspose = Transpose(worldInverseMatrix);
        pTransformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    }
    else
    {
        if (pModel_->GetModelData().hasBones)
        {
            pTransformationMatrixData_->WVP = worldViewProjectionMatrix;
            pTransformationMatrixData_->World = worldMatrix;
            pTransformationMatrixData_->WorldInverseTranspose = Transpose(worldInverseMatrix);
            pTransformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
        }
        else
        {
            Matrix4x4 localMat = pModel_->GetAnimator()->GetLocalMatrix();
            pTransformationMatrixData_->WVP = localMat * worldViewProjectionMatrix;
            pTransformationMatrixData_->World = localMat * worldMatrix;
            pTransformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
            pTransformationMatrixData_->LightWVP = localMat * worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
        }
    }

    if (pModel_ && pModel_->IsGltf())
    {
        // 影パスで既にこのフレームのスキニングを済ませていれば再実行しない
        // （同一フレームのポーズは不変なので出力は同じ）
        if (pModel_->GetModelData().hasAnimations && !skinnedThisFrame_)
        {
            objectCommon_->computeSkinningDrawCommonSetting();
            pModel_->Update();
            skinnedThisFrame_ = true;
        }
    }

    for (auto &color : color_)
    {
        color.TransferMatrix();
    }
}

void Object3d::Draw(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool reflect, bool lighting, bool modelDraw)
{
    if (ShadowMap::GetInstance()->IsShadowPassActive())
    {
        DrawShadow(worldTransform);
        return;
    }

    // ── ディファードの振り分け ──
    // G-Buffer に載せられるのは「不透明かつライティングあり」のものだけ。
    // 半透明（加算合成など）やライティング無効のものは従来どおり前方描画に残す。
    DeferredRenderer *deferred = DeferredRenderer::GetInstance();
    const bool deferredEligible = deferred->IsEnabled() && lighting && useDeferred_ &&
                                  (blendMode_ == BlendMode::None || blendMode_ == BlendMode::Normal);
    if (deferred->IsGBufferPassActive())
    {
        // G-Buffer パス中: 対象外のものは前方描画のフェーズで描く
        if (!deferredEligible)
        {
            return;
        }
    }
    else if (deferredEligible)
    {
        // 前方描画フェーズ: G-Buffer で描き済みなので二重に描かない
        return;
    }

    objectCommon_->SetBlendMode(blendMode_);
    Update(worldTransform, viewProjection);

    // アニメーション設定
    if (pModel_ && pModel_->IsGltf())
    {
        if (pModel_->GetModelData().hasAnimations)
        {
            objectCommon_->skinningDrawCommonSetting();
        }
    }

    // G-Buffer パス中は PSO だけ差し替える（ルートシグネチャは前方描画と共通なので
    // 以降のバインド処理は一切変えなくてよい）
    if (deferred->IsGBufferPassActive())
    {
        const bool skinned = pModel_ && pModel_->IsGltf() && pModel_->GetModelData().hasAnimations;
        PipelineManager::GetInstance()->DrawCommonSetting(
            skinned ? PipelineType::GBufferSkinning : PipelineType::GBuffer);
    }

    // 変換行列設定（頂点シェーダーの b0）
    {
        const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
        assert(rootSignature && "オブジェクト描画のルートシグネチャが未生成です");
        pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
            rootSignature->GetCbvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX),
            transformationMatrixResource_->GetGPUVirtualAddress());
    }

    // ライティング設定
    if (pLightGroup_)
    {
        pLightGroup_->Draw();
    }

    // モデル描画
    if (modelDraw)
    {
        if (pModel_)
        {
            pModel_->Draw(materials_, color_, lighting, reflect);
        }
    }
}

void Object3d::AnimationUpdate()
{
    // 新しいフレームの開始。影パス／本描画のどちらか最初の1回だけスキニングする
    skinnedThisFrame_ = false;

    if (currentModelAnimation_)
    {
        // 基本的には modelFilePath_ に紐づくループフラグを使用するが、
        // 切り替え待機中（補間中）は、切り替え先（targetLoop_）の設定を優先する
        bool loop = true;
        if (isAnimationSwitchPending_)
        {
            loop = targetLoop_;
        }
        else
        {
            auto it = animationLoopFlags_.find(modelFilePath_);
            if (it != animationLoopFlags_.end())
            {
                loop = it->second;
            }
        }

        currentModelAnimation_->Update(loop);

        // 補間完了後の切り替え処理
        if (isAnimationSwitchPending_)
        {
            Animator *currentAnimator = currentModelAnimation_->GetAnimator();

            // 補間が完了しているかチェック
            if (!currentAnimator->IsBlending())
            {
                // ファイル名の比較（パスを除いた部分のみ比較）
                std::string currentFile = currentAnimator->GetCurrentFilename();
                std::string nextFile = nextAnimationFileName_;

                // アニメーター側のファイル情報がまだ切り替え先と異なる場合のみ更新する
                if (currentFile != nextFile)
                {
                    currentAnimator->UpdateCurrentFileInfo(AssetPath::ModelsRoot(nextFile), nextFile);
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

bool Object3d::IsAnimationBlending() const
{
    if (currentModelAnimation_ && currentModelAnimation_->GetAnimator())
    {
        return currentModelAnimation_->GetAnimator()->IsBlending();
    }
    return false;
}

void Object3d::SetAnimationImmediate(const std::string &fileName)
{
    if (fileName == modelFilePath_)
    {
        return;
    }

    auto it = modelAnimations_.find(fileName);
    assert(it != modelAnimations_.end() && "Error: Animation file not found in modelAnimations_!");

    currentModelAnimation_ = it->second;
    currentModelAnimation_->GetAnimator()->SetAnimationTime(0.0f);
    currentModelAnimation_->GetAnimator()->SetIsAnimation(true);

    pModel_->SetAnimator(currentModelAnimation_->GetAnimator());
    pModel_->SetBone(currentModelAnimation_->GetBone());
    pModel_->SetSkin(currentModelAnimation_->GetSkin());

    modelFilePath_ = fileName;

    // 即時切り替え時は待機フラグを折る
    isAnimationSwitchPending_ = false;
    nextAnimationFileName_.clear();
}

void Object3d::SetAnimation(const std::string &animationFileName)
{
    if (!currentModelAnimation_)
    {
        return;
    }

    Animator *pAnimator = currentModelAnimation_->GetAnimator();
    if (!pAnimator)
    {
        return;
    }

    // 現在のファイル名と比較
    std::string currentFile = pAnimator->GetCurrentFilename();

    // 同じアニメーションの場合は何もしない
    if (currentFile == animationFileName && !isAnimationSwitchPending_)
    {
        return;
    }

    // 既に同じアニメーションへの切り替えが待機中の場合は何もしない
    if (isAnimationSwitchPending_ && nextAnimationFileName_ == animationFileName)
    {
        return;
    }

    // 新しいアニメーションのループ設定を予約しておく
    targetLoop_ = GetAnimationLoop(animationFileName);

    // 新しいアニメーションへの補間開始
    pAnimator->BlendToAnimation(AssetPath::ModelsRoot(animationFileName), animationFileName, blendDuration_);

    // 切り替え待機状態にする
    isAnimationSwitchPending_ = true;
    nextAnimationFileName_ = animationFileName;
}

void Object3d::PlayLayerAnimation(const std::string &animationFileName, const std::string &maskRootJoint,
                                  bool loop, float fadeDuration)
{
    if (!currentModelAnimation_)
    {
        return;
    }
    currentModelAnimation_->PlayLayerAnimation(AssetPath::ModelsRoot(animationFileName), animationFileName,
                                               maskRootJoint, loop, fadeDuration);
}

void Object3d::StopLayerAnimation(float fadeDuration)
{
    if (!currentModelAnimation_)
    {
        return;
    }
    currentModelAnimation_->StopLayerAnimation(fadeDuration);
}

bool Object3d::IsLayerAnimationPlaying() const
{
    return currentModelAnimation_ && currentModelAnimation_->IsLayerPlaying();
}

void Object3d::AddAnimation(const std::string &fileName, bool loop)
{
    // ループフラグを登録（既存エントリも上書き更新）
    animationLoopFlags_[fileName] = loop;

    if (modelAnimations_.count(fileName) > 0)
    {
        return;
    }

    auto animation = std::make_unique<ModelAnimation>();

    animation->SetModelData(pModel_->GetModelData());
    animation->Initialize(AssetPath::ModelsRoot(fileName), fileName);
    animation->GetAnimator()->SetAnimationTime(0.0f);
    animation->SetSpeed(animationSpeed_);

    modelAnimations_.emplace(fileName, std::move(animation));
}

void Object3d::SetAnimationSpeed(float speed)
{
    animationSpeed_ = speed;
    if (currentModelAnimation_)
    {
        currentModelAnimation_->SetSpeed(animationSpeed_);
    }
    // 全てのアニメーションに適用する場合
    for (auto &animation : modelAnimations_)
    {
        animation.second->SetSpeed(animationSpeed_);
    }
}

void Object3d::SetAnimationBlendDuration(float duration)
{
    blendDuration_ = duration;
    if (currentModelAnimation_)
    {
        currentModelAnimation_->SetBlendDuration(blendDuration_);
    }
    // 全てのアニメーションに適用する場合
    for (auto &animation : modelAnimations_)
    {
        animation.second->SetBlendDuration(blendDuration_);
    }
}

void Object3d::SetAnimationLoop(const std::string &fileName, bool loop)
{
    animationLoopFlags_[fileName] = loop;
}

bool Object3d::GetAnimationLoop(const std::string &fileName)
{
    auto it = animationLoopFlags_.find(fileName);
    if (it != animationLoopFlags_.end())
    {
        return it->second;
    }
    return true; // デフォルトはループあり
}

void Object3d::DrawWireframe(const WorldTransform &worldTransform, const ViewProjection &viewProjection, bool isRainbow)
{
    // worldTransformを更新
    Update(worldTransform, viewProjection);
    if (!pModel_)
    {
        return;
    }

    const ModelData &modelData = pModel_->GetModelData();

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

        if (h < 1.0f / 6.0f)
        {
            r = c;
            g = x;
            b = 0;
        }
        else if (h < 2.0f / 6.0f)
        {
            r = x;
            g = c;
            b = 0;
        }
        else if (h < 3.0f / 6.0f)
        {
            r = 0;
            g = c;
            b = x;
        }
        else if (h < 4.0f / 6.0f)
        {
            r = 0;
            g = x;
            b = c;
        }
        else if (h < 5.0f / 6.0f)
        {
            r = x;
            g = 0;
            b = c;
        }
        else
        {
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

    for (const auto &mesh : modelData.meshes)
    {
        const std::vector<VertexData> &vertices = mesh.vertices;
        const std::vector<uint32_t> &indices = mesh.indices;

        // 三角形ごとに3本積むので本数が多い。シングルトン参照と色詰めはループ外へ出しておく
        LineRenderer *pLine = LineRenderer::GetInstance();
        constexpr uint32_t kWhite = 0xFFFFFFFFu;

        auto drawTriangle = [&](const Vector3 &v0, const Vector3 &v1, const Vector3 &v2) {
            if (gamingMode)
            {
                pLine->AddLinePacked(v0, v1, PackLineColor(GetTimeGradientColor(v0)));
                pLine->AddLinePacked(v1, v2, PackLineColor(GetTimeGradientColor(v1)));
                pLine->AddLinePacked(v2, v0, PackLineColor(GetTimeGradientColor(v2)));
            }
            else
            {
                pLine->AddLinePacked(v0, v1, kWhite);
                pLine->AddLinePacked(v1, v2, kWhite);
                pLine->AddLinePacked(v2, v0, kWhite);
            }
        };

        if (indices.empty())
        {
            for (size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                // 頂点をワールド行列で変換
                Vector4 v0_4 = Transformation(Vector4{vertices[i].position.x, vertices[i].position.y, vertices[i].position.z, 1.0f}, pTransformationMatrixData_->World);
                Vector4 v1_4 = Transformation(Vector4{vertices[i + 1].position.x, vertices[i + 1].position.y, vertices[i + 1].position.z, 1.0f}, pTransformationMatrixData_->World);
                Vector4 v2_4 = Transformation(Vector4{vertices[i + 2].position.x, vertices[i + 2].position.y, vertices[i + 2].position.z, 1.0f}, pTransformationMatrixData_->World);

                drawTriangle({v0_4.x, v0_4.y, v0_4.z}, {v1_4.x, v1_4.y, v1_4.z}, {v2_4.x, v2_4.y, v2_4.z});
            }
        }
        else
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                uint32_t idx0 = indices[i];
                uint32_t idx1 = indices[i + 1];
                uint32_t idx2 = indices[i + 2];

                if (idx0 >= vertices.size() || idx1 >= vertices.size() || idx2 >= vertices.size())
                    continue;

                // 頂点をワールド行列で変換
                Vector4 v0_4 = Transformation(Vector4{vertices[idx0].position.x, vertices[idx0].position.y, vertices[idx0].position.z, 1.0f}, pTransformationMatrixData_->World);
                Vector4 v1_4 = Transformation(Vector4{vertices[idx1].position.x, vertices[idx1].position.y, vertices[idx1].position.z, 1.0f}, pTransformationMatrixData_->World);
                Vector4 v2_4 = Transformation(Vector4{vertices[idx2].position.x, vertices[idx2].position.y, vertices[idx2].position.z, 1.0f}, pTransformationMatrixData_->World);

                drawTriangle({v0_4.x, v0_4.y, v0_4.z}, {v1_4.x, v1_4.y, v1_4.z}, {v2_4.x, v2_4.y, v2_4.z});
            }
        }
    }
}

void Object3d::DrawSkeleton(const WorldTransform &worldTransform, const ViewProjection &viewProjection)
{
    const Skeleton &skeleton = currentModelAnimation_->GetSkeletonData();

    // モデルに適用されているワールド変換を生成
    Matrix4x4 worldMatrix = MakeAffineMatrix(
        worldTransform.scale_,
        worldTransform.quaternionRotation_,
        worldTransform.translation_);

    if (worldTransform.pParent_)
    {
        worldMatrix *= worldTransform.pParent_->matWorld_;
    }

    for (const auto &joint : skeleton.joints)
    {
        // ローカル座標 → ワールド座標に変換
        Matrix4x4 jointWorldMat = joint.skeletonSpaceMatrix * worldMatrix;
        Vector3 jointPosition = ExtractTranslation(jointWorldMat);

        // Jointの大きさに応じて半径を決定
        float jointRadius = 0.03f * worldTransform.scale_.x;

        Vector4 jointColor = {0.8f, 0.2f, 0.2f, 1.0f};
        LineRenderer::GetInstance()->AddSphere(jointPosition, jointRadius, jointColor, 8);

        if (!joint.parent.has_value())
        {
            continue;
        }

        const auto &parentJoint = skeleton.joints[*joint.parent];
        Matrix4x4 parentWorldMat = parentJoint.skeletonSpaceMatrix * worldMatrix;
        Vector3 parentPosition = ExtractTranslation(parentWorldMat);

        // アーマチュアの描画
        DrawBoneArmature(parentPosition, jointPosition, worldTransform.scale_.x);
    }
}

void Object3d::DrawBoneArmature(const Vector3 &parentPos, const Vector3 &childPos, float scale)
{
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
                                 float baseWidth, float tipWidth, const Vector4 &color)
{
    // ボーンの方向ベクトル
    Vector3 direction = endPos - startPos;
    float length = direction.Length();

    if (length < 0.001f)
        return;

    Vector3 normalizedDir = direction.Normalize();

    // 垂直なベクトルを2つ作成（ボーンの断面用）
    Vector3 up = {0.0f, 1.0f, 0.0f};
    if (std::abs(normalizedDir.Dot(up)) > 0.9f)
    {
        up = {1.0f, 0.0f, 0.0f}; // 方向がY軸に近い場合はX軸を使用
    }

    Vector3 right = (normalizedDir.Cross(up)).Normalize();
    up = (right.Cross(normalizedDir)).Normalize();

    // 分割数
    const int segments = 8;       // 断面の分割数
    const int lengthSegments = 4; // 長さ方向の分割数

    // 各セグメントでボーンを描画
    for (int i = 0; i < lengthSegments; i++)
    {
        float t1 = static_cast<float>(i) / lengthSegments;
        float t2 = static_cast<float>(i + 1) / lengthSegments;

        // 現在の位置と次の位置
        Vector3 pos1 = startPos + direction * t1;
        Vector3 pos2 = startPos + direction * t2;

        // 現在の太さ（線形補間で基部から先端に向かって細くなる）
        float width1 = baseWidth * (1.0f - t1) + tipWidth * t1;
        float width2 = baseWidth * (1.0f - t2) + tipWidth * t2;

        // 断面の円を描画
        for (int j = 0; j < segments; j++)
        {
            float angle1 = static_cast<float>(j) / segments * 2.0f * 3.14159f;
            float angle2 = static_cast<float>(j + 1) / segments * 2.0f * 3.14159f;

            // 現在のセグメントの円周上の点
            Vector3 p1_1 = pos1 + (right * cosf(angle1) + up * sinf(angle1)) * width1;
            Vector3 p1_2 = pos1 + (right * cosf(angle2) + up * sinf(angle2)) * width1;
            Vector3 p2_1 = pos2 + (right * cosf(angle1) + up * sinf(angle1)) * width2;
            Vector3 p2_2 = pos2 + (right * cosf(angle2) + up * sinf(angle2)) * width2;

            // 円周の線
            LineRenderer::GetInstance()->AddLine(p1_1, p1_2, color);
            LineRenderer::GetInstance()->AddLine(p2_1, p2_2, color);

            // 縦の線（長さ方向）
            LineRenderer::GetInstance()->AddLine(p1_1, p2_1, color);

            // 最後のセグメントの場合、先端を中心点に収束させる
            if (i == lengthSegments - 1)
            {
                LineRenderer::GetInstance()->AddLine(p2_1, endPos, color);
            }
        }
    }

    // 基部から中心軸への線も描画（強調用）
    LineRenderer::GetInstance()->AddLine(startPos, endPos, {color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, color.w});
}

void Object3d::SetModel(const std::string &filePath)
{
    // モデルを検索してセットする
    ModelManager::GetInstance()->LoadModel(filePath);
    pModel_ = ModelManager::GetInstance()->FindModel(filePath);

    if (pModel_->IsGltf())
    {
        currentModelAnimation_->SetModelData(pModel_->GetModelData());
        currentModelAnimation_->Initialize(AssetPath::ModelsRoot(filePath), filePath);

        pModel_->SetAnimator(currentModelAnimation_->GetAnimator());
        pModel_->SetBone(currentModelAnimation_->GetBone());
        pModel_->SetSkin(currentModelAnimation_->GetSkin());
    }
}

bool Object3d::CanBatchInstanced() const
{
    if (!pModel_)
    {
        return false;
    }
    // スキニングは「スキニング結果の頂点バッファ」がモデル側の1本しかないため、
    // 同じモデルを参照する複数オブジェクトを1回の描画にまとめると全員同じポーズになる。
    if (pModel_->GetModelData().hasAnimations)
    {
        return false;
    }
    // マテリアルが1つも無い（＝バインドできない）ものは対象外
    if (materials_.empty() || color_.empty())
    {
        return false;
    }
    return true;
}

bool Object3d::ShouldDrawInCurrentPass(bool lighting) const
{
    if (ShadowMap::GetInstance()->IsShadowPassActive())
    {
        return true; // 影パスは全オブジェクトが対象
    }
    // Draw() の振り分けと同じ: G-Buffer に載せられるのは不透明かつライティングありのものだけ。
    DeferredRenderer *deferred = DeferredRenderer::GetInstance();
    const bool deferredEligible = deferred->IsEnabled() && lighting && useDeferred_ &&
                                  (blendMode_ == BlendMode::None || blendMode_ == BlendMode::Normal);
    if (deferred->IsGBufferPassActive())
    {
        return deferredEligible;
    }
    // 前方描画フェーズ: G-Buffer で描き済みのものは描かない
    return !deferredEligible;
}

size_t Object3d::ComputeBatchSignature(bool reflect, bool lighting) const
{
    size_t hash = 0;
    auto mix = [&hash](size_t value) {
        hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    };

    // 頂点/インデックスバッファが同一であること＝同じモデル実体であること
    mix(std::hash<const void *>{}(pModel_));
    mix(static_cast<size_t>(blendMode_));
    mix(lighting ? 1u : 0u);
    mix(reflect ? 1u : 0u);
    mix(useDeferred_ ? 1u : 0u);
    // マテリアルは1本の定数バッファとテクスチャを共有するので全て一致している必要がある
    for (const auto &material : materials_)
    {
        mix(material->ComputeDrawSignature());
    }
    // 色は「マテリアルが1つのときだけ」インスタンスごとに渡せる（頂点シェーダーの instanceColor は
    // 個体単位でマテリアル単位ではないため）。複数マテリアルの場合は色も一致していないとまとめられない。
    if (!UsesInstanceColor())
    {
        for (const auto &objColor : color_)
        {
            const Vector4 c = objColor.GetColor();
            auto mixFloat = [&mix](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                mix(static_cast<size_t>(bits));
            };
            mixFloat(c.x);
            mixFloat(c.y);
            mixFloat(c.z);
            mixFloat(c.w);
        }
    }
    return hash;
}

void Object3d::BuildInstanceMatrices(const WorldTransform &worldTransform, const ViewProjection &viewProjection,
                                     Matrix4x4 &outWVP, Matrix4x4 &outWorld,
                                     Matrix4x4 &outWorldInverseTranspose, Matrix4x4 &outLightWVP) const
{
    // Update() の非アニメーション経路と同じ計算（バッチ対象はアニメーション無しに限定済み）
    Matrix4x4 localMatrix = MakeAffineMatrix(worldTransform.scale_, worldTransform.quaternionRotation_, worldTransform.translation_);
    Matrix4x4 worldMatrix = localMatrix;
    if (worldTransform.pParent_)
    {
        worldMatrix = localMatrix * worldTransform.pParent_->matWorld_;
    }

    outWorld = worldMatrix;
    outWVP = worldMatrix * (viewProjection.matView_ * viewProjection.matProjection_);
    outWorldInverseTranspose = Transpose(Inverse(worldMatrix));
    outLightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
}

void Object3d::DrawInstancedBatch(D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress, uint32_t instanceCount,
                                  const ViewProjection &viewProjection, bool reflect, bool lighting)
{
    if (!pModel_ || instanceCount == 0)
    {
        return;
    }

    DeferredRenderer *deferred = DeferredRenderer::GetInstance();
    const bool gBufferPass = deferred->IsGBufferPassActive();

    // PSO は頂点シェーダーだけがインスタンシング版のもの。ルートシグネチャは通常描画と共有なので、
    // 以降のバインド（マテリアル・ライト・シャドウ）は Draw() とまったく同じでよい。
    PipelineManager::GetInstance()->DrawCommonSetting(
        gBufferPass ? PipelineType::GBufferInstanced : PipelineType::StandardInstanced,
        gBufferPass ? BlendMode::Normal : blendMode_);

    // インスタンスデータ（変換行列＋個体色）をルートSRV(t4)で直接指す
    {
        const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
        assert(rootSignature && "インスタンシング描画のルートシグネチャが未生成です");
        pDxCommon_->GetCommandList()->SetGraphicsRootShaderResourceView(
            rootSignature->GetSrvIndex(4, D3D12_SHADER_VISIBILITY_VERTEX), instanceBufferAddress);
    }

    if (pLightGroup_)
    {
        pLightGroup_->Update(viewProjection);
        pLightGroup_->Draw();
    }

    if (UsesInstanceColor())
    {
        // 個体色はインスタンスバッファ側で乗算するので、マテリアル定数バッファの色は白にしておく。
        // （こうしないとバッチ代表の色が全インスタンスに二重で掛かる）
        // ObjColor は値の取り出ししか使われないので GPU リソースの初期化は不要。
        if (instanceWhiteColors_.size() != color_.size())
        {
            instanceWhiteColors_.assign(color_.size(), ObjColor{});
            for (auto &objColor : instanceWhiteColors_)
            {
                objColor.SetColor({1.0f, 1.0f, 1.0f, 1.0f});
            }
        }
        pModel_->Draw(materials_, instanceWhiteColors_, lighting, reflect, instanceCount);
    }
    else
    {
        // 複数マテリアルのときは色までバッチキーに含めて一致させているので、そのまま送ってよい
        // （インスタンス側の色は白）。
        pModel_->Draw(materials_, color_, lighting, reflect, instanceCount);
    }
}

void Object3d::DrawShadowInstancedBatch(D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress, uint32_t instanceCount)
{
    if (!pModel_ || instanceCount == 0)
    {
        return;
    }
    PipelineManager::GetInstance()->DrawCommonSetting(PipelineType::ShadowMapInstanced);
    // シャドウ用ルートシグネチャの t0 = インスタンスデータ SRV。
    // 番号はリフレクション由来なのでレジスタ番号で引く
    const ShaderRootSignature *shadowRootSignature =
        PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::ShadowMap);
    assert(shadowRootSignature && "シャドウマップのルートシグネチャが未生成です");
    pDxCommon_->GetCommandList()->SetGraphicsRootShaderResourceView(shadowRootSignature->GetSrvIndex(0),
                                                                   instanceBufferAddress);
    pModel_->DrawShadow(instanceCount);
}

void Object3d::DrawShadow(const WorldTransform &worldTransform)
{
    if (!pModel_)
        return;

    // スキニングを「現在フレームのポーズ」で適用してからシャドウマップへ描く。
    // シャドウパスはメインパスより前に走るため、ここでスキニングしないと
    // 後段メインパスでしか走らない＝シャドウは前フレームのスキン結果を使い、
    // 自己影のUV/深度が1フレームずれてキャラ全身が影になってしまう（自己影バグ）。
    if (pModel_->IsGltf() && pModel_->GetModelData().hasAnimations && !skinnedThisFrame_)
    {
        objectCommon_->computeSkinningDrawCommonSetting();
        pModel_->Update();
        skinnedThisFrame_ = true;
    }

    Matrix4x4 localMatrix = MakeAffineMatrix(worldTransform.scale_, worldTransform.quaternionRotation_, worldTransform.translation_);
    Matrix4x4 worldMatrix = localMatrix;
    if (worldTransform.pParent_)
    {
        worldMatrix = localMatrix * worldTransform.pParent_->matWorld_;
    }

    if (!pModel_->GetModelData().hasAnimations)
    {
        pTransformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    }
    else if (pModel_->GetModelData().hasBones)
    {
        pTransformationMatrixData_->LightWVP = worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    }
    else
    {
        pTransformationMatrixData_->LightWVP = pModel_->GetAnimator()->GetLocalMatrix() * worldMatrix * ShadowMap::GetInstance()->GetLightViewProjection();
    }

    PipelineManager::GetInstance()->DrawCommonSetting(PipelineType::ShadowMap);
    {
        const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
        assert(rootSignature && "シャドウマップのルートシグネチャが未生成です");
        pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
            rootSignature->GetCbvIndex(0), transformationMatrixResource_->GetGPUVirtualAddress());
    }

    if (pModel_)
    {
        pModel_->DrawShadow();
    }
}

void Object3d::CreateTransformationMatrix()
{

    transformationMatrixResource_ = pDxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    // 書き込むかめのアドレスを取得
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void **>(&pTransformationMatrixData_));
    // 単位行列を書き込んでおく
    pTransformationMatrixData_->WVP = MakeIdentity4x4();
    pTransformationMatrixData_->World = MakeIdentity4x4();
    pTransformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
    pTransformationMatrixData_->LightWVP = MakeIdentity4x4();
}

void Object3d::CreateIndependentMaterials()
{
    if (!pModel_)
        return;

    size_t materialCount = materials_.size();
    materials_.clear();
    materials_.resize(materialCount);

    for (size_t i = 0; i < materialCount; ++i)
    {
        materials_[i] = std::make_unique<Material>();
        materials_[i]->Initialize();

        // 元のマテリアルデータをコピー
        const Material *originalMaterial = GetMaterial(uint32_t(i));
        if (originalMaterial)
        {
            materials_[i]->GetMaterialData() = originalMaterial->GetMaterialData();
            materials_[i]->LoadTexture();
        }
    }
}

void Object3d::SetTexture(const std::string &filePath, uint32_t materialIndex)
{
    if (materialIndex < materials_.size())
    {
        materials_[materialIndex]->SetTexture(filePath);
    }
}

void Object3d::SetEnvironmentCoefficients(float value)
{
    for (auto &material : materials_)
    {
        material->SetEnvironmentCoefficients(value);
    }
}
} // namespace Hagine
