#include "Line3d.hlsli"

struct Camera
{
    matrix viewProject; // ビュープロジェクション行列
};

// ルート定数。静的バッチはローカル座標のままGPUへ常駐させ、ここでワールド行列と色を差し替える。
// 動的線は world=単位行列 / tint=白 で描画される。
struct DrawParams
{
    matrix world; // ワールド行列
    float4 tint;  // 頂点色に乗算する色
};

ConstantBuffer<Camera> gCamera : register(b0);
ConstantBuffer<DrawParams> gDraw : register(b1);

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.pos, 1.0f), gDraw.world);
    output.pos = mul(worldPos, gCamera.viewProject);
    output.color = input.color * gDraw.tint;

    return output;
}
