#pragma once

#include <wrl/client.h>

class IRenderable;

// The player's mirror image for the scope glass (gamedata/shaders/r3/scope_glass_head.h): the player's body
// model, seen from a camera in front of the eye looking back, drawn by the reflection capture
// (ScopeVslr::capture_mirror) into one of two images that blend in turn.
namespace scope_vslr
{
inline constexpr LPCSTR mirror_texture = "$user$vslr_mirror";
inline constexpr LPCSTR mirror_previous_texture = "$user$vslr_mirror_previous";
// The image's far plane: the body and nothing behind it.
inline constexpr float mirror_far_m = 2.f;

class Mirror
{
public:
    bool create(unsigned size);
    // Lets go of the images (the texture aliases would otherwise keep them alive).
    void release();
    bool available() const { return images_[0].view != nullptr; }
    // The body model's transform with its eyes moved to eye.
    Fmatrix& place_body(IRenderable& viewer, const Fvector& eye, const Fvector& forward, const Fvector& up);
    // An image's camera, projection, and weight over the one before (1 when that one is not continuous).
    struct Pass
    {
        Fmatrix view, projection;
        float weight;
    };
    // Starts an image for the eye and view axes.
    Pass begin(const Fvector& eye, const Fvector& forward, const Fvector& up, bool aiming);
    ID3D11RenderTargetView* target() const { return images_[1 - current_].target.Get(); }
    // After the image was drawn and its target unbound: its mips, and the image published for the glass.
    void finish();
    // The camera for this view's eye and axes (forward.w: tangent of the half field of view); false while
    // no image is current.
    bool view(const Fvector& eye, const Fvector& view_forward, const Fvector& view_up, Fvector4& origin,
        Fvector4& right, Fvector4& up, Fvector4& forward, Fvector4& head) const;
    // r__scope_vslr_debug 2: the newest image once per process, to the logs folder.
    void dump();

private:
    struct Image
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };
    // Written in turn, each blended over the other; current_ is the newest.
    Image images_[2];
    unsigned current_ = 0;
    ref_texture alias_, previous_alias_;
    u32 frame_ = u32(-1);
    u32 next_report_ms_ = 0;
    Fmatrix body_;
    float tan_ = 1.f; // tangent of the newest image's half field of view
};
}
