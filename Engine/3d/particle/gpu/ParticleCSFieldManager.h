#pragma once
#include "data/DataHandler.h"
#include "ParticleCSField.h"
#include "ParticleCSFieldSettingOverride.h"
#include <DirectXCommon.h>
#include <graphics/srv/SrvManager.h>
#include <particle/ParticleCommon.h>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>
#include <wrl.h>

namespace Hagine {
/// =============================================
/// ParticleCSFieldManager
///   全フィールドを持ち、毎フレーム GPU バッファへ詰めて送る。
///   エミッターは受けるレイヤーを渡して GetUpdateMask / GetEmitMask を受け取り、
///   シェーダーはそのビットのフィールドだけを読む（レイヤー判定を粒子ごとに繰り返さない）。
/// =============================================
class ParticleCSFieldManager
{
  public:
    static ParticleCSFieldManager *GetInstance()
    {
        static ParticleCSFieldManager instance;
        return &instance;
    }
    void Finalize();

    void Initialize();
    /// 毎フレーム呼ぶ：発生のタイマーを進め、有効なフィールドを GPU バッファへ転送する
    void Update();

    // --- フィールド編集 ---
    void AddField(const ParticleField &field = {});
    void RemoveField(int index);
    ParticleField *GetField(int index);
    int GetFieldCount() const { return static_cast<int>(fields_.size()); }
    std::vector<ParticleField> &GetFields() { return fields_; }

    // --- エミッターから見た「効くフィールド」 ---

    /// <summary>
    /// 受けるレイヤーに対して、粒子を動かす・変える効果を持つフィールドの GPU 番号をビットで返す
    /// （0 ならこのエミッターの Update ではフィールドを一切読まない＝軽量版の Update が使える）
    /// </summary>
    uint32_t GetUpdateMask(uint32_t receiveLayers) const;

    /// <summary>
    /// 受けるレイヤーに対して、今フレーム「この範囲から発生」するフィールドの GPU 番号をビットで返す
    /// </summary>
    uint32_t GetEmitMask(uint32_t receiveLayers) const;

    /// <summary>
    /// GetEmitMask のフィールドが今フレーム出す数の合計（接触発生のディスパッチ数）
    /// </summary>
    uint32_t GetEmitBurstTotal(uint32_t receiveLayers) const;

    // --- フィールド生成（セーブ/ロード付き） ---
    /// フィールドを生成して登録し、そのポインタを返す
    /// @param name        新しいフィールドの名前（セーブファイル名にも使われる）
    /// @param templateName テンプレートとして読み込むjsonのファイル名（省略時は新規作成）
    /// @return 登録されたフィールドへのポインタ（所有権はマネージャーが持つ）
    ParticleField *CreateField(const std::string &name, const std::string &templateName = "");

    // --- セーブ/ロード ---
    /// 指定フィールドのデータをjsonに保存する
    void SaveField(const ParticleField &field);
    /// json からフィールドデータをロードして返す（失敗時は defaultField を返す）
    ParticleField LoadField(const std::string &fileName, const ParticleField &defaultField = {});

    // --- GPU ---
    /// UpdateParticle_CS の SRV に設定するハンドル
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetFieldsSrvHandle() const { return fieldsSrvHandle_; }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetFieldCountResource() const { return fieldCountResource_; }
    uint32_t GetFieldsSrvIndex() const { return fieldsSrvIndex_; }

    // --- ImGui ---
    /// フィールド管理ウィンドウを表示する。
    void DrawImGui();

    /// <summary>
    /// フィールドのギズモ登録名（シーンのアイコン・ギズモ選択で使う）
    /// </summary>
    static std::string GizmoName(const std::string &fieldName) { return "Field: " + fieldName; }

    /// <summary>
    /// ギズモ登録名からフィールドの番号を引く（無ければ -1）
    /// </summary>
    int FindFieldByGizmoName(const std::string &gizmoName) const;

    /// <summary>
    /// 1本ぶんの範囲（形・芯）と効果の向き（風・渦・引き寄せ）を線で描く
    /// </summary>
    void DrawFieldGizmo(int index);

    /// <summary>
    /// 代表の効果に合わせた表示色（線・アイコン・一覧でそろえる）
    /// </summary>
    static Vector4 FieldColor(const ParticleField &field);

    /// <summary>
    /// 代表の効果に合わせたアイコン（Font Awesome の文字）
    /// </summary>
    static const char *FieldIcon(const ParticleField &field);

    /// <summary>
    /// 代表の効果の名前（一覧の補足に出す）
    /// </summary>
    static const char *FieldSummary(const ParticleField &field);

    /// <summary>
    /// ソロ中のフィールド（-1 ならなし）。ソロ中はそのフィールドだけが GPU へ送られる
    /// </summary>
    int GetSoloIndex() const { return soloIndex_; }

    // 同時に有効化できるフィールドの上限。
    // エミッターへ渡すマスクが 32bit なので 32 を超えられない（超えるなら uint2 にする）。
    // シェーダーは立っているビットのフィールドだけを読むので、ここを増やしても重くなるのは
    // 実際に効いているフィールドの数だけ。
    static constexpr uint32_t kMaxFields = 32;

