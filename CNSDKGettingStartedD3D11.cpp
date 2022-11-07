// Project Requirements:
//    C++17
//    3 Additional include paths for CNSDK

#include <stdio.h>

// CNSDKGettingStartedD3D11 includes
#include "framework.h"
#include "CNSDKGettingStartedD3D11.h"
#include "CNSDKGettingStartedMath.h"

// CNSDK includes
#include "leia/sdk/sdk.hpp"
#include "leia/sdk/interlacer.hpp"
#include "leia/sdk/debugMenu.hpp"
#include "leia/common/platform.hpp"

// D3D11 includes.
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>

// CNSDK single library
#pragma comment(lib, "CNSDK/lib/leiaSDK-faceTrackingInApp.lib")

// D3D11 libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x) if(x != nullptr) { x->Release(); x = nullptr; }
#endif

enum class eDemoMode { Spinning3DCube, StereoImage };

// Global Variables.
const wchar_t*                  g_windowTitle       = L"CNSDK Getting Started D3D11 Sample";
const wchar_t*                  g_windowClass       = L"CNSDKGettingStartedD3D11WindowClass";
int                             g_windowWidth       = 1280;
int                             g_windowHeight      = 720;
bool                            g_fullscreen        = true;
leia::sdk::ILeiaSDK*            g_sdk               = nullptr;
leia::sdk::IThreadedInterlacer* g_interlacer        = nullptr;
eDemoMode                       g_demoMode          = eDemoMode::Spinning3DCube;

// Global D3D11 Variables.
D3D_DRIVER_TYPE           g_driverType                  = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL         g_featureLevel                = D3D_FEATURE_LEVEL_11_0;
DXGI_FORMAT               g_renderTargetViewFormat      = DXGI_FORMAT_R8G8B8A8_UNORM;
ID3D11Device*             g_device                      = nullptr;
ID3D11Device1*            g_device1                     = nullptr;
ID3D11DeviceContext*      g_immediateContext            = nullptr;
ID3D11DeviceContext1*     g_immediateContext1           = nullptr;
IDXGISwapChain*           g_swapChain                   = nullptr;
IDXGISwapChain1*          g_swapChain1                  = nullptr;
ID3D11RenderTargetView*   g_renderTargetView            = nullptr;
ID3D11Texture2D*          g_depthStencilTexture         = nullptr;
ID3D11DepthStencilView*   g_depthStencilView            = nullptr;
ID3D11Texture2D*          g_offscreenTexture            = nullptr;
ID3D11ShaderResourceView* g_offscreenShaderResourceView = nullptr;
ID3D11RenderTargetView*   g_offscreenRenderTargetView   = nullptr;
ID3D11Texture2D*          g_offscreenDepthTexture       = nullptr;
ID3D11DepthStencilView*   g_offscreenDepthStencilView   = nullptr;
ID3D11Buffer*             g_vertexBuffer                = nullptr;
ID3D11Buffer*             g_indexBuffer                 = nullptr;
ID3D11Buffer*             g_shaderConstantBuffer        = nullptr;
ID3D11VertexShader*       g_vertexShader                = nullptr;
ID3D11InputLayout*        g_inputLayout                 = nullptr;
ID3D11PixelShader*        g_pixelShader                 = nullptr;
ID3D11Texture2D*          g_imageTexture                = nullptr;
ID3D11ShaderResourceView* g_imageShaderResourceView     = nullptr;

struct CONSTANTBUFFER
{
    mat4f transform;
};

struct VERTEX
{
    float pos[3];
    float color[3];
};

void OnError(const wchar_t* msg)
{
    MessageBox(NULL, msg, L"CNSDKGettingStartedD3D11", MB_ICONERROR | MB_OK); 
    exit(-1);
}

bool ReadEntireFile(const char* filename, bool binary, char*& data, size_t& dataSize)
{
    const int BUFFERSIZE = 4096;
    char buffer[BUFFERSIZE];

    // Open file.
    FILE* f = fopen(filename, binary ? "rb" : "rt");    
    if (f == NULL)
        return false;

    data     = nullptr;
    dataSize = 0;

    while (true)
    {
        // Read chunk into buffer.
        const size_t bytes = (int)fread(buffer, sizeof(char), BUFFERSIZE, f);
        if (bytes <= 0)
            break;

        // Extend allocated memory and copy chunk into it.
        char* newData = new char[dataSize + bytes];
        if (dataSize > 0)
        {
            memcpy(newData, data, dataSize);
            delete [] data;
            data = nullptr;
        }
        memcpy(newData + dataSize, buffer, bytes);
        dataSize += bytes;
        data = newData;
    }

    // Done and close.
    fclose(f);

    return dataSize > 0;
}

