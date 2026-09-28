// ============================================================
// SSAO（スクリーンスペースアンビエントオクルージョン）
//
// 深度と法線だけを見て「その点がどれだけ周りに囲まれているか」を測る。
// 物と床の接地部分や、へこみ・隙間に自然な陰りが入り、
// 置いてあるものが地に足のついた見え方になる。
// ============================================================

Texture2D<float> gDepth : register(t0);
Texture2D<float4> gNormal : register(t1);
RWTexture2D<float> gOutput : register(u0);
SamplerState gSampler : register(s0);

struct SsaoParameters
{
    matrix invViewProjection; // クリップ→ワールド復元用
    matrix view;              // ワールド→ビュー
    matrix projection;        // ビュー→クリップ

    uint2 screenSize; // 描画解像度
    float radius;     // どれだけ離れた所まで遮蔽として見るか[ワールド単位]
    float bias;       // 自分自身を遮蔽と誤判定しないための余裕

    float intensity;  // 効きの強さ
    float power;      // 陰りのコントラスト（大きいほどくっきり）
    int sampleCount;  // サンプル数
    float maxDistance; // これ以上離れた点は遮蔽に数えない（遠景の誤検出対策）
};
ConstantBuffer<SsaoParameters> gSsao : register(b0);

/// 0〜1 の擬似乱数
float Hash(float2 position)
{
    return frac(sin(dot(position, float2(12.9898f, 78.233f))) * 43758.5453f);
}

/// ワールド座標を復元する
float3 ReconstructWorld(float2 uv, float deviceDepth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, deviceDepth, 1.0f);
    const float4 world = mul(clip, gSsao.invViewProjection);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gSsao.screenSize.x || pixel.y >= gSsao.screenSize.y)
    {
        return;
    }

    const float deviceDepth = gDepth.Load(int3(pixel, 0));

    // 何も描かれていない所（空）は遮蔽なしにする
    if (deviceDepth >= 1.0f)
    {
        gOutput[pixel] = 1.0f;
        return;
    }

    const float4 normalSample = gNormal.Load(int3(pixel, 0));
    const float3 normal = normalize(normalSample.xyz);
    // 法線が書かれていない（ライティング対象外の）画素は素通し
    if (dot(normalSample.xyz, normalSample.xyz) < 0.0001f)
    {
        gOutput[pixel] = 1.0f;
        return;
    }

    const float2 uv = (float2(pixel) + 0.5f) / float2(gSsao.screenSize);
    const float3 worldPosition = ReconstructWorld(uv, deviceDepth);
    const float3 viewPosition = mul(float4(worldPosition, 1.0f), gSsao.view).xyz;

    // 法線を軸にした半球の中へサンプルを飛ばすための直交基底を作る。
    // ランダムな向きを起点にすることで、格子状のムラを防ぐ
    const float randomAngle = Hash(float2(pixel)) * 6.2831853f;
    const float3 randomVector = float3(cos(randomAngle), sin(randomAngle), 0.0f);
    const float3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
    const float3 bitangent = cross(normal, tangent);
    const float3x3 tbn = float3x3(tangent, bitangent, normal);

    const int count = clamp(gSsao.sampleCount, 4, 32);
    float occlusion = 0.0f;

    for (int i = 0; i < count; ++i)
    {
        // 半球内に散らした点を作る。中心に寄せると近くの遮蔽をよく拾う
        const float u1 = Hash(float2(pixel) + float2(i * 1.37f, i * 2.71f));
        const float u2 = Hash(float2(pixel) + float2(i * 3.11f, i * 0.57f));
        const float u3 = Hash(float2(pixel) + float2(i * 5.23f, i * 7.13f));

        float3 sampleDirection = float3(u1 * 2.0f - 1.0f, u2 * 2.0f - 1.0f, u3);
        sampleDirection = normalize(sampleDirection);
        // 半球の内側に寄せる（サンプルを外周に均等配置すると効きが弱くなる）
        float scale = (float)i / (float)count;
        scale = lerp(0.1f, 1.0f, scale * scale);

        const float3 samplePosition = worldPosition + mul(sampleDirection, tbn) * gSsao.radius * scale;

        // その点を画面へ投影して、実際にそこに写っているものの深度と比べる
        const float4 sampleClip = mul(float4(samplePosition, 1.0f), mul(gSsao.view, gSsao.projection));
        if (sampleClip.w <= 0.0f)
        {
            continue;
        }
        const float3 sampleNdc = sampleClip.xyz / sampleClip.w;
        const float2 sampleUv = float2(sampleNdc.x * 0.5f + 0.5f, 0.5f - sampleNdc.y * 0.5f);
        if (sampleUv.x < 0.0f || sampleUv.x > 1.0f || sampleUv.y < 0.0f || sampleUv.y > 1.0f)
        {
            continue;
        }

        const uint2 samplePixel = (uint2)(sampleUv * float2(gSsao.screenSize));
        const float sceneDepth = gDepth.Load(int3(samplePixel, 0));
        const float3 sceneWorld = ReconstructWorld(sampleUv, sceneDepth);
        const float sceneViewZ = mul(float4(sceneWorld, 1.0f), gSsao.view).z;
        const float sampleViewZ = mul(float4(samplePosition, 1.0f), gSsao.view).z;

        // 手前に何かあれば遮蔽。ただし極端に手前のもの（別の物体）は数えない
        if (sceneViewZ < sampleViewZ - gSsao.bias)
        {
            const float rangeCheck = smoothstep(0.0f, 1.0f,
                                                gSsao.radius / max(abs(viewPosition.z - sceneViewZ), 0.0001f));
            const float distanceCheck = step(abs(viewPosition.z - sceneViewZ), gSsao.maxDistance);
            occlusion += rangeCheck * distanceCheck;
        }
    }

    occlusion = occlusion / (float)count;
    float ambientOcclusion = saturate(1.0f - occlusion * gSsao.intensity);
    ambientOcclusion = pow(ambientOcclusion, max(gSsao.power, 0.01f));

    gOutput[pixel] = ambientOcclusion;
}
