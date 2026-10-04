#pragma once
#include "PostEffectFxFields.h"
#include "PostEffectParamsImpl.h"
#include <chrono>
#ifdef USE_IMGUI
#include <icon/IconsFontAwesome5.h>
#endif

// ============================================================
//  OffScreen/Fx/ のポストエフェクト（すべてコンピュートシェーダー版）
//
//  1つのエフェクトは
//    ・定数バッファの構造体（先頭は PostFxFrameHeader。HLSL の cbuffer と同じ並び）
//    ・項目の説明の並び（FxField。UI・保存・読み込みはここから自動で作られる）
//    ・シェーダーのファイル名
//  だけで書ける。メンバ名は HLSL 側の名前から先頭の g を取って小文字始まりにしたもの
//  （gShadowColor → shadowColor）にそろえてある。
// ============================================================
namespace Hagine {

/// <summary>
/// OffScreen/Fx/ のエフェクトの共通部分。
/// 定数バッファを1つ持ち、時間と解像度を書き込んでから b0 へ差す
/// </summary>
/// <typeparam name="DataT">定数バッファの構造体（先頭に PostFxFrameHeader frame を持つこと）</typeparam>
template <typename DataT>
class FxComputeParams : public IPostEffectParams
{
  public:
    void Initialize(DirectXCommon *pDxCommon) override
    {
        PostEffectParamsHelper::CreateConstantBuffer(pDxCommon, resource_, &pData_);
        *pData_ = DataT{};
    }

    // PS版は持たない（素通しのパイプラインがフォールバックとして割り当てられている）
    void Apply(ID3D12GraphicsCommandList *, SrvManager *, DirectXCommon *) override {}

    void UpdateTime(float deltaTime) override
    {
        pData_->frame.time += deltaTime;
        pData_->frame.deltaTime = deltaTime;
        OnUpdate(deltaTime);
    }

    void ApplyCompute(ID3D12GraphicsCommandList *pCommandList, UINT cbvRootIndex, int,
                      uint32_t textureWidth, uint32_t textureHeight) override
    {
        pData_->frame.width = static_cast<int>(textureWidth);
        pData_->frame.height = static_cast<int>(textureHeight);
        if (cbvRootIndex == UINT_MAX)
        {
            return;
        }
        pCommandList->SetComputeRootConstantBufferView(cbvRootIndex, resource_->GetGPUVirtualAddress());
    }

    void DrawUI() override
    {
        static const DataT kDefaults{};
        DrawExtraUI();
        FxFields::DrawUI(Fields(), pData_, &kDefaults);
    }

    void Save(DataHandler *handler, const std::string &prefix) const override
    {
        FxFields::Save(Fields(), pData_, handler, prefix);
        SaveExtra(handler, prefix);
    }

    void Load(DataHandler *handler, const std::string &prefix) override
    {
        static const DataT kDefaults{};
        FxFields::Load(Fields(), pData_, &kDefaults, handler, prefix);
        LoadExtra(handler, prefix);
    }

    /// <summary>定数バッファの中身（ゲーム側から直接書き換えてよい）</summary>
    DataT *GetData() { return pData_; }
    const DataT *GetData() const { return pData_; }

  protected:
    /// <summary>調整項目の並び</summary>
    virtual std::span<const FxField> Fields() const = 0;
    /// <summary>毎フレームの更新（有効な間だけ呼ばれる）</summary>
    virtual void OnUpdate(float /*deltaTime*/) {}
    /// <summary>項目の並びでは表せないUI（ボタンなど）。項目より上に出る</summary>
    virtual void DrawExtraUI() {}
    /// <summary>項目の並び以外に保存したい物</summary>
    virtual void SaveExtra(DataHandler * /*handler*/, const std::string & /*prefix*/) const {}
    /// <summary>項目の並び以外に読み込みたい物</summary>
    virtual void LoadExtra(DataHandler * /*handler*/, const std::string & /*prefix*/) {}

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    DataT *pData_ = nullptr;
};

// シェーダーのファイル名とモードだけを返す、いちばん単純なエフェクトの書き方
#define HAGINE_FX_IDENTITY(ModeName, ShaderName)                                                   \
  public:                                                                                          \
    ShaderMode GetMode() const override { return ShaderMode::ModeName; }                           \
    std::string GetComputeShaderFile() const override { return "OffScreen/Fx/" ShaderName ".CS.hlsl"; }

// ============================================================
//  色・トーン
// ============================================================

struct SepiaData
{
    PostFxFrameHeader frame;
    Vector3 shadowColor = {0.10f, 0.06f, 0.03f};
    float strength = 1.0f;
    Vector3 highlightColor = {1.0f, 0.92f, 0.76f};
    float contrast = 1.1f;
};

/// <summary>セピア調</summary>
class SepiaParams final : public FxComputeParams<SepiaData>
{
    HAGINE_FX_IDENTITY(Sepia, "Sepia")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(SepiaData, strength, "強さ", 0.01f, 0.0f, 1.0f, "元の色との混ぜ具合"),
            HAGINE_FX_COLOR3(SepiaData, shadowColor, "暗い所の色", nullptr),
            HAGINE_FX_COLOR3(SepiaData, highlightColor, "明るい所の色", nullptr),
            HAGINE_FX_FLOAT(SepiaData, contrast, "コントラスト", 0.01f, 0.2f, 3.0f, nullptr),
        };
        return kFields;
    }
};

struct PosterizeData
{
    PostFxFrameHeader frame;
    float levels = 6.0f;
    int mode = 0;
    float strength = 1.0f;
    float dither = 0.0f;
};

