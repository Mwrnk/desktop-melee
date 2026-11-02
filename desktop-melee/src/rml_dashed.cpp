/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "dashed" RmlUi decorator (rml_dashed.h): the element's rounded
 * outline, inset by half the line width, cut into dashes. Each dash is a
 * strip of quads with a soft half-pixel edge on both sides. */
#include "rml_dashed.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using Rml::Vector2f;

constexpr float kPi = 3.14159265f;
constexpr int kArcSteps = 12;

/* The outline, clockwise from the top edge's left end, closed. */
std::vector<Vector2f> outline(Vector2f size, const Rml::CornerSizes& radii, float inset) {
    const float x0 = inset;
    const float y0 = inset;
    const float x1 = size.x - inset;
    const float y1 = size.y - inset;
    const float limit = std::max(0.f, std::min(x1 - x0, y1 - y0) / 2);
    /* RmlUi's order: top-left, top-right, bottom-right, bottom-left. */
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = std::clamp(radii[i] - inset, 0.f, limit);
    }
    const struct {
        float cx, cy, radius, from;
    } corners[4] = {
        {x1 - r[1], y0 + r[1], r[1], -kPi / 2},
        {x1 - r[2], y1 - r[2], r[2], 0},
        {x0 + r[3], y1 - r[3], r[3], kPi / 2},
        {x0 + r[0], y0 + r[0], r[0], kPi},
    };
    std::vector<Vector2f> points;
    points.push_back({x0 + r[0], y0});
    for (const auto& c : corners) {
        for (int step = 0; step <= kArcSteps; step++) {
            const float a = c.from + (kPi / 2) * step / kArcSteps;
            points.push_back({c.cx + c.radius * std::cos(a), c.cy + c.radius * std::sin(a)});
        }
    }
    points.push_back(points.front());
    return points;
}

Vector2f normal(Vector2f a, Vector2f b) {
    const Vector2f d = b - a;
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    return length > 0 ? Vector2f{-d.y / length, d.x / length} : Vector2f{0, 0};
}

/* One dash: a polyline drawn as four rows of vertices across it (soft
 * edge, solid, solid, soft edge), joined with mitred normals. */