bool ReadTGA(const char* filename, int& width, int& height, GLint& format, char*& data, int& dataSize)
{
    char* ptr = nullptr;
    size_t fileSize = 0;
    if (!ReadEntireFile(filename, true, ptr, fileSize))
    {
        OnError(L"Failed to read TGA file.");
        return false;
    }

    static std::uint8_t DeCompressed[12] = { 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
    static std::uint8_t IsCompressed[12] = { 0x0, 0x0, 0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

    typedef union PixelInfo
    {
        std::uint32_t Colour;
        struct
        {
            std::uint8_t R, G, B, A;
        };
    } *PPixelInfo;

    // Read header.
    std::uint8_t Header[18] = { 0 };
    memcpy(&Header, ptr, sizeof(Header));
    ptr += sizeof(Header);

    int bitsPerPixel = 0;

    if (!std::memcmp(DeCompressed, &Header, sizeof(DeCompressed)))
    {
        bitsPerPixel = Header[16];
        width        = Header[13] * 256 + Header[12];
        height       = Header[15] * 256 + Header[14];
        dataSize     = ((width * bitsPerPixel + 31) / 32) * 4 * height;

        if ((bitsPerPixel != 24) && (bitsPerPixel != 32))
        {
            OnError(L"Invalid TGA file isn't 24/32-bit.");
            return false;
        }

        format = (bitsPerPixel == 24) ? GL_BGR : GL_BGRA;

        data = new char[dataSize];
        memcpy(data, ptr, dataSize);
    }
    else if (!std::memcmp(IsCompressed, &Header, sizeof(IsCompressed)))
    {
        bitsPerPixel = Header[16];
        width        = Header[13] * 256 + Header[12];
        height       = Header[15] * 256 + Header[14];
        dataSize     = width * height * sizeof(PixelInfo);

        if ((bitsPerPixel != 24) && (bitsPerPixel != 32))
        {
            OnError(L"Invalid TGA file isn't 24/32-bit.");
            return false;
        }

        format = (bitsPerPixel == 24) ? GL_BGR : GL_BGRA;

        PixelInfo Pixel = { 0 };
        int CurrentByte = 0;
        std::size_t CurrentPixel = 0;
        std::uint8_t ChunkHeader = { 0 };
        int BytesPerPixel = (bitsPerPixel / 8);

        data = new char[dataSize];

        do
        {
            memcpy(&ChunkHeader, ptr, sizeof(ChunkHeader));
            ptr += sizeof(ChunkHeader);

            if (ChunkHeader < 128)
            {
                ++ChunkHeader;
                for (int I = 0; I < ChunkHeader; ++I, ++CurrentPixel)
                {
                    memcpy(&Pixel, ptr, BytesPerPixel);
                    ptr += BytesPerPixel;

                    data[CurrentByte++] = Pixel.B;
                    data[CurrentByte++] = Pixel.G;
                    data[CurrentByte++] = Pixel.R;
                    if (bitsPerPixel > 24)
                        data[CurrentByte++] = Pixel.A;
                }
            }
            else
            {
                ChunkHeader -= 127;
                memcpy(&Pixel, ptr, BytesPerPixel);
                ptr += BytesPerPixel;

                for (int I = 0; I < ChunkHeader; ++I, ++CurrentPixel)
                {
                    data[CurrentByte++] = Pixel.B;
                    data[CurrentByte++] = Pixel.G;
                    data[CurrentByte++] = Pixel.R;
                    if (bitsPerPixel > 24)
                        data[CurrentByte++] = Pixel.A;
                }
            }
        } while (CurrentPixel < (width * height));
    }
    else
    {
        OnError(L"Invalid TGA file isn't 24/32-bit.");
        return false;
    }
   
    return true;
}

BOOL CALLBACK GetDefaultWindowStartPos_MonitorEnumProc(__in  HMONITOR hMonitor, __in  HDC hdcMonitor, __in  LPRECT lprcMonitor, __in  LPARAM dwData)
{
    std::vector<MONITORINFOEX>& infoArray = *reinterpret_cast<std::vector<MONITORINFOEX>*>(dwData);
    MONITORINFOEX info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    GetMonitorInfo(hMonitor, &info);
    infoArray.push_back(info);
    return TRUE;
}

bool GetNonPrimaryDisplayTopLeftCoordinate(int& x, int& y)
{
    // Get connected monitor info.
    std::vector<MONITORINFOEX> mInfo;
    mInfo.reserve(::GetSystemMetrics(SM_CMONITORS));
    EnumDisplayMonitors(NULL, NULL, GetDefaultWindowStartPos_MonitorEnumProc, reinterpret_cast<LPARAM>(&mInfo));

    // If we have multiple monitors, select the first non-primary one.
    if (mInfo.size() > 1)
    {
        for (int i = 0; i < mInfo.size(); i++)
        {
            const MONITORINFOEX& mi = mInfo[i];

            if (0 == (mi.dwFlags & MONITORINFOF_PRIMARY))
            {
                x = mi.rcMonitor.left;
                y = mi.rcMonitor.top;
                return true;
            }
        }
    }

    // Didn't find a non-primary, there is only one display connected.
    x = 0;
    y = 0;
    return false;
}

HWND CreateGraphicsWindow(HINSTANCE hInstance)
{
    // Create window.
    HWND hWnd = NULL;
    {
        int defaultX = 0;
        int defaultY = 0;
        GetNonPrimaryDisplayTopLeftCoordinate(defaultX, defaultY);

        DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;            // Window Extended Style
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;                            // Windows Style

        RECT WindowRect;
        WindowRect.left = (long)defaultX;
        WindowRect.right = (long)(defaultX + g_windowWidth);
        WindowRect.top = (long)defaultY;
        WindowRect.bottom = (long)(defaultY + g_windowHeight);
        //AdjustWindowRectEx(&WindowRect, dwStyle, FALSE, dwExStyle);        // Adjust Window To True Requested Size

        hWnd = CreateWindowEx
        (
            dwExStyle,
            g_windowClass,                         // Class Name
            g_windowTitle,                         // Window Title
            dwStyle |                              // Defined Window Style
            WS_CLIPSIBLINGS |                      // Required Window Style
            WS_CLIPCHILDREN,                       // Required Window Style
            WindowRect.left,                       // Window left
            WindowRect.top,                        // Window top
            WindowRect.right - WindowRect.left,    // Calculate Window Width
            WindowRect.bottom - WindowRect.top,    // Calculate Window Height
            NULL,                                  // No Parent Window
            NULL,                                  // No Menu
            hInstance,                             // Instance
            NULL                                   // Dont Pass Anything To WM_CREATE
        );

        if (!hWnd)
            OnError(L"Failed to create window.");
    }
    return hWnd;
}

void SetFullscreen(HWND hWnd, bool fullscreen)
{
    static int windowPrevX = 0;
    static int windowPrevY = 0;
    static int windowPrevWidth = 0;
    static int windowPrevHeight = 0;

    DWORD style = GetWindowLong(hWnd, GWL_STYLE);
    if (fullscreen)
    {
        RECT rect;
        MONITORINFO mi = { sizeof(mi) };
        GetWindowRect(hWnd, &rect);

        windowPrevX = rect.left;
        windowPrevY = rect.top;
        windowPrevWidth = rect.right - rect.left;
        windowPrevHeight = rect.bottom - rect.top;

        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLong(hWnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        MONITORINFO mi = { sizeof(mi) };
        UINT flags = SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW;
        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLong(hWnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPos(hWnd, HWND_NOTOPMOST, windowPrevX, windowPrevY, windowPrevWidth, windowPrevHeight, flags);
    }
}

#if 0
HGLRC InitializeOpenGL(HWND hWnd, HDC hDC)
{
    HGLRC context = NULL;

    PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARBFunc = nullptr;
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARBFunc = nullptr;
    {
        // First create a context for the purpose of getting access to wglChoosePixelFormatARB / wglCreateContextAttribsARB.
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 16;
        int pf = ChoosePixelFormat(hDC, &pfd);
        if (pf == 0)
            OnError(L"Failed to choose pixel format.");

        if (!SetPixelFormat(hDC, pf, &pfd))
            OnError(L"Failed to set pixel format.");

        HGLRC context = wglCreateContext(hDC);
        if (context == 0)
            OnError(L"wglCreateContextfailed failed.");

        if (!wglMakeCurrent(hDC, context))
            OnError(L"wglMakeCurrent failed.");

        wglChoosePixelFormatARBFunc = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        wglCreateContextAttribsARBFunc = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

        wglDeleteContext(context);

        if (wglChoosePixelFormatARBFunc == nullptr || wglCreateContextAttribsARBFunc == nullptr)
            OnError(L"wglChoosePixelFormatARB and/or wglCreateContextAttribsARB missing.");
    }

    // Now create the real context that we will be using.
    const int iAttributes[] =
    {
        // WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, 16,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_TRUE,
        0, 0
    };

    const float fAttributes[] = { 0, 0 };
    UINT  numFormats = 0;
    int   pf = 0;
    if (!wglChoosePixelFormatARBFunc(hDC, iAttributes, fAttributes, 1, &pf, &numFormats))
        OnError(L"wglChoosePixelFormatARBFunc failed.");

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    if (!SetPixelFormat(hDC, pf, &pfd))
        OnError(L"SetPixelFormat failed.");

#ifdef _DEBUG
    bool UseDebugContext = true;
#else
    bool UseDebugContext = false;
#endif

    // Crete context attributes.
    GLint attribs[16];
    {
        int attribCount = 0;
        if (UseDebugContext)
        {
            attribs[attribCount++] = WGL_CONTEXT_FLAGS_ARB;
            attribs[attribCount++] = WGL_CONTEXT_DEBUG_BIT_ARB;
        }

        attribs[attribCount++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
        attribs[attribCount++] = 3;

        attribs[attribCount++] = WGL_CONTEXT_MINOR_VERSION_ARB;
        attribs[attribCount++] = 0;

        attribs[attribCount++] = GL_CONTEXT_PROFILE_MASK;
        attribs[attribCount++] = GL_CONTEXT_CORE_PROFILE_BIT;

        attribs[attribCount] = 0;
    }

    context = wglCreateContextAttribsARBFunc(hDC, 0, attribs);
    if (!wglMakeCurrent(hDC, context))
        OnError(L"wglMakeCurrent failed.");
       
    return context;
}
#endif


HRESULT ResizeBuffers(int width, int height)//bool prolog, bool resize, bool epilog)
{
    if (g_immediateContext == nullptr)
        return S_OK;

    if (g_depthStencilView != nullptr)
    {
        g_depthStencilView->Release();
        g_depthStencilView = nullptr;
    }

    if (g_depthStencilTexture != nullptr)
    {
        g_depthStencilTexture->Release();
        g_depthStencilTexture = nullptr;
    }

    if (g_renderTargetView != nullptr)
    {
        g_renderTargetView->Release();
        g_renderTargetView = nullptr;
    }

    // Resize swapchain.
    HRESULT hr = g_swapChain->ResizeBuffers(1, width/*g_windowWidth*/, height/*g_windowHeight*/, g_renderTargetViewFormat, 0);
    if (FAILED(hr))
    {
        //LogPrint(L"Error while resizing swapchain (%s).\n", GetHRESULTString(hr));
        return hr;
    }
        
    // Get swapchain buffer.
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
    if (FAILED(hr))
    {
        //LogPrint(L"Error while getting swapchain buffer (%s).\n", GetHRESULTString(hr));
        return hr;
    }

    // Create a render target view.
    hr = g_device->CreateRenderTargetView(pBackBuffer, nullptr, &g_renderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
    {
        //LogPrint(L"Error while creating rendertarget view (%s).\n", GetHRESULTString(hr));
        return hr;
    }

    D3D11_TEXTURE2D_DESC depthStencilDesc;
    depthStencilDesc.Width              = width;
    depthStencilDesc.Height             = height;
    depthStencilDesc.MipLevels          = 1;
    depthStencilDesc.ArraySize          = 1;
    depthStencilDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count   = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage              = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags     = 0; 
    depthStencilDesc.MiscFlags          = 0;

    //Create the Depth/Stencil View
    g_device->CreateTexture2D(&depthStencilDesc, NULL, &g_depthStencilTexture);
    g_device->CreateDepthStencilView(g_depthStencilTexture, NULL, &g_depthStencilView);

    // Set render target and depth stencil.
    g_immediateContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);

    return S_OK;
}

HRESULT InitializeD3D11(HWND hWnd)
{
    HRESULT hr = S_OK;

    // Get window size.
    RECT rc;
    GetClientRect(hWnd, &rc);
    const UINT width  = rc.right  - rc.left;
    const UINT height = rc.bottom - rc.top;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };

    const int driverTypeCount = ARRAYSIZE(driverTypes);

    const D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        //D3D_FEATURE_LEVEL_10_1,
        //D3D_FEATURE_LEVEL_10_0,
    };

    const int featureLevelCount = ARRAYSIZE(featureLevels);

    for (int i = 0; i < driverTypeCount; i++)
    {
        g_driverType = driverTypes[i];

        // Create device.
        hr = D3D11CreateDevice
        (
            nullptr,
            g_driverType,
            nullptr,
            createDeviceFlags,
            featureLevels,
            featureLevelCount,
            D3D11_SDK_VERSION,
            &g_device,
            &g_featureLevel,
            &g_immediateContext
        );

        // DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
        if (hr == E_INVALIDARG)
        {
            hr = D3D11CreateDevice
            (
                nullptr,
                g_driverType,
                nullptr,
                createDeviceFlags,
                &featureLevels[1],
                featureLevelCount - 1,
                D3D11_SDK_VERSION,
                &g_device,
                &g_featureLevel,
                &g_immediateContext
            );
        }

        if (SUCCEEDED(hr))
            break;
    }

    if (FAILED(hr))
    {
        //LogPrint(L"Could not find a Direct3D11 device.\n");
        return hr;
    }

    // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    IDXGIFactory1* dxgiFactory = nullptr;
    {
        IDXGIDevice* dxgiDevice = nullptr;
        hr = g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (SUCCEEDED(hr))
        {
            IDXGIAdapter* adapter = nullptr;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr))
            {
                hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
                //if (FAILED(hr))
                    //LogPrint(L"Error while getting DXGIFactory from IDXGIAdapter (%s).\n", GetHRESULTString(hr));
                adapter->Release();
            }
            else
            {
                //LogPrint(L"Error while getting IDXGIAdapter from IDXGIDevice (%s).\n", GetHRESULTString(hr));
            }
            dxgiDevice->Release();
        }
        else
        {
            //LogPrint(L"Error getting DXGIDevice from ID3D11Device (%s).\n", GetHRESULTString(hr));
        }

        if (FAILED(hr))
            return hr;
    }

    // Create swap chain
    {
        IDXGIFactory2* dxgiFactory2 = nullptr;
        hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
        if (dxgiFactory2)
        {
            // DirectX 11.1 or later
            hr = g_device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_device1));
            if (SUCCEEDED(hr))
            {
                g_immediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_immediateContext1));
            }
            else
            {
                //LogPrint(L"Error getting ID3D11DeviceContext1 from ID3D11DeviceContext (%s).\n", GetHRESULTString(hr));
            }

            DXGI_SWAP_CHAIN_DESC1 sd = {};
            sd.Width              = width;
            sd.Height             = height;
            sd.Format             = g_renderTargetViewFormat;
            sd.SampleDesc.Count   = 1;
            sd.SampleDesc.Quality = 0;
            sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount        = 1;

            hr = dxgiFactory2->CreateSwapChainForHwnd(g_device, hWnd, &sd, nullptr, nullptr, &g_swapChain1);
            if (SUCCEEDED(hr))
            {
                hr = g_swapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g_swapChain));
                //if (FAILED(hr))
                    //LogPrint(L"Error getting IDXGISwapChain from IDXGISwapChain1 (%s).\n", GetHRESULTString(hr));
            }
            else
            {
                //LogPrint(L"Error creating IDXGISwapChain1 (%s).\n", GetHRESULTString(hr));
            }

            dxgiFactory2->Release();
        }
        else
        {
            // DirectX 11.0 systems
            DXGI_SWAP_CHAIN_DESC sd = {};
            sd.BufferCount                        = 1;
            sd.BufferDesc.Width                   = width;
            sd.BufferDesc.Height                  = height;
            sd.BufferDesc.Format                  = g_renderTargetViewFormat;
            sd.BufferDesc.RefreshRate.Numerator   = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow                       = hWnd;
            sd.SampleDesc.Count                   = 1;
            sd.SampleDesc.Quality                 = 0;
            sd.Windowed                           = TRUE;

            hr = dxgiFactory->CreateSwapChain(g_device, &sd, &g_swapChain);
            //if (FAILED(hr))
                //LogPrint(L"Error creating IDXGISwapChain (%s).\n", GetHRESULTString(hr));
        }

        // Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
        dxgiFactory->MakeWindowAssociation(hWnd, 0);// DXGI_MWA_NO_ALT_ENTER);

        dxgiFactory->Release();

        if (FAILED(hr))
            return hr;
    }

    // Update render-target view.
    hr = ResizeBuffers(width, height);//false, false, true);
    if (FAILED(hr))
        return hr;

    return S_OK;
}

