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
    /// public method
    /// ==============================================

    /// <summary>
    /// コンストラクタ（生存エミッター一覧へ登録する）
    /// </summary>
    ParticleCSEmitter();

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

    /// <summary>
    /// このエミッターの親子付け登録名を返す（3Dオブジェクトの名前と衝突しないよう接頭辞が付く）
    /// </summary>
    /// <param name="emitterName">エミッター名</param>
    /// <returns>std::string: 親子付けの登録名</returns>
    static std::string AttachName(const std::string &emitterName) { return "パーティクル/" + emitterName; }

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

    /// ===================================================
    /// 発光（周囲を照らす動的ポイントライト）
    /// ===================================================
    ///
    /// 有効にすると、このエミッターの位置に毎フレーム点光源を登録し、
    /// 地面やキャラクターなど周囲の Object3d を照らす。
    /// エネルギー弾が地面を照らすような演出に使う。
    /// ※ 光源枠は LightGroup と共有（MAX_POINT_LIGHTS）。溢れた場合は
    ///    カメラからの距離と明るさで優先度の高いものが採用される。

    /// <summary>発光のON/OFF</summary>
    void SetLightEnabled(bool enable) { lightEnabled_ = enable; }
    bool GetLightEnabled() const { return lightEnabled_; }

    /// <summary>光の色</summary>
    void SetLightColor(const Vector4 &color) { lightColor_ = color; }
    Vector4 GetLightColor() const { return lightColor_; }

    /// <summary>光の強さ</summary>
    void SetLightIntensity(float intensity) { lightIntensity_ = intensity; }
    float GetLightIntensity() const { return lightIntensity_; }

    /// <summary>光の届く半径</summary>
    void SetLightRadius(float radius) { lightRadius_ = radius; }
    float GetLightRadius() const { return lightRadius_; }

    /// <summary>減衰の強さ（大きいほど中心付近に光が集まる）</summary>
    void SetLightDecay(float decay) { lightDecay_ = decay; }
    float GetLightDecay() const { return lightDecay_; }

    /// <summary>エミッター位置からの光源オフセット（エミッターの向きに追従する）</summary>
    void SetLightOffset(const Vector3 &offset) { lightOffset_ = offset; }
    Vector3 GetLightOffset() const { return lightOffset_; }

    /// <summary>
    /// 粒子が出ていないときは消灯するか（既定true）。
    /// falseにすると発生が止まっていてもエミッターが有効な限り光り続ける。
    /// </summary>
    void SetLightFollowParticles(bool follow) { lightFollowParticles_ = follow; }
    bool GetLightFollowParticles() const { return lightFollowParticles_; }

    /// <summary>
    /// プレビュー窓専用のエミッターか（ParticleCSEditor が持つ編集用インスタンス）。
    /// true のものはシミュレーションだけ回してゲーム画面には描かないため、
    /// 発光もゲームシーンへ登録しない。
    /// </summary>
    void SetPreviewOnly(bool previewOnly) { previewOnly_ = previewOnly; }
    bool GetPreviewOnly() const { return previewOnly_; }

    /// ===================================================
    /// 粒子ごとの発光（粒子1個1個を光源にする）
    /// ===================================================
    ///
    /// 「発光」がエミッター位置に点光源1個を置くのに対し、こちらは
    /// 生きている粒子そのものを光源にする。位置がGPU上にしか無いため、
    /// 専用のコンピュートパスがライト配列を直接組み立てる。
    /// ※ ディファードレンダリングON時のみ有効（前方描画は16灯までしか持てない）。
    /// ※ 光源は数が増えるほど重くなるので、必ず「間引き」と「上限」で絞ること。

    /// <summary>粒子ごとの発光のON/OFF</summary>
    void SetParticleLightEnabled(bool enable) { particleLightEnabled_ = enable; }
    bool GetParticleLightEnabled() const { return particleLightEnabled_; }

    /// <summary>何粒ごとに1つ光源にするか（1で全粒子。大きいほど軽い）</summary>
    void SetParticleLightStride(uint32_t stride) { particleLightStride_ = stride < 1u ? 1u : stride; }
    uint32_t GetParticleLightStride() const { return particleLightStride_; }

    /// <summary>1グループあたりに作る光源の上限</summary>
    void SetParticleLightMaxCount(uint32_t maxCount) { particleLightMaxCount_ = maxCount; }
    uint32_t GetParticleLightMaxCount() const { return particleLightMaxCount_; }

    /// <summary>粒子の色をそのまま光の色にするか（false なら固定色）</summary>
    void SetParticleLightUseParticleColor(bool use) { particleLightUseParticleColor_ = use; }
    bool GetParticleLightUseParticleColor() const { return particleLightUseParticleColor_; }

    /// <summary>固定色（粒子の色を使わないとき）</summary>
    void SetParticleLightColor(const Vector4 &color) { particleLightColor_ = color; }
    Vector4 GetParticleLightColor() const { return particleLightColor_; }

    /// <summary>1粒あたりの光の強さ（粒子のアルファが掛かる）</summary>
    void SetParticleLightIntensity(float intensity) { particleLightIntensity_ = intensity; }
    float GetParticleLightIntensity() const { return particleLightIntensity_; }

    /// <summary>1粒あたりの光の届く半径</summary>
    void SetParticleLightRadius(float radius) { particleLightRadius_ = radius; }
    float GetParticleLightRadius() const { return particleLightRadius_; }

    /// <summary>減衰の強さ（大きいほど中心付近に光が集まる）</summary>
    void SetParticleLightDecay(float decay) { particleLightDecay_ = decay; }
    float GetParticleLightDecay() const { return particleLightDecay_; }

    /// <summary>カメラからこの距離を超える粒子は光源にしない（0で無効）</summary>
    void SetParticleLightCullDistance(float distance) { particleLightCullDistance_ = distance; }
    float GetParticleLightCullDistance() const { return particleLightCullDistance_; }

    /// <summary>
    /// 生存中の全エミッターについて、粒子光源の生成パスをコマンドリストへ記録する。
    /// LightGroup::BeginGpuLightAppend() と EndGpuLightAppend() の間で
    /// DrawSystem が1フレームに1回だけ呼ぶ。
    /// </summary>
    /// <param name="vp">このフレームのビュープロジェクション</param>
    /// <param name="pCommandList">記録先のコマンドリスト（Direct Queue）</param>
    /// <returns>bool: 1つでもディスパッチを記録したか</returns>
    static bool SubmitAllParticleLights(const ViewProjection &vp, ID3D12GraphicsCommandList *pCommandList);

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

    size_t GetTotalAliveParticles();

    // グループごとの統計情報
    struct GroupStatistics
    {
        std::string groupName;
        uint32_t aliveCount;
    };

    // 全グループの統計を取得
    std::vector<GroupStatistics> GetGroupStatistics();

    /// ==============================================
    /// シーン全体の集計（生存中の全エミッターが対象）
    ///   エディタに登録したものだけでなく、ParticleCSSpawner::Spawn で実行時に出したものや
    ///   ゲームクラスが自前で持っているものも含めて数える。
    /// ==============================================

    /// <summary>エミッター1つぶんの統計</summary>
    struct EmitterStatistics
    {
        std::string emitterName; // エミッター名
        size_t aliveCount;       // 生存パーティクル数
        bool previewOnly;        // エディタのプレビュー窓専用（ゲーム画面には出ていない）
    };

    /// <summary>
    /// 今シーンに出ている GPU パーティクルの総数を取得する。
    /// エディタのプレビュー専用エミッターは含めない（ゲーム画面に出ている数）。
    /// ※ 生存数は GPU からの読み戻しなので1〜2フレーム遅延する。
    /// </summary>
    /// <returns>size_t: 生存パーティクル総数</returns>
    static size_t GetSceneAliveParticleCount();

    /// <summary>
    /// プレビュー専用も含めた全エミッターの生存パーティクル総数を取得する
    /// </summary>
    /// <returns>size_t: 生存パーティクル総数</returns>
    static size_t GetAllAliveParticleCount();

    /// <summary>
    /// 生存中の全エミッターの統計を取得する（内訳表示・デバッグ用）
    /// </summary>
    /// <param name="includePreviewOnly">エディタのプレビュー専用エミッターも含めるか</param>
    /// <returns>std::vector&lt;EmitterStatistics&gt;: エミッターごとの統計</returns>
    static std::vector<EmitterStatistics> GetAllEmitterStatistics(bool includePreviewOnly = false);

  private:
    /// ==============================================
    /// private method
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
    /// <param name="pCommandList">使用するコマンドリスト（省略時は既定）</param>
    void EmitterDisPatch(ID3D12GraphicsCommandList *pCommandList = nullptr);

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

    /// <summary>
    /// 発光設定が有効なら、このフレームの動的ポイントライトを LightGroup へ登録する。
    /// 位置が確定した後（ResolveEmitterTransform の後）に呼ぶこと。
    /// </summary>
    void SubmitEmissiveLight();

    /// <summary>
    /// このエミッターの生存粒子から光源を生成するコンピュートパスを記録する。
    /// </summary>
    /// <param name="vp">ビュープロジェクション（距離カリングのカメラ位置に使う）</param>
    /// <param name="pCommandList">記録先のコマンドリスト</param>
    /// <returns>bool: ディスパッチを記録したか</returns>
    bool SubmitParticleLights(const ViewProjection &vp, ID3D12GraphicsCommandList *pCommandList);

    /// <summary>
    /// 粒子光源の生成パラメータ用定数バッファを必要になった時点で生成する
    /// </summary>
    void EnsureParticleLightResource();

    /// <summary>
    /// GPU駆動描画（DrawInstanceIndirect）を1メッシュぶん発行する。
    /// 描画数は引数バッファ内の InstanceCount（Compute が GPU カウンタからコピー）が決める。
    /// </summary>
    /// <param name="group">対象グループ</param>
    /// <param name="meshIndex">メッシュ番号（引数バッファ内のオフセットに対応）</param>
    void ExecuteIndirectDraw(ParticleCSGroup *group, size_t meshIndex);

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
    // 共有 LineRenderer を使わず呼び出し側（プレビュー）が専用VPで描けるよう、線分列を返す。
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
    // 親子付け（AttachmentManager）へ登録済みか。破棄時の解除判定に使う
    bool attachRegistered_ = false;
    // 実際に ImGuizmo へ登録済みか。デストラクタでの解除判定に使う
    // （registerGizmo_ は Initialize 後にも変更されうるので、そちらは判定に使えない）
    bool gizmoRegistered_ = false;

    // ---- 発光（動的ポイントライト）----
    bool lightEnabled_ = false;                       // 発光するか
    Vector4 lightColor_ = {1.0f, 0.9f, 0.6f, 1.0f};   // 光の色
    float lightIntensity_ = 2.0f;                     // 光の強さ
    float lightRadius_ = 8.0f;                        // 光の届く半径
    float lightDecay_ = 1.0f;                         // 減衰の強さ
    Vector3 lightOffset_ = {0.0f, 0.0f, 0.0f};        // エミッター位置からのオフセット
    bool lightFollowParticles_ = true;                // 粒子が無いときは消灯する
    bool previewOnly_ = false;                        // プレビュー窓専用（ゲームシーンを照らさない）

    /// <summary>
    /// 粒子光源の生成パラメータ（ParticleLightGen.CS.hlsl の同名構造体と一致させること）
    /// </summary>
    struct ParticleLightGenConstants
    {
        uint32_t particleStride;   // 何粒ごとに1つ光源にするか
        uint32_t maxLights;        // このディスパッチで作れる光源の上限
        uint32_t bufferCapacity;   // ライトバッファ全体の容量
        uint32_t useParticleColor; // 1=粒子の色を使う

        Vector3 lightColor; // 固定色
        float intensity;    // 光の強さ

        float radius;       // 光の届く半径
        float decay;        // 減衰の強さ
        float cullDistance; // 距離カリング（0で無効）
        float alphaCutoff;  // このアルファ未満は光源にしない

        Vector3 cameraPosition; // 距離カリング用
        float padding;
    };

    // ---- 粒子ごとの発光（粒子1個1個を光源にする）----
    bool particleLightEnabled_ = false;              // 粒子ごとの発光をするか
    uint32_t particleLightStride_ = 8;               // 何粒ごとに1つ光源にするか
    uint32_t particleLightMaxCount_ = 64;            // 1グループあたりの光源上限
    bool particleLightUseParticleColor_ = true;      // 粒子の色をそのまま光の色にする
    Vector4 particleLightColor_ = {1.0f, 0.9f, 0.6f, 1.0f}; // 固定色
    float particleLightIntensity_ = 1.0f;            // 1粒あたりの光の強さ
    float particleLightRadius_ = 3.0f;               // 1粒あたりの光の届く半径
    float particleLightDecay_ = 1.0f;                // 減衰の強さ
    float particleLightCullDistance_ = 60.0f;        // これより遠い粒子は光源にしない（0で無効）

    // 生成パラメータの定数バッファ（粒子光源を使うエミッターだけが持つ）
    Microsoft::WRL::ComPtr<ID3D12Resource> particleLightCBResource_;
    ParticleLightGenConstants *pParticleLightCBData_ = nullptr;

    // 生存中の全エミッター。粒子光源の生成をどのエミッターに対しても回せるように、
    // 所有者（ParticleCSSpawner / ParticleCSEditor / シーン）に依らずここで束ねる。
    static std::vector<ParticleCSEmitter *> liveEmitters_;
};
} // namespace Hagine
