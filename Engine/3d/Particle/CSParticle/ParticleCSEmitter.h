#pragma once
#include "Graphics/Model/ModelManager.h"
#include "Particle/ParticleStruct.h"
#include "ParticleCSGroup.h"
#include <Camera/ViewProjection/ViewProjection.h>
#include <DirectXCommon.h>
#include <Graphics/Srv/SrvManager.h>
#include <Particle/ParticleCommon.h>
#include <set>
#include <vector>

namespace Hagine {

/// <summary>
/// GPU（コンピュートシェーダー）パーティクルの発生源クラス
/// 発生源メッシュ・グループを保持し、Emit/Updateのコンピュート実行と描画を行う
/// </summary>
class ParticleCSEmitter {

  public:
    /// ==============================================
    /// public methods
    /// ==============================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ParticleCSEmitter() = default;

    /// <summary>
    /// デストラクタ（保有する独立グループを再利用プールへ返却しバッファ累積を防ぐ）
    /// </summary>
    ~ParticleCSEmitter();

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="name">エミッター名</param>
    void Initialize(const std::string &name);

    /// <summary>
    /// 初期化（モデルを指定）
    /// </summary>
    /// <param name="name">エミッター名</param>
    /// <param name="modelPath">発生源メッシュのモデルパス</param>
    void Initialize(const std::string &name, const std::string &modelPath);

    /// <summary>
    /// 初期化（プリミティブを指定）
    /// </summary>
    /// <param name="name">エミッター名</param>
    /// <param name="primitiveType">発生源メッシュのプリミティブ種別</param>
    void Initialize(const std::string &name, PrimitiveType primitiveType);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="vp">ビュープロジェクション</param>
    void Draw(const ViewProjection &vp);

    /// <summary>
    /// ImGuiでの設定UIを表示
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// パーティクルグループを追加
    /// </summary>
    /// <param name="particleGroup">追加するグループ</param>
    void AddParticleGroup(ParticleCSGroup *particleGroup);

    /// <summary>
    /// 名前を指定してパーティクルグループを削除
    /// </summary>
    /// <param name="groupName">削除するグループ名</param>
    void RemoveParticleGroup(const std::string &groupName);

    /// <summary>
    /// 1回だけパーティクルを発生させる
    /// </summary>
    void EmitOnce();

    EmitterMesh GetEmitterMesh() const {
        if (emitterMeshData_)
            return *emitterMeshData_;
        return EmitterMesh{};
    }

    void SetName(const std::string &name) { name_ = name; }
    void SetFrequency(float frequency) {
        if (emitterMeshData_)
            emitterMeshData_->frequency = frequency;
    }
    void SetActive(bool isActive) { isActive_ = isActive; }
    void SetAuto(bool isAuto) { isAuto_ = isAuto; }
    bool GetAuto() const { return isAuto_; }
    // エミッターのワイヤーフレーム描画の表示・非表示を切り替える
    void SetVisible(bool isVisible) { isVisible_ = isVisible; }

    bool IsGizmoSelectable() const { return isGizmoSelectable_; }
    void SetGizmoSelectable(bool selectable) { isGizmoSelectable_ = selectable; }

