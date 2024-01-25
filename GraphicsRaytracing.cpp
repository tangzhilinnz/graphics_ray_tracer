#include <windows.h>
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>


const int CANVAS_WIDTH = 600;
const int CANVAS_HEIGHT = 600;
const int RECURSION_DEPTH = 3; // 0, 1, 2, 3, 5
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

struct Matrix3 {
    std::array<float, 9> matrix_buf;
    Vector3 line0 = { matrix_buf[0], matrix_buf[1], matrix_buf[2] };
    Vector3 line1 = { matrix_buf[3], matrix_buf[4], matrix_buf[5] };
    Vector3 line2 = { matrix_buf[6], matrix_buf[7], matrix_buf[8] };

    Vector3 col0 = { matrix_buf[0], matrix_buf[3], matrix_buf[6] };
    Vector3 col1 = { matrix_buf[1], matrix_buf[4], matrix_buf[7] };
    Vector3 col2 = { matrix_buf[2], matrix_buf[5], matrix_buf[8] };
};

struct Color {
    unsigned b, g, r;
};

const Color BACKGROUND_COLOR = {0, 0, 0};

struct Sphere {
    Vector3 center;
    float radius;
    Color color;
    int specular;
    float reflective; // [0.f, 1.f]
};

struct Light {
    LightType ltype;
    float intensity;
    Vector3 position;
};

void PutPixel(int x, int y, const Color& color) {
    int x_r = CANVAS_WIDTH / 2 + x;
    int y_r = CANVAS_HEIGHT / 2 - y;

    if (x_r >= 0 && x_r < CANVAS_WIDTH && y_r >= 0 && y_r < CANVAS_HEIGHT) {
        int offset = x_r + CANVAS_WIDTH * y_r;
        canvasBuffer[offset] = RGB(color.b, color.g, color.r);
    }
}

void UpdateCanvas(HWND hwnd, HDC hdc, int CANVAS_WIDTH, int CANVAS_HEIGHT) {
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = CANVAS_WIDTH;
    bmi.bmiHeader.biHeight = -CANVAS_HEIGHT;  // Negative height to ensure top-down drawing
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // Additional setup if using 32 bits per pixel
    bmi.bmiHeader.biClrUsed = 0;
    bmi.bmiHeader.biClrImportant = 0;

    SetDIBitsToDevice(hdc, 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, 0, 0, 0,
        CANVAS_HEIGHT, canvasBuffer.data(), &bmi, DIB_RGB_COLORS);
}


// =============================================================================
//                         Vector3 operating routines
// =============================================================================

float DotProduct(const Vector3& v1, const Vector3& v2) {
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

// Length of a 3D vector.
float Length(const Vector3& v) {
    return std::sqrt(DotProduct(v, v));
}

// Computes k * vec.
Vector3 Multiply(float k, const Vector3& v) {
    return { k * v.x, k * v.y, k * v.z };
}

// Computes v1 + v2.
Vector3 Add(const Vector3& v1, const Vector3& v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

// =============================================================================
//                            Color operating routines                
// =============================================================================

// Computes i * color.
Color Multiply(float i, const Color& c) {
    return { (unsigned)(i * c.b), (unsigned)(i * c.g), (unsigned)(i * c.r) };
}

// Computes color1 + color2.
Color Add(const Color& c1, const Color& c2) {
    return { c1.b + c2.b, c1.g + c2.g, c1.r + c2.r };
}

// Clamps a color to the canonical color range.
Color Clamp(const Color& c) {
    return {
        min(255, max(0, c.b)),
        min(255, max(0, c.g)),
        min(255, max(0, c.r))
    };
}

// =============================================================================
//                            Matrix operating routines                
// =============================================================================

// Multiplies a matrix and a vector.
Vector3 MultiplyMV(const Matrix3& mat, const Vector3& vec) {
    Vector3 result = { 0.f, 0.f, 0.f };

    result.x = DotProduct(mat.line0, vec);
    result.y = DotProduct(mat.line1, vec);
    result.z = DotProduct(mat.line2, vec);

    return result;
}


Vector3 ReflectRayDirection(const Vector3& ray, const Vector3& normal) {
    return Subtract(Multiply(2 * DotProduct(ray, normal), normal), ray);
}

Vector3 CanvasToViewport(int canvas_x, int canvas_y) {
    static float viewportSize_x = 1.0f;
    static float viewportSize_y = 1.0f;
    static float projectionPlaneZ = 1.0f;
    return { canvas_x * viewportSize_x / CANVAS_WIDTH,
             canvas_y* viewportSize_y / CANVAS_HEIGHT,
             projectionPlaneZ };
}

std::pair<float, float> IntersectRaySphere(const Vector3& origin,
    const Vector3& direction, const Sphere& sphere) {
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

float ComputeLighting(const Vector3& point, const Vector3& normal,
    const Vector3& view, const std::vector<Light>& lights,
    const std::vector<Sphere>& spheres, int specular, const Sphere* sphere_tag) {
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
                if (sphere_tag == &sphere)
                    continue;

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
                Vector3 vec_r = ReflectRayDirection(vec_l, normal);

                float r_dot_v = DotProduct(vec_r, view);
                if (r_dot_v > 0)
                    intensity += light.intensity *
                    std::pow(r_dot_v / (Length(vec_r) * length_v), specular);
            }
        }
    }

    return intensity;
}

