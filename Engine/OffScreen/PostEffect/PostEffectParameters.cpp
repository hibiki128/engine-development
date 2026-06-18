#include "PostEffectParameters.h"
#include <Graphics/PipeLine/PipeLineManager.h>
#include <Graphics/Srv/SrvManager.h>
#include <Graphics/Texture/TextureManager.h>
#include <d3d12.h>

namespace Hagine {
void PostEffectParameters::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
    TextureManager::GetInstance()->LoadTexture(texPath_);
    CreateAllBuffers();
}

void PostEffectParameters::SetShaderParameters(ShaderMode mode, ID3D12GraphicsCommandList *commandList,
                                               SrvManager *srvManager, DirectXCommon *dxCommon) {
    switch (mode) {
    case ShaderMode::kVigneet:
        commandList->SetGraphicsRootConstantBufferView(1, vignetteResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kSmooth:
        commandList->SetGraphicsRootConstantBufferView(1, smoothResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kGauss:
        commandList->SetGraphicsRootConstantBufferView(1, gaussianResouce_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kDepth:
        depthData_->projectionInverse = Inverse(projectionInverse_);
        commandList->SetGraphicsRootConstantBufferView(1, depthResouce_->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(2, dxCommon->GetDepthGPUHandle());
        break;
    case ShaderMode::kBlur:
        commandList->SetGraphicsRootConstantBufferView(1, radialResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kCinematic:
        commandList->SetGraphicsRootConstantBufferView(1, cinematicResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kDissolve:
        commandList->SetGraphicsRootConstantBufferView(1, dissolveResource_->GetGPUVirtualAddress());
        srvManager->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetTextureIndexByFilePath(texPath_));
        break;
    case ShaderMode::kRandom:
        commandList->SetGraphicsRootConstantBufferView(1, randomResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kFocusLine:
        commandList->SetGraphicsRootConstantBufferView(1, focusLineResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kPixelate:
        commandList->SetGraphicsRootConstantBufferView(1, pixelateResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kBloom:
        commandList->SetGraphicsRootConstantBufferView(1, bloomResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::kRetro:
        commandList->SetGraphicsRootConstantBufferView(1, retroResource_->GetGPUVirtualAddress());
        break;
    }
}

void PostEffectParameters::UpdateTimeParameters(float deltaTime) {
    if (randomData_) {
        randomData_->time += deltaTime;
    }
    if (focusLineData_) {
        focusLineData_->time += deltaTime;
    }
    if (retroData_) {
        retroData_->time += deltaTime;
    }
}

void PostEffectParameters::SaveParameters(DataHandler *dataHandler) const {
    // Vignette パラメータ
    if (vignetteData_) {
        dataHandler->Save<float>("vignette_exponent", vignetteData_->vignetteExponent);
        dataHandler->Save<float>("vignette_radius", vignetteData_->vignetteRadius);
        dataHandler->Save<float>("vignette_strength", vignetteData_->vignetteStrength);
        dataHandler->Save<Vector2>("vignette_center", vignetteData_->vignetteCenter);
    }

    // Smooth パラメータ
    if (smoothData_) {
        dataHandler->Save<int>("smooth_kernelSize", smoothData_->kernelSize);
    }

    // Gaussian パラメータ
    if (gaussianData_) {
        dataHandler->Save<int>("gaussian_kernelSize", gaussianData_->kernelSize);
        dataHandler->Save<float>("gaussian_sigma", gaussianData_->sigma);
    }

    // Depth パラメータ
    if (depthData_) {
        dataHandler->Save<int>("depth_kernelSize", depthData_->kernelSize);
    }

    // Radial Blur パラメータ
    if (radialData_) {
        dataHandler->Save<Vector2>("radial_center", radialData_->kCenter);
        dataHandler->Save<float>("radial_blurWidth", radialData_->kBlurWidth);
    }

    // Cinematic パラメータ
    if (cinematicData_) {
        dataHandler->Save<Vector2>("cinematic_resolution", cinematicData_->iResolution);
        dataHandler->Save<float>("cinematic_contrast", cinematicData_->contrast);
        dataHandler->Save<float>("cinematic_saturation", cinematicData_->saturation);
        dataHandler->Save<float>("cinematic_brightness", cinematicData_->brightness);
    }

    // Dissolve パラメータ
    if (dissolveData_) {
        dataHandler->Save<float>("dissolve_threshold", dissolveData_->threshold);
        dataHandler->Save<float>("dissolve_edgeWidth", dissolveData_->edgeWidth);
        dataHandler->Save<Vector3>("dissolve_edgeColor", dissolveData_->edgeColor);
    }

    // Focus Line パラメータ
    if (focusLineData_) {
        dataHandler->Save<float>("focusLine_lines", focusLineData_->lines);
        dataHandler->Save<float>("focusLine_width", focusLineData_->width);
        dataHandler->Save<float>("focusLine_speed", focusLineData_->speed);
        dataHandler->Save<float>("focusLine_intensity", focusLineData_->intensity);
        dataHandler->Save<float>("focusLine_centerRadius", focusLineData_->centerRadius);
        dataHandler->Save<float>("focusLine_maxDistance", focusLineData_->maxDistance);
        dataHandler->Save<Vector4>("focusLine_lineColor", focusLineData_->lineColor);
    }

    if (pixelateData_) {
        dataHandler->Save<float>("pixelate_blockSize", pixelateData_->blockSize);
        dataHandler->Save<float>("pixelate_centerX", pixelateData_->centerX);
        dataHandler->Save<float>("pixelate_centerY", pixelateData_->centerY);
    }

    if (bloomData_) {
        dataHandler->Save<float>("bloom_threshold", bloomData_->bloomThreshold);
        dataHandler->Save<float>("bloom_intensity", bloomData_->bloomIntensity);
    }

    if (retroData_) {
        dataHandler->Save<float>("retro_pixelSize", retroData_->pixelSize);
        dataHandler->Save<float>("retro_colorLevels", retroData_->colorLevels);
        dataHandler->Save<float>("retro_scanlineIntensity", retroData_->scanlineIntensity);
        dataHandler->Save<float>("retro_scanlineCount", retroData_->scanlineCount);
        dataHandler->Save<float>("retro_vignetteStrength", retroData_->vignetteStrength);
        dataHandler->Save<float>("retro_chromaticOffset", retroData_->chromaticOffset);
    }

}

void PostEffectParameters::LoadParameters(DataHandler *dataHandler) {
    // Vignette パラメータ
    if (vignetteData_) {
        vignetteData_->vignetteExponent = dataHandler->Load<float>("vignette_exponent", 1.0f);
        vignetteData_->vignetteRadius = dataHandler->Load<float>("vignette_radius", 1.0f);
        vignetteData_->vignetteStrength = dataHandler->Load<float>("vignette_strength", 1.0f);
        vignetteData_->vignetteCenter = dataHandler->Load<Vector2>("vignette_center", {0.5f, 0.5f});
    }

    // Smooth パラメータ
    if (smoothData_) {
        smoothData_->kernelSize = dataHandler->Load<int>("smooth_kernelSize", 3);
    }

    // Gaussian パラメータ
    if (gaussianData_) {
        gaussianData_->kernelSize = dataHandler->Load<int>("gaussian_kernelSize", 3);
        gaussianData_->sigma = dataHandler->Load<float>("gaussian_sigma", 1.0f);
    }

    // Depth パラメータ
    if (depthData_) {
        depthData_->kernelSize = dataHandler->Load<int>("depth_kernelSize", 3);
    }

    // Radial Blur パラメータ
    if (radialData_) {
        radialData_->kCenter = dataHandler->Load<Vector2>("radial_center", {0.5f, 0.5f});
        radialData_->kBlurWidth = dataHandler->Load<float>("radial_blurWidth", 0.01f);
    }

    // Cinematic パラメータ
    if (cinematicData_) {
        cinematicData_->iResolution = dataHandler->Load<Vector2>("cinematic_resolution", {1280.0f, 720.0f});
        cinematicData_->contrast = dataHandler->Load<float>("cinematic_contrast", 1.05f);
        cinematicData_->saturation = dataHandler->Load<float>("cinematic_saturation", 0.68f);
        cinematicData_->brightness = dataHandler->Load<float>("cinematic_brightness", 0.13f);
    }

    // Dissolve パラメータ
    if (dissolveData_) {
        dissolveData_->threshold = dataHandler->Load<float>("dissolve_threshold", 0.0f);
        dissolveData_->edgeWidth = dataHandler->Load<float>("dissolve_edgeWidth", 0.01f);
        dissolveData_->edgeColor = dataHandler->Load<Vector3>("dissolve_edgeColor", {1.0f, 0.0f, 0.0f});
    }

    // Focus Line パラメータ
    if (focusLineData_) {
        focusLineData_->lines = dataHandler->Load<float>("focusLine_lines", 16.0f);
        focusLineData_->width = dataHandler->Load<float>("focusLine_width", 0.01f);
        focusLineData_->speed = dataHandler->Load<float>("focusLine_speed", 1.0f);
        focusLineData_->intensity = dataHandler->Load<float>("focusLine_intensity", 0.3f);
        focusLineData_->centerRadius = dataHandler->Load<float>("focusLine_centerRadius", 0.5f);
        focusLineData_->maxDistance = dataHandler->Load<float>("focusLine_maxDistance", 1.0f);
        focusLineData_->lineColor = dataHandler->Load<Vector4>("focusLine_lineColor", {1.0f, 1.0f, 1.0f, 1.0f});
    }

    if (pixelateData_) {
        pixelateData_->blockSize = dataHandler->Load<float>("pixelate_blockSize", 0.1f);
        pixelateData_->centerX = dataHandler->Load<float>("pixelate_centerX", 0.5f);
        pixelateData_->centerY = dataHandler->Load<float>("pixelate_centerY", 0.5f);
    }

    if (bloomData_) {
        bloomData_->bloomThreshold = dataHandler->Load<float>("bloom_threshold", 1.0f);
        bloomData_->bloomIntensity = dataHandler->Load<float>("bloom_intensity", 1.2f);
    }

    if (retroData_) {
        retroData_->pixelSize = dataHandler->Load<float>("retro_pixelSize", 4.0f);
        retroData_->colorLevels = dataHandler->Load<float>("retro_colorLevels", 8.0f);
        retroData_->scanlineIntensity = dataHandler->Load<float>("retro_scanlineIntensity", 0.4f);
        retroData_->scanlineCount = dataHandler->Load<float>("retro_scanlineCount", 400.0f);
        retroData_->vignetteStrength = dataHandler->Load<float>("retro_vignetteStrength", 0.6f);
        retroData_->chromaticOffset = dataHandler->Load<float>("retro_chromaticOffset", 0.003f);
    }
}

void PostEffectParameters::DrawParameterUI(ShaderMode mode) {
#ifdef _DEBUG
    switch (mode) {
    case ShaderMode::kVigneet:
        if (vignetteData_) {
            ImGui::DragFloat("滑らかさ", &vignetteData_->vignetteExponent, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("半径", &vignetteData_->vignetteRadius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("強度", &vignetteData_->vignetteStrength, 0.01f);
            ImGui::DragFloat2("中心", &vignetteData_->vignetteCenter.x, 0.01f, -10.0f, 10.0f);
        }
        break;
    case ShaderMode::kSmooth:
        if (smoothData_) {
            ImGui::DragInt("カーネルサイズ", &smoothData_->kernelSize, 2, 3, 7);
        }
        break;
    case ShaderMode::kGauss:
        if (gaussianData_) {
            ImGui::DragInt("カーネルサイズ", &gaussianData_->kernelSize, 2, 3, 7);
            ImGui::DragFloat("シグマ", &gaussianData_->sigma, 0.01f, 0.01f, 10.0f);
        }
        break;
    case ShaderMode::kDepth:
        if (depthData_) {
            ImGui::DragInt("カーネルサイズ", &depthData_->kernelSize, 2, 3, 7);
        }
        break;
    case ShaderMode::kBlur:
        if (radialData_) {
            ImGui::DragFloat2("中心座標", &radialData_->kCenter.x, 0.1f);
            ImGui::DragFloat("幅", &radialData_->kBlurWidth, 0.01f);
        }
        break;
    case ShaderMode::kCinematic:
        if (cinematicData_) {
            ImGui::DragFloat("コンストラクト", &cinematicData_->contrast, 0.01f);
            ImGui::DragFloat("彩度", &cinematicData_->saturation, 0.01f);
            ImGui::DragFloat("輝度", &cinematicData_->brightness, 0.01f);
        }
        break;
    case ShaderMode::kDissolve:
        if (dissolveData_) {
            ImGui::SliderFloat("Threshold", &dissolveData_->threshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Edge Width", &dissolveData_->edgeWidth, 0.0f, 0.5f, "%.3f");
            ImGui::ColorEdit3("Edge Color", reinterpret_cast<float *>(&dissolveData_->edgeColor));
            ImGui::Checkbox("Invert", &dissolveData_->invert);
        }
        break;
    case ShaderMode::kRandom:
        // Randomは時間のみなので特にUIなし
        break;
    case ShaderMode::kFocusLine:
        if (focusLineData_) {
            ImGui::DragFloat("Time", &focusLineData_->time, 0.1f);
            ImGui::DragFloat("Lines", &focusLineData_->lines, 0.1f);
            ImGui::DragFloat("Width", &focusLineData_->width, 0.01f);
            ImGui::DragFloat("Speed", &focusLineData_->speed, 0.1f);
            ImGui::DragFloat("Intensity", &focusLineData_->intensity, 0.2f, 1.5f);

            ImGui::Separator();
            ImGui::Text("Area Settings");
            ImGui::DragFloat("Center Radius", &focusLineData_->centerRadius, 0.1f);
            ImGui::DragFloat("Max Distance", &focusLineData_->maxDistance, 0.1f);

            ImGui::Separator();
            ImGui::Text("Line Color");
            ImGui::ColorEdit3("Color", &focusLineData_->lineColor.x);
        }
        break;
    case ShaderMode::kPixelate:
        if (pixelateData_) {
            ImGui::DragFloat("ブロックサイズ", &pixelateData_->blockSize, 0.001f, 0.001f, 1.0f);
            ImGui::DragFloat("中心X", &pixelateData_->centerX, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("中心Y", &pixelateData_->centerY,0.01f, 0.0f, 1.0f);
        }
        break;
    case ShaderMode::kBloom:
        if (bloomData_) {
            ImGui::DragFloat("しきい値", &bloomData_->bloomThreshold, 0.01f);
            ImGui::DragFloat("ブルーム強度", &bloomData_->bloomIntensity, 0.01f);
        }
        break;
    case ShaderMode::kRetro:
        if (retroData_) {
            ImGui::DragFloat("ピクセルサイズ", &retroData_->pixelSize, 0.1f, 1.0f, 32.0f);
            ImGui::DragFloat("減色レベル", &retroData_->colorLevels, 0.1f, 2.0f, 32.0f);
            ImGui::DragFloat("スキャンライン強度", &retroData_->scanlineIntensity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("スキャンライン本数", &retroData_->scanlineCount, 1.0f, 50.0f, 800.0f);
            ImGui::DragFloat("CRTビネット", &retroData_->vignetteStrength, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("色収差", &retroData_->chromaticOffset, 0.0001f, 0.0f, 0.02f);
            ImGui::DragFloat("解像度X", &retroData_->resolutionX, 1.0f, 320.0f, 3840.0f);
        }
        break;
    }
#endif // _DEBUG
}

void PostEffectParameters::CreateAllBuffers() {
    CreateSmooth();
    CreateGauss();
    CreateVignette();
    CreateDepth();
    CreateRadial();
    CreateCinematic();
    CreateDissolve();
    CreateRandom();
    CreateFocusLine();
    CreatePixelate();
    CreateBloom();
    CreateRetro();
}

void PostEffectParameters::CreateSmooth() {
    smoothResource_ = dxCommon_->CreateBufferResource(sizeof(KernelSettings));
    smoothResource_->Map(0, nullptr, reinterpret_cast<void **>(&smoothData_));
    smoothData_->kernelSize = 3;
}

void PostEffectParameters::CreateGauss() {
    gaussianResouce_ = dxCommon_->CreateBufferResource(sizeof(GaussianParams));
    gaussianResouce_->Map(0, nullptr, reinterpret_cast<void **>(&gaussianData_));
    gaussianData_->kernelSize = 3;
    gaussianData_->sigma = 1;
}

void PostEffectParameters::CreateVignette() {
    vignetteResource_ = dxCommon_->CreateBufferResource(sizeof(VignetteParameter));
    vignetteResource_->Map(0, nullptr, reinterpret_cast<void **>(&vignetteData_));
    vignetteData_->vignetteExponent = 1.0f;
    vignetteData_->vignetteRadius = 1.0f;
    vignetteData_->vignetteStrength = 1.0f;
    vignetteData_->vignetteCenter = {0.5f, 0.5f};
}

void PostEffectParameters::CreateDepth() {
    depthResouce_ = dxCommon_->CreateBufferResource(sizeof(Depth));
    depthResouce_->Map(0, nullptr, reinterpret_cast<void **>(&depthData_));
    depthData_->projectionInverse = MakeIdentity4x4();
    depthData_->kernelSize = 3;
}

void PostEffectParameters::CreateRadial() {
    radialResource_ = dxCommon_->CreateBufferResource(sizeof(RadialBlur));
    radialResource_->Map(0, nullptr, reinterpret_cast<void **>(&radialData_));
    radialData_->kBlurWidth = 0.01f;
    radialData_->kCenter = {0.5f, 0.5f};
}

void PostEffectParameters::CreateCinematic() {
    cinematicResource_ = dxCommon_->CreateBufferResource(sizeof(Cinematic));
    cinematicResource_->Map(0, nullptr, reinterpret_cast<void **>(&cinematicData_));
    cinematicData_->iResolution = {1280.0f, 720.0f};
    cinematicData_->contrast = 1.05f;
    cinematicData_->saturation = 0.68f;
    cinematicData_->brightness = 0.13f;
}

void PostEffectParameters::CreateDissolve() {
    dissolveResource_ = dxCommon_->CreateBufferResource(sizeof(Dissolve));
    dissolveResource_->Map(0, nullptr, reinterpret_cast<void **>(&dissolveData_));
    dissolveData_->threshold = 0.0f;
    dissolveData_->edgeWidth = 0.01f;
    dissolveData_->edgeColor = {1.0f, 1.0f, 1.0f}; // 白色
    dissolveData_->invert = false;                 // 初期値はfalse
}

void PostEffectParameters::CreateRandom() {
    randomResource_ = dxCommon_->CreateBufferResource(sizeof(Random));
    randomResource_->Map(0, nullptr, reinterpret_cast<void **>(&randomData_));
    randomData_->time = 0.0f; // 初期値は適宜設定
}

void PostEffectParameters::CreateFocusLine() {
    focusLineResource_ = dxCommon_->CreateBufferResource(sizeof(FocusLine));
    focusLineResource_->Map(0, nullptr, reinterpret_cast<void **>(&focusLineData_));
    focusLineData_->lines = 16.0f;
    focusLineData_->width = 0.01f;
    focusLineData_->speed = 1.0f;
    focusLineData_->intensity = 0.3f;
}

void PostEffectParameters::CreatePixelate() {
    pixelateResource_ = dxCommon_->CreateBufferResource(sizeof(Pixelate));
    pixelateResource_->Map(0, nullptr, reinterpret_cast<void **>(&pixelateData_));
    pixelateData_->blockSize = 0.1f;
    pixelateData_->centerX = 0.5f;
    pixelateData_->centerY = 0.5f;
}

void PostEffectParameters::CreateBloom() {
    bloomResource_ = dxCommon_->CreateBufferResource(sizeof(Bloom));
    bloomResource_->Map(0, nullptr, reinterpret_cast<void **>(&bloomData_));
    bloomData_->bloomThreshold = 1.0f;
    bloomData_->bloomIntensity = 1.2f;
    bloomData_->texelSize.x = 1.0f / WinApp::kClientWidth;
    bloomData_->texelSize.y = 1.0f / WinApp::kClientHeight;
}

void PostEffectParameters::CreateRetro() {
    retroResource_ = dxCommon_->CreateBufferResource(sizeof(Retro));
    retroResource_->Map(0, nullptr, reinterpret_cast<void **>(&retroData_));
    retroData_->pixelSize = 4.0f;
    retroData_->colorLevels = 8.0f;
    retroData_->scanlineIntensity = 0.4f;
    retroData_->scanlineCount = 400.0f;
    retroData_->vignetteStrength = 0.6f;
    retroData_->chromaticOffset = 0.003f;
    retroData_->time = 0.0f;
    retroData_->resolutionX = static_cast<float>(WinApp::kClientWidth);
}
} // namespace Hagine
