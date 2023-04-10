#pragma once

#include "chisel/Tools.h"
#include "assets/Assets.h"
#include "glm/ext/matrix_transform.hpp"
#include "render/Render.h"
#include "render/CBuffers.h"
#include "core/Mesh.h"
#include "math/Math.h"
#include "core/Primitives.h"
#include "Selection.h"

#include <vector>
#include <glm/gtx/vector_angle.hpp>

namespace chisel
{
    inline struct Gizmos
    {
        static inline Texture* icnObsolete;
        static inline Texture* icnHandle;
        static inline render::Shader sh_Sprite;

        void DrawIcon(vec3 pos, SelectionID selection, Texture* icon, vec4 color = vec4(1.0f), vec3 size = vec3(32.0f), bool test = true)
        {
            auto& r = Tools.rctx;
            r.SetShader(sh_Sprite);
            r.ctx->PSSetShaderResources(0, 1, &icon->srvSRGB);
            if (selection == 0)
                r.SetBlendState(render::BlendFuncs::AlphaFirstWriteOnly);
            else
                r.SetBlendState(render::BlendFuncs::AlphaFirstOneRest);
            r.ctx->OMSetDepthStencilState(test ? r.Depth.NoWrite.ptr() : r.Depth.Ignore.ptr(), 0);

            cbuffers::ObjectState data;
            data.model = glm::scale(glm::translate(mat4x4(1.0f), pos), size);
            data.color = color;
            data.id = selection;
            r.UpdateDynamicBuffer(r.cbuffers.object.ptr(), data);
            r.ctx->VSSetConstantBuffers1(1, 1, &r.cbuffers.object, nullptr, nullptr);
            r.ctx->PSSetConstantBuffers1(1, 1, &r.cbuffers.object, nullptr, nullptr);
            
            uint stride = sizeof(Primitives::Vertex);
            uint offset = 0;
            r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            r.ctx->IASetVertexBuffers(0, 1, &Primitives.Quad, &stride, &offset);
            r.ctx->Draw(6, 0);
            r.ctx->OMSetDepthStencilState(r.Depth.Default.ptr(), 0);
        }

        // TODO: These could be batched.
        void DrawLine(vec3 start, vec3 end, Color color = Colors.White)
        {
#if 0
            auto& r = Tools.Render;
            r.SetShader(Tools.sh_Color);
            r.SetUniform("u_color", color);
            mat4x4 mtx = glm::translate(mat4x4(1), start);
            mtx = glm::rotate(mtx, glm::orientedAngle(glm::vec3(1, 0, 0), glm::normalize(end - start), Vectors.Up), glm::vec3(0, 0, 1));
            mtx = glm::scale(mtx, glm::vec3(glm::distance(start, end), 1.0f, 1.0f));
            r.SetTransform(mtx);
            r.SetPrimitiveType(render::PrimitiveType::Lines);
            r.DrawMesh(&Primitives.Line);
#endif
        }

        void Init()
        {
            icnObsolete = Assets.Load<Texture>("textures/ui/obsolete.png");
            icnHandle   = Assets.Load<Texture>("textures/ui/handle.png");
            sh_Sprite   = render::Shader(Tools.rctx.device.ptr(), Primitives::Vertex::Layout, "sprite");
        }
    } Gizmos;
}