#include "HitStop.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include "filesystem"
#include <chrono>
#include <Frame.h>

using namespace Hagine;
void HitStop::Initialize() {
    LoadSettings();
}

void HitStop::Update() {
    // アクティブでないなら何もしない
    if (!isActive_)
        return;

    // 経過時間を加算
    elapsedTime_ += Frame::DeltaTime();
    // 停止時間を経過したら終了
    if (elapsedTime_ >= stopDuration_) {
        isActive_ = false;
        elapsedTime_ = 0.0f;
    }
}

void HitStop::Start() {
    // 経過時間をリセットして開始
    elapsedTime_ = 0.0f;
    isActive_ = true;
}

void HitStop::LoadSettings() {
    // JSONファイルから設定を読み込み
    std::ifstream file("resources/jsons/HitStop/hitstop.json");
    if (!file)
        return;

    nlohmann::json json;
    file >> json;
    file.close();

    stopDuration_ = json["stopDuration"];
    ImGuiNotification::Post("ヒットストップ設定を読み込みました", {0.2f, 0.8f, 0.8f, 1.0f});
}

void HitStop::SaveSettings() {
    // フォルダが存在しなければ作成
    std::filesystem::create_directories("resources/jsons/HitStop");
    nlohmann::json json;

    json["stopDuration"] = stopDuration_;

    // JSONファイルに保存
    std::ofstream file("resources/jsons/HitStop/hitstop.json");
    file << json.dump(4);
    file.close();
    ImGuiNotification::Post("ヒットストップ設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
}

void HitStop::imgui() {
#ifdef _DEBUG
    if (ImGui::Begin("ヒットストップ設定")) {
        // 停止時間のスライダー
        ImGui::DragFloat("停止時間 (秒)", &stopDuration_, 0.01f, 0.01f, 5.0f);

        // セーブボタン
        if (ImGui::Button("セーブ")) {
            SaveSettings();
        }

        // テスト用の開始ボタン
        if (ImGui::Button("ヒットストップ開始")) {
            Start();
        }
    }
    ImGui::End();
#endif
}