// color = (1 - r) * local_color + r * reflected_color || color = background_color
// reflected_color = (1 - r1) * local_color1 + r1 * reflected_color1 || reflected_color = background_color
// reflected_color1 = (1 - r2) * local_color2 + r2 * reflected_color2 || reflected_color1 = background_color
// reflected_color2 = (1 - r3) * local_color3 + r3 * reflected_color3 || reflected_color2 = background_color
// ... ... ... ...
Color TraceRay(const Vector3& origin, const Vector3& direction, float min_t,
    float max_t, const std::vector<Sphere>& spheres,
    const std::vector<Light>& lights, int depth, const Sphere* sphere_tag) {
    float closest_t = INFINITY;
    const Sphere* closest_sphere = nullptr;

    for (const auto& sphere : spheres) {
        if (sphere_tag == &sphere)
            continue;

        auto ts = IntersectRaySphere(origin, direction, sphere);
        if (ts.first < closest_t && min_t < ts.first && ts.first < max_t) {
            closest_t = ts.first;
            closest_sphere = &sphere;
        }
        if (ts.second < closest_t && min_t < ts.second && ts.second < max_t) {
            closest_t = ts.second;
            closest_sphere = &sphere;
        }
    }

    if (closest_sphere == nullptr)
        return BACKGROUND_COLOR; // Background color white

    //return closest_sphere->color;

    Vector3 point = Add(origin, Multiply(closest_t, direction));
    Vector3 normal = Subtract(point, closest_sphere->center);
    normal = Multiply(1.0f / Length(normal), normal);

    auto view = Multiply(-1, direction);
    auto light_intensity = ComputeLighting(point, normal, view, lights, spheres,
        closest_sphere->specular, closest_sphere);
    // return Multiply(light_intensity, closest_sphere->color);
    auto local_color = Multiply(light_intensity, closest_sphere->color);

    if (closest_sphere->reflective <= 0 || depth <= 0)
        return local_color;

    auto reflected_ray_direction = ReflectRayDirection(view, normal);
    auto reflected_color = TraceRay(point, reflected_ray_direction, EPSILON,
                                    INFINITY, spheres, lights, depth - 1,
                                    closest_sphere);

    return Add(Multiply(1 - closest_sphere->reflective, local_color),
               Multiply(closest_sphere->reflective, reflected_color));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;

    switch (message) {
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        UpdateCanvas(hwnd, hdc, CANVAS_WIDTH, CANVAS_HEIGHT);
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
    canvasBuffer.resize(CANVAS_WIDTH * CANVAS_HEIGHT);

    // Scene setup
    const Vector3 CAMERA_POSITION = { 3, 0, 1 };

    const Matrix3 CAMERA_ROTATION = { 
        0.7071f, 0.f, -0.7071f,
        0.f, 1.f, 0.f,
        0.7071f, 0, 0.7071f
    };

    const std::vector<Sphere> SPHERES = {
        { {0, -1, 3}, 1, {0, 0, 255}, 500, 0.2f }, // red sphere
        { {2, 0, 4}, 1, {255, 0, 0}, 500, 0.3f }, // blue sphere
        { {-2, 0, 4 }, 1, { 0, 255, 0}, 10, 0.4f }, // green sphere
        { {0, -5001, 0}, 5000, {0, 255, 255}, 1000, 0.5f } // yellow sphere
    };

    // Lights setup
    const std::vector<Light> LIGHTS = {
        { LightType::AMBIENT, 0.2f, {INFINITY, INFINITY, INFINITY} },
        { LightType::POINT, 0.6f, {2, 1, 0} },
        { LightType::DIRECTIONAL, 0.2f, {1, 4, 4} }
    };

    // Main rendering loop.
    // the range of the x coordinate is [ –Cw/2, Cw/2 ) and the range of the y
    // coordinate is [ –Ch/2, Ch/2 ).
    for (int x = -CANVAS_WIDTH / 2; x < CANVAS_WIDTH / 2; ++x) {
        for (int y = -CANVAS_HEIGHT / 2; y < CANVAS_HEIGHT / 2; ++y) {
            Vector3 direction = CanvasToViewport(x, y);
            direction = MultiplyMV(CAMERA_ROTATION, direction);
            Color color = TraceRay(CAMERA_POSITION, direction, 1, INFINITY,
                SPHERES, LIGHTS, RECURSION_DEPTH, nullptr);
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
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CANVAS_WIDTH,
        CANVAS_HEIGHT + 40, nullptr, nullptr, wc.hInstance, nullptr);

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
