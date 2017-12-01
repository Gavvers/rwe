#include "RenderService.h"
#include "rwe_string.h"
#include <rwe/math/rwe_math.h>

namespace rwe
{
    class IsOccupiedVisitor : public boost::static_visitor<bool>
    {
    public:
        bool operator()(const OccupiedNone&) const
        {
            return false;
        }

        bool operator()(const OccupiedUnit&) const
        {
            return true;
        }

        bool operator()(const OccupiedFeature&) const
        {
            return true;
        }
    };

    RenderService::RenderService(
        GraphicsContext* graphics,
        ShaderService* shaders,
        const CabinetCamera& camera)
        : graphics(graphics),
          shaders(shaders),
          camera(camera)
    {
    }

    void
    RenderService::renderSelectionRect(const Unit& unit)
    {
        // try to ensure that the selection rectangle vertices
        // are aligned with the middle of pixels,
        // to prevent discontinuities in the drawn lines.
        Vector3f snappedPosition(
            snapToInterval(unit.position.x, 1.0f) + 0.5f,
            snapToInterval(unit.position.y, 2.0f),
            snapToInterval(unit.position.z, 1.0f) + 0.5f);

        auto matrix = Matrix4f::translation(snappedPosition) * Matrix4f::rotationY(unit.rotation);

        const auto& shader = shaders->basicColor;
        graphics->bindShader(shader.handle.get());
        graphics->setUniformMatrix(shader.mvpMatrix, camera.getViewProjectionMatrix() * matrix);
        graphics->drawLineLoop(unit.selectionMesh.visualMesh);
    }

    void RenderService::renderUnit(const Unit& unit, float seaLevel)
    {
        auto matrix = Matrix4f::translation(unit.position) * Matrix4f::rotationY(unit.rotation);
        renderUnitMesh(unit.mesh, matrix, seaLevel);
    }

    void RenderService::renderUnitMesh(const UnitMesh& mesh, const Matrix4f& modelMatrix, float seaLevel)
    {
        Vector3f testRotation(-mesh.rotation.x, mesh.rotation.y, mesh.rotation.z);
        auto matrix = modelMatrix * Matrix4f::translation(mesh.origin) * Matrix4f::rotationXYZ(testRotation) * Matrix4f::translation(mesh.offset);

        if (mesh.visible)
        {
            auto mvpMatrix = camera.getViewProjectionMatrix() * matrix;

            {
                const auto& colorShader = shaders->unitColor;
                graphics->bindShader(colorShader.handle.get());
                graphics->setUniformMatrix(colorShader.mvpMatrix, mvpMatrix);
                graphics->setUniformMatrix(colorShader.modelMatrix, matrix);
                graphics->setUniformFloat(colorShader.seaLevel, seaLevel);
                graphics->drawTriangles(mesh.mesh->coloredVertices);
            }

            {
                const auto& textureShader = shaders->unitTexture;
                graphics->bindShader(textureShader.handle.get());
                graphics->bindTexture(mesh.mesh->texture.get());
                graphics->setUniformMatrix(textureShader.mvpMatrix, mvpMatrix);
                graphics->setUniformMatrix(textureShader.modelMatrix, matrix);
                graphics->setUniformFloat(textureShader.seaLevel, seaLevel);
                graphics->drawTriangles(mesh.mesh->texturedVertices);
            }
        }

        for (const auto& c : mesh.children)
        {
            renderUnitMesh(c, matrix, seaLevel);
        }
    }

