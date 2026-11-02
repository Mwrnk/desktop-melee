/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "Melee" font family for RmlUi (melee_font.h), drawn from the game's
 * glyphs as the macOS app does (ui/GameAssets.swift): each glyph is a 32x32
 * coverage cell, `left` pixels of it before the ink and `right` after, so
 * it advances 32 - left - right. All 288 cells go into one texture; a size
 * scales them, as macOS draws them at any size. */
#include "melee_font.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Mesh.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "../ui/bridge/desktop_melee_bridge.h"
}

namespace {

constexpr int kCell = 32;
constexpr int kColumns = 18; /* the texture: 18 x 16 cells */
constexpr int kRows = 16;
/* The windows' stylesheets size text for Noto Sans, about 0.85 of the
 * macOS sizes; a Melee glyph cell is the macOS size. */
constexpr float kCellPerSize = 1.f / 0.85f;
constexpr const char kMagic[8] = {'D', 'M', 'F', 'O', 'N', 'T', '1', '\0'};

struct Stored {
    uint8_t hi, lo;
    int8_t left, right;
    uint8_t alpha[kCell * kCell];
};

struct Glyph {
    int cell; /* its place in the texture */
    int left;
    int advance;
};

/* Shift-JIS row 0x81 (0x8140 on), as Unicode: punctuation and symbols. */
constexpr char32_t kRow81[] = {0x3000, 0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x30FB, 0xFF1A, 0xFF1B,
    0xFF1F, 0xFF01, 0x309B, 0x309C, 0x00B4, 0xFF40, 0x00A8, 0xFF3E, 0xFFE3, 0xFF3F, 0x30FD, 0x30FE,
    0x309D, 0x309E, 0x3003, 0x4EDD, 0x3005, 0x3006, 0x3007, 0x30FC, 0x2015, 0x2010, 0xFF0F, 0xFF3C,
    0x301C, 0x2016, 0xFF5C, 0x2026, 0x2025, 0x2018, 0x2019, 0x201C, 0x201D, 0xFF08, 0xFF09, 0x3014,
    0x3015, 0xFF3B, 0xFF3D, 0xFF5B, 0xFF5D, 0x3008, 0x3009, 0x300A, 0x300B, 0x300C, 0x300D, 0x300E,
    0x300F, 0x3010, 0x3011, 0xFF0B, 0x2212, 0x00B1, 0x00D7, 0, /* 0x817F is unused */
    0x00F7, 0xFF1D, 0x2260, 0xFF1C, 0xFF1E, 0x2266, 0x2267, 0x221E, 0x2234, 0x2642, 0x2640, 0x00B0,
    0x2032, 0x2033, 0x2103, 0xFFE5, 0xFF04, 0xFFE0, 0xFFE1, 0xFF05, 0xFF03, 0xFF06, 0xFF0A, 0xFF20,
    0x00A7};

/* A Shift-JIS character as Unicode, full-width forms folded to ASCII
 * ("Ａ" to "A"), as macOS's compatibility mapping does; 0 for the kana and
 * kanji, which the windows never show. */
char32_t unicode(uint8_t hi, uint8_t lo) {
    char32_t c = 0;
    if (hi == 0x81 && lo >= 0x40 && lo - 0x40 < sizeof kRow81 / sizeof kRow81[0]) {
        c = kRow81[lo - 0x40];
    } else if (hi == 0x82 && lo >= 0x4F && lo <= 0x58) {
        c = U'0' + (lo - 0x4F);
    } else if (hi == 0x82 && lo >= 0x60 && lo <= 0x79) {
        c = U'A' + (lo - 0x60);
    } else if (hi == 0x82 && lo >= 0x81 && lo <= 0x9A) {
        c = U'a' + (lo - 0x81);
    }
    if (c >= 0xFF01 && c <= 0xFF5E) {
        c -= 0xFEE0;
    } else if (c == 0x3000) {
        c = U' ';
    } else if (c == 0xFFE5) {
        c = 0xA5; /* ¥ */
    }
    return c;
}

/* Characters the font lacks, drawn with one it has: macOS's aliases, and
 * the typography our own strings use. */
char32_t alias(char32_t c) {
    switch (c) {
    case U'"':
        return 0x201D;
    case U'\'':
        return 0x2019;
    case U'-':
    case 0x2013:
    case 0x2014:
        return 0x2212;
    case U'\\':
        return 0xA5;
    case U'~':
        return 0x301C;
    case 0xB7: /* · */
        return 0x30FB;
    default:
        return c;
    }
}

/* ---- the glyphs --------------------------------------------------------- */

std::vector<Stored> s_stored;
std::unordered_map<char32_t, Glyph> s_glyphs;
std::vector<Rml::byte> s_texture; /* premultiplied white, kColumns x kRows cells */
float s_baseline = 25.f;          /* in cell pixels, from the top */
float s_x_height = 12.f;
int s_version = 1;

std::string cache_path() {
    char* folder = SDL_GetPrefPath(nullptr, "Desktop Melee");
    std::string path = folder != nullptr ? std::string(folder) + "melee-font.bin" : "";
    SDL_free(folder);
    return path;
}

/* The bottom of the ink of `c` (its last row with any), or -1. */
int ink_bottom(char32_t c) {
    const auto it = s_glyphs.find(c);
    if (it == s_glyphs.end()) {
        return -1;
    }
    const Stored& g = s_stored[it->second.cell];
    for (int y = kCell - 1; y >= 0; y--) {
        for (int x = 0; x < kCell; x++) {
            if (g.alpha[y * kCell + x] > 96) {
                return y;
            }
        }
    }
    return -1;
}

int ink_top(char32_t c) {
    const auto it = s_glyphs.find(c);
    if (it == s_glyphs.end()) {
        return -1;
    }
    const Stored& g = s_stored[it->second.cell];
    for (int y = 0; y < kCell; y++) {
        for (int x = 0; x < kCell; x++) {
            if (g.alpha[y * kCell + x] > 96) {
                return y;
            }
        }
    }
    return -1;
}

/* The glyph table and texture from s_stored. */
void build() {
    s_glyphs.clear();
    const int count = std::min<int>(static_cast<int>(s_stored.size()), kColumns * kRows);
    s_texture.assign(static_cast<size_t>(kColumns * kCell) * kRows * kCell * 4, 0);
    for (int i = 0; i < count; i++) {
        const Stored& g = s_stored[i];
        const int ox = (i % kColumns) * kCell;
        const int oy = (i / kColumns) * kCell;
        for (int y = 0; y < kCell; y++) {
            for (int x = 0; x < kCell; x++) {
                const Rml::byte a = static_cast<Rml::byte>(g.alpha[y * kCell + x]);
                const size_t p = (static_cast<size_t>(oy + y) * kColumns * kCell + ox + x) * 4;
                s_texture[p] = s_texture[p + 1] = s_texture[p + 2] = s_texture[p + 3] = a;
            }
        }
        const char32_t c = unicode(g.hi, g.lo);
        if (c != 0 && s_glyphs.find(c) == s_glyphs.end()) {
            s_glyphs[c] = {i, g.left, std::max(1, kCell - g.left - g.right)};
        }
    }
    /* The baseline under the capitals, the x-height over the x. */
    if (const int bottom = ink_bottom(U'H'); bottom >= 0) {
        s_baseline = static_cast<float>(bottom + 1);
    }
    if (const int top = ink_top(U'x'); top >= 0) {
        s_x_height = s_baseline - static_cast<float>(top);
    }
    s_version++;
}

bool read_cache() {
    const std::string path = cache_path();
    size_t size = 0;
    void* data = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    constexpr size_t entry = 4 + kCell * kCell;
    bool ok = size >= 12 && std::memcmp(bytes, kMagic, 8) == 0;
    const uint32_t count =
        ok ? (bytes[8] | bytes[9] << 8 | bytes[10] << 16 | uint32_t(bytes[11]) << 24) : 0;
    ok = ok && count > 0 && size == 12 + count * entry;
    if (ok) {
        s_stored.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* e = bytes + 12 + i * entry;
            Stored& g = s_stored[i];
            g.hi = e[0];
            g.lo = e[1];
            g.left = static_cast<int8_t>(e[2]);
            g.right = static_cast<int8_t>(e[3]);
            std::memcpy(g.alpha, e + 4, sizeof g.alpha);
        }
    }
    SDL_free(data);
    return ok;
}