void InitializeCNSDK(HWND hWnd)
{
    // Initialize SDK.
    g_sdk = leia::sdk::CreateLeiaSDK();
    leia::PlatformInitArgs pia;
    g_sdk->InitializePlatform(pia);
    g_sdk->Initialize(nullptr);

    // Initialize interlacer.
    leia::sdk::ThreadedInterlacerInitArgs interlacerInitArgs = {};
    interlacerInitArgs.useMegaTextureForViews = true;
    interlacerInitArgs.graphicsAPI = leia::sdk::GraphicsAPI::D3D11;
    g_interlacer = g_sdk->CreateNewThreadedInterlacer(interlacerInitArgs);
    g_interlacer->InitializeD3D11(g_immediateContext, leia::sdk::eLeiaTaskResponsibility::SDK, leia::sdk::eLeiaTaskResponsibility::SDK, leia::sdk::eLeiaTaskResponsibility::SDK);

    // Initialize interlacer GUI.
    leia::sdk::DebugMenuInitArgs debugMenuInitArgs;
    debugMenuInitArgs.gui.surface = hWnd;
    debugMenuInitArgs.gui.d3d11Device = g_device;
    debugMenuInitArgs.gui.d3d11DeviceContext = g_immediateContext;
    debugMenuInitArgs.gui.graphicsAPI = leia::sdk::GuiGraphicsAPI::D3D11;
    g_interlacer->InitializeGui(debugMenuInitArgs);

    // Set stereo sliding mode.
    g_interlacer->SetInterlaceMode(leia::sdk::eLeiaInterlaceMode::StereoSliding);
    const int numViews = g_interlacer->GetNumViews();
    if (numViews != 2)
        OnError(L"Unexpected number of views");

    // Have to init this after a glContext is created but before we make any calls to OpenGL
    g_interlacer->InitOnDesiredThread();
}

