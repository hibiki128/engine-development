#include "PrimitiveModel.h"
#include <DirectXMath.h>
#include <MyMath.h>
namespace Hagine {
using namespace DirectX;

void PrimitiveModel::Initialize()
{
    CreateSphere();
    CreatePlane();
    CreateCube();
    CreateCylinder();
    CreateRing();
    CreateTriangle();
    CreateCone();
    CreatePyramid();
    CreateClosedCylinder();
}

void PrimitiveModel::Finalize()
{
    primitiveDataMap_.clear();
}

bool IsParametricPrimitive(PrimitiveType type)
{
    return type == PrimitiveType::Ring ||
           type == PrimitiveType::Sphere ||
           type == PrimitiveType::Cylinder ||
           type == PrimitiveType::Cone;
}

PrimitiveModel::PrimitiveData PrimitiveModel::BuildParametricData(const PrimitiveType &type, const PrimitiveParams &params)
{
    // 分割数は最低3（潰れ防止）。Ring の内半径は外半径以下にクランプする。
    const uint32_t divide = params.divide < 3u ? 3u : params.divide;
    switch (type)
    {
    case PrimitiveType::Sphere:
        return BuildSphere(divide);
    case PrimitiveType::Cylinder:
        return BuildCylinder(divide, params.heightDivide);
    case PrimitiveType::Cone:
        return BuildCone(divide);
    case PrimitiveType::Ring: {
        float outer = params.ringOuterRadius;
        float inner = params.ringInnerRadius;
        if (inner > outer)
            inner = outer;
        return BuildRing(divide, outer, inner);
    }
    default:
        // パラメータ非対応の形状は既定の固定データを返す
        return GetPrimitiveData(type);
    }
}

void PrimitiveModel::CreateSphere()
{
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Sphere, BuildSphere(32)));
}

PrimitiveModel::PrimitiveData PrimitiveModel::BuildSphere(uint32_t divide)
{
    // Sphereの頂点データ
    PrimitiveData primitiveData{};
    // 球分割数
    const uint32_t kSubdivision = divide;
    const float kLonEvery = std::numbers::pi_v<float> * 2.0f / static_cast<float>(kSubdivision);
    const float kLatEvery = std::numbers::pi_v<float> / static_cast<float>(kSubdivision);

    // 頂点の生成
    for (uint32_t latIndex = 0; latIndex <= kSubdivision; ++latIndex)
    {
        float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;
        float sinLat = std::sinf(lat);
        float cosLat = std::cosf(lat);

        for (uint32_t lonIndex = 0; lonIndex <= kSubdivision; ++lonIndex)
        {
            float lon = kLonEvery * lonIndex;
            float sinLon = std::sinf(lon);
            float cosLon = std::cosf(lon);

            VertexData vertex{};
            vertex.position = {cosLat * cosLon, sinLat, cosLat * sinLon, 1.0f};
            vertex.normal = {cosLat * cosLon, sinLat, cosLat * sinLon};
            vertex.texcoord = {static_cast<float>(lonIndex) / static_cast<float>(kSubdivision),
                               1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision)};
            primitiveData.vertices.push_back(vertex);
        }
    }

    // インデックスの生成
    for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex)
    {
        for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex)
        {
            uint32_t first = latIndex * (kSubdivision + 1) + lonIndex;
            uint32_t second = first + kSubdivision + 1;

            // 三角形1
            primitiveData.indices.push_back(first);
            primitiveData.indices.push_back(second);
            primitiveData.indices.push_back(first + 1);

            // 三角形2
            primitiveData.indices.push_back(second);
            primitiveData.indices.push_back(second + 1);
            primitiveData.indices.push_back(first + 1);
        }
    }

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    return primitiveData;
}

void PrimitiveModel::CreatePlane()
{
    // Planeの頂点データ（垂直配置）
    PrimitiveData primitiveData{};

    // 頂点の生成（X軸周りに90度回転 - 垂直配置）
    primitiveData.vertices = {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}, // 左下
        {{1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},  // 右下
        {{-1.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // 左上
        {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}    // 右上
    };

    // インデックスの生成
    primitiveData.indices = {
        0, 2, 1, // 三角形1
        1, 2, 3  // 三角形2
    };

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Plane, primitiveData));
}

