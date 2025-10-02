// Credit: Used ChatGPT for assistance

#pragma once
#include <hb.h>
#include <hb-ft.h>
#include <vector>
#include <string>
#include <ft2build.h>
#include FT_FREETYPE_H

struct GlyphRun
{
    std::vector<hb_glyph_info_t> infos;
    std::vector<hb_glyph_position_t> poss;
};

struct TextHB
{
    explicit TextHB(FT_Face face);
    ~TextHB();
    GlyphRun shape(const std::string &utf8);

private:
    hb_font_t *hb_font = nullptr;
};
