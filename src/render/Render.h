#pragma once

#include "platform/Window.h"
#include "math/Math.h"
#include "math/Color.h"
#include "render/TextureFormat.h"
#include "render/BlendMode.h"

#include <functional>
#include <string_view>

#include "D3D11Include.h"

namespace chisel::render
{
    struct Texture
    {
        Com<ID3D11Texture2D>          texture;
        Com<ID3D11ShaderResourceView> srv;

        operator bool() const { return texture != nullptr; }
        virtual uint2 GetSize()
        {
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            return uint2(desc.Width, desc.Height);
        }
    };

    struct RenderTarget : public Texture
    {
        Com<ID3D11RenderTargetView>   rtv;
    };

    struct Shader
    {
        Com<ID3D11VertexShader> vs;
        Com<ID3D11PixelShader>  ps;
        Com<ID3D11InputLayout>  inputLayout;

        Shader() {}
        Shader(Com<ID3D11Device1> device, std::string_view name);
    };

    struct RenderContext
    {
        void Init(Window* window);
        void Shutdown();

        void BeginFrame();
        void EndFrame();

        void SetShader(const Shader& shader);

        Com<ID3D11Device1> device;
        Com<ID3D11DeviceContext1> ctx;
        Com<IDXGISwapChain> swapchain;

        RenderTarget backbuffer;
    };
}
