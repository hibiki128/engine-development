#include "../Particle.hlsli"

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

// 描画コンパクション: Update が instanceId 順に詰めた描画データ(DrawCore=40B)を順次読みする。
// 旧 gDrawCore[slot] の散乱gatherを排除。
StructuredBuffer<PDrawCore> gRenderCompact : register(t0);
// 回転グループのみ: instanceId -> 実 slot index（gRotation の散乱参照に使用）
StructuredBuffer<uint> gAliveList : register(t2);
// 生存コンパクション: 当該フレームの生存数（これを超える instanceId は破棄）
StructuredBuffer<uint> gAliveCount : register(t3);
// 回転は回転グループのみ参照（enableRotation==0 なら gather しない）
StructuredBuffer<PRotation> gRotation : register(t4);
ConstantBuffer<PerView> gPerView : register(b0);

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    // instanceCount は生存数の概算(CPU側の1〜2F遅延値+マージン)で発行されるため、
    // 実際の生存数を超える instanceId はここで確実に破棄する。
    if (instanceId >= gAliveCount[0])
    {
        output.position = float4(0.0f, 0.0f, 0.0f, 0.0f); // クリップされる縮退頂点
        output.texcoord = float2(0.0f, 0.0f);
        output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return output;
    }

    // 詰めた順なので instanceId で順次読み（散乱gatherなし・DrawCore=36B）。
    PDrawCore dc = gRenderCompact[instanceId];
    float3 pTranslate = dc.translate;
    float3 pScale = UnpackScale3(dc.scaleXY, dc.scaleZ);
    float3 pVelocity = dc.velocity;
    float4 pColor = UnpackColorRGBA8(dc.color);

    // 距離カリング/フェード（overdraw 対策）: 遠い粒子はアルファを 1→0 にフェードし、
    // 完全カリング距離を超えたら縮退頂点で破棄する。半透明粒子の重なり(ROP/blend律速)を
    // 減らす狙い。enableDistanceCull=0 のときは従来どおり全粒子を描画（既存挙動不変）。
    // ※ ここで早期 return することで、カリングされた粒子はビルボード/回転計算もスキップできる。
    if (gPerView.enableDistanceCull != 0)
    {
        float camDist = distance(pTranslate, gPerView.cameraPosition);
        if (camDist >= gPerView.distanceCullEnd)
        {
            output.position = float4(0.0f, 0.0f, 0.0f, 0.0f); // クリップされる縮退頂点
            output.texcoord = float2(0.0f, 0.0f);
            output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
            return output;
        }
        float fadeRange = max(gPerView.distanceCullEnd - gPerView.distanceCullStart, 0.0001f);
        float fade = saturate((gPerView.distanceCullEnd - camDist) / fadeRange);
        pColor.a *= fade;
    }

    // 画面サイズ上限 + 微小カリング（overdraw 対策）: pScale を使う worldMatrix 構築の前に行う。
    //   ・微小カリング: 画面上の高さが minScreenHeight 未満（サブピクセル相当）なら縮退カリング。
    //   ・サイズ上限: 画面高さが maxScreenHeight を超える巨大粒子は world スケールを一律縮小し、
    //                 1粒子が画面を埋め尽くす fillrate 暴発を抑える。
    //   enableSizeClamp=0 のときは何もしない（既存挙動不変）。
    if (gPerView.enableSizeClamp != 0)
    {
        // 粒子中心の clip.w（≒ビュー空間の奥行き）。perspective では 画面NDC高さ = worldHalf*projScaleY/w。
        float centerW = max(mul(float4(pTranslate, 1.0f), gPerView.viewProjection).w, 0.0001f);
        float worldToScreen = gPerView.projScaleY / centerW; // worldHalf → 画面NDC高さ の倍率
        float curHalf = max(max(pScale.x, pScale.y), pScale.z); // ビルボードは ±scale なので scale=半径相当
        float screenH = curHalf * worldToScreen;                // 画面上のおおよその高さ(NDC)

        // 微小カリング（サブピクセル）
        if (gPerView.minScreenHeight > 0.0f && screenH < gPerView.minScreenHeight)
        {
            output.position = float4(0.0f, 0.0f, 0.0f, 0.0f);
            output.texcoord = float2(0.0f, 0.0f);
            output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
            return output;
        }
        // 画面サイズ上限（縦横同率で縮小）
        float maxHalf = gPerView.maxScreenHeight * 0.5f; // 高さ→半径
        if (worldToScreen > 0.0f && screenH > maxHalf && curHalf > 0.0001f)
        {
            float cappedHalf = maxHalf / worldToScreen;
            pScale *= (cappedHalf / curHalf);
        }
    }
    // 回転は回転グループのみ gather（aliveList経由で実slotを引く。非回転グループは触れない）
    float3 pRotation = float3(0.0f, 0.0f, 0.0f);
    if (gPerView.enableRotation != 0)
        pRotation = gRotation[gAliveList[instanceId]].rotation;

    // --- スケール → XYZ回転 → ビルボード → 平行移動 ---
    float4x4 worldMatrix;

    if (gPerView.enableVelocityStretch != 0)
    {
        // 速度方向に引き伸ばすストレッチビルボード
        float3 vel = pVelocity;
        float3 camRight = normalize(gPerView.billboardMatrix[0].xyz);
        float3 camUp    = normalize(gPerView.billboardMatrix[1].xyz);
        float vRight = dot(vel, camRight);
        float vUp    = dot(vel, camUp);
        float2 screenVel = float2(vRight, vUp);
        float speed = length(screenVel);

        if (speed > 0.001f)
        {
            float2 stretchDir = screenVel / speed;
            float3 newUp    = normalize(stretchDir.x * camRight + stretchDir.y * camUp);
            float3 camBack  = normalize(gPerView.billboardMatrix[2].xyz);
            float3 newRight = normalize(cross(newUp, camBack));
            float stretchScale = 1.0f + speed * gPerView.velocityStretchFactor;

            worldMatrix[0] = float4(newRight * pScale.x, 0.0f);
            worldMatrix[1] = float4(newUp * (pScale.y * stretchScale), 0.0f);
            worldMatrix[2] = float4(camBack * pScale.z, 0.0f);
            worldMatrix[3] = float4(pTranslate, 1.0f);
        }
        else
        {
            // 速度ゼロ時は通常ビルボード
            worldMatrix = gPerView.billboardMatrix;
            worldMatrix[0] *= pScale.x;
            worldMatrix[1] *= pScale.y;
            worldMatrix[2] *= pScale.z;
            worldMatrix[3].xyz = pTranslate;
        }
    }
    else if (gPerView.enableBillboard != 0)
    {
        // 通常ビルボード（常にカメラを向く）
        worldMatrix = gPerView.billboardMatrix;
        worldMatrix[0] *= pScale.x;
        worldMatrix[1] *= pScale.y;
        worldMatrix[2] *= pScale.z;
        worldMatrix[3].xyz = pTranslate;
    }
    else
    {
        // 非ビルボード: ワールド軸に固定（カメラ追従しない）。向きは回転(rotXYZ)で付与する。
        worldMatrix[0] = float4(pScale.x, 0.0f, 0.0f, 0.0f);
        worldMatrix[1] = float4(0.0f, pScale.y, 0.0f, 0.0f);
        worldMatrix[2] = float4(0.0f, 0.0f, pScale.z, 0.0f);
        worldMatrix[3] = float4(pTranslate, 1.0f);
    }

    // ワールド×ビュープロジェクション。回転を使うグループのみ回転行列を合成する
    // （回転なしグループでは sincos×3＋行列積をスキップ＝全頂点ぶんの無駄を削減）。
    float4x4 wvp = mul(worldMatrix, gPerView.viewProjection);
    if (gPerView.enableRotation != 0)
    {
        float sx, cx, sy, cy, sz, cz;
        sincos(pRotation.x, sx, cx);
        sincos(pRotation.y, sy, cy);
        sincos(pRotation.z, sz, cz);
        float4x4 rotXYZ = float4x4(
            cy * cz, cy * sz, -sy, 0,
            sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy, 0,
            cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy, 0,
            0, 0, 0, 1
        );
        wvp = mul(rotXYZ, wvp);
    }
    output.position = mul(input.position, wvp);
    output.texcoord = input.texcoord;
    output.color = pColor;
    return output;
}