void write_cache() {
    std::vector<uint8_t> out(kMagic, kMagic + 8);
    const uint32_t count = static_cast<uint32_t>(s_stored.size());
    for (int b = 0; b < 4; b++) {
        out.push_back(static_cast<uint8_t>(count >> (8 * b)));
    }
    for (const Stored& g : s_stored) {
        out.insert(
            out.end(), {g.hi, g.lo, static_cast<uint8_t>(g.left), static_cast<uint8_t>(g.right)});
        out.insert(out.end(), g.alpha, g.alpha + sizeof g.alpha);
    }
    const std::string path = cache_path();
    if (!path.empty() && SDL_SaveFile(path.c_str(), out.data(), out.size())) {
        SDL_Log("desktop-melee: saved Melee's font (%u glyphs) to %s", count, path.c_str());
    }
}

/* The game's glyphs: only once its assets are ready, since the sheet is
 * blank before the disc loads and desktop_melee_font_glyph_alpha keeps what
 * it decodes. */
bool read_game() {
    std::vector<Stored> stored;
    for (int i = 0; i < desktop_melee_font_glyph_count(); i++) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        int atlas = 0;
        int left = 0;
        int right = 0;
        if (!desktop_melee_font_glyph_info(i, &hi, &lo, &atlas, &left, &right)) {
            continue;
        }
        const uint8_t* alpha = desktop_melee_font_glyph_alpha(atlas);
        if (alpha == nullptr) {
            continue;
        }
        Stored g{hi, lo, static_cast<int8_t>(std::clamp(left, -128, 127)),
            static_cast<int8_t>(std::clamp(right, -128, 127)), {}};
        std::memcpy(g.alpha, alpha, sizeof g.alpha);
        stored.push_back(g);
    }
    /* A blank sheet (the disc not loaded after all) is no font. */
    const bool inked = std::any_of(stored.begin(), stored.end(), [](const Stored& g) {
        return std::any_of(std::begin(g.alpha), std::end(g.alpha), [](uint8_t a) { return a > 0; });
    });
    if (!inked) {
        return false;
    }
    s_stored = std::move(stored);
    return true;
}

