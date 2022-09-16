#include "Renderer.h"

#include "Camera.h"
#include "DirectionComponent.h"
#include "Drawable.h"
#include "GL/glew.h"
#include "GlobalCache.h"
#include "Material.h"
#include "Mesh.h"
#include "Node.h"
#include "ResourceComponent.h"
#include "Scene.h"
#include "Shader.h"
#include "Skeleton.h"
#include "Surface.h"
#include "Texture.h"
#include "ToolKit.h"
#include "UIManager.h"
#include "Viewport.h"

#include <algorithm>

#include "DebugNew.h"

namespace ToolKit
{
#define BUFFER_OFFSET(idx) (static_cast<char*>(0) + (idx))

  Renderer::Renderer()
  {
    m_uiCamera = new Camera();
  }

  Renderer::~Renderer()
  {
    SafeDel(m_shadowMapCamera);
    SafeDel(m_uiCamera);
  }

  void Renderer::RenderScene(const ScenePtr scene,
                             Viewport* viewport,
                             const LightRawPtrArray& editorLights)
  {
    Camera* cam                = viewport->GetCamera();
    EntityRawPtrArray entities = scene->GetEntities();

    // Shadow pass
    UpdateShadowMaps(editorLights, entities);

    SetViewport(viewport);

    RenderEntities(entities, cam, viewport, editorLights);

    if (!cam->IsOrtographic())
    {
      RenderSky(scene->GetSky(), cam);
    }
  }

  /**
   * Renders given UILayer to given Viewport.
   * @param layer UILayer that will be rendered.
   * @param viewport that UILayer will be rendered with.
   */
  void Renderer::RenderUI(Viewport* viewport, UILayer* layer)
  {
    float halfWidth  = viewport->m_wndContentAreaSize.x * 0.5f;
    float halfHeight = viewport->m_wndContentAreaSize.y * 0.5f;

    m_uiCamera->SetLens(
        -halfWidth, halfWidth, -halfHeight, halfHeight, 0.5f, 1000.0f);

    EntityRawPtrArray entities = layer->m_scene->GetEntities();
    RenderEntities(entities, m_uiCamera, viewport);
  }

