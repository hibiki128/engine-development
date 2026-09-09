#include "SceneTransition.h"
#include "Easing.h"
#include "frame/Frame.h"
#include "SpriteCommon.h"
#include "algorithm"
#include "MyMath.h"
#include <vector>

namespace Hagine {
namespace {

/// <summary>六角形のマスク画像（白で描いてあるので色はマテリアル側で付く）</summary>
constexpr const char *kCellTexturePath = "debug/hexagon.png";

/// <summary>正六角形の 幅 ÷ 高さ。尖った頂点が上下に来る向きなので sqrt(3)/2</summary>
constexpr float kHexWidthRatio = 0.86602540f;

/// <summary>
/// 画像の高さに対する六角形の高さの割合（黒フチが切れないよう少し内側に描いてある）。
/// 画像は正方形で、六角形の縦横比はその中に描き込んである。
/// なのでスプライトは縦横おなじ倍率で拡大すること（横をさらに縮めると正六角形でなくなる）
/// </summary>
constexpr float kTextureFillRatio = 250.0f / 256.0f;

/// <summary>
/// 継ぎ目を消すための余裕。ちょうど敷き詰める寸法だと辺が接するだけで
/// 隙間が見えることがあるので、少しだけ重ねる
/// </summary>
constexpr float kSeamMargin = 1.03f;

} // namespace

void SceneTransition::Finalize()
{
    colorSprites_.clear();
    cellsByColor_.clear();
    cells_.clear();
    sprite_.reset();
}

void SceneTransition::SetColors(const std::vector<Vector4> &colors)
{
    if (colors.empty())
    {
        return;
    }
    colors_ = colors;

    // 色ごとのスプライトを用意し直す。色数が変わることもあるので作り直す
    colorSprites_.clear();
    colorSprites_.reserve(colors_.size());
    for (const Vector4 &color : colors_)
    {
        auto sprite = std::make_unique<Sprite>();
        // 六角形のマスク画像。白で描いてあるので、マテリアル色がそのまま出る
        sprite->Initialize(kCellTexturePath, {0.0f, 0.0f}, color, {0.5f, 0.5f});
        // 行列はこちらが全インスタンスぶん入れるので、スプライト側に上書きさせない
        sprite->SetUseExternalTransforms(true);
        colorSprites_.push_back(std::move(sprite));
    }

    ShuffleCells();
    UpdateTransitionInstances();
}

void SceneTransition::Initialize()
{
    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize("debug/black1x1.png", {0, 0}, {1.0f, 1.0f, 1.0f, 1.0f});
    sprite_->SetSize(Vector2(static_cast<float>(WinApp::GetVirtualWidth()), static_cast<float>(WinApp::GetVirtualHeight()))); // 画面全体を覆うサイズ
    sprite_->SetAlpha(0.0f);                                                // 最初は完全に透明
    duration_ = 1.0f;                                                       // フェードの持続時間（例: 1秒）
    counter_ = 0.0f;                                                        // 経過時間カウンターを初期化
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    useTransition_ = true; // デフォルトはトランジションを使用

    // 六角形のマスを敷き詰め、色ごとのスプライトを用意する。
    // SetColors はスプライトの作り直しと色の割り振りまで面倒を見る
    BuildCells();
    SetColors(colors_);
}

void SceneTransition::BuildCells()
{
    const float screenWidth = static_cast<float>(WinApp::GetVirtualWidth());
    const float screenHeight = static_cast<float>(WinApp::GetVirtualHeight());

    // 正六角形（尖った頂点が上下）の敷き詰め。
    // 高さ h の正六角形は幅が h*sqrt(3)/2 で、縦は h*3/4 ごとに重ねると隙間なく並ぶ。
    // 1行おきに半マスずらすのがハニカムの形
    const float hexWidth = cellSize_ * kHexWidthRatio;
    const float stepX = hexWidth;
    const float stepY = cellSize_ * 0.75f;

    // 画面のふちが欠けないよう、上下左右に1マスぶん余分に置く
    const int cols = static_cast<int>(screenWidth / stepX) + 3;
    const int rows = static_cast<int>(screenHeight / stepY) + 3;

    cells_.clear();
    cells_.reserve(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    for (int row = 0; row < rows; ++row)
    {
        // 奇数行は半マスずらす
        const float offsetX = (row % 2 == 0) ? 0.0f : stepX * 0.5f;
        for (int col = 0; col < cols; ++col)
        {
            Cell cell{};
            cell.center = {col * stepX + offsetX - stepX, row * stepY - stepY};
            cells_.push_back(cell);
        }
    }
}

void SceneTransition::ShuffleCells()
{
    if (colors_.empty())
    {
        return;
    }

    std::uniform_int_distribution<int> colorPick(0, static_cast<int>(colors_.size()) - 1);
    std::uniform_real_distribution<float> delayPick(0.0f, maxDelay_);

    cellsByColor_.assign(colors_.size(), {});
    for (int index = 0; index < static_cast<int>(cells_.size()); ++index)
    {
        Cell &cell = cells_[static_cast<size_t>(index)];
        cell.colorIndex = colorPick(random_);
        // 埋まる順番はマスごとにばらばら。左上から順に、ではなく散らばって埋まる
        cell.delay = delayPick(random_);
        cell.size = 0.0f;
        cellsByColor_[static_cast<size_t>(cell.colorIndex)].push_back(index);
    }

    // 色ごとのインスタンス数を、その色に割り振られたマスの数へ合わせる
    for (size_t colorIndex = 0; colorIndex < colorSprites_.size(); ++colorIndex)
    {
        const size_t count = (colorIndex < cellsByColor_.size()) ? cellsByColor_[colorIndex].size() : 0;
        colorSprites_[colorIndex]->SetInstanceCount(static_cast<uint32_t>(count));
    }
}

void SceneTransition::Update()
{
    // トランジションを使用しない場合は即座に完了状態にする
    if (!useTransition_)
    {
        if (fadeInStart_ && !fadeInFinish_)
        {
            fadeInFinish_ = true;
        }
        if (fadeOutStart_ && !fadeOutFinish_)
        {
            fadeOutFinish_ = true;
        }
        if (fadeInFinish_ && fadeOutFinish_)
        {
            isEnd_ = true;
            fadeInStart_ = false;
            fadeOutStart_ = false;
        }
        return;
    }

    FadeUpdate();
}

void SceneTransition::Draw()
{
    SpriteCommon::GetInstance()->DrawCommonSetting();
    // トランジションを使用しない場合は描画しない
    if (!useTransition_)
    {
        return;
    }

    // 色ごとに1回。同じ色のマスはインスタンシングでまとめて描かれる
    for (const std::unique_ptr<Sprite> &sprite : colorSprites_)
    {
        sprite->Draw();
    }
}

void SceneTransition::Debug()
{
#ifdef USE_IMGUI
    ImGui::Begin("遷移");
    ImGui::DragFloat2("位置", &spPos_.x, 0.1f);
    ImGui::Checkbox("トランジション使用", &useTransition_);
    ImGui::End();
#endif // USE_IMGUI
}

void SceneTransition::FadeUpdate()
{
    if (fadeInStart_)
    {
        // フェードイン中
        if (!fadeInFinish_)
        {
            FadeIn();
        }
    }
    if (fadeOutStart_)
    {
        // フェードインが終わったら、フェードアウトを開始
        if (fadeInFinish_ && !fadeOutFinish_)
        {
            FadeOut();
        }
    }

    // トランジションが終了したら、終了フラグを立てる
    if (fadeInFinish_ && fadeOutFinish_)
    {
        isEnd_ = true;
        fadeInStart_ = false;
        fadeOutStart_ = false;
    }
}

void SceneTransition::FadeIn()
{
    // DefaultFadeIn();
    ReverseFadeIn();

    counter_ += 1.0f / 60.0f; // フレームレートを基にカウント（1フレームごとに0.0167秒進む）
    if (counter_ >= duration_)
    {
        counter_ = duration_; // 終了時間を超えないように制限
        fadeInFinish_ = true; // フェードイン終了フラグを立てる
    }
}

void SceneTransition::FadeOut()
{
    // DefaultFadeOut();
    ReverseFadeOut();

    // カウンターを減少（フレームレートに基づく）
    counter_ -= 1.0f / 60.0f;
    if (counter_ <= 0.0f)
    {
        counter_ = 0.0f;       // カウンターが負になるのを防ぐ
        fadeOutFinish_ = true; // フェードアウト完了フラグを立てる
    }
}

void SceneTransition::DefaultFadeIn()
{
    // アルファ値の計算（0.0fから1.0fに増加）
    float alpha = counter_ / duration_;
    sprite_->SetAlpha(alpha);
}

void SceneTransition::DefaultFadeOut()
{
    // アルファ値の計算（1.0fから0.0fに減少）
    float alpha = counter_ / duration_; // カウンターが減るほどアルファも減る
    sprite_->SetAlpha(alpha);           // アルファ値を設定
}

float SceneTransition::CalcCellSize(float localTime) const
{
    // cellSize_ は「画面に出る六角形の高さ」。画像の中では少し内側に描いてあるので、
    // その割合で割り戻したものがスプライト（正方形の板）の大きさになる
    const float fullSize = cellSize_ / kTextureFillRatio * kSeamMargin;

    // 伸びきるまでの時間。遅れの最大値を引いておくことで、
    // いちばん遅く始まったマスも counter_ が duration_ に届くまでに埋まりきる
    const float growTime = (std::max)(0.01f, duration_ - maxDelay_);

    if (localTime <= 0.0f)
    {
        return 0.0f; // まだ順番待ち
    }
    if (localTime >= growTime)
    {
        return fullSize; // 埋まりきった
    }
    return EaseInSine<float>(0.0f, fullSize, localTime / growTime, 1.0f);
}

void SceneTransition::ReverseFadeIn()
{
    // マスごとの遅れは ShuffleCells がばらばらに決めてある。
    // なので左上から順ではなく、画面のあちこちから埋まっていく
    for (Cell &cell : cells_)
    {
        cell.size = CalcCellSize(counter_ - cell.delay);
    }

    UpdateTransitionInstances();
}

void SceneTransition::ReverseFadeOut()
{
    // 明けるときは counter_ が減っていくので、埋めたときと同じ式でそのまま縮む。
    // 遅れも同じものを使うため、最後に埋まったマスから先に消えていく
    for (Cell &cell : cells_)
    {
        cell.size = CalcCellSize(counter_ - cell.delay);
    }

    UpdateTransitionInstances();
}

void SceneTransition::UpdateTransitionInstances()
{
    // 射影は毎回同じなので、マスごとに作り直さず1回で済ませる
    const Matrix4x4 viewMatrix = MakeIdentity4x4();
    const Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
        0.0f, 0.0f, float(WinApp::GetVirtualWidth()), float(WinApp::GetVirtualHeight()), 0.0f, 100.0f);
    const Matrix4x4 viewProjection = viewMatrix * projectionMatrix;

    // 色ごとに、その色のマスだけを詰めて入れる
    for (size_t colorIndex = 0; colorIndex < colorSprites_.size(); ++colorIndex)
    {
        if (colorIndex >= cellsByColor_.size())
        {
            continue;
        }

        Sprite *sprite = colorSprites_[colorIndex].get();
        const std::vector<int> &indices = cellsByColor_[colorIndex];
        for (size_t slot = 0; slot < indices.size(); ++slot)
        {
            const Cell &cell = cells_[static_cast<size_t>(indices[slot])];

            // 画像が正方形で六角形の形はその中に描いてあるので、縦横おなじ倍率で拡大する
            Transform transform{{cell.size, cell.size, 1.0f},
                                {0.0f, 0.0f, 0.0f},
                                {cell.center.x, cell.center.y, 0.0f}};
            Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

            TransformationMatrix transformMatrix;
            transformMatrix.WVP = worldMatrix * viewProjection;
            transformMatrix.World = worldMatrix;

            sprite->SetInstanceTransform(static_cast<uint32_t>(slot), transformMatrix);
        }
    }
}

// トランジション状態をリセット
void SceneTransition::Reset()
{
    counter_ = 0.0f;
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    sprite_->SetAlpha(0.0f); // 最初の透明状態に戻す

    // 色と埋まる順番を引き直す。切り替えのたびに違う模様になる
    ShuffleCells();
    UpdateTransitionInstances();
}
} // namespace Hagine
