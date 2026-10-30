#include "companion_item_skills_ui.h"
#include "interaction_icons.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace framevr::companionui {

namespace {

const char* GetBodyLocationName(int loc) {
    switch (loc) {
        case 1: return "Casco";
        case 3: return "Armadura";
        case 4: return "Arma principal";
        case 5: return "Escudo / Secundaria";
        default: return "Equipamiento";
    }
}

// Uniform 1:1 aspect ratio icon painter using native sprite alpha
void DrawIcon(
    const interaction::icons::Image& img,
    uint8_t* dst, int dstW, int dstH,
    int destX, int destY, int targetW, int targetH)
{
    if (img.width == 0 || img.height == 0 || img.rgba.empty()) return;

    // Maintain exact square 1:1 aspect ratio
    const float scale = std::min(float(targetW) / static_cast<float>(img.width),
                                 float(targetH) / static_cast<float>(img.height));
    const int w = std::max(1, static_cast<int>(img.width * scale));
    const int h = std::max(1, static_cast<int>(img.height * scale));
    const int left = destX + (targetW - w) / 2;
    const int top = destY + (targetH - h) / 2;

    for (int y = 0; y < h; ++y) {
        const int outY = top + y;
        if (outY < 0 || outY >= dstH) continue;

        const unsigned srcY = std::min(img.height - 1, static_cast<unsigned>(y / scale));
        const auto* pRow = &img.rgba[srcY * img.width * 4];

        for (int x = 0; x < w; ++x) {
            const int outX = left + x;
            if (outX < 0 || outX >= dstW) continue;

            const unsigned srcX = std::min(img.width - 1, static_cast<unsigned>(x / scale));
            const auto* p = &pRow[srcX * 4];
            const float a = p[3] / 255.0f;
            if (a <= 0.02f) continue;

            const size_t dIdx = (static_cast<size_t>(outY) * dstW + outX) * 4;
            // DIBSection is BGRA: dst[0]=B, dst[1]=G, dst[2]=R, dst[3]=A
            dst[dIdx + 0] = static_cast<uint8_t>(p[2] * a + dst[dIdx + 0] * (1.0f - a));
            dst[dIdx + 1] = static_cast<uint8_t>(p[1] * a + dst[dIdx + 1] * (1.0f - a));
            dst[dIdx + 2] = static_cast<uint8_t>(p[0] * a + dst[dIdx + 2] * (1.0f - a));
            dst[dIdx + 3] = static_cast<uint8_t>(std::max<int>(dst[dIdx + 3], static_cast<int>(p[3])));
        }
    }
}

} // namespace