/// <summary>ポスタリゼーション</summary>
class PosterizeParams final : public FxComputeParams<PosterizeData>
{
    HAGINE_FX_IDENTITY(Posterize, "Posterize")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(PosterizeData, levels, "段階の数", 0.1f, 2.0f, 32.0f, "少ないほどべた塗りになります"),
            HAGINE_FX_COMBO(PosterizeData, mode, "段にする物", "RGB それぞれ\0明るさだけ（色味は残す）\0", nullptr),
            HAGINE_FX_FLOAT(PosterizeData, dither, "ディザ", 0.01f, 0.0f, 1.0f, "段の境目を細かい模様でぼかします"),
            HAGINE_FX_FLOAT(PosterizeData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct GradientMapData
{
    PostFxFrameHeader frame;
    Vector3 shadowColor = {0.10f, 0.05f, 0.28f};
    float strength = 1.0f;
    Vector3 midColor = {0.80f, 0.38f, 0.42f};
    float midPoint = 0.45f;
    Vector3 highlightColor = {1.0f, 0.88f, 0.60f};
    int useMid = 1;
};

/// <summary>グラデーションマップ（デュオトーン）</summary>
class GradientMapParams final : public FxComputeParams<GradientMapData>
{
    HAGINE_FX_IDENTITY(GradientMap, "GradientMap")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(GradientMapData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_COLOR3(GradientMapData, shadowColor, "暗い所の色", nullptr),
            HAGINE_FX_TOGGLE(GradientMapData, useMid, "中間の色も使う（3色）", nullptr),
            HAGINE_FX_COLOR3(GradientMapData, midColor, "中間の色", nullptr),
            HAGINE_FX_FLOAT(GradientMapData, midPoint, "中間の色の位置", 0.005f, 0.05f, 0.95f, nullptr),
            HAGINE_FX_COLOR3(GradientMapData, highlightColor, "明るい所の色", nullptr),
        };
        return kFields;
    }
};

struct InvertData
{
    PostFxFrameHeader frame;
    int mode = 0;
    float strength = 1.0f;
    float threshold = 0.5f;
    float padding = 0.0f;
};

/// <summary>反転・ソラリゼーション</summary>
class InvertParams final : public FxComputeParams<InvertData>
{
    HAGINE_FX_IDENTITY(Invert, "Invert")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(InvertData, mode, "やり方", "色を反転（ネガ）\0ソラリゼーション（明るい所だけ反転）\0明るさだけ反転\0", nullptr),
            HAGINE_FX_FLOAT(InvertData, threshold, "しきい値", 0.005f, 0.0f, 1.0f, "ソラリゼーションで反転し始める明るさ"),
            HAGINE_FX_FLOAT(InvertData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct HueSaturationData
{
    PostFxFrameHeader frame;
    float hueShift = 0.0f;
    float saturation = 1.0f;
    float vibrance = 0.3f;
    float brightness = 1.0f;
    float hueSpeed = 0.0f;
    float contrast = 1.0f;
    float padding[2] = {};
};

/// <summary>色相・彩度</summary>
class HueSaturationParams final : public FxComputeParams<HueSaturationData>
{
    HAGINE_FX_IDENTITY(HueSaturation, "HueSaturation")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(HueSaturationData, hueShift, "色相を回す（度）", 1.0f, -180.0f, 180.0f, nullptr),
            HAGINE_FX_FLOAT(HueSaturationData, hueSpeed, "回し続ける速さ（度/秒）", 1.0f, -720.0f, 720.0f, "0以外で色が虹のように巡り続けます"),
            HAGINE_FX_FLOAT(HueSaturationData, saturation, "彩度", 0.01f, 0.0f, 3.0f, "0で白黒、1で元のまま"),
            HAGINE_FX_FLOAT(HueSaturationData, vibrance, "自然な彩度", 0.01f, -1.0f, 2.0f, "くすんだ色ほど強く鮮やかにします（肌色が派手になりにくい）"),
            HAGINE_FX_FLOAT(HueSaturationData, brightness, "明るさ", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(HueSaturationData, contrast, "コントラスト", 0.01f, 0.0f, 3.0f, nullptr),
        };
        return kFields;
    }
};

struct ColorIsolationData
{
    PostFxFrameHeader frame;
    Vector3 targetColor = {0.9f, 0.1f, 0.1f};
    float hueRange = 20.0f;
    float softness = 15.0f;
    float otherSaturation = 0.0f;
    float boost = 1.3f;
    float minSaturation = 0.2f;
};

/// <summary>一色だけ残す</summary>
class ColorIsolationParams final : public FxComputeParams<ColorIsolationData>
{
    HAGINE_FX_IDENTITY(ColorIsolation, "ColorIsolation")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COLOR3(ColorIsolationData, targetColor, "残す色", "この色相に近い所だけ色が残ります"),
            HAGINE_FX_FLOAT(ColorIsolationData, hueRange, "色の幅（度）", 0.5f, 0.0f, 180.0f, nullptr),
            HAGINE_FX_FLOAT(ColorIsolationData, softness, "境目のなめらかさ（度）", 0.5f, 0.0f, 90.0f, nullptr),
            HAGINE_FX_FLOAT(ColorIsolationData, minSaturation, "灰色は除く", 0.005f, 0.0f, 1.0f, "これより彩度の低い色は対象外（白や灰色が残らないように）"),
            HAGINE_FX_FLOAT(ColorIsolationData, boost, "残した色の鮮やかさ", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(ColorIsolationData, otherSaturation, "他の所の彩度", 0.01f, 0.0f, 1.0f, "0で完全な白黒"),
        };
        return kFields;
    }
};

struct ThermalData
{
    PostFxFrameHeader frame;
    float strength = 1.0f;
    float contrast = 1.3f;
    float noise = 0.04f;
    int palette = 0;
};

/// <summary>サーモグラフィ</summary>
class ThermalParams final : public FxComputeParams<ThermalData>
{
    HAGINE_FX_IDENTITY(Thermal, "Thermal")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(ThermalData, palette, "色の付け方", "アイアン（黒→紫→赤→黄→白）\0レインボー\0白熱（白黒）\0", nullptr),
            HAGINE_FX_FLOAT(ThermalData, contrast, "温度差の強調", 0.01f, 0.2f, 4.0f, nullptr),
            HAGINE_FX_FLOAT(ThermalData, noise, "センサーのざらつき", 0.002f, 0.0f, 0.3f, nullptr),
            HAGINE_FX_FLOAT(ThermalData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct NightVisionData
{
    PostFxFrameHeader frame;
    Vector3 tint = {0.25f, 1.0f, 0.3f};
    float gain = 4.0f;
    float noise = 0.12f;
    float scanline = 0.15f;
    float scopeSize = 0.48f;
    int scopeMode = 1;
};

/// <summary>暗視ゴーグル</summary>
class NightVisionParams final : public FxComputeParams<NightVisionData>
{
    HAGINE_FX_IDENTITY(NightVision, "NightVision")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(NightVisionData, gain, "明るさの増幅", 0.05f, 0.5f, 20.0f, "暗い所をどれだけ持ち上げるか"),
            HAGINE_FX_COLOR3(NightVisionData, tint, "色", nullptr),
            HAGINE_FX_FLOAT(NightVisionData, noise, "ざらつき", 0.005f, 0.0f, 0.6f, nullptr),
            HAGINE_FX_FLOAT(NightVisionData, scanline, "走査線", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_COMBO(NightVisionData, scopeMode, "覗き窓", "なし\0双眼鏡\0単眼\0", nullptr),
            HAGINE_FX_FLOAT(NightVisionData, scopeSize, "窓の大きさ", 0.005f, 0.1f, 1.5f, nullptr),
        };
        return kFields;
    }
};

struct SharpenData
{
    PostFxFrameHeader frame;
    float strength = 0.8f;
    float radius = 1.0f;
    float clamp = 0.08f;
    float padding = 0.0f;
};

/// <summary>シャープ</summary>
class SharpenParams final : public FxComputeParams<SharpenData>
{
    HAGINE_FX_IDENTITY(Sharpen, "Sharpen")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(SharpenData, strength, "強さ", 0.01f, 0.0f, 4.0f, nullptr),
            HAGINE_FX_FLOAT(SharpenData, radius, "見る範囲（ピクセル）", 0.05f, 0.5f, 4.0f, "大きくすると細部より大きな形の輪郭が立ちます"),
            HAGINE_FX_FLOAT(SharpenData, clamp, "効きすぎ防止", 0.002f, 0.005f, 0.5f, "一度に変える量の上限。輪郭の白い縁取り（ハロー）を抑えます"),
        };
        return kFields;
    }
};

struct ColorOverlayData
{
    PostFxFrameHeader frame;
    Vector4 colorA = {1.0f, 0.55f, 0.2f, 0.6f};
    Vector4 colorB = {0.2f, 0.3f, 0.9f, 0.6f};
    int blendMode = 5;
    float angle = 90.0f;
    int shape = 1;
    float strength = 1.0f;
};

/// <summary>カラーオーバーレイ</summary>
class ColorOverlayParams final : public FxComputeParams<ColorOverlayData>
{
    HAGINE_FX_IDENTITY(ColorOverlay, "ColorOverlay")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(ColorOverlayData, shape, "形", "単色\0直線のグラデーション\0円形のグラデーション\0", nullptr),
            HAGINE_FX_COLOR4(ColorOverlayData, colorA, "色A（始まり・中心）", "a は濃さ"),
            HAGINE_FX_COLOR4(ColorOverlayData, colorB, "色B（終わり・外側）", "a は濃さ"),
            HAGINE_FX_FLOAT(ColorOverlayData, angle, "向き（度）", 1.0f, -180.0f, 180.0f, "直線のグラデーションの向き。90で上→下"),
            HAGINE_FX_COMBO(ColorOverlayData, blendMode, "重ね方", "通常\0乗算\0スクリーン\0オーバーレイ\0加算\0ソフトライト\0", nullptr),
            HAGINE_FX_FLOAT(ColorOverlayData, strength, "強さ", 0.01f, 0.0f, 1.0f, "ダメージで赤く光らせる・夕方にするなど、演出側から動かす値"),
        };
        return kFields;
    }
};

// ============================================================
//  絵のタッチ
// ============================================================

struct KuwaharaData
{
    PostFxFrameHeader frame;
    int radius = 4;
    float strength = 1.0f;
    float saturation = 1.15f;
    float padding = 0.0f;
};

