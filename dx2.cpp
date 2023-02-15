#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "pmp")
#define NOMINMAX
#define WNI32_LEAN_AND_MEAN
#define π 3.14159265359
#ifndef M_PI
#define M_PI π
#endif

#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite_1.h>
#include <wincodec.h>

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
#include <atlcom.h>

#include "PerlinNoise.hpp"

using namespace std::literals;

using vec3 = Eigen::Vector3f;
using vec4 = Eigen::Vector4f;
using mat3 = Eigen::Matrix3f;
using mat4 = Eigen::Matrix4f;

struct constants
{
  mat4 object;
  mat4 cam;
  mat4 world;
  mat4 light_camera;
  vec3 light;
  int material;
} constants0;

struct camera
{
  vec3 position{ 0.f, 0.f, 0.f };
  vec3 look_at{ 0.f, 0.f, 0.f };
  vec3 up{ 0.0f, 1.0f, 0.0f };
  float n = 0.5f;
  float f = 100.0f;

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
    mat4 P;

    P <<
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, f / (f - n), n* f / (n - f),
      0, 0, 1, 0
      ;

    auto M = P * matrix_world();

    return M;
  }
};

struct player
{
  vec3 position{ 0.f, 0.f, -2.0f };
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
    cam.up = rot() * rot2() * vec3 { 0.f, 1.f, 0.f };
    cam.look_at = cam.position + rot() * rot2() * vec3 { 0.f, 0.f, .1f };

