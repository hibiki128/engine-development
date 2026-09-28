#include "BlendSpace.h"
#include "Animator.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <cmath>
#include <MyMath.h>
#include <numbers>

namespace Hagine {

namespace {
constexpr float kCenterEpsilon = 1.0e-4f; // これより原点に近い点は「中心」とみなす
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
} // namespace

Vector2 MakeLocomotionParameter(const Vector3 &worldVelocity, const Quaternion &rotation, float referenceSpeed)
{
    if (referenceSpeed <= 0.0f)
    {
        return {0.0f, 0.0f};
    }
    // 顔の向き（水平面）と、その右
    Vector3 forward = TransformNormal(kWorldForward, QuaternionToMatrix4x4(rotation));
    forward.y = 0.0f;
    if (forward.Length() < 1.0e-5f)
    {
        return {0.0f, 0.0f};
    }
    forward = forward.Normalize();
    const Vector3 right = kWorldUp.Cross(forward).Normalize();

    const Vector3 horizontal = {worldVelocity.x, 0.0f, worldVelocity.z};
    Vector2 parameter = {horizontal.Dot(right) / referenceSpeed, horizontal.Dot(forward) / referenceSpeed};
    const float length = parameter.Length();
    if (length > 1.0f)
    {
        parameter = parameter / length;
    }
    return parameter;
}

void AnimationBlendSpace::SetPoints(const std::vector<BlendSpacePoint> &points, const std::vector<std::string> &filePaths)
{
    points_ = points;
    sources_.clear();
    sources_.resize(points_.size());
    for (size_t i = 0; i < points_.size() && i < filePaths.size(); ++i)
    {
        sources_[i].filePath = filePaths[i];
        if (filePaths[i].empty())
        {
            continue;
        }
        // Animator はファイルごとに読み込み結果をキャッシュしているので、同じクリップを
        // 通常再生でも使っていれば読み直しは起きない
        Animator loader;
        loader.Initialize(AssetPath::ModelsRoot(filePaths[i]), filePaths[i]);
        sources_[i].animation = loader.GetAnimation();
    }
    weights_.assign(points_.size(), 0.0f);
    ComputeWeights(parameter_, weights_);
}

float AnimationBlendSpace::GetCycleLength(size_t index) const
{
    if (index >= sources_.size())
    {
        return 0.0f;
    }
    const float duration = sources_[index].animation.duration;
    const float speed = (index < points_.size() && points_[index].speed > 0.0f) ? points_[index].speed : 1.0f;
    return duration / speed;
}

void AnimationBlendSpace::Advance(float deltaTime)
{
    // パラメータは目標へ指数的に寄せる（入力のガタつきで重みが跳ねないように）
    const float k = (smoothTime_ > 0.0f) ? 1.0f - std::exp(-deltaTime / smoothTime_) : 1.0f;
    parameter_ += (targetParameter_ - parameter_) * k;

    ComputeWeights(parameter_, weights_);

    // 周期は重みで混ぜる。走り（短い周期）と待機（長い周期）の間では、
    // 混ざり具合に応じて1周の長さもなめらかに変わる
    float cycle = 0.0f;
    for (size_t i = 0; i < weights_.size(); ++i)
    {
        cycle += weights_[i] * GetCycleLength(i);
    }
    if (cycle > 0.0f)
    {
        phase_ += deltaTime * playbackSpeed_ / cycle;
        phase_ -= std::floor(phase_);
    }
}

void AnimationBlendSpace::GetSampleTimes(std::vector<float> &outTimes) const
{
    outTimes.resize(sources_.size());
    for (size_t i = 0; i < sources_.size(); ++i)
    {
        outTimes[i] = phase_ * sources_[i].animation.duration;
    }
}

void AnimationBlendSpace::ComputeWeights(const Vector2 &parameter, std::vector<float> &outWeights) const
{
    outWeights.assign(points_.size(), 0.0f);
    if (points_.empty())
    {
        return;
    }
    if (points_.size() == 1)
    {
        outWeights[0] = 1.0f;
        return;
    }

    switch (mode_)
    {
    case BlendSpaceMode::Directional2D:
        ComputeDirectionalWeights(parameter, outWeights);
        break;
    case BlendSpaceMode::Cartesian2D:
        ComputeCartesianWeights(parameter, outWeights);
        break;
    case BlendSpaceMode::Linear1D:
        ComputeLinearWeights(parameter.x, outWeights);
        break;
    }
}

void AnimationBlendSpace::ComputeDirectionalWeights(const Vector2 &parameter, std::vector<float> &outWeights) const
{
    // 中心の点（待機など）と、周りの点（前後左右など）に分ける
    std::vector<size_t> centers;
    struct Ring
    {
        size_t index;
        float angle;  // 前(+Y)を0として右回りに正
        float radius; // 中心からの距離
    };
    std::vector<Ring> ring;
    for (size_t i = 0; i < points_.size(); ++i)
    {
        const Vector2 &p = points_[i].position;
        const float r = p.Length();
        if (r < kCenterEpsilon)
        {
            centers.push_back(i);
        }
        else
        {
            ring.push_back({i, std::atan2(p.x, p.y), r});
        }
    }

    if (ring.empty())
    {
        for (size_t c : centers)
        {
            outWeights[c] = 1.0f / static_cast<float>(centers.size());
        }
        return;
    }

    // ---- 向き: 隣り合う2点を角度で混ぜる ----
    std::sort(ring.begin(), ring.end(), [](const Ring &a, const Ring &b) { return a.angle < b.angle; });
    const float magnitude = parameter.Length();
    const float angle = std::atan2(parameter.x, parameter.y);

    size_t first = 0;
    size_t second = 0;
    float fraction = 0.0f;
    if (ring.size() > 1)
    {
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const size_t next = (i + 1) % ring.size();
            const float a = ring[i].angle;
            float b = ring[next].angle;
            if (b <= a)
            {
                b += kTwoPi; // 最後の点から最初の点へは一周回り込む
            }
            float t = angle;
            while (t < a)
            {
                t += kTwoPi;
            }
            if (t <= b)
            {
                first = i;
                second = next;
                fraction = (b - a > 0.0f) ? (t - a) / (b - a) : 0.0f;
                break;
            }
        }
    }

