#ifndef RWE_RECTANGLE2F_H
#define RWE_RECTANGLE2F_H

#include <rwe/math/Vector2f.h>

namespace rwe
{
    struct Rectangle2f
    {
        Vector2f position;
        Vector2f extents;

        Rectangle2f(Vector2f position, Vector2f extents) : position(position), extents(extents) {}
        Rectangle2f(float x, float y, float halfWidth, float halfHeight) : position(x, y), extents(halfWidth, halfHeight) {}

        static Rectangle2f fromTLBR(float top, float left, float bottom, float right);

        bool contains(Vector2f point);

        bool contains(float x, float y);
    };
}


#endif