    return cam;
  }

  void step()
  {
    dir += key_left * -.1f + key_right * .1f;
    dir2 += key_home * -.1f + key_end * .1f;
    vec3 velocity = rot() * vec3 { 0.f, key_pgup * .1f + key_pgdown * -.1f, key_up * .1f + key_down * -.1f };
    position += velocity;
  }

  player ahead(float distance)
  {
    player p = *this;
    p.position += rot() * vec3 { 0, 0, distance };
    return p;
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

vertex_shader create_vertex_shader(std::wstring_view file, ID3D11Device* device)
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

pixel_shader create_pixel_shader(std::wstring_view file, ID3D11Device* device)
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
  vec4 color{ 1.f, 1.f, 1.f, 1.f };
  uint32_t type = 0;
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
  CComPtr<ID3D11Buffer> constantBuffer;

  d3d_device()
  {
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_DEBUGGABLE, &featureLevel, 1, D3D11_SDK_VERSION, &baseDevice, nullptr, &baseDeviceContext);
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
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; //DXGI_SWAP_EFFECT_DISCARD;
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

template<typename T>
struct mapped_resource
{
  d3d_device& m_device;
  D3D11_MAPPED_SUBRESOURCE m_data{};
  ID3D11Resource* m_resource;

  mapped_resource(d3d_device& device, ID3D11Resource* resource, D3D11_MAP map_type = D3D11_MAP_WRITE_DISCARD)
    : m_device(device), m_resource{ resource }
  {
    m_device.deviceContext->Map(m_resource, 0, map_type, 0, &m_data);
  }

  mapped_resource(const mapped_resource&) = delete;
  mapped_resource(mapped_resource&& b)
    : m_device{ b.m_devicedevice }
  {
    std::swap(m_data, b.m_data);
  }

  T* data()
  {
    return reinterpret_cast<T*>(m_data.pData);
  }

  T* operator ->()
  {
    return data();
  }

  ~mapped_resource()
  {
    if (m_data.pData)
      m_device.deviceContext->Unmap(m_resource, 0);
  }
};


struct mesh
{
  d3d_device& m_device;
  std::vector<vertex_data> m_vertices;
  std::vector<uint32_t> m_indices;
  CComPtr<ID3D11Buffer> m_vertexBuffer;
  CComPtr<ID3D11Buffer> m_indexBuffer;
  bool m_generated{ false };
  int m_material{ 0 };

  virtual void generate() = 0;

  virtual D3D_PRIMITIVE_TOPOLOGY topology() const {
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }

  mesh(d3d_device& device)
    : m_device{ device }
  {
  }

  mesh(mesh&&) = default;
  mesh(const mesh&) = delete;

  void createBuffers()
  {
    if (!m_generated)
    {
      m_vertexBuffer.Release();
      m_indexBuffer.Release();
      generate();
      m_generated = true;

      D3D11_BUFFER_DESC vertexBufferDesc{};
      vertexBufferDesc.ByteWidth = m_vertices.size() * sizeof(m_vertices[0]);
      vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
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
  }

  virtual void draw(int pass, mat4 transform = mat4::Identity())
  {
    if (pass == 1 && topology() != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
      return;

    createBuffers();

    auto deviceContext = m_device.deviceContext.p;

    uint32_t stride = sizeof(vertex_data);
    uint32_t offset = 0;

    deviceContext->IASetPrimitiveTopology(topology());
    deviceContext->IASetVertexBuffers(0, 1, &m_vertexBuffer.p, &stride, &offset);
    deviceContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    constants0.object = transform;
    constants0.material = m_material;
    *mapped_resource<constants> { m_device, m_device.constantBuffer }.data() = constants0;

    deviceContext->DrawIndexed(m_indices.size(), 0, 0);
  }
};

struct multi_mesh : mesh
{
  mesh& m_base;
  int m_count;
  std::function<mat4(int)> m_transformer;

  void generate() {}

  D3D_PRIMITIVE_TOPOLOGY topology() const override {
    return m_base.topology();
  }

  multi_mesh(d3d_device& device, mesh& base, int count, decltype(m_transformer) transformer)
    : mesh{ device }, m_base{ base }, m_count{ count }, m_transformer{ transformer }
  {}

  void draw(int pass, mat4 transform = mat4::Identity()) override
  {
    for (int i = 0; i < m_count; ++i)
      m_base.draw(pass, transform * m_transformer(i));
  }
};

// {ACBD3E59-A56F-450D-ADC6-80E3DF9A98BE}
static const GUID CLSID_my_renderer =
{ 0xacbd3e59, 0xa56f, 0x450d, { 0xad, 0xc6, 0x80, 0xe3, 0xdf, 0x9a, 0x98, 0xbe } };

class ATL_NO_VTABLE my_renderer : 
  public CComObjectRoot,
  public CComCoClass<my_renderer, &CLSID_my_renderer>,
  public IDispatchImpl<IDWriteTextRenderer>
{
public:
  BEGIN_COM_MAP(my_renderer)
    COM_INTERFACE_ENTRY(IDWriteTextRenderer)
  END_COM_MAP()

  HRESULT IsPixelSnappingDisabled(void*, BOOL*)
  {
    return E_NOTIMPL;
  }

  HRESULT GetCurrentTransform(
    void* clientDrawingContext,
    DWRITE_MATRIX* transform
  ) 
  {
    return E_NOTIMPL;
  }

  HRESULT GetPixelsPerDip(
    void* clientDrawingContext,
    FLOAT* pixelsPerDip
  )
  {
    return E_NOTIMPL;
  }

  HRESULT DrawGlyphRun(
    void* clientDrawingContext,
    FLOAT                              baselineOriginX,
    FLOAT                              baselineOriginY,
    DWRITE_MEASURING_MODE              measuringMode,
    DWRITE_GLYPH_RUN const* glyphRun,
    DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    IUnknown* clientDrawingEffect
  )
  {
    return E_NOTIMPL;
  }

  HRESULT DrawInlineObject(
    void* clientDrawingContext,
    FLOAT               originX,
    FLOAT               originY,
    IDWriteInlineObject* inlineObject,
    BOOL                isSideways,
    BOOL                isRightToLeft,
    IUnknown* clientDrawingEffect
  )
  {
    return E_NOTIMPL;
  }

  HRESULT DrawStrikethrough(
    void* clientDrawingContext,
    FLOAT                      baselineOriginX,
    FLOAT                      baselineOriginY,
    DWRITE_STRIKETHROUGH const* strikethrough,
    IUnknown* clientDrawingEffect
  )
  {
    return E_NOTIMPL;
  }

  HRESULT DrawUnderline(
    void* clientDrawingContext,
    FLOAT                  baselineOriginX,
    FLOAT                  baselineOriginY,
    DWRITE_UNDERLINE const* underline,
    IUnknown* clientDrawingEffect
  )
  {
    return E_NOTIMPL;
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

    auto v1 = add_vertex(vec3{ 0, b, -a }.normalized());
    auto v2 = add_vertex(vec3{ b, a, 0 }.normalized());
    auto v3 = add_vertex(vec3{ -b, a, 0 }.normalized());
    auto v4 = add_vertex(vec3{ 0, b, a }.normalized());
    auto v5 = add_vertex(vec3{ 0, -b, a }.normalized());
    auto v6 = add_vertex(vec3{ -a, 0, b }.normalized());
    auto v7 = add_vertex(vec3{ 0, -b, -a }.normalized());
    auto v8 = add_vertex(vec3{ a, 0, -b }.normalized());
    auto v9 = add_vertex(vec3{ a, 0, b }.normalized());
    auto v10 = add_vertex(vec3{ -a, 0, -b }.normalized());
    auto v11 = add_vertex(vec3{ b, -a, 0 }.normalized());
    auto v12 = add_vertex(vec3{ -b, -a, 0 }.normalized());

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

  pmp_mesh(d3d_device& device, std::unique_ptr<pmp::SurfaceMesh>&& m)
    : mesh{ device }, m{ *m.get() }, m_p{ std::move(m) }
  {}

  void generate() override
  {
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
      std::transform(circulator.begin(), circulator.end(), std::back_inserter(m_indices), [](const pmp::Vertex& v) { return v.idx(); });
      std::reverse(m_indices.begin() + firstIx, m_indices.end());
    }
  }
};

struct coord_mesh : mesh
{
  D3D_PRIMITIVE_TOPOLOGY topology() const { return D3D11_PRIMITIVE_TOPOLOGY_LINELIST; }
  using mesh::mesh;

  void generate() override
  {
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{-10000, 0, 0, 1}, {}, {}, {}, 1 });
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{10000, 0, 0, 1}, {}, {}, {}, 1 });

    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{0, -10000, 0, 1}, {}, {}, {}, 1 });
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{0, 10000, 0, 1}, {}, {}, {}, 1 });

    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{0, 0, -10000, 1}, {}, {}, {}, 1 });
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ vec4{0, 0, 10000, 1}, {}, {}, {}, 1 });

  }
};

