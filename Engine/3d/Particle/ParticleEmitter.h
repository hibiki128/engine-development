#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "ParticleManager.h"
#include "Transform/WorldTransform.h"
#include <string>
#ifdef _DEBUG
#include "imgui.h"
#endif

#include "externals/nlohmann/json.hpp"

#include "Data/DataHandler.h"
#include <filesystem>
#include <fstream>

namespace Hagine {

/// <summary>
/// パーティクルの発生源（エミッター）クラス
/// パーティクルグループの設定を保持し、発生・更新・描画・ImGui編集・Json保存を行う
/// </summary>
class ParticleEmitter {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// コンストラクタ（メンバ変数を初期化）
    /// </summary>
    ParticleEmitter();

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="name">エミッター名</param>
    void Initialize(std::string name = {});

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 1回だけ発生させる更新処理
    /// </summary>
    void UpdateOnce();

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="vp_">ビュープロジェクション</param>
    void Draw(const ViewProjection &vp_);

    /// <summary>
    /// エミッター自体（発生範囲）の描画
    /// </summary>
    void DrawEmitter();

    /// <summary>
    /// ImGuiでのデバッグ表示
    /// </summary>
    void Debug();

    /// <summary>
    /// 全パーティクルが寿命を終えたかを判定
    /// </summary>
    /// <returns>bool: 全て完了していれば true</returns>
    bool IsAllParticlesComplete();

    /// <summary>
    /// パーティクルグループを追加
    /// </summary>
    /// <param name="particleGroup">追加するパーティクルグループ</param>
    void AddParticleGroup(ParticleGroup *particleGroup);

    /// <summary>
    /// 名前を指定してパーティクルグループを削除
    /// </summary>
    /// <param name="name">削除するグループ名</param>
    void RemoveParticleGroup(const std::string &name) {
        Manager_->RemoveParticleGroup(name);
    }

    int selectedGroupIndex_ = 0; // ImGuiで選択中のグループインデックス

    /// <summary>
    /// 自身を複製する
    /// </summary>
    /// <returns>std::unique_ptr&lt;ParticleEmitter&gt;: 複製されたエミッター</returns>
    std::unique_ptr<ParticleEmitter> Clone() const;

    bool GetIsAuto() { return isAuto_; }                                          // 自動発生フラグを取得
    Matrix4x4 GetWorldMatrix() { return transform_.matWorld_; }                  // ワールド行列を取得
    Vector3 GetPosition() { return transform_.translation_; }                    // 位置を取得
    void SetPosition(const Vector3 &position) { transform_.translation_ = position; } // 位置を設定

    bool IsGizmoSelectable() const { return isGizmoSelectable_; }                 // ギズモ選択可能か取得
    void SetGizmoSelectable(bool selectable) { isGizmoSelectable_ = selectable; } // ギズモ選択可否を設定

  public:
    void SetPositionY(const std::string &groupName, float positionY) {
        particleSettings_[groupName].translate.y = positionY;
        FlushSetting(groupName); // Manager に即時反映
    }
    void SetRotate(const std::string &groupName, const Vector3 &rotate) {
        particleSettings_[groupName].rotation = rotate;
        FlushSetting(groupName);
    }
    void SetRotateY(const std::string &groupName, float rotateY) {
        particleSettings_[groupName].rotation.y = rotateY;
        FlushSetting(groupName);
    }
    void SetScale(const std::string &groupName, const Vector3 &scale) {
        particleSettings_[groupName].scale = scale;
        FlushSetting(groupName);
    }

    // SetStartScale / SetEndScale:
    //   particleSettings_ への書き込みと同時に Manager_ にも即時反映する
    //   （transform の dirty 判定とは独立して動作する）
    void SetStartScale(const std::string &groupName, const Vector3 &scale) {
        particleSettings_[groupName].particleStartScale = scale;
        FlushSetting(groupName);
    }
    void SetEndScale(const std::string &groupName, const Vector3 &scale) {
        particleSettings_[groupName].particleEndScale = scale;
        FlushSetting(groupName);
    }

    void SetWorldMatrix(const Matrix4x4 &worldMatrix) {
        transform_.matWorld_ = worldMatrix;
    }
    void SetIsAuto(bool isAuto) { isAuto_ = isAuto; }
    void SetCount(const std::string &groupName, int count) {
        particleSettings_[groupName].count = count;
        FlushSetting(groupName);
    }
    void SetStartRotate(const std::string &groupName, const Vector3 &startRotate) {
        particleSettings_[groupName].startRote = startRotate;
        FlushSetting(groupName);
    }
    void SetEndRotate(const std::string &groupName, const Vector3 &endRotate) {
        particleSettings_[groupName].endRote = endRotate;
        FlushSetting(groupName);
    }
    void SetActive(bool isActive) { isActive_ = isActive; }
    void SetFrequency(float frequency) { emitFrequency_ = frequency; }
    void SetName(const std::string &name) { name_ = name; }
    const std::string &GetName() const { return name_; }
    const std::string &GetDrawGroup() const { return drawGroup_; }
    void SetDrawGroup(const std::string &group) { drawGroup_ = group; }
    void SetTrailEnabled(const std::string &groupName, bool enabled);
    void SetTrailInterval(const std::string &groupName, float interval);
    void SetMaxTrailParticles(const std::string &groupName, int maxTrails);
    void SetTrailLifeScale(const std::string &groupName, float scale);
    void SetTrailScaleMultiplier(const std::string &groupName, const Vector3 &multiplier);
    void SetTrailColorMultiplier(const std::string &groupName, const Vector4 &multiplier);
    void SetTrailVelocityInheritance(const std::string &groupName, bool inherit, float scale = 0.3f);
    void SetStartColor(const std::string &groupName, const Vector4 &color) {
        particleSettings_[groupName].startColor = color;
        FlushSetting(groupName);
    }
    void SetEndColor(const std::string &groupName, const Vector4 &color) {
        particleSettings_[groupName].endColor = color;
        FlushSetting(groupName);
    }

