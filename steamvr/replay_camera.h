// The right eye's camera in the replayed left pass: the camera constant
// buffers the left pass binds, found by their contents and rewritten for the
// right eye. Adapted from natpoh/diablo2-vr-port 8bd1d3b vr/vrcam.cpp namespace
// camfix (Learn, PrevMoved, PrevToRightEye, PatchKnown with replay_proj 3)
// (MIT, Copyright (c) 2026 BodyWalkVR). Pure: steamvr/replay_camera_test.cpp.
//
// D2R's camera buffers (32-bit words) hold records of 192 words at 20, 212,
// 404 and 596, and one more view at 788: the view V (row-major, v*M), its
// inverse at +16, the projection P at +32 and its inverse at +48, V*P at +64
// and its inverse at +80, the last frame's V*P at +96. Camera positions stand
// alone at 4 and 68 (buffers with one) or 112, 304, 496, 688 (with four).
//
// The eyes are parallel: the right eye is the left one moved along the left
// view's x by `shift`. Per-object constants hold left-eye view-space positions
// and are not rewritten, so the move is folded into the projection,
// P_R = T(-shift) * P, and the view stays the left one; V*P and its inverse
// follow. Projections that are not the eye's (orthographic shadow cascades,
// P[11] == 0, or another x scale) and views with no projection beside them
// are left alone.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace replaycam {

// This pair's eyes, from the camera that drew the left one.
struct Eyes {
    double left[16]{};      // the left eye's view, row-major (v*M), world units
    double shift = 0.0;     // the right eye: the left one moved this far along the left view's x, world units
    double projSx = 0.0;    // the eye projection's x scale (P[0]): a record with another is not an eye camera
    double projShift = 0.0; // half the left minus right off-axis term (P[8]); 0 for a symmetric fov
    int viewMask = 0xff;    // which of a buffer's views to rewrite (bit k: the k-th found)
    int positions = 2;      // camera positions moved: 0 none, 1 all, 2 only where a buffer holds exactly one
    bool prev = true;       // the last frame's V*P made the right eye's (in both eyes: the replay drew last)
};

inline bool Rigid(const float* m) {   // rows 0-2 a rotation, column 3 = (0,0,0,1)
    for (int r = 0; r < 3; ++r) {
        double len = 0.0;
        for (int c = 0; c < 3; ++c) len += (double)m[r * 4 + c] * m[r * 4 + c];
        if (std::fabs(len - 1.0) > 1e-3 || std::fabs(m[r * 4 + 3]) > 1e-6) return false;
    }
    return std::fabs(m[15] - 1.0f) < 1e-6;
}
inline bool Near(const float* f, const double* m, int n, double rel, double abs_) {
    for (int i = 0; i < n; ++i) if (std::fabs(f[i] - m[i]) > abs_ + rel * std::fabs(m[i])) return false;
    return true;
}
inline void Put(float* f, const double* m, int n) { for (int i = 0; i < n; ++i) f[i] = (float)m[i]; }
inline bool Inv4(const double M[16], double out[16]) {   // Gauss-Jordan, partial pivoting
    double a[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { a[r][c] = M[r * 4 + c]; a[r][4 + c] = r == c ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 8; ++k) { const double t = a[c][k]; a[c][k] = a[piv][k]; a[piv][k] = t; }
        const double d = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c || a[r][c] == 0.0) continue;
            const double m = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= m * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][4 + c];
    for (int i = 0; i < 16; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}
inline void InvRigid(const double V[16], double out[16]) {   // rotation transposed, translation -t*R^T
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r * 4 + c] = V[c * 4 + r];
    out[3] = out[7] = out[11] = 0.0;
    for (int c = 0; c < 3; ++c) out[12 + c] = -(V[12] * out[c] + V[13] * out[4 + c] + V[14] * out[8 + c]);
    out[15] = 1.0;
}
inline void Mul(const double A[16], const double B[16], double out[16]) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += A[r * 4 + k] * B[k * 4 + c];
        out[r * 4 + c] = s;
    }
}