struct line_mesh : mesh
{
  vec4 m_a;
  vec4 m_b;

  D3D_PRIMITIVE_TOPOLOGY topology() const { return D3D11_PRIMITIVE_TOPOLOGY_LINELIST; }
  line_mesh(d3d_device& device, vec4 a, vec4 b)
    : mesh(device), m_a{ a }, m_b{ b }
  {
  }

  void generate() override
  {
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ m_a, {}, {}, {}, 1 });
    m_indices.push_back(m_vertices.size());
    m_vertices.push_back({ m_b, {}, {}, {}, 1 });
  }
};

struct cube_mesh : mesh
{
  vec4 m_point;
  vec4 m_color;
  float m_scale;
  D3D_PRIMITIVE_TOPOLOGY topology() const { return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; }

  cube_mesh(d3d_device& device, vec4 p, float scale, vec4 color = vec4{ 0.2f, 0.7f, 0.5f, 1.f })
    : mesh{ device }, m_point{ p }, m_color{ color }, m_scale{ scale }
  {
  }


  void generate() override
  {
    if (m_generated)
      return;
    auto rot1 = [](float α) {
      mat4 rot;
      rot <<
        cosf(α), sinf(α), 0.f, 0.f,
        -sinf(α), cosf(α), 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f;
      return rot;
    };
    auto rot2 = [](float α) {
      mat4 rot;
      rot <<
        cosf(α), 0.f, -sinf(α), 0.f,
        0.f, 1.f, 0.f, 0.f,
        sinf(α), 0.f, cosf(α), 0.f,
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
      m_vertices.push_back({ d0 + m_point, norm(n), vec4{ 1.0f, 1.f, 0.f, 0.f }, m_color });
      auto id1 = m_vertices.size();
      m_vertices.push_back({ d1 + m_point, norm(n), vec4 { 0.f, 1.f, 0.f, 0.f }, m_color });
      auto id2 = m_vertices.size();
      m_vertices.push_back({ d2 + m_point, norm(n), vec4 { 0.f, 0.f, 1.f, 0.f }, m_color });

      m_indices.push_back(id0);
      m_indices.push_back(id1);
      m_indices.push_back(id2);

      id2 = m_vertices.size();
      m_vertices.push_back({ d2 + m_point, norm(n), vec4 { 1.f, 0.f, 1.f, 0.f }, m_color });
      id0 = m_vertices.size();
      m_vertices.push_back({ d0 + m_point, norm(n), vec4{ 0.0f, 1.f, 0.f, 0.f }, m_color });
      auto id3 = m_vertices.size();
      m_vertices.push_back({ d3 + m_point, norm(n), vec4 { 0.f, 0.f, 1.f, 0.f }, m_color });

      m_indices.push_back(id0);
      m_indices.push_back(id2);
      m_indices.push_back(id3);
    }
  }
};
size_t round_up(size_t size)
{
  return (size + 15) / 16 * 16;
}

