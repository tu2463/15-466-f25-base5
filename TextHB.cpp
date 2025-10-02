// Credit: Used ChatGPT for assistance

#include "TextHB.hpp"
#include <stdexcept>

TextHB::TextHB(FT_Face face)
{
    hb_font = hb_ft_font_create(face, nullptr);
    if (!hb_font)
        throw std::runtime_error("hb_ft_font_create failed");
}
TextHB::~TextHB()
{
    if (hb_font)
        hb_font_destroy(hb_font);
}
GlyphRun TextHB::shape(const std::string &utf8)
{
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8.c_str(), -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hb_font, buf, nullptr, 0);
    unsigned int n = hb_buffer_get_length(buf);
    GlyphRun run;
    run.infos.resize(n);
    run.poss.resize(n);
    auto *infos = hb_buffer_get_glyph_infos(buf, nullptr);
    auto *poss = hb_buffer_get_glyph_positions(buf, nullptr);
    for (unsigned int i = 0; i < n; ++i)
    {
        run.infos[i] = infos[i];
        run.poss[i] = poss[i];
    }
    hb_buffer_destroy(buf);
    return run;
}
