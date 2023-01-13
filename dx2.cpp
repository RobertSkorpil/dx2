#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "pmp")
#define NOMINMAX
#define WNI32_LEAN_AND_MEAN
#ifndef M_PI
#define M_PI 3.14159265359
#endif
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
#include <chrono>
#include <iostream>
#include <Eigen/Dense>
#include <pmp/algorithms/Shapes.h>
#include <pmp/algorithms/Remeshing.h>
#include <pmp/algorithms/Triangulation.h>
#include <pmp/algorithms/Normals.h>

using namespace std::literals;

using vec3 = Eigen::Vector3f;
using vec4 = Eigen::Vector4f;
using mat3 = Eigen::Matrix3f;
using mat4 = Eigen::Matrix4f;

struct camera
{
	vec3 position{ 0.f, 0.f, 0.f };
	vec3 look_at{ 0.f, 0.f, 0.f };
	vec3 up{ 0.0f, 1.0f, 0.0f };

	mat4 matrix_world()
	{
		vec3 forward = (look_at - position).normalized();
		vec3 right = forward.cross(up).normalized();

		mat4 M;
		M <<
			right.x(), up.x(), forward.x(), position.x(),
			right.y(), up.y(), forward.y(), position.y(),
			right.z(), up.z(), forward.z(), position.z(),
			0, 0, 0, 1;
		return M.inverse().eval();
	}

	mat4 matrix()
	{
		float f = 100.f;
		float n = .1f;

		mat4 P;

        P <<
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, f / (f - n), n * f / (n - f),
			0, 0, 1, 0
		;

		auto M = P * matrix_world();

		vec4 t = M * vec4{ 0.f, 0.f, 0.f, 1.f };

		return M;
	}
};

struct player
{
	vec3 position{ 0.0f, 0.0f, 0.0f };
	float dir = 0;
	float dir2 = 0;
	float f = 1;

	bool key_up{}, key_down{}, key_left{}, key_right{}, key_pgup{}, key_pgdown{}, key_home{}, key_end{};

	mat3 rot() const
	{
		mat3 r;
		r <<
			cosf(dir), 0, -sinf(dir),
			0, 1, 0,
			sinf(dir), 0, cosf(dir);

		return r;
	}

	mat3 rot2() const
	{
		mat3 r;
		r <<
			1, 0, 0,
			0, cosf(dir2), -sinf(dir2),
			0, sinf(dir2), cosf(dir2);

		return r;
	}

	camera cam() const
	{
		camera cam;
		cam.position = position;
		cam.up = rot() * rot2() * vec3{ 0.f, 1.f, 0.f };
		cam.look_at = cam.position + rot() * rot2() * vec3{ 0.f, 0.f, .1f };

		return cam;
	}

	void step()
	{
		dir += key_left * -.1f + key_right * .1f;
		dir2 += key_home * -.1f + key_end * .1f;
		vec3 velocity = rot() * vec3 { 0.f, key_pgup * .1f + key_pgdown * -.1f, key_up * .1f + key_down * -.1f };
		position += velocity;
	}
} player1;

void handle_key(WPARAM virtual_key, bool up)
{
	switch (virtual_key)
	{
	case VK_UP:
		player1.key_up = !up;
		break;
	case VK_DOWN:
		player1.key_down = !up;
		break;
	case VK_LEFT:
		player1.key_left = !up;
		break;
	case VK_RIGHT:
		player1.key_right = !up;
		break;	
	case VK_PRIOR:
		player1.key_pgup = !up;
		break;	
	case VK_NEXT:
		player1.key_pgdown = !up;
		break;	
	case VK_HOME:
		player1.key_home = !up;
		break;	
	case VK_END:
		player1.key_end = !up;
		break;	
	}
}

