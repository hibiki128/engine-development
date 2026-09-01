#include "PostEffectParameters.h"
#include <graphics/pipeline/PipelineManager.h>
#include <graphics/srv/SrvManager.h>
#include <graphics/texture/TextureManager.h>
#include <d3d12.h>

namespace Hagine {
void PostEffectParameters::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;
    TextureManager::GetInstance()->LoadTexture(texPath_);
    CreateAllBuffers();
}

void PostEffectParameters::SetShaderParameters(ShaderMode mode, ID3D12GraphicsCommandList *pCommandList,
                                               SrvManager *pSrvManager, DirectXCommon *pDxCommon)
{
    // b0 = 各エフェクトのパラメータ、t1 = 深度やノイズテクスチャ
    const ShaderRootSignature *postEffectRS =
        PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::Render, mode);
    assert(postEffectRS && "ポストエフェクトのルートシグネチャが未生成です");

    switch (mode)
    {
    case ShaderMode::Vignette:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), vignetteResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Smooth:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), smoothResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Gauss:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), gaussianResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Depth:
        pDepthData_->projectionInverse = Inverse(projectionInverse_);
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), depthResource_->GetGPUVirtualAddress());
        pCommandList->SetGraphicsRootDescriptorTable(postEffectRS->GetSrvIndex(1), pDxCommon->GetDepthGPUHandle());
        break;
    case ShaderMode::Blur:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), radialResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Cinematic:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), cinematicResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Dissolve:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), dissolveResource_->GetGPUVirtualAddress());
        pSrvManager->SetGraphicsRootDescriptorTable(postEffectRS->GetSrvIndex(1), TextureManager::GetInstance()->GetTextureIndexByFilePath(texPath_));
        break;
    case ShaderMode::Random:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), randomResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::FocusLine:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), focusLineResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Pixelate:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), pixelateResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Bloom:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), bloomResource_->GetGPUVirtualAddress());
        break;
    case ShaderMode::Retro:
        pCommandList->SetGraphicsRootConstantBufferView(postEffectRS->GetCbvIndex(0), retroResource_->GetGPUVirtualAddress());
        break;
    }
}

void PostEffectParameters::UpdateTimeParameters(float deltaTime)
{
    if (pRandomData_)
    {
        pRandomData_->time += deltaTime;
    }
    if (pFocusLineData_)
    {
        pFocusLineData_->time += deltaTime;
    }
    if (pRetroData_)
    {
        pRetroData_->time += deltaTime;
    }
}