    // SetScaleAll / SetStartAcce 系:
    //   全グループへの一括書き込みも Manager_ に即時反映する
    void SetScaleAll(const Vector3 &scale) {
        for (auto &[groupName, setting] : particleSettings_) {
            if (setting.isSinMove) {
                setting.particleStartScale = scale;
            } else {
                setting.scale = scale;
            }
            FlushSetting(groupName);
        }
    }
    void SetStartAcce(const Vector3 &acce) {
        for (auto &[groupName, setting] : particleSettings_) {
            setting.startAcce = acce;
            FlushSetting(groupName);
        }
    }
    void SetStartAcceX(const float &acce) {
        for (auto &[groupName, setting] : particleSettings_) {
            setting.startAcce.x = acce;
            FlushSetting(groupName);
        }
    }
    void SetStartAcceZ(const float &acce) {
        for (auto &[groupName, setting] : particleSettings_) {
            setting.startAcce.z = acce;
            FlushSetting(groupName);
        }
    }
    void SetEndAcce(const Vector3 &acce) {
        for (auto &[groupName, setting] : particleSettings_) {
            setting.endAcce = acce;
            FlushSetting(groupName);
        }
    }

    size_t GetActiveParticleCount() const {
        return Manager_ ? Manager_->GetActiveParticleCount() : 0;
    }

    // パーティクルマネージャーへのアクセス（デバッグ用）
    ParticleManager *GetParticleManager() const {
        return Manager_.get();
    }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// パーティクルを発生させる（外部用・設定同期込み）
    /// </summary>
    void Emit();

    /// <summary>
    /// transform を全グループの ParticleSetting に反映する（dirty判定用）
    /// </summary>
    void SyncSettingsToTransform();

    /// <summary>
    /// 設定同期なしで発射するだけの内部用処理
    /// </summary>
    void EmitInternal();

    /// <summary>
    /// 指定グループの設定を Manager_ に即時反映する
    /// </summary>
    /// <param name="groupName">対象グループ名</param>
    void FlushSetting(const std::string &groupName) {
        if (!Manager_)
            return;
        auto it = particleSettings_.find(groupName);
        if (it == particleSettings_.end())
            return;
        // transform 系は常に現在の transform_ を優先して上書き
        it->second.translate = transform_.translation_;
        it->second.rotation = transform_.quateRotation_.ToEulerAngles();
        it->second.scale = transform_.scale_;
        Manager_->SetParticleSetting(groupName, it->second);
    }

    /// <summary>設定をJsonへ保存</summary>
    void SaveToJson();

    /// <summary>設定をJsonから読み込み</summary>
    void LoadFromJson();

    /// <summary>パーティクルグループを読み込み</summary>
    void LoadParticleGroup();

    /// <summary>既定のパーティクル設定を生成</summary>
    /// <returns>ParticleSetting: 既定設定</returns>
    ParticleSetting DefaultSetting();

    /// <summary>ブレンドモード選択コンボを表示</summary>
    /// <param name="currentMode">現在のブレンドモード</param>
    void ShowBlendModeCombo(BlendMode &currentMode);

    /// <summary>パーティクルデータのデバッグ表示</summary>
    void DebugParticleData();

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    using json = nlohmann::json;
    float elapsedTime_ = 0.0f;   // 経過時間
    float emitFrequency_ = 0.1f; // パーティクルの発生頻度

    bool isVisible_ = false;          // 表示フラグ
    bool isActive_ = false;           // アクティブフラグ
    bool isAuto_ = false;             // 自動発生フラグ
    bool isGizmoSelectable_ = true;   // ギズモ選択可能フラグ

    std::string name_;                   // パーティクルの名前
    std::string drawGroup_ = "3D";       // 描画グループ＝描画ステージ（既定は3D）
    WorldTransform transform_; // 位置や回転などのトランスフォーム

    std::unordered_map<std::string, ParticleSetting> particleSettings_; // グループごとの設定

    std::unique_ptr<ParticleManager> Manager_;      // パーティクル管理
    std::unique_ptr<DataHandler> datas_;            // データ管理
    std::vector<std::string> particleGroupNames_;   // パーティクルグループ名一覧

    // dirty判定用：前フレームの transform を保持する
    Vector3 lastTranslation_ = {};
    Quaternion lastRotation_ = Quaternion::IdentityQuaternion();
    Vector3 lastScale_ = {1.0f, 1.0f, 1.0f};
};
} // namespace Hagine
