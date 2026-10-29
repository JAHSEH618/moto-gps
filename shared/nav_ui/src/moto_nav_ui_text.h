#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lvgl.h"
#include "moto_hant_hans_table.h"

namespace moto::nav_ui {

namespace detail {

/// The codepoint at `index`, advancing past it. Zero when the bytes are not a
/// complete, valid UTF-8 sequence; callers treat that as undrawable. A string
/// that ends partway through a sequence is consumed to its end, so the caller
/// can tell a cut-off last character from garbage in the middle.
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
    const std::size_t available = index + width > size ? size - index : width;
    for(std::size_t next = 1; next < available; ++next) {
        const auto byte = static_cast<std::uint8_t>(text[index + next]);
        if((byte & 0xC0U) != 0x80U) {
            ++index;
            return 0;
        }
        value = (value << 6U) | (byte & 0x3FU);
    }
    if(available < width) {
        index = size;
        return 0;
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

/// The Simplified form of a Traditional character, or zero. The table holds
/// only characters the text face lacks whose Simplified form it carries, one
/// codepoint for one, so a swap never changes the length of a title.
inline std::uint32_t simplified_form(std::uint32_t codepoint) {
    if(codepoint > 0xFFFFU) return 0;
    std::size_t low = 0;
    std::size_t high = sizeof(kHantHans) / sizeof(kHantHans[0]);
    while(low < high) {
        const std::size_t middle = low + (high - low) / 2;
        const std::uint32_t key = kHantHans[middle].traditional;
        if(key == codepoint) return kHantHans[middle].simplified;
        if(key < codepoint) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return 0;
}

/// Space of any width. An ideographic or no-break space has no glyph in these
/// faces, but it still separates two words.
inline bool is_space(std::uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\r' ||
           codepoint == 0x0BU || codepoint == 0x0CU || codepoint == 0x85U ||
           codepoint == 0xA0U || codepoint == 0x1680U ||
           (codepoint >= 0x2000U && codepoint <= 0x200AU) ||
           codepoint == 0x2028U || codepoint == 0x2029U ||
           codepoint == 0x202FU || codepoint == 0x205FU ||
           codepoint == 0x3000U;
}

/// Characters with no visible form of their own: controls, soft hyphen,
/// zero-width space and non-joiner, direction marks and isolates, word joiner,
/// byte-order mark, and the variation selectors that pick a glyph variant of
/// the character before them (FE00-FE0D, E0100-E01EF). Dropping one loses
/// nothing the rider could have read.
inline bool is_invisible(std::uint32_t codepoint) {
    return codepoint < 0x20U || (codepoint >= 0x7FU && codepoint <= 0x9FU) ||
           codepoint == 0xADU ||
           (codepoint >= 0x200BU && codepoint <= 0x200FU) ||
           (codepoint >= 0x202AU && codepoint <= 0x202EU) ||
           (codepoint >= 0x2060U && codepoint <= 0x206FU) ||
           (codepoint >= 0xFE00U && codepoint <= 0xFE0DU) ||
           codepoint == 0xFEFFU ||
           (codepoint >= 0xE0100U && codepoint <= 0xE01EFU);
}

/// Emoji and the characters that only exist to build one. Unicode's
/// Extended_Pictographic is the exact set, but it is ~80 runs and reserves
/// unassigned space; these blocks hold every emoji a phone keyboard sends:
///   1F000-1F0FF  mahjong, domino and playing-card tiles (🀄 🃏)
///   1F170-1F1FF  negative squared letters and words (🅰 🆗), and the
///                regional indicators that pair into flags (🇨🇳)
///   1F200-1F2FF  enclosed ideographs (🈚 🉐)
///   1F300-1F64F  pictographs and emoticons, including the skin-tone
///                modifiers 1F3FB-1F3FF
///   1F680-1F6FF  transport and map symbols
///   1F7E0-1F7FF  coloured circles and squares (🟠 🟩)
///   1F900-1FAFF  supplemental pictographs, chess, pictographs extended-A
///   2600-2775 2794-27BF
///                miscellaneous symbols and dingbats (☀ ❤ ✨ ✅); this also
///                takes ★ and ♪, which decorate a title rather than spell it,
///                but not the dingbat digits 2776-2793 (❶ ➀), which are words
///   231A-231B 23E9-23EC 23F0 23F3 25FD-25FE 2B1B-2B1C 2B50 2B55
///                the other BMP characters that are emoji by default
///                (⌚ ⏩ ⏰ ⏳ ◽ ⬛ ⭐ ⭕)
///   FE0E FE0F    text / emoji presentation selectors
///   200D         zero-width joiner, which glues emoji into one (a family)
///   20E3         combining keycap; the digit before it is kept
///   E0000-E007F  tag characters, which spell the subdivision flags
/// A character outside these that is followed by FE0F, a keycap or a skin
/// tone, or that follows a joiner, is part of an emoji too (© or ↔ written
/// as emoji); the caller handles that context.
inline bool is_emoji(std::uint32_t codepoint) {
    if(codepoint >= 0x1F000U) {
        return codepoint <= 0x1F0FFU ||
               (codepoint >= 0x1F170U && codepoint <= 0x1F64FU) ||
               (codepoint >= 0x1F680U && codepoint <= 0x1F6FFU) ||
               (codepoint >= 0x1F7E0U && codepoint <= 0x1F7FFU) ||
               (codepoint >= 0x1F900U && codepoint <= 0x1FAFFU) ||
               (codepoint >= 0xE0000U && codepoint <= 0xE007FU);
    }
    return (codepoint >= 0x2600U && codepoint <= 0x2775U) ||
           (codepoint >= 0x2794U && codepoint <= 0x27BFU) ||
           codepoint == 0x231AU || codepoint == 0x231BU ||
           (codepoint >= 0x23E9U && codepoint <= 0x23ECU) ||
           codepoint == 0x23F0U || codepoint == 0x23F3U ||
           codepoint == 0x25FDU || codepoint == 0x25FEU ||
           codepoint == 0x2B1BU || codepoint == 0x2B1CU ||
           codepoint == 0x2B50U || codepoint == 0x2B55U ||
           codepoint == 0xFE0EU || codepoint == 0xFE0FU ||
           codepoint == 0x200DU || codepoint == 0x20E3U;
}

/// Whether the codepoint at `index` turns the one before it into an emoji.
inline bool emoji_modifier_at(const char *text, std::size_t size,
                              std::size_t index) {
    if(index >= size) return false;
    const std::uint32_t next = next_codepoint(text, size, index);
    return next == 0xFE0FU || next == 0x20E3U ||
           (next >= 0x1F3FBU && next <= 0x1F3FFU);
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
/// Each character with no glyph anywhere in `font`'s fallback chain is, in
/// order of preference:
///   - swapped for its Simplified form when that one has a glyph (張學友
///     reads 张学友); a character the face draws is never swapped;
///   - dropped when it is an emoji, or invisible to begin with;
///   - otherwise shown as "?", once for a whole run of them, so the rider can
///     see that something is there that the screen cannot draw.
/// Any space becomes one ASCII space, and only between two things that were
/// written, so a dropped emoji leaves no doubled, leading or trailing gap and
/// spaces inside a run do not split its "?". The copy never splits a
/// codepoint; a character that does not fit ends it. When nothing readable
/// is left — only "?" or nothing at all — the result is `fallback`.
inline void copy_display_text(char *destination, std::size_t capacity,
                              const lv_font_t *font, const char *source,
                              const char *fallback) {
    if(destination == nullptr || capacity == 0) return;
    if(source == nullptr || source[0] == '\0') source = fallback;
    if(source == nullptr) source = "";

    const std::size_t size = std::strlen(source);
    std::size_t used = 0;
    // A space is owed, not written, until a character follows it.
    bool space = false;
    // The last mark written was "?": a missing character joins it.
    bool marked = false;
    // A character other than "?" was written.
    bool readable = false;
    // The previous character was a zero-width joiner, so this one is the next
    // part of the same emoji.
    bool joined = false;
    // A character that does not fit ends the copy; skipping it to fit a later,
    // narrower one would close up a gap the rider cannot see.
    auto append = [&](const char *bytes, std::size_t width) {
        const std::size_t gap =
            space && used > 0 && destination[used - 1] != '\n' ? 1 : 0;
        if(used + gap + width + 1 > capacity) return false;
        if(gap != 0) destination[used++] = ' ';
        std::memcpy(destination + used, bytes, width);
        used += width;
        space = false;
        return true;
    };

    std::size_t index = 0;
    while(index < size) {
        const std::size_t start = index;
        const std::uint32_t codepoint =
            detail::next_codepoint(source, size, index);
        const bool after_joiner = joined;
        joined = codepoint == 0x200DU;
        const auto lead = static_cast<std::uint8_t>(source[start]);
        if(codepoint == 0 && index == size && lead >= 0xC2U && lead <= 0xF4U) {
            // The sender cut its last character in half. That is a short
            // string, not a glyph the face is missing, so it gets no "?".
            break;
        }
        if(codepoint == '\n') {
            // A line break, not a glyph; keep it so wrapped titles break
            // where the tagger put the break. It also ends a run of "?".
            space = false;
            marked = false;
            if(used > 0 && !append("\n", 1)) break;
            continue;
        }
        if(detail::is_space(codepoint)) {
            space = true;
            continue;
        }
        if(codepoint != 0 && detail::drawable(font, codepoint)) {
            if(!append(source + start, index - start)) break;
            marked = false;
            readable = true;
            continue;
        }
        const std::uint32_t simplified = detail::simplified_form(codepoint);
        if(simplified != 0 && detail::drawable(font, simplified)) {
            // Every table entry is a BMP ideograph: three bytes.
            const char bytes[] = {
                static_cast<char>(0xE0U | (simplified >> 12U)),
                static_cast<char>(0x80U | ((simplified >> 6U) & 0x3FU)),
                static_cast<char>(0x80U | (simplified & 0x3FU))};
            if(!append(bytes, sizeof(bytes))) break;
            marked = false;
            readable = true;
            continue;
        }
        if(codepoint != 0 &&
           (detail::is_invisible(codepoint) || detail::is_emoji(codepoint) ||
            after_joiner || detail::emoji_modifier_at(source, size, index))) {
            continue;
        }
        if(marked) {
            // Same run: the spaces inside it go with it.
            space = false;
            continue;
        }
        if(!append("?", 1)) break;
        marked = true;
    }
    while(used > 0 && destination[used - 1] == '\n') --used;
    destination[used] = '\0';

    if(!readable && fallback != nullptr && fallback[0] != '\0') {
        // Nothing the rider can read: show the same placeholder an absent
        // value gets rather than a blank line or a lone "?".
        std::size_t copy = std::strlen(fallback);
        if(copy + 1 > capacity) copy = capacity - 1;
        std::memcpy(destination, fallback, copy);
        destination[copy] = '\0';
    }
}

}  // namespace moto::nav_ui