void PostEffectParameters::SaveParameters(DataHandler *dataHandler) const
{
    // Vignette パラメータ
    if (pVignetteData_)
    {
        dataHandler->Save<float>("vignette_exponent", pVignetteData_->vignetteExponent);
        dataHandler->Save<float>("vignette_radius", pVignetteData_->vignetteRadius);
        dataHandler->Save<float>("vignette_strength", pVignetteData_->vignetteStrength);
        dataHandler->Save<Vector2>("vignette_center", pVignetteData_->vignetteCenter);
    }

    // Smooth パラメータ
    if (pSmoothData_)
    {
        dataHandler->Save<int>("smooth_kernelSize", pSmoothData_->kernelSize);
    }

    // Gaussian パラメータ
    if (pGaussianData_)
    {
        dataHandler->Save<int>("gaussian_kernelSize", pGaussianData_->kernelSize);
        dataHandler->Save<float>("gaussian_sigma", pGaussianData_->sigma);
    }

    // Depth パラメータ
    if (pDepthData_)
    {
        dataHandler->Save<int>("depth_kernelSize", pDepthData_->kernelSize);
    }

    // Radial Blur パラメータ
    if (pRadialData_)
    {
        dataHandler->Save<Vector2>("radial_center", pRadialData_->kCenter);
        dataHandler->Save<float>("radial_blurWidth", pRadialData_->kBlurWidth);
    }

    // Cinematic パラメータ
    if (pCinematicData_)
    {
        dataHandler->Save<Vector2>("cinematic_resolution", pCinematicData_->iResolution);
        dataHandler->Save<float>("cinematic_contrast", pCinematicData_->contrast);
        dataHandler->Save<float>("cinematic_saturation", pCinematicData_->saturation);
        dataHandler->Save<float>("cinematic_brightness", pCinematicData_->brightness);
    }

    // Dissolve パラメータ
    if (pDissolveData_)
    {
        dataHandler->Save<float>("dissolve_threshold", pDissolveData_->threshold);
        dataHandler->Save<float>("dissolve_edgeWidth", pDissolveData_->edgeWidth);
        dataHandler->Save<Vector3>("dissolve_edgeColor", pDissolveData_->edgeColor);
    }

    // Focus Line パラメータ
    if (pFocusLineData_)
    {
        dataHandler->Save<float>("focusLine_lines", pFocusLineData_->lines);
        dataHandler->Save<float>("focusLine_width", pFocusLineData_->width);
        dataHandler->Save<float>("focusLine_speed", pFocusLineData_->speed);
        dataHandler->Save<float>("focusLine_intensity", pFocusLineData_->intensity);
        dataHandler->Save<float>("focusLine_centerRadius", pFocusLineData_->centerRadius);
        dataHandler->Save<float>("focusLine_maxDistance", pFocusLineData_->maxDistance);
        dataHandler->Save<Vector4>("focusLine_lineColor", pFocusLineData_->lineColor);
    }

    if (pPixelateData_)
    {
        dataHandler->Save<float>("pixelate_blockSize", pPixelateData_->blockSize);
        dataHandler->Save<float>("pixelate_centerX", pPixelateData_->centerX);
        dataHandler->Save<float>("pixelate_centerY", pPixelateData_->centerY);
    }

    if (pBloomData_)
    {
        dataHandler->Save<float>("bloom_threshold", pBloomData_->bloomThreshold);
        dataHandler->Save<float>("bloom_intensity", pBloomData_->bloomIntensity);
    }

    if (pRetroData_)
    {
        dataHandler->Save<float>("retro_pixelSize", pRetroData_->pixelSize);
        dataHandler->Save<float>("retro_colorLevels", pRetroData_->colorLevels);
        dataHandler->Save<float>("retro_scanlineIntensity", pRetroData_->scanlineIntensity);
        dataHandler->Save<float>("retro_scanlineCount", pRetroData_->scanlineCount);
        dataHandler->Save<float>("retro_vignetteStrength", pRetroData_->vignetteStrength);
        dataHandler->Save<float>("retro_chromaticOffset", pRetroData_->chromaticOffset);
    }
}

