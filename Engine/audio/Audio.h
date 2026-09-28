#pragma once
#include "array"
#include "cstdint"
#include "memory"
#include "string"
#include "vector"
#include "wrl.h"
#include "xaudio2.h"
#include "xaudio2fx.h"
#include "AudioTypes.h"
#include <asset/AssetPath.h>
#include <filesystem>
#include <map>
#include <set>

namespace Hagine {
class Audio
{

    class VoiceCallback : public IXAudio2VoiceCallback
    {
      public:
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnBufferStart(void *) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
        void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}

        void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) override
        {
            if (pBufferContext)
            {
                Voice *voice = reinterpret_cast<Voice *>(pBufferContext);
                if (voice && voice->sourceVoice)
                {
                    voice->sourceVoice = nullptr;
                }
            }
        }
    };

  private:
    static const int kMaxSoundData = 2108;

    Audio() = default;
    ~Audio() = default;
    Audio(Audio &) = default;
    Audio &operator=(Audio &) = default;

  private:
    struct ChunkHeader
    {
        char id[4];
        int32_t size;
    };

    struct RiffHeader
    {
        ChunkHeader chunk;
        char type[4];
    };

    struct FormatChunk
    {
        ChunkHeader chunk;
        WAVEFORMATEX fmt;
    };

    struct SoundData
    {
        WAVEFORMATEX wfex;
        std::vector<uint8_t> buffer;
        std::string name_;
    };

    struct Voice
    {
        uint32_t soundIndex = 0u; // 鳴らしている音声データの番号
        uint32_t id = 0u;         // SoundHandle の値。1つ1つの発音を指す通し番号
        IXAudio2SourceVoice *sourceVoice = nullptr;
        float volume = 1.0f;       // 今の音量。フェード中はここが動く
        float targetVolume = 1.0f; // フェードの行き先
        float fadeSpeed = 0.0f;    // 1秒あたりの音量変化量。0 ならフェードしない
        bool stopOnFadeEnd = false; // フェードし切ったら止めるか

        SoundBus bus = SoundBus::SE; // どの区分へ流しているか
        bool is3D = false;           // 3D 定位を掛けるか
        SoundEmitter3D emitter;      // 3D のときの音源情報
        float basePitch = 1.0f;      // ドップラーを掛ける前のピッチ
        uint32_t sourceChannels = 1; // 音声データのチャンネル数

        std::unique_ptr<VoiceCallback> callback;
    };

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static Audio *GetInstance()
    {
        static Audio instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(const std::string &directoryPath = AssetPath::SoundRoot());

    /// <summary>
    /// 音声読み込み
    /// </summary>
    uint32_t LoadWave(const std::string &filename);

    /// <summary>
    /// 音声データ解放
    /// </summary>
    void Unload(uint32_t soundIndex);

    /// <summary>
    /// 音声再生
    /// </summary>
    void PlayWave(uint32_t soundIndex, float volume, bool loop = false);

    /// <summary>
    /// 音声停止
    /// </summary>
    void StopWave(uint32_t soundIndex);

    /// <summary>
    /// 音量設定
    /// </summary>
    void SetVolume(uint32_t soundIndex, float volume);

    /// <summary>
    /// マスター音量を設定する。鳴っている音・これから鳴る音すべてにまとめて掛かる
    /// </summary>
    /// <param name="volume">音量 (0.0f 〜 1.0f)</param>
    void SetMasterVolume(float volume);

    /// <summary>
    /// マスター音量を取得する
    /// </summary>
    /// <returns>float: 音量 (0.0f 〜 1.0f)</returns>
    float GetMasterVolume() const { return masterVolume_; }

    /// <summary>
    /// 今再生中の音の振幅[0,1]を返す（再生中ボイスのPCMを再生位置でRMSサンプリング）。
    /// 複数再生中は最も大きい振幅を返す。何も再生していなければ0。
    /// パーティクルの音声振動など、再生に影響しない読み取り専用の用途に使う。
    /// </summary>
    float GetCurrentAmplitude() const;

    // ------------------------------------------------------------------
    // 再生（こちらが本命の入口。PlayWave は従来互換のために残してある）
    // ------------------------------------------------------------------

    /// <summary>
    /// ファイル名から音声を用意する。すでに読み込み済みならそれを使い回す
    /// </summary>
    /// <param name="filename">サウンドルートからの相対パス ("player/jump.wav" など)</param>
    /// <returns>uint32_t: 音声番号。失敗したら UINT32_MAX</returns>
    uint32_t GetOrLoad(const std::string &filename);

    /// <summary>
    /// 音を鳴らす
    /// </summary>
    /// <param name="soundIndex">音声番号</param>
    /// <param name="params">鳴らし方</param>
    /// <returns>SoundHandle: この発音を指す識別子</returns>
    SoundHandle Play(uint32_t soundIndex, const SoundPlayParams &params = {});

    /// <summary>
    /// ファイル名を指定して鳴らす（未読み込みならその場で読み込む）
    /// </summary>
    /// <param name="filename">サウンドルートからの相対パス</param>
    /// <param name="params">鳴らし方</param>
    /// <returns>SoundHandle: この発音を指す識別子</returns>
    SoundHandle PlayOneShot(const std::string &filename, const SoundPlayParams &params = {});

    /// <summary>
    /// ワールド上の位置から鳴らす。距離で小さくなり、左右に振られ、遠いほどこもる
    /// </summary>
    /// <param name="soundIndex">音声番号</param>
    /// <param name="emitter">音源の位置など</param>
    /// <param name="params">鳴らし方</param>
    /// <returns>SoundHandle: この発音を指す識別子</returns>
    SoundHandle Play3D(uint32_t soundIndex, const SoundEmitter3D &emitter, const SoundPlayParams &params = {});

    /// <summary>
    /// ファイル名を指定してワールド上の位置から鳴らす
    /// </summary>
    /// <param name="filename">サウンドルートからの相対パス</param>
    /// <param name="position">鳴らす位置</param>
    /// <param name="params">鳴らし方</param>
    /// <returns>SoundHandle: この発音を指す識別子</returns>
    SoundHandle PlayOneShot3D(const std::string &filename, const Vector3 &position,
                              const SoundPlayParams &params = {});

    /// <summary>
    /// 鳴っている音を止める
    /// </summary>
    /// <param name="handle">対象</param>
    /// <param name="fadeOutSeconds">消えるまでにかける時間。0 なら即座に止める</param>
    void Stop(SoundHandle handle, float fadeOutSeconds = 0.0f);

    /// <summary>
    /// 鳴っている音の音量を変える
    /// </summary>
    /// <param name="handle">対象</param>
    /// <param name="volume">音量</param>
    /// <param name="fadeSeconds">その音量へ移るまでの時間。0 なら即座に反映</param>
    void SetVoiceVolume(SoundHandle handle, float volume, float fadeSeconds = 0.0f);

    /// <summary>
    /// 鳴っている音の高さ（再生速度）を変える
    /// </summary>
    /// <param name="handle">対象</param>
    /// <param name="pitch">1.0 で原音</param>
    void SetVoicePitch(SoundHandle handle, float pitch);

    /// <summary>
    /// 動く音源の位置を更新する（キャラに付いて回る音など。毎フレーム呼ぶ）
    /// </summary>
    /// <param name="handle">対象</param>
    /// <param name="emitter">新しい音源情報</param>
    void UpdateEmitter(SoundHandle handle, const SoundEmitter3D &emitter);

    /// <summary>
    /// まだ鳴っているか
    /// </summary>
    /// <param name="handle">対象</param>
    /// <returns>bool: 鳴っていれば true</returns>
    bool IsPlaying(SoundHandle handle) const;

    /// <summary>
    /// 指定した区分の音をすべて止める
    /// </summary>
    /// <param name="bus">対象の区分</param>
    /// <param name="fadeOutSeconds">消えるまでにかける時間</param>
    void StopBus(SoundBus bus, float fadeOutSeconds = 0.0f);

    /// <summary>
    /// 今鳴っている BGM を消しながら、次の BGM を重ねて立ち上げる
    /// </summary>
    /// <param name="filename">次に流す BGM のパス</param>
    /// <param name="fadeSeconds">入れ替えにかける時間</param>
    /// <param name="volume">次の BGM の音量</param>
    /// <returns>SoundHandle: 新しい BGM の識別子</returns>
    SoundHandle CrossFadeBgm(const std::string &filename, float fadeSeconds = 1.5f, float volume = 1.0f);

    // ------------------------------------------------------------------
    // ミキサー
    // ------------------------------------------------------------------

    /// <summary>
    /// 区分ごとの音量を設定する（設定画面の「BGM音量」「効果音音量」に対応）
    /// </summary>
    /// <param name="bus">対象の区分</param>
    /// <param name="volume">音量 (0.0f 〜 1.0f)</param>
    void SetBusVolume(SoundBus bus, float volume);

    /// <summary>区分ごとの音量を取得する</summary>
    /// <param name="bus">対象の区分</param>
    /// <returns>float: 音量</returns>
    float GetBusVolume(SoundBus bus) const;

    /// <summary>
    /// 聞き手（カメラ）の位置と向きを設定する。3D 再生の基準になる
    /// </summary>
    /// <param name="listener">聞き手の情報</param>
    void SetListener(const SoundListener &listener) { listener_ = listener; }

    /// <summary>聞き手の情報を取得する</summary>
    /// <returns>const SoundListener&amp;: 聞き手の情報</returns>
    const SoundListener &GetListener() const { return listener_; }

    /// <summary>残響（リバーブ）の有効・無効</summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetReverbEnabled(bool enabled);

    /// <summary>残響が有効か</summary>
    bool IsReverbEnabled() const { return reverbEnabled_; }

    /// <summary>
    /// 残響の響き方をプリセットから選ぶ（部屋・洞窟・ホールなど）
    /// </summary>
    /// <param name="presetIndex">プリセット番号 (0 〜 GetReverbPresetCount()-1)</param>
    void SetReverbPreset(int presetIndex);

    /// <summary>今選ばれている残響プリセット番号</summary>
    int GetReverbPreset() const { return reverbPreset_; }

    /// <summary>残響プリセットの数</summary>
    static int GetReverbPresetCount();

    /// <summary>残響プリセットの表示名</summary>
    /// <param name="presetIndex">プリセット番号</param>
    /// <returns>const char*: 表示名</returns>
    static const char *GetReverbPresetName(int presetIndex);

    /// <summary>
    /// ドップラー効果の強さ。0 で無効、1 で物理どおり
    /// </summary>
    /// <param name="scale">強さ</param>
    void SetDopplerScale(float scale) { dopplerScale_ = scale; }
    float GetDopplerScale() const { return dopplerScale_; }

    /// <summary>
    /// 毎フレームの更新。フェードの進行・3D の再計算・終わったボイスの回収を行う
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間[秒]</param>
    void Update(float deltaTime);

    /// <summary>今鳴っている音の数</summary>
    /// <returns>int: 発音数</returns>
    int GetActiveVoiceCount() const { return static_cast<int>(voices_.size()); }

    /// <summary>
    /// XAudio2 本体を借りる。
    /// 音楽機能（MusicEngine）が自前のストリーミング用ソースボイスを作るために使う。
    /// 返したポインタの寿命は Audio が握っているので、Finalize より後に触らないこと
    /// </summary>
    /// <returns>IXAudio2*: XAudio2 本体。未初期化なら nullptr</returns>
    IXAudio2 *GetXAudio2() const { return xAudio2_.Get(); }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 再生終了済みボイスを解放（毎フレーム呼び出し推奨）
    /// </summary>
    void CleanupFinishedVoices();

    /// <summary>
    /// ImGui デバッグウィンドウ描画
    /// _DEBUG ビルド時に毎フレーム呼び出してください
    /// </summary>
    void Debug();

  private:
    //------------------------------------------------------------------
    // ミキサー・3D の内部処理（AudioMixer.cpp）
    //------------------------------------------------------------------

    /// <summary>区分ごとのサブミックスボイスと残響を作る</summary>
    void CreateBuses();

    /// <summary>区分ごとのサブミックスボイスと残響を解放する</summary>
    void DestroyBuses();

    /// <summary>
    /// ソースボイスを1本作って再生を始める（Play / Play3D の共通部分）
    /// </summary>
    /// <param name="soundIndex">音声番号</param>
    /// <param name="params">鳴らし方</param>
    /// <param name="emitter">3D の音源情報。nullptr なら 2D 再生</param>
    /// <returns>SoundHandle: 生成した発音の識別子</returns>
    SoundHandle StartVoice(uint32_t soundIndex, const SoundPlayParams &params, const SoundEmitter3D *emitter);

    /// <summary>識別子からボイスを探す</summary>
    /// <param name="handle">対象</param>
    /// <returns>Voice*: 見つからなければ nullptr</returns>
    Voice *FindVoice(SoundHandle handle) const;

    /// <summary>1つのボイスへ 3D の定位・減衰・こもりを反映する</summary>
    /// <param name="voice">対象</param>
    void ApplySpatialization(Voice &voice) const;

    //------------------------------------------------------------------
    // デバッグ補助関数（private）
    //------------------------------------------------------------------

    /// ミキサー（区分音量・残響・3D）のデバッグUIを描く
    void DebugDrawMixer();

    /// 鳴っている音の一覧を描く
    void DebugDrawActiveVoices();

    /// サウンドルート配下を再帰スキャンして .wav ファイル一覧を更新する
    void DebugScanWavFiles();

    /// 指定インデックスの音声総時間（秒）を返す
    float DebugGetDurationSec(uint32_t index) const;

    /// 指定インデックスの現在再生位置（秒）を返す（再生中でなければ 0）
    float DebugGetPositionSec(uint32_t index) const;

    /// 指定インデックスが再生中かどうかを返す
    bool DebugIsPlaying(uint32_t index) const;

    /// ロード済みファイル名 → soundIndex を解決する（未ロードなら UINT32_MAX）
    uint32_t DebugResolveIndex(const std::string &filename) const;

    /// 指定インデックスの PCM から表示用の波形エンベロープ（min/max）を構築しキャッシュする
    void DebugBuildWaveform(uint32_t index);

  private:
    //------------------------------------------------------------------
    // 通常メンバ
    //------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice *pMasterVoice_ = nullptr;
    float masterVolume_ = 1.0f; // マスター音量（ゲーム設定から書き換える）
    std::string directoryPath_;
    std::array<SoundData, kMaxSoundData> soundDatas_;
    size_t soundDataIndex_ = 0;
    std::set<std::unique_ptr<Voice>> voices_;
    std::set<std::string> loadedFiles_;

    // --- ミキサー（区分ごとのサブミックス + 残響） ---
    std::array<IXAudio2SubmixVoice *, static_cast<size_t>(SoundBus::Count)> busVoices_{};
    std::array<float, static_cast<size_t>(SoundBus::Count)> busVolumes_{1.0f, 1.0f, 1.0f};
    IXAudio2SubmixVoice *reverbVoice_ = nullptr;
    bool reverbEnabled_ = false;
    int reverbPreset_ = 0;
    uint32_t outputChannels_ = 2; // マスターボイスのチャンネル数
    uint32_t outputSampleRate_ = 48000;

    // --- 3D ---
    SoundListener listener_;
    float dopplerScale_ = 1.0f;

    uint32_t nextVoiceId_ = 1; // SoundHandle に配る通し番号（0 は無効値）

    uint16_t audioFormat_ = 0;
    uint16_t numChannels_ = 0;
    uint32_t sampleRate_ = 0;
    uint32_t byteRate_ = 0;
    uint16_t blockAlign_ = 0;
    uint16_t bitsPerSample_ = 0;

    //------------------------------------------------------------------
    // デバッグ専用メンバ
    //------------------------------------------------------------------
    std::vector<std::string> debugWavFileList_;      // スキャン済み .wav 一覧
    int debugSelectedFile_ = -1;                     // リスト選択インデックス
    float debugVolume_ = 1.0f;                       // 再生ボリューム
    bool debugLoop_ = false;                         // ループフラグ
    std::map<std::string, uint32_t> debugLoadedMap_; // ファイル名 → soundIndex

    // 波形プレビュー用キャッシュ（再構築は選択音が変わったときだけ）
    int debugWaveformIndex_ = -1;         // キャッシュ中の soundIndex（-1=未構築）
    std::vector<float> debugWaveformX_;   // X 座標（0..buckets-1）
    std::vector<float> debugWaveformMin_; // 各バケットの最小サンプル
    std::vector<float> debugWaveformMax_; // 各バケットの最大サンプル
};
} // namespace Hagine