/* ---- the engine --------------------------------------------------------- */

struct Face {
    int cell; /* the size of a glyph cell, in pixels */
    Rml::FontMetrics metrics;
};

std::string lower(const Rml::String& s) {
    std::string out = s;
    std::transform(
        out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

/* Decodes one UTF-8 character at p, moving p past it. */
char32_t next_char(const char*& p, const char* end) {
    const auto b = static_cast<unsigned char>(*p);
    int length = b < 0x80         ? 1 :
                 (b >> 5) == 0x6  ? 2 :
                 (b >> 4) == 0xE  ? 3 :
                 (b >> 3) == 0x1E ? 4 :
                                    1;
    if (p + length > end) {
        length = 1;
    }
    const char32_t c = static_cast<char32_t>(Rml::StringUtilities::ToCharacter(p, end));
    p += length;
    return c;
}

/* The glyph that draws c: itself, an alias, or nothing (drawn as a space). */
const Glyph* glyph_for(char32_t c) {
    auto it = s_glyphs.find(c);
    if (it == s_glyphs.end()) {
        it = s_glyphs.find(alias(c));
    }
    return it != s_glyphs.end() ? &it->second : nullptr;
}

class MeleeFontEngine : public Rml::FontEngineInterface {
public:
    Rml::FontEngineInterface* base = nullptr;

    void Initialize() override { base->Initialize(); }
    void Shutdown() override {
        faces_.clear();
        texture_.reset();
        base->Shutdown();
    }

    bool LoadFontFace(const Rml::String& file_name, int face_index, bool fallback_face,
        Rml::Style::FontWeight weight) override {
        return base->LoadFontFace(file_name, face_index, fallback_face, weight);
    }
    bool LoadFontFace(const Rml::String& file_name, int face_index, const Rml::String& family,
        Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face) override {
        return base->LoadFontFace(file_name, face_index, family, style, weight, fallback_face);
    }
    bool LoadFontFace(Rml::Span<const Rml::byte> data, int face_index, const Rml::String& family,
        Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face) override {
        return base->LoadFontFace(data, face_index, family, style, weight, fallback_face);
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
        Rml::Style::FontWeight weight, int size) override {
        if (lower(family) != "melee") {
            return base->GetFontFaceHandle(family, style, weight, size);
        }
        if (s_glyphs.empty()) {
            /* Until the disc: RmlUi keeps family names in lower case. */
            return base->GetFontFaceHandle("noto sans", style, weight, size);
        }
        const int cell = std::max(1, static_cast<int>(std::lround(size * kCellPerSize)));
        auto& face = faces_[cell];
        if (!face) {
            face = std::make_unique<Face>();
            face->cell = cell;
            const float unit = cell / float(kCell);
            Rml::FontMetrics& m = face->metrics;
            m.size = cell;
            m.ascent = s_baseline * unit;
            m.descent = (kCell - s_baseline) * unit;
            m.line_spacing = cell * 0.96f; /* macOS: lines size * 0.04 closer */
            m.x_height = s_x_height * unit;
            m.underline_position = 2.f * unit;
            m.underline_thickness = std::max(1.f, 2.f * unit);
            m.has_ellipsis = false;
        }
        return reinterpret_cast<Rml::FontFaceHandle>(face.get());
    }

    Rml::FontEffectsHandle PrepareFontEffects(
        Rml::FontFaceHandle handle, const Rml::FontEffectList& effects) override {
        return ours(handle) ? 0 : base->PrepareFontEffects(handle, effects);
    }

    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
        if (const Face* face = ours(handle)) {
            return face->metrics;
        }
        return base->GetFontMetrics(handle);
    }

    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
        const Rml::TextShapingContext& context, Rml::Character prior) override {
        const Face* face = ours(handle);
        if (face == nullptr) {
            return base->GetStringWidth(handle, string, context, prior);
        }
        return static_cast<int>(std::lround(layout(*face, string, context, {}, {}, nullptr)));
    }

    int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle handle,
        Rml::FontEffectsHandle effects, Rml::StringView string, Rml::Vector2f position,
        Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& context,
        Rml::TexturedMeshList& mesh_list) override {
        const Face* face = ours(handle);
        if (face == nullptr) {
            return base->GenerateString(render_manager, handle, effects, string, position, colour,
                opacity, context, mesh_list);
        }
        Rml::TexturedMesh textured;
        textured.texture = texture().GetTexture(render_manager);
        const float width = layout(*face, string, context, position, colour, &textured.mesh);
        if (textured.mesh) {
            mesh_list.push_back(std::move(textured));
        }
        return static_cast<int>(std::lround(width));
    }

    int GetVersion(Rml::FontFaceHandle handle) override {
        return ours(handle) ? s_version : base->GetVersion(handle);
    }

    void ReleaseFontResources() override {
        faces_.clear();
        texture_.reset();
        base->ReleaseFontResources();
    }