void LoadScene()
{
    if (g_demoMode == eDemoMode::Spinning3DCube)
    {
        const int vertexCount = 8;
        const int indexCount = 36;

        // XYZ|RGB
        const VERTEX vertices[vertexCount] = {
            // Front face
             100.0f,  100.0f,  100.0f, 1.0f, 0.4f, 0.6f,
            -100.0f,  100.0f,  100.0f, 1.0f, 0.9f, 0.2f,
            -100.0f, -100.0f,  100.0f, 0.7f, 0.3f, 0.8f,
             100.0f, -100.0f,  100.0f, 1.0f, 0.3f, 1.0f,

            // Back face
             100.0f,  100.0f, -100.0f, 0.2f, 0.6f, 1.0f,
            -100.0f,  100.0f, -100.0f, 0.6f, 1.0f, 0.4f,
            -100.0f, -100.0f, -100.0f, 0.6f, 0.8f, 0.8f,
             100.0f, -100.0f, -100.0f, 0.4f, 0.8f, 0.8f,
        };

        const unsigned short indices[indexCount] = {
            0, 1, 2, // Front
            2, 3, 0,
            0, 3, 7, // Right
            7, 4, 0,
            2, 6, 7, // Bottom
            7, 3, 2,
            1, 5, 6, // Left
            6, 2, 1,
            4, 7, 6, // Back
            6, 5, 4,
            5, 1, 0, // Top
            0, 4, 5,
        };

        // Create vertex buffer.
        {
            // Format = XYZ|RGB
            const int vertexSize = 6 * sizeof(float);

            D3D11_BUFFER_DESC bd = {};
            bd.Usage          = D3D11_USAGE_DEFAULT;
            bd.ByteWidth      = vertexCount * vertexSize;
            bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = 0;

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = vertices;

            HRESULT hr = g_device->CreateBuffer(&bd, &initData, &g_vertexBuffer);
            if (FAILED(hr))
            {
                OnError(L"Error creating vertex buffer");
                return;
            }
        }

        // Create index buffer.
        {
            // Format = int
            const int ibsize = indexCount * sizeof(int);

            D3D11_BUFFER_DESC bd = {};
            bd.Usage          = D3D11_USAGE_DEFAULT;
            bd.ByteWidth      = ibsize;
            bd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;

            // Input data is right-handed, so we reverse the triangles here.
            int* indicesLH = new int[indexCount];
            for (int i = 0; i < indexCount; i += 3)
            {
                indicesLH[i] = indices[i];
                indicesLH[i + 1] = indices[i + 2];
                indicesLH[i + 2] = indices[i + 1];
            }

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = indicesLH;

            HRESULT hr = g_device->CreateBuffer(&bd, &initData, &g_indexBuffer);
            if (FAILED(hr))
            {
                OnError(L"Error creating index buffer");
                return;
            }

            delete[] indicesLH;
        }        

        {
            // Round up to 16 bytes.
            int shaderUniformBufferSizeRounded = (sizeof(CONSTANTBUFFER) + 15) & ~15;

            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth           = shaderUniformBufferSizeRounded;
            bd.Usage               = D3D11_USAGE_DEFAULT;
            bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags      = 0;
            bd.MiscFlags           = 0;
            bd.StructureByteStride = 0;

            // Create the buffer.
            HRESULT hr = g_device->CreateBuffer(&bd, NULL, &g_shaderConstantBuffer);
            if (FAILED(hr))
            {
                OnError(L"Failed to create constant buffer");
                return;
            }
        }

        const char* vertexShaderText = 
            "struct VSInput\n"
            "{\n"
            "    float3 Pos : POSITION;\n"
            "    float3 Col : COLOR;\n"
            "};\n"
            "struct PSInput\n"
            "{\n"
            "    float4 Pos : SV_POSITION;\n"
            "    float3 Col : COLOR;\n"
            "};\n"
            "cbuffer ConstantBufferData : register(b0)\n"
            "{\n"
            "    float4x4 transform;\n"
            "};\n"
            "PSInput VSMain(VSInput input)\n"
            "{\n"
            "    PSInput output = (PSInput)0;\n"
            "    output.Pos = mul(transform, float4(input.Pos, 1.0f));\n"
            "    output.Col = input.Col;\n"
            "    return output;\n"
            "}\n";

	    const char* pixelShaderText =
            "struct PSInput\n"
            "{\n"
            "    float4 Pos : SV_POSITION;\n"
            "    float3 Col : COLOR;\n"
            "};\n"
            "float4 PSMain(PSInput input) : SV_Target0\n"
            "{\n"
            "    return float4(input.Col, 1);\n"
            "};\n";

        // Compile the vertex shader
        ID3DBlob* pVSBlob = nullptr;
        ID3DBlob* pVSErrors = nullptr;
        HRESULT hr = D3DCompile(vertexShaderText, strlen(vertexShaderText), NULL, NULL, NULL, "VSMain", "vs_5_0", 0, 0, &pVSBlob, &pVSErrors);
        if (FAILED(hr))
        {
            std::string errorMsg;
            if (pVSErrors != nullptr)
                errorMsg.append((char*)pVSErrors->GetBufferPointer(), pVSErrors->GetBufferSize());
            OnError(L"Failed to compile vertex shader");
            return;
        }

        // Create the vertex shader
        hr = g_device->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &g_vertexShader);
        if (FAILED(hr))
        {
            SAFE_RELEASE(pVSBlob);
            OnError(L"Failed to create vertex shader");
            return;
        }

        // Define the input layout
        const D3D11_INPUT_ELEMENT_DESC layoutElements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        const int layoutElementCount = ARRAYSIZE(layoutElements);

        // Create the input layout        
        hr = g_device->CreateInputLayout(layoutElements, layoutElementCount, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &g_inputLayout);
        SAFE_RELEASE(pVSBlob);
        if (FAILED(hr))
        {
            OnError(L"Failed to create vertex layout");
            return;
        }

        // Compile the pixel shader
        ID3DBlob* pPSBlob = nullptr;
        ID3DBlob* pPSErrors = nullptr;
        hr = D3DCompile(pixelShaderText, strlen(pixelShaderText), NULL, NULL, NULL, "PSMain", "ps_5_0", 0, 0, &pPSBlob, &pPSErrors);
        if (FAILED(hr))
        {
            std::string errorMsg;
            if (pPSErrors != nullptr)
                errorMsg.append((char*)pPSErrors->GetBufferPointer(), pPSErrors->GetBufferSize());
            OnError(L"Failed to compile pixel shader");
            return;
        }

        // Create the pixel shader
        hr = g_device->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &g_pixelShader);
        SAFE_RELEASE(pPSBlob);
        if (FAILED(hr))
        {
            OnError(L"Failed to create pixel shader");
            return;
        }
    }
    else if (g_demoMode == eDemoMode::StereoImage)
    {
        // Load stereo image.
        int width = 0;
        int height = 0;
        GLint format = 0;
        char* data = nullptr;
        int dataSize = 0;
        ReadTGA("StereoBeerGlass.tga", width, height, format, data, dataSize);

        // D3D11 doesn't support RGB textures, so expand initial data from RGB->RGBA.
        unsigned char* convertedInitialData = nullptr;
        {
            convertedInitialData = new unsigned char[width * height * 4];

            const unsigned char* pSrc = (const unsigned char*)data;
                  unsigned char* pDst = (unsigned char*)convertedInitialData;

            for (int i = 0; i < width * height; i++)
            {
                pDst[0] = pSrc[2];
                pDst[1] = pSrc[1];
                pDst[2] = pSrc[0];
                pDst[3] = 255;

                pDst += 4;
                pSrc += 3;
            }
        }

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem          = convertedInitialData;
        initData.SysMemPitch      = width * 4;
        initData.SysMemSlicePitch = height * initData.SysMemPitch;

        // Create texture.
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width            = width;
        textureDesc.Height           = height;
        textureDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.MipLevels        = 1;
        textureDesc.ArraySize        = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage            = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = g_device->CreateTexture2D(&textureDesc, &initData, &g_imageTexture);
        if (FAILED(hr))
        {
            OnError(L"Failed to create stereo image texture");
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format                    = textureDesc.Format;
        SRVDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels       = 1;

        hr = g_device->CreateShaderResourceView(g_imageTexture, &SRVDesc, &g_imageShaderResourceView);
        if (FAILED(hr))
        {
            OnError(L"Failed to create stereo image shader resource view");
            return;
        }
    }
}