const char* layout = R"(
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
void generate_cubes(Out out, d3d_device& device)
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

mat4 hom(const mat3& mat)
{
  auto result = mat4::Identity().eval();
  result.topLeftCorner(3, 3) = mat;
  return result;
} 

std::vector<vec4> text(d3d_device& device, std::wstring_view t, unsigned int width, unsigned int height)
{
  CComPtr<ID2D1Factory1> factory;
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), (void**)&factory.p);
  
  CComPtr<IDWriteFactory> writeFactory;
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), (IUnknown **)&writeFactory.p);

  CComPtr<IDWriteTextFormat> textFormat;
  writeFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 140, L"en-US", &textFormat);

  CComPtr<IDWriteTextLayout> textLayout;
  writeFactory->CreateTextLayout(t.data(), t.size(), textFormat, 100, 100, &textLayout);

  my_renderer renderer;
//  textLayout->Draw(nullptr, 

/*
  CComQIPtr<IDXGIDevice1> dxgi{ device.baseDevice };
  CComPtr<ID2D1Device> d2device;
  D2D1_CREATION_PROPERTIES props{};
  props.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
  props.threadingMode = D2D1_THREADING_MODE_SINGLE_THREADED;
  D2D1CreateDevice(dxgi, &props, &d2device);
  CComPtr<ID2D1DeviceContext> d2device_context;
  D2D1_DEVICE_CONTEXT_OPTIONS opts{};
  d2device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2device_context);
  auto bmProps{ D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)) };
  CComPtr<ID2D1Bitmap1> bitmap, bitmap2;
  d2device_context->CreateBitmap(D2D1_SIZE_U{ .width = width, .height = height }, nullptr, 0, bmProps, &bitmap);

  auto bmProps2{ D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)) };
  d2device_context->CreateBitmap(D2D1_SIZE_U{ .width = width, .height = height }, nullptr, 0, bmProps2, &bitmap2);

  CComPtr<IDXGISurface> surface;
  bitmap->GetSurface(&surface);
  CComPtr<ID2D1RenderTarget> target;
  auto renderTargetProps{ D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96) };
  factory->CreateDxgiSurfaceRenderTarget(surface, renderTargetProps, &target);

  CComPtr<IDWriteTextFormat> textFormat;
  writeFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 140, L"en-US", &textFormat);

  D2D1_COLOR_F color{};
  color.a = 0xff;
  color.r = 0xff;
  CComPtr<ID2D1SolidColorBrush> brush;
  target->CreateSolidColorBrush(color, &brush);
  target->BeginDraw();
  target->Clear();
  auto textRect{ D2D1::RectF(0, height / 2, width, height) };
  target->DrawTextW(t.data(), t.length(), textFormat, textRect, brush);
  target->EndDraw();

  auto dstPoint { D2D1::Point2U() };
  auto srcRect { D2D1::RectU(0, 0, width, height) };
  bitmap2->CopyFromBitmap(&dstPoint, bitmap, &srcRect);
  D2D1_MAPPED_RECT rect;
  bitmap2->Map(D2D1_MAP_OPTIONS_READ, &rect);

  std::vector<vec4> result;
  result.resize(width * height);
  std::transform(reinterpret_cast<uint32_t*>(rect.bits), reinterpret_cast<uint32_t*>(rect.bits) + width * height, result.begin(),
    [](uint32_t val)
    {
      struct pixel { uint8_t r, g, b, a; } pix;
      memcpy(&pix, &val, sizeof(pix));
      return vec4{ pix.r / 255.0f, pix.g / 255.0f, pix.b / 255.0f, pix.a / 255.0f };
    });

  bitmap2->Unmap();

  return result;*/
}