    std::string GetName() const { return name_; }
    const std::string &GetDrawGroup() const { return drawGroup_; }
    void SetDrawGroup(const std::string &group) { drawGroup_ = group; }
    void SetEnableGravity(bool enable) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->enableGravity = enable;
        }
    }

    void SetEnableLifeTimeScale(bool enable) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->enableLifetimeScale = enable;
        }
    }

    void SetMinVelocity(Vector3 minVelocity) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->velocityMin = minVelocity;
        }
    }

    void SetMaxVelocity(Vector3 maxVelocity) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->velocityMax = maxVelocity;
        }
    }

    /// <summary>
    /// 自身を複製する
    /// </summary>
    /// <returns>std::unique_ptr&lt;ParticleCSEmitter&gt;: 複製されたエミッター</returns>
    std::unique_ptr<ParticleCSEmitter> Clone() const;

    void SetTranslate(Vector3 transform) {
        if (emitterMeshData_)
            emitterMeshData_->translate = transform;
    }

    void SetStartColor(Vector4 color) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->startColor = color;
        }
    }

    void SetEndColor(Vector4 color) {
        for (auto &group : particleGroups_) {
            group->GetSettingsData()->endColor = color;
        }
    }

    void SetRotation(Quaternion rotation) {
        if (emitterMeshData_)
            emitterMeshData_->rotation = -rotation;
    }

    void SetScale(Vector3 scale) {
        if (emitterMeshData_)
            emitterMeshData_->scale = scale;
    }

    void SetAnchorPoint(Vector3 anchor) {
        if (emitterMeshData_)
            emitterMeshData_->anchorPoint = anchor;
    }

    void SetReceiveFields(bool receive) { receiveFields_ = receive; }
    bool GetReceiveFields() const { return receiveFields_; }

    // フィールド接触時のみEmitするモード
    // true  = enableEmitSpawnフィールドが存在する場合、シェーダー側で
    //         フィールド球内のランダム点→エミッター表面投影でEmit位置を決定する。
    //         emitCount は fieldContactEmitCount_ の値を使用する。
    // false = 通常の自動Emit（フィールドは UpdateCS での物理影響のみ）
    void SetEmitOnlyOnFieldContact(bool enable) { emitOnlyOnFieldContact_ = enable; }
    bool GetEmitOnlyOnFieldContact() const { return emitOnlyOnFieldContact_; }

    // フィールド接触Emitモード時の1フレームあたり発生数
    // 全スレッドがフィールド接触部分にEmitするので、
    // 少ない値（例: 500〜2000）でも十分密になる
    void SetFieldContactEmitCount(uint32_t count) { fieldContactEmitCount_ = count; }
    uint32_t GetFieldContactEmitCount() const { return fieldContactEmitCount_; }

    // フィールドグループID（このIDと一致するフィールドのみ影響を受ける）
    // -1 = 全フィールドから影響を受ける（デフォルト）
    void SetFieldGroupId(int32_t id) { fieldGroupId_ = id; }
    int32_t GetFieldGroupId() const { return fieldGroupId_; }
    bool GetAcitve() const { return isActive_; }

    Vector3 GetAnchorPoint() const {
        if (emitterMeshData_)
            return emitterMeshData_->anchorPoint;
        return Vector3(0.5f, 0.5f, 0.5f);
    }

    Vector3 GetTranslate() const {
        if (emitterMeshData_)
            return emitterMeshData_->translate;
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    Quaternion GetRotation() const {
        if (emitterMeshData_)
            return emitterMeshData_->rotation;
        return Quaternion::IdentityQuaternion();
    }

    Vector3 GetScale() const {
        if (emitterMeshData_)
            return emitterMeshData_->scale;
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    Vector3 GetRadius() const {
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    static void ClearNameCounter() {
        GetNameCounter().clear();
    }

    static void ClearNameCounter(const std::string &baseName) {
        GetNameCounter().erase(baseName);
    }

    size_t GetTotalAliveParticles();

    // グループごとの統計情報
    struct GroupStatistics {
        std::string groupName;
        uint32_t aliveCount;
    };

    // 全グループの統計を取得
    std::vector<GroupStatistics> GetGroupStatistics();

  private:
    /// ==============================================
    /// private methods
    /// ==============================================

    /// <summary>
    /// 発生源メッシュ用の定数バッファリソースを生成
    /// </summary>
    void CreateEmitterMeshResource();

    /// <summary>
    /// 発生源メッシュの更新
    /// </summary>
    void EmitterUpdate();

    /// <summary>
    /// Emitのコンピュートをディスパッチする（全グループ・kEmitter PSO）。
    /// </summary>
    /// <param name="cmdList">使用するコマンドリスト（省略時は既定）</param>
    void EmitterDisPatch(ID3D12GraphicsCommandList *cmdList = nullptr);

  public:
    /// ---- バッチ非同期コンピュート用 2フェーズ API ----

    /// <summary>
    /// Compute フェーズ: Emit/Update を Compute Queue に記録するだけ（Execute しない）
    /// </summary>
    /// <param name="vp">ビュープロジェクション</param>
    void DrawCompute(const ViewProjection &vp);

    /// <summary>
    /// Graphics フェーズ: Count + DrawIndexed を Direct Queue で実行（Compute 済み前提）
    /// </summary>
    /// <param name="vp">ビュープロジェクション</param>
    void DrawGraphics(const ViewProjection &vp);

    /// <summary>
    /// プレビュー隔離描画: 外部 per-view CB（プレビューVP）で Graphics のみ描画する。
    /// RT/DSV/Viewport/DescriptorHeap は呼び出し側で設定済みであること。ワイヤー(DrawEmitter)は描かない。
    /// </summary>
    /// <param name="perViewGpuAddress">プレビュー用 per-view 定数バッファのGPUアドレス</param>
    void DrawGraphicsForPreview(D3D12_GPU_VIRTUAL_ADDRESS perViewGpuAddress);

    /// <summary>
    /// エミッターのワイヤーフレームを描画
    /// </summary>
    void DrawEmitter();

    // プレビュー窓用: エミッタのワイヤーフレーム線分を取得する（DrawEmitter と同一形状）。
    // 共有 DrawLine3D を使わず呼び出し側（プレビュー）が専用VPで描けるよう、線分列を返す。
    struct WireSegment {
        Vector3 a;
        Vector3 b;
        Vector4 color;
    };
    std::vector<WireSegment> GetWireframeSegments() const;

    /// <summary>設定をJsonへ保存</summary>
    void SaveSetting();

    /// <summary>設定をJsonから読み込み</summary>
    void LoadSetting();

    /// <summary>複製時の設定を読み込み</summary>
    void LoadCloneSetting();

    /// <summary>発生源メッシュのモデルを読み込み</summary>
    /// <param name="modelPath">モデルパス</param>
    void LoadModel(const std::string &modelPath);

    /// <summary>発生源メッシュのプリミティブモデルを読み込み</summary>
    /// <param name="type">プリミティブの種類</param>
    void LoadPrimitiveModel(PrimitiveType type);

    /// <summary>発生源メッシュの三角形情報を生成</summary>
    void CreateModelTriangles();

    /// <summary>発生源メッシュのエッジ情報を生成</summary>
    void CreateModelEdges();

  private:
    /// ==============================================
    /// private variables
    /// ==============================================
    ///

    static std::unordered_map<std::string, int> &GetNameCounter() {
        static std::unordered_map<std::string, int> nameCounter;
        return nameCounter;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> emitterMeshResource_ = nullptr;
    EmitterMesh *emitterMeshData_ = nullptr;

    DirectXCommon *dxCommon_ = nullptr;
    ID3D12GraphicsCommandList *commandList_ = nullptr;
    ParticleCommon *particleCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    std::vector<ParticleCSGroup *> particleGroups_;
    std::set<std::string> particleGroupNames_;

    Microsoft::WRL::ComPtr<ID3D12Resource> triangleInfoResource_ = nullptr;
    TriangleInfo *triangleInfoData_ = nullptr;
    std::vector<TriangleInfo> triangleInfoList_;

    Microsoft::WRL::ComPtr<ID3D12Resource> triangleCDFResource_ = nullptr;
    float *triangleCDFData_ = nullptr;
    std::vector<float> triangleCDF_;

    uint32_t triangleInfoSrvIndex_ = 0;
    uint32_t triangleCDFSrvIndex_ = 0;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> triangleInfoSrvHandle_;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> triangleCDFSrvHandle_;

    Microsoft::WRL::ComPtr<ID3D12Resource> edgeInfoResource_ = nullptr;
    EdgeInfo *edgeInfoData_ = nullptr;
    std::vector<EdgeInfo> edgeInfoList_;

    uint32_t edgeInfoSrvIndex_ = 0;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> edgeInfoSrvHandle_;

    // Model data for mesh emitters
    Model *model_ = nullptr;
    ModelData modelData_;
    std::string modelPath_;
    PrimitiveType primitiveType_ = PrimitiveType::None;

    std::string name_;
    std::string drawGroup_ = "3D"; // 描画グループ＝描画ステージ（既定は3D）
    int groupNum_ = 0;

    bool isAuto_ = false;
    bool isActive_ = false;
    bool isVisible_ = true;
    bool isGizmoSelectable_ = true;
    bool emitOnce_ = false;
    bool receiveFields_ = true;
    int32_t fieldGroupId_ = -1;             // -1=全フィールド対象, 0以上=同じIDのフィールドのみ対象
    bool emitOnlyOnFieldContact_ = false;   // true=フィールド接触時のみEmit（シェーダー側で位置を決定）
    uint32_t fieldContactEmitCount_ = 1000; // 接触Emitモード時の発生数/フレーム
};
} // namespace Hagine
