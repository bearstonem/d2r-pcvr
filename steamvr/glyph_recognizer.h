#pragma once
// Glyph casting (owner 2026-11-01: "a glyph drawing system where we can draw a glyph in the air ... for
// applying passive skills and spells"): while a hand holds its "Draw glyph" button, its path is the
// stroke; on release the stroke is matched against eight shapes, and the skill the player put on that
// shape (the wheel's GLYPHS page) is cast.
//
// The match is $P (Vatavu, Anthony, Wobbrock 2012): both clouds resampled to N points, scaled to a
// unit box keeping their proportions and centred, then greedily paired point to point. A cloud has no
// order, so the direction a shape is drawn in and where it starts do not matter; it is not turned, so
// up stays up (a V is not a caret). A stroke too small, too short or too long is no glyph, and so is
// one whose best shape is not clearly better than the next.
//
// Value-only: steamvr/glyph_recognizer_test.cpp. The transport projects the hand's path onto the
// upright plane facing the player as the stroke began (Project).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace framevr::glyphs {

enum class Shape : int { Circle, Triangle, Square, Lightning, Spiral, V, Star, Wave, Count, None = -1 };
constexpr int ShapeCount = int(Shape::Count);
inline const char* Name(Shape s) {
    switch (s) {
    case Shape::Circle: return "circle";
    case Shape::Triangle: return "triangle";
    case Shape::Square: return "square";
    case Shape::Lightning: return "lightning bolt";
    case Shape::Spiral: return "spiral";
    case Shape::V: return "V";
    case Shape::Star: return "star";
    case Shape::Wave: return "wave";
    default: return "none";
    }
}

struct Point { float x = 0, y = 0; };
using Cloud = std::vector<Point>;

constexpr int N = 32;                     // points per cloud
constexpr float MinExtent = .12f;         // m: the stroke's larger side at least this
constexpr float MinLength = .25f;         // m: its path at least this long
constexpr float MaxSeconds = 4.0f;        // a hold longer than this is no glyph
constexpr float MaxScore = 2.0f;          // $P distance of the best shape at most this (drawn shapes ~0-1.4)...
constexpr float MinMargin = .14f;         // ...and the next shape at least this share farther

inline float Distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
inline float PathLength(const Cloud& c) {
    float d = 0;
    for (size_t i = 1; i < c.size(); ++i) d += Distance(c[i - 1], c[i]);
    return d;
}

// n points evenly spaced along the path.
inline Cloud Resample(const Cloud& in, int n = N) {
    Cloud out;
    if (in.empty() || n < 2) return out;
    const float total = PathLength(in);
    if (!(total > 0)) { out.assign(size_t(n), in.front()); return out; }
    const float step = total / float(n - 1);
    out.push_back(in.front());
    float carried = 0;
    Point prev = in.front();
    for (size_t i = 1; i < in.size() && int(out.size()) < n; ++i) {
        Point cur = in[i];
        float d = Distance(prev, cur);
        while (d > 0 && carried + d >= step && int(out.size()) < n) {
            const float t = (step - carried) / d;
            const Point q{prev.x + t * (cur.x - prev.x), prev.y + t * (cur.y - prev.y)};
            out.push_back(q);
            prev = q;
            d = Distance(prev, cur);
            carried = 0;
        }
        carried += d;
        prev = cur;
    }
    while (int(out.size()) < n) out.push_back(in.back());
    return out;
}

// Resampled, scaled to a unit box keeping its proportions, centred on its centroid.
inline Cloud Normalize(const Cloud& in, int n = N) {
    Cloud c = Resample(in, n);
    if (c.empty()) return c;
    float minX = c[0].x, maxX = c[0].x, minY = c[0].y, maxY = c[0].y;
    for (const auto& p : c) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    const float size = std::max(maxX - minX, maxY - minY);
    const float s = size > 1e-6f ? 1.0f / size : 1.0f;
    float cx = 0, cy = 0;
    for (auto& p : c) { p.x = (p.x - minX) * s; p.y = (p.y - minY) * s; cx += p.x; cy += p.y; }
    cx /= float(c.size()); cy /= float(c.size());
    for (auto& p : c) { p.x -= cx; p.y -= cy; }
    return c;
}

inline float CloudDistance(const Cloud& a, const Cloud& b, size_t start) {
    const size_t n = a.size();
    std::array<bool, 256> matched{};
    float sum = 0;
    size_t i = start;
    do {
        float best = std::numeric_limits<float>::infinity();
        size_t index = 0;
        for (size_t j = 0; j < n; ++j)
            if (!matched[j]) { const float d = Distance(a[i], b[j]); if (d < best) { best = d; index = j; } }
        matched[index] = true;
        const float weight = 1.0f - float((i + n - start) % n) / float(n);
        sum += weight * best;
        i = (i + 1) % n;
    } while (i != start);
    return sum;
}
// Both clouds normalized to the same N.
inline float Match(const Cloud& a, const Cloud& b) {
    const size_t n = a.size();
    if (n == 0 || n != b.size() || n > 256) return std::numeric_limits<float>::infinity();
    const size_t step = std::max<size_t>(1, size_t(std::floor(std::pow(double(n), 0.5))));
    float best = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < n; i += step) best = std::min({best, CloudDistance(a, b, i), CloudDistance(b, a, i)});
    return best;
}