struct texture
{
  d3d_device& m_device;

  CComPtr<ID3D11Texture2D> m_texture;
  CComPtr<ID3D11ShaderResourceView> m_view;

  int m_width = 1024;
  int m_height = 1024;

  texture(d3d_device& device)
    : m_device(device)
  {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;

    m_device->CreateTexture2D(&desc, nullptr, &m_texture);

    m_device->CreateShaderResourceView(m_texture, nullptr, &m_view);
  }

  void diff(vec4* src, vec4* dst)
  {
    auto n{ [](int x) {return float(x == 0 ? 0 : (x > 0 ? 1 : -1)); } };
    int span = 2;
    std::fill_n(dst, m_width * m_height, vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
/*    for (int x = 3; x < m_width - 3; ++x)
    {
      for (int y = 3; y < m_height - 3; ++y)
      {
        vec4 sum{ 0.f, 0.f, 0.f, 0.f };
        for (int dx = -span; dx <= span; ++dx)
        {
          for(int dy = -span; dy <= span; ++dy)
          {
            if (!dx && !dy)
              continue;
            auto dif = src[y * m_width + x].x() - src[(y + dy) * m_width + x + dx].x();
            sum += dif / (((span * 2 + 1) * (span * 2 + 1)) - 1) * vec4{ n(dx), n(dy), 0, 0.f };
          }
        }
        dst[y * m_width + x] = sum;
      }
    }   */
  }

  void generate()
  {
    mapped_resource<vec4> mapping(m_device, m_texture);

    auto t = text(m_device, L"Hello, World!", m_width, m_height);
    auto t2 = t;
    diff(t.data(), t2.data());
    std::copy(t2.begin(), t2.end(), mapping.data());
/*
    siv::PerlinNoise noise1{ std::random_device{} };
    siv::PerlinNoise noise2{ std::random_device{} };
    siv::PerlinNoise noise3{ std::random_device{} };

    for (int i = 0; i < m_width; ++i)
      for (int j = 0; j < m_height; ++j)
      {
        auto n = [&](siv::PerlinNoise& noise) { return (float)noise.noise2D(i / 20.f, j / 20.f); };
        mapping.data()[j + i * m_height] = vec4{ n(noise1), n(noise2), n(noise3), 0.0f };
      }*/
  }
};
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
  device->CreateBuffer(&constantBufferDesc, nullptr, &device.constantBuffer);

