#pragma once

#include "math/Math.h"
#include "core/Transform.h"

#include "platform/Window.h"
#include "render/Render.h"

namespace chisel
{
    struct Camera
    {
        // Horizontal field of view in degrees
        float fieldOfView = 90.0f;

        // Reduce or expand FOV based on aspect ratio.
        bool scaleFOV = false;
        // Base aspect ratio for FOV scaling.
        vec2 scaleFOVAspect = vec2(16, 9);

        float near = 0.1f;
        float far  = 16384.f;

        // If not null then camera only renders to this texture
        render::RenderTarget* renderTarget = nullptr;

        float pitch = 0.0f;
        float yaw = 0.0f;
        vec3 position = vec3();
        vec3 up = vec3(0, 0, 1);
        // TODO: Update BGFX_STATE_CULL_CW -> BGFX_STATE_CULL_CCW
        // when we change from rightHanded -> leftHanded.
        bool rightHanded = true;
        vec3 right() const
        {
            vec3 res = glm::cross(forward(), up);
            return rightHanded ? res : -res;
        }
        vec3 forward() const
        {
            // TODO: Genericize the Forward function
            // to account for other Ups.
            const float x = std::cos(pitch) * std::cos(yaw);
            const float y = std::sin(pitch);
            const float z = std::cos(pitch) * std::sin(yaw);

            return vec3(x, z, y);
        }

    public:
        // World to camera matrix
        mat4x4 ViewMatrix()
        {
            if (overrideViewMatrix)
                return view;

            if (rightHanded)
            {
                view = glm::lookAtRH(
                    position,
                    position + forward(),
                    up);
            }
            else
            {
                view = glm::lookAtLH(
                    position,
                    position + forward(),
                    up);
            }

            return view;
        }

        // Set custom world to camera matrix
        void SetViewMatrix(mat4x4& m)
        {
            overrideViewMatrix = true;
            view = m;
        }

        void ResetViewMatrix() {
            overrideViewMatrix = false;
        }

    public:
        float AspectRatio()
        {
            vec2 size;
            if (renderTarget) {
                size = renderTarget->GetSize();
            } else {
                size = Window::main->GetSize();
            }

            return size.x / size.y;
        }

        // Projection matrix
        mat4x4 ProjMatrix()
        {
            if (overrideProjMatrix)
                return proj;

            aspectRatio = AspectRatio();

            float fovX = fieldOfView;
            if (scaleFOV)
                fovX = ScaleFOVByWidthRatio(fovX, aspectRatio, (scaleFOVAspect.x/scaleFOVAspect.y));
            float fovY = CalcVerticalFOV(fovX, aspectRatio);

            if (rightHanded)
                proj = glm::perspectiveRH_ZO(glm::radians(fovY), aspectRatio, near, far);
            else
                proj = glm::perspectiveLH_ZO(glm::radians(fovY), aspectRatio, near, far);

            return proj;
        }

        // Set custom projection matrix
        void SetProjMatrix(mat4x4& m)
        {
            overrideProjMatrix = true;
            view = m;
        }

        void ResetProjMatrix() {
            overrideProjMatrix = false;
        }

    private:
        // Aspect ratio W/H
        float aspectRatio = 16.0f / 9.0f;

        // World to camera matrix
        mat4x4 view;

        // Projection matrix
        mat4x4 proj;

        bool overrideViewMatrix = false;
        bool overrideProjMatrix = false;

    public:
        static float ScaleFOV(float fovDegrees, float ratio) {
            return glm::degrees(atan(tan(glm::radians(fovDegrees) * 0.5f) * ratio)) * 2.0f;
        }

        // Calculate vertical Y-FOV from X-FOV based on W/H aspect ratio
        static float CalcVerticalFOV(float fovx, float aspectRatio) {
            return ScaleFOV(fovx, 1.f/aspectRatio);
        }

        // Calculate horizontal X-FOV from Y-FOV based on W/H aspect ratio
        static float CalcHorizontalFOV(float fovy, float aspectRatio) {
            return ScaleFOV(fovy, aspectRatio);
        }

        // Scale (reduce or expand) FOV based on ratio of two aspect ratios
        // Typically the base aspect is a reference aspect like 16:9 or 4:3
        static float ScaleFOVByWidthRatio(float fov, float aspect, float baseAspect) {
            return ScaleFOV(fov, aspect / baseAspect);
        }
    };
}