#include "MeshDatabase.h"

namespace rwe
{
    void MeshDatabase::addSelectionMesh(const std::string& objectName, std::shared_ptr<GlMesh> mesh)
    {
        selectionMeshesMap.insert({objectName, mesh});
    }

    std::optional<std::shared_ptr<GlMesh>> MeshDatabase::getSelectionMesh(const std::string& objectName) const
    {
        auto it = selectionMeshesMap.find(objectName);
        if (it == selectionMeshesMap.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void MeshDatabase::addUnitPieceMesh(const std::string& unitName, const std::string& pieceName, std::shared_ptr<ShaderMesh> pieceMesh)
    {
        unitPieceMeshesMap.insert({{unitName, pieceName}, pieceMesh});
    }

    std::optional<std::shared_ptr<ShaderMesh>> MeshDatabase::getUnitPieceMesh(const std::string& objectName, const std::string& pieceName) const
    {
        auto it = unitPieceMeshesMap.find({objectName, pieceName});
        if (it == unitPieceMeshesMap.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

}