  CComPtr<ID3D11Texture2D> frameBuffer;
  swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frameBuffer));

  CComPtr<ID3D11RenderTargetView> frameBufferView;
  device->CreateRenderTargetView(frameBuffer, nullptr, &frameBufferView);

  D3D11_TEXTURE2D_DESC shadowMapDesc;
  frameBuffer->GetDesc(&shadowMapDesc);
  shadowMapDesc.Width = 4 * 600;
  shadowMapDesc.Height = 4 * 600;
  shadowMapDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  shadowMapDesc.Usage = D3D11_USAGE_DEFAULT;
  shadowMapDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

  CComPtr<ID3D11Texture2D> shadowMap;
  device->CreateTexture2D(&shadowMapDesc, nullptr, &shadowMap);

  D3D11_DEPTH_STENCIL_VIEW_DESC shadowMapDepthStencilViewDesc{};
  shadowMapDepthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  shadowMapDepthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

  CComPtr<ID3D11DepthStencilView> shadowMapDepthView;
  device->CreateDepthStencilView(shadowMap, &shadowMapDepthStencilViewDesc, &shadowMapDepthView);

  D3D11_SHADER_RESOURCE_VIEW_DESC shadowMapResourceViewDesc{};
  shadowMapResourceViewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  shadowMapResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  shadowMapResourceViewDesc.Texture2D.MipLevels = 1;

  CComPtr<ID3D11ShaderResourceView> shadowMapResurceView;
  device->CreateShaderResourceView(shadowMap, &shadowMapResourceViewDesc, &shadowMapResurceView);

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

  D3D11_RASTERIZER_DESC1 shadowMapRasterizerDesc{};
  shadowMapRasterizerDesc.FillMode = D3D11_FILL_SOLID;
  shadowMapRasterizerDesc.CullMode = D3D11_CULL_FRONT;
  shadowMapRasterizerDesc.MultisampleEnable = TRUE;
  CComPtr<ID3D11RasterizerState1> shadowMapRasterizerState;
  device->CreateRasterizerState1(&shadowMapRasterizerDesc, &shadowMapRasterizerState);

  D3D11_RASTERIZER_DESC1 rasterizerDesc{};
  rasterizerDesc.FillMode = D3D11_FILL_SOLID;
  rasterizerDesc.CullMode = D3D11_CULL_BACK;
  rasterizerDesc.MultisampleEnable = TRUE;
  CComPtr<ID3D11RasterizerState1> rasterizerState;
  device->CreateRasterizerState1(&rasterizerDesc, &rasterizerState);

  auto shadow_v_shader = create_vertex_shader(L"ShadowVS.cso", device.device);
  auto shadow_p_shader = create_pixel_shader(L"ShadowPS.cso", device.device);

  auto v_shader = create_vertex_shader(L"VertexShader.cso", device.device);
  auto p_shader = create_pixel_shader(L"PixelShader.cso", device.device);

  std::array<D3D11_INPUT_ELEMENT_DESC, 5> inputElementDesc
  {
    D3D11_INPUT_ELEMENT_DESC { "SV_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC { "TYPE", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  CComPtr<ID3D11InputLayout> inputLayout;
  device->CreateInputLayout(inputElementDesc.data(), inputElementDesc.size(), v_shader.data.data(), v_shader.data.size(), &inputLayout);

  D3D11_VIEWPORT shadowViewport{ 0, 0, 4 * 600, 4 * 600, 0, 1 };
  D3D11_VIEWPORT viewport{ 0, 0, 600, 600, 0, 1 };

  texture tex1(device);
  tex1.generate();

  auto t = std::chrono::system_clock::now();
  auto t2 = t;
  auto ts = t;

  std::vector<std::unique_ptr<mesh>> meshes;
  //	generate_cubes(std::back_inserter(meshes), device);

  auto base_torus = std::make_unique<pmp_mesh>(device, std::make_unique<pmp::SurfaceMesh>(pmp::Shapes::torus()));
  meshes.push_back(std::make_unique<multi_mesh>(device, *base_torus, 10, [&](int i)
    {
      float α = (t - ts) / 1ms / 1000.0f + i;
      float β = (t - ts) / 1ms / 300.0f + i;

      Eigen::Quaternionf q1{ Eigen::AngleAxisf { α, vec3 {0.0f, 1.0f, 0.0f }} };
      Eigen::Quaternionf q2{ Eigen::AngleAxisf { β, vec3 {1.0f, 0.0f, 0.0f }} };
      auto q = q1 * q2;

      mat3 s = 0.3 * mat3::Identity();
      mat4 p;
      auto γ = i / 10.f * 2 * π;
      p <<
        1, 0, 0, cos(γ) * 2,
        0, 1, 0, 0,
        0, 0, 1, -5 + sin(γ) * 2,
        0, 0, 0, 1;
      mat4 result = p * hom(s * q.matrix());
      return result;
    }
  ));
  //	meshes.push_back(std::make_unique<cube_mesh>(device, vec4{ 0.f, 0.f, 0.f, 1.0f }, 2, vec4{ .2f, .9f, .4f, 1.f }));
  auto pmp_cube = pmp::Shapes::hexahedron();
  pmp::Triangulation tr(pmp_cube);
  tr.triangulate();
  pmp::Remeshing rem(pmp_cube);
  rem.uniform_remeshing(0.1);
//  meshes.push_back(std::make_unique<pmp_mesh>(device, std::make_unique<pmp::SurfaceMesh>(pmp_cube)));
  meshes.push_back(std::make_unique<cube_mesh>(device, vec4{ 0.f, 0.f, 10.f, 1.0f }, 10));
  meshes[1]->m_material = 1;
  //	meshes.push_back(std::make_unique<cube_mesh>(device, vec4{ 0.f, 0.f, 0.f, 1.0f }, 1, vec4{ .2f, .9f, .4f, 1.f }));

  camera light;
  light.position = vec3{ 0, 0, -15.f };
  //	auto cube = meshes[1].get();
  //	cube->generate();
  //	for (auto& v : cube->m_vertices)
  //		meshes.push_back(std::make_unique<line_mesh>(device, (light.position + 3 * (v.position.hnormalized() - light.position)).homogeneous(), light.position.homogeneous()));

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
      player1.step();
      t2 = t;

      auto deviceContext = device.deviceContext.p;


      auto α = (t - ts) / 1ms / 1000.0f;
      light.look_at = vec3{ 0, 0, 0 };
      light.f = 100;
      light.n = .1f;

      constants0.light_camera = light.matrix();
      constants0.light = (light.look_at - light.position).normalized();
      //constants0.light = (player1.cam().look_at - player1.cam().position).normalized();
      //constants0.light_camera = player1.ahead(0.5).cam().matrix();
      constants0.world = player1.cam().matrix_world();
      constants0.cam = player1.cam().matrix();
      *mapped_resource<constants> { device, device.constantBuffer }.data() = constants0;

#pragma region PASS 1
      deviceContext->ClearDepthStencilView(shadowMapDepthView, D3D11_CLEAR_DEPTH, 1.f, 0);

      deviceContext->IASetInputLayout(inputLayout);

      deviceContext->VSSetShader(shadow_v_shader.shader, nullptr, 0);
      deviceContext->VSSetConstantBuffers(0, 1, &device.constantBuffer.p);

      deviceContext->RSSetViewports(1, &shadowViewport);
      deviceContext->RSSetState(shadowMapRasterizerState);

      //      deviceContext->PSSetShader(shadow_p_shader.shader, nullptr, 0);
      //      deviceContext->PSSetSamplers(0, 1, &samplerState.p);
      //      deviceContext->PSSetConstantBuffers(0, 1, &constantBuffer.p);

      deviceContext->OMSetRenderTargets(0, nullptr, shadowMapDepthView);
      deviceContext->OMSetDepthStencilState(depthStencilState, 0);
      deviceContext->OMSetBlendState(nullptr, nullptr, -1);

      for (auto& m : meshes)
        m->draw(1);
      deviceContext->ClearState();
#pragma endregion

#pragma region PASS 2

      deviceContext->ClearRenderTargetView(frameBufferView, (std::array<float, 4> { 0.1, 0.2, 0.2, 1.0 }).data());
      deviceContext->ClearDepthStencilView(depthBufferView, D3D11_CLEAR_DEPTH, 1.f, 0);

      deviceContext->IASetInputLayout(inputLayout);

      deviceContext->VSSetShader(v_shader.shader, nullptr, 0);
      deviceContext->VSSetConstantBuffers(0, 1, &device.constantBuffer.p);

      deviceContext->RSSetViewports(1, &viewport);
      deviceContext->RSSetState(rasterizerState);

      deviceContext->PSSetShader(p_shader.shader, nullptr, 0);
      deviceContext->PSSetSamplers(0, 1, &samplerState.p);
      deviceContext->PSSetConstantBuffers(0, 1, &device.constantBuffer.p);
      deviceContext->PSSetShaderResources(0, 1, &shadowMapResurceView.p);
      deviceContext->PSSetShaderResources(1, 1, &tex1.m_view.p);


      deviceContext->OMSetRenderTargets(1, &frameBufferView.p, depthBufferView);
      deviceContext->OMSetDepthStencilState(depthStencilState, 0);
      deviceContext->OMSetBlendState(nullptr, nullptr, -1);

      for (auto& m : meshes)
        m->draw(2);

      deviceContext->ClearState();
#pragma endregion


      swapChain->Present(1, 0);

      alpha += 0.01;
      draw = false;
    }
  }

}

