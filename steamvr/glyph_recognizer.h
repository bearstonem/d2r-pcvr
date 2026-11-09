#pragma once
// Glyph casting (owner 2026-11-01: "a glyph drawing system where we can draw a glyph in the air ... for
// applying passive skills and spells"): while a hand holds its "Draw glyph" button, its path is the
// stroke; on release the stroke is matched against eight shapes, and the skill the player put on that
// shape (the wheel's GLYPHS page) is cast.
//
// The match is $P (Vatavu, Anthony, Wobbrock 2012): both clouds resampled to N points, scaled to a
// unit box and centred, then greedily paired point to point. A cloud has no order, so where a shape
// starts and which way round it goes do not matter. Its mirror image would, so the stroke is compared
// mirrored too (a bolt zigzagging either way, a wave starting up or down), a little turned and brought
// towards each shape's proportions (Views, Proportioned). It is never turned far, so up stays up (a V
// is not a caret). A stroke too small, too short or too long is no glyph, and so is one whose best
// shape is not clearly better than the next.
//
// Value-only: steamvr/glyph_recognizer_test.cpp. The transport flattens the hand's path onto the plane
// it was drawn in (Flatten).
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
constexpr float MaxScore = 1.8f;          // $P distance of the best shape at most this (drawn shapes ~0-1.3)...
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

