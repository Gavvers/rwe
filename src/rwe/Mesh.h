#ifndef RWE_MESH_H
#define RWE_MESH_H

#include <array>
#include <rwe/SharedTextureHandle.h>
#include <rwe/math/Vector2f.h>
#include <rwe/math/Vector3f.h>

namespace rwe
{
    struct Mesh
    {
        struct Vertex
        {
            Vector3f position;
            Vector2f textureCoord;

            Vertex() = default;
            Vertex(const Vector3f& position, const Vector2f& textureCoord);
        };

        struct Triangle
        {
            Vertex a;
            Vertex b;
            Vertex c;

            Triangle() = default;
            Triangle(const Vertex& a, const Vertex& b, const Vertex& c);
        };

        SharedTextureHandle texture;

        std::vector<Triangle> faces;
    };
}

#endif
