#include <Windows.h>
#include <iostream>
#include <wrl/client.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "pass_through.hpp"

using Microsoft::WRL::ComPtr;
namespace Renderer
{
	ComPtr<ID3D11Device> d3d_device;
	ComPtr<ID3D11DeviceContext> d3d_device_context;
	ComPtr<IDXGISwapChain> swap_chain;
	ComPtr<ID2D1Factory> d2d_factory;
	ComPtr<ID2D1RenderTarget> d2d_render_target;

	bool initialize_d3d(HWND hwnd)
	{
		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 2;
		sd.BufferDesc.Width = 0;
		sd.BufferDesc.Height = 0;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hwnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		D3D_FEATURE_LEVEL feature_level{};
		const D3D_FEATURE_LEVEL feature_levels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};

		HRESULT res = D3D11CreateDeviceAndSwapChain(
			nullptr, 
			D3D_DRIVER_TYPE_HARDWARE, 
			nullptr, 
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			feature_levels, 
			std::size(feature_levels), 
			D3D11_SDK_VERSION, 
			&sd, 
			swap_chain.GetAddressOf(),
			d3d_device.GetAddressOf(), 
			&feature_level, 
			d3d_device_context.GetAddressOf());
		if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		{
			res = D3D11CreateDeviceAndSwapChain(
				nullptr, 
				D3D_DRIVER_TYPE_WARP, 
				nullptr, 
				D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				feature_levels, 
				std::size(feature_levels), 
				D3D11_SDK_VERSION, 
				&sd, 
				swap_chain.GetAddressOf(),
				d3d_device.GetAddressOf(), 
				&feature_level, 
				d3d_device_context.GetAddressOf());
		}
		if (res != S_OK)
			return false;
		return true;
	}

	bool initialize_d2d()
	{
		HRESULT res = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory.GetAddressOf());
		if (res != S_OK)
			return false;
		ComPtr<IDXGISurface> dxgi_back_buffer;
		swap_chain->GetBuffer(0, IID_PPV_ARGS(&dxgi_back_buffer));
		D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
		res = d2d_factory->CreateDxgiSurfaceRenderTarget(dxgi_back_buffer.Get(), &props, d2d_render_target.GetAddressOf());
		if (res != S_OK)
			return false;
		return true;
	}

	bool initialize(HWND hwnd)
	{
		if (!initialize_d3d(hwnd))
			return false;
		if (!initialize_d2d())
			return false;
		return true;
	}

	void render()
	{
		d2d_render_target->BeginDraw();
		d2d_render_target->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0.f));
		d2d_render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		// create a brush
		ComPtr<ID2D1SolidColorBrush> brush;
		d2d_render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), brush.GetAddressOf());
		d2d_render_target->DrawLine(D2D1::Point2F(0, 0), D2D1::Point2F(800, 600), brush.Get(), 1.f);
		d2d_render_target->EndDraw();
		swap_chain->Present(1, 0);
	}
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main(int, char**)
{
	// 加载驱动
	if (!PassThrough::Driver::load())
		return 1;

	// 打开设备句柄
	if (!PassThrough::Driver::open_device_handle())
	{
		PassThrough::Driver::unload();
		return 1;
	}

	// 创建窗口
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"passthrough", nullptr };
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowExW(
		WS_EX_TOPMOST/* | WS_EX_TRANSPARENT*/,
		wc.lpszClassName,
		L"passthrough",
		WS_POPUP,
		0,
		0,
		800,
		600,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);

	// 保证hrgn_clip有效
	SetWindowRgn(hwnd, CreateRectRgn(0, 0, 800, 600), TRUE);

	// 初始化PassThrough
	if (!PassThrough::initialize(hwnd) || !PassThrough::start())
	{
		PassThrough::Driver::unload();
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// 显示窗口
	ShowWindow(hwnd, SW_SHOWNA);
	UpdateWindow(hwnd);

	// 窗口透明
	MARGINS margins = { -1,-1,-1,-1 };
	DwmExtendFrameIntoClientArea(hwnd, &margins);
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
	
	// 初始化渲染器
	if (!Renderer::initialize(hwnd))
	{
		PassThrough::stop();
		PassThrough::Driver::unload();
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	bool done = false;
	while (!done)
	{
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;
		Renderer::render();
	}

	PassThrough::stop();
	PassThrough::Driver::unload();
	DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	return 0;
}