    void RenderService::renderOccupiedGrid(const MapTerrain& terrain, const OccupiedGrid& occupiedGrid)
    {
        auto halfWidth = camera.getWidth() / 2.0f;
        auto halfHeight = camera.getHeight() / 2.0f;
        auto left = camera.getPosition().x - halfWidth;
        auto top = camera.getPosition().z - halfHeight;
        auto right = camera.getPosition().x + halfWidth;
        auto bottom = camera.getPosition().z + halfHeight;

        assert(left < right);
        assert(top < bottom);

        assert(terrain.getHeightMap().getWidth() >= 2);
        assert(terrain.getHeightMap().getHeight() >= 2);

        auto topLeftCell = terrain.worldToHeightmapCoordinate(Vector3f(left, 0.0f, top));
        topLeftCell.x = std::clamp(topLeftCell.x, 0, static_cast<int>(terrain.getHeightMap().getWidth() - 2));
        topLeftCell.y = std::clamp(topLeftCell.y, 0, static_cast<int>(terrain.getHeightMap().getHeight() - 2));

        auto bottomRightCell = terrain.worldToHeightmapCoordinate(Vector3f(right, 0.0f, bottom));
        bottomRightCell.y += 7; // compensate for height
        bottomRightCell.x = std::clamp(bottomRightCell.x, 0, static_cast<int>(terrain.getHeightMap().getWidth() - 2));
        bottomRightCell.y = std::clamp(bottomRightCell.y, 0, static_cast<int>(terrain.getHeightMap().getHeight() - 2));

        assert(topLeftCell.x <= bottomRightCell.x);
        assert(topLeftCell.y <= bottomRightCell.y);

        std::vector<Line3f> lines;

        std::vector<Triangle3f> tris;

        for (int y = topLeftCell.y; y <= bottomRightCell.y; ++y)
        {
            for (int x = topLeftCell.x; x <= bottomRightCell.x; ++x)
            {
                auto pos = terrain.heightmapIndexToWorldCorner(x, y);
                pos.y = terrain.getHeightMap().get(x, y);

                auto rightPos = terrain.heightmapIndexToWorldCorner(x + 1, y);
                rightPos.y = terrain.getHeightMap().get(x + 1, y);

                auto downPos = terrain.heightmapIndexToWorldCorner(x, y + 1);
                downPos.y = terrain.getHeightMap().get(x, y + 1);

                lines.emplace_back(pos, rightPos);
                lines.emplace_back(pos, downPos);

                if (boost::apply_visitor(IsOccupiedVisitor(), occupiedGrid.grid.get(x, y)))
                {
                    auto downRightPos = terrain.heightmapIndexToWorldCorner(x + 1, y + 1);
                    downRightPos.y = terrain.getHeightMap().get(x + 1, y + 1);

                    tris.emplace_back(pos, downPos, downRightPos);
                    tris.emplace_back(pos, downRightPos, rightPos);
                }
            }
        }

        const auto& shader = shaders->basicColor;
        graphics->bindShader(shader.handle.get());
        graphics->setUniformMatrix(shader.mvpMatrix, camera.getViewProjectionMatrix());

        auto mesh = createTemporaryLinesMesh(lines);
        graphics->drawLines(mesh);

        auto triMesh = createTemporaryTriMesh(tris);
        graphics->drawTriangles(triMesh);
    }

    GlMesh RenderService::createTemporaryLinesMesh(const std::vector<Line3f>& lines)
    {
        std::vector<GlColoredVertex> buffer;
        buffer.reserve(lines.size() * 2); // 2 verts per line

        Vector3f white(1.0f, 1.0f, 1.0f);

        for (const auto& l : lines)
        {
            buffer.emplace_back(l.start, white);
            buffer.emplace_back(l.end, white);
        }

        return graphics->createColoredMesh(buffer, GL_STREAM_DRAW);
    }

    GlMesh RenderService::createTemporaryTriMesh(const std::vector<Triangle3f>& tris)
    {
        std::vector<GlColoredVertex> buffer;
        buffer.reserve(tris.size() * 3); // 3 verts per triangle

        Vector3f white(1.0f, 1.0f, 1.0f);

        for (const auto& l : tris)
        {
            buffer.emplace_back(l.a, white);
            buffer.emplace_back(l.b, white);
            buffer.emplace_back(l.c, white);
        }

        return graphics->createColoredMesh(buffer, GL_STREAM_DRAW);
    }


