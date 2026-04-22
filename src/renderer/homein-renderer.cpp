#include "homein-renderer.hpp"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <mutex>
#include <atomic>

// FIX #16: Use atomic pointer to eliminate data race between OBS source
// management thread (create/destroy) and the Qt dock thread (GetActiveRenderer).
static std::atomic<HomeInRenderer*> g_active_renderer{nullptr};

HomeInRenderer* GetActiveRenderer() {
    return g_active_renderer.load(std::memory_order_acquire);
}

static const char* homein_renderer_get_name(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("Home Indeed Overlay");
}

static void* homein_renderer_create(obs_data_t* settings, obs_source_t* context) {
    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(context);
    auto renderer = new HomeInRenderer();
    g_active_renderer.store(renderer, std::memory_order_release);
    return renderer;
}

static void homein_renderer_destroy(void* data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    HomeInRenderer* expected = renderer;
    // Only null the global if it still points to this instance
    g_active_renderer.compare_exchange_strong(expected, nullptr,
                                               std::memory_order_acq_rel);
    delete renderer;
}

static void homein_renderer_video_render(void* data, gs_effect_t* effect) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    if (renderer) renderer->Render(effect);
}

static uint32_t homein_renderer_get_width(void* data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    return renderer ? renderer->GetWidth() : 1920;
}

static uint32_t homein_renderer_get_height(void* data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    return renderer ? renderer->GetHeight() : 1080;
}

void HomeInRenderer::Register() {
    struct obs_source_info info = {};
    info.id           = "homein_overlay_source";
    info.type         = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name     = homein_renderer_get_name;
    info.create       = homein_renderer_create;
    info.destroy      = homein_renderer_destroy;
    info.video_render = homein_renderer_video_render;
    info.get_width    = homein_renderer_get_width;
    info.get_height   = homein_renderer_get_height;
    obs_register_source(&info);
}

HomeInRenderer::HomeInRenderer() {}

HomeInRenderer::~HomeInRenderer() {
    obs_enter_graphics();
    if (texture) gs_texture_destroy(texture);
    obs_leave_graphics();
}

void HomeInRenderer::SetText(const std::string& text) {
    std::lock_guard<std::mutex> lock(text_mutex);
    if (current_text != text) {
        pending_text  = text;
        is_fading_out = true;
    }
}

void HomeInRenderer::UpdateSettings(const OverlaySettings& new_settings) {
    std::lock_guard<std::mutex> lock(text_mutex);
    settings = new_settings;
    dirty    = true;
}

// FIX #14: PrepareTexture() runs Qt QPainter on the Qt main thread (called
// from the dock's level_timer). It pre-renders the QImage with alpha already
// baked in for the current fade level, then signals the GPU upload via image_ready.
// This prevents calling QPainter from OBS's graphics/render thread (UB on some
// platforms, blank textures on others).
void HomeInRenderer::PrepareTexture() {
    std::string     text_to_draw;
    OverlaySettings draw_settings;
    float           alpha;

    {
        std::lock_guard<std::mutex> lock(text_mutex);
        if (!dirty) return;
        text_to_draw  = current_text;
        draw_settings = settings;
        alpha         = current_alpha;
    }

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    if (!text_to_draw.empty() && alpha > 0.0f) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        QFont font(draw_settings.font_family, draw_settings.base_font_size);
        font.setBold(true);
        painter.setFont(font);

        QRect rect;
        if (draw_settings.full_screen) {
            rect = QRect(100, 100, (int)width - 200, (int)height - 200);
        } else {
            int margin    = 100;
            int bar_h     = (int)(height * 0.3f);
            rect = QRect(margin, (int)height - bar_h - margin,
                         (int)width - margin * 2, bar_h);
        }

        // FIX #14b: Bake alpha into paint colours so the GPU upload is
        // self-contained. OBS_EFFECT_DEFAULT has no "color" uniform.
        int a8 = (int)(alpha * 255.0f);

        painter.setPen(QColor(0, 0, 0, (int)(a8 * 0.7f)));
        painter.drawText(rect.translated(2, 2),
                         draw_settings.alignment | Qt::AlignVCenter | Qt::TextWordWrap,
                         QString::fromStdString(text_to_draw));

        painter.setPen(QColor(255, 255, 255, a8));
        painter.drawText(rect,
                         draw_settings.alignment | Qt::AlignVCenter | Qt::TextWordWrap,
                         QString::fromStdString(text_to_draw));

        painter.end();
    }

    {
        std::lock_guard<std::mutex> lock(text_mutex);
        pending_image = std::move(image);
        dirty         = false;
    }
    image_ready.store(true, std::memory_order_release);
}

// UpdateTexture() runs on the OBS render thread — only GPU work here, no Qt paint.
void HomeInRenderer::UpdateTexture() {
    if (!image_ready.exchange(false, std::memory_order_acq_rel)) return;

    QImage image;
    {
        std::lock_guard<std::mutex> lock(text_mutex);
        image = pending_image;
    }

    obs_enter_graphics();
    if (image.isNull() || image.width() == 0) {
        if (texture) { gs_texture_destroy(texture); texture = nullptr; }
        obs_leave_graphics();
        return;
    }

    if (!texture)
        texture = gs_texture_create(width, height, GS_RGBA, 1, nullptr, GS_DYNAMIC);

    if (texture)
        gs_texture_set_image(texture, image.bits(), (uint32_t)(width * 4), false);

    obs_leave_graphics();
}

void HomeInRenderer::Render(gs_effect_t* effect) {
    UNUSED_PARAMETER(effect);

    // Advance fade state
    {
        std::lock_guard<std::mutex> lock(text_mutex);
        if (is_fading_out) {
            current_alpha -= fade_speed;
            if (current_alpha <= 0.0f) {
                current_alpha = 0.0f;
                is_fading_out = false;
                current_text  = pending_text;
                dirty         = true;
            }
        } else if (!current_text.empty()) {
            if (current_alpha < 1.0f) {
                current_alpha += fade_speed;
                dirty = true; // re-bake alpha each frame during fade-in
            }
            if (current_alpha > 1.0f) current_alpha = 1.0f;
        } else if (current_text.empty() && current_alpha > 0.0f) {
            current_alpha -= fade_speed;
            dirty = true;
        }
    }

    // GPU upload (safe here — Qt paint already done in PrepareTexture)
    UpdateTexture();

    if (!texture || current_alpha <= 0.0f) return;

    // FIX #15: Use gs_effect_loop with standard "Draw" pass.
    // The old code tried to set a "color" param that doesn't exist in
    // OBS_EFFECT_DEFAULT — alpha is now baked into the texture pixels instead.
    gs_effect_t* default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (!default_effect) return;

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    gs_effect_set_texture(
        gs_effect_get_param_by_name(default_effect, "image"), texture);

    while (gs_effect_loop(default_effect, "Draw")) {
        gs_draw_sprite(texture, 0, width, height);
    }

    gs_blend_state_pop();
}
