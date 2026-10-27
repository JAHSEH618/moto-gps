#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lvgl.h"

namespace moto::nav_ui {

namespace detail {

/// The codepoint at `index`, advancing past it. Zero when the bytes are not a
/// complete, valid UTF-8 sequence; callers treat that as undrawable.
inline std::uint32_t next_codepoint(const char *text, std::size_t size,
                                    std::size_t &index) {
    const auto lead = static_cast<std::uint8_t>(text[index]);
    std::size_t width = 0;
    std::uint32_t value = 0;
    if(lead < 0x80U) {
        width = 1;
        value = lead;
    } else if(lead >= 0xC2U && lead <= 0xDFU) {
        width = 2;
        value = lead & 0x1FU;
    } else if(lead >= 0xE0U && lead <= 0xEFU) {
        width = 3;
        value = lead & 0x0FU;
    } else if(lead >= 0xF0U && lead <= 0xF4U) {
        width = 4;
        value = lead & 0x07U;
    } else {
        // Stray continuation byte or an invalid lead: skip one byte so the
        // scan always makes progress.
        ++index;
        return 0;
    }
    if(index + width > size) {
        index = size;
        return 0;
    }
    for(std::size_t next = 1; next < width; ++next) {
        const auto byte = static_cast<std::uint8_t>(text[index + next]);
        if((byte & 0xC0U) != 0x80U) {
            ++index;
            return 0;
        }
        value = (value << 6U) | (byte & 0x3FU);
    }
    index += width;
    return value;
}

/// Whether the face — or anything in its fallback chain — can draw this
/// codepoint. LVGL reports a missing glyph as a placeholder, and the
/// placeholder is exactly the empty box this guards against.
inline bool drawable(const lv_font_t *font, std::uint32_t codepoint) {
    // A face that was never initialised (line_height 0) has no glyphs at all;
    // refusing every character would empty the label, so trust the bytes.
    if(font == nullptr || font->line_height == 0) return true;
    lv_font_glyph_dsc_t glyph{};
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) &&
           !glyph.is_placeholder;
}

}  // namespace detail

/// Copies a phone-supplied string into a fixed label buffer.
///
/// Two things a plain `snprintf` copy gets wrong here: it can stop in the
/// middle of a codepoint, and the orphaned bytes paint as one empty box; and
/// it copies characters the installed faces cannot draw, which paint as boxes
/// too. Track titles and place names arrive in whatever script the rider's
/// library uses, so both are routine, not errors.
///
/// Characters with no glyph anywhere in `font`'s fallback chain are dropped,
/// the copy never splits a codepoint, and a trailing space left behind by a
/// dropped character is trimmed. An empty result falls back to `fallback`.
inline void copy_display_text(char *destination, std::size_t capacity,
                              const lv_font_t *font, const char *source,
                              const char *fallback) {
    if(destination == nullptr || capacity == 0) return;
    if(source == nullptr || source[0] == '\0') source = fallback;
    if(source == nullptr) source = "";

    const std::size_t size = std::strlen(source);
    std::size_t used = 0;
    std::size_t index = 0;
    while(index < size) {
        const std::size_t start = index;
        const std::uint32_t codepoint =
            detail::next_codepoint(source, size, index);
        // A newline is a line break, not a glyph; keep it so wrapped titles
        // break where the tagger put the break.
        if(codepoint != '\n' && !detail::drawable(font, codepoint)) continue;
        const std::size_t width = index - start;
        if(used + width + 1 > capacity) break;
        std::memcpy(destination + used, source + start, width);
        used += width;
    }
    while(used > 0 && destination[used - 1] == ' ') --used;
    destination[used] = '\0';

    if(destination[0] == '\0' && fallback != nullptr && fallback[0] != '\0') {
        // Everything was undrawable: show the same placeholder an absent value
        // gets rather than a blank line.
        std::size_t copy = std::strlen(fallback);
        if(copy + 1 > capacity) copy = capacity - 1;
        std::memcpy(destination, fallback, copy);
        destination[copy] = '\0';
    }
}

}  // namespace moto::nav_ui
