#include "chisel/map/Solid.h"
#include "chisel/Chisel.h"
#include "common/Bit.h"

#include <unordered_set>

namespace chisel
{
    inline void InitSideData(Side& side)
    {
        side.rotate = 0.0f;

        side.textureAxes[0].w = 0.0f;
        side.textureAxes[1].w = 0.0f;

        side.scale[0] = 0.25f;
        side.scale[1] = 0.25f;

        side.textureAxes[0].xyz = vec3(0);
        side.textureAxes[1].xyz = vec3(0);

        Orientation orientation = Orientations::CalcOrientation(side.plane);
        if (orientation == Orientations::Invalid)
            return;

        side.textureAxes[1].xyz = Orientations::DownVectors[orientation];
        if (trans_texture_face_alignment)
        {
            // Calculate true U axis
            side.textureAxes[0].xyz = glm::normalize(
                glm::cross(glm::vec3(side.textureAxes[1].xyz), side.plane.normal));

            // Now calculate the true V axis
            side.textureAxes[1].xyz = glm::normalize(
                glm::cross(side.plane.normal, glm::vec3(side.textureAxes[0].xyz)));
        }
        else
        {
            side.textureAxes[0].xyz = Orientations::RightVectors[orientation];
        }
    }

    Solid::Solid(BrushEntity* parent)
        : m_parent(parent)
    {
    }

    Solid::Solid(BrushEntity* parent, std::vector<Side> sides, bool initMesh)
        : m_parent(parent)
        , m_sides(std::move(sides))
    {
        if (initMesh)
            UpdateMesh();
    }

    Solid::Solid(Solid&& other)
    {
        this->m_parent = other.m_parent;
        other.m_parent = nullptr;

        this->m_meshes = std::move(other.m_meshes);
        other.m_meshes.clear();

        this->m_sides = std::move(other.m_sides);
        other.m_sides.clear();

        this->m_faces = std::move(other.m_faces);
        other.m_faces.clear();

        this->m_bounds = other.m_bounds;
        other.m_bounds = std::nullopt;

    }
        
    Solid::~Solid()
    {
    }

    struct Winding
    {
        static constexpr uint32_t MaxWindingPoints = 128;

        Winding()
        {
            count = 0;
        }

        vec3 points[MaxWindingPoints];
        uint32_t count;
    };

    void CreateWindingFromPlane(const Plane& plane, Winding& winding)
    {
        uint32_t x = ~0u;
        float max = -FLT_MAX;
        for (uint32_t i = 0; i < 3; i++)
        {
            float v = fabsf(plane.normal[i]);
            if (v > max)
            {
                x = i;
                max = v;
            }
        }

        if (x == ~0u)
            abort();

        vec3 up = vec3(0);
        switch (x)
        {
        case 0:
        case 1:
            up[2] = 1.0f;
            break;
        case 2:
            up[0] = 1.0f;
            break;
        }

        float v = glm::dot(up, plane.normal);
        up = glm::normalize(up + plane.normal * -v);

        vec3 org = plane.normal * plane.Dist();
        vec3 right = glm::cross(up, plane.normal);

        static constexpr float MaxTraceLength = 1.732050807569f * 32768.0f;

        up = up * MaxTraceLength;
        right = right * MaxTraceLength;

        winding.count = 4;
        winding.points[0] = (org - right) + up;
        winding.points[1] = (org + right) + up;
        winding.points[2] = (org + right) - up;
        winding.points[3] = (org - right) - up;
    }

