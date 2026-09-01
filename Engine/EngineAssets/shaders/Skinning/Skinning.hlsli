struct Vertex
{
    float4 position;
    float2 texcoord;
    float3 normal;
};

struct VertexInfluence
{
    float4 weight;
    int4 index;
};

struct SkinningInformation
{
    uint numVertices;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 normal        : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 shadowCoord   : POSITION1;
    // ピクセルシェーダ（Object3d.PS / GBuffer.PS）が要求するのでスキニングでも出力する。
    // スキニングはインスタンシングしないので常に白。Object3d.hlsli と並びを揃えること。
    float4 instanceColor : COLOR0;
};