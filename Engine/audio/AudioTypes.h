#pragma once
#include <cstdint>
#include <type/Vector3.h>

namespace Hagine {

/// <summary>
/// 音の用途。用途ごとに音量をまとめて上げ下げできるようにするための区分。
/// XAudio2 のサブミックスボイス1本ずつに対応している
/// </summary>
enum class SoundBus
{
    BGM,   // 背景音楽
    SE,    // 効果音
    Voice, // ボイス・セリフ
    Count,
};

/// <summary>
/// 再生中の音を指す識別子。
/// すでに鳴り終わった音を指していても、操作は黙って無視されるだけで安全
/// </summary>
struct SoundHandle
{
    uint32_t value = 0;

    /// <summary>有効な識別子かどうか</summary>
    bool IsValid() const { return value != 0; }
};

/// <summary>
/// 鳴らし方の指定。すべて既定値のままでも鳴る
/// </summary>
struct SoundPlayParams
{
    float volume = 1.0f;           // 音量
    float pitch = 1.0f;            // 再生速度＝音の高さ (1.0 で原音)
    float pitchRandomRange = 0.0f; // ピッチをこの幅だけ毎回ランダムにずらす。連打しても単調にならない
    bool loop = false;             // 繰り返すか
    SoundBus bus = SoundBus::SE;   // どの区分へ流すか
    float fadeInSeconds = 0.0f;    // 立ち上がりにかける時間
    float reverbSend = 0.0f;       // 残響へ送る量 (0〜1)
};

/// <summary>
/// 3D で鳴らすときの音源の情報
/// </summary>
struct SoundEmitter3D
{
    Vector3 position = {};      // ワールド座標
    Vector3 velocity = {};      // 速度。ドップラー効果に使う（不要なら 0 のまま）
    float minDistance = 5.0f;   // これより近ければ減衰しない
    float maxDistance = 80.0f;  // これより遠ければ聞こえない
};

/// <summary>
/// 聞き手（＝カメラ）の情報。Audio::SetListener で毎フレーム更新する
/// </summary>
struct SoundListener
{
    Vector3 position = {};                // 耳の位置
    Vector3 forward = {0.0f, 0.0f, 1.0f}; // 正面
    Vector3 right = {1.0f, 0.0f, 0.0f};   // 右手方向。左右どちらから鳴るかの判定に使う
    Vector3 velocity = {};                // 速度。ドップラー効果に使う
};

} // namespace Hagine