    Winding* ClipWinding(const Plane& split, Winding& inWinding, Winding& scratchWinding)
    {
        static constexpr int SIDE_FRONT = 0;
        static constexpr int SIDE_BACK = 1;
        static constexpr int SIDE_ON = 2;

        static constexpr float SplitEpsilion = 0.01f;

        float dists[Winding::MaxWindingPoints];
        int sides[Winding::MaxWindingPoints];
        int counts[3] = { 0, 0, 0 };
        for (uint32_t i = 0; i < inWinding.count; i++)
        {
            vec3 point = inWinding.points[i];

            float dot = glm::dot(point, split.normal) - split.Dist();
            dists[i] = dot;

            int side;
            if (dot > SplitEpsilion)
                side = SIDE_FRONT;
            else if (dot < -SplitEpsilion)
                side = SIDE_BACK;
            else
                side = SIDE_ON;
            sides[i] = side;

            counts[side]++;
        }
        sides[inWinding.count] = sides[0];
        dists[inWinding.count] = dists[0];

        if (!counts[SIDE_FRONT] && !counts[SIDE_BACK])
            return &inWinding;

        if (!counts[SIDE_FRONT])
            return nullptr;

        if (!counts[SIDE_BACK])
            return &inWinding;

        uint32_t maxPoints = inWinding.count + 4;
        assert(Winding::MaxWindingPoints >= maxPoints);

        uint32_t numPoints = 0;
        for (uint32_t i = 0; i < inWinding.count; i++)
        {
            vec3 *p1 = &inWinding.points[i];
            vec3* mid = &scratchWinding.points[numPoints];

            if (sides[i] == SIDE_FRONT || sides[i] == SIDE_ON)
            {
                *mid = *p1;
                numPoints++;
                if (sides[i] == SIDE_ON)
                    continue;
                mid = &scratchWinding.points[numPoints];
            }

            if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
                continue;

            vec3* p2 = i == inWinding.count - 1
                ? &inWinding.points[0]
                : p1 + 1;

            numPoints++;

            float dot = dists[i] / (dists[i] - dists[i + 1]);
            for (uint32_t j = 0; j < 3; j++)
            {
                if (split.normal[j] == 1)
                    (*mid)[j] = split.Dist();
                else if (split.normal[j] == -1)
                    (*mid)[j] = -split.Dist();

                (*mid)[j] = (*p1)[j] + dot * ((*p2)[j] - (*p1)[j]);
            }
        }

        scratchWinding.count = numPoints;
        return &scratchWinding;
    }