void PostEffectParameters::LoadParameters(DataHandler *dataHandler)
{
    // Vignette パラメータ
    if (pVignetteData_)
    {
        pVignetteData_->vignetteExponent = dataHandler->Load<float>("vignette_exponent", 1.0f);
        pVignetteData_->vignetteRadius = dataHandler->Load<float>("vignette_radius", 1.0f);
        pVignetteData_->vignetteStrength = dataHandler->Load<float>("vignette_strength", 1.0f);
        pVignetteData_->vignetteCenter = dataHandler->Load<Vector2>("vignette_center", {0.5f, 0.5f});
    }

    // Smooth パラメータ
    if (pSmoothData_)
    {
        pSmoothData_->kernelSize = dataHandler->Load<int>("smooth_kernelSize", 3);
    }

    // Gaussian パラメータ
    if (pGaussianData_)
    {
        pGaussianData_->kernelSize = dataHandler->Load<int>("gaussian_kernelSize", 3);
        pGaussianData_->sigma = dataHandler->Load<float>("gaussian_sigma", 1.0f);
    }

    // Depth パラメータ
    if (pDepthData_)
    {
        pDepthData_->kernelSize = dataHandler->Load<int>("depth_kernelSize", 3);
    }

    // Radial Blur パラメータ
    if (pRadialData_)
    {
        pRadialData_->kCenter = dataHandler->Load<Vector2>("radial_center", {0.5f, 0.5f});
        pRadialData_->kBlurWidth = dataHandler->Load<float>("radial_blurWidth", 0.01f);
    }

    // Cinematic パラメータ
    if (pCinematicData_)
    {
        pCinematicData_->iResolution = dataHandler->Load<Vector2>("cinematic_resolution", {1280.0f, 720.0f});
        pCinematicData_->contrast = dataHandler->Load<float>("cinematic_contrast", 1.05f);
        pCinematicData_->saturation = dataHandler->Load<float>("cinematic_saturation", 0.68f);
        pCinematicData_->brightness = dataHandler->Load<float>("cinematic_brightness", 0.13f);
    }

    // Dissolve パラメータ
    if (pDissolveData_)
    {
        pDissolveData_->threshold = dataHandler->Load<float>("dissolve_threshold", 0.0f);
        pDissolveData_->edgeWidth = dataHandler->Load<float>("dissolve_edgeWidth", 0.01f);
        pDissolveData_->edgeColor = dataHandler->Load<Vector3>("dissolve_edgeColor", {1.0f, 0.0f, 0.0f});
    }

    // Focus Line パラメータ
    if (pFocusLineData_)
    {
        pFocusLineData_->lines = dataHandler->Load<float>("focusLine_lines", 16.0f);
        pFocusLineData_->width = dataHandler->Load<float>("focusLine_width", 0.01f);
        pFocusLineData_->speed = dataHandler->Load<float>("focusLine_speed", 1.0f);
        pFocusLineData_->intensity = dataHandler->Load<float>("focusLine_intensity", 0.3f);
        pFocusLineData_->centerRadius = dataHandler->Load<float>("focusLine_centerRadius", 0.5f);
        pFocusLineData_->maxDistance = dataHandler->Load<float>("focusLine_maxDistance", 1.0f);
        pFocusLineData_->lineColor = dataHandler->Load<Vector4>("focusLine_lineColor", {1.0f, 1.0f, 1.0f, 1.0f});
    }

    if (pPixelateData_)
    {
        pPixelateData_->blockSize = dataHandler->Load<float>("pixelate_blockSize", 0.1f);
        pPixelateData_->centerX = dataHandler->Load<float>("pixelate_centerX", 0.5f);
        pPixelateData_->centerY = dataHandler->Load<float>("pixelate_centerY", 0.5f);
    }

    if (pBloomData_)
    {
        pBloomData_->bloomThreshold = dataHandler->Load<float>("bloom_threshold", 1.0f);
        pBloomData_->bloomIntensity = dataHandler->Load<float>("bloom_intensity", 1.2f);
    }

    if (pRetroData_)
    {
        pRetroData_->pixelSize = dataHandler->Load<float>("retro_pixelSize", 4.0f);
        pRetroData_->colorLevels = dataHandler->Load<float>("retro_colorLevels", 8.0f);
        pRetroData_->scanlineIntensity = dataHandler->Load<float>("retro_scanlineIntensity", 0.4f);
        pRetroData_->scanlineCount = dataHandler->Load<float>("retro_scanlineCount", 400.0f);
        pRetroData_->vignetteStrength = dataHandler->Load<float>("retro_vignetteStrength", 0.6f);
        pRetroData_->chromaticOffset = dataHandler->Load<float>("retro_chromaticOffset", 0.003f);
    }
}