void PrimitiveModel::CreateCube()
{
    // Cubeの頂点データ
    PrimitiveData primitiveData{};

    // 頂点の生成（8頂点）
    VertexData vertices[8] = {
        // 前面（z = -1）
        {{-1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 左下
        {{1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},  // 右下
        {{-1.0f, 1.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},  // 左上
        {{1.0f, 1.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},   // 右上
        // 背面（z = 1）
        {{-1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}, // 左下
        {{1.0f, -1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},  // 右下
        {{-1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // 左上
        {{1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}    // 右上
    };

    // 各面の頂点を追加
    // 前面（z = -1）
    primitiveData.vertices.push_back(vertices[0]); // 0: 左下
    primitiveData.vertices.push_back(vertices[1]); // 1: 右下
    primitiveData.vertices.push_back(vertices[2]); // 2: 左上
    primitiveData.vertices.push_back(vertices[3]); // 3: 右上

    // 背面（z = 1）
    primitiveData.vertices.push_back(vertices[5]); // 4: 右下
    primitiveData.vertices.push_back(vertices[4]); // 5: 左下
    primitiveData.vertices.push_back(vertices[7]); // 6: 右上
    primitiveData.vertices.push_back(vertices[6]); // 7: 左上

    // 右面（x = 1）
    VertexData rightFace[4] = {
        {{1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}}, // 8: 左下
        {{1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},  // 9: 右下
        {{1.0f, 1.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // 10: 左上
        {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}    // 11: 右上
    };
    for (int i = 0; i < 4; i++)
    {
        primitiveData.vertices.push_back(rightFace[i]);
    }

    // 左面（x = -1）
    VertexData leftFace[4] = {
        {{-1.0f, -1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},  // 12: 左下
        {{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}}, // 13: 右下
        {{-1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},   // 14: 左上
        {{-1.0f, 1.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}}   // 15: 右上
    };
    for (int i = 0; i < 4; i++)
    {
        primitiveData.vertices.push_back(leftFace[i]);
    }

    // 上面（y = 1）
    VertexData topFace[4] = {
        {{-1.0f, 1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}, // 16: 左下
        {{1.0f, 1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},  // 17: 右下
        {{-1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // 18: 左上
        {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}    // 19: 右上
    };
    for (int i = 0; i < 4; i++)
    {
        primitiveData.vertices.push_back(topFace[i]);
    }

    // 下面（y = -1）
    VertexData bottomFace[4] = {
        {{-1.0f, -1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},  // 20: 左下
        {{1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},   // 21: 右下
        {{-1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, // 22: 左上
        {{1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}   // 23: 右上
    };
    for (int i = 0; i < 4; i++)
    {
        primitiveData.vertices.push_back(bottomFace[i]);
    }

    // 各面のインデックスを設定（2つの三角形で1つの四角形）
    for (int face = 0; face < 6; face++)
    {
        uint32_t baseIndex = face * 4;
        // 三角形1
        primitiveData.indices.push_back(baseIndex + 0);
        primitiveData.indices.push_back(baseIndex + 2);
        primitiveData.indices.push_back(baseIndex + 1);
        // 三角形2
        primitiveData.indices.push_back(baseIndex + 1);
        primitiveData.indices.push_back(baseIndex + 2);
        primitiveData.indices.push_back(baseIndex + 3);
    }

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Cube, primitiveData));
}

void PrimitiveModel::CreateCylinder()
{
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Cylinder, BuildCylinder(32, 1)));
}

PrimitiveModel::PrimitiveData PrimitiveModel::BuildCylinder(uint32_t divide, uint32_t heightDivide)
{
    // Cylinderの頂点データ（側面を円周・高さ方向に格子状へ分割）
    PrimitiveData primitiveData{};

    const uint32_t kRingDivide = divide < 3u ? 3u : divide;
    const uint32_t kHeightDivide = heightDivide < 1u ? 1u : heightDivide;
    const float kRadius = 1.0f;
    const float kHeight = 2.0f;
    const float halfHeight = kHeight / 2.0f;
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(kRingDivide);

    // 頂点の生成（下→上へ heightDivide+1 段のリング）。
    // レイアウト: index = row * (kRingDivide + 1) + col
    for (uint32_t row = 0; row <= kHeightDivide; ++row)
    {
        float t = float(row) / float(kHeightDivide); // 0=下, 1=上
        float y = -halfHeight + kHeight * t;
        float v = 1.0f - t; // 下(row=0)=1, 上=0（従来と同じ）

        for (uint32_t col = 0; col <= kRingDivide; ++col)
        {
            float angle = col * radianPerDivide;
            float sinTheta = std::sinf(angle);
            float cosTheta = std::cosf(angle);
            float u = float(col) / float(kRingDivide);

            VertexData vertex{};
            vertex.position = {kRadius * cosTheta, y, kRadius * sinTheta, 1.0f};
            vertex.normal = {cosTheta, 0.0f, sinTheta};
            vertex.texcoord = {u, v};
            primitiveData.vertices.push_back(vertex);
        }
    }

    // インデックスの生成（各セルを2三角形で埋める）
    const uint32_t stride = kRingDivide + 1;
    for (uint32_t row = 0; row < kHeightDivide; ++row)
    {
        for (uint32_t col = 0; col < kRingDivide; ++col)
        {
            uint32_t bottomLeft = row * stride + col;
            uint32_t bottomRight = bottomLeft + 1;
            uint32_t topLeft = bottomLeft + stride;
            uint32_t topRight = topLeft + 1;

            // 三角形1
            primitiveData.indices.push_back(bottomLeft);
            primitiveData.indices.push_back(bottomRight);
            primitiveData.indices.push_back(topLeft);

            // 三角形2
            primitiveData.indices.push_back(topLeft);
            primitiveData.indices.push_back(bottomRight);
            primitiveData.indices.push_back(topRight);
        }
    }

    // その他属性
    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    return primitiveData;
}

std::vector<std::pair<Vector3, Vector3>> PrimitiveModel::BuildCylinderGridLines(uint32_t divide, uint32_t heightDivide)
{
    std::vector<std::pair<Vector3, Vector3>> lines;

    const uint32_t kRingDivide = divide < 3u ? 3u : divide;
    const uint32_t kHeightDivide = heightDivide < 1u ? 1u : heightDivide;
    const float kRadius = 1.0f;
    const float kHeight = 2.0f;
    const float halfHeight = kHeight / 2.0f;
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(kRingDivide);

    // BuildCylinder の頂点と同じ座標を返すためのヘルパー
    auto pos = [&](uint32_t col, uint32_t row) -> Vector3 {
        float angle = col * radianPerDivide;
        float y = -halfHeight + kHeight * (float(row) / float(kHeightDivide));
        return Vector3(kRadius * std::cosf(angle), y, kRadius * std::sinf(angle));
    };

    lines.reserve(kRingDivide * kHeightDivide + (kHeightDivide + 1) * kRingDivide);

    // 縦線（各列を下から上へ）。col=kRingDivide は col=0 と重なるので 0..kRingDivide-1 のみ。
    for (uint32_t col = 0; col < kRingDivide; ++col)
    {
        for (uint32_t row = 0; row < kHeightDivide; ++row)
        {
            lines.emplace_back(pos(col, row), pos(col, row + 1));
        }
    }

    // 横リング（各段の円周を col→col+1 で1周）
    for (uint32_t row = 0; row <= kHeightDivide; ++row)
    {
        for (uint32_t col = 0; col < kRingDivide; ++col)
        {
            lines.emplace_back(pos(col, row), pos(col + 1, row));
        }
    }

    return lines;
}

void PrimitiveModel::CreateRing()
{
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Ring, BuildRing(32, 1.0f, 0.5f)));
}

PrimitiveModel::PrimitiveData PrimitiveModel::BuildRing(uint32_t divide, float outerRadius, float innerRadius)
{
    // Ringの頂点データ（垂直配置）
    PrimitiveData primitiveData{};

    const uint32_t kRingDivide = divide;
    const float kOuterRadius = outerRadius;
    const float kInnerRadius = innerRadius;
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(kRingDivide);

    // 頂点の生成（垂直にするためにX軸90度回転）
    for (uint32_t i = 0; i <= kRingDivide; ++i)
    {
        float angle = i * radianPerDivide;
        float sinTheta = std::sinf(angle);
        float cosTheta = std::cosf(angle);
        float u = float(i) / float(kRingDivide);

        // 外側の頂点（垂直配置）
        VertexData outerVertex{};
        outerVertex.position = {kOuterRadius * cosTheta, kOuterRadius * sinTheta, 0.0f, 1.0f};
        outerVertex.normal = {0.0f, 0.0f, 1.0f}; // 正面向きの法線
        outerVertex.texcoord = {u, 0.0f};
        primitiveData.vertices.push_back(outerVertex);

        // 内側の頂点（垂直配置）
        VertexData innerVertex{};
        innerVertex.position = {kInnerRadius * cosTheta, kInnerRadius * sinTheta, 0.0f, 1.0f};
        innerVertex.normal = {0.0f, 0.0f, 1.0f}; // 正面向きの法線
        innerVertex.texcoord = {u, 1.0f};
        primitiveData.vertices.push_back(innerVertex);
    }

    // インデックスの生成
    for (uint32_t i = 0; i < kRingDivide; ++i)
    {
        uint32_t outerCurrent = i * 2;
        uint32_t innerCurrent = outerCurrent + 1;
        uint32_t outerNext = outerCurrent + 2;
        uint32_t innerNext = outerCurrent + 3;

        // 三角形1（外側の頂点から内側へ）
        primitiveData.indices.push_back(outerCurrent);
        primitiveData.indices.push_back(innerCurrent);
        primitiveData.indices.push_back(outerNext);

        // 三角形2（内側の頂点から外側へ）
        primitiveData.indices.push_back(innerCurrent);
        primitiveData.indices.push_back(innerNext);
        primitiveData.indices.push_back(outerNext);
    }

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    return primitiveData;
}

void PrimitiveModel::CreateTriangle()
{
    // 三角形の頂点データ（XY平面上で垂直配置）
    PrimitiveData primitiveData{};

    // 原点を中心とした正三角形を作成
    // 正三角形の高さ = sqrt(3)/2 * 辺の長さ
    const float side = 2.0f; // 辺の長さ2単位
    const float height = std::sqrt(3.0f) * side / 2.0f;
    const float halfSide = side / 2.0f;
    const float offsetY = height / 3.0f; // 三角形を原点中心にするためのオフセット

    // 頂点の生成（XY平面上、Z+方向を正面に）
    primitiveData.vertices = {
        // Position (x,y,z,w), UV, Normal
        {{0.0f, height - offsetY, 0.0f, 1.0f}, {0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // 上部
        {{-halfSide, -offsetY, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},    // 左下
        {{halfSide, -offsetY, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}      // 右下
    };

    // インデックスの生成（一つの三角形のみ）
    primitiveData.indices = {0, 1, 2};

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    // プリミティブマップに追加
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Triangle, primitiveData));
}

void PrimitiveModel::CreateCone()
{
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Cone, BuildCone(32)));
}

PrimitiveModel::PrimitiveData PrimitiveModel::BuildCone(uint32_t divide)
{
    // 円錐の頂点データ
    PrimitiveData primitiveData{};

    const uint32_t kBaseDivide = divide;
    const float kRadius = 1.0f;
    const float kHeight = 2.0f;
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(kBaseDivide);

    // 頂点（円錐の先端）を作成
    VertexData apex{};
    apex.position = {0.0f, kHeight / 2.0f, 0.0f, 1.0f};
    apex.normal = {0.0f, 1.0f, 0.0f};
    apex.texcoord = {0.5f, 0.0f};
    primitiveData.vertices.push_back(apex);

    // 底面の頂点を作成
    for (uint32_t i = 0; i <= kBaseDivide; ++i)
    {
        float angle = i * radianPerDivide;
        float sinTheta = std::sinf(angle);
        float cosTheta = std::cosf(angle);
        float u = float(i) / float(kBaseDivide);

        // 側面の法線計算（隣接面の平均）
        // 円錐の場合、法線は他のプリミティブほど単純ではない
        float nx = cosTheta;
        float ny = 0.2f; // Y成分を少し入れて角度をつける
        float nz = sinTheta;
        float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= length;
        ny /= length;
        nz /= length;

        VertexData baseVertex{};
        baseVertex.position = {kRadius * cosTheta, -kHeight / 2.0f, kRadius * sinTheta, 1.0f};
        baseVertex.normal = {nx, ny, nz};
        baseVertex.texcoord = {u, 1.0f};
        primitiveData.vertices.push_back(baseVertex);
    }

    // 側面の三角形を作成
    for (uint32_t i = 0; i < kBaseDivide; ++i)
    {
        uint32_t current = i + 1;
        uint32_t next = i + 2;

        // 三角形（先端と底面の2頂点で構成）
        primitiveData.indices.push_back(0); // 先端
        primitiveData.indices.push_back(current);
        primitiveData.indices.push_back(next);
    }

    // 底面の中心頂点を追加
    VertexData baseCenter{};
    baseCenter.position = {0.0f, -kHeight / 2.0f, 0.0f, 1.0f};
    baseCenter.normal = {0.0f, -1.0f, 0.0f};
    baseCenter.texcoord = {0.5f, 0.5f};
    uint32_t baseCenterIndex = static_cast<uint32_t>(primitiveData.vertices.size());
    primitiveData.vertices.push_back(baseCenter);

    // 底面の三角形を作成
    for (uint32_t i = 0; i < kBaseDivide; ++i)
    {
        uint32_t current = i + 1;
        uint32_t next = i + 2;

        // 底面の三角形
        primitiveData.indices.push_back(baseCenterIndex);
        primitiveData.indices.push_back(next);
        primitiveData.indices.push_back(current);
    }

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    return primitiveData;
}

void PrimitiveModel::CreatePyramid()
{
    // 四角錐（ピラミッド）の頂点データ
    PrimitiveData primitiveData{};

    const float kSide = 2.0f;
    const float kHeight = 2.0f;
    const float halfSide = kSide / 2.0f;

    // 頂点を作成
    // 頂点
    VertexData apex{};
    apex.position = {0.0f, kHeight / 2.0f, 0.0f, 1.0f};
    apex.texcoord = {0.5f, 0.0f};

    // 底面の四隅
    VertexData frontLeft{};
    frontLeft.position = {-halfSide, -kHeight / 2.0f, halfSide, 1.0f};
    frontLeft.texcoord = {0.0f, 1.0f};

    VertexData frontRight{};
    frontRight.position = {halfSide, -kHeight / 2.0f, halfSide, 1.0f};
    frontRight.texcoord = {1.0f, 1.0f};

    VertexData backLeft{};
    backLeft.position = {-halfSide, -kHeight / 2.0f, -halfSide, 1.0f};
    backLeft.texcoord = {0.0f, 0.0f};

    VertexData backRight{};
    backRight.position = {halfSide, -kHeight / 2.0f, -halfSide, 1.0f};
    backRight.texcoord = {1.0f, 0.0f};

    // 前面
    VertexData apexFront = apex;
    apexFront.normal = Vector3(0.0f, 0.5f, 1.0f).Normalize();
    primitiveData.vertices.push_back(apexFront);

    VertexData frontLeftCopy = frontLeft;
    frontLeftCopy.normal = Vector3(0.0f, 0.5f, 1.0f).Normalize();
    primitiveData.vertices.push_back(frontLeftCopy);

    VertexData frontRightCopy = frontRight;
    frontRightCopy.normal = Vector3(0.0f, 0.5f, 1.0f).Normalize();
    primitiveData.vertices.push_back(frontRightCopy);

    // 右面
    VertexData apexRight = apex;
    apexRight.normal = Vector3(1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(apexRight);

    VertexData frontRightCopy2 = frontRight;
    frontRightCopy2.normal = Vector3(1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(frontRightCopy2);

    VertexData backRightCopy = backRight;
    backRightCopy.normal = Vector3(1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(backRightCopy);

    // 背面
    VertexData apexBack = apex;
    apexBack.normal = Vector3(0.0f, 0.5f, -1.0f).Normalize();
    primitiveData.vertices.push_back(apexBack);

    VertexData backRightCopy2 = backRight;
    backRightCopy2.normal = Vector3(0.0f, 0.5f, -1.0f).Normalize();
    primitiveData.vertices.push_back(backRightCopy2);

    VertexData backLeftCopy = backLeft;
    backLeftCopy.normal = Vector3(0.0f, 0.5f, -1.0f).Normalize();
    primitiveData.vertices.push_back(backLeftCopy);

    // 左面
    VertexData apexLeft = apex;
    apexLeft.normal = Vector3(-1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(apexLeft);

    VertexData backLeftCopy2 = backLeft;
    backLeftCopy2.normal = Vector3(-1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(backLeftCopy2);

    VertexData frontLeftCopy2 = frontLeft;
    frontLeftCopy2.normal = Vector3(-1.0f, 0.5f, 0.0f).Normalize();
    primitiveData.vertices.push_back(frontLeftCopy2);

    // 底面
    VertexData bottomFrontLeft = frontLeft;
    bottomFrontLeft.normal = {0.0f, -1.0f, 0.0f};
    primitiveData.vertices.push_back(bottomFrontLeft);

    VertexData bottomFrontRight = frontRight;
    bottomFrontRight.normal = {0.0f, -1.0f, 0.0f};
    primitiveData.vertices.push_back(bottomFrontRight);

    VertexData bottomBackLeft = backLeft;
    bottomBackLeft.normal = {0.0f, -1.0f, 0.0f};
    primitiveData.vertices.push_back(bottomBackLeft);

    VertexData bottomBackRight = backRight;
    bottomBackRight.normal = {0.0f, -1.0f, 0.0f};
    primitiveData.vertices.push_back(bottomBackRight);

    // インデックスの生成
    // 四つの側面（それぞれ三角形）
    for (uint32_t i = 0; i < 4; ++i)
    {
        uint32_t baseIndex = i * 3;
        primitiveData.indices.push_back(baseIndex);
        primitiveData.indices.push_back(baseIndex + 1);
        primitiveData.indices.push_back(baseIndex + 2);
    }

    // 底面（二つの三角形）
    primitiveData.indices.push_back(12); // 底面左前
    primitiveData.indices.push_back(13); // 底面右前
    primitiveData.indices.push_back(14); // 底面左後

    primitiveData.indices.push_back(13); // 底面右前
    primitiveData.indices.push_back(15); // 底面右後
    primitiveData.indices.push_back(14); // 底面左後

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    // PrimitiveTypeを追加する必要があります
    primitiveDataMap_.insert(std::make_pair(PrimitiveType::Pyramid, primitiveData));
}

void PrimitiveModel::CreateClosedCylinder()
{
    // ClosedCylinder（上下に蓋あり円柱）の頂点データ
    PrimitiveData primitiveData{};

    const uint32_t kRingDivide = 32;
    const float kRadius = 1.0f;
    const float kHeight = 2.0f;
    const float halfHeight = kHeight / 2.0f;
    const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(kRingDivide);

    // -------------------------------------------------------
    // 側面（CreateCylinder と同じ構造）
    // 頂点レイアウト: [i*2+0]=下リング, [i*2+1]=上リング
    // -------------------------------------------------------
    for (uint32_t i = 0; i <= kRingDivide; ++i)
    {
        float angle = i * radianPerDivide;
        float sinTheta = std::sinf(angle);
        float cosTheta = std::cosf(angle);
        float u = float(i) / float(kRingDivide);

        // 下リングの頂点
        VertexData bottomVertex{};
        bottomVertex.position = {kRadius * cosTheta, -halfHeight, kRadius * sinTheta, 1.0f};
        bottomVertex.normal = {cosTheta, 0.0f, sinTheta};
        bottomVertex.texcoord = {u, 1.0f};
        primitiveData.vertices.push_back(bottomVertex);

        // 上リングの頂点
        VertexData topVertex{};
        topVertex.position = {kRadius * cosTheta, halfHeight, kRadius * sinTheta, 1.0f};
        topVertex.normal = {cosTheta, 0.0f, sinTheta};
        topVertex.texcoord = {u, 0.0f};
        primitiveData.vertices.push_back(topVertex);
    }

    // 側面インデックス
    for (uint32_t i = 0; i < kRingDivide; ++i)
    {
        uint32_t bottomLeft = i * 2;
        uint32_t topLeft = bottomLeft + 1;
        uint32_t bottomRight = bottomLeft + 2;
        uint32_t topRight = bottomLeft + 3;

        // 三角形1
        primitiveData.indices.push_back(bottomLeft);
        primitiveData.indices.push_back(bottomRight);
        primitiveData.indices.push_back(topLeft);

        // 三角形2
        primitiveData.indices.push_back(topLeft);
        primitiveData.indices.push_back(bottomRight);
        primitiveData.indices.push_back(topRight);
    }

    // -------------------------------------------------------
    // 上蓋（法線: +Y、UV はディスク状に展開）
    // 中心頂点 1 個 + リング頂点 kRingDivide+1 個
    // -------------------------------------------------------
    uint32_t topCapCenterIndex = static_cast<uint32_t>(primitiveData.vertices.size());
    {
        VertexData center{};
        center.position = {0.0f, halfHeight, 0.0f, 1.0f};
        center.normal = {0.0f, 1.0f, 0.0f};
        center.texcoord = {0.5f, 0.5f};
        primitiveData.vertices.push_back(center);

        for (uint32_t i = 0; i <= kRingDivide; ++i)
        {
            float angle = i * radianPerDivide;
            float sinTheta = std::sinf(angle);
            float cosTheta = std::cosf(angle);

            VertexData v{};
            v.position = {kRadius * cosTheta, halfHeight, kRadius * sinTheta, 1.0f};
            v.normal = {0.0f, 1.0f, 0.0f};
            v.texcoord = {cosTheta * 0.5f + 0.5f, sinTheta * 0.5f + 0.5f};
            primitiveData.vertices.push_back(v);
        }
    }

    // 上蓋インデックス（時計回り＝表が上向き）
    for (uint32_t i = 0; i < kRingDivide; ++i)
    {
        uint32_t current = topCapCenterIndex + 1 + i;
        uint32_t next = topCapCenterIndex + 1 + i + 1;

        primitiveData.indices.push_back(topCapCenterIndex);
        primitiveData.indices.push_back(next);
        primitiveData.indices.push_back(current);
    }

    // -------------------------------------------------------
    // 下蓋（法線: -Y）
    // -------------------------------------------------------
    uint32_t bottomCapCenterIndex = static_cast<uint32_t>(primitiveData.vertices.size());
    {
        VertexData center{};
        center.position = {0.0f, -halfHeight, 0.0f, 1.0f};
        center.normal = {0.0f, -1.0f, 0.0f};
        center.texcoord = {0.5f, 0.5f};
        primitiveData.vertices.push_back(center);

        for (uint32_t i = 0; i <= kRingDivide; ++i)
        {
            float angle = i * radianPerDivide;
            float sinTheta = std::sinf(angle);
            float cosTheta = std::cosf(angle);

            VertexData v{};
            v.position = {kRadius * cosTheta, -halfHeight, kRadius * sinTheta, 1.0f};
            v.normal = {0.0f, -1.0f, 0.0f};
            v.texcoord = {cosTheta * 0.5f + 0.5f, sinTheta * 0.5f + 0.5f};
            primitiveData.vertices.push_back(v);
        }
    }

    // 下蓋インデックス（反時計回り＝表が下向き）
    for (uint32_t i = 0; i < kRingDivide; ++i)
    {
        uint32_t current = bottomCapCenterIndex + 1 + i;
        uint32_t next = bottomCapCenterIndex + 1 + i + 1;

        primitiveData.indices.push_back(bottomCapCenterIndex);
        primitiveData.indices.push_back(current);
        primitiveData.indices.push_back(next);
    }

    primitiveData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    primitiveData.uvMatrix = MakeIdentity4x4();

    primitiveDataMap_.insert(std::make_pair(PrimitiveType::ClosedCylinder, primitiveData));
}
} // namespace Hagine