void InitializeOffscreenFrameBuffer()
{    
    // Create a single double-wide offscreen framebuffer. 
    // When rendering, we will do two passes, like a typical VR application.
    // On pass 1 we render to the left and on pass 2 we render to the right.
    
    // Use Leia's pre-defined view size (you can use a different size to suit your application).
    const int width  = g_sdk->GetViewWidth() * 2;
    const int height = g_sdk->GetViewHeight();

    // Create render-target.
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width            = width;
    textureDesc.Height           = height;
    textureDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.MipLevels        = 1;
    textureDesc.ArraySize        = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage            = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_offscreenTexture);
    if (FAILED(hr))
    {
        OnError(L"Failed to create offscreen texture");
        return;
    }

    // Create shader view.        
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format                    = textureDesc.Format;
        SRVDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels       = 1;

        hr = g_device->CreateShaderResourceView(g_offscreenTexture, &SRVDesc, &g_offscreenShaderResourceView);
        if (FAILED(hr))
        {
            OnError(L"Failed to create shader resource view");
            return;
        }
    }

    // Create render-target view.        
    {
        D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
        RTVDesc.Format             = textureDesc.Format;
        RTVDesc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
        RTVDesc.Texture2D.MipSlice = 0;
        hr = g_device->CreateRenderTargetView(g_offscreenTexture, &RTVDesc, &g_offscreenRenderTargetView);
        if (FAILED(hr))
        {
            OnError(L"Failed to create render-target view");
            return;
        }
    }
                
    // Create depth texture.
    textureDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
    textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_offscreenDepthTexture);
    if (FAILED(hr))
    {
        OnError(L"Failed to create depth texture");
        return;
    }

    // Create depth view.
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
        DSVDesc.Format             = textureDesc.Format;
        DSVDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        DSVDesc.Texture2D.MipSlice = 0;
        hr = g_device->CreateDepthStencilView(g_offscreenDepthTexture, &DSVDesc, &g_offscreenDepthStencilView);
        if (FAILED(hr))
        {
            OnError(L"Failed to create depth-stencil view");
            return;
        }
    }
}

