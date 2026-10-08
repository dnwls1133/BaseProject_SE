#pragma once
#include "WorldRenderer3D.h"
class RandomLevelMap;

class Terrain3D
{
  public:
    static float Height(float x, float z);
    void Build(const RandomLevelMap& map);
    void Draw(Renderer& renderer) const;

  private:
    std::vector<std::shared_ptr<Geometry3D>> chunks;
};
