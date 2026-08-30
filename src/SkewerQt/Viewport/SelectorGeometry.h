#pragma once

#include <QtQuick3D/qquick3dgeometry.h>

#include <cstdint>
#include <vector>

namespace skewer::qt {

struct RenderVertex {
    float px = 0.0F;
    float py = 0.0F;
    float pz = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
    float selected = 0.0F;
    float selectionPadding = 0.0F;
};

class SelectorGeometry final : public QQuick3DGeometry {
public:
    explicit SelectorGeometry(QQuick3DObject* parent = nullptr);

    void setTriangles(const std::vector<RenderVertex>& vertices);
};

} // namespace skewer::qt