    void Solid::UpdateMesh()
    {
        static std::vector<uint32_t> shouldUse;
        static std::unordered_set<AssetID> uniqueMaterials;

        BrushGPUAllocator& a = *Chisel.brushAllocator;

        // TODO: Avoid clearing meshes out every time.
        for (auto& mesh : m_meshes)
        {
            if (mesh.alloc)
            {
                a.free(*mesh.alloc);
                mesh.alloc = std::nullopt;
            }
        }

        uniqueMaterials.clear();
        uniqueMaterials.reserve(m_sides.size());
        m_meshes.clear();
        m_meshes.reserve(m_sides.size());
        m_faces.clear();
        m_faces.reserve(m_sides.size());
        shouldUse.clear();
        shouldUse.resize(m_sides.size());

        for (uint32_t i = 0; i < m_sides.size(); i++)
        {
            AssetID id = InvalidAssetID;
            if (m_sides[i].material)
                id = m_sides[i].material->id;
            uniqueMaterials.insert(id);

            glm::vec3 normal0 = m_sides[i].plane.normal;
            float dist0 = m_sides[i].plane.Dist();
            if (normal0 == glm::vec3(0.0f))
            {
                shouldUse[i / 32u] &= ~(1u << (i % 32));
                continue;
            }

            shouldUse[i / 32u] |= 1u << (i % 32);
            for (uint32_t j = 0; j < i; j++)
            {
                glm::vec3 normal1 = m_sides[j].plane.normal;
                float dist1 = m_sides[j].plane.Dist();

                if (glm::dot(normal0, normal1) > 0.999f && (fabsf(dist0 - dist1) < 0.01f))
                {
                    shouldUse[j / 32u] &= ~(1u << (j % 32));
                    break;
                }
            }
        }
        m_meshes.resize(uniqueMaterials.size());

        // Convert from sides as planes to faces.
        for (uint32_t i = 0; i < m_sides.size(); i++)
        {
            for (uint32_t mask = shouldUse[i]; mask; mask &= mask - 1)
            {
                uint32_t sideIdx = i * 32 + bit::tzcnt(mask);

                Side& side = m_sides[sideIdx];

                Winding scratchWindings[2];
                auto* currentWinding = &scratchWindings[0];

                CreateWindingFromPlane(side.plane, *currentWinding);
                for (uint32_t j = 0; j < m_sides.size() && currentWinding; j++)
                {
                    if (j != sideIdx)
                    {
                        Plane clipPlane = Plane(-m_sides[j].plane.normal, -m_sides[j].plane.offset);

                        currentWinding = ClipWinding(clipPlane, *currentWinding, currentWinding == &scratchWindings[0] ? scratchWindings[1] : scratchWindings[0]);
                    }
                }

                if (currentWinding)
                {
                    // If a point in the winding is close enough to an integer coordinate,
                    // treat it as being at that coordinate.
                    // This matches Hammer's and VBSP's behaviour to combat imprecisions.
                    for (uint32_t j = 0; j < currentWinding->count; j++)
                    {
                        vec3& point = currentWinding->points[j];
                        for (uint32_t k = 0; k < 3; k++)
                        {
                            static constexpr float ROUND_VERTEX_EPSILON = 0.01f;
                            float val     = point[k];
                            float rounded = round(val);
                            if (math::CloseEnough(val, rounded, ROUND_VERTEX_EPSILON))
                                point[k] = rounded;
                        }
                    }

#if 0
                    // Remove duplicate points.
                    for (uint32_t i = 0; i < currentWinding->count; i++)
                    {
                        for (uint32_t j = i + 1; j < currentWinding->count; j++)
                        {
                            static constexpr float MIN_EDGE_LENGTH_EPSILON = 0.1f;
                            vec3 edge = currentWinding->points[i] - currentWinding->points[j];
                            if (glm::length(edge) < MIN_EDGE_LENGTH_EPSILON)
                            {
                                if (j + 1 < currentWinding->count)
                                {
                                    std::memmove(&(currentWinding->points[j]), &(currentWinding->points[j + 1]), (currentWinding->count - (j + 1)) * sizeof(currentWinding[0]));
                                    currentWinding->count = currentWinding->count - 1;
                                }
                            }
                        }
                    }
#endif
                    
                    m_faces.emplace_back(&side, std::vector<vec3>(currentWinding->points, currentWinding->points + currentWinding->count));
                }
            }
        }

        m_bounds = std::nullopt;
        // Create mesh from faces
        for (auto& face : m_faces)
        {
            uint32_t numVertices = face.points.size();
            if (numVertices < 3)
                continue;
            uint32_t numIndices = (numVertices - 2) * 3;

            AssetID id = InvalidAssetID;
            if (face.side->material)
                id = face.side->material->id;

            uint32_t meshIdx = std::distance(uniqueMaterials.begin(), uniqueMaterials.find(id));
            auto& mesh = m_meshes[meshIdx];
            mesh.material = face.side->material;
            mesh.brush = this;
            uint32_t startingVertex = mesh.vertices.size();
            uint32_t startingIndex = mesh.indices.size();
            mesh.vertices.reserve(startingVertex + numVertices);
            mesh.indices.reserve(startingIndex + numIndices);
            for (uint32_t i = 0; i < numVertices; i++)
            {
                vec3 pos = face.points[i];

                float mappingWidth = 32.0f;
                float mappingHeight = 32.0f;
                if (face.side->material != nullptr && face.side->material->baseTexture != nullptr && face.side->material->baseTexture->texture != nullptr)
                {
                    D3D11_TEXTURE2D_DESC desc;
                    face.side->material->baseTexture->texture->GetDesc(&desc);

                    mappingWidth = float(desc.Width);
                    mappingHeight = float(desc.Height);
                }

                float u = glm::dot(glm::vec3(face.side->textureAxes[0].xyz), glm::vec3(pos)) / face.side->scale[0] + face.side->textureAxes[0].w;
                float v = glm::dot(glm::vec3(face.side->textureAxes[1].xyz), glm::vec3(pos)) / face.side->scale[1] + face.side->textureAxes[1].w;

                u = mappingWidth ? u / float(mappingWidth) : 0.0f;
                v = mappingHeight ? v / float(mappingHeight) : 0.0f;

                mesh.vertices.emplace_back(pos, face.side->plane.normal, glm::vec2(u, v));
                m_bounds = m_bounds
                    ? AABB::Extend(*m_bounds, pos)
                    : AABB{ pos, pos };
            }
            // Naiive fan-ing.
            // Should move to delaugney potentially.
            // Need to consider perf impact of that though compared to simple approach.
            const uint32_t numPolygons = numIndices / 3;
            for (uint32_t i = 0; i < numPolygons; i++)
            {
                mesh.indices.emplace_back(startingVertex + i + 2);
                mesh.indices.emplace_back(startingVertex + i + 1);
                mesh.indices.emplace_back(startingVertex);
            }
        }

        // Upload all meshes after they're complete
        a.open();
        for (auto& mesh : m_meshes)
        {
            uint32_t verticesSize = sizeof(VertexSolid) * mesh.vertices.size();
            uint32_t indicesSize = sizeof(uint32_t) * mesh.indices.size();
            mesh.alloc = a.alloc(verticesSize + indicesSize);
            // Store vertices then indices.
            memcpy(&a.data()[mesh.alloc->offset + 0],            mesh.vertices.data(), verticesSize);
            memcpy(&a.data()[mesh.alloc->offset + verticesSize], mesh.indices.data(),  indicesSize);
        }
        a.close();
    }