// Sharp corners along the path, in order: the path resampled to 48 points and smoothed, a corner
// where it turns more than CornerDegrees between the points two before and two after; a run of
// turning points counts once. Closed shapes started at a corner lose that one (the ranges allow it).
constexpr float CornerDegrees = 45;   // a wobbly square's corners measure 50-65 over the window
inline int Corners(const Cloud& stroke) {
    Cloud c = Resample(stroke, 48);
    if (c.size() < 8) return 0;
    Cloud s = c;
    for (size_t i = 1; i + 1 < c.size(); ++i) s[i] = {(c[i - 1].x + c[i].x + c[i + 1].x) / 3, (c[i - 1].y + c[i].y + c[i + 1].y) / 3};
    int corners = 0;
    bool in = false;
    for (size_t i = 2; i + 2 < s.size(); ++i) {
        const float ax = s[i].x - s[i - 2].x, ay = s[i].y - s[i - 2].y, bx = s[i + 2].x - s[i].x, by = s[i + 2].y - s[i].y;
        const float la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        bool sharp = false;
        if (la > 1e-6f && lb > 1e-6f) {
            const float cosA = std::clamp((ax * bx + ay * by) / (la * lb), -1.0f, 1.0f);
            sharp = std::acos(cosA) * 57.29578f > CornerDegrees;
        }
        if (sharp && !in) ++corners;
        in = sharp;
    }
    return corners;
}
// The corners a drawing of each shape may show (smooth shapes may pick one up from a shaky hand;
// measured on drawings jittered, turned, stretched and drawn backwards: glyph_recognizer_test).
struct CornerRange { int low, high; };
inline CornerRange CornersOf(Shape s) {
    switch (s) {
    case Shape::Circle: return {0, 1};
    case Shape::Triangle: return {2, 4};
    case Shape::Square: return {3, 5};
    case Shape::Lightning: return {2, 3};
    case Shape::Spiral: return {0, 2};
    case Shape::V: return {1, 2};
    case Shape::Star: return {4, 6};
    case Shape::Wave: return {3, 5};   // its crests turn as sharply as corners
    default: return {0, 0};
    }
}
constexpr float MinAspect = .2f;   // the smaller side at least this share of the larger: a line is no glyph
constexpr float MaxGap = .3f;      // a closed shape ends within this share of its size of where it began
constexpr float MinOneWay = .85f;  // a wave, V or bolt travels this share of its path one way across (or down)

// The shape's own build, beyond its points: closed shapes end where they began; a wave and a V travel
// across one way, a bolt down (or up) one way. A scribble seldom does both, so it seldom matches.
inline float OneWay(const Cloud& c, bool across) {
    float forward = 0, back = 0;
    for (size_t i = 1; i < c.size(); ++i) {
        const float d = across ? c[i].x - c[i - 1].x : c[i].y - c[i - 1].y;
        (d >= 0 ? forward : back) += std::fabs(d);
    }
    const float total = forward + back;
    return total > 0 ? std::max(forward, back) / total : 0;
}
inline bool Built(Shape s, const Cloud& stroke, float size) {
    switch (s) {
    case Shape::Circle: case Shape::Triangle: case Shape::Square: case Shape::Star:
        return Distance(stroke.front(), stroke.back()) <= MaxGap * size;
    case Shape::Wave: case Shape::V: return OneWay(Resample(stroke, N), true) >= MinOneWay;
    case Shape::Lightning: return OneWay(Resample(stroke, N), false) >= MinOneWay;
    default: return true;
    }
}