int timer = 0;
bool draw = false;
LRESULT windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static int nt = 0;
	switch (msg)
	{
	case WM_CLOSE:
		PostMessage(hwnd, WM_QUIT, 0, 0);
		return 0;
	case WM_TIMER:
		draw = true;
		std::cout << "Timer " << nt++ << "\n";
		return 0;
	case WM_KEYDOWN:
		handle_key(wparam, false);
		return 0;
	case WM_KEYUP:
		handle_key(wparam, true);
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

struct vertex_data
{
	vec4 position{};
	vec4 normal{};
	vec4 tex_coord{};
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
			m_generated = true;
			m_vertexBuffer.Release();
			m_indexBuffer.Release();
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

struct sphere_mesh : mesh
{
	using mesh::mesh;

	std::minstd_rand m_rand;

	size_t add_vertex(vec3 p)	
	{
		p = p.normalized();

		p *= std::normal_distribution{ 1.0, 0.01 }(m_rand);

		m_vertices.push_back({ p.homogeneous(), { p.x(), p.y(), p.z(), 0.f} });
		return m_vertices.size() - 1;
	}

	void add_triangle(size_t i0, size_t i1, size_t i2, int level = 0)
	{
		if (level == 4)
		{
			m_indices.push_back(i1);
			m_indices.push_back(i0);
			m_indices.push_back(i2);
		}
		else
		{
			auto interpolate = [this](size_t a, size_t b)
			{
				return ((m_vertices[a].position + m_vertices[b].position) / 2).hnormalized();
			};

			auto i01 = add_vertex(interpolate(i0, i1));
			auto i12 = add_vertex(interpolate(i1, i2));
			auto i20 = add_vertex(interpolate(i2, i0));

			add_triangle(i0, i20, i01, level + 1);
			add_triangle(i1, i01, i12, level + 1);
			add_triangle(i2, i12, i20, level + 1);
			add_triangle(i01, i20, i12, level + 1);
		}
	}

	void generate() override
	{
		float phi = (1.0f + sqrt(5.0f)) * 0.5f;
		float a = 1.0f;
		float b = 1.0f / phi;

		auto v1 = add_vertex(vec3{0, b, -a}.normalized());
		auto v2 = add_vertex(vec3{b, a, 0}.normalized());
		auto v3 = add_vertex(vec3{-b, a, 0}.normalized());
		auto v4 = add_vertex(vec3{0, b, a}.normalized());
		auto v5 = add_vertex(vec3{0, -b, a}.normalized());
		auto v6 = add_vertex(vec3{-a, 0, b}.normalized());
		auto v7 = add_vertex(vec3{0, -b, -a}.normalized());
		auto v8 = add_vertex(vec3{a, 0, -b}.normalized());
		auto v9 = add_vertex(vec3{a, 0, b}.normalized());
		auto v10 = add_vertex(vec3{-a, 0, -b}.normalized());
		auto v11 = add_vertex(vec3{b, -a, 0}.normalized());
		auto v12 = add_vertex(vec3{-b, -a, 0}.normalized());

		add_triangle(v3, v2, v1);
		add_triangle(v2, v3, v4);
		add_triangle(v6, v5, v4);
		add_triangle(v5, v9, v4);
		add_triangle(v8, v7, v1);
		add_triangle(v7, v10, v1);
		add_triangle(v12, v11, v5);
		add_triangle(v11, v12, v7);
		add_triangle(v10, v6, v3);
		add_triangle(v6, v10, v12);
		add_triangle(v9, v8, v2);
		add_triangle(v8, v9, v11);
		add_triangle(v3, v6, v4);
		add_triangle(v9, v2, v4);
		add_triangle(v10, v3, v1);
		add_triangle(v2, v8, v1);
		add_triangle(v12, v10, v7);
		add_triangle(v8, v11, v7);
		add_triangle(v6, v12, v5);
		add_triangle(v11, v9, v5);
	}
};

struct tree_mesh : mesh
{
	std::minstd_rand m_rand;
	vec3 m_o;

	D3D_PRIMITIVE_TOPOLOGY topology() const { return D3D11_PRIMITIVE_TOPOLOGY_LINELIST; }

	tree_mesh(d3d_device& device, vec4 origin)
		: mesh{ device }, m_rand{ std::random_device{}() }, m_o{ origin.hnormalized() }
	{}

	void add_line(vec3 a, vec3 b)
	{
		m_indices.push_back(m_vertices.size());
		m_vertices.push_back({ a.homogeneous() });
		m_indices.push_back(m_vertices.size());
		m_vertices.push_back({ b.homogeneous() });
	}

	struct state
	{
		vec3 o = { 0, 0, 0 };
		vec3 dir = { 0, 1, 0 };
		float strength = .2;
		int depth = 0;
	};

	mat3 rot(double a, double b) const
	{
		mat3 r;
		r <<
			cosf(a), 0, -sinf(a),
			0, 1, 0,
			sinf(a), 0, cosf(a);

		mat3 r2;
		r2 <<
			1, 0, 0,
			0, cosf(b), -sinf(b),
			0, sinf(b), cosf(b);
		return r * r2;
	}

	void gen(state s)
	{
		using nd = std::normal_distribution<double>;
		if (s.strength < 0.05 || s.depth > 12)
			return;

		auto o2 = s.o + s.dir * s.strength;
		add_line(s.o, o2);
		auto s_1 = s;
		auto s_2 = s;
		
		s_1.depth = s.depth + 1;
		s_2.depth = s.depth + 1;

		s_1.o = o2;
		s_2.o = o2;

		s_1.strength = s.strength * nd{ 0.8, 0.3 }(m_rand);
		s_2.strength = s.strength * nd{ 0.8, 0.2 }(m_rand);

		nd adist{ 0.0, 1.2 };
		s_1.dir = (rot(adist(m_rand), adist(m_rand)) * s.dir + vec3{ 0, .7, 0 }).normalized();
		s_2.dir = (rot(adist(m_rand), adist(m_rand)) * s.dir + vec3{ 0, .7, 0 }).normalized();

		gen(s_1);
		gen(s_2);
	}

	void generate() override
	{
		state s;
		s.o = m_o;
		gen(s);
	}
};

struct pmp_mesh : mesh
{
	pmp::SurfaceMesh& m;
	std::unique_ptr<pmp::SurfaceMesh> m_p;

	pmp_mesh(d3d_device& device, pmp::SurfaceMesh& m)
		: mesh{ device }, m{ m }
	{}

	pmp_mesh(d3d_device& device, std::unique_ptr<pmp::SurfaceMesh> &&m)
		: mesh{ device }, m{ *m.get() }, m_p{ std::move(m) }
	{}

	void generate() override
	{
		m_generated = false;
		m_vertices.clear();
		m_indices.clear();
		pmp::Triangulation t(m);
		t.triangulate();

		auto pos = m.get_vertex_property<pmp::Point>("v:point");
		for (auto v : m.vertices())
		{
			auto p = (vec3)pos[v];
			auto normal = (vec3)pmp::Normals::compute_vertex_normal(m, v);
			m_vertices.push_back({ p.homogeneous(), { normal.x(), normal.y(), normal.z(), 0 } });
		}

		for (auto f = m.faces_begin(); f != m.faces_end(); ++f)
		{
			const auto& face = *f;
			auto circulator = m.vertices(face);
			auto firstIx = m_indices.size();
			std::transform(circulator.begin(), circulator.end(), std::back_inserter(m_indices), [](const pmp::Vertex &v) { return v.idx(); });
			std::reverse(m_indices.begin() + firstIx, m_indices.end());
		}
	}
};

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
				cosf(alpha), sinf(alpha), 0.f, 0.f,
				-sinf(alpha), cosf(alpha), 0.f, 0.f,
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
			return v.normalized();
		};

		vec4 normal{ m_scale / 2, 0.f, 0.f, 0.f };
		vec4 dir0{ 0.f, m_scale / 2, m_scale / 2, 0.f };
		vec4 dir1{ 0.f, m_scale / 2, -m_scale / 2, 0.f };
		vec4 dir2{ 0.f, -m_scale / 2, -m_scale / 2, 0.f };
		vec4 dir3{ 0.f, -m_scale / 2, m_scale / 2, 0.f };

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
			m_vertices.push_back({ d0 + m_point, norm(n), vec4{ 1.0f, 1.f, 0.f, 0.f } });
			auto id1 = m_vertices.size();
			m_vertices.push_back({ d1 + m_point, norm(n), vec4 { 0.f, 1.f, 0.f, 0.f } });
			auto id2 = m_vertices.size();
			m_vertices.push_back({ d2 + m_point, norm(n), vec4 { 0.f, 0.f, 1.f, 0.f } });

			m_indices.push_back(id0);
			m_indices.push_back(id1);
			m_indices.push_back(id2);

			id2 = m_vertices.size();
			m_vertices.push_back({ d2 + m_point, norm(n), vec4 { 1.f, 0.f, 1.f, 0.f } });
			id0 = m_vertices.size();
			m_vertices.push_back({ d0 + m_point, norm(n), vec4{ 0.0f, 1.f, 0.f, 0.f } });
			auto id3 = m_vertices.size();
			m_vertices.push_back({ d3 + m_point, norm(n), vec4 { 0.f, 0.f, 1.f, 0.f } });

			m_indices.push_back(id0);
			m_indices.push_back(id2);
			m_indices.push_back(id3);
		}
	}
};
struct constants
{
	mat4 cam;
	mat4 world;
	vec4 light;
	float f;
};

