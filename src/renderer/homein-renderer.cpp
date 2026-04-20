#include "homein-renderer.hpp"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <mutex>

static HomeInRenderer* g_active_renderer = nullptr;

HomeInRenderer* GetActiveRenderer() {
    return g_active_renderer;
}

static const char* homein_renderer_get_name(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("Home Indeed Overlay");
}

static void* homein_renderer_create(obs_data_t* settings, obs_source_t* context) {
    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(context);
    
    auto renderer = new HomeInRenderer();
    g_active_renderer = renderer;
    return renderer;
}

static void homein_renderer_destroy(void* data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    if (renderer == g_active_renderer) {
        g_active_renderer = nullptr;
    }
    delete renderer;
}

static void homein_renderer_video_render(void* data, gs_effect_t* effect) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    if (renderer) {
        renderer->Render(effect);
    }
}

static uint32_t homein_renderer_get_width(void *data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    return renderer ? renderer->GetWidth() : 1920;
}

static uint32_t homein_renderer_get_height(void *data) {
    auto renderer = static_cast<HomeInRenderer*>(data);
    return renderer ? renderer->GetHeight() : 1080;
}

void HomeInRenderer::Register() {
    struct obs_source_info info = {};
    info.id = "homein_overlay_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = homein_renderer_get_name;
    info.create = homein_renderer_create;
    info.destroy = homein_renderer_destroy;
    info.video_render = homein_renderer_video_render;
    info.get_width = homein_renderer_get_width;
    info.get_height = homein_renderer_get_height;

    obs_register_source(&info);
}

HomeInRenderer::HomeInRenderer() {
    // Initialize graphics resources if needed
}

HomeInRenderer::~HomeInRenderer() {
    obs_enter_graphics();
    if (texture) gs_texture_destroy(texture);
    obs_leave_graphics();
}

void HomeInRenderer::SetText(const std::string& text) {
    std::lock_guard<std::mutex> lock(text_mutex);
    if (current_text != text) {
        pending_text = text;
        is_fading_out = true; // Trigger fade-out before swapping
    }
}

void HomeInRenderer::UpdateSettings(const OverlaySettings& new_settings) {
    std::lock_guard<std::mutex> lock(text_mutex);
    settings = new_settings;
    dirty = true;
}

void HomeInRenderer::UpdateTexture() {
    // Only update if text or settings have changed
    if (!dirty) return;
    dirty = false;

    std::string text_to_draw;
    OverlaySettings draw_settings;
    {
        std::lock_guard<std::mutex> lock(text_mutex);
        text_to_draw = current_text;
        draw_settings = settings;
    }

    if (text_to_draw.empty()) {
        if (texture) {
            gs_texture_destroy(texture);
            texture = nullptr;
        }
        return;
    }

    // 1. Create a transparent QImage as the drawing canvas
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 2. Setup Typography (Arial)
    QFont font(draw_settings.font_family, draw_settings.base_font_size);
    font.setBold(true);
    painter.setFont(font);

    // 3. Define the bounding box based on Dynamic Positioning
    QRect rect;
    if (draw_settings.full_screen) {
        rect = QRect(100, 100, width - 200, height - 200); // Full screen with margins
    } else {
        // Lower Third: Bottom 30% of the screen
        int margin = 100;
        int bar_height = (int)(height * 0.3);
        rect = QRect(margin, height - bar_height - margin, width - (margin * 2), bar_height);
    }

    // 4. Draw Text with Drop Shadow for readability
    painter.setPen(QColor(0, 0, 0, 180)); // Shadow
    painter.drawText(rect.translated(2, 2), draw_settings.alignment | Qt::AlignVCenter | Qt::TextWordWrap, QString::fromStdString(text_to_draw));
    
    painter.setPen(Qt::white); // Main Text
    painter.drawText(rect, draw_settings.alignment | Qt::AlignVCenter | Qt::TextWordWrap, QString::fromStdString(text_to_draw));

    painter.end();

    // 5. High-Performance Texture Upload
    obs_enter_graphics();
    if (!texture) {
        texture = gs_texture_create(width, height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
    }

    // Convert QImage (ARGB) to OBS (RGBA) - simple byte swap if needed
    // QImage::Format_ARGB32_Premultiplied is 0xAARRGGBB on little-endian (BGRA)
    // We map it using the dynamic set function
    gs_texture_set_image(texture, image.bits(), width * 4, false);
    obs_leave_graphics();
}

void HomeInRenderer::Render(gs_effect_t* effect) {
    UNUSED_PARAMETER(effect);

    // --- Transition Logic ---
    {
        std::lock_guard<std::mutex> lock(text_mutex);
        if (is_fading_out) {
            current_alpha -= fade_speed;
            if (current_alpha <= 0.0f) {
                current_alpha = 0.0f;
                is_fading_out = false;
                current_text = pending_text; // Swap text at bottom of fade
                dirty = true;
            }
        } else if (!current_text.empty()) {
            if (current_alpha < 1.0f) current_alpha += fade_speed;
            if (current_alpha > 1.0f) current_alpha = 1.0f;
        } else if (current_text.empty() && current_alpha > 0.0f) {
            current_alpha -= fade_speed;
        }
    }

    UpdateTexture();

    if (!texture || current_alpha <= 0.0f) return;

    // Draw the generated texture to the OBS scene
    gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (default_effect) {
        gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
        size_t passes = gs_technique_begin(tech);
        for (size_t i = 0; i < passes; i++) {
            gs_technique_begin_pass(tech, i);
            
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), texture);
            
            // Apply Alpha Fade natively
            struct vec4 color;
            vec4_set(&color, current_alpha, current_alpha, current_alpha, current_alpha);
            gs_effect_set_vec4(gs_effect_get_param_by_name(default_effect, "color"), &color);

            gs_draw_sprite(texture, 0, width, height);

            gs_technique_end_pass(tech);
        }
        gs_technique_end(tech);
    }
}