    Microsoft::WRL::ComPtr<ID3D12Resource> GetZeroFieldCountResource() const { return zeroFieldCountResource_; }

    /// 設定上書きバッファの SRV ハンドル (UpdateParticle_CS の t1 にバインド)
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetOverrideSrvHandle() const { return overrideSrvHandle_; }
    uint32_t GetOverrideSrvIndex() const { return overrideSrvIndex_; }

  private:
    ParticleCSFieldManager() = default;
    ~ParticleCSFieldManager() = default;
    ParticleCSFieldManager(const ParticleCSFieldManager &) = delete;
    ParticleCSFieldManager &operator=(const ParticleCSFieldManager &) = delete;

    void CreateGPUResources();
    void UploadToGPU();

    /// 「この範囲から発生」の間隔タイマーを進め、今フレーム出す数（spawn.burst）を決める
    void UpdateSpawnTimers();

    /// エディタのデータを GPU の形へ詰める
    static ParticleFieldGPU PackField(const ParticleField &field);

    // --- セーブ/ロード内部処理 ---
    void SaveFieldData(DataHandler &data, const ParticleField &field);
    void LoadFieldData(DataHandler &data, ParticleField &field);
    // 旧形式（fieldType + 機能ごとの enable 群）から読み替える
    void LoadLegacyFieldData(DataHandler &data, ParticleField &field);
    void SaveOverrideData(DataHandler &data, const ParticleFieldSettingsOverride &ov);
    void LoadOverrideData(DataHandler &data, ParticleFieldSettingsOverride &ov);

    // --- ギズモ描画内部処理 ---
    void DrawFieldGizmos();

    // --- Undo（エディタの編集を履歴へ積む。状態はフィールドの並び全部）---
    nlohmann::json CaptureUndoState() const;
    void RestoreUndoState(const nlohmann::json &state);

    // --- ImGui 内部（ParticleCSFieldManagerImGui.cpp）---
    void DrawOverrideImGui(ParticleFieldSettingsOverride &ov, int fieldIndex);
    // 左の一覧（検索・有効/ソロ・並べ替え・右クリックメニュー）
    void DrawFieldList(float height);
    // 右の詳細（選択中の1本）。インスペクタ窓からも呼ばれる
    void DrawFieldDetail(int index);
    // 形と範囲・効果・レイヤーの各部
    void DrawShapeSection(ParticleField &field);
    void DrawEffectsSection(ParticleField &field, int index);
    void DrawLayerSection(ParticleField &field);
    // 追加メニュー（空・プリセット）
    void DrawAddFieldMenuItems();
    // 名前の重複を避けた名前を作る
    std::string MakeUniqueFieldName(const std::string &baseName) const;
    // 複製・並べ替え・削除（選択番号とソロ番号もずらす）
    void DuplicateField(int index);
    void MoveField(int from, int to);
    void RemoveFieldAt(int index);
    // 一覧で選んだフィールドをギズモの選択にも反映する／ギズモ側の選択を一覧へ
    void SelectField(int index);
    void SyncSelectionFromGizmo();
    // ギズモ登録（フィールドの増減・改名・並べ替えのときだけ作り直す）
    void SyncGizmoTargets();

    std::vector<ParticleField> fields_;

    // GPU に送ったフィールドの、スロットごとの情報（エミッターのマスク計算に使う）
    struct UploadedSlot
    {
        uint32_t layers = 0;
        bool affectsParticles = false;
        uint32_t emitCount = 0; // 今フレーム出す数（0 なら発生しない）
    };
    std::vector<UploadedSlot> uploadedSlots_;

    // GPU側バッファ (fields)
    Microsoft::WRL::ComPtr<ID3D12Resource> fieldsResource_;
    ParticleFieldGPU *pFieldsMappedData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> fieldCountResource_;
    uint32_t *pFieldCountMappedData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> zeroFieldCountResource_;

    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> fieldsSrvHandle_{};
    uint32_t fieldsSrvIndex_ = 0;

    // GPU側バッファ (settings override) — gFieldsOverride: t1
    Microsoft::WRL::ComPtr<ID3D12Resource> overrideResource_;
    void *pOverrideMappedData_ = nullptr; // byte単位で扱うためvoid*
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> overrideSrvHandle_{};
    uint32_t overrideSrvIndex_ = 0;

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    // --- デバッグ表示状態 ---
    bool showGizmos_ = false;

    // --- エディタ状態 ---
    int selectedIndex_ = -1;         // 一覧で選択中のフィールド
    int soloIndex_ = -1;             // ソロ中のフィールド（保存しない）
    char searchBuffer_[64] = "";     // 一覧の検索
    std::vector<std::string> gizmoNames_; // 今ギズモに登録している名前（並び順ごと）
    const ParticleField *gizmoRegisteredData_ = nullptr; // 登録時の fields_.data()（再確保の検出用）
    std::string lastGizmoSelection_; // 前フレームのギズモ選択（変化したときだけ一覧へ反映）
};
} // namespace Hagine
