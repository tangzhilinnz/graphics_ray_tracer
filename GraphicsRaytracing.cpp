#include <windows.h>
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>


const int canvasWidth = 800;
const int canvasHeight = 800;
std::vector<DWORD> canvasBuffer;
const float EPSILON = 0.001f;

enum class LightType {
    AMBIENT = 0,
    POINT = 1,
    DIRECTIONAL = 2,
};

struct Vector3 {
    float x, y, z;
};

struct Color {
    unsigned b, g, r;
};

struct Sphere {
    Vector3 center;
    float radius;
    Color color;
    int specular;
};

struct Light {
    LightType ltype;
    float intensity;
    Vector3 position;
};

void PutPixel(int x, int y, Color color) {
    int x_r = canvasWidth / 2 + x;
    int y_r = canvasHeight / 2 - y;

    if (x_r >= 0 && x_r < canvasWidth && y_r >= 0 && y_r < canvasHeight) {
        int offset = x_r + canvasWidth * y_r;
        canvasBuffer[offset] = RGB(color.b, color.g, color.r);
    }
}

void UpdateCanvas(HWND hwnd, HDC hdc, int canvasWidth, int canvasHeight) {
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = canvasWidth;
    bmi.bmiHeader.biHeight = -canvasHeight;  // Negative height to ensure top-down drawing
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // Additional setup if using 32 bits per pixel
    bmi.bmiHeader.biClrUsed = 0;
    bmi.bmiHeader.biClrImportant = 0;

    SetDIBitsToDevice(hdc, 0, 0, canvasWidth, canvasHeight, 0, 0, 0,
        canvasHeight, canvasBuffer.data(), &bmi, DIB_RGB_COLORS);
}


// =============================================================================
//                         Vector3 operating routines
// =============================================================================

