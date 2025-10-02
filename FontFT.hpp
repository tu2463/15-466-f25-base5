// Credit: Used ChatGPT for assistance

#pragma once
#include <ft2build.h>
#include FT_FREETYPE_H
#include "GL.hpp" // for GLuint and GL enums
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

struct Glyph
{
    GLuint tex = 0;       // GL_R8 texture
    glm::ivec2 size{};    // bitmap size (px)
    glm::ivec2 bearing{}; // left/top bearing (px)
    float advance = 0.f;  // advance.x in px
};

struct FontFT
{
    FontFT(const std::string &font_path, int pixel_size);
    ~FontFT();
    FT_Face get_ft_face() const { return ft_face; }
    const Glyph &get_glyph(FT_UInt glyph_index);
    int pixel_size() const { return pixel_size_; }

private:
    FT_Library ft_library = nullptr;
    FT_Face ft_face = nullptr;
    int pixel_size_ = 0;
    std::unordered_map<FT_UInt, Glyph> cache;
};
