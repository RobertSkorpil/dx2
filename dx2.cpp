#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <comdef.h>
#include <atlbase.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <random>

#include <Eigen/Dense>


LRESULT windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_CLOSE:
		PostMessage(hwnd, WM_QUIT, 0, 0);
		return 0;
	case WM_TIMER:
		SetTimer(hwnd, 1, 10, nullptr);
		return 0;
	default:
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}
}

std::vector<uint8_t> read_file(std::wstring_view fileNameView)
{
	std::wstring fileName{ fileNameView };
	auto file = CreateFile(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		auto error = GetLastError();
		char buf[256];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			buf, (sizeof(buf) / sizeof(wchar_t)), NULL);
		throw std::runtime_error(buf);
	}
	std::vector<uint8_t> data(GetFileSize(file, nullptr));
	DWORD bytesRead;
	ReadFile(file, data.data(), data.size(), &bytesRead, nullptr);
	data.resize(bytesRead);
	CloseHandle(file);
	return data;
}

struct vertex_shader
{
	std::vector<uint8_t> data;
	CComPtr<ID3D11VertexShader> shader;
};
	
vertex_shader create_vertex_shader(std::wstring_view file, ID3D11Device *device)
{
	vertex_shader result;
	result.data = read_file(file);
	device->CreateVertexShader(result.data.data(), result.data.size(), nullptr, &result.shader);
	return result;
}

struct pixel_shader
{
	std::vector<uint8_t> data;
	CComPtr<ID3D11PixelShader> shader;
};

pixel_shader create_pixel_shader(std::wstring_view file, ID3D11Device *device)
{
	pixel_shader result;
	result.data = read_file(file);
	device->CreatePixelShader(result.data.data(), result.data.size(), nullptr, &result.shader);
	return result;
}

using vec3 = Eigen::Vector3f;
using vec4 = Eigen::Vector4f;
using mat4 = Eigen::Matrix4f;

struct vertex_data
{
	vec4 position;
	vec4 normal;
	vec4 color;
};

struct d3d_device
{
	CComPtr<ID3D11Device> baseDevice;
	CComPtr<ID3D11DeviceContext> baseDeviceContext;
	CComQIPtr<ID3D11Device1> device;
	CComQIPtr<ID3D11DeviceContext1> deviceContext;
	CComQIPtr<IDXGIDevice1> dxgiDevice;
	CComPtr<IDXGIAdapter> dxgiAdapter;
	CComPtr<IDXGIFactory2> dxgiFactory;

	d3d_device()
	{
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &featureLevel, 1, D3D11_SDK_VERSION, &baseDevice, nullptr, &baseDeviceContext);
		device = baseDevice;
		deviceContext = baseDeviceContext;
		dxgiDevice = device;
		dxgiDevice->GetAdapter(&dxgiAdapter);
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
	}

	CComPtr<IDXGISwapChain1> create_swap_chain(HWND hwnd)
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

		CComPtr<IDXGISwapChain1> swapChain;
		dxgiFactory->CreateSwapChainForHwnd(device, hwnd, &swapChainDesc, nullptr, nullptr, &swapChain);

		return swapChain;
	}

	ID3D11Device1* operator ->() 
	{
		return device;
	}
};

struct mesh
{
	d3d_device& m_device;
	std::vector<vertex_data> m_vertices;
	std::vector<uint32_t> m_indices;
	CComPtr<ID3D11Buffer> m_vertexBuffer;
	CComPtr<ID3D11Buffer> m_indexBuffer;
	bool m_generated = false;

	virtual void generate() = 0;

