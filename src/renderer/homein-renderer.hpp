#pragma once

#include <obs-module.h>
#include <string>
#include <mutex>
#include <atomic>
#include <QImage>
#include <QPainter>
#include <QFont>

/**
 * @struct OverlaySettings
 * @brief User-configurable settings for text alignment and positioning.
 */
struct OverlaySettings {
    Qt::Alignment alignment = Qt::AlignCenter;
    bool full_screen = false;
    QString font_family = "Arial";
    int base_font_size = 48;
};

/**
 * @class HomeInRenderer
 * @brief Universal OBS video source using Qt6 for professional text rendering.
 */
class HomeInRenderer {
public:
    HomeInRenderer();
    ~HomeInRenderer();

    /**
     * @brief Registers the 'Home Indeed Overlay' video source in OBS.
     */
    static void Register();

    /**
     * @brief Updates the content to be displayed on the overlay.
     * @param text The new text string (Bible verse or song lyrics).
     */
    void SetText(const std::string& text);

    /**
     * @brief The core rendering callback called by OBS on the graphics thread.
     * @param effect The graphics effect being used for the draw call.
     */
    void Render(gs_effect_t *effect);

    /**
     * @brief Updates the overlay settings (alignment, size, etc.)
     */
    void UpdateSettings(const OverlaySettings& settings);

private:
    /**
     * @brief Re-generates the text texture using the Qt QPainter engine.
     */
    void UpdateTexture();

    std::string current_text;
    std::mutex text_mutex;
    std::atomic<bool> dirty{false};
    
    OverlaySettings settings;
    
    // Graphics resources
    gs_texture_t *texture = nullptr;
    uint32_t width = 1920;
    uint32_t height = 1080;
};

/**
 * @brief Thread-safe accessor for the currently active renderer instance.
 */
extern HomeInRenderer* GetActiveRenderer();
