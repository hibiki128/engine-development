#pragma once
#include "graphics/model/ModelManager.h"
#include "particle/ParticleStruct.h"
#include "ParticleCSGroup.h"
#include <camera/projection/ViewProjection.h>
#include <DirectXCommon.h>
#include <graphics/srv/SrvManager.h>
#include <particle/ParticleCommon.h>
#include <set>
#include <vector>

namespace Hagine {

class WorldTransform;
class BaseObject;

/// <summary>
/// GPU（コンピュートシェーダー）パーティクルの発生源クラス
/// 発生源メッシュ・グループを保持し、Emit/Updateのコンピュート実行と描画を行う
/// </summary>
class ParticleCSEmitter
{

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

    EmitterMesh GetEmitterMesh() const
    {
        if (pEmitterMeshData_)
            return *pEmitterMeshData_;
        return EmitterMesh{};
    }

    void SetName(const std::string &name) { name_ = name; }
    void SetFrequency(float frequency)
    {
        if (pEmitterMeshData_)
            pEmitterMeshData_->frequency = frequency;
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
    void SetEnableGravity(bool enable)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->enableGravity = enable;
        }
    }

    void SetEnableLifeTimeScale(bool enable)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->enableLifetimeScale = enable;
        }
    }

    void SetMinVelocity(Vector3 minVelocity)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->velocityMin = minVelocity;
        }
    }

    void SetMaxVelocity(Vector3 maxVelocity)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->velocityMax = maxVelocity;
        }
    }

    /// <summary>
    /// 自身を複製する
    /// </summary>
    /// <returns>std::unique_ptr&lt;ParticleCSEmitter&gt;: 複製されたエミッター</returns>
    std::unique_ptr<ParticleCSEmitter> Clone() const;

    void SetTranslate(Vector3 transform)
    {
        // 親がいるときは親からのローカル座標として扱う
        //（ワールド座標は毎フレーム親の行列から解決されるので直接書いても上書きされる）。
        if (pParentTransform_)
        {
            localTranslate_ = transform;
            return;
        }
        if (pEmitterMeshData_)
            pEmitterMeshData_->translate = transform;
    }

    /// ===================================================
    /// 親子付け（位置と回転が親に追従する。スケールは追従しない）
    /// ===================================================

    /// <summary>
    /// 親のワールド変換を設定する。以後このエミッターの位置は
    /// 「親のワールド位置 ＋ 親の回転 × ローカル座標」で毎フレーム解決される。
    /// 向きも親の回転に追従する（ビルボードONのときは向きだけカメラ優先）。
    /// </summary>
    /// <param name="parent">親のワールド変換（nullptr で解除）</param>
    void SetParent(const WorldTransform *parent) { pParentTransform_ = parent; }

    /// <summary>BaseObject を親にする（内部でその WorldTransform を使う）</summary>
    /// <param name="parent">親オブジェクト（nullptr で解除）</param>
    void SetParent(BaseObject *parent);

    /// <summary>親子付けを解除する（現在のワールド位置・向きにその場で留まる）</summary>
    void ClearParent() { pParentTransform_ = nullptr; }

    const WorldTransform *GetParentTransform() const { return pParentTransform_; }

    /// <summary>
    /// 親からのローカル座標を設定する。親がいないときは使われない
    /// （その場合は SetTranslate がそのままワールド座標になる）。
    /// </summary>
    void SetLocalTranslate(const Vector3 &translate) { localTranslate_ = translate; }
    Vector3 GetLocalTranslate() const { return localTranslate_; }

    /// <summary>
    /// ImGuizmo へギズモ対象として登録するかどうか。Initialize より前に呼ぶこと。
    /// 実行時に量産されるインスタンスは false にして、エディタ上の
    /// テンプレート（同名エミッター）のギズモ登録を奪わないようにする。
    /// </summary>
    void SetRegisterGizmo(bool enable) { registerGizmo_ = enable; }

    void SetStartColor(Vector4 color)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->startColor = color;
        }
    }

    void SetEndColor(Vector4 color)
    {
        for (auto &group : particleGroups_)
        {
            group->GetSettingsData()->endColor = color;
        }
    }

    void SetRotation(Quaternion rotation)
    {
        baseRotation_ = -rotation;
        // ビルボードONなら次の DrawCompute が baseRotation_ にカメラ回転を合成して上書きする。
        // OFF（またはこのフレームがアイドル）でも即座に反映されるようここでも入れておく。
        if (pEmitterMeshData_)
            pEmitterMeshData_->rotation = baseRotation_;
    }

    // ビルボード（常にカメラへ正対）: true にすると発生源メッシュの向きを毎フレーム
    // カメラの回転で置き換える（作者が指定した回転はカメラ空間でのオフセットとして残る）。
    // 渦の基準空間（ParticleCSSettings::effectSpace）を「エミッター」にしておくと
    // 発生形状と渦の軸が一緒にカメラへ追従し、どの角度から見ても同じ動きになる。
    void SetBillboardEmitter(bool enable) { billboardEmitter_ = enable; }
    bool GetBillboardEmitter() const { return billboardEmitter_; }

    // ビルボード合成前の、作者が指定した回転
    Quaternion GetBaseRotation() const { return baseRotation_; }

    void SetScale(Vector3 scale)
    {
        if (pEmitterMeshData_)
            pEmitterMeshData_->scale = scale;
    }

    void SetAnchorPoint(Vector3 anchor)
    {
        if (pEmitterMeshData_)
            pEmitterMeshData_->anchorPoint = anchor;
    }

    void SetReceiveFields(bool receive) { receiveFields_ = receive; }
    bool GetReceiveFields() const { return receiveFields_; }

    // フィールド接触部分にのみ発生するモード
    // true  = enableEmitSpawn フィールドと接触しているエミッター表面にのみEmitする。
    //         発生数・間隔・寿命はフィールド側（ParticleField の接触Emit設定）が管理し、
    //         エミッター側の frequency / emitCount は使われない。
    // false = 通常の自動Emit（フィールドは UpdateCS での物理影響のみ）
    void SetEmitOnlyOnFieldContact(bool enable) { emitOnlyOnFieldContact_ = enable; }
    bool GetEmitOnlyOnFieldContact() const { return emitOnlyOnFieldContact_; }

    // フィールドグループID（このIDと一致するフィールドのみ影響を受ける）
    // -1 = 全フィールドから影響を受ける（デフォルト）
    void SetFieldGroupId(int32_t id) { fieldGroupId_ = id; }
    int32_t GetFieldGroupId() const { return fieldGroupId_; }
    bool GetActive() const { return isActive_; }

    Vector3 GetAnchorPoint() const
    {
        if (pEmitterMeshData_)
            return pEmitterMeshData_->anchorPoint;
        return Vector3(0.5f, 0.5f, 0.5f);
    }

    Vector3 GetTranslate() const
    {
        if (pEmitterMeshData_)
            return pEmitterMeshData_->translate;
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    Quaternion GetRotation() const
    {
        if (pEmitterMeshData_)
            return pEmitterMeshData_->rotation;
        return Quaternion::IdentityQuaternion();
    }

    Vector3 GetScale() const
    {
        if (pEmitterMeshData_)
            return pEmitterMeshData_->scale;
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    Vector3 GetRadius() const
    {
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    static void ClearNameCounter()
    {
        GetNameCounter().clear();
    }

    static void ClearNameCounter(const std::string &baseName)
    {
        GetNameCounter().erase(baseName);
    }

    size_t GetTotalAliveParticles();

    // グループごとの統計情報
    struct GroupStatistics
    {
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

    /// <summary>
    /// 発生源メッシュの位置と向き（CB の translate / rotation）を解決する。
    ///   位置: 親がいれば「親のワールド位置 ＋ 親の回転 × localTranslate_」
    ///   向き: baseRotation_ に親の回転（またはビルボードON時はカメラ回転）を合成
    /// あわせて「演出の基準空間」用のカメラ回転行列をキャッシュする。
    /// Emit/Update のディスパッチより前に呼ぶこと。
    /// </summary>
    /// <param name="vp">このフレームのビュープロジェクション</param>
    void ResolveEmitterTransform(const ViewProjection &vp);

    /// <summary>
    /// ParticleCSSettings::effectSpace に対応する回転行列（行ベクトル規約）を返す。
    /// </summary>
    /// <param name="effectSpace">0=ワールド / 1=エミッター / 2=ビルボード</param>
    Matrix4x4 GetEffectSpaceMatrix(uint32_t effectSpace) const;

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
    /// <param name="previewPerView">プレビュー per-view のマップ済みポインタ（描画カリング設定の書込先・nullptr可）</param>
    /// <param name="cameraPos">プレビューカメラのワールド座標（距離カリング用）</param>
    /// <param name="projScaleY">プレビュー射影の[1][1]（画面サイズカリング用）</param>
    void DrawGraphicsForPreview(D3D12_GPU_VIRTUAL_ADDRESS perViewGpuAddress,
                                PerView *previewPerView = nullptr,
                                const Vector3 &cameraPos = {0.0f, 0.0f, 0.0f},
                                float projScaleY = 1.0f);

    /// <summary>
    /// エミッターのワイヤーフレームを描画
    /// </summary>
    void DrawEmitter();

    // プレビュー窓用: エミッタのワイヤーフレーム線分を取得する（DrawEmitter と同一形状）。
    // 共有 DrawLine3D を使わず呼び出し側（プレビュー）が専用VPで描けるよう、線分列を返す。
    struct WireSegment
    {
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

    /// <summary>
    /// 現在のプリミティブ形状パラメータ(分割数/半径など)でモデルと発生メッシュを作り直す。
    /// 分割数や円の幅を変更したときに呼ぶ。
    /// </summary>
    void RebuildPrimitiveModel();

    /// <summary>発生源メッシュの三角形情報を生成</summary>
    void CreateModelTriangles();

    /// <summary>発生源メッシュのエッジ情報を生成</summary>
    void CreateModelEdges();

  private:
    /// ==============================================
    /// private variables
    /// ==============================================
    ///

    static std::unordered_map<std::string, int> &GetNameCounter()
    {
        static std::unordered_map<std::string, int> nameCounter;
        return nameCounter;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> emitterMeshResource_ = nullptr;
    EmitterMesh *pEmitterMeshData_ = nullptr;

    DirectXCommon *pDxCommon_ = nullptr;
    ID3D12GraphicsCommandList *pCommandList_ = nullptr;
    ParticleCommon *pParticleCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    std::vector<ParticleCSGroup *> particleGroups_;
    std::set<std::string> particleGroupNames_;

    // DrawCompute の各フェーズ(Reset/Emit/Update/Readback)で参照する、グループごとの
    // 「今フレーム処理するか」フラグ。生存0かつ発生なしのアイドルグループを一括スキップする。
    // particleGroups_ と同じ順序・サイズ。DrawCompute 冒頭で毎フレーム再計算する。
    std::vector<uint8_t> groupActive_;

    Microsoft::WRL::ComPtr<ID3D12Resource> triangleInfoResource_ = nullptr;
    TriangleInfo *pTriangleInfoData_ = nullptr;
    std::vector<TriangleInfo> triangleInfoList_;

    Microsoft::WRL::ComPtr<ID3D12Resource> triangleCDFResource_ = nullptr;
    float *pTriangleCDFData_ = nullptr;
    std::vector<float> triangleCDF_;

    uint32_t triangleInfoSrvIndex_ = 0;
    uint32_t triangleCDFSrvIndex_ = 0;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> triangleInfoSrvHandle_;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> triangleCDFSrvHandle_;

    Microsoft::WRL::ComPtr<ID3D12Resource> edgeInfoResource_ = nullptr;
    EdgeInfo *pEdgeInfoData_ = nullptr;
    std::vector<EdgeInfo> edgeInfoList_;

    uint32_t edgeInfoSrvIndex_ = 0;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> edgeInfoSrvHandle_;

    // Model data for mesh emitters
    Model *pModel_ = nullptr;
    ModelData modelData_;
    std::string modelPath_;
    PrimitiveType primitiveType_ = PrimitiveType::None;
    PrimitiveParams primitiveParams_; // プリミティブの分割数/半径など（リング等で調整可）

    std::string name_;
    std::string drawGroup_ = "3D"; // 描画グループ＝描画ステージ（既定は3D）
    int groupNum_ = 0;

    bool isAuto_ = false;
    bool isActive_ = false;
    bool isVisible_ = true;
    bool isGizmoSelectable_ = true;
    bool emitOnce_ = false;
    bool receiveFields_ = true;
    int32_t fieldGroupId_ = -1;           // -1=全フィールド対象, 0以上=同じIDのフィールドのみ対象
    bool emitOnlyOnFieldContact_ = false; // true=フィールド接触部分にのみEmit（数・間隔はフィールド側が管理）

    // ---- 向きの解決（ビルボード）----
    // pEmitterMeshData_->rotation は「解決済み」の値。作者が指定した回転はこちらに持ち、
    // 毎フレーム ResolveEmitterRotation() が baseRotation_(+カメラ回転) から CB を作り直す。
    bool billboardEmitter_ = false;
    Quaternion baseRotation_ = Quaternion::IdentityQuaternion();
    // 今フレームのカメラ回転（行ベクトル規約・平行移動なし）。effectSpace=ビルボード で使う。
    Matrix4x4 cameraRotation_ = MakeIdentity4x4();

    // ---- 親子付け ----
    // 親がいる間、pEmitterMeshData_ の translate/rotation は毎フレーム解決される値になる。
    const WorldTransform *pParentTransform_ = nullptr;
    Vector3 localTranslate_ = {0.0f, 0.0f, 0.0f};

    // ImGuizmo へ登録するか（実行時インスタンスは false。Initialize より前に決めること）
    bool registerGizmo_ = true;
};
} // namespace Hagine
