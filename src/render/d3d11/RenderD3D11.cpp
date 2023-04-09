#include "render/Render.h"

#include <imgui.h>
#include "gui/Common.h"
#include "gui/impl/imgui_impl_dx11.h"
#include "common/Filesystem.h"
#include "core/Mesh.h"
#include "console/Console.h"
#include "render/CBuffers.h"

namespace chisel::render
{
    void RenderContext::Init(Window* window)
    {
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_1;
        DXGI_SWAP_CHAIN_DESC swapchainDesc =
        {
            .BufferDesc =
            {
                .Width  = window->GetSize().x,
                .Height = window->GetSize().y,
                .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            },
            .SampleDesc =
            {
                .Count   = 1,
                .Quality = 0,
            },
            .BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 3,
            .OutputWindow = (HWND)window->GetNativeWindow(),
            .Windowed = TRUE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        Com<ID3D11Device> localDevice;
        Com<IDXGISwapChain> localSwapchain;

        HRESULT hr;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            &level,
            1,
            D3D11_SDK_VERSION,
            &swapchainDesc,
            &localSwapchain,
            &localDevice,
            nullptr,
            nullptr);

        if (FAILED(hr))
            return;

        localDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device));
        localSwapchain->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&swapchain));
        device->GetImmediateContext1(&ctx);

        swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer.texture));
        device->CreateRenderTargetView(backbuffer.texture.ptr(), nullptr, &backbuffer.rtv);

        ImGui::CreateContext();

        GUI::Setup();

        ImGui_ImplDX11_Init(device.ptr(), ctx.ptr());
        ImGui_ImplDX11_NewFrame();

        // Global CBuffers
        D3D11_BUFFER_DESC bufferDesc =
        {
            .ByteWidth = sizeof(cbuffers::CameraState),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        device->CreateBuffer(&bufferDesc, nullptr, &cbuffers.camera);

        // Initial sampler... May want to change this down the line
        D3D11_SAMPLER_DESC samplerDesc =
        {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 0,
            .ComparisonFunc = D3D11_COMPARISON_ALWAYS,
            .MinLOD = 0.0f,
            .MaxLOD = FLT_MAX,
        };
        device->CreateSamplerState(&samplerDesc, &sampler);
        ctx->PSSetSamplers(0, 1, &sampler);

        D3D11_RASTERIZER_DESC desc =
        {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .FrontCounterClockwise = TRUE,
            .DepthClipEnable = TRUE,
        };
        device->CreateRasterizerState(&desc, &rsState);
        ctx->RSSetState(rsState.ptr());
    }

    void RenderContext::Shutdown()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();

        backbuffer.texture = nullptr;
        backbuffer.rtv = nullptr;
        swapchain = nullptr;
        ctx = nullptr;
        device = nullptr;
    }

    void RenderContext::SetShader(const Shader& shader)
    {
        if (shader.inputLayout != nullptr)
            ctx->IASetInputLayout(shader.inputLayout.ptr());
        if (shader.vs != nullptr)
            ctx->VSSetShader(shader.vs.ptr(), nullptr, 0);
        if (shader.ps != nullptr)
            ctx->PSSetShader(shader.ps.ptr(), nullptr, 0);
    }

    void RenderContext::BeginFrame()
    {
        D3D11_TEXTURE2D_DESC desc;
        backbuffer.texture->GetDesc(&desc);

        ctx->OMSetRenderTargets(1, &backbuffer.rtv, nullptr);
        D3D11_VIEWPORT viewport =
        {
            .TopLeftX = 0,
            .TopLeftY = 0,
            .Width    = float(desc.Width),
            .Height   = float(desc.Height),
        };
        ctx->RSSetViewports(1, &viewport);

        float debugColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        ctx->ClearRenderTargetView(backbuffer.rtv.ptr(), debugColor);
    }

    void RenderContext::EndFrame()
    {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        swapchain->Present(0, 0);
    }

    void RenderContext::DrawMesh(Mesh* mesh)
    {
        if (!mesh->uploaded) {
            for (auto& group : mesh->groups) {
                D3D11_BUFFER_DESC desc = {};
                desc.ByteWidth  = group.vertices.Size();
                desc.Usage      = D3D11_USAGE_DEFAULT;
                desc.BindFlags  = D3D11_BIND_VERTEX_BUFFER;

                D3D11_SUBRESOURCE_DATA data = {};
                data.pSysMem    = group.vertices.pointer;

                HRESULT hr = device->CreateBuffer(&desc, &data, (ID3D11Buffer**)&group.vertices.handle);
                if (FAILED(hr)) {
                    Console.Error("[D3D11] Failed to create vertex buffer");
                    return;
                }
            }
        }
        uint strides[] = {(uint)mesh->groups[0].vertices.Stride()};
        uint offsets[] = {0};
        ctx->IASetVertexBuffers(0, 1, (ID3D11Buffer**)&mesh->groups[0].vertices.handle, strides, offsets);
        ctx->Draw(mesh->groups[0].vertices.count, 0);
    }

    Shader::Shader(ID3D11Device1* device, std::span<D3D11_INPUT_ELEMENT_DESC const> ia, std::string_view name)
    {
        fs::Path path = fs::Path("core/shaders") / name;
        path.setExt(".vsc");
        auto vsFile = fs::readFile(path);
        if (!vsFile) {
            Console.Error("[D3D11] Failed to find vertex shader 'shaders/{}.vsc'", name);
            return;
        }

        path.setExt(".psc");
        auto psFile = fs::readFile(path);
        if (!psFile) {
            Console.Error("[D3D11] Failed to find pixel shader 'shaders/{}.psc'", name);
            return;
        }

        HRESULT hr = device->CreateVertexShader(vsFile->data(), vsFile->size(), NULL, &vs);
        if (FAILED(hr)) {
            Console.Error("[D3D11] Failed to create vertex shader '{}'", name);
            return;
        }

        hr = device->CreatePixelShader(psFile->data(), psFile->size(), NULL, &ps);
        if (FAILED(hr)) {
            Console.Error("[D3D11] Failed to create pixel shader '{}'", name);
            return;
        }

        hr = device->CreateInputLayout(ia.data(), ia.size(), vsFile->data(), vsFile->size(), &inputLayout);
        if (FAILED(hr)) {
            Console.Error("[D3D11] Failed to create input layout for shader '{}'", name);
            return;
        }
    }

}
