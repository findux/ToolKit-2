﻿/*
 * Copyright (c) 2019-2024 OtSofware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

// Purpose of this pass is exporting forward depths and normals before SSAO pass

#include "ForwardPreProcessPass.h"

#include "Shader.h"
#include "TKProfiler.h"
#include "stdafx.h"

namespace ToolKit
{

  ForwardPreProcess::ForwardPreProcess()
  {
    ShaderPtr vertexShader   = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcessVert.shader", true));
    ShaderPtr fragmentShader = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcess.shader", true));

    m_framebuffer            = MakeNewPtr<Framebuffer>();
    m_linearMaterial         = MakeNewPtr<Material>();
    m_normalRt               = MakeNewPtr<RenderTarget>();
    m_linearDepthRt          = MakeNewPtr<RenderTarget>();

    m_linearMaterial->m_vertexShader   = vertexShader;
    m_linearMaterial->m_fragmentShader = fragmentShader;
    m_linearMaterial->Init();

    RenderTargetSettigs oneChannelSet = {};
    oneChannelSet.WarpS               = GraphicTypes::UVClampToEdge;
    oneChannelSet.WarpT               = GraphicTypes::UVClampToEdge;
    oneChannelSet.InternalFormat      = GraphicTypes::FormatRGBA16F;
    oneChannelSet.Format              = GraphicTypes::FormatRGBA;
    oneChannelSet.Type                = GraphicTypes::TypeFloat;

    m_normalRt->m_settings            = oneChannelSet;

    oneChannelSet.InternalFormat      = GraphicTypes::FormatRGBA32F;
    m_linearDepthRt->m_settings       = oneChannelSet;
  }

  ForwardPreProcess::~ForwardPreProcess() {}

  void ForwardPreProcess::InitBuffers(uint width, uint height)
  {
    PUSH_GPU_MARKER("ForwardPreProcess::InitBuffers");
    PUSH_CPU_MARKER("ForwardPreProcess::InitBuffers");

    m_framebuffer->Init({width, height, false, false});
    m_framebuffer->ReconstructIfNeeded(width, height);
    m_normalRt->ReconstructIfNeeded(width, height);
    m_linearDepthRt->ReconstructIfNeeded(width, height);

    using FAttachment = Framebuffer::Attachment;

    m_framebuffer->DetachColorAttachment(FAttachment::ColorAttachment0);
    m_framebuffer->DetachColorAttachment(FAttachment::ColorAttachment1);

    m_framebuffer->SetColorAttachment(FAttachment::ColorAttachment0, m_linearDepthRt);
    m_framebuffer->SetColorAttachment(FAttachment::ColorAttachment1, m_normalRt);
    if (m_params.gFrameBuffer)
    {
      m_framebuffer->AttachDepthTexture(m_params.gFrameBuffer->GetDepthTexture());
    }
    else
    {
      InitDefaultDepthTexture(width, height);
      m_framebuffer->AttachDepthTexture(m_depthTexture);
    }

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void ForwardPreProcess::Render()
  {
    PUSH_GPU_MARKER("ForwardPreProcess::Render");
    PUSH_CPU_MARKER("ForwardPreProcess::Render");

    Renderer* renderer                      = GetRenderer();

    const auto renderLinearDepthAndNormalFn = [this, renderer](RenderJobArray& renderJobArray)
    {
      for (RenderJob& job : renderJobArray)
      {
        MaterialPtr activeMaterial         = job.Material;
        RenderState* renderstate           = activeMaterial->GetRenderState();
        BlendFunction beforeBlendFunc      = renderstate->blendFunction;
        m_linearMaterial->m_diffuseTexture = activeMaterial->m_diffuseTexture;
        m_linearMaterial->m_color          = activeMaterial->m_color;
        m_linearMaterial->SetAlpha(activeMaterial->GetAlpha());
        m_linearMaterial->SetRenderState(renderstate);
        m_linearMaterial->UnInit();

        renderer->m_overrideMat = m_linearMaterial;
        renderer->Render(job, m_params.Cam, {});
        renderstate->blendFunction = beforeBlendFunc;
      }
    };

    renderLinearDepthAndNormalFn(m_params.OpaqueJobs);
    // currently transparent objects are not rendered to export screen space normals or linear depth
    // we want SSAO and DOF to effect on opaque objects only
    // renderLinearDepthAndNormalFn(m_params.TranslucentJobs);

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void ForwardPreProcess::PreRender()
  {
    PUSH_GPU_MARKER("ForwardPreProcess::PreRender");
    PUSH_CPU_MARKER("ForwardPreProcess::PreRender");

    RenderPass::PreRender();

    Renderer* renderer = GetRenderer();
    if (m_params.gFrameBuffer)
    {
      renderer->SetFramebuffer(m_framebuffer, GraphicBitFields::None);

      // copy normal and linear depth from gbuffer to this
      renderer->CopyTexture(m_params.gNormalRt, m_normalRt);
      renderer->CopyTexture(m_params.gLinearRt, m_linearDepthRt);
    }
    else
    {
      // If no gbuffer, clear the current buffers to render onto
      GetRenderer()->SetFramebuffer(m_framebuffer, GraphicBitFields::AllBits);
    }

    renderer->SetCameraLens(m_params.Cam);

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void ForwardPreProcess::PostRender()
  {
    PUSH_GPU_MARKER("ForwardPreProcess::PostRender");
    PUSH_CPU_MARKER("ForwardPreProcess::PostRender");

    RenderPass::PostRender();

    POP_CPU_MARKER();
    POP_GPU_MARKER();
  }

  void ForwardPreProcess::InitDefaultDepthTexture(int width, int height)
  {
    if (m_depthTexture == nullptr)
    {
      m_depthTexture = MakeNewPtr<DepthTexture>();
      m_depthTexture->Init(width, height, false);
    }
  } // namespace ToolKit
} // namespace ToolKit