#pragma once

#include "Drawable.h"
#include "GlobalDef.h"

namespace ToolKit
{
  namespace Editor
  {

    class Grid : public Entity
    {
     public:
      explicit Grid(UVec2 size,
                    AxisLabel axis,
                    float cellSize,
                    float linePixelCount);

      void Resize(UVec2 size,
                  AxisLabel axis       = AxisLabel::ZX,
                  float cellSize       = 1.0f,
                  float linePixelCount = 2.0f);

      bool HitTest(const Ray& ray, Vec3& pos);

     private:
      void Init();

     public:
      UVec2 m_size;                      // m^2 size of the grid.
      float m_gridCellSize       = 1.0f; // m^2 size of each cell
      Vec3 m_horizontalAxisColor = g_gridAxisRed;
      Vec3 m_verticalAxisColor   = g_gridAxisBlue;
      float m_maxLinePixelCount  = 2.0f;

     private:
      bool m_initiated = false;
    };

  } //  namespace Editor
} //  namespace ToolKit