    void RenderService::drawMapTerrain(const MapTerrain& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height)
    {
        // disable mipmapping
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        std::unordered_map<TextureIdentifier, std::vector<std::pair<unsigned int, unsigned int>>> batches;

        for (unsigned int dy = 0; dy < height; ++dy)
        {
            for (unsigned int dx = 0; dx < width; ++dx)
            {
                auto tileIndex = terrain.getTiles().get(x + dx, y + dy);
                const auto& tileTexture = terrain.getTileTexture(tileIndex);

                batches[tileTexture.texture.get()].emplace_back(dx, dy);
            }
        }

        for (const auto& batch : batches)
        {
            std::vector<GlTexturedVertex> vertices;

            for (const auto& p : batch.second)
            {
                auto dx = p.first;
                auto dy = p.second;

                auto tileIndex = terrain.getTiles().get(x + dx, y + dy);
                auto tilePosition = terrain.tileCoordinateToWorldCorner(x + dx, y + dy);

                const auto& tileTexture = terrain.getTileTexture(tileIndex);

                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z), tileTexture.region.topLeft());
                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z + MapTerrain::TileHeightInWorldUnits), tileTexture.region.bottomLeft());
                vertices.emplace_back(Vector3f(tilePosition.x + MapTerrain::TileWidthInWorldUnits, 0.0f, tilePosition.z + MapTerrain::TileHeightInWorldUnits), tileTexture.region.bottomRight());

                vertices.emplace_back(Vector3f(tilePosition.x + MapTerrain::TileWidthInWorldUnits, 0.0f, tilePosition.z + MapTerrain::TileHeightInWorldUnits), tileTexture.region.bottomRight());
                vertices.emplace_back(Vector3f(tilePosition.x + MapTerrain::TileWidthInWorldUnits, 0.0f, tilePosition.z), tileTexture.region.topRight());
                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z), tileTexture.region.topLeft());
            }

            const auto& shader = shaders->basicTexture;

            auto mesh = graphics->createTexturedMesh(vertices, GL_STREAM_DRAW);

            graphics->bindShader(shader.handle.get());
            graphics->bindTexture(batch.first);
            graphics->setUniformMatrix(shader.mvpMatrix, camera.getViewProjectionMatrix());
            graphics->drawTriangles(mesh);
        }
    }

    void RenderService::drawFeature(const MapFeature& feature)
    {
        if (feature.shadowAnimation)
        {
            graphics->disableDepth();
            float alpha = feature.transparentShadow ? 0.5f : 1.0f;
            drawStandingSprite(feature.position, *(*feature.shadowAnimation)->sprites[0], alpha);
            graphics->enableDepth();
        }

        float alpha = feature.transparentAnimation ? 0.5f : 1.0f;
        drawStandingSprite(feature.position, *feature.animation->sprites[0], alpha);
    }

    void RenderService::drawStandingSprite(const Vector3f& position, const Sprite& sprite, float alpha)
    {
        auto height = sprite.bounds.height();

        // Convert to a world-space standing position.
        // We stretch sprite y-dimension values by 2x
        // to correct for TA camera distortion.
        auto modelMatrix =
            Matrix4f::translation(position)
            * Matrix4f::scale(Vector3f(0.0f, -2.0f, 0.0f))
            * Matrix4f::translation(Vector3f(0.0f, -height, 0.0f));

        const auto& shader = shaders->basicTexture;
        graphics->bindShader(shader.handle.get());
        graphics->bindTexture(sprite.mesh.texture.get());
        graphics->setUniformMatrix(shader.mvpMatrix, camera.getViewProjectionMatrix() * modelMatrix);
        graphics->setUniformFloat(shader.alpha, alpha);
        graphics->drawTriangles(sprite.mesh.mesh);
    }

    void RenderService::renderMapTerrain(const MapTerrain& terrain)
    {
        Vector3f cameraExtents(camera.getWidth() / 2.0f, 0.0f, camera.getHeight() / 2.0f);
        auto topLeft = terrain.worldToTileCoordinate(camera.getPosition() - cameraExtents);
        auto bottomRight = terrain.worldToTileCoordinate(camera.getPosition() + cameraExtents);
        auto x1 = static_cast<unsigned int>(std::clamp<int>(topLeft.x, 0, terrain.getTiles().getWidth() - 1));
        auto y1 = static_cast<unsigned int>(std::clamp<int>(topLeft.y, 0, terrain.getTiles().getHeight() - 1));
        auto x2 = static_cast<unsigned int>(std::clamp<int>(bottomRight.x, 0, terrain.getTiles().getWidth() - 1));
        auto y2 = static_cast<unsigned int>(std::clamp<int>(bottomRight.y, 0, terrain.getTiles().getHeight() - 1));

        drawMapTerrain(terrain, x1, y1, (x2 + 1) - x1, (y2 + 1) - y1);
    }

    void RenderService::renderFlatFeatures(const MapTerrain& terrain)
    {
        for (const auto& f : terrain.getFeatures())
        {
            if (!f.isBlocking())
            {
                drawFeature(f);
            }
        }
    }

    void RenderService::renderStandingFeatures(const MapTerrain& terrain)
    {
        for (const auto& f : terrain.getFeatures())
        {
            if (f.isBlocking())
            {
                drawFeature(f);
            }
        }
    }

    void RenderService::renderUnitShadow(const Unit& unit, float groundHeight)
    {
        auto shadowProjection = Matrix4f::translation(Vector3f(0.0f, groundHeight, 0.0f))
            * Matrix4f::scale(Vector3f(1.0f, 0.0f, 1.0f))
            * Matrix4f::shearXZ(0.25f, -0.25f)
            * Matrix4f::translation(Vector3f(0.0f, -groundHeight, 0.0f));

        auto matrix = Matrix4f::translation(unit.position) * Matrix4f::rotationY(unit.rotation);

        renderUnitMesh(unit.mesh, shadowProjection * matrix, 0.0f);
    }

    CabinetCamera& RenderService::getCamera()
    {
        return camera;
    }

    const CabinetCamera& RenderService::getCamera() const
    {
        return camera;
    }
}