	virtual D3D_PRIMITIVE_TOPOLOGY topology() const {
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	mesh(d3d_device &device)
		: m_device{ device }
	{
	}

	mesh(mesh&&) = default;
	mesh(const mesh&) = delete;

	void draw()
	{
		if (!m_generated)
		{
			generate();

			D3D11_BUFFER_DESC vertexBufferDesc{};
			vertexBufferDesc.ByteWidth = m_vertices.size() * sizeof(m_vertices[0]);
			vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vertexData = { m_vertices.data() };
			m_device->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer);

			D3D11_BUFFER_DESC indexBufferDesc{};
			indexBufferDesc.ByteWidth = m_indices.size() * sizeof(m_indices[0]);
			indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA indexData = { m_indices.data() };
			m_device->CreateBuffer(&indexBufferDesc, &indexData, &m_indexBuffer);

			m_generated = true;
		}

		auto deviceContext = m_device.deviceContext.p;

		uint32_t stride = sizeof(vertex_data);
		uint32_t offset = 0;

		deviceContext->IASetPrimitiveTopology(topology());
		deviceContext->IASetVertexBuffers(0, 1, &m_vertexBuffer.p, &stride, &offset);
		deviceContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

		deviceContext->DrawIndexed(m_indices.size(), 0, 0);
	}
};

/*
struct circle_mesh : mesh
{
	using mesh::mesh;

	void generate() override
	{
		const int n = 12;
		m_vertices.emplace_back(0, 0, 0, 1);
		for (int i = 0; i < n; ++i)
		{
			double a = 2 * 3.1415927 / double(n) * double(i);
			m_vertices.emplace_back(sin(a), cos(a), 0, 1);

			if (i & 1)
			{
				m_indices.push_back(0);
				m_indices.push_back(i + 1);
				m_indices.push_back((i + 2) % n);
			}
		}	
	}
};	   */

struct cube_mesh : mesh
{
	vec4 m_point;
	vec4 m_color;
	float m_scale;
	D3D_PRIMITIVE_TOPOLOGY topology() const { return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; }

	cube_mesh(d3d_device& device, vec4 p, float scale, vec4 color = vec4{ 0.8f, 0.2f, 0.6f, 1.f })
		: mesh{ device }, m_point{ p }, m_color{ color }, m_scale{ scale }
	{
	}

	
	void generate() override
	{
		auto rot1 = [](float alpha) {
			mat4 rot;
			rot <<
				cosf(alpha), -sinf(alpha), 0.f, 0.f,
				sinf(alpha), cosf(alpha), 0.f, 0.f,
				0.f, 0.f, 1.f, 0.f,
				0.f, 0.f, 0.f, 1.f;
			return rot;
		};
		auto rot2 = [](float alpha) {
			mat4 rot;
			rot <<
				cosf(alpha), 0.f, -sinf(alpha), 0.f,
				0.f, 1.f, 0.f, 0.f,
				sinf(alpha), 0.f, cosf(alpha), 0.f,
				0.f, 0.f, 0.f, 1.f;
			return rot;
		};

		auto norm = [](vec4 v)
		{
			vec4 origin = { 0.f, 0.f, 0.f, 1.f };
			return (v - origin).normalized() + origin;
		};

		vec4 normal{ m_scale, 0.f, 0.f, 1.f };
		vec4 dir0{ 0.f, m_scale, m_scale, 0.f };
		vec4 dir1{ 0.f, m_scale, -m_scale, 0.f };
		vec4 dir2{ 0.f, -m_scale, -m_scale, 0.f };
		vec4 dir3{ 0.f, -m_scale, m_scale, 0.f };

		for (int i = 0; i < 6; ++i)
		{
			mat4 rot;
			if (i < 4)
				rot = rot1(i / 4.0f * 2 * 3.1415927);
			else
				rot = rot2((i - 4.5) * 3.1415927);

			auto n = rot * normal;
			auto d0 = rot * dir0 + n;
			auto d1 = rot * dir1 + n;
			auto d2 = rot * dir2 + n;
			auto d3 = rot * dir3 + n;

			auto id0 = m_vertices.size();
			m_vertices.push_back({ d0 + m_point, norm(n), m_color });
			auto id1 = m_vertices.size();
			m_vertices.push_back({ d1 + m_point, norm(n), m_color });
			auto id2 = m_vertices.size();
			m_vertices.push_back({ d2 + m_point, norm(n), m_color });
			auto id3 = m_vertices.size();
			m_vertices.push_back({ d3 + m_point, norm(n), m_color });

			m_indices.push_back(id1);
			m_indices.push_back(id0);
			m_indices.push_back(id2);

			m_indices.push_back(id2);
			m_indices.push_back(id0);
			m_indices.push_back(id3);
		}
	}
};