/// <summary>油絵風（桑原フィルタ）</summary>
class KuwaharaParams final : public FxComputeParams<KuwaharaData>
{
    HAGINE_FX_IDENTITY(Kuwahara, "Kuwahara")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_INT(KuwaharaData, radius, "筆の大きさ", 1.0f, 8.0f, "大きいほど大きな筆でざっくり塗った感じに（重くなります）"),
            HAGINE_FX_FLOAT(KuwaharaData, saturation, "鮮やかさ", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(KuwaharaData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct WatercolorData
{
    PostFxFrameHeader frame;
    float wobble = 3.0f;
    float wobbleScale = 6.0f;
    float edgeDarken = 0.35f;
    float paper = 0.25f;
    float blur = 2.0f;
    float levels = 8.0f;
    float saturation = 0.9f;
    float brightness = 0.12f;
};

/// <summary>水彩画風</summary>
class WatercolorParams final : public FxComputeParams<WatercolorData>
{
    HAGINE_FX_IDENTITY(Watercolor, "Watercolor")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(WatercolorData, wobble, "にじみのゆらぎ（ピクセル）", 0.05f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, wobbleScale, "ゆらぎの細かさ", 0.05f, 0.5f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, blur, "ぼかし（ピクセル）", 0.05f, 0.0f, 8.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, levels, "色の段", 0.1f, 0.0f, 32.0f, "絵の具を塗り重ねたような段。0で段にしない"),
            HAGINE_FX_FLOAT(WatercolorData, edgeDarken, "輪郭の色だまり", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, paper, "紙の目", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, saturation, "鮮やかさ", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(WatercolorData, brightness, "紙の白さ", 0.005f, 0.0f, 0.8f, "全体を白へ寄せて、薄く塗った感じにします"),
        };
        return kFields;
    }
};

struct ToonData
{
    PostFxFrameHeader frame;
    Vector3 lineColor = {0.05f, 0.04f, 0.06f};
    float bands = 4.0f;
    float lineThreshold = 0.25f;
    float lineWidth = 1.0f;
    float lineStrength = 1.0f;
    float saturation = 1.2f;
};

/// <summary>セル画風（トゥーン）</summary>
class ToonParams final : public FxComputeParams<ToonData>
{
    HAGINE_FX_IDENTITY(Toon, "Toon")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(ToonData, bands, "影の段の数", 0.05f, 1.0f, 12.0f, nullptr),
            HAGINE_FX_FLOAT(ToonData, saturation, "鮮やかさ", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_SECTION("線"),
            HAGINE_FX_COLOR3(ToonData, lineColor, "線の色", nullptr),
            HAGINE_FX_FLOAT(ToonData, lineThreshold, "線を引く差", 0.005f, 0.01f, 2.0f, "小さいほど線が増えます"),
            HAGINE_FX_FLOAT(ToonData, lineWidth, "線の太さ（ピクセル）", 0.05f, 0.5f, 4.0f, nullptr),
            HAGINE_FX_FLOAT(ToonData, lineStrength, "線の濃さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct SketchData
{
    PostFxFrameHeader frame;
    Vector3 inkColor = {0.12f, 0.12f, 0.16f};
    float edgeStrength = 1.0f;
    Vector3 paperColor = {0.96f, 0.94f, 0.88f};
    float hatchSpacing = 6.0f;
    float hatchStrength = 0.7f;
    float colorAmount = 0.0f;
    float wobble = 0.3f;
    float padding = 0.0f;
};

/// <summary>鉛筆画風</summary>
class SketchParams final : public FxComputeParams<SketchData>
{
    HAGINE_FX_IDENTITY(Sketch, "Sketch")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(SketchData, edgeStrength, "輪郭線の濃さ", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(SketchData, hatchStrength, "斜線の濃さ", 0.01f, 0.0f, 1.0f, "暗い所ほど斜線を重ねて塗ります"),
            HAGINE_FX_FLOAT(SketchData, hatchSpacing, "斜線の間隔（ピクセル）", 0.1f, 2.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(SketchData, wobble, "線の揺れ", 0.01f, 0.0f, 2.0f, "手描きらしいふらつき"),
            HAGINE_FX_COLOR3(SketchData, inkColor, "鉛筆の色", nullptr),
            HAGINE_FX_COLOR3(SketchData, paperColor, "紙の色", nullptr),
            HAGINE_FX_FLOAT(SketchData, colorAmount, "元の色を残す", 0.01f, 0.0f, 1.0f, "0で白黒の鉛筆画、上げると色鉛筆風"),
        };
        return kFields;
    }
};

struct HalftoneData
{
    PostFxFrameHeader frame;
    Vector3 inkColor = {0.05f, 0.05f, 0.08f};
    float dotSize = 6.0f;
    Vector3 paperColor = {0.98f, 0.96f, 0.90f};
    float angle = 45.0f;
    int mode = 0;
    float strength = 1.0f;
    float softness = 0.15f;
    float padding = 0.0f;
};

/// <summary>網点（漫画のトーン・印刷物）</summary>
class HalftoneParams final : public FxComputeParams<HalftoneData>
{
    HAGINE_FX_IDENTITY(Halftone, "Halftone")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(HalftoneData, mode, "刷り方", "白黒（漫画のトーン）\0CMYK（カラー印刷）\0元の色の点\0", nullptr),
            HAGINE_FX_FLOAT(HalftoneData, dotSize, "網点の間隔（ピクセル）", 0.1f, 2.0f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(HalftoneData, angle, "網の角度（度）", 0.5f, -90.0f, 90.0f, nullptr),
            HAGINE_FX_FLOAT(HalftoneData, softness, "点のふちのぼかし", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_COLOR3(HalftoneData, inkColor, "インクの色", "白黒のときの点の色"),
            HAGINE_FX_COLOR3(HalftoneData, paperColor, "紙の色", nullptr),
            HAGINE_FX_FLOAT(HalftoneData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct AsciiData
{
    PostFxFrameHeader frame;
    Vector3 textColor = {0.9f, 0.9f, 0.9f};
    float cellSize = 10.0f;
    int colorMode = 0;
    float background = 0.15f;
    float contrast = 1.2f;
    float padding = 0.0f;
};

/// <summary>文字アート（ASCIIアート）</summary>
class AsciiParams final : public FxComputeParams<AsciiData>
{
    HAGINE_FX_IDENTITY(Ascii, "Ascii")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(AsciiData, cellSize, "文字の大きさ（ピクセル）", 0.1f, 4.0f, 32.0f, nullptr),
            HAGINE_FX_COMBO(AsciiData, colorMode, "文字の色", "元の色\0単色\0ターミナル（黒地に緑）\0", nullptr),
            HAGINE_FX_COLOR3(AsciiData, textColor, "単色のときの色", nullptr),
            HAGINE_FX_FLOAT(AsciiData, contrast, "明るさの強調", 0.01f, 0.2f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(AsciiData, background, "背景に元の絵を残す", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct DitherData
{
    PostFxFrameHeader frame;
    int palette = 0;
    float pixelSize = 3.0f;
    float dither = 1.0f;
    float levels = 4.0f;
    int matrix = 0;
    float contrast = 1.1f;
    float strength = 1.0f;
    float padding = 0.0f;
};

/// <summary>ディザ・レトロゲーム機風</summary>
class DitherParams final : public FxComputeParams<DitherData>
{
    HAGINE_FX_IDENTITY(Dither, "Dither")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(DitherData, palette, "色の数（パレット）",
                            "携帯ゲーム機（緑4色）\0白黒2色\0RGBの段を減らす\0CGA（4色）\0PICO-8 風（16色）\0", nullptr),
            HAGINE_FX_FLOAT(DitherData, pixelSize, "1画素の大きさ（ピクセル）", 0.05f, 1.0f, 16.0f, nullptr),
            HAGINE_FX_FLOAT(DitherData, dither, "ディザの強さ", 0.01f, 0.0f, 2.0f, "中間の色を点の混ぜ方で表す強さ。0でべた塗り"),
            HAGINE_FX_COMBO(DitherData, matrix, "ディザの模様", "4x4（粗い）\08x8（細かい）\0", nullptr),
            HAGINE_FX_FLOAT(DitherData, levels, "RGBの段", 0.1f, 2.0f, 16.0f, "「RGBの段を減らす」のときの段の数"),
            HAGINE_FX_FLOAT(DitherData, contrast, "コントラスト", 0.01f, 0.2f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(DitherData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct EmbossData
{
    PostFxFrameHeader frame;
    float strength = 3.0f;
    float angle = -45.0f;
    float colorKeep = 0.0f;
    float distance = 1.5f;
};

/// <summary>レリーフ（エンボス）</summary>
class EmbossParams final : public FxComputeParams<EmbossData>
{
    HAGINE_FX_IDENTITY(Emboss, "Emboss")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(EmbossData, strength, "浮き出しの強さ", 0.05f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(EmbossData, angle, "光の向き（度）", 1.0f, -180.0f, 180.0f, nullptr),
            HAGINE_FX_FLOAT(EmbossData, distance, "段差を見る距離（ピクセル）", 0.05f, 0.5f, 6.0f, nullptr),
            HAGINE_FX_FLOAT(EmbossData, colorKeep, "元の色を残す", 0.01f, 0.0f, 1.0f, "0で灰色の石膏、1で色付きの浮き彫り"),
        };
        return kFields;
    }
};

struct ShapeMosaicData
{
    PostFxFrameHeader frame;
    Vector3 backgroundColor = {0.02f, 0.02f, 0.02f};
    float cellSize = 16.0f;
    int shape = 0;
    float gap = 0.08f;
    float glow = 0.6f;
    float strength = 1.0f;
};

/// <summary>形モザイク</summary>
class ShapeMosaicParams final : public FxComputeParams<ShapeMosaicData>
{
    HAGINE_FX_IDENTITY(ShapeMosaic, "ShapeMosaic")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(ShapeMosaicData, shape, "マスの形", "六角形\0円（ドット）\0三角形\0LED\0ひし形\0", nullptr),
            HAGINE_FX_FLOAT(ShapeMosaicData, cellSize, "マスの大きさ（ピクセル）", 0.2f, 3.0f, 120.0f, nullptr),
            HAGINE_FX_FLOAT(ShapeMosaicData, gap, "隙間", 0.005f, 0.0f, 0.9f, nullptr),
            HAGINE_FX_FLOAT(ShapeMosaicData, glow, "LEDの光", 0.01f, 0.0f, 3.0f, "LED のときの光のにじみと明るさ"),
            HAGINE_FX_COLOR3(ShapeMosaicData, backgroundColor, "隙間の色", nullptr),
            HAGINE_FX_FLOAT(ShapeMosaicData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

// ============================================================
//  歪み
// ============================================================

struct SwirlData
{
    PostFxFrameHeader frame;
    Vector2 center = {0.5f, 0.5f};
    float radius = 0.45f;
    float angle = 180.0f;
    float speed = 0.0f;
    float padding[3] = {};
};

/// <summary>渦巻き</summary>
class SwirlParams final : public FxComputeParams<SwirlData>
{
    HAGINE_FX_IDENTITY(Swirl, "Swirl")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT2(SwirlData, center, "中心（UV）", 0.005f, -0.5f, 1.5f, nullptr),
            HAGINE_FX_FLOAT(SwirlData, radius, "範囲", 0.005f, 0.01f, 2.0f, "画面の高さを1とした半径"),
            HAGINE_FX_FLOAT(SwirlData, angle, "ねじれ（度）", 1.0f, -1440.0f, 1440.0f, nullptr),
            HAGINE_FX_FLOAT(SwirlData, speed, "回し続ける速さ（度/秒）", 1.0f, -720.0f, 720.0f, nullptr),
        };
        return kFields;
    }
};

struct BulgeData
{
    PostFxFrameHeader frame;
    Vector2 center = {0.5f, 0.5f};
    float radius = 0.4f;
    float strength = 0.6f;
};

/// <summary>膨らみ・へこみ</summary>
class BulgeParams final : public FxComputeParams<BulgeData>
{
    HAGINE_FX_IDENTITY(Bulge, "Bulge")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT2(BulgeData, center, "中心（UV）", 0.005f, -0.5f, 1.5f, nullptr),
            HAGINE_FX_FLOAT(BulgeData, radius, "範囲", 0.005f, 0.01f, 2.0f, "画面の高さを1とした半径"),
            HAGINE_FX_FLOAT(BulgeData, strength, "強さ（＋膨らむ / −へこむ）", 0.01f, -0.9f, 4.0f, nullptr),
        };
        return kFields;
    }
};

struct WaveData
{
    PostFxFrameHeader frame;
    float amplitude = 0.01f;
    float frequency = 6.0f;
    float speed = 3.0f;
    int direction = 0;
    Vector2 center = {0.5f, 0.5f};
    float chroma = 0.0f;
    float padding = 0.0f;
};

/// <summary>画面の揺らぎ</summary>
class WaveParams final : public FxComputeParams<WaveData>
{
    HAGINE_FX_IDENTITY(Wave, "Wave")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(WaveData, direction, "揺れ方", "横に揺れる\0縦に揺れる\0両方\0同心円（波紋）\0", nullptr),
            HAGINE_FX_FLOAT(WaveData, amplitude, "揺れ幅", 0.0005f, 0.0f, 0.2f, "画面の高さを1とした幅"),
            HAGINE_FX_FLOAT(WaveData, frequency, "波の数", 0.05f, 0.1f, 60.0f, nullptr),
            HAGINE_FX_FLOAT(WaveData, speed, "速さ", 0.05f, -30.0f, 30.0f, nullptr),
            HAGINE_FX_FLOAT2(WaveData, center, "波紋の中心（UV）", 0.005f, -0.5f, 1.5f, nullptr),
            HAGINE_FX_FLOAT(WaveData, chroma, "色のずれ", 0.01f, 0.0f, 2.0f, "揺れに合わせて赤と青がずれます"),
        };
        return kFields;
    }
};

struct KaleidoscopeData
{
    PostFxFrameHeader frame;
    Vector2 center = {0.5f, 0.5f};
    float segments = 8.0f;
    float rotation = 0.0f;
    float speed = 10.0f;
    float zoom = 1.0f;
    float strength = 1.0f;
    float padding = 0.0f;
};

/// <summary>万華鏡</summary>
class KaleidoscopeParams final : public FxComputeParams<KaleidoscopeData>
{
    HAGINE_FX_IDENTITY(Kaleidoscope, "Kaleidoscope")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(KaleidoscopeData, segments, "扇の数", 0.1f, 2.0f, 32.0f, nullptr),
            HAGINE_FX_FLOAT(KaleidoscopeData, rotation, "回転（度）", 1.0f, -180.0f, 180.0f, nullptr),
            HAGINE_FX_FLOAT(KaleidoscopeData, speed, "回し続ける速さ（度/秒）", 0.5f, -360.0f, 360.0f, nullptr),
            HAGINE_FX_FLOAT(KaleidoscopeData, zoom, "拡大", 0.01f, 0.1f, 4.0f, nullptr),
            HAGINE_FX_FLOAT2(KaleidoscopeData, center, "中心（UV）", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(KaleidoscopeData, strength, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct MirrorData
{
    PostFxFrameHeader frame;
    int mode = 0;
    float position = 0.5f;
    float padding[2] = {};
};

/// <summary>ミラー（鏡像）</summary>
class MirrorParams final : public FxComputeParams<MirrorData>
{
    HAGINE_FX_IDENTITY(Mirror, "Mirror")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(MirrorData, mode, "写し方", "左を右へ\0右を左へ\0上を下へ\0下を上へ\0四方（左上を写す）\0", nullptr),
            HAGINE_FX_FLOAT(MirrorData, position, "折り返す位置", 0.005f, 0.01f, 0.99f, nullptr),
        };
        return kFields;
    }
};

struct HeatHazeData
{
    PostFxFrameHeader frame;
    float strength = 4.0f;
    float scale = 8.0f;
    float speed = 0.6f;
    float coverage = 1.0f;
};

/// <summary>陽炎</summary>
class HeatHazeParams final : public FxComputeParams<HeatHazeData>
{
    HAGINE_FX_IDENTITY(HeatHaze, "HeatHaze")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(HeatHazeData, strength, "揺らぎ（ピクセル）", 0.05f, 0.0f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(HeatHazeData, scale, "細かさ", 0.05f, 0.5f, 60.0f, nullptr),
            HAGINE_FX_FLOAT(HeatHazeData, speed, "立ち上る速さ", 0.01f, -5.0f, 5.0f, nullptr),
            HAGINE_FX_FLOAT(HeatHazeData, coverage, "下からの範囲", 0.005f, 0.05f, 1.0f, "1で画面全体。下げると地面に近い所だけ揺らぎます"),
        };
        return kFields;
    }
};

struct UnderwaterData
{
    PostFxFrameHeader frame;
    Vector3 waterColor = {0.15f, 0.45f, 0.65f};
    float distortion = 6.0f;
    float caustics = 0.5f;
    float tint = 0.6f;
    float scale = 3.0f;
    float speed = 1.0f;
};

/// <summary>水中</summary>
class UnderwaterParams final : public FxComputeParams<UnderwaterData>
{
    HAGINE_FX_IDENTITY(Underwater, "Underwater")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COLOR3(UnderwaterData, waterColor, "水の色", nullptr),
            HAGINE_FX_FLOAT(UnderwaterData, tint, "水の色の濃さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(UnderwaterData, distortion, "ゆらゆら（ピクセル）", 0.05f, 0.0f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(UnderwaterData, caustics, "差し込む光の網", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(UnderwaterData, scale, "模様の大きさ", 0.05f, 0.3f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(UnderwaterData, speed, "速さ", 0.01f, 0.0f, 5.0f, nullptr),
        };
        return kFields;
    }
};

struct RainLensData
{
    PostFxFrameHeader frame;
    float amount = 0.5f;
    float size = 1.0f;
    float speed = 0.25f;
    float blur = 3.0f;
    float refraction = 1.0f;
    float padding[3] = {};
};

/// <summary>窓・レンズの水滴</summary>
class RainLensParams final : public FxComputeParams<RainLensData>
{
    HAGINE_FX_IDENTITY(RainLens, "RainLens")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(RainLensData, amount, "水滴の量", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(RainLensData, size, "水滴の大きさ", 0.01f, 0.2f, 4.0f, nullptr),
            HAGINE_FX_FLOAT(RainLensData, speed, "流れ落ちる速さ", 0.005f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(RainLensData, refraction, "屈折", 0.01f, 0.0f, 4.0f, nullptr),
            HAGINE_FX_FLOAT(RainLensData, blur, "ガラスのくもり（ピクセル）", 0.05f, 0.0f, 12.0f, nullptr),
        };
        return kFields;
    }
};

struct FrostedGlassData
{
    PostFxFrameHeader frame;
    float amount = 6.0f;
    float scale = 1.0f;
    float samples = 6.0f;
    float tint = 0.2f;
};

/// <summary>すりガラス</summary>
class FrostedGlassParams final : public FxComputeParams<FrostedGlassData>
{
    HAGINE_FX_IDENTITY(FrostedGlass, "FrostedGlass")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(FrostedGlassData, amount, "散らす幅（ピクセル）", 0.05f, 0.0f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(FrostedGlassData, scale, "凹凸の細かさ", 0.01f, 0.05f, 10.0f, nullptr),
            HAGINE_FX_FLOAT(FrostedGlassData, samples, "なめらかさ", 0.1f, 1.0f, 16.0f, "重ねる回数。多いほどなめらかで重い"),
            HAGINE_FX_FLOAT(FrostedGlassData, tint, "白っぽさ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct DizzyData
{
    PostFxFrameHeader frame;
    float strength = 1.0f;
    float speed = 1.0f;
    float doubleVision = 8.0f;
    float tintAmount = 0.0f;
    Vector3 tint = {0.6f, 1.0f, 0.5f};
    float padding = 0.0f;
};

/// <summary>めまい・酔い</summary>
class DizzyParams final : public FxComputeParams<DizzyData>
{
    HAGINE_FX_IDENTITY(Dizzy, "Dizzy")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(DizzyData, strength, "強さ", 0.01f, 0.0f, 3.0f, "状態異常の残り時間などに合わせて演出側から動かす値"),
            HAGINE_FX_FLOAT(DizzyData, speed, "揺れの速さ", 0.01f, 0.0f, 5.0f, nullptr),
            HAGINE_FX_FLOAT(DizzyData, doubleVision, "二重に見えるずれ（ピクセル）", 0.1f, 0.0f, 60.0f, nullptr),
            HAGINE_FX_COLOR3(DizzyData, tint, "濁りの色", "毒なら緑、混乱なら紫など"),
            HAGINE_FX_FLOAT(DizzyData, tintAmount, "濁りの強さ", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

// ============================================================
//  ぼかし
// ============================================================

struct TiltShiftData
{
    PostFxFrameHeader frame;
    float focusCenter = 0.55f;
    float focusWidth = 0.15f;
    float blurRadius = 10.0f;
    float saturation = 1.35f;
    float contrast = 1.1f;
    float angle = 0.0f;
    float falloff = 0.25f;
    float padding = 0.0f;
};

/// <summary>ミニチュア風（ティルトシフト）</summary>
class TiltShiftParams final : public FxComputeParams<TiltShiftData>
{
    HAGINE_FX_IDENTITY(TiltShift, "TiltShift")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(TiltShiftData, focusCenter, "ピントの位置（0=上 / 1=下）", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(TiltShiftData, focusWidth, "ピントの合う幅", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(TiltShiftData, falloff, "ぼけ始めるまでの広さ", 0.005f, 0.01f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(TiltShiftData, blurRadius, "ぼかしの大きさ（ピクセル）", 0.1f, 0.0f, 40.0f, nullptr),
            HAGINE_FX_FLOAT(TiltShiftData, angle, "帯の傾き（度）", 0.5f, -90.0f, 90.0f, nullptr),
            HAGINE_FX_FLOAT(TiltShiftData, saturation, "鮮やかさ", 0.01f, 0.0f, 3.0f, "模型らしく少し鮮やかにすると効果的"),
            HAGINE_FX_FLOAT(TiltShiftData, contrast, "コントラスト", 0.01f, 0.2f, 3.0f, nullptr),
        };
        return kFields;
    }
};

struct MotionBlurData
{
    PostFxFrameHeader frame;
    Matrix4x4 inverseViewProjection{};
    Matrix4x4 previousViewProjection{};
    float strength = 0.5f;
    int samples = 12;
    float maxBlur = 0.05f;
    int hasPrevious = 0;
};

/// <summary>カメラのモーションブラー</summary>
class MotionBlurParams final : public FxComputeParams<MotionBlurData>
{
    HAGINE_FX_IDENTITY(MotionBlur, "MotionBlur")
  public:
    std::vector<ComputeInput> GetComputeInputs() const override { return {ComputeInput::SourceColor, ComputeInput::SceneDepth}; }

    void SetCameraInfo(const PostEffectCameraInfo &info) override
    {
        // 前のフレームの行列は「1つ前に受け取った物」。初めの1回は比べる相手が無いのでぼかさない
        pData_->inverseViewProjection = info.inverseViewProjection;
        pData_->previousViewProjection = hasPrevious_ ? previousViewProjection_ : info.viewProjection;
        pData_->hasPrevious = hasPrevious_ ? 1 : 0;
        previousViewProjection_ = info.viewProjection;
        hasPrevious_ = true;
    }

  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_NOTE("カメラが動いた向きへぼかします（物だけが動いてもぼけません）"),
            HAGINE_FX_FLOAT(MotionBlurData, strength, "強さ", 0.01f, 0.0f, 4.0f, "シャッターの開いている長さ。1で1フレーム分"),
            HAGINE_FX_INT(MotionBlurData, samples, "分割数", 2.0f, 32.0f, "多いほどなめらかで重い"),
            HAGINE_FX_FLOAT(MotionBlurData, maxBlur, "ぼけの上限", 0.001f, 0.0f, 0.5f, "画面の幅を1とした長さ。カメラが切り替わった瞬間の大ぶれを抑えます"),
        };
        return kFields;
    }

  private:
    Matrix4x4 previousViewProjection_{};
    bool hasPrevious_ = false;
};

struct DirectionalBlurData
{
    PostFxFrameHeader frame;
    float angle = 0.0f;
    float length = 24.0f;
    int samples = 16;
    float centerMask = 0.0f;
};

/// <summary>方向ブラー</summary>
class DirectionalBlurParams final : public FxComputeParams<DirectionalBlurData>
{
    HAGINE_FX_IDENTITY(DirectionalBlur, "DirectionalBlur")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(DirectionalBlurData, angle, "向き（度）", 1.0f, -180.0f, 180.0f, nullptr),
            HAGINE_FX_FLOAT(DirectionalBlurData, length, "長さ（ピクセル）", 0.2f, 0.0f, 200.0f, nullptr),
            HAGINE_FX_INT(DirectionalBlurData, samples, "分割数", 2.0f, 48.0f, nullptr),
            HAGINE_FX_FLOAT(DirectionalBlurData, centerMask, "中心はぼかさない", 0.01f, 0.0f, 1.0f, "上げると画面の中心がくっきり残ります"),
        };
        return kFields;
    }
};

// ============================================================
//  光・レンズ
// ============================================================

struct LensFlareData
{
    PostFxFrameHeader frame;
    float threshold = 1.0f;
    float intensity = 0.3f;
    int ghostCount = 5;
    float ghostSpacing = 0.35f;
    float haloRadius = 0.45f;
    float haloIntensity = 0.5f;
    float chroma = 0.01f;
    float padding = 0.0f;
    Vector3 tint = {1.0f, 0.95f, 0.9f};
    float padding2 = 0.0f;
};

/// <summary>レンズフレア</summary>
class LensFlareParams final : public FxComputeParams<LensFlareData>
{
    HAGINE_FX_IDENTITY(LensFlare, "LensFlare")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(LensFlareData, threshold, "光る明るさ", 0.01f, 0.0f, 20.0f, "これより明るい所（太陽・強い光源）だけがフレアになります"),
            HAGINE_FX_FLOAT(LensFlareData, intensity, "ゴーストの明るさ", 0.005f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_INT(LensFlareData, ghostCount, "ゴーストの数", 0.0f, 8.0f, nullptr),
            HAGINE_FX_FLOAT(LensFlareData, ghostSpacing, "ゴーストの間隔", 0.005f, 0.0f, 1.5f, nullptr),
            HAGINE_FX_FLOAT(LensFlareData, haloRadius, "ハローの半径", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(LensFlareData, haloIntensity, "ハローの明るさ", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(LensFlareData, chroma, "色のにじみ", 0.0005f, 0.0f, 0.05f, nullptr),
            HAGINE_FX_COLOR3(LensFlareData, tint, "色", nullptr),
        };
        return kFields;
    }
};

struct AnamorphicStreakData
{
    PostFxFrameHeader frame;
    float threshold = 0.9f;
    float intensity = 0.8f;
    float length = 250.0f;
    int samples = 32;
    Vector3 tint = {0.4f, 0.6f, 1.0f};
    float falloff = 3.0f;
    float angle = 0.0f;
    float padding[3] = {};
};

/// <summary>アナモルフィックの光の筋</summary>
class AnamorphicStreakParams final : public FxComputeParams<AnamorphicStreakData>
{
    HAGINE_FX_IDENTITY(AnamorphicStreak, "AnamorphicStreak")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(AnamorphicStreakData, threshold, "光る明るさ", 0.01f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(AnamorphicStreakData, intensity, "筋の明るさ", 0.01f, 0.0f, 5.0f, nullptr),
            HAGINE_FX_FLOAT(AnamorphicStreakData, length, "長さ（ピクセル）", 1.0f, 10.0f, 1500.0f, nullptr),
            HAGINE_FX_FLOAT(AnamorphicStreakData, falloff, "先の弱まり方", 0.05f, 0.0f, 10.0f, nullptr),
            HAGINE_FX_INT(AnamorphicStreakData, samples, "分割数（片側）", 4.0f, 64.0f, nullptr),
            HAGINE_FX_FLOAT(AnamorphicStreakData, angle, "向き（度）", 1.0f, -90.0f, 90.0f, nullptr),
            HAGINE_FX_COLOR3(AnamorphicStreakData, tint, "色", "青みがかった色が定番"),
        };
        return kFields;
    }
};

struct ScreenGodRaysData
{
    PostFxFrameHeader frame;
    Vector2 lightUv = {0.5f, 0.3f};
    float density = 0.9f;
    float weight = 0.05f;
    float decay = 0.97f;
    float exposure = 0.6f;
    float threshold = 0.8f;
    int samples = 64;
    Vector3 tint = {1.0f, 0.9f, 0.7f};
    float visibility = 1.0f;
    int skyOnly = 1;
    float sunDisk = 1.5f;
    float sunSize = 0.06f;
    float padding = 0.0f;
};

/// <summary>光芒（画面空間）</summary>
class ScreenGodRaysParams final : public FxComputeParams<ScreenGodRaysData>
{
    HAGINE_FX_IDENTITY(ScreenGodRays, "ScreenGodRays")
  public:
    std::vector<ComputeInput> GetComputeInputs() const override { return {ComputeInput::SourceColor, ComputeInput::SceneDepth}; }

    /// <summary>
    /// 光源の位置をシーンの太陽から求めるか（false なら GetData()->lightUv を演出側が決める）
    /// </summary>
    void SetFollowSun(bool follow)
    {
        // 太陽がカメラの後ろにあった時の「見えない」を引きずらないよう、手で決めるときは見える状態から始める
        if (followSun_ && !follow)
        {
            pData_->visibility = 1.0f;
        }
        followSun_ = follow;
    }

    void SetCameraInfo(const PostEffectCameraInfo &info) override
    {
        if (!followSun_)
        {
            return;
        }
        // 太陽は「光が進む向きの逆」のずっと先にある。そこを画面へ写す
        const Vector3 &d = info.sunDirection;
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length < 1e-5f)
        {
            pData_->visibility = 0.0f;
            return;
        }
        const float distance = 1000.0f / length;
        const Vector3 sun = {info.cameraPosition.x - d.x * distance, info.cameraPosition.y - d.y * distance,
                             info.cameraPosition.z - d.z * distance};
        const Matrix4x4 &m = info.viewProjection;
        const float clipX = sun.x * m.m[0][0] + sun.y * m.m[1][0] + sun.z * m.m[2][0] + m.m[3][0];
        const float clipY = sun.x * m.m[0][1] + sun.y * m.m[1][1] + sun.z * m.m[2][1] + m.m[3][1];
        const float clipW = sun.x * m.m[0][3] + sun.y * m.m[1][3] + sun.z * m.m[2][3] + m.m[3][3];
        if (clipW <= 1e-4f)
        {
            pData_->visibility = 0.0f; // カメラの後ろ
            return;
        }
        const float ndcX = clipX / clipW;
        const float ndcY = clipY / clipW;
        pData_->lightUv = {ndcX * 0.5f + 0.5f, 0.5f - ndcY * 0.5f};
        // 画面の外へ出るほど薄くする（画面の端から半画面ぶん外で消える）
        const float outside = (std::max)(std::abs(ndcX), std::abs(ndcY)) - 1.0f;
        pData_->visibility = std::clamp(1.0f - outside, 0.0f, 1.0f);
    }

  protected:
    void DrawExtraUI() override
    {
#ifdef USE_IMGUI
        bool follow = followSun_;
        if (ImGui::Checkbox("シーンの太陽に合わせる", &follow))
        {
            SetFollowSun(follow);
        }
        ImGui::SetItemTooltip("平行光源の向きから太陽の位置を求めます。外すと下の「光源の位置」を手で決められます");
#endif
    }
    void SaveExtra(DataHandler *handler, const std::string &prefix) const override { handler->Save<bool>(prefix + "followSun", followSun_); }
    void LoadExtra(DataHandler *handler, const std::string &prefix) override { followSun_ = handler->Load<bool>(prefix + "followSun", true); }

    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT2(ScreenGodRaysData, lightUv, "光源の位置（UV）", 0.005f, -1.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(ScreenGodRaysData, visibility, "見え具合", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_TOGGLE(ScreenGodRaysData, skyOnly, "空だけから出す", "物に当たった明るい所からは筋を出しません"),
            HAGINE_FX_FLOAT(ScreenGodRaysData, sunDisk, "太陽の円の明るさ", 0.01f, 0.0f, 10.0f, "光源の位置に明るい円を足して、そこから筋を出します。空が明るくないシーンでも筋が出ます"),
            HAGINE_FX_FLOAT(ScreenGodRaysData, sunSize, "太陽の円の大きさ", 0.001f, 0.0f, 0.5f, nullptr),
            HAGINE_FX_FLOAT(ScreenGodRaysData, threshold, "光る明るさ", 0.01f, 0.0f, 10.0f, "これより明るい空（太陽など）からも筋を出します"),
            HAGINE_FX_FLOAT(ScreenGodRaysData, exposure, "明るさ", 0.01f, 0.0f, 5.0f, nullptr),
            HAGINE_FX_FLOAT(ScreenGodRaysData, density, "筋の長さ", 0.005f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(ScreenGodRaysData, weight, "1回の明るさ", 0.001f, 0.0f, 0.5f, nullptr),
            HAGINE_FX_FLOAT(ScreenGodRaysData, decay, "弱まり方", 0.001f, 0.8f, 1.0f, nullptr),
            HAGINE_FX_INT(ScreenGodRaysData, samples, "分割数", 8.0f, 128.0f, nullptr),
            HAGINE_FX_COLOR3(ScreenGodRaysData, tint, "光の色", nullptr),
        };
        return kFields;
    }

  private:
    bool followSun_ = true;
};

struct NeonEdgeData
{
    PostFxFrameHeader frame;
    Vector3 color = {0.2f, 0.9f, 1.0f};
    float threshold = 0.08f;
    float glow = 6.0f;
    float intensity = 3.0f;
    float hueSpeed = 0.1f;
    int colorMode = 1;
    float background = 0.15f;
    float padding[3] = {};
};

/// <summary>ネオン輪郭</summary>
class NeonEdgeParams final : public FxComputeParams<NeonEdgeData>
{
    HAGINE_FX_IDENTITY(NeonEdge, "NeonEdge")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_COMBO(NeonEdgeData, colorMode, "線の色", "元の色\0虹色\0単色\0", nullptr),
            HAGINE_FX_COLOR3(NeonEdgeData, color, "単色のときの色", nullptr),
            HAGINE_FX_FLOAT(NeonEdgeData, hueSpeed, "虹色を流す速さ", 0.005f, -2.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(NeonEdgeData, threshold, "線にする差", 0.002f, 0.0f, 1.0f, "小さいほど線が増えます"),
            HAGINE_FX_FLOAT(NeonEdgeData, glow, "光のにじみ（ピクセル）", 0.1f, 0.0f, 30.0f, nullptr),
            HAGINE_FX_FLOAT(NeonEdgeData, intensity, "線の明るさ", 0.05f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(NeonEdgeData, background, "元の絵を残す", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

// ============================================================
//  画面の加工
// ============================================================

struct CrtData
{
    PostFxFrameHeader frame;
    float curvature = 0.5f;
    float scanline = 0.5f;
    float mask = 0.3f;
    float vignette = 0.6f;
    float flicker = 0.2f;
    float glow = 0.3f;
    float chroma = 1.0f;
    float lineSize = 3.0f;
};

/// <summary>ブラウン管</summary>
class CrtParams final : public FxComputeParams<CrtData>
{
    HAGINE_FX_IDENTITY(Crt, "Crt")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(CrtData, curvature, "画面の丸み", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, scanline, "走査線", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, lineSize, "走査線の太さ（ピクセル）", 0.05f, 1.0f, 12.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, mask, "RGBの縦じま", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, glow, "光のにじみ", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, chroma, "色ずれ（ピクセル）", 0.05f, 0.0f, 8.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, vignette, "四隅の暗さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(CrtData, flicker, "ちらつき", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct GlitchData
{
    PostFxFrameHeader frame;
    float intensity = 0.6f;
    float blockSize = 24.0f;
    float speed = 12.0f;
    float rgbSplit = 12.0f;
    float lineNoise = 0.5f;
    float colorDrop = 0.3f;
    float frequency = 0.35f;
    float padding = 0.0f;
};

/// <summary>グリッチ</summary>
class GlitchParams final : public FxComputeParams<GlitchData>
{
    HAGINE_FX_IDENTITY(Glitch, "Glitch")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(GlitchData, intensity, "強さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(GlitchData, frequency, "起きる頻度", 0.01f, 0.0f, 1.0f, "1で常に乱れる"),
            HAGINE_FX_FLOAT(GlitchData, speed, "切り替わる速さ（回/秒）", 0.1f, 0.5f, 60.0f, nullptr),
            HAGINE_FX_FLOAT(GlitchData, blockSize, "ブロックの高さ（ピクセル）", 0.5f, 2.0f, 200.0f, nullptr),
            HAGINE_FX_FLOAT(GlitchData, rgbSplit, "RGBの分かれ幅（ピクセル）", 0.1f, 0.0f, 80.0f, nullptr),
            HAGINE_FX_FLOAT(GlitchData, lineNoise, "横線のずれ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(GlitchData, colorDrop, "色化け", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct VhsData
{
    PostFxFrameHeader frame;
    float wobble = 2.0f;
    float chromaBleed = 6.0f;
    float noise = 0.06f;
    float tracking = 0.5f;
    float saturation = 0.8f;
    float blur = 1.5f;
    float scanline = 0.1f;
    float padding = 0.0f;
};

/// <summary>VHS（ビデオテープ）</summary>
class VhsParams final : public FxComputeParams<VhsData>
{
    HAGINE_FX_IDENTITY(Vhs, "Vhs")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(VhsData, wobble, "横の揺れ（ピクセル）", 0.05f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(VhsData, chromaBleed, "色のにじみ（ピクセル）", 0.1f, 0.0f, 30.0f, nullptr),
            HAGINE_FX_FLOAT(VhsData, blur, "甘さ（ピクセル）", 0.05f, 0.0f, 8.0f, nullptr),
            HAGINE_FX_FLOAT(VhsData, tracking, "乱れ帯", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(VhsData, noise, "砂嵐", 0.002f, 0.0f, 0.5f, nullptr),
            HAGINE_FX_FLOAT(VhsData, saturation, "彩度", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(VhsData, scanline, "走査線", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct OldFilmData
{
    PostFxFrameHeader frame;
    float sepia = 0.8f;
    float scratches = 0.4f;
    float dust = 0.5f;
    float flicker = 0.4f;
    float vignette = 0.7f;
    float jitter = 0.5f;
    float grain = 0.08f;
    float frameRate = 18.0f;
};

/// <summary>古いフィルム</summary>
class OldFilmParams final : public FxComputeParams<OldFilmData>
{
    HAGINE_FX_IDENTITY(OldFilm, "OldFilm")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(OldFilmData, sepia, "セピア", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, scratches, "縦の傷", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, dust, "ほこり", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, flicker, "明るさのちらつき", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, jitter, "コマのがたつき", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, grain, "粒子", 0.002f, 0.0f, 0.4f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, vignette, "周辺減光", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(OldFilmData, frameRate, "コマの速さ（回/秒）", 0.1f, 1.0f, 60.0f, "傷やほこりが入れ替わる速さ"),
        };
        return kFields;
    }
};

struct AfterimageData
{
    PostFxFrameHeader frame;
    float persistence = 0.8f;
    int mode = 0;
    float tintStrength = 0.0f;
    int reset = 1;
    Vector3 tint = {0.4f, 0.7f, 1.0f};
    float padding = 0.0f;
};

/// <summary>
/// 残像（モーショントレイル）。自分の出力を次のフレームまで取っておき、重ねて描く
/// </summary>
class AfterimageParams final : public FxComputeParams<AfterimageData>
{
    HAGINE_FX_IDENTITY(Afterimage, "Afterimage")
  public:
    void Initialize(DirectXCommon *pDxCommon) override
    {
        FxComputeParams<AfterimageData>::Initialize(pDxCommon);
        D3D12_CLEAR_VALUE clearValue = pDxCommon->GetClearColorValue();
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        history_ = pDxCommon->CreateRenderTextureResource(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight(),
                                                          DXGI_FORMAT_R16G16B16A16_FLOAT, clearValue);
    }

    std::vector<ComputeInput> GetComputeInputs() const override { return {ComputeInput::SourceColor, ComputeInput::History}; }
    ID3D12Resource *GetHistoryResource() const override { return history_.Get(); }

    void OnComputeFinished(ID3D12GraphicsCommandList *pCommandList, ID3D12Resource *pOutput, DirectXCommon *pDxCommon) override
    {
        // 今の結果を次のフレームの「前の結果」として写しておく
        pDxCommon->BarrierTransition(pOutput, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pDxCommon->BarrierTransition(history_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
        pCommandList->CopyResource(history_.Get(), pOutput);
        pDxCommon->BarrierTransition(history_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        pDxCommon->BarrierTransition(pOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_GENERIC_READ);
        pData_->reset = 0;
    }

  protected:
    void OnUpdate(float) override
    {
        // しばらく使われていなかったら（無効にしていた・作り直した直後）前の結果は古いので捨てる
        const auto now = std::chrono::steady_clock::now();
        if (now - lastUpdate_ > std::chrono::milliseconds(200))
        {
            pData_->reset = 1;
        }
        lastUpdate_ = now;
    }

    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(AfterimageData, persistence, "残る割合", 0.005f, 0.0f, 0.98f, "大きいほど長く尾を引きます"),
            HAGINE_FX_COMBO(AfterimageData, mode, "残し方", "混ぜる（全体がぶれる）\0明るい方を残す（光の軌跡）\0", nullptr),
            HAGINE_FX_COLOR3(AfterimageData, tint, "残像の色", nullptr),
            HAGINE_FX_FLOAT(AfterimageData, tintStrength, "色を付ける量", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> history_;
    std::chrono::steady_clock::time_point lastUpdate_{};
};

// ============================================================
//  ゲーム演出
// ============================================================

struct LetterboxData
{
    PostFxFrameHeader frame;
    Vector3 color = {0.0f, 0.0f, 0.0f};
    float amount = 1.0f;
    float aspect = 2.35f;
    float softness = 1.0f;
    float opacity = 1.0f;
    float padding = 0.0f;
};

/// <summary>黒帯（レターボックス）</summary>
class LetterboxParams final : public FxComputeParams<LetterboxData>
{
    HAGINE_FX_IDENTITY(Letterbox, "Letterbox")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(LetterboxData, amount, "出し具合", 0.005f, 0.0f, 1.0f, "0→1 に動かすと帯がせり出します（ムービーに入る演出）"),
            HAGINE_FX_FLOAT(LetterboxData, aspect, "画面の比率（横/縦）", 0.01f, 0.5f, 4.0f, "2.35 でシネスコ、1.0 で正方形"),
            HAGINE_FX_COLOR3(LetterboxData, color, "帯の色", nullptr),
            HAGINE_FX_FLOAT(LetterboxData, opacity, "帯の濃さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(LetterboxData, softness, "境目のぼかし（ピクセル）", 0.1f, 0.5f, 60.0f, nullptr),
        };
        return kFields;
    }
};

struct SpotlightData
{
    PostFxFrameHeader frame;
    Vector2 center = {0.5f, 0.5f};
    float radius = 0.3f;
    float softness = 0.4f;
    Vector3 darkColor = {0.0f, 0.0f, 0.02f};
    float darkness = 0.9f;
    float pulse = 0.03f;
    float pulseSpeed = 4.0f;
    float aspectX = 1.0f;
    float padding = 0.0f;
};

/// <summary>スポットライト</summary>
class SpotlightParams final : public FxComputeParams<SpotlightData>
{
    HAGINE_FX_IDENTITY(Spotlight, "Spotlight")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT2(SpotlightData, center, "光の中心（UV）", 0.005f, -0.5f, 1.5f, "プレイヤーの画面上の位置などを演出側から入れる"),
            HAGINE_FX_FLOAT(SpotlightData, radius, "半径", 0.005f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(SpotlightData, softness, "ふちのぼかし", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(SpotlightData, aspectX, "横の伸び", 0.01f, 0.2f, 5.0f, "1で真円"),
            HAGINE_FX_FLOAT(SpotlightData, darkness, "外の暗さ", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_COLOR3(SpotlightData, darkColor, "暗い所の色", nullptr),
            HAGINE_FX_FLOAT(SpotlightData, pulse, "ゆらめき", 0.002f, 0.0f, 0.5f, "懐中電灯や炎のようなゆらぎ"),
            HAGINE_FX_FLOAT(SpotlightData, pulseSpeed, "ゆらめきの速さ", 0.05f, 0.0f, 30.0f, nullptr),
        };
        return kFields;
    }
};

struct DangerVignetteData
{
    PostFxFrameHeader frame;
    Vector3 color = {0.75f, 0.0f, 0.02f};
    float intensity = 0.6f;
    float pulseSpeed = 1.2f;
    float pulseAmount = 0.5f;
    float radius = 0.4f;
    float desaturate = 0.5f;
    float veins = 0.3f;
    float padding[3] = {};
};

/// <summary>ピンチ演出（赤い縁の脈動）</summary>
class DangerVignetteParams final : public FxComputeParams<DangerVignetteData>
{
    HAGINE_FX_IDENTITY(DangerVignette, "DangerVignette")
  protected:
    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT(DangerVignetteData, intensity, "強さ", 0.01f, 0.0f, 1.0f, "体力の減り具合に合わせて演出側から動かす値"),
            HAGINE_FX_COLOR3(DangerVignetteData, color, "縁の色", nullptr),
            HAGINE_FX_FLOAT(DangerVignetteData, radius, "縁の広さ", 0.005f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(DangerVignetteData, pulseSpeed, "鼓動の速さ（回/秒）", 0.01f, 0.0f, 5.0f, nullptr),
            HAGINE_FX_FLOAT(DangerVignetteData, pulseAmount, "鼓動の大きさ", 0.01f, 0.0f, 2.0f, nullptr),
            HAGINE_FX_FLOAT(DangerVignetteData, veins, "縁のむら", 0.01f, 0.0f, 1.0f, nullptr),
            HAGINE_FX_FLOAT(DangerVignetteData, desaturate, "色の抜け", 0.01f, 0.0f, 1.0f, nullptr),
        };
        return kFields;
    }
};

struct WorldScanData
{
    PostFxFrameHeader frame;
    Matrix4x4 inverseViewProjection{};
    Vector3 origin = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    Vector3 color = {0.2f, 0.8f, 1.0f};
    float width = 1.5f;
    float intensity = 2.0f;
    float trail = 12.0f;
    float grid = 0.6f;
    float gridSize = 1.0f;
    float fade = 0.0f;
    float padding[3] = {};
};

/// <summary>
/// ワールドスキャン（ソナー）。Trigger で地点を決めると、そこから輪が広がっていく
/// </summary>
class WorldScanParams final : public FxComputeParams<WorldScanData>
{
    HAGINE_FX_IDENTITY(WorldScan, "WorldScan")
  public:
    std::vector<ComputeInput> GetComputeInputs() const override { return {ComputeInput::SourceColor, ComputeInput::SceneDepth}; }

    void SetCameraInfo(const PostEffectCameraInfo &info) override
    {
        pData_->inverseViewProjection = info.inverseViewProjection;
        lastCameraPosition_ = info.cameraPosition;
    }

    /// <summary>
    /// その地点から輪を広げ始める
    /// </summary>
    /// <param name="origin">広がり始める地点（ワールド座標）</param>
    void Trigger(const Vector3 &origin)
    {
        pData_->origin = origin;
        pData_->radius = 0.0f;
        playing_ = true;
    }

  protected:
    void OnUpdate(float deltaTime) override
    {
        if (!playing_)
        {
            pData_->fade = 0.0f;
            return;
        }
        pData_->radius += speed_ * deltaTime;
        // 広がりきる手前から薄くして消す
        const float t = std::clamp((pData_->radius - maxRange_ * 0.6f) / (maxRange_ * 0.4f), 0.0f, 1.0f);
        pData_->fade = 1.0f - t * t;
        if (pData_->radius >= maxRange_)
        {
            if (loop_)
            {
                pData_->radius = 0.0f;
            }
            else
            {
                playing_ = false;
                pData_->fade = 0.0f;
            }
        }
    }

    void DrawExtraUI() override
    {
#ifdef USE_IMGUI
        if (PrimaryButton(ICON_FA_BROADCAST_TOWER " カメラの位置から広げる"))
        {
            Trigger(lastCameraPosition_);
        }
        ImGui::SameLine();
        if (NeutralButton(ICON_FA_CROSSHAIRS " 中心の地点から広げる"))
        {
            Trigger(pData_->origin);
        }
        ImGui::SetItemTooltip("下の「広がり始める地点」から広げます。ゲームからは WorldScanParams::Trigger(位置) を呼びます");
        ImGui::DragFloat("広がる速さ", &speed_, 0.1f, 0.1f, 500.0f);
        ImGui::DragFloat("届く距離", &maxRange_, 0.5f, 1.0f, 2000.0f);
        ImGui::Checkbox("繰り返す", &loop_);
#endif
    }
    void SaveExtra(DataHandler *handler, const std::string &prefix) const override
    {
        handler->Save<float>(prefix + "speed", speed_);
        handler->Save<float>(prefix + "maxRange", maxRange_);
        handler->Save<bool>(prefix + "loop", loop_);
    }
    void LoadExtra(DataHandler *handler, const std::string &prefix) override
    {
        speed_ = handler->Load<float>(prefix + "speed", 25.0f);
        maxRange_ = handler->Load<float>(prefix + "maxRange", 80.0f);
        loop_ = handler->Load<bool>(prefix + "loop", false);
        playing_ = loop_;
    }

    std::span<const FxField> Fields() const override
    {
        static const FxField kFields[] = {
            HAGINE_FX_FLOAT3(WorldScanData, origin, "広がり始める地点", 0.1f, -10000.0f, 10000.0f, nullptr),
            HAGINE_FX_COLOR3(WorldScanData, color, "色", nullptr),
            HAGINE_FX_FLOAT(WorldScanData, intensity, "輪の明るさ", 0.05f, 0.0f, 20.0f, nullptr),
            HAGINE_FX_FLOAT(WorldScanData, width, "輪の太さ", 0.05f, 0.05f, 50.0f, nullptr),
            HAGINE_FX_FLOAT(WorldScanData, trail, "後ろに残る色の長さ", 0.1f, 0.0f, 200.0f, nullptr),
            HAGINE_FX_FLOAT(WorldScanData, grid, "地面の格子", 0.01f, 0.0f, 3.0f, nullptr),
            HAGINE_FX_FLOAT(WorldScanData, gridSize, "格子の間隔", 0.05f, 0.1f, 50.0f, nullptr),
        };
        return kFields;
    }

  private:
    float speed_ = 25.0f;
    float maxRange_ = 80.0f;
    bool loop_ = false;
    bool playing_ = false;
    Vector3 lastCameraPosition_{};
};

#undef HAGINE_FX_IDENTITY

} // namespace Hagine