void add_stroke(Rml::Mesh& mesh, const std::vector<Vector2f>& line, float half,
    Rml::ColourbPremultiplied colour) {
    if (line.size() < 2) {
        return;
    }
    const float inner = std::max(half - 0.5f, 0.f);
    const float outer = half + 0.5f;
    const float offsets[4] = {-outer, -inner, inner, outer};
    const Rml::ColourbPremultiplied clear{0, 0, 0, 0};
    const int first = static_cast<int>(mesh.vertices.size());
    for (size_t i = 0; i < line.size(); i++) {
        const Vector2f before = i > 0 ? normal(line[i - 1], line[i]) : normal(line[0], line[1]);
        const Vector2f after = i + 1 < line.size() ? normal(line[i], line[i + 1]) : before;
        Vector2f n = before + after;
        const float length = std::sqrt(n.x * n.x + n.y * n.y);
        n = length > 0 ? n / length : before;
        const float cosine = n.x * before.x + n.y * before.y;
        const float miter = cosine > 0.5f ? 1.f / cosine : 1.f;
        for (int row = 0; row < 4; row++) {
            Rml::Vertex v;
            v.position = line[i] + n * (offsets[row] * miter);
            v.colour = row == 0 || row == 3 ? clear : colour;
            v.tex_coord = {0, 0};
            mesh.vertices.push_back(v);
        }
    }
    for (int i = 0; i + 1 < static_cast<int>(line.size()); i++) {
        for (int row = 0; row < 3; row++) {
            const int a = first + i * 4 + row;
            const int b = a + 4;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
}

/* The point at distance s along a polyline with cumulative lengths. */
Vector2f point_at(const std::vector<Vector2f>& points, const std::vector<float>& lengths, float s) {
    const auto it = std::upper_bound(lengths.begin(), lengths.end(), s);
    const size_t i = std::clamp<size_t>(it - lengths.begin(), 1, points.size() - 1);
    const float span = lengths[i] - lengths[i - 1];
    const float t = span > 0 ? (s - lengths[i - 1]) / span : 0;
    return points[i - 1] + (points[i] - points[i - 1]) * t;
}

class DashedBorder : public Rml::Decorator {
public:
    DashedBorder(Rml::NumericValue width, Rml::NumericValue dash, Rml::NumericValue gap)
        : width_(width), dash_(dash), gap_(gap) {}

    Rml::DecoratorDataHandle GenerateElementData(
        Rml::Element* element, Rml::BoxArea) const override {
        Rml::Context* context = element->GetContext();
        if (context == nullptr) {
            return INVALID_DECORATORDATAHANDLE;
        }
        auto* geometry = new Rml::Geometry(context->GetRenderManager().MakeGeometry(mesh(element)));
        return reinterpret_cast<Rml::DecoratorDataHandle>(geometry);
    }

    void ReleaseElementData(Rml::DecoratorDataHandle data) const override {
        delete reinterpret_cast<Rml::Geometry*>(data);
    }

    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle data) const override {
        reinterpret_cast<Rml::Geometry*>(data)->Render(
            element->GetAbsoluteOffset(Rml::BoxArea::Border));
    }

private:
    Rml::Mesh mesh(Rml::Element* element) const {
        Rml::Mesh mesh;
        const auto& computed = element->GetComputedValues();
        const float width = element->ResolveLength(width_);
        const float dash = element->ResolveLength(dash_);
        const float gap = element->ResolveLength(gap_);
        const Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
        if (width <= 0 || dash <= 0 || size.x <= width || size.y <= width) {
            return mesh;
        }
        const std::vector<Vector2f> points = outline(size, computed.border_radius(), width / 2);
        std::vector<float> lengths(points.size(), 0.f);
        for (size_t i = 1; i < points.size(); i++) {
            const Vector2f d = points[i] - points[i - 1];
            lengths[i] = lengths[i - 1] + std::sqrt(d.x * d.x + d.y * d.y);
        }
        /* A whole number of dashes around, so the last meets the first. */
        const float perimeter = lengths.back();
        const int count = std::max(1, static_cast<int>(std::lround(perimeter / (dash + gap))));
        const float period = perimeter / count;
        const float on = period * dash / (dash + gap);
        const auto colour = computed.image_color().ToPremultiplied(computed.opacity());
        std::vector<Vector2f> line;
        for (int k = 0; k < count; k++) {
            const float from = k * period;
            const float to = from + on;
            line.clear();
            line.push_back(point_at(points, lengths, from));
            for (size_t i = 0; i < points.size(); i++) {
                if (lengths[i] > from && lengths[i] < to) {
                    line.push_back(points[i]);
                }
            }
            line.push_back(point_at(points, lengths, to));
            add_stroke(mesh, line, width / 2, colour);
        }
        return mesh;
    }

    Rml::NumericValue width_;
    Rml::NumericValue dash_;
    Rml::NumericValue gap_;
};

class DashedInstancer : public Rml::DecoratorInstancer {
public:
    DashedInstancer() {
        width_ = RegisterProperty("width", "1.5dp").AddParser("length").GetId();
        dash_ = RegisterProperty("dash", "6dp").AddParser("length").GetId();
        gap_ = RegisterProperty("gap", "5dp").AddParser("length").GetId();
        RegisterShorthand("decorator", "width, dash, gap", Rml::ShorthandType::FallThrough);
    }

    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&,
        const Rml::PropertyDictionary& properties,
        const Rml::DecoratorInstancerInterface&) override {
        const Rml::Property* width = properties.GetProperty(width_);
        const Rml::Property* dash = properties.GetProperty(dash_);
        const Rml::Property* gap = properties.GetProperty(gap_);
        if (width == nullptr || dash == nullptr || gap == nullptr) {
            return nullptr;
        }
        return Rml::MakeShared<DashedBorder>(
            width->GetNumericValue(), dash->GetNumericValue(), gap->GetNumericValue());
    }

private:
    Rml::PropertyId width_;
    Rml::PropertyId dash_;
    Rml::PropertyId gap_;
};

}  // namespace

void desktop_melee_rml_dashed_register() {
    static DashedInstancer* instancer = nullptr;
    if (instancer == nullptr) {
        instancer = new DashedInstancer();
        Rml::Factory::RegisterDecoratorInstancer("dashed", instancer);
    }
}
