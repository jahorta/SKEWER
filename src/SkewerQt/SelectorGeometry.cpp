#include "SelectorGeometry.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm>
#include <cstring>
#include <limits>

namespace skewer::qt {

SelectorGeometry::SelectorGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent) {
}

void SelectorGeometry::setTriangles(const std::vector<RenderVertex>& vertices) {
    clear();
    setPrimitiveType(PrimitiveType::Triangles);
    if (vertices.empty()) {
        update();
        return;
    }

    QByteArray data{};
    data.resize(static_cast<qsizetype>(vertices.size() * sizeof(RenderVertex)));
    std::memcpy(data.data(), vertices.data(), vertices.size() * sizeof(RenderVertex));
    setStride(static_cast<int>(sizeof(RenderVertex)));
    setVertexData(data);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, 3 * static_cast<int>(sizeof(float)), Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, 6 * static_cast<int>(sizeof(float)), Attribute::F32Type);

    QVector3D minimum(std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D maximum(std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (const auto& vertex : vertices) {
        minimum.setX(std::min(minimum.x(), vertex.px));
        minimum.setY(std::min(minimum.y(), vertex.py));
        minimum.setZ(std::min(minimum.z(), vertex.pz));
        maximum.setX(std::max(maximum.x(), vertex.px));
        maximum.setY(std::max(maximum.y(), vertex.py));
        maximum.setZ(std::max(maximum.z(), vertex.pz));
    }
    setBounds(minimum, maximum);
    update();
}

} // namespace skewer::qt