    // ---- 長さ: 周りの点までの距離に対する割合で中心と混ぜる ----
    const float radius = ring[first].radius + (ring[second].radius - ring[first].radius) * fraction;
    float outer = 1.0f;
    if (!centers.empty())
    {
        outer = (radius > 0.0f) ? std::clamp(magnitude / radius, 0.0f, 1.0f) : 1.0f;
        const float centerWeight = (1.0f - outer) / static_cast<float>(centers.size());
        for (size_t c : centers)
        {
            outWeights[c] = centerWeight;
        }
    }
    outWeights[ring[first].index] += outer * (1.0f - fraction);
    outWeights[ring[second].index] += outer * fraction;
}

void AnimationBlendSpace::ComputeCartesianWeights(const Vector2 &parameter, std::vector<float> &outWeights) const
{
    // グラデーションバンド補間: 各点について「他のどの点へ向かっても、どれだけ手前にいるか」の最小値
    float total = 0.0f;
    for (size_t i = 0; i < points_.size(); ++i)
    {
        const Vector2 &pi = points_[i].position;
        const Vector2 toParam = parameter - pi;
        float w = 1.0f;
        for (size_t j = 0; j < points_.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            const Vector2 edge = points_[j].position - pi;
            const float lengthSq = edge.x * edge.x + edge.y * edge.y;
            if (lengthSq <= 0.0f)
            {
                continue;
            }
            const float t = 1.0f - (toParam.x * edge.x + toParam.y * edge.y) / lengthSq;
            w = (std::min)(w, t);
        }
        w = (std::max)(w, 0.0f);
        outWeights[i] = w;
        total += w;
    }
    if (total > 0.0f)
    {
        for (float &w : outWeights)
        {
            w /= total;
        }
    }
    else
    {
        // 全点の外側: 一番近い点だけにする
        size_t nearest = 0;
        float best = 1.0e30f;
        for (size_t i = 0; i < points_.size(); ++i)
        {
            const float d = (parameter - points_[i].position).Length();
            if (d < best)
            {
                best = d;
                nearest = i;
            }
        }
        outWeights[nearest] = 1.0f;
    }
}

void AnimationBlendSpace::ComputeLinearWeights(float x, std::vector<float> &outWeights) const
{
    std::vector<size_t> order(points_.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [this](size_t a, size_t b) { return points_[a].position.x < points_[b].position.x; });

    // 端より外は端の点だけ
    if (x <= points_[order.front()].position.x)
    {
        outWeights[order.front()] = 1.0f;
        return;
    }
    if (x >= points_[order.back()].position.x)
    {
        outWeights[order.back()] = 1.0f;
        return;
    }
    for (size_t k = 0; k + 1 < order.size(); ++k)
    {
        const float a = points_[order[k]].position.x;
        const float b = points_[order[k + 1]].position.x;
        if (x >= a && x <= b)
        {
            const float t = (b - a > 0.0f) ? (x - a) / (b - a) : 0.0f;
            outWeights[order[k]] = 1.0f - t;
            outWeights[order[k + 1]] = t;
            return;
        }
    }
}

} // namespace Hagine