void PostEffectParameters::DrawParameterUI(ShaderMode mode)
{
#ifdef USE_IMGUI
    switch (mode)
    {
    case ShaderMode::Vignette:
        if (pVignetteData_)
        {
            ImGui::DragFloat("滑らかさ", &pVignetteData_->vignetteExponent, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("半径", &pVignetteData_->vignetteRadius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("強度", &pVignetteData_->vignetteStrength, 0.01f);
            ImGui::DragFloat2("中心", &pVignetteData_->vignetteCenter.x, 0.01f, -10.0f, 10.0f);
        }
        break;
    case ShaderMode::Smooth:
        if (pSmoothData_)
        {
            ImGui::DragInt("カーネルサイズ", &pSmoothData_->kernelSize, 2, 3, 7);
        }
        break;
    case ShaderMode::Gauss:
        if (pGaussianData_)
        {
            ImGui::DragInt("カーネルサイズ", &pGaussianData_->kernelSize, 2, 3, 7);
            ImGui::DragFloat("シグマ", &pGaussianData_->sigma, 0.01f, 0.01f, 10.0f);
        }
        break;
    case ShaderMode::Depth:
        if (pDepthData_)
        {
            ImGui::DragInt("カーネルサイズ", &pDepthData_->kernelSize, 2, 3, 7);
        }
        break;
    case ShaderMode::Blur:
        if (pRadialData_)
        {
            ImGui::DragFloat2("中心座標", &pRadialData_->kCenter.x, 0.1f);
            ImGui::DragFloat("幅", &pRadialData_->kBlurWidth, 0.01f);
        }
        break;
    case ShaderMode::Cinematic:
        if (pCinematicData_)
        {
            ImGui::DragFloat("コンストラクト", &pCinematicData_->contrast, 0.01f);
            ImGui::DragFloat("彩度", &pCinematicData_->saturation, 0.01f);
            ImGui::DragFloat("輝度", &pCinematicData_->brightness, 0.01f);
        }
        break;
    case ShaderMode::Dissolve:
        if (pDissolveData_)
        {
            ImGui::SliderFloat("Threshold", &pDissolveData_->threshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Edge Width", &pDissolveData_->edgeWidth, 0.0f, 0.5f, "%.3f");
            ImGui::ColorEdit3("Edge Color", reinterpret_cast<float *>(&pDissolveData_->edgeColor));
            ImGui::Checkbox("Invert", &pDissolveData_->invert);
        }
        break;
    case ShaderMode::Random:
        // Randomは時間のみなので特にUIなし
        break;
    case ShaderMode::FocusLine:
        if (pFocusLineData_)
        {
            ImGui::DragFloat("Time", &pFocusLineData_->time, 0.1f);
            ImGui::DragFloat("Lines", &pFocusLineData_->lines, 0.1f);
            ImGui::DragFloat("Width", &pFocusLineData_->width, 0.01f);
            ImGui::DragFloat("Speed", &pFocusLineData_->speed, 0.1f);
            ImGui::DragFloat("Intensity", &pFocusLineData_->intensity, 0.2f, 1.5f);

            ImGui::Separator();
            ImGui::Text("Area Settings");
            ImGui::DragFloat("Center Radius", &pFocusLineData_->centerRadius, 0.1f);
            ImGui::DragFloat("Max Distance", &pFocusLineData_->maxDistance, 0.1f);

            ImGui::Separator();
            ImGui::Text("Line Color");
            ImGui::ColorEdit3("Color", &pFocusLineData_->lineColor.x);
        }
        break;
    case ShaderMode::Pixelate:
        if (pPixelateData_)
        {
            ImGui::DragFloat("ブロックサイズ", &pPixelateData_->blockSize, 0.001f, 0.001f, 1.0f);
            ImGui::DragFloat("中心X", &pPixelateData_->centerX, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("中心Y", &pPixelateData_->centerY, 0.01f, 0.0f, 1.0f);
        }
        break;
    case ShaderMode::Bloom:
        if (pBloomData_)
        {
            ImGui::DragFloat("しきい値", &pBloomData_->bloomThreshold, 0.01f);
            ImGui::DragFloat("ブルーム強度", &pBloomData_->bloomIntensity, 0.01f);
        }
        break;
    case ShaderMode::Retro:
        if (pRetroData_)
        {
            ImGui::DragFloat("ピクセルサイズ", &pRetroData_->pixelSize, 0.1f, 1.0f, 32.0f);
            ImGui::DragFloat("減色レベル", &pRetroData_->colorLevels, 0.1f, 2.0f, 32.0f);
            ImGui::DragFloat("スキャンライン強度", &pRetroData_->scanlineIntensity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("スキャンライン本数", &pRetroData_->scanlineCount, 1.0f, 50.0f, 800.0f);
            ImGui::DragFloat("CRTビネット", &pRetroData_->vignetteStrength, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("色収差", &pRetroData_->chromaticOffset, 0.0001f, 0.0f, 0.02f);
            ImGui::DragFloat("解像度X", &pRetroData_->resolutionX, 1.0f, 320.0f, 3840.0f);
        }
        break;
    }
#endif // USE_IMGUI
}

void PostEffectParameters::CreateAllBuffers()
{
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

void PostEffectParameters::CreateSmooth()
{
    smoothResource_ = pDxCommon_->CreateBufferResource(sizeof(KernelSettings));
    smoothResource_->Map(0, nullptr, reinterpret_cast<void **>(&pSmoothData_));
    pSmoothData_->kernelSize = 3;
}

void PostEffectParameters::CreateGauss()
{
    gaussianResource_ = pDxCommon_->CreateBufferResource(sizeof(GaussianParams));
    gaussianResource_->Map(0, nullptr, reinterpret_cast<void **>(&pGaussianData_));
    pGaussianData_->kernelSize = 3;
    pGaussianData_->sigma = 1;
}

void PostEffectParameters::CreateVignette()
{
    vignetteResource_ = pDxCommon_->CreateBufferResource(sizeof(VignetteParameter));
    vignetteResource_->Map(0, nullptr, reinterpret_cast<void **>(&pVignetteData_));
    pVignetteData_->vignetteExponent = 1.0f;
    pVignetteData_->vignetteRadius = 1.0f;
    pVignetteData_->vignetteStrength = 1.0f;
    pVignetteData_->vignetteCenter = {0.5f, 0.5f};
}

void PostEffectParameters::CreateDepth()
{
    depthResource_ = pDxCommon_->CreateBufferResource(sizeof(Depth));
    depthResource_->Map(0, nullptr, reinterpret_cast<void **>(&pDepthData_));
    pDepthData_->projectionInverse = MakeIdentity4x4();
    pDepthData_->kernelSize = 3;
}

void PostEffectParameters::CreateRadial()
{
    radialResource_ = pDxCommon_->CreateBufferResource(sizeof(RadialBlur));
    radialResource_->Map(0, nullptr, reinterpret_cast<void **>(&pRadialData_));
    pRadialData_->kBlurWidth = 0.01f;
    pRadialData_->kCenter = {0.5f, 0.5f};
}

void PostEffectParameters::CreateCinematic()
{
    cinematicResource_ = pDxCommon_->CreateBufferResource(sizeof(Cinematic));
    cinematicResource_->Map(0, nullptr, reinterpret_cast<void **>(&pCinematicData_));
    pCinematicData_->iResolution = {1280.0f, 720.0f};
    pCinematicData_->contrast = 1.05f;
    pCinematicData_->saturation = 0.68f;
    pCinematicData_->brightness = 0.13f;
}

void PostEffectParameters::CreateDissolve()
{
    dissolveResource_ = pDxCommon_->CreateBufferResource(sizeof(Dissolve));
    dissolveResource_->Map(0, nullptr, reinterpret_cast<void **>(&pDissolveData_));
    pDissolveData_->threshold = 0.0f;
    pDissolveData_->edgeWidth = 0.01f;
    pDissolveData_->edgeColor = {1.0f, 1.0f, 1.0f}; // 白色
    pDissolveData_->invert = false;                 // 初期値はfalse
}

void PostEffectParameters::CreateRandom()
{
    randomResource_ = pDxCommon_->CreateBufferResource(sizeof(Random));
    randomResource_->Map(0, nullptr, reinterpret_cast<void **>(&pRandomData_));
    pRandomData_->time = 0.0f; // 初期値は適宜設定
}

void PostEffectParameters::CreateFocusLine()
{
    focusLineResource_ = pDxCommon_->CreateBufferResource(sizeof(FocusLine));
    focusLineResource_->Map(0, nullptr, reinterpret_cast<void **>(&pFocusLineData_));
    pFocusLineData_->lines = 16.0f;
    pFocusLineData_->width = 0.01f;
    pFocusLineData_->speed = 1.0f;
    pFocusLineData_->intensity = 0.3f;
}

void PostEffectParameters::CreatePixelate()
{
    pixelateResource_ = pDxCommon_->CreateBufferResource(sizeof(Pixelate));
    pixelateResource_->Map(0, nullptr, reinterpret_cast<void **>(&pPixelateData_));
    pPixelateData_->blockSize = 0.1f;
    pPixelateData_->centerX = 0.5f;
    pPixelateData_->centerY = 0.5f;
}

void PostEffectParameters::CreateBloom()
{
    bloomResource_ = pDxCommon_->CreateBufferResource(sizeof(Bloom));
    bloomResource_->Map(0, nullptr, reinterpret_cast<void **>(&pBloomData_));
    pBloomData_->bloomThreshold = 1.0f;
    pBloomData_->bloomIntensity = 1.2f;
    pBloomData_->texelSize.x = 1.0f / static_cast<float>(WinApp::GetVirtualWidth());
    pBloomData_->texelSize.y = 1.0f / static_cast<float>(WinApp::GetVirtualHeight());
}

void PostEffectParameters::CreateRetro()
{
    retroResource_ = pDxCommon_->CreateBufferResource(sizeof(Retro));
    retroResource_->Map(0, nullptr, reinterpret_cast<void **>(&pRetroData_));
    pRetroData_->pixelSize = 4.0f;
    pRetroData_->colorLevels = 8.0f;
    pRetroData_->scanlineIntensity = 0.4f;
    pRetroData_->scanlineCount = 400.0f;
    pRetroData_->vignetteStrength = 0.6f;
    pRetroData_->chromaticOffset = 0.003f;
    pRetroData_->time = 0.0f;
    pRetroData_->resolutionX = static_cast<float>(WinApp::GetVirtualWidth());
}
} // namespace Hagine