size_t round_up(size_t size)
{
	return (size + 15) / 16 * 16;
}

const char *layout = R"(
9999999999
9.....T..9
9.31.....9
9.2..T...9
9.T...2..9
9T...23..9
9T..234529
9T...21..9
9T.....T.9
9.2.T..2.9
56781T...8
4..932.3.7
3.T8...2.6
2..76432.5
1.1.X....4
...1.2....
)";

template<typename Out>
void generate_cubes(Out out, d3d_device &device)
{
	std::string_view l{ layout };
	int x = 0;
	int y = 0;
	for (auto c : l)
	{
		if (c >= '0' && c <= '9')
		{
			for (int i = 0; i < c - '0'; ++i)
			{
				int z = i - 1;
				vec4 p{ float(x), float(z), float(y), 1.f };
				*out++ = std::make_unique<cube_mesh>(device, p, 1);
			}
			++x;
		}
		else if (c == 'T')
		{
			vec4 p{ float(x), float(-1.5), float(y), 1.f };
			*out++ = std::make_unique<tree_mesh>(device, p);
		}
		else if (c == '.')
		{
			++x;
		}
		else if (c == 'X')
		{
			player1.position.x() = x;
			player1.position.z() = y;
			player1.dir = 3.14159;
			++x;
		}
		else if (c == '\n')
		{
			x = 0;
			++y;
		}
	}
}