void RotateOrientation(mat3f& orientation, float x, float y, float z)
{
    mat3f rx, ry, rz;
    rx.setAxisAngleRotation(vec3f(1.0, 0.0, 0.0), x);
    ry.setAxisAngleRotation(vec3f(0.0, 1.0, 0.0), y);
    rz.setAxisAngleRotation(vec3f(0.0, 0.0, 1.0), z);
    orientation = orientation * (rx * ry * rz);
}

void Render(float elapsedTime) 
{
    const int   viewWidth   = g_sdk->GetViewWidth();
    const int   viewHeight  = g_sdk->GetViewHeight();
    const float aspectRatio = (float)viewWidth / (float)viewHeight;

    if (g_demoMode == eDemoMode::StereoImage)
    {
        // Clear backbuffer to green.
        const FLOAT color[4] = {0.0f, 0.4f, 0.0f, 1.0f};
        g_immediateContext->ClearRenderTargetView(g_renderTargetView, color);
        g_immediateContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // Perform interlacing.
        g_interlacer->SetSourceViewsSize(viewWidth, viewHeight, true);
        g_interlacer->DoPostProcessPicture(g_windowWidth, g_windowHeight, g_imageShaderResourceView, g_renderTargetView);
    }
    else if (g_demoMode == eDemoMode::Spinning3DCube)
    {
        // geometry transform.
        mat4f geometryTransform;
        {
            // Place cube at convergence distance.
            float convergenceDistance = g_sdk->GetConvergenceDistance();
            vec3f geometryPos = vec3f(0, convergenceDistance, 0);

            mat3f geometryOrientation;
            geometryOrientation.setIdentity();
            RotateOrientation(geometryOrientation, 0.1f * elapsedTime, 0.2f * elapsedTime, 0.3f * elapsedTime);
            geometryTransform.create(geometryOrientation, geometryPos);
        }

        // Clear back-buffer to green.
        const FLOAT backBufferColor[4] = { 0.0f, 0.4f, 0.0f, 1.0f };
        g_immediateContext->ClearRenderTargetView(g_renderTargetView, backBufferColor);
        g_immediateContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // Clear offscreen render-target to blue
        const FLOAT offscreenColor[4] = { 0.0f, 0.2f, 0.5f, 1.0f };
        g_immediateContext->ClearRenderTargetView(g_offscreenRenderTargetView, offscreenColor);
        g_immediateContext->ClearDepthStencilView(g_offscreenDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // Render stereo views.
        for (int i = 0; i < 2; i++)
        {
            // Get view offset.
            const glm::vec3 viewOffset = g_interlacer->GetViewOffset(i);

            // Get shear to apply to perspective projection.
            const float convergenceDistance = g_sdk->GetConvergenceDistance();
            const float shearX = -viewOffset.x / convergenceDistance;
            const float shearY = -viewOffset.z / convergenceDistance;

            // Create camera projection with shear.
            mat4f cameraProjection;
            cameraProjection.setPerspective(90.0f * (3.14159f / 180.0f), aspectRatio, 0.01f, 1000.0f);
            cameraProjection[2][0] = cameraProjection[0][0] * shearX;
            cameraProjection[2][1] = cameraProjection[1][1] * shearY;

            // Get camera position (including offset from interlacer).
            vec3f camPos = vec3f(0, 0, 0);
            camPos += vec3f(viewOffset.x, viewOffset.z, viewOffset.y);

            // Get camera direction.
            vec3f camDir = vec3f(0, 1, 0);

            // Get camera transform.
            mat4f cameraTransform;
            cameraTransform.lookAt(camPos, camPos + camDir, vec3f(0.0f, 0.0f, 1.0f));

            // Compute combined matrix.
            const mat4f mvp = cameraProjection * cameraTransform * geometryTransform;

            // Set viewport to render to left, then right.
            D3D11_VIEWPORT viewport = {};
            viewport.TopLeftX = (FLOAT)(i * viewWidth);
            viewport.TopLeftY = 0.0f;
            viewport.Width    = (FLOAT)viewWidth;
            viewport.Height   = (FLOAT)viewHeight;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            g_immediateContext->RSSetViewports(1, &viewport);

            g_immediateContext->OMSetRenderTargets(1, &g_offscreenRenderTargetView, g_offscreenDepthStencilView);

            g_immediateContext->VSSetShader(g_vertexShader, nullptr, 0);
            g_immediateContext->PSSetShader(g_pixelShader, nullptr, 0);
            g_immediateContext->IASetInputLayout(g_inputLayout);

            g_immediateContext->UpdateSubresource(g_shaderConstantBuffer, 0, NULL, &mvp, 0, 0);
            g_immediateContext->VSSetConstantBuffers(0, 1, &g_shaderConstantBuffer);
            g_immediateContext->PSSetConstantBuffers(0, 1, &g_shaderConstantBuffer);

            // Set vertex buffer (XYZ|RGB)
            UINT stride = 6 * sizeof(float);
            UINT offset = 0;
            g_immediateContext->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);

            // Set index buffer.
            g_immediateContext->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // Set primitive topology
            g_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Render.
            g_immediateContext->DrawIndexed(36, 0, 0);
        }    

        // Set viewport.
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width    = (FLOAT)g_windowWidth;
        viewport.Height   = (FLOAT)g_windowHeight;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        g_immediateContext->RSSetViewports(1, &viewport);

        g_immediateContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);

        // Perform interlacing.
        g_interlacer->SetSourceViewsSize(viewWidth, viewHeight, true);
        g_interlacer->SetInterlaceViewTextureAtlas(g_offscreenShaderResourceView);
        g_interlacer->DoPostProcess(g_windowWidth, g_windowHeight, false, g_renderTargetView);
    }
    
    g_swapChain->Present(1, 0);
}