struct camera
{
	vec4 position{ 0.f, 0.f, 0.f, 1.f };
	vec4 look_at{ 0.f, 1.f, 1.f, 1.f };
	vec4 up{ 1.0f, 0.0f, 0.0f, 0.0f };

	mat4 matrix()
	{
		vec4 origin{ 0.0f, 0.0f, 0.0f, 1.0f };
		auto r = origin - position;
		mat4 P, R;
		P <<
			0, 0, 0, 0,
			0, 0, 0, 0,
			0, 0, 0, 0,
			r.x(), r.y(), r.z(), 1;

		vec3 forward{ 0.f, 0.f, 1.f };
		vec3 eye = (look_at.hnormalized() - position.hnormalized()).normalized();
		auto rot_axis = eye.cross(forward).normalized();
		auto& u = rot_axis;
		auto rot_angle = acosf(eye.dot(forward));
		auto cr = cosf(rot_angle);
		auto sr = sinf(rot_angle);
		auto rot_quat = vec4{ eye.dot(forward), rot_axis.x(), rot_axis.y(), rot_axis.z(), };
		R <<
			cr + u.x() * u.x() * (1 - cr), u.x()* u.y()* (1 - cr) - u.z() * sr, u.x()* u.z()* (1 - cr) + u.y() * sr, 0,
			u.y()* u.x()* (1 - cr) + u.z() * sr, cr + u.y() * u.y() * (1 - cr), u.y()* u.z()* (1 - cr) - u.x() * sr, 0,
			u.z()* u.x()* (1 - cr) - u.y() * sr, u.z()* u.y()* (1 - cr) + u.x() * sr, cr + u.z() * u.z() * (1 - cr), 0,
			0, 0, 0, 1;

		auto M = R * P;

		return M;
	}
};

struct constants
{
	mat4 cam;
	vec4 light;
};

size_t round_up(size_t size)
{
	return (size + 15) / 16 * 16;
}

