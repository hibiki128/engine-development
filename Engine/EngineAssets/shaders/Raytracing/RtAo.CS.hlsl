// =============================================================
// レイトレーシングによるアンビエントオクルージョン（インラインRT / RayQuery）
//
// 深度バッファからワールド座標を復元し、その点の法線まわりの半球へ短いレイを何本か飛ばす。
// 当たった本数の割合がそのまま「どれだけ周りに囲まれているか」になる。
//
// SSAO との違いは **画面に写っていない物も遮蔽に数えられる** こと。
// SSAO は深度バッファしか見ないので、カメラの外にある壁や、手前の物の裏側は
// 遮蔽物として存在しないことになっていた。ここではシーン全体の加速構造を見るので
// その穴が無い。代わりにレイのぶんノイズが乗るので、後段でならす。
//
// **シェーダーテーブルも DXR 用のステートオブジェクトも要らない。**
// 普通のコンピュートシェーダーの中で RayQuery を回すだけ（cs_6_5 以上が必要）。
// =============================================================

struct RtAoConstants
{
    float4x4 inverseViewProjection; // NDC → ワールド

    float radius;     // 遮蔽を探す距離（これより遠い物は数えない）
    float normalBias; // 自分自身に当たらないよう、始点を法線方向へ押し出す量
    float intensity;  // 遮蔽の効かせ具合
    int sampleCount;  // 1画素あたりに飛ばすレイの本数

    int2 textureSize; // 出力の解像度
    float power;      // 陰りの立ち上がり（大きいほど暗い所だけ残る）
    float pad;
};
ConstantBuffer<RtAoConstants> gConstants : register(b0);

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gNormal : register(t2);
RWTexture2D<float> gAmbientOcclusion : register(u0);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(float2 uv, float rawDepth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);
    float4 world = mul(ndc, gConstants.inverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// 画素ごとに散らばる 0〜1 の値。レイの向きを画素ごとにずらして縞を防ぐ。
/// 時間で動かすとTAAが無いぶんチラつくので、ここでは静的にしている
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

/// ビットを逆順にして 0〜1 に写す（ファンデルコルプット列）。
/// 乱数より偏りが少ないので、少ない本数でもムラが出にくい
float RadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

/// 法線まわりの半球へ、真上ほど密になるように向きを1本決める。
/// こう散らすと「当たった割合＝コサインで重み付けした遮蔽率」になるので、
/// 重みを別に掛ける必要が無い
float3 CosineSampleHemisphere(float2 random, float3 normal)
{
    const float radius = sqrt(saturate(random.x));
    const float phi = 6.2831853f * random.y;

    // 法線に垂直な2軸を作る。法線がY軸と重なるときだけ別の軸を使う
    const float3 helper = (abs(normal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(helper, normal));
    const float3 bitangent = cross(normal, tangent);

    return normalize(tangent * (radius * cos(phi)) +
                     bitangent * (radius * sin(phi)) +
                     normal * sqrt(saturate(1.0f - random.x)));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gConstants.textureSize.x || pixel.y >= gConstants.textureSize.y)
    {
        return;
    }

    const float rawDepth = gDepth.Load(int3(pixel, 0));

    // 空には遮蔽を掛けない
    if (rawDepth >= 1.0f)
    {
        gAmbientOcclusion[pixel] = 1.0f;
        return;
    }

    // 法線が無い画素（前方描画で描かれた物）は向きが決められないので素通しにする。
    // ここで暗くすると、G-Bufferに載っていない地面だけが理由なく黒ずむ
    const float4 normalSample = gNormal.Load(int3(pixel, 0));
    if (dot(normalSample.xyz, normalSample.xyz) <= 1e-6f)
    {
        gAmbientOcclusion[pixel] = 1.0f;
        return;
    }
    const float3 normal = normalize(normalSample.xyz);

    const float2 uv = (float2(pixel) + 0.5f) / float2(gConstants.textureSize);
    const float3 worldPosition = ReconstructWorldPosition(uv, rawDepth);
    const float3 rayOrigin = worldPosition + normal * max(gConstants.normalBias, 1e-4f);

    // 画素ごとに列をずらす。これをしないと同じ方向の並びが画面全体に出て、縞になる
    const float rotation = InterleavedGradientNoise(float2(pixel));

    const int sampleCount = clamp(gConstants.sampleCount, 1, 64);
    const float rayLength = max(gConstants.radius, 1e-3f);

    float occludedCount = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        float2 random;
        random.x = (float(i) + 0.5f) / float(sampleCount);
        random.y = frac(RadicalInverse(uint(i)) + rotation);

        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = CosineSampleHemisphere(random, normal);
        ray.TMin = 0.0f;
        ray.TMax = rayLength;

        // 遮蔽は「何かに当たったか」だけ分かればよいので、最初の交差で打ち切ってよい
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();

        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            occludedCount += 1.0f;
        }
    }

    const float occlusion = saturate(occludedCount / float(sampleCount) * max(gConstants.intensity, 0.0f));
    gAmbientOcclusion[pixel] = pow(saturate(1.0f - occlusion), max(gConstants.power, 0.01f));
}