    void Solid::Transform(const mat4x4& _matrix)
    {
        for (auto& side : m_sides)
            side.plane = side.plane.Transformed(_matrix);

        for (auto& side : m_sides)
        {
            mat4x4 trans = _matrix;

            bool locking = trans_texture_lock;
            bool scaleLocking = trans_texture_scale_lock;

            vec3 delta = trans[3].xyz;
            trans[3].xyz = glm::vec3(0.0f, 0.0f, 0.0f);
                
            bool moving = glm::length2(delta) > 0.00001f;

            if (trans == glm::identity<glm::mat4x4>())
            {
                if (moving && locking)
                {
                    side.textureAxes[0][3] -= glm::dot(delta, vec3(side.textureAxes[0].xyz)) / side.scale[0];
                    side.textureAxes[1][3] -= glm::dot(delta, vec3(side.textureAxes[1].xyz)) / side.scale[1];
                }

                continue;
            }

            vec3 u = side.textureAxes[0];
            vec3 v = side.textureAxes[1];

            float scaleU = glm::length(u);
            float scaleV = glm::length(v);
            if (scaleU <= 0.0f) scaleU = 1.0f;
            if (scaleV <= 0.0f) scaleV = 1.0f;

            u = glm::mat3(trans) * u;
            v = glm::mat3(trans) * v;

            scaleU = glm::length(u) / scaleU;
            scaleV = glm::length(v) / scaleV;
            if (scaleU <= 0.0f) scaleU = 1.0f;
            if (scaleV <= 0.0f) scaleV = 1.0f;

            bool uvAxisSameScale = math::CloseEnough(scaleU, 1.0f, 0.0001f) && math::CloseEnough(scaleV, 1.0f, 0.0001f);
            bool uvAxisPerpendicular = math::CloseEnough(glm::dot(u, v), 0.0f, 0.0025f);

            if (locking && uvAxisPerpendicular)
            {
                side.textureAxes[0].xyz = u / scaleU;
                side.textureAxes[1].xyz = v / scaleV;
            }

            if (uvAxisSameScale)
            {
                if (!locking)
                {
                    // TODO: re-init
                }
            }
            else
            {
                if (scaleLocking)
                {
                    side.scale[0] *= scaleU;
                    side.scale[1] *= scaleV;
                }
            }

            if (moving && locking)
            {
                side.textureAxes[0][3] -= glm::dot(delta, vec3(side.textureAxes[0].xyz)) / side.scale[0];
                side.textureAxes[1][3] -= glm::dot(delta, vec3(side.textureAxes[1].xyz)) / side.scale[1];
            }
        }

        UpdateMesh();
    }

    void Solid::AlignToGrid(vec3 gridSize)
    {
        auto bounds = GetBounds();
        if (!bounds)
            return;

        // Align this brush to mins of bounding box
        // like Hammer does (for when moving brushes)
        // on-grid.
        vec3 ref  = bounds->min;
        vec3 snap = math::Snap(bounds->min, gridSize);
        Transform(glm::translate(glm::identity<mat4x4>(), snap - ref));
    }

    Selectable* Solid::ResolveSelectable()
    {
        if (Chisel.selectMode == SelectMode::Solids)
            return this;

        // Groups/Objects
        if (m_parent->IsMap())
            return this;

        return m_parent;
    }

    bool Solid::IsSelected() const
    {
        return Atom::IsSelected() || m_parent->IsSelected();
    }

    std::vector<Side> CreateCubeBrush(vec3 size, const mat4x4& transform)
    {
        static const std::array<Plane, 6> kUnitCubePlanes =
        {
            Plane(vec3(+1,0,0), vec3(+1,0,0)),
            Plane(vec3(-1,0,0), vec3(-1,0,0)),
            Plane(vec3(0,+1,0), vec3(0,+1,0)),
            Plane(vec3(0,-1,0), vec3(0,-1,0)),
            Plane(vec3(0,0,+1), vec3(0,0,+1)),
            Plane(vec3(0,0,-1), vec3(0,0,-1))
        };

        std::vector<Side> sides;
        sides.resize(6);
        for (size_t i = 0; i < 6; i++)
        {
            sides[i].plane = kUnitCubePlanes[i].Transformed(glm::scale(transform, size));

            InitSideData(sides[i]);
        }
        
        return sides;
    }

}
