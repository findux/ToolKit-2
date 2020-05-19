#pragma once

#include "Types.h"
#include "RenderState.h"
#include <memory>
#include <unordered_map>

namespace ToolKit
{

  class Drawable;
  class Camera;
  class Light;
  class Surface;
  class SpriteAnimation;
  class Shader;
  class Material;
	class RenderTarget;

  class Renderer
  {
  public:
    Renderer();
    ~Renderer();
    void Render(Drawable* object, Camera* cam, const LightRawPtrArray& lights = LightRawPtrArray());
    void RenderSkinned(Drawable* object, Camera* cam);
    void Render2d(Surface* object, glm::ivec2 screenDimensions);
    void Render2d(SpriteAnimation* object, glm::ivec2 screenDimensions);
    void SetRenderState(RenderState state);
		void SetRenderTarget(RenderTarget* renderTarget);

  private:
    class Program
    {
    public:
      Program();
      Program(std::shared_ptr<Shader> vertex, std::shared_ptr<Shader> fragment);
      ~Program();

    public:
      GLuint m_handle = 0;
      String m_tag;
      std::vector<std::shared_ptr<Shader>> m_shaders;
    };

  private:
    void SetProjectViewModel(Drawable* object, Camera* cam);
    void BindProgram(std::shared_ptr<Program> program);
    void LinkProgram(GLuint program, GLuint vertexP, GLuint fragmentP);
    std::shared_ptr<Program> CreateProgram(std::shared_ptr<Shader> vertex, std::shared_ptr<Shader> fragment);
    void FeedUniforms(std::shared_ptr<Program> program);

  public:
    uint m_frameCount = 0;
		uint m_windowWidth = 0;
		uint m_windowHeight = 0;

  private:
    GLuint m_currentProgram;
    Mat4 m_project;
    Mat4 m_view;
    Mat4 m_model;
    LightRawPtrArray m_lights;
    Camera* m_cam = nullptr;
    Material* m_mat = nullptr;
		RenderTarget* m_renderTarget = nullptr;

    std::unordered_map<String, std::shared_ptr<Program>> m_programs;
    RenderState m_renderState;
  };

}
