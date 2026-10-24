#pragma once
#include "vr_settings_help.h"
#include "vr_settings_layout.h"
#include "body_calibration.h"
#include <string>
#include <vector>
namespace framevr::settings {
// VR Settings pages: Direct2D + DirectWrite through draw::Painter.
// One finished page: Width x Height straight RGBA (the help card is inside
// the panel), the layout it was drawn from (hit test
// and help on the frame thread use this one, so a press always lands on what
// the picture shows), and the time spent building and drawing.
struct PagePicture {
    std::vector<uint8_t> pixels;
    layout::Layout layout;
    double buildMs=0,drawMs=0;
    std::string error;   // the painter's, when pixels is empty
};
// Builds the layout with this thread's painter measuring the text, then draws
// every element. Call it on the settings worker: the painter is created on
// the first call and cached per thread (COM and DirectWrite stay on it).
// input.notes left empty is filled from release_notes.h.
// tables: the menu's rows (default: the real merged menu; previews and tests may pass another).
PagePicture PaintPage(const Model& model,const layout::LayoutInput& input,const layout::Tables* tables=nullptr);
// The layout PaintPage would draw, without drawing it (the checks).
layout::Layout MeasuredLayout(const Model& model,const layout::LayoutInput& input,const layout::Tables* tables=nullptr);
// This thread's painter as a layout text measure (what PaintPage measures with).
layout::TextMeasure ThisThreadMeasure();
// Every text of a built layout measured with this thread's painter as Draw
// draws it: what would not fit its element (a one-line text wider than its
// box, wrapped help taller than its box at the 22 px floor). Empty: all fits.
std::vector<std::string> FitProblems(const layout::Layout& layout);
// LayoutInput::calibration from the calibration flow (active while it runs).
layout::CalibrationView CalibrationOf(const calibration::Flow& body);
// LayoutInput::notes from release_notes.h.
layout::NotesView ReleaseNotesView();

// The preview set (vr-settings-preview --pages <folder>): every View and the
// states worth checking, for a few classes, each painted twice (cold and
// warm). name: the file name without .png. The fallback shots use DemoTables.
struct PreviewShot {std::string name;PagePicture picture;double warmMs=0;};
std::vector<PreviewShot> PreviewShots();
// What's new's pages for this build's release lines (Model::SetNotesPages).
int ReleaseNotesPages();
}