int main()
{
	camera cam;

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
	constantBufferDesc.ByteWidth = round_up(sizeof(constants));
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

	D3D11_RASTERIZER_DESC1 rasterizerDesc{};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.MultisampleEnable = TRUE;
	CComPtr<ID3D11RasterizerState1> rasterizerState;
	device->CreateRasterizerState1(&rasterizerDesc, &rasterizerState);

	auto v_shader = create_vertex_shader(L"VertexShader.cso", device.device);
	auto p_shader = create_pixel_shader(L"PixelShader.cso", device.device);

	std::array<D3D11_INPUT_ELEMENT_DESC, 3> inputElementDesc
	{
		D3D11_INPUT_ELEMENT_DESC { "SV_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		D3D11_INPUT_ELEMENT_DESC { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	CComPtr<ID3D11InputLayout> inputLayout;
	device->CreateInputLayout(inputElementDesc.data(), inputElementDesc.size(), v_shader.data.data(), v_shader.data.size(), &inputLayout);

	D3D11_VIEWPORT viewport{ (depthBufferDesc.Width - depthBufferDesc.Height) / 2.0, 0, depthBufferDesc.Height, depthBufferDesc.Height, 0, 1};

	std::vector<std::unique_ptr<mesh>> meshes;
	//generate_cubes(std::back_inserter(meshes), device);

	auto sphere = std::make_unique<pmp::SurfaceMesh>(pmp::Shapes::icosphere(2));
	auto pos = sphere->get_vertex_property<pmp::Point>("v:point");

//	meshes.push_back(std::make_unique<pmp_mesh>(device, *sphere.get()));
	meshes.push_back(std::make_unique<pmp_mesh>(device, std::make_unique<pmp::SurfaceMesh>(pmp::Shapes::torus())));

	double alpha = 0;
	auto rot1 = [](float alpha) {
		mat4 rot;
		rot <<
			cosf(alpha), sinf(alpha), 0.f, 0.f,
			-sinf(alpha), cosf(alpha), 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f;
		return rot;
	};

	auto deviceContext = device.deviceContext.p;
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

	auto t = std::chrono::system_clock::now();
	auto t2 = t;

	std::minstd_rand rand;
	for (;;)
	{
		MSG msg;
		WaitMessage();
		if (GetMessage(&msg, hwnd, 0, 0))
			DispatchMessage(&msg);
		else
			break;

		t = std::chrono::system_clock::now();
		if (t - t2 > 10ms)
		{
			draw = true;
			SetTimer(hwnd, 1, 10, nullptr);
		}

		if (draw)
		{
			std::normal_distribution dist{ 1.0, 0.005 };
			for (auto v : sphere->vertices())
			{
				vec3 p = pos[v];
				pos[v] *= dist(rand);
			}

			player1.step();
			t2 = t;
			D3D11_MAPPED_SUBRESOURCE mappedSubresource;
			deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
			auto cnst = reinterpret_cast<constants*>(mappedSubresource.pData);
			cnst->f = player1.f;
			cnst->light = vec4{-.3f, -.5f, 1.f, 0.f }.normalized();
			cnst->world = player1.cam().matrix_world();
			cnst->cam = player1.cam().matrix();
			deviceContext->Unmap(constantBuffer, 0);

			deviceContext->ClearRenderTargetView(frameBufferView, (std::array<float, 4> { 0.1, 0.2, 0.2, 1.0 }).data());
			deviceContext->ClearDepthStencilView(depthBufferView, D3D11_CLEAR_DEPTH, 1.f, 0);


			for (auto& m : meshes)
				m->draw();

			swapChain->Present(1, 0);

			alpha += 0.01;
			draw = false;
		}
	}

}

