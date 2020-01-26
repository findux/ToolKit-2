#pragma once

#include "GL\glew.h"

namespace ToolKit
{

  enum class BlendFunction
  {
    NONE,
    SRC_ALPHA_ONE_MINUS_SRC_ALPHA
  };

  enum class DrawType
  {
    //Quad = GL_QUADS,
    Triangle = GL_TRIANGLES,
    Line = GL_LINES,
    Point = GL_POINTS
  };

  struct RenderState
  {
    bool backCullingEnabled = true;
    bool depthTestEnabled = true;
    BlendFunction blendFunction = BlendFunction::NONE;
    DrawType drawType = DrawType::Triangle;
    GLuint diffuseTexture = 0;
    bool diffuseTextureInUse = false;
    GLuint cubeMap = 0;
    bool cubeMapInUse = false;
  };

}
