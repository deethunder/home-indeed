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
    info.get_defaults = [](obs_data_t* settings) {
        obs_data_set_default_string(settings, "font_color", "white");
        obs_data_set_default_int(settings, "alignment", (int)Qt::AlignCenter);
        obs_data_set_default_int(settings, "font_size", 48);
    };
    obs_register_source(&info);
}

HomeInRenderer::HomeInRenderer() {
    settings.font_color = "white";
    settings.base_font_size = 48;
    settings.font_family = "Arial";
    settings.alignment = Qt::AlignCenter;
}

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

        // Split text into Reference and Verse Text
        QString qFull = QString::fromStdString(text_to_draw);
        bool isLyrics = qFull.startsWith('\x01');
        if (isLyrics) qFull = qFull.mid(1); // Remove invisible marker

        int firstNL = qFull.indexOf('\n');
        QString refStr = "";
        QString bodyStr = qFull;

        // If NOT lyrics, we use the Bible-style split (Ref on first line)
        if (!isLyrics && firstNL != -1) {
            refStr = qFull.left(firstNL);
            bodyStr = qFull.mid(firstNL + 1);
        }

        // Extract verse number (Bible only)
        QString verseNum = "";
        if (!isLyrics && !bodyStr.isEmpty() && bodyStr[0].isDigit()) {
            int spaceIdx = bodyStr.indexOf(' ');
            if (spaceIdx != -1) {
                verseNum = bodyStr.left(spaceIdx);
                bodyStr = bodyStr.mid(spaceIdx + 1);
            }
        }

        int a8 = (int)(alpha * 255.0f);

        auto drawWithOutline = [&](const QRect& r, const QString& text, QFont& f, int extra_flags = 0) {
            int flags = draw_settings.alignment | Qt::AlignVCenter | Qt::TextWordWrap | extra_flags;
            
            // DYNAMIC AUTO-SCALING: Shrink font until it fits the rectangle
            int minSize = 12;
            while (f.pointSize() > minSize) {
                QFontMetrics fm(f);
                QRect boundingRect = fm.boundingRect(r, flags, text);
                if (boundingRect.height() <= r.height()) break;
                f.setPointSize(f.pointSize() - 2);
            }

            painter.setFont(f);
            QString colorStr = draw_settings.font_color.toLower().trimmed();
            if (colorStr != "black") colorStr = "white"; 
            
            QColor textColor = (colorStr == "black") ? Qt::black : Qt::white;
            painter.setPen(QColor(textColor.red(), textColor.green(), textColor.blue(), a8));
            painter.drawText(r, flags, text);
        };

        // Layout
        int margin = 80;
        int area_h = (int)(height * 0.40f); // Slightly larger area for lyrics
        QRect contentRect(margin, (int)height - area_h - 40, (int)width - margin * 2, area_h);
        
        if (isLyrics) {
            // Lyrics Mode: Draw everything as one big bold block
            int calculatedSize = (int)(draw_settings.base_font_size * 1.3f);
            QFont lyricFont(draw_settings.font_family, calculatedSize);
            lyricFont.setBold(true);
            lyricFont.setWeight(QFont::Bold);
            drawWithOutline(contentRect, bodyStr, lyricFont);
        } else {
            // Bible Mode: Original reference/superscript layout
            QRect bodyRect = contentRect;
            bodyRect.setHeight((int)(contentRect.height() * 0.85f));
            
            QRect refRect = contentRect;
            refRect.setTop(bodyRect.bottom() - 20);

            int calculatedBodySize = (int)(draw_settings.base_font_size * 1.2f);
            QFont bodyFont(draw_settings.font_family, calculatedBodySize);
            bodyFont.setBold(true);

            if (!bodyStr.isEmpty()) {
                if (!verseNum.isEmpty()) {
                    QFont superFont(draw_settings.font_family, (int)(calculatedBodySize * 0.8f));
                    superFont.setBold(true);
                    QFontMetrics fm(superFont);
                    int superW = fm.horizontalAdvance(verseNum) + 60;
                    // Move the verse number ABOVE the text line
                    drawWithOutline(bodyRect.translated(-superW/2, -(int)(calculatedBodySize * 0.7f)), verseNum, superFont);
                    drawWithOutline(bodyRect.translated(superW/2, 0), bodyStr, bodyFont);
                } else {
                    drawWithOutline(bodyRect, bodyStr, bodyFont);
                }
            }
            
            if (!refStr.isEmpty()) {
                QFont refFont(draw_settings.font_family, (int)(draw_settings.base_font_size * 0.7f));
                refFont.setBold(true);
                refFont.setItalic(true);
                drawWithOutline(refRect, refStr, refFont);
            }
        }

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