int main()
{
	camera cam;
	cam.matrix();
	return 0;

	(void)CoInitialize(nullptr);

	WNDCLASS wndClass{};
	wndClass.lpfnWndProc = windowProc;
	wndClass.lpszClassName = L"DX2 Window";
	RegisterClass(&wndClass);
	HWND hwnd = CreateWindowEx(0, wndClass.lpszClassName, L"DX2", WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0, 0, 600, 600, nullptr, nullptr, nullptr, nullptr);
	ShowWindow(hwnd, SW_NORMAL);

	SetTimer(hwnd, 1, 10, nullptr);

	d3d_device device;

	auto swapChain = device.create_swap_chain(hwnd);

	D3D11_BUFFER_DESC constantBufferDesc{};
	constantBufferDesc.ByteWidth = round_up(sizeof(constantBufferDesc));
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	CComPtr<ID3D11Buffer> constantBuffer;
	device->CreateBuffer(&constantBufferDesc, nullptr, &constantBuffer);

	CComPtr<ID3D11Texture2D> frameBuffer;
	swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frameBuffer));

	CComPtr<ID3D11RenderTargetView> frameBufferView;
	device->CreateRenderTargetView(frameBuffer, nullptr, &frameBufferView);

	D3D11_TEXTURE2D_DESC depthBufferDesc;
	frameBuffer->GetDesc(&depthBufferDesc);
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	CComPtr<ID3D11Texture2D> depthBuffer;
	device->CreateTexture2D(&depthBufferDesc, nullptr, &depthBuffer);

	CComPtr<ID3D11DepthStencilView> depthBufferView;
	device->CreateDepthStencilView(depthBuffer, nullptr, &depthBufferView);

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
	CComPtr<ID3D11DepthStencilState> depthStencilState;
	device->CreateDepthStencilState(&depthStencilDesc, &depthStencilState);

	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	CComPtr<ID3D11SamplerState> samplerState;
	device->CreateSamplerState(&samplerDesc, &samplerState);

	D3D11_RASTERIZER_DESC1 rasterizedDesc{};
	rasterizedDesc.FillMode = D3D11_FILL_SOLID;
	rasterizedDesc.CullMode = D3D11_CULL_BACK;
	CComPtr<ID3D11RasterizerState1> rasterizerState;
	device->CreateRasterizerState1(&rasterizedDesc, &rasterizerState);

	auto v_shader = create_vertex_shader(L"VertexShader.cso", device.device);
	auto p_shader = create_pixel_shader(L"PixelShader.cso", device.device);

	std::array<D3D11_INPUT_ELEMENT_DESC, 3> inputElementDesc
	{
		D3D11_INPUT_ELEMENT_DESC { "SV_Position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		D3D11_INPUT_ELEMENT_DESC { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		D3D11_INPUT_ELEMENT_DESC { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	CComPtr<ID3D11InputLayout> inputLayout;
	device->CreateInputLayout(inputElementDesc.data(), inputElementDesc.size(), v_shader.data.data(), v_shader.data.size(), &inputLayout);

	D3D11_VIEWPORT viewport{ (depthBufferDesc.Width - depthBufferDesc.Height) / 2.0, 0, depthBufferDesc.Height, depthBufferDesc.Height, 0, 1};

	std::vector<cube_mesh> cubes;

	int n = 7;
	for (int k = 0; k < n; ++k)
	for (int i = 0; i < n; ++i)
	{
		cube_mesh c0{ device, vec4 { -0.5f + i / float(n), 0.f, -0.5f + k / float(n), 1 }, .4f / n };
		cubes.push_back(std::move(c0));
	}
//	cube_mesh c0{ device, vec4 { 0.f, 0.f, 0.f, 1.f }, 0.4f };
//	cubes.push_back(std::move(c0));

	double alpha = 0;
	auto rot1 = [](float alpha) {
		mat4 rot;
		rot <<
			cosf(alpha), -sinf(alpha), 0.f, 0.f,
			sinf(alpha), cosf(alpha), 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f;
		return rot;
	};
	for (;;)
	{
		MSG msg;
		WaitMessage();
		if (GetMessage(&msg, hwnd, 0, 0))
			DispatchMessage(&msg);
		else
			break;

		auto deviceContext = device.deviceContext.p;

		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		auto cnst = reinterpret_cast<constants*>(mappedSubresource.pData);
		cnst->light = rot1(alpha) * vec4 { 1.f, 0.f, 0.f, 0.f };
		deviceContext->Unmap(constantBuffer, 0);

		deviceContext->ClearRenderTargetView(frameBufferView, (std::array<float, 4> { 0.1, 0.2, 0.2, 1.0 }).data());
		deviceContext->ClearDepthStencilView(depthBufferView, D3D11_CLEAR_DEPTH, 1.f, 0);

		deviceContext->IASetInputLayout(inputLayout);

		deviceContext->VSSetShader(v_shader.shader, nullptr, 0);
		deviceContext->VSSetConstantBuffers(0, 1, &constantBuffer.p);

		deviceContext->RSSetViewports(1, &viewport);
		deviceContext->RSSetState(rasterizerState);

		deviceContext->PSSetShader(p_shader.shader, nullptr, 0);
		deviceContext->PSSetSamplers(0, 1, &samplerState.p);
		deviceContext->PSSetConstantBuffers(0, 1, &constantBuffer.p);

		deviceContext->OMSetRenderTargets(1, &frameBufferView.p, depthBufferView);
		deviceContext->OMSetDepthStencilState(depthStencilState, 0);
		deviceContext->OMSetBlendState(nullptr, nullptr, -1);

		for(auto &cube : cubes)
			cube.draw();

		swapChain->Present(1, 0);

		alpha += 0.01;
	}

}

