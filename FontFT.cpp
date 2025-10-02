// Credit: Used ChatGPT for assistance

#include "FontFT.hpp"
#include <stdexcept>

FontFT::FontFT(const std::string &font_path, int pixel_size)
{
    if (FT_Init_FreeType(&ft_library))
        throw std::runtime_error("FT init failed");
    if (FT_New_Face(ft_library, font_path.c_str(), 0, &ft_face))
        throw std::runtime_error("Open font failed");
    FT_Set_Pixel_Sizes(ft_face, 0, pixel_size);
    pixel_size_ = pixel_size;
}
FontFT::~FontFT()
{
    if (ft_face)
        FT_Done_Face(ft_face);
    if (ft_library)
        FT_Done_FreeType(ft_library);
}
const Glyph &FontFT::get_glyph(FT_UInt glyph_index)
{
    auto it = cache.find(glyph_index);
    if (it != cache.end())
        return it->second;
    if (FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_RENDER))
        throw std::runtime_error("FT_Load_Glyph failed");
    FT_GlyphSlot g = ft_face->glyph;
    Glyph out{};
    out.size = {int(g->bitmap.width), int(g->bitmap.rows)};
    out.bearing = {g->bitmap_left, g->bitmap_top};
    out.advance = float(g->advance.x) / 64.0f;
    glGenTextures(1, &out.tex);
    glBindTexture(GL_TEXTURE_2D, out.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
        out.size.x, out.size.y, 0, GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    auto res = cache.emplace(glyph_index, out);
    return res.first->second;
}