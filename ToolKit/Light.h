#pragma once

#include "Entity.h"
#include "Framebuffer.h"
#include "Primative.h"
#include "Types.h"

#include <string>

namespace ToolKit
{
  class Framebuffer;
  class TK_API Light : public Entity
  {
   public:
    Light();
    virtual ~Light();
    virtual void ParameterEventConstructor();

    EntityType GetType() const override;
    void Serialize(XmlDocument* doc, XmlNode* parent) const override;
    void DeSerialize(XmlDocument* doc, XmlNode* parent) override;

    // Shadow operations
    virtual void InitShadowMap();
    virtual void UnInitShadowMap();
    Framebuffer* GetShadowMapFramebuffer();
    RenderTarget* GetShadowMapRenderTarget();
    MaterialPtr GetShadowMaterial();

   protected:
    virtual void InitShadowMapDepthMaterial();
    void ReInitShadowMap();

   public:
    TKDeclareParam(Vec3, Color);
    TKDeclareParam(float, Intensity);
    TKDeclareParam(bool, CastShadow);
    TKDeclareParam(float, FixedBias);
    TKDeclareParam(float, SlopedBias);
    TKDeclareParam(float, NormalBias);
    TKDeclareParam(Vec2, ShadowResolution);
    TKDeclareParam(float, PCFSampleSize);
    TKDeclareParam(int, PCFKernelSize);

    bool m_isStudioLight = false;
    Mat4 m_shadowMapCameraProjectionViewMatrix;
    float m_shadowMapCameraFar;

   protected:
    bool m_shadowMapInitialized       = false;
    bool m_shadowMapResolutionChanged = false;
    MaterialPtr m_shadowMapMaterial   = nullptr;
    Framebuffer* m_depthFramebuffer   = nullptr;
    RenderTarget* m_shadowRt          = nullptr;
  };

  class TK_API DirectionalLight : public Light
  {
   public:
    DirectionalLight();
    virtual ~DirectionalLight();

    EntityType GetType() const override;

    Vec3Array GetShadowFrustumCorners();
  };

  class TK_API PointLight : public Light
  {
   public:
    PointLight();
    virtual ~PointLight()
    {
    }

    EntityType GetType() const override;

    void InitShadowMap() override;

   protected:
    void InitShadowMapDepthMaterial() override;

   public:
    TKDeclareParam(float, Radius);
    TKDeclareParam(int, PCFLevel);
  };

  class TK_API SpotLight : public Light
  {
   public:
    SpotLight();
    virtual ~SpotLight()
    {
    }

    EntityType GetType() const override;

   protected:
    void InitShadowMapDepthMaterial() override;

   public:
    TKDeclareParam(float, Radius);
    TKDeclareParam(float, OuterAngle);
    TKDeclareParam(float, InnerAngle);
  };

} // namespace ToolKit
