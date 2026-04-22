#pragma once

#include <obs-module.h>
#include <string>
#include <mutex>
#include <atomic>
#include <QImage>
#include <QPainter>
#include <QFont>

struct OverlaySettings {
    Qt::Alignment alignment    = Qt::AlignCenter;
    bool          full_screen  = false;
    QString       font_family  = "Arial";
    int           base_font_size = 48;
};

class HomeInRenderer {
public:
    HomeInRenderer();
    ~HomeInRenderer();

    static void Register();

    void SetText(const std::string& text);
    void Render(gs_effect_t* effect);
    void UpdateSettings(const OverlaySettings& settings);

    // FIX #14: Must be called from the Qt main thread (e.g. from the dock's
    // level_timer). Runs QPainter to pre-render the QImage with the current
    // alpha baked in, then sets image_ready so Render() can upload to GPU.
    void PrepareTexture();

    uint32_t GetWidth()  const { return width; }
    uint32_t GetHeight() const { return height; }

private:
    // GPU-only upload — safe to call from OBS render thread.
    void UpdateTexture();

    std::string current_text;
    std::string pending_text;
    std::mutex  text_mutex;

    // FIX #14: Separates Qt paint work (PrepareTexture, Qt thread) from
    // GPU upload (UpdateTexture, OBS render thread).
    QImage              pending_image;
    std::atomic<bool>   image_ready{false};
    std::atomic<bool>   dirty{false};

    OverlaySettings settings;

    float current_alpha  = 0.0f;
    float fade_speed     = 0.04f;
    bool  is_fading_out  = false;

    gs_texture_t* texture = nullptr;
    uint32_t width  = 1920;
    uint32_t height = 1080;
};

extern HomeInRenderer* GetActiveRenderer();
