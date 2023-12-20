/*
 * Copyright (c) 2019-2024 OtSofware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "SsaoPass.h"

#include "Camera.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Shader.h"
#include "TKOpenGL.h"
#include "TKProfiler.h"
#include "TKStats.h"
#include "ToolKit.h"

#include <random>

#include "DebugNew.h"

namespace ToolKit
{

  // SSAONoiseTexture
  //////////////////////////////////////////////////////////////////////////

  TKDefineClass(SSAONoiseTexture, DataTexture);

  void SSAONoiseTexture::Init(void* data)
  {
    if (m_initiated)
    {
      return;
    }

    GLint currId;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &currId);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, m_width, m_height, 0, GL_RG, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, currId);

    AddVRAMUsageInBytes(m_width * m_height * m_textureInternalFormatSize);

    m_initiated = true;
  }

  SSAONoiseTexture::SSAONoiseTexture()
  {
    // RG32 is the format
    m_textureInternalFormatSize = 8;
  }

  void SSAONoiseTexture::Init(bool flushClientSideArray)
  {
    assert(false); // The code should never come here
  }

  // SSAOPass
  //////////////////////////////////////////////////////////////////////////

  StringArray SSAOPass::m_ssaoSamplesStrCache;

  SSAOPass::SSAOPass()
  {
    m_ssaoFramebuffer = MakeNewPtr<Framebuffer>();
    m_ssaoTexture     = MakeNewPtr<RenderTarget>();
    m_tempBlurRt      = MakeNewPtr<RenderTarget>();
    m_noiseTexture    = MakeNewPtr<SSAONoiseTexture>(4, 4);
    m_quadPass        = MakeNewPtr<FullQuadPass>();

    m_ssaoSamplesStrCache.reserve(128);
    for (int i = 0; i < m_ssaoSamplesStrCacheSize; ++i)
    {
      m_ssaoSamplesStrCache.push_back("samples[" + std::to_string(i) + "]");
    }
  }

  SSAOPass::SSAOPass(const SSAOPassParams& params) : SSAOPass() { m_params = params; }

  SSAOPass::~SSAOPass()
  {
    m_ssaoFramebuffer = nullptr;
    m_noiseTexture    = nullptr;
    m_tempBlurRt      = nullptr;
    m_quadPass        = nullptr;
    m_ssaoShader      = nullptr;
  }

  void SSAOPass::Render()
  {
    PUSH_GPU_MARKER("SSAOPass::Render");
    PUSH_CPU_MARKER("SSAOPass::Render");

    Renderer* renderer = GetRenderer();

    // Generate SSAO texture
    renderer->SetTexture(1, m_params.GNormalBuffer->m_textureId);
    renderer->SetTexture(2, m_noiseTexture->m_textureId);
    renderer->SetTexture(3, m_params.GLinearDepthBuffer->m_textureId);

    m_ssaoShader->SetShaderParameter("radius", ParameterVariant(m_params.Radius));
    m_ssaoShader->SetShaderParameter("bias", ParameterVariant(m_params.Bias));

    RenderSubPass(m_quadPass);

    // Horizontal blur
    renderer->Apply7x1GaussianBlur(m_ssaoTexture, m_tempBlurRt, X_AXIS, 1.0f / m_ssaoTexture->m_width);

    // Vertical blur
    renderer->Apply7x1GaussianBlur(m_tempBlurRt, m_ssaoTexture, Y_AXIS, 1.0f / m_ssaoTexture->m_height);

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void SSAOPass::PreRender()
  {
    PUSH_GPU_MARKER("SSAOPass::PreRender");
    PUSH_CPU_MARKER("SSAOPass::PreRender");

    Pass::PreRender();

    int width           = m_params.GNormalBuffer->m_width;
    int height          = m_params.GNormalBuffer->m_height;

    // Clamp kernel size
    m_params.KernelSize = glm::clamp(m_params.KernelSize, m_minimumKernelSize, m_maximumKernelSize);

    GenerateSSAONoise();

    // No need destroy and re init framebuffer when size is changed, because
    // the only render target is already being resized.
    m_ssaoFramebuffer->Init({(uint) width, (uint) height, false, false});

    RenderTargetSettigs oneChannelSet = {};
    oneChannelSet.WarpS               = GraphicTypes::UVClampToEdge;
    oneChannelSet.WarpT               = GraphicTypes::UVClampToEdge;
    oneChannelSet.InternalFormat      = GraphicTypes::FormatR32F;
    oneChannelSet.Format              = GraphicTypes::FormatRed;
    oneChannelSet.Type                = GraphicTypes::TypeFloat;

    // Init ssao texture
    m_ssaoTexture->m_settings         = oneChannelSet;
    m_ssaoTexture->ReconstructIfNeeded((uint) width, (uint) height);

    m_ssaoFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_ssaoTexture);

    // Init temporary blur render target
    m_tempBlurRt->m_settings = oneChannelSet;
    m_tempBlurRt->ReconstructIfNeeded((uint) width, (uint) height);

    // Init noise texture
    m_noiseTexture->Init(&m_ssaoNoise[0]);

    m_quadPass->m_params.FrameBuffer      = m_ssaoFramebuffer;
    m_quadPass->m_params.ClearFrameBuffer = false;

    // SSAO fragment shader
    if (!m_ssaoShader)
    {
      m_ssaoShader = GetShaderManager()->Create<Shader>(ShaderPath("ssaoCalcFrag.shader", true));
    }

    if (m_params.KernelSize != m_currentKernelSize || m_prevSpread != m_params.spread)
    {
      // Update kernel
      for (int i = 0; i < m_params.KernelSize; ++i)
      {
        m_ssaoShader->SetShaderParameter(m_ssaoSamplesStrCache[i], ParameterVariant(m_ssaoKernel[i]));
      }

      m_prevSpread = m_params.spread;
    }

    m_ssaoShader->SetShaderParameter("screenSize", ParameterVariant(Vec2(width, height)));
    m_ssaoShader->SetShaderParameter("bias", ParameterVariant(m_params.Bias));
    m_ssaoShader->SetShaderParameter("kernelSize", ParameterVariant(m_params.KernelSize));
    m_ssaoShader->SetShaderParameter("projection", ParameterVariant(m_params.Cam->GetProjectionMatrix()));
    m_ssaoShader->SetShaderParameter("viewMatrix", ParameterVariant(m_params.Cam->GetViewMatrix()));

    m_quadPass->m_params.FragmentShader = m_ssaoShader;

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void SSAOPass::PostRender()
  {
    PUSH_GPU_MARKER("SSAOPass::PostRender");
    PUSH_CPU_MARKER("SSAOPass::PostRender");

    m_currentKernelSize = m_params.KernelSize;

    Pass::PostRender();

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void SSAOPass::GenerateSSAONoise()
  {
    CPU_FUNC_RANGE();

    if (m_prevSpread != m_params.spread)
    {
      GenerateRandomSamplesInHemisphere(m_maximumKernelSize, m_params.spread, m_ssaoKernel);
    }

    if (m_ssaoNoise.size() == 0)
    {
      // generates random floats between 0.0 and 1.0
      std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
      std::default_random_engine generator;

      for (unsigned int i = 0; i < 16; i++)
      {
        glm::vec2 noise(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f);
        m_ssaoNoise.push_back(noise);
      }
    }
  }

} // namespace ToolKit