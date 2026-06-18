#pragma once
#include "Data/DataHandler.h"
#include "Line/DrawLine3D.h"
#include "ParticleCSFieldSettingOverride.h"
#include "../ParticleStruct.h"
#include <DirectXCommon.h>
#include <Graphics/Srv/SrvManager.h>
#include <Particle/ParticleCommon.h>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>
#include <wrl.h>

/// =============================================
/// フィールドの種類
/// =============================================
namespace Hagine {
enum class ParticleFieldType : uint32_t {
    Wind = 0,    // 一定方向に力を加える
    Attract = 1, // 中心に引き寄せる
    Repel = 2,   // 中心から押し出す
    Vortex = 3,  // 渦巻き
};

/// =============================================
/// ParticleFieldManager
///   シングルトン。全フィールドを管理し、
///   GPUバッファを毎フレーム更新する。
/// =============================================
class ParticleCSFieldManager {
  public:
    static ParticleCSFieldManager *GetInstance() {
        static ParticleCSFieldManager instance;
        return &instance;
    }
    void Finalize();

    void Initialize();
    /// 毎フレーム呼ぶ：有効フィールドをGPUバッファへ転送
    void Update();

    // --- フィールド編集 ---
    void AddField(const ParticleField &field = {});
    void RemoveField(int index);
    ParticleField *GetField(int index);
    int GetFieldCount() const { return static_cast<int>(fields_.size()); }
    std::vector<ParticleField> &GetFields() { return fields_; }

    // --- フィールド生成（セーブ/ロード付き） ---
    /// フィールドを生成してシングルトンに登録し、そのポインタを返す
    /// @param name        新しいフィールドの名前（セーブファイル名にも使われる）
    /// @param templateName テンプレートとして読み込むjsonのファイル名（省略時は新規作成）
    /// @return 登録されたフィールドへのポインタ（所有権はシングルトンが持つ）
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
    /// フィールド管理ウィンドウを表示する。ギズモ描画もここから行われる。
    void DrawImGui();

    // 同時に有効化できるフィールドの上限。
    // fields バッファ（ParticleFieldData × kMaxFields）と override バッファ（× kMaxFields）の
    // 両方のサイズ、AddField の上限、シェーダ ApplyFields の fieldCount クランプ上限を兼ねる。
    // 変更時は GPU バッファ容量が増えるだけで挙動互換（シェーダは fieldCount までしか走査しない）。
    static constexpr uint32_t kMaxFields = 8;

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

    // --- セーブ/ロード内部処理 ---
    void SaveFieldData(DataHandler &data, const ParticleField &field);
    void LoadFieldData(DataHandler &data, ParticleField &field);
    void SaveOverrideData(DataHandler &data, const ParticleFieldSettingsOverride &ov);
    void LoadOverrideData(DataHandler &data, ParticleFieldSettingsOverride &ov);

    // --- ギズモ描画内部処理 ---
    void DrawFieldGizmos();
    void DrawFieldSphere(const ParticleField &field, const Vector4 &color);
    void DrawWindArrows(const ParticleField &field, const Vector4 &color);
    void DrawRadialLines(const ParticleField &field, const Vector4 &color, bool inward);
    void DrawVortexArcs(const ParticleField &field, const Vector4 &color);

    // --- ImGui 内部 ---
    void DrawOverrideImGui(ParticleFieldSettingsOverride &ov, int fieldIndex);

    std::vector<ParticleField> fields_;

    // GPU側バッファ (fields)
    Microsoft::WRL::ComPtr<ID3D12Resource> fieldsResource_;
    ParticleFieldData *fieldsMappedData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> fieldCountResource_;
    uint32_t *fieldCountMappedData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> zeroFieldCountResource_;

    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> fieldsSrvHandle_{};
    uint32_t fieldsSrvIndex_ = 0;

    // GPU側バッファ (settings override) — gFieldsOverride: t1
    // C++ の ParticleFieldSettingsOverride を GPU用レイアウトに変換して転送する
    Microsoft::WRL::ComPtr<ID3D12Resource> overrideResource_;
    void *overrideMappedData_ = nullptr; // byte単位で扱うためvoid*
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> overrideSrvHandle_{};
    uint32_t overrideSrvIndex_ = 0;

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    // --- デバッグ表示状態 ---
    bool showGizmos_ = false;
};
} // namespace Hagine