private:
    std::map<int, std::unique_ptr<Face>> faces_;
    std::unique_ptr<Rml::CallbackTextureSource> texture_;

    const Face* ours(Rml::FontFaceHandle handle) const {
        for (const auto& [cell, face] : faces_) {
            if (reinterpret_cast<Rml::FontFaceHandle>(face.get()) == handle) {
                return face.get();
            }
        }
        return nullptr;
    }

    Rml::CallbackTextureSource& texture() {
        if (!texture_) {
            texture_ = std::make_unique<Rml::CallbackTextureSource>(
                [](const Rml::CallbackTextureInterface& out) {
                    return out.GenerateTexture(
                        Rml::Span<const Rml::byte>(s_texture.data(), s_texture.size()),
                        {kColumns * kCell, kRows * kCell});
                });
        }
        return *texture_;
    }

    /* The string's width; with a mesh, its quads too, from the baseline at
     * `position`. */
    static float layout(const Face& face, Rml::StringView string,
        const Rml::TextShapingContext& context, Rml::Vector2f position,
        Rml::ColourbPremultiplied colour, Rml::Mesh* mesh) {
        const float unit = face.cell / float(kCell);
        const float atlas_w = float(kColumns * kCell);
        const float atlas_h = float(kRows * kCell);
        float x = 0.f;
        const char* p = string.begin();
        const char* end = string.end();
        bool first = true;
        while (p < end) {
            char32_t c = next_char(p, end);
            /* An ellipsis is three of the font's full stops. */
            const int repeat = c == 0x2026 ? 3 : 1;
            if (c == 0x2026) {
                c = U'.';
            }
            for (int r = 0; r < repeat; r++) {
                if (!first) {
                    x += context.letter_spacing;
                }
                first = false;
                const Glyph* g = glyph_for(c);
                if (g == nullptr) {
                    const Glyph* space = glyph_for(U' ');
                    x += (space != nullptr ? space->advance : kCell / 3) * unit;
                    continue;
                }
                if (mesh != nullptr && c != U' ') {
                    const float left = position.x + x - g->left * unit;
                    const float top = position.y - s_baseline * unit;
                    const float size = kCell * unit;
                    const float u0 = (g->cell % kColumns) * kCell / atlas_w;
                    const float v0 = (g->cell / kColumns) * kCell / atlas_h;
                    const float u1 = u0 + kCell / atlas_w;
                    const float v1 = v0 + kCell / atlas_h;
                    const int base = static_cast<int>(mesh->vertices.size());
                    const Rml::Vector2f corners[4] = {{left, top}, {left + size, top},
                        {left + size, top + size}, {left, top + size}};
                    const Rml::Vector2f uvs[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                    for (int k = 0; k < 4; k++) {
                        mesh->vertices.push_back({corners[k], colour, uvs[k]});
                    }
                    mesh->indices.insert(
                        mesh->indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                }
                x += g->advance * unit;
            }
        }
        return x;
    }
};

MeleeFontEngine s_engine;

}  // namespace

void desktop_melee_font_install(void) {
    if (Rml::GetFontEngineInterface() == &s_engine || Rml::GetFontEngineInterface() == nullptr) {
        return;
    }
    s_engine.base = Rml::GetFontEngineInterface();
    Rml::SetFontEngineInterface(&s_engine);
    if (s_glyphs.empty() && read_cache()) {
        build();
        SDL_Log("desktop-melee: Melee's font from %s", cache_path().c_str());
    }
}

void desktop_melee_font_assets_ready(void) {
    if (!s_glyphs.empty() || !read_game()) {
        return;
    }
    build();
    write_cache();
    if (Rml::GetFontEngineInterface() == &s_engine) {
        Rml::ReleaseFontResources(); /* every element picks its font again */
    }
}
