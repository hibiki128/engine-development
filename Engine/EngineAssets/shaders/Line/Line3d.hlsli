struct VSInput
{
    float3 pos : POSITION; // 座標
    float4 color : COLOR0; // 色（RGBA8をUNORMで受け取る）
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};