std::vector<uint8_t> PaintSkillsOverlay(
    const std::vector<MercenarySkillBadge>& badges,
    int width,
    int height,
    int hoverIndex)
{
    if (width <= 0 || height <= 0) {
        return {};
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    if (badges.empty()) {
        return rgba;
    }

    const int count = static_cast<int>(std::min(badges.size(), static_cast<size_t>(MaxBadges)));
    const int cell = BadgeCellSize;
    const int gap = BadgeGap;
    const int totalW = count * cell + (count - 1) * gap;
    const int pixelW = std::min(width, totalW);
    const int startX = std::max(0, (width - pixelW) / 2);
    const int startY = std::max(0, (height - cell) / 2);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;

    if (!dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return rgba;
    }

    auto oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, static_cast<size_t>(width) * height * 4);

    HFONT font = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    // D2R Palette
    HBRUSH bgCell = CreateSolidBrush(RGB(12, 10, 8));           // Dark plate under icon
    HBRUSH borderGold = CreateSolidBrush(RGB(205, 165, 60));    // Charged skill gold
    HBRUSH borderDim = CreateSolidBrush(RGB(110, 85, 45));      // Empty charges bronze
    HBRUSH borderBlue = CreateSolidBrush(RGB(70, 130, 210));    // Class skill sapphire
    HBRUSH borderAmber = CreateSolidBrush(RGB(220, 140, 40));   // Aura amber
    HBRUSH borderGlow = CreateSolidBrush(RGB(255, 235, 140));   // Hover highlight aura

    auto* dst = static_cast<uint8_t*>(bits);

    // Draw individual badge cells (clean native styling without heavy outer bounding box)
    for (int i = 0; i < count; ++i) {
        const auto& badge = badges[i];
        const int bx = startX + i * (cell + gap);
        const int by = startY;
        const bool isHovered = (i == hoverIndex);

        // Fill inner dark plate
        RECT rBox{ bx, by, bx + cell, by + cell };
        FillRect(dc, &rBox, bgCell);

        // Determine border style
        HBRUSH curBorder = borderGold;
        if (isHovered) {
            curBorder = borderGlow;
        } else if (badge.type == SkillBadgeType::ChargedSkill) {
            curBorder = (badge.currentCharges > 0) ? borderGold : borderDim;
        } else if (badge.type == SkillBadgeType::VirtualClassSkill || badge.type == SkillBadgeType::Oskill) {
            curBorder = borderBlue;
        } else if (badge.type == SkillBadgeType::Aura) {
            curBorder = borderAmber;
        }

        // Draw 1px bezel (2px if hovered)
        FrameRect(dc, &rBox, curBorder);
        if (isHovered) {
            RECT rInner{ bx + 1, by + 1, bx + cell - 1, by + cell - 1 };
            FrameRect(dc, &rInner, borderGlow);
        }

        // Render skill icon (centered, 1:1 aspect ratio)
        auto image = interaction::icons::Get("s" + std::to_string(badge.skillId));
        if (image && image->width > 0 && image->height > 0 && !image->rgba.empty()) {
            GdiFlush();
            const int iconTarget = BadgeIconSize; // 42px
            const int ix = bx + (cell - iconTarget) / 2;
            const int iy = by + (cell - iconTarget) / 2;
            DrawIcon(*image, dst, width, height, ix, iy, iconTarget, iconTarget);
        }

        // Text counter badge in bottom-right corner with drop shadow
        char text[16]{};
        COLORREF textColor = RGB(245, 235, 200);

        if (badge.type == SkillBadgeType::ChargedSkill) {
            std::snprintf(text, sizeof(text), "%u", badge.currentCharges);
            textColor = (badge.currentCharges > 0) ? RGB(245, 235, 200) : RGB(235, 65, 65);
        } else if (badge.type == SkillBadgeType::VirtualClassSkill || badge.type == SkillBadgeType::Oskill) {
            std::snprintf(text, sizeof(text), "+%u", badge.level);
            textColor = RGB(140, 210, 255);
        } else if (badge.type == SkillBadgeType::Aura) {
            std::snprintf(text, sizeof(text), "L%u", badge.level);
            textColor = RGB(255, 215, 120);
        }

        if (text[0] != '\0') {
            // Drop shadow
            SetTextColor(dc, RGB(0, 0, 0));
            RECT rShadow{ bx + 1, by + cell - 15, bx + cell, by + cell };
            DrawTextA(dc, text, -1, &rShadow, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

            // Foreground text
            SetTextColor(dc, textColor);
            RECT rText{ bx, by + cell - 16, bx + cell - 1, by + cell - 1 };
            DrawTextA(dc, text, -1, &rText, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    GdiFlush();

    // Convert DIBSection (BGRA) to output RGBA
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            const uint8_t b = dst[idx + 0];
            const uint8_t g = dst[idx + 1];
            const uint8_t r = dst[idx + 2];
            uint8_t a = dst[idx + 3];

            if (a == 0 && (r > 0 || g > 0 || b > 0)) {
                a = 240;
            }

            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = a;
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteObject(bgCell);
    DeleteObject(borderGold);
    DeleteObject(borderDim);
    DeleteObject(borderBlue);
    DeleteObject(borderAmber);
    DeleteObject(borderGlow);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    return rgba;
}

std::vector<uint8_t> PaintSkillTooltipCard(
    const MercenarySkillBadge& badge,
    int width,
    int height)
{
    if (width <= 0 || height <= 0) {
        return {};
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;

    if (!dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return rgba;
    }

    auto oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, static_cast<size_t>(width) * height * 4);

    // Compact native D2R skill inspection card layout
    const int cardW = std::min(width, TooltipWidth);
    const int cardH = std::min(height, TooltipHeight);
    const int startX = (width - cardW) / 2;
    const int startY = (height - cardH) / 2;

    HBRUSH bgDark = CreateSolidBrush(RGB(10, 8, 7));            // Obsidian background
    HBRUSH borderFrame = CreateSolidBrush(RGB(85, 70, 48));      // Subtle bronze/gold border
    HBRUSH borderInner = CreateSolidBrush(RGB(40, 32, 22));      // Inner shadow line

    // Card background & 1px frame
    RECT cardRect{ startX, startY, startX + cardW, startY + cardH };
    FillRect(dc, &cardRect, bgDark);
    FrameRect(dc, &cardRect, borderFrame);

    RECT cardInnerRect{ startX + 1, startY + 1, startX + cardW - 1, startY + cardH - 1 };
    FrameRect(dc, &cardInnerRect, borderInner);

    // 1. Skill Name in Diablo Gold (extracted directly from native D2R)
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Georgia");
    auto oldFont = SelectObject(dc, titleFont);
    SetBkMode(dc, TRANSPARENT);

    const char* skillTitle = (badge.skillName[0] != '\0') ? badge.skillName : "Habilidad";
    wchar_t wTitle[128]{};
    MultiByteToWideChar(CP_UTF8, 0, skillTitle, -1, wTitle, 128);

    // Title shadow + text
    SetTextColor(dc, RGB(0, 0, 0));
    RECT rTitleShadow{ startX + 1, startY + 11, startX + cardW + 1, startY + 33 };
    DrawTextW(dc, wTitle, -1, &rTitleShadow, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, RGB(229, 200, 138)); // Diablo Gold
    RECT rTitle{ startX, startY + 10, startX + cardW, startY + 32 };
    DrawTextW(dc, wTitle, -1, &rTitle, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    // 2. Skill Level & Charges (Silver / White in native D2R style)
    HFONT detailFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Georgia");
    SelectObject(dc, detailFont);

    char subText[128]{};
    if (badge.type == SkillBadgeType::ChargedSkill) {
        std::snprintf(subText, sizeof(subText), "Nivel %u  •  Cargas: %u / %u", badge.level, badge.currentCharges, badge.maxCharges);
    } else if (badge.type == SkillBadgeType::VirtualClassSkill || badge.type == SkillBadgeType::Oskill) {
        std::snprintf(subText, sizeof(subText), "Nivel de habilidad: +%u", badge.level);
    } else if (badge.type == SkillBadgeType::Aura) {
        std::snprintf(subText, sizeof(subText), "Aura activa: Nivel %u", badge.level);
    }

    wchar_t wSub[128]{};
    MultiByteToWideChar(CP_UTF8, 0, subText, -1, wSub, 128);

    SetTextColor(dc, RGB(0, 0, 0));
    RECT rSubShadow{ startX + 1, startY + 39, startX + cardW + 1, startY + 59 };
    DrawTextW(dc, wSub, -1, &rSubShadow, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, RGB(220, 220, 220)); // Native silver
    RECT rSub{ startX, startY + 38, startX + cardW, startY + 58 };
    DrawTextW(dc, wSub, -1, &rSub, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    // 3. Equipment source slot
    char srcText[128]{};
    std::snprintf(srcText, sizeof(srcText), "Conferido por: %s", GetBodyLocationName(badge.bodyLocation));
    wchar_t wSrc[128]{};
    MultiByteToWideChar(CP_UTF8, 0, srcText, -1, wSrc, 128);

    SetTextColor(dc, RGB(0, 0, 0));
    RECT rSrcShadow{ startX + 1, startY + 65, startX + cardW + 1, startY + 85 };
    DrawTextW(dc, wSrc, -1, &rSrcShadow, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, RGB(165, 145, 115)); // Subdued Bronze/Gold
    RECT rSrc{ startX, startY + 64, startX + cardW, startY + 84 };
    DrawTextW(dc, wSrc, -1, &rSrc, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    GdiFlush();

    // Convert DIBSection (BGRA) to output RGBA
    auto* dst = static_cast<uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            const uint8_t b = dst[idx + 0];
            const uint8_t g = dst[idx + 1];
            const uint8_t r = dst[idx + 2];
            uint8_t a = dst[idx + 3];

            if (a == 0 && (r > 0 || g > 0 || b > 0)) {
                a = 245; // High opacity for clean reading in VR
            }

            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = a;
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(detailFont);
    DeleteObject(bgDark);
    DeleteObject(borderFrame);
    DeleteObject(borderInner);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    return rgba;
}

} // namespace framevr::companionui
