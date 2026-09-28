// =============================================================
// RT反射（インラインRT / RayQuery）
//
// SSR と同じ「床や水面に周囲を映す」効果だが、交点の探し方が違う。
// SSR は深度バッファを少しずつ進んで交点を探すので
//   ・画面の外に出た瞬間に打ち切られる
//   ・分割が粗いと物をまたいでしまう／細かくすると重い
//   ・厚みの許容量という当てずっぽうの値が要る
// という制約があった。ここではレイを1本飛ばすだけで**正確な交点**が手に入るので、
// その3つがまるごと無くなる。
//
// 交点の色は次の順で決める:
//   1. 何にも当たらなかった → 環境マップ（空）をその向きでサンプルする。
//      **画面の外を向いた反射でも正しい色が出る。SSRには出せなかったもの。**
//   2. 当たった＋その点が画面に写っている → その画素の色を使う（正確）
//   3. 当たった＋画面に写っていない → 色が分からないので「空は映さない」だけにする。
//      何も映さないのが正しい（そこには物があって空を遮っているため）。
//      ここを正しい色にするには、当たった三角形の頂点とマテリアルを
//      シェーダーから引ける仕組み（バインドレス）が要る。
//
// 必要なもの: シーンの深度、G-Buffer の法線、加速構造、環境マップ。
// → **ディファードが有効で、かつレイトレーシングが使えるときだけ動く。**
// =============================================================

cbuffer RtReflectionParams : register(b0)
{
    float4x4 gViewProjection;        // ワールド → クリップ
    float4x4 gInverseViewProjection; // NDC → ワールド
    float3 gCameraPosition;          // カメラのワールド座標
    float gIntensity;                // 反射の強さ

    float gMaxDistance;      // レイを飛ばす最大距離（ワールド単位）
    float gFresnelPower;     // 斜めから見たときに強くなる度合い
    float gReflectance;      // 正面から見たときの反射率（F0）
    float gSkyIntensity;     // 空（環境マップ）の映り込みの強さ

    float gDepthTolerance; // 交点が画面に写っているとみなす深度差
    float gRoughness;      // 反射のばらつき（0で鏡面）
    int gHasNormals;       // G-Bufferの法線が使えるか（0なら素通し）
    int gHasEnvironment;   // 環境マップが使えるか（0なら空を映さない）

    int2 gTextureSize; // 処理対象の解像度
    float2 gPadding;
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gNormal : register(t2);
RaytracingAccelerationStructure gScene : register(t3);
TextureCube<float4> gEnvironment : register(t4);
RWTexture2D<float4> gOutput : register(u0);

SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(float2 uv, float rawDepth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);
    float4 world = mul(ndc, gInverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// ワールド座標を画面UVと深度へ移す
/// 戻り値 xy=UV, z=NDC深度, w=クリップw（カメラの後ろなら0以下）
float4 ProjectToScreen(float3 worldPosition)
{
    float4 clip = mul(float4(worldPosition, 1.0f), gViewProjection);
    if (clip.w <= 1e-6f)
    {
        return float4(0.0f, 0.0f, 0.0f, -1.0f);
    }
    float3 ndc = clip.xyz / clip.w;
    return float4(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ndc.z, clip.w);
}

/// 画素ごとに散らばる 0〜1 の値。ざらつかせた反射のばらつきに使う
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    float4 sourceColor = gSource.Load(int3(pixel, 0));

    // 法線が無い（ディファード無効）ときと、強さ0のときは何もしない
    float rawDepth = gDepth.Load(int3(pixel, 0));
    if (gHasNormals == 0 || gIntensity <= 0.0f || rawDepth >= 1.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    // 前方描画で描かれた物（ライティングOFFなど）は G-Buffer に法線が書かれていない。
    // そのまま normalize すると 0除算で NaN になり、画面が壊れる
    float4 normalSample = gNormal.Load(int3(pixel, 0));
    if (dot(normalSample.xyz, normalSample.xyz) < 1e-6f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }
    float3 normal = normalize(normalSample.xyz);

    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);
    float3 worldPosition = ReconstructWorldPosition(uv, rawDepth);

    float3 viewDirection = normalize(worldPosition - gCameraPosition);
    float3 reflectDirection = reflect(viewDirection, normal);

    // 面の裏側へ潜る向きは映しようがない
    if (dot(reflectDirection, normal) <= 0.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    // ざらついた面にしたいときは向きを少し散らす。0なら完全な鏡面
    if (gRoughness > 0.0f)
    {
        float noise = InterleavedGradientNoise(float2(pixel));
        float angle = noise * 6.2831853f;
        float3 helper = (abs(normal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 tangent = normalize(cross(helper, reflectDirection));
        float3 bitangent = cross(reflectDirection, tangent);
        float spread = gRoughness * frac(noise * 7.0f);
        reflectDirection = normalize(reflectDirection +
                                     (tangent * cos(angle) + bitangent * sin(angle)) * spread);
    }

    // 自分自身に当たらないよう、始点を法線側へ少し浮かせる
    RayDesc ray;
    ray.Origin = worldPosition + normal * 0.02f;
    ray.Direction = reflectDirection;
    ray.TMin = 0.0f;
    ray.TMax = max(gMaxDistance, 1e-3f);

    // 反射は「一番手前に何があるか」を知りたいので、最初の交差で打ち切ってはいけない
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    float3 reflectionColor = float3(0.0f, 0.0f, 0.0f);

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        // ── 何にも当たらなかった ──
        // その向きの空を映す。画面の外を向いていても正しく出るのがRTの強み
        if (gHasEnvironment != 0)
        {
            reflectionColor = gEnvironment.SampleLevel(gSamplerLinear, reflectDirection, 0).rgb *
                              gSkyIntensity;
        }
    }
    else
    {
        // ── 何かに当たった ──
        // その点が画面に写っているなら、写っている色をそのまま持ってくる
        float3 hitPosition = ray.Origin + reflectDirection * query.CommittedRayT();
        float4 projected = ProjectToScreen(hitPosition);

        bool visibleOnScreen = false;
        if (projected.w > 0.0f &&
            projected.x >= 0.0f && projected.x <= 1.0f &&
            projected.y >= 0.0f && projected.y <= 1.0f)
        {
            // 手前に別の物が写っていないか確かめる。
            // 深度が一致していれば「当たった点そのもの」が写っている
            float sceneRawDepth = gDepth.SampleLevel(gSamplerPoint, projected.xy, 0);
            visibleOnScreen = (abs(projected.z - sceneRawDepth) <= gDepthTolerance);
        }

        if (visibleOnScreen)
        {
            reflectionColor = gSource.SampleLevel(gSamplerPoint, projected.xy, 0).rgb;
        }
        // 写っていない場合は色が分からないので何も足さない。
        // 「空が遮られている」ことだけは正しく表現できている
    }

    // 斜めから見るほど強く映り込む（フレネル）
    float cosTheta = saturate(dot(-viewDirection, normal));
    float fresnel = gReflectance + (1.0f - gReflectance) * pow(1.0f - cosTheta, max(gFresnelPower, 0.1f));

    gOutput[pixel] = float4(sourceColor.rgb + reflectionColor * (fresnel * gIntensity), sourceColor.a);
}