// The shapes as drawn (y up), each a polyline in a unit box.
inline Cloud Polyline(std::initializer_list<Point> corners, int perSegment = 16) {
    Cloud out;
    const std::vector<Point> c(corners);
    for (size_t i = 1; i < c.size(); ++i)
        for (int k = 0; k < perSegment; ++k) {
            const float t = float(k) / float(perSegment);
            out.push_back({c[i - 1].x + t * (c[i].x - c[i - 1].x), c[i - 1].y + t * (c[i].y - c[i - 1].y)});
        }
    out.push_back(c.back());
    return out;
}
inline Cloud Template(Shape s) {
    constexpr float Pi = 3.14159265f;
    Cloud c;
    switch (s) {
    case Shape::Circle:
        for (int i = 0; i <= 64; ++i) { const float a = 2 * Pi * float(i) / 64; c.push_back({.5f + .5f * std::cos(a), .5f + .5f * std::sin(a)}); }
        break;
    case Shape::Triangle: c = Polyline({{.5f, 1}, {1, 0}, {0, 0}, {.5f, 1}}); break;
    case Shape::Square: c = Polyline({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}); break;
    case Shape::Lightning: c = Polyline({{.25f, 1}, {.75f, .58f}, {.3f, .42f}, {.75f, 0}}); break;   // a bolt from the top down
    case Shape::Spiral:
        for (int i = 0; i <= 96; ++i) {   // outside in, two and a quarter turns
            const float t = float(i) / 96, a = t * 2.25f * 2 * Pi, r = .5f * (1 - .85f * t);
            c.push_back({.5f + r * std::cos(a), .5f + r * std::sin(a)});
        }
        break;
    case Shape::V: c = Polyline({{0, 1}, {.5f, 0}, {1, 1}}); break;
    case Shape::Star: {   // a pentagram: every second point of a pentagon, point up
        std::vector<Point> p;
        for (int k = 0; k <= 5; ++k) { const float a = Pi / 2 + float(k * 2 % 5) * 2 * Pi / 5; p.push_back({.5f + .5f * std::cos(a), .5f + .5f * std::sin(a)}); }
        for (size_t i = 1; i < p.size(); ++i)
            for (int k = 0; k < 16; ++k) { const float t = float(k) / 16; c.push_back({p[i - 1].x + t * (p[i].x - p[i - 1].x), p[i - 1].y + t * (p[i].y - p[i - 1].y)}); }
        c.push_back(p.back());
        break;
    }
    case Shape::Wave:   // two swells left to right
        for (int i = 0; i <= 64; ++i) { const float x = float(i) / 64; c.push_back({x, .5f + .22f * std::sin(x * 2 * 2 * Pi)}); }
        break;
    default: break;
    }
    return c;
}
inline const std::array<Cloud, ShapeCount>& Templates() {
    static const std::array<Cloud, ShapeCount> t = [] {
        std::array<Cloud, ShapeCount> out;
        for (int i = 0; i < ShapeCount; ++i) out[size_t(i)] = Normalize(Template(Shape(i)));
        return out;
    }();
    return t;
}

struct Result {
    Shape shape = Shape::None;     // None: no glyph
    Shape nearest = Shape::None;   // the best shape, accepted or not (for the log)
    float score = 0, second = 0;   // $P distances of the best and the next shape
    int corners = 0;               // sharp corners in the stroke (Corners)
    const char* why = "";          // why it is no glyph
};
// stroke: the drawn path in metres on the drawing plane (y up); seconds: how long it took.
inline Result Recognize(const Cloud& stroke, float seconds) {
    Result r;
    if (stroke.size() < 4) { r.why = "too few points"; return r; }
    float minX = stroke[0].x, maxX = minX, minY = stroke[0].y, maxY = minY;
    for (const auto& p : stroke) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    if (std::max(maxX - minX, maxY - minY) < MinExtent) { r.why = "too small"; return r; }
    if (PathLength(stroke) < MinLength) { r.why = "too short"; return r; }
    if (!(seconds <= MaxSeconds)) { r.why = "held too long"; return r; }
    if (std::min(maxX - minX, maxY - minY) < MinAspect * std::max(maxX - minX, maxY - minY)) { r.why = "a line, not a shape"; return r; }
    const Cloud c = Normalize(stroke);
    r.corners = Corners(stroke);
    float best = std::numeric_limits<float>::infinity(), second = best;
    int at = -1;
    for (int i = 0; i < ShapeCount; ++i) {
        const auto range = CornersOf(Shape(i));
        if (r.corners < range.low || r.corners > range.high) continue;   // the wrong number of corners
        if (!Built(Shape(i), stroke, std::max(maxX - minX, maxY - minY))) continue;   // not open / closed / one way as the shape is
        const float d = Match(c, Templates()[size_t(i)]);
        if (d < best) { second = best; best = d; at = i; }
        else if (d < second) second = d;
    }
    if (at < 0) { r.why = "no glyph has that many corners and that build"; return r; }
    r.nearest = Shape(at); r.score = best; r.second = second;
    if (!(best <= MaxScore)) { r.why = "not like any glyph"; return r; }
    if (!(second - best >= MinMargin * second)) { r.why = "between two glyphs"; return r; }
    r.shape = Shape(at);
    return r;
}

// The hand's path onto the upright plane facing the player as the stroke began: x along the head's
// right (level), y straight up; positions in metres, the head's yaw as radians (0: facing -z).
struct Plane {
    float originX = 0, originY = 0, originZ = 0;
    float rightX = 1, rightZ = 0;   // level unit vector to the player's right
    static Plane Facing(float x, float y, float z, float yaw) {
        Plane p; p.originX = x; p.originY = y; p.originZ = z;
        p.rightX = std::cos(yaw); p.rightZ = -std::sin(yaw);
        return p;
    }
    Point Project(float x, float y, float z) const {
        return {(x - originX) * rightX + (z - originZ) * rightZ, y - originY};
    }
};

}  // namespace framevr::glyphs