  void Renderer::Render(Entity* ntt,
                        Camera* cam,
                        const LightRawPtrArray& lights)
  {
    MeshComponentPtrArray meshComponents;
    ntt->GetComponent<MeshComponent>(meshComponents);

    MaterialPtr nttMat;
    if (MaterialComponentPtr matCom = ntt->GetComponent<MaterialComponent>())
    {
      if (nttMat = matCom->GetMaterialVal())
      {
        nttMat->Init();
      }
    }

    for (MeshComponentPtr meshCom : meshComponents)
    {
      MeshPtr mesh = meshCom->GetMeshVal();
      m_lights     = GetBestLights(ntt, lights);
      m_cam        = cam;
      SetProjectViewModel(ntt, cam);
      if (mesh->IsSkinned())
      {
        RenderSkinned(ntt, cam);
        return;
      }

      mesh->Init();

      MeshRawPtrArray meshCollector;
      mesh->GetAllMeshes(meshCollector);

      for (Mesh* mesh : meshCollector)
      {
        if (m_overrideMat != nullptr)
        {
          m_mat = m_overrideMat.get();
        }
        else
        {
          m_mat = nttMat ? nttMat.get() : mesh->m_material.get();
        }

        ProgramPtr prg =
            CreateProgram(m_mat->m_vertexShader, m_mat->m_fragmetShader);

        BindProgram(prg);
        FeedUniforms(prg);

        RenderState* rs = m_mat->GetRenderState();
        SetRenderState(rs, prg);

        glBindVertexArray(mesh->m_vaoId);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vboVertexId);
        SetVertexLayout(VertexLayout::Mesh);

        if (mesh->m_indexCount != 0)
        {
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_vboIndexId);
          glDrawElements((GLenum) rs->drawType,
                         mesh->m_indexCount,
                         GL_UNSIGNED_INT,
                         nullptr);
        }
        else
        {
          glDrawArrays((GLenum) rs->drawType, 0, mesh->m_vertexCount);
        }
      }
    }
  }

  void Renderer::RenderSkinned(Entity* object, Camera* cam)
  {
    MeshPtr mesh = object->GetMeshComponent()->GetMeshVal();
    SetProjectViewModel(object, cam);
    MaterialPtr nttMat = nullptr;
    if (object->GetMaterialComponent())
    {
      nttMat = object->GetMaterialComponent()->GetMaterialVal();
    }

    static ShaderPtr skinShader = GetShaderManager()->Create<Shader>(
        ShaderPath("defaultSkin.shader", true));

    SkeletonPtr skeleton = static_cast<SkinMesh*>(mesh.get())->m_skeleton;
    skeleton->UpdateTransformationTexture();

    MeshRawPtrArray meshCollector;
    mesh->GetAllMeshes(meshCollector);

    for (Mesh* mesh : meshCollector)
    {
      if (m_overrideMat != nullptr)
      {
        m_mat = m_overrideMat.get();
      }
      else
      {
        m_mat = nttMat ? nttMat.get() : mesh->m_material.get();
      }

      GLenum beforeSetting = glGetError();
      ProgramPtr prg       = CreateProgram(skinShader, m_mat->m_fragmetShader);
      BindProgram(prg);

      // Bind bone data
      {
        SetTexture(2, skeleton->m_bindPoseTexture->m_textureId);
        SetTexture(3, skeleton->m_boneTransformTexture->m_textureId);

        GLint loc       = glGetUniformLocation(prg->m_handle, "numBones");
        float boneCount = static_cast<float>(skeleton->m_bones.size());
        glUniform1fv(loc, 1, &boneCount);
      }
      GLenum afterSetting = glGetError();

      FeedUniforms(prg);

      RenderState* rs = m_mat->GetRenderState();
      SetRenderState(rs, prg);

      glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vboVertexId);
      SetVertexLayout(VertexLayout::SkinMesh);

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_vboIndexId);
      glDrawElements(
          (GLenum) rs->drawType, mesh->m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
  }

  void Renderer::Render2d(Surface* object, glm::ivec2 screenDimensions)
  {
    static ShaderPtr vertexShader = GetShaderManager()->Create<Shader>(
        ShaderPath("defaultVertex.shader", true));
    static ShaderPtr fragShader = GetShaderManager()->Create<Shader>(
        ShaderPath("unlitFrag.shader", true));
    static ProgramPtr prog = CreateProgram(vertexShader, fragShader);
    BindProgram(prog);

    MeshPtr mesh = object->GetMeshComponent()->GetMeshVal();
    mesh->Init();

    RenderState* rs = mesh->m_material->GetRenderState();
    SetRenderState(rs, prog);

    GLint pvloc = glGetUniformLocation(prog->m_handle, "ProjectViewModel");
    Mat4 pm     = glm::ortho(0.0f,
                         static_cast<float>(screenDimensions.x),
                         0.0f,
                         static_cast<float>(screenDimensions.y),
                         0.0f,
                         100.0f);

    Mat4 mul = pm * object->m_node->GetTransform(TransformationSpace::TS_WORLD);

    glUniformMatrix4fv(pvloc, 1, false, reinterpret_cast<float*>(&mul));

    glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vboVertexId);
    SetVertexLayout(VertexLayout::Mesh);

    glDrawArrays((GLenum) rs->drawType, 0, mesh->m_vertexCount);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    SetVertexLayout(VertexLayout::None);
  }

  void Renderer::Render2d(SpriteAnimation* object, glm::ivec2 screenDimensions)
  {
    Surface* surface = object->GetCurrentSurface();

    Node* backup    = surface->m_node;
    surface->m_node = object->m_node;

    Render2d(surface, screenDimensions);

    surface->m_node = backup;
  }

  void Renderer::SetRenderState(const RenderState* const state,
                                ProgramPtr program)
  {
    if (m_renderState.cullMode != state->cullMode)
    {
      if (state->cullMode == CullingType::TwoSided)
      {
        glDisable(GL_CULL_FACE);
      }

      if (state->cullMode == CullingType::Front)
      {
        if (m_renderState.cullMode == CullingType::TwoSided)
        {
          glEnable(GL_CULL_FACE);
        }
        glCullFace(GL_FRONT);
      }

      if (state->cullMode == CullingType::Back)
      {
        if (m_renderState.cullMode == CullingType::TwoSided)
        {
          glEnable(GL_CULL_FACE);
        }
        glCullFace(GL_BACK);
      }

      m_renderState.cullMode = state->cullMode;
    }

    if (m_renderState.depthTestEnabled != state->depthTestEnabled)
    {
      if (state->depthTestEnabled)
      {
        glEnable(GL_DEPTH_TEST);
      }
      else
      {
        glDisable(GL_DEPTH_TEST);
      }
      m_renderState.depthTestEnabled = state->depthTestEnabled;
    }

    if (m_renderState.blendFunction != state->blendFunction)
    {
      switch (state->blendFunction)
      {
      case BlendFunction::NONE:
        glDisable(GL_BLEND);
        break;
      case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
      }

      m_renderState.blendFunction = state->blendFunction;
    }

    if (state->diffuseTextureInUse)
    {
      m_renderState.diffuseTexture = state->diffuseTexture;
      SetTexture(0, state->diffuseTexture);
    }

    if (state->cubeMapInUse)
    {
      m_renderState.cubeMap = state->cubeMap;
      SetTexture(6, state->cubeMap);
    }

    if (m_renderState.lineWidth != state->lineWidth)
    {
      m_renderState.lineWidth = state->lineWidth;
      glLineWidth(m_renderState.lineWidth);
    }
  }

  void Renderer::SetRenderTarget(RenderTarget* renderTarget,
                                 bool clear,
                                 const Vec4& color)
  {
    if (m_renderTarget == renderTarget && m_renderTarget != nullptr)
    {
      return;
    }

    if (renderTarget != nullptr)
    {
      glBindFramebuffer(GL_FRAMEBUFFER, renderTarget->m_frameBufferId);
      glViewport(0, 0, renderTarget->m_width, renderTarget->m_height);

      if (glm::all(glm::epsilonNotEqual(color, m_bgColor, 0.001f)))
      {
        glClearColor(color.r, color.g, color.b, color.a);
      }

      if (clear)
      {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
      }

      if (glm::all(glm::epsilonNotEqual(color, m_bgColor, 0.001f)))
      {
        glClearColor(m_bgColor.r, m_bgColor.g, m_bgColor.b, m_bgColor.a);
      }
    }
    else
    {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, m_windowSize.x, m_windowSize.y);
    }

    m_renderTarget = renderTarget;
  }

  void Renderer::SwapRenderTarget(RenderTarget** renderTarget,
                                  bool clear,
                                  const Vec4& color)
  {
    RenderTarget* tmp = *renderTarget;
    *renderTarget     = m_renderTarget;
    SetRenderTarget(tmp, clear, color);
  }

  void Renderer::SetViewport(Viewport* viewport)
  {
    m_viewportSize = UVec2(viewport->m_wndContentAreaSize);
    SetRenderTarget(viewport->m_viewportImage);
  }

  void Renderer::SetViewportSize(uint width, uint height)
  {
    m_viewportSize.x = width;
    m_viewportSize.y = height;
    glViewport(0, 0, width, height);
  }

  void Renderer::DrawFullQuad(ShaderPtr fragmentShader)
  {
    static ShaderPtr fullQuadVert = GetShaderManager()->Create<Shader>(
        ShaderPath("fullQuadVert.shader", true));
    static MaterialPtr material = std::make_shared<Material>();
    material->UnInit();

    material->m_vertexShader  = fullQuadVert;
    material->m_fragmetShader = fragmentShader;
    material->Init();

    static Quad quad;
    quad.GetMeshComponent()->GetMeshVal()->m_material = material;

    static Camera quadCam;
    Render(&quad, &quadCam);
  }

  void Renderer::DrawCube(Camera* cam, MaterialPtr mat)
  {
    static Cube cube;
    cube.Generate();

    MaterialComponentPtr matc = cube.GetMaterialComponent();
    if (matc == nullptr)
    {
      cube.AddComponent(new MaterialComponent);
    }
    cube.GetMaterialComponent()->SetMaterialVal(mat);

    Render(&cube, cam);
  }

  void Renderer::RenderEntities(EntityRawPtrArray& entities,
                                Camera* cam,
                                Viewport* viewport,
                                const LightRawPtrArray& lights)
  {
    GetEnvironmentLightEntities(entities);

    // Dropout non visible & drawable entities.
    entities.erase(std::remove_if(entities.begin(),
                                  entities.end(),
                                  [](Entity* ntt) -> bool {
                                    return !ntt->GetVisibleVal() ||
                                           !ntt->IsDrawable();
                                  }),
                   entities.end());

    FrustumCull(entities, cam);

    EntityRawPtrArray blendedEntities;
    GetTransparentEntites(entities, blendedEntities);

    RenderOpaque(entities, cam, viewport->m_zoom, lights);

    RenderTransparent(blendedEntities, cam, viewport->m_zoom, lights);
  }

  void Renderer::FrustumCull(EntityRawPtrArray& entities, Camera* camera)
  {
    // Frustum cull
    Mat4 pr         = camera->GetProjectionMatrix();
    Mat4 v          = camera->GetViewMatrix();
    Frustum frustum = ExtractFrustum(pr * v, false);

    auto delFn = [frustum](Entity* ntt) -> bool {
      IntersectResult res = FrustumBoxIntersection(frustum, ntt->GetAABB(true));
      if (res == IntersectResult::Outside)
      {
        return true;
      }
      else
      {
        return false;
      }
    };
    entities.erase(std::remove_if(entities.begin(), entities.end(), delFn),
                   entities.end());
  }

  void Renderer::GetTransparentEntites(EntityRawPtrArray& entities,
                                       EntityRawPtrArray& blendedEntities)
  {
    auto delTrFn = [&blendedEntities](Entity* ntt) -> bool {
      // Check too see if there are any material with blend state.
      MaterialComponentPtrArray materials;
      ntt->GetComponent<MaterialComponent>(materials);

      if (!materials.empty())
      {
        for (MaterialComponentPtr& mt : materials)
        {
          if (mt->GetMaterialVal() &&
              mt->GetMaterialVal()->GetRenderState()->blendFunction !=
                  BlendFunction::NONE)
          {
            blendedEntities.push_back(ntt);
            return true;
          }
        }
      }
      else
      {
        MeshComponentPtrArray meshes;
        ntt->GetComponent<MeshComponent>(meshes);

        if (meshes.empty())
        {
          return false;
        }

        for (MeshComponentPtr& ms : meshes)
        {
          MeshRawCPtrArray all;
          ms->GetMeshVal()->GetAllMeshes(all);
          for (const Mesh* m : all)
          {
            if (m->m_material->GetRenderState()->blendFunction !=
                BlendFunction::NONE)
            {
              blendedEntities.push_back(ntt);
              return true;
            }
          }
        }
      }

      return false;
    };

    entities.erase(std::remove_if(entities.begin(), entities.end(), delTrFn),
                   entities.end());
  }

  void Renderer::RenderOpaque(EntityRawPtrArray entities,
                              Camera* cam,
                              float zoom,
                              const LightRawPtrArray& editorLights)
  {
    // Render opaque objects
    for (Entity* ntt : entities)
    {
      if (ntt->GetType() == EntityType::Entity_Billboard)
      {
        Billboard* billboard = static_cast<Billboard*>(ntt);
        billboard->LookAt(cam, zoom);
      }

      FindEnvironmentLight(ntt, cam);

      Render(ntt, cam, editorLights);
    }
  }

  void Renderer::RenderTransparent(EntityRawPtrArray entities,
                                   Camera* cam,
                                   float zoom,
                                   const LightRawPtrArray& editorLights)
  {
    StableSortByDistanceToCamera(entities, cam);
    StableSortByMaterialPriority(entities);

    // Render transparent entities
    for (Entity* ntt : entities)
    {
      if (ntt->GetType() == EntityType::Entity_Billboard)
      {
        Billboard* billboard = static_cast<Billboard*>(ntt);
        billboard->LookAt(cam, zoom);
      }

      FindEnvironmentLight(ntt, cam);

      // For two sided materials,
      // first render back of transparent objects then render front
      MaterialPtr renderMaterial = GetRenderMaterial(ntt);
      if (renderMaterial->GetRenderState()->cullMode == CullingType::TwoSided)
      {
        renderMaterial->GetRenderState()->cullMode = CullingType::Front;
        Render(ntt, cam, editorLights);

        renderMaterial->GetRenderState()->cullMode = CullingType::Back;
        Render(ntt, cam, editorLights);

        renderMaterial->GetRenderState()->cullMode = CullingType::TwoSided;
      }
      else
      {
        Render(ntt, cam, editorLights);
      }
    }
  }

  void Renderer::RenderSky(Sky* sky, Camera* cam)
  {
    if (sky == nullptr || !sky->GetDrawSkyVal())
    {
      return;
    }

    glDepthFunc(GL_LEQUAL);

    DrawCube(cam, sky->GetSkyboxMaterial());

    glDepthFunc(GL_LESS); // Return to default depth test
  }

  LightRawPtrArray Renderer::GetBestLights(Entity* entity,
                                           const LightRawPtrArray& lights)
  {
    LightRawPtrArray bestLights;
    LightRawPtrArray outsideRadiusLights;
    bestLights.reserve(lights.size());

    // Find the end of directional lights
    for (int i = 0; i < lights.size(); i++)
    {
      if (lights[i]->GetType() == EntityType::Entity_DirectionalLight)
      {
        bestLights.push_back(lights[i]);
      }
    }

    // Add the lights inside of the radius first
    for (int i = 0; i < lights.size(); i++)
    {
      {
        float radius;
        if (lights[i]->GetType() == EntityType::Entity_PointLight)
        {
          radius = static_cast<PointLight*>(lights[i])->GetRadiusVal();
        }
        else if (lights[i]->GetType() == EntityType::Entity_SpotLight)
        {
          radius = static_cast<SpotLight*>(lights[i])->GetRadiusVal();
        }
        else
        {
          continue;
        }

        float distance = glm::length2(
            entity->m_node->GetTranslation(TransformationSpace::TS_WORLD) -
            lights[i]->m_node->GetTranslation(TransformationSpace::TS_WORLD));

        if (distance < radius * radius)
        {
          bestLights.push_back(lights[i]);
        }
        else
        {
          outsideRadiusLights.push_back(lights[i]);
        }
      }
    }
    bestLights.insert(bestLights.end(),
                      outsideRadiusLights.begin(),
                      outsideRadiusLights.end());

    return bestLights;
  }

  void Renderer::GetEnvironmentLightEntities(EntityRawPtrArray entities)
  {
    // Find entities which have environment component
    m_environmentLightEntities.clear();
    for (Entity* ntt : entities)
    {
      if (ntt->GetType() == EntityType::Entity_Sky)
      {
        continue;
      }

      EnvironmentComponentPtr envCom =
          ntt->GetComponent<EnvironmentComponent>();
      if (envCom != nullptr && envCom->GetHdriVal() != nullptr &&
          envCom->GetHdriVal()->IsTextureAssigned() &&
          envCom->GetIlluminateVal())
      {
        envCom->Init(true);
        m_environmentLightEntities.push_back(ntt);
      }
    }
  }

  void Renderer::FindEnvironmentLight(Entity* entity, Camera* camera)
  {
    if (camera->IsOrtographic())
    {
      return;
    }

    // Note: If multiple bounding boxes are intersecting and the intersection
    // volume includes the entity, the smaller bounding box is taken

    // Iterate all entities and mark the ones which should
    // be lit with environment light

    Entity* env = nullptr;

    MaterialPtr mat = GetRenderMaterial(entity);
    if (mat == nullptr)
    {
      return;
    }

    Vec3 pos = entity->m_node->GetTranslation(TransformationSpace::TS_WORLD);
    BoundingBox bestBox;
    bestBox.max = ZERO;
    bestBox.min = ZERO;
    BoundingBox currentBox;
    env = nullptr;
    for (Entity* envNtt : m_environmentLightEntities)
    {
      currentBox.max =
          envNtt->GetComponent<EnvironmentComponent>()->GetBBoxMax();
      currentBox.min =
          envNtt->GetComponent<EnvironmentComponent>()->GetBBoxMin();

      if (PointInsideBBox(pos, currentBox.max, currentBox.min))
      {
        auto setCurrentBBox = [&bestBox, &env](const BoundingBox& box,
                                               Entity* ntt) -> void {
          bestBox = box;
          env     = ntt;
        };

        if (bestBox.max == bestBox.min && bestBox.max == ZERO)
        {
          setCurrentBBox(currentBox, envNtt);
          continue;
        }

        bool change = false;
        if (BoxBoxIntersection(bestBox, currentBox))
        {
          // Take the smaller box
          if (bestBox.Volume() > currentBox.Volume())
          {
            change = true;
          }
        }
        else
        {
          change = true;
        }

        if (change)
        {
          setCurrentBBox(currentBox, envNtt);
        }
      }
    }

    if (env != nullptr)
    {
      mat->GetRenderState()->IBLInUse = true;
      EnvironmentComponentPtr envCom =
          env->GetComponent<EnvironmentComponent>();
      mat->GetRenderState()->iblIntensity = envCom->GetIntensityVal();
      mat->GetRenderState()->irradianceMap =
          envCom->GetHdriVal()->GetIrradianceCubemapId();
    }
    else
    {
      // Sky light
      Sky* sky = GetSceneManager()->GetCurrentScene()->GetSky();
      if (sky != nullptr && sky->GetIlluminateVal())
      {
        mat->GetRenderState()->IBLInUse = true;
        EnvironmentComponentPtr envCom =
            sky->GetComponent<EnvironmentComponent>();
        mat->GetRenderState()->iblIntensity = envCom->GetIntensityVal();
        mat->GetRenderState()->irradianceMap =
            envCom->GetHdriVal()->GetIrradianceCubemapId();
      }
      else
      {
        mat->GetRenderState()->IBLInUse      = false;
        mat->GetRenderState()->irradianceMap = 0;
      }
    }
  }

  void Renderer::UpdateShadowMaps(LightRawPtrArray lights,
                                  EntityRawPtrArray entities)
  {
    MaterialPtr lastOverrideMaterial = m_overrideMat;

    GLint lastFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &lastFBO);

    for (Light* light : lights)
    {
      if (light->GetCastShadowVal())
      {
        // Create framebuffer
        light->InitShadowMap();

        // Get shadow map camera
        if (m_shadowMapCamera == nullptr)
        {
          m_shadowMapCamera = new Camera();
        }

        if (light->GetType() == EntityType::Entity_DirectionalLight)
        {
          FitSceneBoundingBoxIntoLightFrustum(
              m_shadowMapCamera,
              entities,
              static_cast<DirectionalLight*>(light));
        }
        else if (light->GetType() == EntityType::Entity_PointLight)
        {
          m_shadowMapCamera->SetLens(
              glm::radians(90.0f),
              light->GetShadowResolutionVal().x,
              light->GetShadowResolutionVal().y,
              0.01f,
              static_cast<SpotLight*>(light)->GetRadiusVal());
          m_shadowMapCamera->m_node->SetTranslation(
              light->m_node->GetTranslation(TransformationSpace::TS_WORLD),
              TransformationSpace::TS_WORLD);
        }
        else if (light->GetType() == EntityType::Entity_SpotLight)
        {
          m_shadowMapCamera->SetLens(
              glm::radians(static_cast<SpotLight*>(light)->GetOuterAngleVal()),
              light->GetShadowResolutionVal().x,
              light->GetShadowResolutionVal().y,
              0.01f,
              static_cast<SpotLight*>(light)->GetRadiusVal());
          m_shadowMapCamera->m_node->SetOrientation(
              light->m_node->GetOrientation(TransformationSpace::TS_WORLD));
          m_shadowMapCamera->m_node->SetTranslation(
              light->m_node->GetTranslation(TransformationSpace::TS_WORLD),
              TransformationSpace::TS_WORLD);
        }

        auto renderForShadowMapFn = [this](Light* light,
                                           EntityRawPtrArray entities) -> void {
          light->m_shadowMapCameraProjectionViewMatrix =
              m_shadowMapCamera->GetProjectionMatrix() *
              m_shadowMapCamera->GetViewMatrix();
          light->m_shadowMapCameraFar = m_shadowMapCamera->GetData().far;

          FrustumCull(entities, m_shadowMapCamera);

          glClear(GL_DEPTH_BUFFER_BIT);
          // Depth only render
          glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
          m_overrideMat = light->GetShadowMaterial();
          for (Entity* ntt : entities)
          {
            if (ntt->IsDrawable() &&
                ntt->GetMeshComponent()->GetCastShadowVal())
            {
              MaterialPtr entityMat = GetRenderMaterial(ntt);
              m_overrideMat->SetRenderState(entityMat->GetRenderState());
              m_overrideMat->m_alpha          = entityMat->m_alpha;
              m_overrideMat->m_diffuseTexture = entityMat->m_diffuseTexture;
              Render(ntt, m_shadowMapCamera);
            }
          }
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        };

        static Quaternion rotations[6];
        static Vec3 scales[6];

        // Calculate view transformations once
        static bool viewsCalculated = false;
        if (!viewsCalculated)
        {
          Mat4 views[6] = {
              glm::lookAt(
                  ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
              glm::lookAt(
                  ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
              glm::lookAt(
                  ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
              glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
              glm::lookAt(
                  ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
              glm::lookAt(
                  ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

          for (int i = 0; i < 6; ++i)
          {
            DecomposeMatrix(views[i], nullptr, &rotations[i], &scales[i]);
          }

          viewsCalculated = true;
        }

        if (light->GetType() == EntityType::Entity_PointLight)
        {
          glBindFramebuffer(GL_FRAMEBUFFER,
                            light->GetShadowMapRenderTarget()->m_frameBufferId);
          glViewport(0,
                     0,
                     static_cast<uint>(light->GetShadowResolutionVal().x),
                     static_cast<uint>(light->GetShadowResolutionVal().y));
          for (unsigned int i = 0; i < 6; ++i)
          {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                light->GetShadowMapRenderTarget()->m_textureId,
                0);
            m_shadowMapCamera->m_node->SetOrientation(
                rotations[i], TransformationSpace::TS_WORLD);
            m_shadowMapCamera->m_node->SetScale(scales[i]);

            renderForShadowMapFn(light, entities);
          }
        }
        else if (light->GetType() == EntityType::Entity_DirectionalLight)
        {
          glPolygonOffset(light->GetSlopedBiasVal() * 0.5f,
                          light->GetFixedBiasVal() * 500.0f);

          glEnable(GL_POLYGON_OFFSET_FILL);

          SetRenderTarget(light->GetShadowMapRenderTarget());
          renderForShadowMapFn(light, entities);

          glDisable(GL_POLYGON_OFFSET_FILL);
        }
        else // Spot light
        {
          SetRenderTarget(light->GetShadowMapRenderTarget());
          renderForShadowMapFn(light, entities);
        }
      }
    }

    m_overrideMat = lastOverrideMaterial;
    glBindFramebuffer(GL_FRAMEBUFFER, lastFBO);
  }

  void Renderer::FitSceneBoundingBoxIntoLightFrustum(
      Camera* lightCamera,
      const EntityRawPtrArray& entities,
      DirectionalLight* light)
  {
    TransformationSpace ts = TransformationSpace::TS_WORLD;

    // Calculate all scene's bounding box
    BoundingBox totalBBox;
    for (Entity* ntt : entities)
    {
      if (!(ntt->IsDrawable() && ntt->GetVisibleVal()))
      {
        continue;
      }
      if (!ntt->GetMeshComponent()->GetCastShadowVal())
      {
        continue;
      }
      BoundingBox bb = ntt->GetAABB(true);
      totalBBox.UpdateBoundary(bb.max);
      totalBBox.UpdateBoundary(bb.min);
    }
    Vec3 center = totalBBox.GetCenter();

    // Set light transformation
    lightCamera->m_node->SetTranslation(center, ts);
    lightCamera->m_node->SetOrientation(light->m_node->GetOrientation(ts), ts);
    Mat4 lightView = lightCamera->GetViewMatrix();

    // Bounding box of the scene
    Vec3 min         = totalBBox.min;
    Vec3 max         = totalBBox.max;
    Vec4 vertices[8] = {Vec4(min.x, min.y, min.z, 1.0f),
                        Vec4(min.x, min.y, max.z, 1.0f),
                        Vec4(min.x, max.y, min.z, 1.0f),
                        Vec4(max.x, min.y, min.z, 1.0f),
                        Vec4(min.x, max.y, max.z, 1.0f),
                        Vec4(max.x, min.y, max.z, 1.0f),
                        Vec4(max.x, max.y, min.z, 1.0f),
                        Vec4(max.x, max.y, max.z, 1.0f)};

    // Calculate bounding box in light space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::min();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::min();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::min();
    for (int i = 0; i < 8; ++i)
    {
      const Vec4 vertex = lightView * vertices[i];

      minX = std::min(minX, vertex.x);
      maxX = std::max(maxX, vertex.x);
      minY = std::min(minY, vertex.y);
      maxY = std::max(maxY, vertex.y);
      minZ = std::min(minZ, vertex.z);
      maxZ = std::max(maxZ, vertex.z);
    }

    lightCamera->SetLens(minX, maxX, minY, maxY, minZ, maxZ);
  }

  void Renderer::FitViewFrustumIntoLightFrustum(Camera* lightCamera,
                                                Camera* viewCamera,
                                                DirectionalLight* light)
  {
    assert(false && "Experimental.");
    // Fit view frustum into light frustum
    Vec3 frustum[8] = {Vec3(-1.0f, -1.0f, -1.0f),
                       Vec3(1.0f, -1.0f, -1.0f),
                       Vec3(1.0f, -1.0f, 1.0f),
                       Vec3(-1.0f, -1.0f, 1.0f),
                       Vec3(-1.0f, 1.0f, -1.0f),
                       Vec3(1.0f, 1.0f, -1.0f),
                       Vec3(1.0f, 1.0f, 1.0f),
                       Vec3(-1.0f, 1.0f, 1.0f)};

    const Mat4 inverseViewProj = glm::inverse(
        viewCamera->GetProjectionMatrix() * viewCamera->GetViewMatrix());

    for (int i = 0; i < 8; ++i)
    {
      const Vec4 t = inverseViewProj * Vec4(frustum[i], 1.0f);
      frustum[i]   = Vec3(t.x / t.w, t.y / t.w, t.z / t.w);
    }

    Vec3 center = ZERO;
    for (int i = 0; i < 8; ++i)
    {
      center += frustum[i];
    }
    center /= 8.0f;

    TransformationSpace ts = TransformationSpace::TS_WORLD;
    lightCamera->m_node->SetTranslation(center, ts);
    lightCamera->m_node->SetOrientation(light->m_node->GetOrientation(ts), ts);
    Mat4 lightView = lightCamera->GetViewMatrix();

    // Calculate bounding box
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::min();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::min();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::min();
    for (int i = 0; i < 8; ++i)
    {
      const Vec4 vertex = lightView * Vec4(frustum[i], 1.0f);
      minX              = std::min(minX, vertex.x);
      maxX              = std::max(maxX, vertex.x);
      minY              = std::min(minY, vertex.y);
      maxY              = std::max(maxY, vertex.y);
      minZ              = std::min(minZ, vertex.z);
      maxZ              = std::max(maxZ, vertex.z);
    }

    lightCamera->SetLens(minX, maxX, minY, maxY, minZ, maxZ);
  }

  void Renderer::SetProjectViewModel(Entity* ntt, Camera* cam)
  {
    m_view    = cam->GetViewMatrix();
    m_project = cam->GetProjectionMatrix();
    m_model   = ntt->m_node->GetTransform(TransformationSpace::TS_WORLD);
  }

  void Renderer::BindProgram(ProgramPtr program)
  {
    if (m_currentProgram == program->m_handle)
    {
      return;
    }

    m_currentProgram = program->m_handle;
    glUseProgram(program->m_handle);
  }

  void Renderer::LinkProgram(GLuint program, GLuint vertexP, GLuint fragmentP)
  {
    glAttachShader(program, vertexP);
    glAttachShader(program, fragmentP);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
      GLint infoLen = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1)
      {
        char* log = new char[infoLen];
        glGetProgramInfoLog(program, infoLen, nullptr, log);
        GetLogger()->Log(log);

        assert(linked);
        SafeDelArray(log);
      }

      glDeleteProgram(program);
    }
  }

  ProgramPtr Renderer::CreateProgram(ShaderPtr vertex, ShaderPtr fragment)
  {
    assert(vertex);
    assert(fragment);
    vertex->Init();
    fragment->Init();

    String tag;
    tag = vertex->m_tag + fragment->m_tag;
    if (m_programs.find(tag) == m_programs.end())
    {
      ProgramPtr program = std::make_shared<Program>(vertex, fragment);
      program->m_handle  = glCreateProgram();
      LinkProgram(
          program->m_handle, vertex->m_shaderHandle, fragment->m_shaderHandle);
      glUseProgram(program->m_handle);
      for (ubyte slotIndx = 0; slotIndx < m_rhiSettings::textureSlotCount;
           slotIndx++)
      {
        GLint loc = glGetUniformLocation(
            program->m_handle,
            ("s_texture" + std::to_string(slotIndx)).c_str());
        if (loc != -1)
        {
          glUniform1i(loc, slotIndx);
        }
      }

      m_programs[program->m_tag] = program;
    }

    return m_programs[tag];
  }

  void Renderer::FeedUniforms(ProgramPtr program)
  {
    for (ShaderPtr shader : program->m_shaders)
    {
      // Built-in variables.
      for (Uniform uni : shader->m_uniforms)
      {
        switch (uni)
        {
        case Uniform::PROJECT_MODEL_VIEW: {
          GLint loc =
              glGetUniformLocation(program->m_handle, "ProjectViewModel");
          Mat4 mul = m_project * m_view * m_model;
          glUniformMatrix4fv(loc, 1, false, &mul[0][0]);
        }
        break;
        case Uniform::MODEL: {
          GLint loc = glGetUniformLocation(program->m_handle, "Model");
          glUniformMatrix4fv(loc, 1, false, &m_model[0][0]);
        }
        break;
        case Uniform::INV_TR_MODEL: {
          GLint loc =
              glGetUniformLocation(program->m_handle, "InverseTransModel");
          Mat4 invTrModel = glm::transpose(glm::inverse(m_model));
          glUniformMatrix4fv(loc, 1, false, &invTrModel[0][0]);
        }
        break;
        case Uniform::LIGHT_DATA: {
          FeedLightUniforms(program);
        }
        break;
        case Uniform::CAM_DATA: {
          if (m_cam == nullptr)
            break;

          Camera::CamData data = m_cam->GetData();
          GLint loc = glGetUniformLocation(program->m_handle, "CamData.pos");
          glUniform3fv(loc, 1, &data.pos.x);
          loc = glGetUniformLocation(program->m_handle, "CamData.dir");
          glUniform3fv(loc, 1, &data.dir.x);
          loc = glGetUniformLocation(program->m_handle, "CamData.farPlane");
          glUniform1f(loc, data.far);
        }
        break;
        case Uniform::COLOR: {
          if (m_mat == nullptr)
            break;

          Vec4 color = Vec4(m_mat->m_color, m_mat->m_alpha);
          if (m_mat->GetRenderState()->blendFunction !=
              BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA)
          {
            color.a = 1.0f;
          }

          GLint loc = glGetUniformLocation(program->m_handle, "Color");
          glUniform4fv(loc, 1, &color.x);
        }
        break;
        case Uniform::FRAME_COUNT: {
          GLint loc = glGetUniformLocation(program->m_handle, "FrameCount");
          glUniform1ui(loc, m_frameCount);
        }
        break;
        case Uniform::GRID_SETTINGS: {
          GLint locCellSize =
              glGetUniformLocation(program->m_handle, "GridData.cellSize");
          glUniform1fv(locCellSize, 1, &m_gridParams.sizeEachCell);
          GLint locLineMaxPixelCount = glGetUniformLocation(
              program->m_handle, "GridData.lineMaxPixelCount");
          glUniform1fv(
              locLineMaxPixelCount, 1, &m_gridParams.maxLinePixelCount);
          GLint locHorizontalAxisColor = glGetUniformLocation(
              program->m_handle, "GridData.horizontalAxisColor");
          glUniform3fv(
              locHorizontalAxisColor, 1, &m_gridParams.axisColorHorizontal.x);
          GLint locVerticalAxisColor = glGetUniformLocation(
              program->m_handle, "GridData.verticalAxisColor");
          glUniform3fv(
              locVerticalAxisColor, 1, &m_gridParams.axisColorVertical.x);
          GLint locIs2dViewport =
              glGetUniformLocation(program->m_handle, "GridData.is2DViewport");
          glUniform1ui(locIs2dViewport, m_gridParams.is2DViewport);
        }
        break;
        case Uniform::EXPOSURE: {
          GLint loc = glGetUniformLocation(program->m_handle, "Exposure");
          glUniform1f(loc, shader->m_shaderParams["Exposure"].GetVar<float>());
        }
        break;
        case Uniform::PROJECTION_VIEW_NO_TR: {
          GLint loc =
              glGetUniformLocation(program->m_handle, "ProjectionViewNoTr");
          // Zero transalate variables in model matrix
          m_view[0][3] = 0.0f;
          m_view[1][3] = 0.0f;
          m_view[2][3] = 0.0f;
          m_view[3][3] = 1.0f;
          m_view[3][0] = 0.0f;
          m_view[3][1] = 0.0f;
          m_view[3][2] = 0.0f;
          Mat4 mul     = m_project * m_view;
          glUniformMatrix4fv(loc, 1, false, &mul[0][0]);
        }
        break;
        case Uniform::USE_IBL: {
          m_renderState.IBLInUse = m_mat->GetRenderState()->IBLInUse;
          GLint loc = glGetUniformLocation(program->m_handle, "UseIbl");
          glUniform1f(loc, static_cast<float>(m_renderState.IBLInUse));
        }
        break;
        case Uniform::IBL_INTENSITY: {
          m_renderState.iblIntensity = m_mat->GetRenderState()->iblIntensity;
          GLint loc = glGetUniformLocation(program->m_handle, "IblIntensity");
          glUniform1f(loc, static_cast<float>(m_renderState.iblIntensity));
        }
        break;
        case Uniform::IBL_IRRADIANCE: {
          m_renderState.irradianceMap = m_mat->GetRenderState()->irradianceMap;
          SetTexture(7, m_renderState.irradianceMap);
        }
        break;
        case Uniform::DIFFUSE_TEXTURE_IN_USE: {
          GLint loc =
              glGetUniformLocation(program->m_handle, "diffuseTextureInUse");
          glUniform1i(
              loc,
              static_cast<int>(m_mat->GetRenderState()->diffuseTextureInUse));
        }
        break;
        case Uniform::COLOR_ALPHA: {
          if (m_mat == nullptr)
            break;

          GLint loc = glGetUniformLocation(program->m_handle, "colorAlpha");
          if (m_mat->GetRenderState()->blendFunction ==
              BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA)
          {
            glUniform1f(loc, m_mat->m_alpha);
          }
          else
          {
            glUniform1f(loc, 1.0f);
          }
        }
        break;
        default:
          assert(false);
          break;
        }
      }

      // Custom variables.
      for (auto var : shader->m_shaderParams)
      {
        GLint loc = glGetUniformLocation(program->m_handle, var.first.c_str());
        if (loc == -1)
        {
          continue;
        }

        switch (var.second.GetType())
        {
        case ParameterVariant::VariantType::Float:
          glUniform1f(loc, var.second.GetVar<float>());
          break;
        case ParameterVariant::VariantType::Int:
          glUniform1i(loc, var.second.GetVar<int>());
          break;
        case ParameterVariant::VariantType::Vec3:
          glUniform3fv(
              loc, 1, reinterpret_cast<float*>(&var.second.GetVar<Vec3>()));
          break;
        case ParameterVariant::VariantType::Vec4:
          glUniform4fv(
              loc, 1, reinterpret_cast<float*>(&var.second.GetVar<Vec4>()));
          break;
        case ParameterVariant::VariantType::Mat3:
          glUniformMatrix3fv(
              loc,
              1,
              false,
              reinterpret_cast<float*>(&var.second.GetVar<Mat3>()));
          break;
        case ParameterVariant::VariantType::Mat4:
          glUniformMatrix4fv(
              loc,
              1,
              false,
              reinterpret_cast<float*>(&var.second.GetVar<Mat4>()));
          break;
        default:
          assert(false && "Invalid type.");
          break;
        }
      }
    }
  }

  void Renderer::FeedLightUniforms(ProgramPtr program)
  {
    ResetShadowMapBindings(program);

    size_t lightSize =
        glm::min(m_lights.size(), m_rhiSettings::maxLightsPerObject);
    for (size_t i = 0; i < lightSize; i++)
    {
      Light* currLight = m_lights[i];

      EntityType type = currLight->GetType();

      // Point light uniforms
      if (type == EntityType::Entity_PointLight)
      {
        Vec3 color      = currLight->GetColorVal();
        float intensity = currLight->GetIntensityVal();
        Vec3 pos =
            currLight->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        float radius = static_cast<PointLight*>(currLight)->GetRadiusVal();

        GLuint loc = glGetUniformLocation(program->m_handle,
                                          g_lightTypeStrCache[i].c_str());
        glUniform1i(loc, static_cast<GLuint>(2));
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightColorStrCache[i].c_str());
        glUniform3fv(loc, 1, &color.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightIntensityStrCache[i].c_str());
        glUniform1f(loc, intensity);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightPosStrCache[i].c_str());
        glUniform3fv(loc, 1, &pos.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightRadiusStrCache[i].c_str());
        glUniform1f(loc, radius);
      }
      // Directional light uniforms
      else if (type == EntityType::Entity_DirectionalLight)
      {
        Vec3 color      = currLight->GetColorVal();
        float intensity = currLight->GetIntensityVal();
        Vec3 dir        = static_cast<DirectionalLight*>(currLight)
                       ->GetComponent<DirectionComponent>()
                       ->GetDirection();

        GLuint loc = glGetUniformLocation(program->m_handle,
                                          g_lightTypeStrCache[i].c_str());
        glUniform1i(loc, static_cast<GLuint>(1));
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightColorStrCache[i].c_str());
        glUniform3fv(loc, 1, &color.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightIntensityStrCache[i].c_str());
        glUniform1f(loc, intensity);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightDirStrCache[i].c_str());
        glUniform3fv(loc, 1, &dir.x);
      }
      // Spot light uniforms
      else if (type == EntityType::Entity_SpotLight)
      {
        Vec3 color      = currLight->GetColorVal();
        float intensity = currLight->GetIntensityVal();
        Vec3 pos =
            currLight->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        SpotLight* spotLight = static_cast<SpotLight*>(currLight);
        Vec3 dir =
            spotLight->GetComponent<DirectionComponent>()->GetDirection();
        float radius = spotLight->GetRadiusVal();
        float outAngle =
            glm::cos(glm::radians(spotLight->GetOuterAngleVal() / 2.0f));
        float innAngle =
            glm::cos(glm::radians(spotLight->GetInnerAngleVal() / 2.0f));

        GLuint loc = glGetUniformLocation(program->m_handle,
                                          g_lightTypeStrCache[i].c_str());
        glUniform1i(loc, static_cast<GLuint>(3));
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightColorStrCache[i].c_str());
        glUniform3fv(loc, 1, &color.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightIntensityStrCache[i].c_str());
        glUniform1f(loc, intensity);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightPosStrCache[i].c_str());
        glUniform3fv(loc, 1, &pos.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightDirStrCache[i].c_str());
        glUniform3fv(loc, 1, &dir.x);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightRadiusStrCache[i].c_str());
        glUniform1f(loc, radius);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightOuterAngleStrCache[i].c_str());
        glUniform1f(loc, outAngle);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightInnerAngleStrCache[i].c_str());
        glUniform1f(loc, innAngle);
      }

      // Sanity check
      if (currLight->GetPCFSampleSizeVal() == 0.0f &&
          currLight->GetCastShadowVal())
      {
        currLight->SetPCFSampleSizeVal(1.0f);
      }

      float size       = currLight->GetPCFSampleSizeVal();
      float kernelSize = (float) currLight->GetPCFKernelSizeVal();
      if (glm::epsilonEqual(kernelSize, 0.0f, 0.00001f))
      {
        kernelSize = FLT_MIN;
      }
      float speed = size / kernelSize;
      speed -= 0.0005f; // Fix floating point error
      float step = kernelSize;
      float unit = 1.0f / ((step + 1.0f) * (step + 1.0f));

      GLuint loc = glGetUniformLocation(
          program->m_handle, g_lightPCFSampleHalfSizeCache[i].c_str());
      glUniform1f(loc, currLight->GetPCFSampleSizeVal() / 2.0f);

      loc = glGetUniformLocation(program->m_handle,
                                 g_lightPCFSampleDistanceCache[i].c_str());
      glUniform1f(loc, speed);

      loc = glGetUniformLocation(program->m_handle,
                                 g_lightPCFUnitSampleDistanceCache[i].c_str());
      glUniform1f(loc, unit);

      bool castShadow = currLight->GetCastShadowVal();
      if (castShadow)
      {
        GLint loc = glGetUniformLocation(
            program->m_handle, g_lightprojectionViewMatrixStrCache[i].c_str());
        glUniformMatrix4fv(
            loc,
            1,
            GL_FALSE,
            &(currLight->m_shadowMapCameraProjectionViewMatrix)[0][0]);

        loc = glGetUniformLocation(program->m_handle,
                                   g_lightNormalBiasStrCache[i].c_str());
        glUniform1f(loc, currLight->GetNormalBiasVal());

        loc = glGetUniformLocation(program->m_handle,
                                   g_lightShadowFixedBiasStrCache[i].c_str());
        glUniform1f(loc, currLight->GetFixedBiasVal() * 0.01f);
        loc = glGetUniformLocation(program->m_handle,
                                   g_lightShadowSlopedBiasStrCache[i].c_str());
        glUniform1f(loc, currLight->GetSlopedBiasVal() * 0.1f);

        loc = glGetUniformLocation(
            program->m_handle, g_lightShadowMapCamFarPlaneStrCache[i].c_str());
        glUniform1f(loc, currLight->m_shadowMapCameraFar);

        if (currLight->GetType() == EntityType::Entity_PointLight)
        {
          int level = static_cast<PointLight*>(currLight)->GetPCFLevelVal();
          if (level == 0)
          {
            level = 1;
          }
          else if (level == 1)
          {
            level = 8;
          }
          else if (level == 2)
          {
            level = 20;
          }
          loc = glGetUniformLocation(program->m_handle,
                                     g_PCFKernelSizeStrCache[i].c_str());
          glUniform1i(loc, level);
        }

        SetShadowMapTexture(
            type, currLight->GetShadowMapRenderTarget()->m_textureId, program);
      }

      loc = glGetUniformLocation(program->m_handle,
                                 g_lightCastShadowStrCache[i].c_str());
      glUniform1i(loc, static_cast<int>(castShadow));
    }

    GLint loc =
        glGetUniformLocation(program->m_handle, "LightData.activeCount");
    glUniform1i(loc, static_cast<int>(m_lights.size()));
  }

  void Renderer::SetVertexLayout(VertexLayout layout)
  {
    if (m_renderState.vertexLayout == layout)
    {
      return;
    }

    if (layout == VertexLayout::None)
    {
      for (int i = 0; i < 6; i++)
      {
        glDisableVertexAttribArray(i);
      }
    }

    if (layout == VertexLayout::Mesh)
    {
      GLuint offset = 0;
      glEnableVertexAttribArray(0); // Vertex
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
      offset += 3 * sizeof(float);

      glEnableVertexAttribArray(1); // Normal
      glVertexAttribPointer(
          1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), BUFFER_OFFSET(offset));
      offset += 3 * sizeof(float);

      glEnableVertexAttribArray(2); // Texture
      glVertexAttribPointer(
          2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), BUFFER_OFFSET(offset));
      offset += 2 * sizeof(float);

      glEnableVertexAttribArray(3); // BiTangent
      glVertexAttribPointer(
          3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), BUFFER_OFFSET(offset));
    }

    if (layout == VertexLayout::SkinMesh)
    {
      GLuint offset = 0;
      glEnableVertexAttribArray(0); // Vertex
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), 0);
      offset += 3 * sizeof(float);

      glEnableVertexAttribArray(1); // Normal
      glVertexAttribPointer(
          1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), BUFFER_OFFSET(offset));
      offset += 3 * sizeof(float);

      glEnableVertexAttribArray(2); // Texture
      glVertexAttribPointer(
          2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), BUFFER_OFFSET(offset));
      offset += 2 * sizeof(float);

      glEnableVertexAttribArray(3); // BiTangent
      glVertexAttribPointer(
          3, 3, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), BUFFER_OFFSET(offset));
      offset += 3 * sizeof(uint);

      glEnableVertexAttribArray(4); // Bones
      glVertexAttribPointer(
          4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), BUFFER_OFFSET(offset));
      offset += 4 * sizeof(unsigned int);

      glEnableVertexAttribArray(5); // Weights
      glVertexAttribPointer(
          5, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), BUFFER_OFFSET(offset));
    }
  }

  void Renderer::SetTexture(ubyte slotIndx, uint textureId)
  {
    // Slots:
    // 0 - 5 : 2D textures
    // 6 - 7 : Cube map textures
    // 0 -> Color Texture
    // 2 & 3 -> Skinning information
    // 7 -> Irradiance Map
    // Note: These are defaults.
    //  You can override these slots in your linked shader program
    assert(slotIndx < m_rhiSettings::textureSlotCount &&
           "You exceed texture slot count");
    m_textureSlots[slotIndx] = textureId;
    glActiveTexture(GL_TEXTURE0 + slotIndx);

    // Slot id 6 - 7 are cubemaps
    if (slotIndx < 6)
    {
      glBindTexture(GL_TEXTURE_2D, m_textureSlots[slotIndx]);
    }
    else
    {
      glBindTexture(GL_TEXTURE_CUBE_MAP, m_textureSlots[slotIndx]);
    }
  }

  void Renderer::SetShadowMapTexture(EntityType type,
                                     uint textureId,
                                     ProgramPtr program)
  {
    assert(IsLightType(type));

    if (m_bindedShadowMapCount >= m_rhiSettings::maxShadows)
    {
      return;
    }

    /*
     * Texture Slots:
     * 8-11: Directional and spot light shadow maps
     * 12-15: Point light shadow maps
     */

    if (type == EntityType::Entity_PointLight)
    {
      if (m_pointLightShadowCount < m_rhiSettings::maxPointLightShadows)
      {
        int curr = m_pointLightShadowCount +
                   m_rhiSettings::maxDirAndSpotLightShadows +
                   m_rhiSettings::textureSlotCount;
        glUniform1i(
            glGetUniformLocation(program->m_handle,
                                 ("LightData.pointLightShadowMap[" +
                                  std::to_string(m_pointLightShadowCount) + "]")
                                     .c_str()),
            curr);
        glActiveTexture(GL_TEXTURE0 + curr);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);
        m_bindedShadowMapCount++;
        m_pointLightShadowCount++;
      }
    }
    else
    {
      if (m_dirAndSpotLightShadowCount <
          m_rhiSettings::maxDirAndSpotLightShadows)
      {
        int curr =
            m_dirAndSpotLightShadowCount + m_rhiSettings::textureSlotCount;
        glUniform1i(glGetUniformLocation(
                        program->m_handle,
                        ("LightData.dirAndSpotLightShadowMap[" +
                         std::to_string(m_dirAndSpotLightShadowCount) + "]")
                            .c_str()),
                    curr);
        glActiveTexture(GL_TEXTURE0 + curr);
        glBindTexture(GL_TEXTURE_2D, textureId);
        m_bindedShadowMapCount++;
        m_dirAndSpotLightShadowCount++;
      }
    }
  }

  void Renderer::ResetShadowMapBindings(ProgramPtr program)
  {
    m_bindedShadowMapCount       = 0;
    m_dirAndSpotLightShadowCount = 0;
    m_pointLightShadowCount      = 0;
  }
} // namespace ToolKit