void UpdateWindowTitle(HWND hWnd, double curTime) 
{
    static double prevTime = 0;
    static int frameCount = 0;

    frameCount++;

    if (curTime - prevTime > 0.25) 
    {
        const double fps = frameCount / (curTime - prevTime);

        wchar_t newWindowTitle[128];
        swprintf(newWindowTitle, 128, L"%s (%.1f FPS)", g_windowTitle, fps);
        SetWindowText(hWnd, newWindowTitle);

        prevTime = curTime;
        frameCount = 0;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Allow CNSDK debug menu to see window messages
    if (g_interlacer != nullptr)
    {
        auto io = g_interlacer->ProcessGuiInput(hWnd, message, wParam, lParam);
        if (io.wantCaptureInput)
            return 0;
    }

    switch (message)
    {

    // Handle keypresses.
    case WM_KEYDOWN:
        switch (wParam) {
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;
        }
        break;

    // Keep track of window size.
    case WM_SIZE:
        g_windowWidth = LOWORD(lParam);
        g_windowHeight = HIWORD(lParam);
        ResizeBuffers(g_windowWidth, g_windowHeight);
        PostMessage(hWnd, WM_PAINT, 0, 0);
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // Register window class.
    WNDCLASSEXW wcex;
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CNSDKGETTINGSTARTEDD3D11));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName  = NULL;
    wcex.lpszClassName = g_windowClass;
    wcex.hIconSm       = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    RegisterClassExW(&wcex);

    // Create window.
    HWND hWnd = CreateGraphicsWindow(hInstance);

    // Get DC.
    HDC hDC = GetDC(hWnd);

    // Switch to fullscreen if requested.
    if (g_fullscreen)
        SetFullscreen(hWnd, true);

    // Initialize OpenGL.
    HRESULT hr = InitializeD3D11(hWnd);
    if (FAILED(hr))
        OnError(L"Failed to initialize D3D11");

    // Initialize CNSDK.
    InitializeCNSDK(hWnd);

    // Create our stereo (double-wide) frame buffer.
    if (g_demoMode == eDemoMode::Spinning3DCube)
        InitializeOffscreenFrameBuffer();

    // Prepare everything to draw.
    LoadScene();

    // Show window.
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
    // Enable Leia display backlight.
    g_sdk->SetBacklight(true);

    // Main loop.
    bool finished = false;
    while (!finished)
    {
        // Empty all messages from queue.
        MSG msg = {};
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (WM_QUIT == msg.message)
            {
                finished = true;
                break;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        // Perform app logic.
        if (!finished)
        {
            // Get timing.
            const ULONGLONG  curTick      = GetTickCount64();
            static ULONGLONG prevTick     = curTick;
            const ULONGLONG  elapsedTicks = curTick - prevTick;
            const double     elapsedTime  = (double)elapsedTicks / 1000.0;
            const double     curTime      = (double)curTick / 1000.0;

            // Render.
            Render((float)elapsedTime);

            // Update window title with FPS.
            UpdateWindowTitle(hWnd, curTime);
        }
    }

    // Disable Leia display backlight.
    g_sdk->SetBacklight(false);

    // Cleanup.
    g_sdk->Destroy();
    
    SAFE_RELEASE(g_imageShaderResourceView);
    SAFE_RELEASE(g_imageTexture);
    SAFE_RELEASE(g_pixelShader);
    SAFE_RELEASE(g_inputLayout);
    SAFE_RELEASE(g_vertexShader);
    SAFE_RELEASE(g_shaderConstantBuffer);
    SAFE_RELEASE(g_indexBuffer);
    SAFE_RELEASE(g_vertexBuffer);
    SAFE_RELEASE(g_offscreenDepthStencilView);
    SAFE_RELEASE(g_offscreenDepthTexture);
    SAFE_RELEASE(g_offscreenRenderTargetView);
    SAFE_RELEASE(g_offscreenShaderResourceView);
    SAFE_RELEASE(g_offscreenTexture);
    SAFE_RELEASE(g_depthStencilView);
    SAFE_RELEASE(g_depthStencilTexture);
    SAFE_RELEASE(g_renderTargetView);
    SAFE_RELEASE(g_swapChain1);
    SAFE_RELEASE(g_swapChain);
    SAFE_RELEASE(g_immediateContext1);
    SAFE_RELEASE(g_immediateContext);
    SAFE_RELEASE(g_device1);
    SAFE_RELEASE(g_device);

    return 0;
}