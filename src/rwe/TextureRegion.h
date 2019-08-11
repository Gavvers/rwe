#pragma once

#include <rwe/TextureHandle.h>
#include <rwe/geometry/Rectangle2f.h>

namespace rwe
{
    struct TextureRegion
    {
        SharedTextureHandle texture;
        Rectangle2f region;

        TextureRegion(const SharedTextureHandle& texture, const Rectangle2f& region);

        bool operator==(const TextureRegion& rhs) const;

        bool operator!=(const TextureRegion& rhs) const;
    };
}