float DotProduct(Vector3 v1, Vector3 v2) {
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

Vector3 Subtract(Vector3 v1, Vector3 v2) {
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

// Length of a 3D vector.
float Length(Vector3 v) {
    return std::sqrt(DotProduct(v, v));
}

// Computes k * vec.
Vector3 Multiply(float k, Vector3 v) {
    return { k * v.x, k * v.y, k * v.z };
}

// Computes v1 + v2.
Vector3 Add(Vector3 v1, Vector3 v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

// =============================================================================


// =============================================================================
//                            Color operating routines                
// =============================================================================

// Computes i * color.
Color Multiply(float i, Color c) {
    return { (unsigned)(i * c.b), (unsigned)(i * c.g), (unsigned)(i * c.r) };
}

// Clamps a color to the canonical color range.
Color Clamp(Color c) {
    return {
        min(255, max(0, c.b)),
        min(255, max(0, c.g)),
        min(255, max(0, c.r))
    };
}

// =============================================================================


Vector3 CanvasToViewport(int x, int y) {
    static float viewportSize_x = 1.0f;
    static float viewportSize_y = 1.0f;
    static float projectionPlaneZ = 1.0f;
    return { x * viewportSize_x / canvasWidth,
             y * viewportSize_y / canvasHeight,
             projectionPlaneZ };
}

std::pair<float, float> IntersectRaySphere(Vector3 origin, Vector3 direction,
    const Sphere& sphere) {
    Vector3 oc = Subtract(origin, sphere.center);

    float k1 = DotProduct(direction, direction);
    float k2 = 2 * DotProduct(oc, direction);
    float k3 = DotProduct(oc, oc) - sphere.radius * sphere.radius;

    float discriminant = k2 * k2 - 4 * k1 * k3;
    if (discriminant < 0) {
        return { INFINITY, INFINITY };
    }

    float t1 = (-k2 + std::sqrt(discriminant)) / (2 * k1);
    float t2 = (-k2 - std::sqrt(discriminant)) / (2 * k1);
    return { t1, t2 };
}

float ComputeLighting(Vector3 point, Vector3 normal, Vector3 view,
    const std::vector<Light>& lights, const std::vector<Sphere>& spheres,
    int specular) {
    float intensity = 0.0f;
    float length_n = Length(normal);  // Should be 1.0, but just in case...
    float length_v = Length(view);

    for (size_t i = 0; i < lights.size(); i++) {
        auto& light = lights[i];

        if (light.ltype == LightType::AMBIENT) {
            intensity += light.intensity;
        }
        else {
            Vector3 vec_l;
            float t_max;
            if (light.ltype == LightType::POINT) {
                vec_l = Subtract(light.position, point);
                t_max = 1.0f;
            }
            else {  // Light.DIRECTIONAL
                vec_l = light.position;
                t_max = INFINITY;
            }

            bool is_shadow = false;
            for (const auto& sphere : spheres) {
                auto ts = IntersectRaySphere(point, vec_l, sphere);
                if (EPSILON < ts.first && ts.first < t_max) {
                    is_shadow = true;
                    break;
                }
                if (EPSILON < ts.second && ts.second < t_max) {
                    is_shadow = true;
                    break;
                }
            }
            if (is_shadow) continue;

            // Diffuse reflection
            auto n_dot_l = DotProduct(normal, vec_l);
            if (n_dot_l > 0)
                intensity +=
                    light.intensity * n_dot_l / (length_n * Length(vec_l));

            // Specular reflection
            if (specular >= 0) {
                Vector3 vec_r = 
                    Subtract(Multiply(2.0f * DotProduct(normal, vec_l), normal),
                             vec_l);

                float r_dot_v = DotProduct(vec_r, view);
                if (r_dot_v > 0)
                    intensity += light.intensity *
                        std::pow(r_dot_v / (Length(vec_r) * length_v), specular);
            }
        }
    }

    return intensity;
}

Color TraceRay(Vector3 origin, Vector3 direction, float min_t, float max_t,
    const std::vector<Sphere>& spheres, const std::vector<Light>& lights) {
    float closest_t = INFINITY;
    const Sphere* closestSphere = nullptr;

    for (const auto& sphere : spheres) {
        auto ts = IntersectRaySphere(origin, direction, sphere);
        if (ts.first < closest_t && min_t < ts.first && ts.first < max_t) {
            closest_t = ts.first;
            closestSphere = &sphere;
        }
        if (ts.second < closest_t && min_t < ts.second && ts.second < max_t) {
            closest_t = ts.second;
            closestSphere = &sphere;
        }
    }

    if (closestSphere == nullptr) {
        return { 255, 255, 255 }; // Background color white
    }

    //return closestSphere->color;

    Vector3 point = Add(origin, Multiply(closest_t, direction));
    Vector3 normal = Subtract(point, closestSphere->center);
    normal = Multiply(1.0f / Length(normal), normal);

    auto view = Multiply(-1, direction);
    auto light_intensity = ComputeLighting(point, normal, view, lights, spheres,
                                           closestSphere->specular);
    return Multiply(light_intensity, closestSphere->color);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;

    switch (message) {
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        UpdateCanvas(hwnd, hdc, canvasWidth, canvasHeight);
        EndPaint(hwnd, &ps);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    // Initialize canvas buffer
    canvasBuffer.resize(canvasWidth * canvasHeight);

    // Scene setup
    const Vector3 cameraPosition = { 0, 0, 0 };
    const std::vector<Sphere> spheres = {
        { {0, -1, 3}, 1, {0, 0, 255}, 500 }, // red sphere
        { {2, 0, 4}, 1, {255, 0, 0}, 500 }, // blue sphere
        { {-2, 0, 4 }, 1, { 0, 255, 0}, 10 }, // green sphere
        { {0, -5001, 0}, 5000, {0, 255, 255}, 1000 } // yellow sphere
    };

    // Lights setup
    const std::vector<Light> lights = {
        { LightType::AMBIENT, 0.2f, {INFINITY, INFINITY, INFINITY} },
        { LightType::POINT, 0.6f, {2, 1, 0} },
        { LightType::DIRECTIONAL, 0.2f, {1, 4, 4} }
    };

    // Main rendering loop.
    // the range of the x coordinate is [ –Cw/2, Cw/2 ) and the range of the y
    // coordinate is [ –Ch/2, Ch/2 ).
    for (int x = -canvasWidth / 2; x < canvasWidth / 2; ++x) {
        for (int y = -canvasHeight / 2; y < canvasHeight / 2; ++y) {
            Vector3 direction = CanvasToViewport(x, y);
            Color color = TraceRay(cameraPosition, direction, 1, INFINITY,
                                   spheres, lights);
            PutPixel(x, y, Clamp(color));
        }
    }

    // Register the window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"RaytracerDemo";

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(0, L"RaytracerDemo", L"Raytracer Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, canvasWidth,
        canvasHeight + 40, nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd == nullptr) {
        MessageBox(nullptr, L"Window creation failed", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);

    // Main loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