// Scaled to a unit box keeping its proportions, centred on its centroid.
inline Cloud Unit(Cloud c) {
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
// Resampled, then Unit.
inline Cloud Normalize(const Cloud& in, int n = N) { return Unit(Resample(in, n)); }
// Height over width.
inline float Aspect(const Cloud& c) {
    if (c.empty()) return 1;
    float minX = c[0].x, maxX = c[0].x, minY = c[0].y, maxY = c[0].y;
    for (const auto& p : c) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    return maxX - minX > 1e-6f ? (maxY - minY) / (maxX - minX) : 1;
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
            if (!matched[j]) {
                const float dx = a[i].x - b[j].x, dy = a[i].y - b[j].y, d = dx * dx + dy * dy;
                if (d < best) { best = d; index = j; }
            }
        matched[index] = true;
        const float weight = 1.0f - float((i + n - start) % n) / float(n);
        sum += weight * std::sqrt(best);
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

// The stroke as the templates are compared with it: resampled, as drawn and mirrored left to right
// (a bolt zigzags either way, a wave starts up or down, a spiral winds either way round), each also
// turned TiltDegrees and twice that either way (a square drawn askew). Never upside down.
constexpr float TiltDegrees = 10;
constexpr int TiltSteps = 2;   // turns each way
inline std::vector<Cloud> Views(const Cloud& stroke) {
    const Cloud r = Resample(stroke, N);
    std::vector<Cloud> out;
    for (int mirror = 0; mirror < 2; ++mirror)
        for (int k = -TiltSteps; k <= TiltSteps; ++k) {
            const float a = float(k) * TiltDegrees * 3.14159265f / 180, c = std::cos(a), s = std::sin(a);
            Cloud turned(r.size());
            for (size_t i = 0; i < r.size(); ++i) {
                const float x = mirror ? -r[i].x : r[i].x, y = r[i].y;
                turned[i] = {c * x - s * y, s * x + c * y};
            }
            out.push_back(turned);
        }
    return out;
}
// A view brought towards a shape's proportions (aspect: its height over width) by at most MaxStretch
// either way, then Unit: a square drawn as a rectangle, a circle as an oval, or either seen from an
// angle, is still that shape.
constexpr float MaxStretch = 1.4f;
inline Cloud Proportioned(Cloud view, float aspect) {
    const float have = Aspect(view);
    const float k = have > 1e-6f ? std::clamp(aspect / have, 1 / MaxStretch, MaxStretch) : 1;
    for (auto& p : view) p.y *= k;
    return Unit(std::move(view));
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
// measured on drawings jittered, turned, stretched, mirrored and drawn backwards: glyph_recognizer_test).
struct CornerRange { int low, high; };
inline CornerRange CornersOf(Shape s) {
    switch (s) {
    case Shape::Circle: return {0, 1};
    case Shape::Triangle: return {2, 4};
    case Shape::Square: return {3, 5};
    case Shape::Lightning: return {2, 3};
    case Shape::Spiral: return {0, 3};   // a squeezed spiral's inner turns can turn as sharply as a corner
    case Shape::V: return {1, 2};
    case Shape::Star: return {4, 6};
    case Shape::Wave: return {3, 7};   // its crests turn as sharply as corners: two swells or three and a half
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
    const auto views = Views(stroke);
    r.corners = Corners(stroke);
    float best = std::numeric_limits<float>::infinity(), second = best;
    int at = -1;
    for (int i = 0; i < ShapeCount; ++i) {
        const auto range = CornersOf(Shape(i));
        if (r.corners < range.low || r.corners > range.high) continue;   // the wrong number of corners
        if (!Built(Shape(i), stroke, std::max(maxX - minX, maxY - minY))) continue;   // not open / closed / one way as the shape is
        const auto& shape = Templates()[size_t(i)];
        const float aspect = Aspect(shape);
        float d = std::numeric_limits<float>::infinity();
        for (const auto& view : views) d = std::min(d, Match(Proportioned(view, aspect), shape));
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

// The hand's path in the room (metres, y up) flattened for Recognize onto the plane it was drawn in:
// the best fit through its points, up as near the room's up as that plane allows (on a plane lying
// nearly flat, away from the player) and right towards the player's right. A shape drawn off to the
// side, low or high, on a plane turned or tilted from the player's face, keeps its proportions. A path
// too deep for its plane (its depth more than PlaneDepth of its next extent) or a line has none: it
// goes onto the upright plane facing the player (rightX, rightZ: the player's level right).
struct Spot { float x = 0, y = 0, z = 0; };
struct Flat {
    Cloud stroke;          // relative to the path's first point
    bool own = false;      // on its own plane (else facing the player)
    float degrees = 0;     // that plane's turn from facing the player
};
constexpr float PlaneDepth = .5f;   // as a share of standard deviations across the plane's thin side
constexpr float FlatUp = .35f;      // up's share left in a plane tilted this far (about 70 degrees) or more: away is up

// Jacobi: a symmetric 3x3 matrix's eigenvalues and their eigenvectors (columns of v).
inline void Eigen3(double a[3][3], double values[3], double v[3][3]) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) v[i][j] = i == j;
    for (int sweep = 0; sweep < 16; ++sweep)
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-15) continue;
                const double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                const double t = (theta >= 0 ? 1 : -1) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 3; ++k) { const double kp = a[k][p], kq = a[k][q]; a[k][p] = c * kp - s * kq; a[k][q] = s * kp + c * kq; }
                for (int k = 0; k < 3; ++k) { const double pk = a[p][k], qk = a[q][k]; a[p][k] = c * pk - s * qk; a[q][k] = s * pk + c * qk; }
                for (int k = 0; k < 3; ++k) { const double kp = v[k][p], kq = v[k][q]; v[k][p] = c * kp - s * kq; v[k][q] = s * kp + c * kq; }
            }
    for (int i = 0; i < 3; ++i) values[i] = a[i][i];
}

inline Flat Flatten(const std::vector<Spot>& path, float rightX, float rightZ) {
    Flat out;
    if (path.empty()) return out;
    const Spot first = path.front();
    const auto facing = [&] {
        Plane p; p.originX = first.x; p.originY = first.y; p.originZ = first.z; p.rightX = rightX; p.rightZ = rightZ;
        for (const auto& s : path) out.stroke.push_back(p.Project(s.x, s.y, s.z));
        return out;
    };
    if (path.size() < 3) return facing();
    double mean[3] = {};
    for (const auto& s : path) { mean[0] += s.x; mean[1] += s.y; mean[2] += s.z; }
    for (double& m : mean) m /= double(path.size());
    double cov[3][3] = {};
    for (const auto& s : path) {
        const double d[3] = {s.x - mean[0], s.y - mean[1], s.z - mean[2]};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) cov[i][j] += d[i] * d[j];
    }
    double values[3], v[3][3];
    Eigen3(cov, values, v);
    int thin = 0, wide = 0;   // the smallest and largest spread
    for (int i = 1; i < 3; ++i) { if (values[i] < values[thin]) thin = i; if (values[i] > values[wide]) wide = i; }
    const int mid = 3 - thin - wide;
    if (thin == wide || !(values[mid] > 1e-9) || values[thin] > PlaneDepth * PlaneDepth * values[mid]) return facing();
    float n[3] = {float(v[0][thin]), float(v[1][thin]), float(v[2][thin])};
    // Up: the room's up within the plane; on a plane lying nearly flat, the player's forward (away).
    const float forward[3] = {rightZ, 0, -rightX};
    float up[3] = {-n[1] * n[0], 1 - n[1] * n[1], -n[1] * n[2]};
    float upLength = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (upLength < FlatUp) {
        const float f = forward[0] * n[0] + forward[2] * n[2];
        for (int i = 0; i < 3; ++i) up[i] = forward[i] - f * n[i];
        upLength = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (upLength < 1e-4f) return facing();
    }
    for (float& u : up) u /= upLength;
    float right[3] = {up[1] * n[2] - up[2] * n[1], up[2] * n[0] - up[0] * n[2], up[0] * n[1] - up[1] * n[0]};
    if (right[0] * rightX + right[2] * rightZ < 0) for (float& r : right) r = -r;
    for (const auto& s : path) {
        const float d[3] = {s.x - first.x, s.y - first.y, s.z - first.z};
        out.stroke.push_back({d[0] * right[0] + d[1] * right[1] + d[2] * right[2], d[0] * up[0] + d[1] * up[1] + d[2] * up[2]});
    }
    out.own = true;
    out.degrees = std::acos(std::clamp(std::fabs(n[0] * forward[0] + n[2] * forward[2]), 0.0f, 1.0f)) * 57.29578f;
    return out;
}

}  // namespace framevr::glyphs