// Where a camera buffer keeps the view, learned once per (root signature,
// parameter) against the left view, loosely: the head may have turned since.
struct Layout {
    int nv = 0, vo[8] = {};
    uint8_t parts[8] = {};   // 2 its inverse follows, 4 V*P at +64 (P at +32), 8 its inverse at +80
    int nc = 0, co[4] = {};  // the camera position on its own
};
// The view and what goes with it, judged against the left view L: rotation
// within 0.05, translation within 2 units.
inline Layout Learn(const float* f, int n, const double L[16]) {
    Layout lay;
    double iL[16];
    InvRigid(L, iL);
    std::vector<int> covered;   // start and end of each record found
    for (int o = 0; o + 16 <= n && lay.nv < 8; ++o) {
        if (!Rigid(f + o) || !Near(f + o, L, 12, 0.0, 0.05) || !Near(f + o + 12, L + 12, 3, 0.0, 2.0)) continue;
        double V[16], iV[16];
        for (int i = 0; i < 16; ++i) V[i] = f[o + i];
        InvRigid(V, iV);
        uint8_t parts = 0;
        if (o + 32 <= n && Near(f + o + 16, iV, 16, 1e-5, 2e-2)) parts |= 2;
        if (o + 96 <= n) {
            double P[16], Pi[16], vp[16], ivp[16];
            for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; Pi[i] = f[o + 48 + i]; }
            Mul(V, P, vp);
            if (Near(f + o + 64, vp, 16, 1e-4, 1e-2)) {
                parts |= 4;
                Mul(Pi, iV, ivp);
                if (Near(f + o + 80, ivp, 16, 1e-3, 1e-1)) parts |= 8;
            }
        }
        lay.vo[lay.nv] = o; lay.parts[lay.nv] = parts; ++lay.nv;
        // The whole record: a translation row of one of its matrices (the
        // inverse V*P of a shadow cascade turned as the eye is holds the
        // eye's place) is no camera position on its own. (Upstream covers
        // the view and its inverse only.)
        covered.push_back(o);
        covered.push_back(o + ((parts & 4) ? 112 : (parts & 2) ? 32 : 16));
        o += 15;
    }
    for (int o = 0; o + 3 <= n && lay.nc < 4; ++o) {
        if (!Near(f + o, iL + 12, 3, 0.0, 0.25) || std::fabs(f[o]) < 10.0f) continue;   // out in the world, not a small number
        bool inside = false;
        for (size_t c = 0; c + 1 < covered.size(); c += 2) if (o + 3 > covered[c] && o < covered[c + 1]) inside = true;
        if (inside) continue;
        lay.co[lay.nc++] = o;
        o += 2;
    }
    return lay;
}

// The eye's projection: our x scale, and perspective (an orthographic shadow
// cascade has P[11] == 0).
inline bool EyeProjection(const float* P, const Eyes& e) {
    return e.projSx != 0.0 && std::fabs(P[0] - e.projSx) <= 1e-3 * std::fabs(e.projSx) && P[11] != 0.0f;
}
// The right eye's projection from the left one: its own off-axis term, then
// T(-shift) * P (row-major: only row 3 changes).
inline void RightProjection(const double P[16], const Eyes& e, double Pr[16]) {
    for (int i = 0; i < 16; ++i) Pr[i] = P[i];
    Pr[8] -= 2.0 * e.projShift;
    for (int c = 0; c < 4; ++c) Pr[12 + c] = Pr[12 + c] - e.shift * Pr[c];
}

// The last frame's V*P (at +96) moved to the right eye's frustum:
// prev * inverse(P) * P_R. False when P is not the eye's.
inline bool PrevMoved(const float* f, int o, const Eyes& e, double out[16]) {
    if (!EyeProjection(f + o + 32, e)) return false;
    double P[16], Pr[16], Pi[16], prev[16], C[16];
    for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; prev[i] = f[o + 96 + i]; }
    RightProjection(P, e, Pr);
    if (!Inv4(P, Pi)) return false;
    Mul(Pi, Pr, C);
    Mul(prev, C, out);
    for (int i = 0; i < 16; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}
// Both eyes' values: each record's last-frame V*P made the right eye's - the
// replay drew last, so the history both eyes' passes read is the right eye's.
inline int PrevToRightEye(float* f, int n, const Layout& lay, const Eyes& e) {
    if (!e.prev) return 0;
    int done = 0;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(e.viewMask & (1 << k)) || !(lay.parts[k] & 4)) continue;
        const int o = lay.vo[k];
        if (o + 112 > n) continue;
        double out[16];
        if (PrevMoved(f, o, e, out)) { Put(f + o + 96, out, 16); ++done; }
    }
    return done;
}

struct Patched { int folded = 0, positions = 0; };
// The right eye's values over a copy of the left eye's, by the learned layout.
inline Patched PatchRight(float* f, int n, const Layout& lay, const Eyes& e) {
    Patched out;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(e.viewMask & (1 << k)) || !(lay.parts[k] & 4)) continue;   // no projection beside it: what reads it pairs it with one carrying the shift
        const int o = lay.vo[k];
        if (o + 80 > n || !Rigid(f + o) || !EyeProjection(f + o + 32, e)) continue;
        double V[16], P[16], Pr[16], Pi[16], vp[16];
        for (int i = 0; i < 16; ++i) { V[i] = f[o + i]; P[i] = f[o + 32 + i]; }
        RightProjection(P, e, Pr);
        const bool piOk = Inv4(Pr, Pi);
        Put(f + o + 32, Pr, 16);
        if (piOk) Put(f + o + 48, Pi, 16);
        Mul(V, Pr, vp);
        Put(f + o + 64, vp, 16);
        if ((lay.parts[k] & 8) && o + 96 <= n && piOk) {
            double iV[16], ivp[16];
            InvRigid(V, iV);
            Mul(Pi, iV, ivp);
            Put(f + o + 80, ivp, 16);
        }
        ++out.folded;
    }
    if (e.positions == 1 || (e.positions == 2 && lay.nc == 1)) {
        const double ax[3] = {e.left[0], e.left[4], e.left[8]};   // the left view's right, in the world
        for (int k = 0; k < lay.nc; ++k) {
            const int o = lay.co[k];
            if (o + 3 > n) continue;
            for (int i = 0; i < 3; ++i) f[o + i] = (float)(f[o + i] + ax[i] * e.shift);
            ++out.positions;
        }
    }
    return out;
}

}  // namespace replaycam
