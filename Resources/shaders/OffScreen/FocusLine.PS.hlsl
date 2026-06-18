#include"FullScreen.hlsli"

// コンスタントバッファ
cbuffer FocusLinesBuffer : register(b0)
{
    float gTime;
    float gNumLines;
    float gLineWidth;
    float gSpeed;
    float gIntensity;
    float gCenterRadius;
    float gMaxDistance;
    float padding1;
    float4 gLineColor;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// ランダム関数
float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 元のテクスチャカラーを取得
    float4 baseColor = gTexture.Sample(gSampler, input.texcoord);
    
    // 中心からの方向ベクトル
    float2 center = float2(0.5, 0.5);
    float2 dir = normalize(input.texcoord - center);
    float distance = length(input.texcoord - center);
    
    float3 lineColor = float3(0.0, 0.0, 0.0);
    
    // 複数の集中線を生成
    for (int i = 0; i < int(gNumLines); i++)
    {
        float lineIndex = float(i);
        
        // 各線の角度をランダムに配置
        float baseAngle = (lineIndex / gNumLines) * 6.28318; // 2π
        float angleVariation = random(float2(lineIndex, 0.0)) * 0.5 - 0.25; // ±0.25ラジアン
        float lineAngle = baseAngle + angleVariation;
        
        // 線の方向ベクトル
        float2 lineDir = float2(cos(lineAngle), sin(lineAngle));
        
        // 現在のピクセルが線上にあるかチェック
        float crossProduct = abs(dir.x * lineDir.y - dir.y * lineDir.x);
        
        // 線の太さを時間で変動させる
        float timeVariation = random(float2(lineIndex, 1.0));
        float dynamicWidth = gLineWidth * (0.5 + 0.5 * sin(gTime * gSpeed + timeVariation * 10.0));
        
        // 線のマスク
        float lineMask = 1.0 - smoothstep(0.0, dynamicWidth, crossProduct);
        
        // 線の長さを時間で変動（バババッと伸びる効果）
        float lengthVariation = random(float2(lineIndex, 2.0));
        float lengthVariation2 = random(float2(lineIndex, 4.0)); // 追加のランダム要素
        
        // 各線に固有の基本長さをランダムに設定
        float baseLength = 0.3 + 0.5 * lengthVariation2; // 0.3～0.8の範囲
        float dynamicLength = baseLength * (0.5 + 0.5 * sin(gTime * gSpeed * 2.0 + lengthVariation * 15.0));
        dynamicLength = max(0.1, dynamicLength); // 最小長さを保証
        
        // 距離に基づく線の表示（中心を除外して周辺に表示）
        float minDistance = gCenterRadius; // 中心除外半径
        float maxDistance = gMaxDistance + 0.2 * lengthVariation2; // 線ごとに異なる最大距離
        
        // 中心除外：minDistance以上の距離で線を表示
        float lengthMask = smoothstep(minDistance - 0.05, minDistance, distance) *
                          smoothstep(maxDistance, maxDistance - dynamicLength * 0.3, distance);
        
        // 線の透明度をランダムに変動
        float alphaVariation = random(float2(lineIndex, 3.0));
        float lineAlpha = 0.3 + 0.7 * sin(gTime * gSpeed * 3.0 + alphaVariation * 20.0);
        lineAlpha = max(0.0, lineAlpha);
        
        // 最終的な線の強度
        float lineStrength = lineMask * lengthMask * lineAlpha * gIntensity;
        
        // 線の色を累積（バッファから色を取得）
        lineColor += gLineColor.rgb * lineStrength;
    }
    
    // 全体の明滅効果
    float globalPulse = 0.8 + 0.2 * sin(gTime * gSpeed * 4.0);
    lineColor *= globalPulse;
    
    // 最終色を計算（加算合成で明るく）
    float3 finalColor = baseColor.rgb + lineColor;
    
    output.color = float4(finalColor, baseColor.a);
    return output;
}