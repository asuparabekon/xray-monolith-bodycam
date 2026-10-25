#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace scope_vslr
{
struct Position
{
    float x, y, z;
};

struct FaceAxes
{
    Position direction;
    Position up;
};

// D3D cubemap face order; world aligned so rotating the camera does not invalidate a capture.
inline constexpr std::array<FaceAxes, 6> face_axes = {{
    {{1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 0, -1}}, {{0, -1, 0}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}}
}};

struct Settings
{
    unsigned resolution = 128;
    float radius = 20.f;
    unsigned faces_per_frame = 2;
};

class Capture
{
public:
    bool create(ID3D11Device* device, Settings settings);
    void begin(Position origin);
    ID3D11RenderTargetView* colour_target() const;
    ID3D11RenderTargetView* distance_target() const;
    ID3D11RenderTargetView* coverage_target() const;
    ID3D11RenderTargetView* direct_target() const;
    // After the current face's four targets were rendered.
    void finish_face();

    unsigned face() const { return next_face_; }
    bool capturing() const { return next_face_ < 6; }
    bool ready() const { return published_ >= 0; }
    Position capture_origin() const { return pending_origin_; }
    Position published_origin() const { return published_origin_; }
    ID3D11ShaderResourceView* colour() const;
    ID3D11ShaderResourceView* distance() const;
    ID3D11ShaderResourceView* coverage() const;
    ID3D11ShaderResourceView* direct() const;
    // The capture published before the current one, kept unchanged so consumers can fade from it.
    bool has_previous() const { return previous_ >= 0; }
    Position previous_origin() const { return previous_origin_; }
    // Null until two captures were published.
    ID3D11ShaderResourceView* previous_colour() const;
    ID3D11ShaderResourceView* previous_coverage() const;
    const Settings& settings() const { return settings_; }
    unsigned texture_bytes() const;

private:
    using Texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>;
    using View = Microsoft::WRL::ComPtr<ID3D11RenderTargetView>;
    using Resource = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
    struct Cube
    {
        Texture texture;
        Resource resource;
        std::array<View, 6> faces;
    };
    // Colour is zero where nothing was drawn, and coverage is one where something was, so their mips
    // are the captured scene and its share of a rough reflection lobe. Distance keeps one level. Direct
    // is colour without the bounce: the next capture bounces it, so bounces do not feed back.
    struct Buffer { Cube colour, distance, coverage, direct; };
    static bool create_cube(ID3D11Device* device, unsigned size, DXGI_FORMAT format, bool mipmapped, Cube& cube);

    Settings settings_;
    // Previous, published and writing generations; a buffer is never written while it is sampled.
    std::array<Buffer, 3> buffers_;
    Position pending_origin_{};
    Position published_origin_{};
    Position previous_origin_{};
    unsigned next_face_ = 6;
    int published_ = -1;
    int previous_ = -1;
    unsigned writing_ = 0;
};
}
