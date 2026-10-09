
// Screenshot-only fork: the settings window.
//
//   The layout follows the Figma mockup "SKIV — settings": a title bar with a
//   close button that sends SKIV to the tray, a section list on the left and
//   the active section on the right. Every section fits into 800 x 560; a
//   shorter window scrolls the right-hand side only.

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/i18n.h>
#include <SKIV.h>
#include "../../version.h"

#include <filesystem>
#include <functional>
#include <unordered_map>
#include <ShlObj.h>
#include <Shlwapi.h>

extern bool  allowShortcutCtrlA;
extern bool  bKeepWindowAlive;
extern bool  RecreateSwapChains;
extern HWND  SKIF_Notify_hWnd;
extern float SKIF_ImGui_GlobalDPIScale;

#pragma region Palette

struct skiv_palette_s
{
  ImU32 canvas, card, cardHi, inset;
  ImU32 line, lineStrong, lineCanvas;
  ImU32 ink, ink2, ink3, ink4;
  ImU32 accent, accentSoft, accentLine, onAccent;
  ImU32 hdr, hdrSoft, sdrBadge;
  ImU32 close;
  ImU32 key, keyOff, keyEdge, keyShadow;
  ImU32 scroll;
};

static ImU32
_RGB (unsigned int rgb, float alpha = 1.0f)
{
  return IM_COL32 ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast <int> (alpha * 255.0f + 0.5f));
}

static const skiv_palette_s&
SKIV_Palette (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // SKIV blends colors in linear light (scRGB), so a 7 % white line would come
  //   out far brighter than in the mockup. Translucent tokens are therefore
  //     blended here, in sRGB, over the surface they sit on, and drawn opaque.
  auto _Over = [](unsigned int fg, float alpha, unsigned int bg) -> ImU32
  {
    auto _Mix = [&](int shift) {
      float f = static_cast <float> ((fg >> shift) & 0xFF);
      float b = static_cast <float> ((bg >> shift) & 0xFF);
      return static_cast <int> (b + (f - b) * alpha + 0.5f);
    };

    return IM_COL32 (_Mix (16), _Mix (8), _Mix (0), 255);
  };

  // Dark: the dimmed desktop of the selection overlay. The single accent is
  //   the blue of the selection frame, amber is reserved for HDR.
  constexpr unsigned int d_canvas = 0x16171a, d_card = 0x1d1f23, d_inset = 0x121316, d_key = 0x2a2d33;

  static const skiv_palette_s dark = {
    _RGB (d_canvas), _RGB (d_card), _RGB (0x23252a), _RGB (d_inset),
    _Over (0xffffff, 0.07f, d_card), _Over (0xffffff, 0.12f, d_card), _Over (0xffffff, 0.07f, d_canvas),
    _RGB (0xeceef1), _RGB (0xa8acb4), _RGB (0x7b8089), _RGB (0x555960),
    _RGB (0x5ab0ff), _Over (0x5ab0ff, 0.14f, d_card), _Over (0x5ab0ff, 0.45f, d_card), _RGB (0x0e1a26),
    _RGB (0xf2b84b), _Over (0xf2b84b, 0.14f, d_inset), _Over (0xffffff, 0.06f, d_inset),
    _RGB (0xc42b1c),
    _RGB (d_key), _RGB (0x222429), _Over (0xffffff, 0.06f, d_card), _Over (0x000000, 0.35f, d_key),
    _Over (0xffffff, 0.18f, d_canvas)
  };

  constexpr unsigned int l_canvas = 0xf3f4f6, l_card = 0xffffff, l_inset = 0xf6f7f9, l_key = 0xffffff;

  static const skiv_palette_s light = {
    _RGB (l_canvas), _RGB (l_card), _RGB (0xeceef1), _RGB (l_inset),
    _Over (0x000000, 0.08f, l_card), _Over (0x000000, 0.14f, l_card), _Over (0x000000, 0.08f, l_canvas),
    _RGB (0x17181b), _RGB (0x4a4f57), _RGB (0x6b7079), _RGB (0xa3a7ae),
    _RGB (0x1a7fe0), _Over (0x1a7fe0, 0.12f, l_card), _Over (0x1a7fe0, 0.45f, l_card), _RGB (0xffffff),
    _RGB (0xb7791f), _Over (0xb7791f, 0.14f, l_inset), _Over (0x000000, 0.06f, l_inset),
    _RGB (0xc42b1c),
    _RGB (l_key), _RGB (0xf3f4f6), _Over (0x000000, 0.12f, l_card), _Over (0x000000, 0.12f, l_key),
    _Over (0x000000, 0.25f, l_canvas)
  };

  return (_registry._StyleLightMode) ? light : dark;
}

#pragma endregion

#pragma region Drawing helpers

static float              s  = 1.0f;     // DPI scale of the current frame
static ImDrawList*        dl = nullptr;  // Draw list of the window being drawn
static const skiv_palette_s* P = nullptr;

static ImFont* F11  (void) { return SKIV_Font (11.0f, false); }
static ImFont* F12  (void) { return SKIV_Font (12.0f, false); }
static ImFont* F13  (void) { return SKIV_Font (13.0f, false); }
static ImFont* F14  (void) { return SKIV_Font (14.0f, false); }
static ImFont* SB11 (void) { return SKIV_Font (11.0f, true);  }
static ImFont* SB12 (void) { return SKIV_Font (12.0f, true);  }
static ImFont* SB13 (void) { return SKIV_Font (13.0f, true);  }
static ImFont* SB14 (void) { return SKIV_Font (14.0f, true);  }
static ImFont* SB15 (void) { return SKIV_Font (15.0f, true);  }
static ImFont* SB20 (void) { return SKIV_Font (20.0f, true);  }

static ImVec2
TextSize (ImFont* font, const char* text)
{
  return font->CalcTextSizeA (font->FontSize, FLT_MAX, 0.0f, text);
}

// Line height for wrapped text in the given font
static float
LH (ImFont* font)
{
  return floorf (font->FontSize * 1.4f);
}

static void
Text (ImFont* font, ImVec2 pos, ImU32 col, const char* text)
{
  dl->AddText (font, font->FontSize, ImVec2 (floorf (pos.x), floorf (pos.y)), col, text);
}

// Draws text vertically centered on a line of the given height
static void
TextCentered (ImFont* font, ImVec2 pos, float height, ImU32 col, const char* text)
{
  Text (font, ImVec2 (pos.x, pos.y + (height - font->FontSize) * 0.5f), col, text);
}

struct rich_s
{
  ImFont*     font;
  ImU32       col;
  std::string text;
};

// Word-wraps runs of differently styled text; returns the height used.
//   With draw = false it only measures.
static float
RichText (ImVec2 pos, float width, float line_height, const std::vector <rich_s>& runs, bool draw = true)
{
  float x     = 0.0f;
  int   lines = 1;

  for (auto& run : runs)
  {
    size_t i = 0;

    while (i < run.text.size ())
    {
      // A word includes its trailing spaces
      size_t j = run.text.find (' ', i);
      j = (j == std::string::npos) ? run.text.size () : j;
      while (j < run.text.size () && run.text [j] == ' ')
        j++;

      std::string word  = run.text.substr (i, j - i);
      std::string bare  = word;
      while (! bare.empty () && bare.back () == ' ')
        bare.pop_back ();

      float w_bare = TextSize (run.font, bare.c_str ()).x;
      float w_word = TextSize (run.font, word.c_str ()).x;

      if (x > 0.0f && x + w_bare > width)
      {
        x = 0.0f;
        lines++;
      }

      if (draw)
        TextCentered (run.font, ImVec2 (pos.x + x, pos.y + (lines - 1) * line_height), line_height, run.col, bare.c_str ());

      x += w_word;
      i  = j;
    }
  }

  return lines * line_height;
}

static float
WrappedText (ImFont* font, ImU32 col, ImVec2 pos, float width, float line_height, const char* text, bool draw = true)
{
  return RichText (pos, width, line_height, { { font, col, text } }, draw);
}

enum skiv_icon_e
{
  Icon_Viewfinder,
  Icon_Folder,
  Icon_Sun,
  Icon_App,
  Icon_Sliders,
  Icon_Window,
  Icon_Screen,
  Icon_Power,
  Icon_External,
  Icon_Close,
  Icon_Chevron,
  Icon_Check
};

// Line icons drawn on a 24-unit grid with a 2-unit stroke, as in the mockup
static void
Icon (skiv_icon_e icon, ImVec2 pos, float size, ImU32 col)
{
  float k = size / 24.0f;
  float t = 2.0f * k;

  auto _P = [&](float x, float y) { return ImVec2 (pos.x + x * k, pos.y + y * k); };

  auto _Line = [&](std::initializer_list <ImVec2> pts, bool closed = false)
  {
    ImVec2 buf [8];
    int    n = 0;

    for (auto& p : pts)
      buf [n++] = _P (p.x, p.y);

    dl->AddPolyline (buf, n, col, (closed) ? ImDrawFlags_Closed : ImDrawFlags_None, t);
  };

  switch (icon)
  {
  case Icon_Viewfinder:
    _Line ({ { 4,  9}, { 4,  4}, { 9,  4} });
    _Line ({ {15,  4}, {20,  4}, {20,  9} });
    _Line ({ {20, 15}, {20, 20}, {15, 20} });
    _Line ({ { 9, 20}, { 4, 20}, { 4, 15} });
    break;
  case Icon_Folder:
    _Line ({ { 3,  6}, {10,  6}, {12,  8}, {21,  8}, {21, 19}, { 3, 19} }, true);
    break;
  case Icon_Sun:
    dl->AddCircle (_P (12, 12), 4.0f * k, col, 0, t);
    _Line ({ {12,  3}, {12,  5} });
    _Line ({ {12, 19}, {12, 21} });
    _Line ({ { 3, 12}, { 5, 12} });
    _Line ({ {19, 12}, {21, 12} });
    _Line ({ {5.6f,  5.6f}, { 7.0f,  7.0f} });
    _Line ({ {17.0f, 17.0f}, {18.4f, 18.4f} });
    _Line ({ {5.6f, 18.4f}, { 7.0f, 17.0f} });
    _Line ({ {17.0f,  7.0f}, {18.4f,  5.6f} });
    break;
  case Icon_App:
    _Line ({ { 3,  4}, {21,  4}, {21, 17}, { 3, 17} }, true);
    _Line ({ { 3, 13}, {21, 13} });
    break;
  case Icon_Sliders:
    _Line ({ { 4,  7}, {14,  7} });
    _Line ({ {18,  7}, {20,  7} });
    _Line ({ { 4, 17}, { 8, 17} });
    _Line ({ {12, 17}, {20, 17} });
    _Line ({ {14,  5}, {18,  5}, {18,  9}, {14,  9} }, true);
    _Line ({ { 8, 15}, {12, 15}, {12, 19}, { 8, 19} }, true);
    break;
  case Icon_Window:
    _Line ({ { 3,  5}, {21,  5}, {21, 19}, { 3, 19} }, true);
    _Line ({ { 3,  9}, {21,  9} });
    break;
  case Icon_Screen:
    _Line ({ { 3,  4}, {21,  4}, {21, 17}, { 3, 17} }, true);
    _Line ({ { 8, 21}, {16, 21} });
    _Line ({ {12, 17}, {12, 21} });
    break;
  case Icon_Power:
    _Line ({ {12,  3}, {12, 12} });
    dl->PathArcTo (_P (12.0f, 12.61f), 8.0f * k, IM_PI * -44.5f / 180.0f, IM_PI * 224.5f / 180.0f);
    dl->PathStroke (col, ImDrawFlags_None, t);
    break;
  case Icon_External:
    _Line ({ {14,  4}, {20,  4}, {20, 10} });
    _Line ({ {20,  4}, {11, 13} });
    _Line ({ {18, 14}, {18, 20}, { 4, 20}, { 4,  6}, {10,  6} });
    break;
  case Icon_Close:
  {
    // 14-unit grid, 1.3-unit stroke
    float c = size / 14.0f;
    dl->AddLine (ImVec2 (pos.x +  2 * c, pos.y +  2 * c), ImVec2 (pos.x + 12 * c, pos.y + 12 * c), col, 1.3f * c);
    dl->AddLine (ImVec2 (pos.x + 12 * c, pos.y +  2 * c), ImVec2 (pos.x +  2 * c, pos.y + 12 * c), col, 1.3f * c);
  } break;
  case Icon_Chevron:
    t = 2.4f * k;
    _Line ({ { 6,  9}, {12, 15}, {18,  9} });
    break;
  case Icon_Check:
  {
    // 12-unit grid, 1.8-unit stroke
    float  c = size / 12.0f;
    ImVec2 pts [3] = {
      ImVec2 (pos.x + 2.5f * c, pos.y + 6.2f * c),
      ImVec2 (pos.x + 4.8f * c, pos.y + 8.5f * c),
      ImVec2 (pos.x + 9.5f * c, pos.y + 3.5f * c)
    };
    dl->AddPolyline (pts, 3, col, ImDrawFlags_None, 1.8f * c);
  } break;
  }
}

// An invisible button; the visuals are drawn by the caller
static bool
Hit (const char* id, ImVec2 pos, ImVec2 size, bool* hovered = nullptr, bool enabled = true)
{
  ImGui::SetCursorScreenPos (pos);

  if (! enabled)
  {
    ImGui::Dummy (size);

    if (hovered != nullptr)
       *hovered  = false;

    return false;
  }

  bool pressed = ImGui::InvisibleButton (id, size);
  bool hover   = ImGui::IsItemHovered ();

  if (hover)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);

  if (hovered != nullptr)
     *hovered  = hover;

  return pressed;
}

static ImVec2 V (float x, float y) { return ImVec2 (x * s, y * s); }

#pragma endregion

#pragma region Controls

static constexpr ImVec2 ToggleSize = ImVec2 (40.0f, 22.0f);

static bool
Toggle (const char* id, ImVec2 pos, bool* value, bool enabled = true)
{
  ImVec2 size = V (ToggleSize.x, ToggleSize.y);
  bool   hover = false;
  bool   pressed = Hit (id, pos, size, &hover, enabled);

  if (pressed)
    *value = ! *value;

  ImVec2 max = ImVec2 (pos.x + size.x, pos.y + size.y);

  if (*value)
  {
    dl->AddRectFilled (pos, max, P->accent, size.y * 0.5f);
    dl->AddCircleFilled (ImVec2 (pos.x + 29.0f * s, pos.y + 11.0f * s), 8.0f * s, P->onAccent);
  }

  else
  {
    dl->AddRect (pos, max, (hover) ? P->ink2 : P->ink3, size.y * 0.5f, 0, 1.0f * s);
    dl->AddCircleFilled (ImVec2 (pos.x + 11.0f * s, pos.y + 11.0f * s), 7.0f * s, (hover) ? P->ink2 : P->ink3);
  }

  return pressed;
}

static float
ButtonWidth (const char* label, bool icon = false, bool ghost = false)
{
  return TextSize (F13 (), label).x + ((ghost) ? 20.0f : 28.0f) * s + ((icon) ? 20.0f * s : 0.0f);
}

static bool
Button (const char* id, ImVec2 pos, const char* label, bool ghost = false, bool external = false)
{
  ImVec2 size  = ImVec2 (ButtonWidth (label, external, ghost), 34.0f * s);
  bool   hover = false;
  bool   pressed = Hit (id, pos, size, &hover);

  ImVec2 max = ImVec2 (pos.x + size.x, pos.y + size.y);

  if (! ghost)
  {
    dl->AddRectFilled (pos, max, (hover) ? P->lineStrong : P->cardHi, 7.0f * s);
    dl->AddRect       (pos, max, P->lineStrong, 7.0f * s, 0, 1.0f * s);
  }

  else if (hover)
    dl->AddRectFilled (pos, max, P->cardHi, 7.0f * s);

  float  x   = pos.x + ((ghost) ? 10.0f : 14.0f) * s;
  ImU32  col = (ghost && ! hover) ? P->ink2 : P->ink;

  if (external)
  {
    Icon (Icon_External, ImVec2 (x, pos.y + 10.0f * s), 14.0f * s, col);
    x += 20.0f * s;
  }

  TextCentered (F13 (), ImVec2 (x, pos.y), size.y, col, label);

  return pressed;
}

struct seg_opt_s
{
  const char* label;
  int         value;
};

static float
SegmentedWidth (const std::vector <seg_opt_s>& opts)
{
  float w = 6.0f * s;

  for (auto& opt : opts)
    w += TextSize (F13 (), opt.label).x + 28.0f * s;

  return w;
}

static bool
Segmented (const char* id, ImVec2 pos, const std::vector <seg_opt_s>& opts, int* value)
{
  ImVec2 size = ImVec2 (SegmentedWidth (opts), 36.0f * s);
  ImVec2 max  = ImVec2 (pos.x + size.x, pos.y + size.y);

  dl->AddRectFilled (pos, max, P->inset, 8.0f * s);
  dl->AddRect       (pos, max, P->line,  8.0f * s, 0, 1.0f * s);

  bool  changed = false;
  float x       = pos.x + 3.0f * s;

  ImGui::PushID (id);

  for (auto& opt : opts)
  {
    float  w     = TextSize (F13 (), opt.label).x + 28.0f * s;
    ImVec2 o_pos = ImVec2 (x, pos.y + 3.0f * s);
    ImVec2 o_max = ImVec2 (x + w, o_pos.y + 30.0f * s);
    bool   on    = (*value == opt.value);
    bool   hover = false;

    if (Hit (opt.label, o_pos, ImVec2 (w, 30.0f * s), &hover) && ! on)
    {
      *value  = opt.value;
      changed = true;
      on      = true;
    }

    if (on)
    {
      dl->AddRectFilled (o_pos, o_max, P->cardHi,     6.0f * s);
      dl->AddRect       (o_pos, o_max, P->lineStrong, 6.0f * s, 0, 1.0f * s);
    }

    TextCentered (F13 (), ImVec2 (x + 14.0f * s, o_pos.y), 30.0f * s, (on || hover) ? P->ink : P->ink2, opt.label);

    x += w;
  }

  ImGui::PopID ();

  return changed;
}

// Wide enough for the longest item, the padding and the chevron
static float
DropdownWidth (const std::vector <seg_opt_s>& items, float min_width)
{
  float w = min_width * s;

  for (auto& item : items)
    w = std::max (w, TextSize (F13 (), item.label).x + 46.0f * s);

  return w;
}

// A drop-down list; items are { label, value }
static bool
Dropdown (const char* id, ImVec2 pos, float width, const char* preview, const char* preview_dim, const std::vector <seg_opt_s>& items, int* value)
{
  ImVec2 size  = ImVec2 (width, 32.0f * s);
  ImVec2 max   = ImVec2 (pos.x + size.x, pos.y + size.y);
  bool   hover = false;

  ImGui::PushID (id);

  if (Hit ("##frame", pos, size, &hover))
    ImGui::OpenPopup ("##list");

  dl->AddRectFilled (pos, max, P->inset, 7.0f * s);
  dl->AddRect       (pos, max, (hover) ? P->lineStrong : P->line, 7.0f * s, 0, 1.0f * s);

  TextCentered (F13 (), ImVec2 (pos.x + 12.0f * s, pos.y), size.y, P->ink, preview);

  if (preview_dim != nullptr)
    TextCentered (F13 (), ImVec2 (pos.x + 12.0f * s + TextSize (F13 (), preview).x, pos.y), size.y, P->ink3, preview_dim);

  Icon (Icon_Chevron, ImVec2 (max.x - 22.0f * s, pos.y + 10.0f * s), 12.0f * s, P->ink3);

  bool changed = false;

  ImGui::SetNextWindowPos (ImVec2 (pos.x, max.y + 4.0f * s));
  ImGui::SetNextWindowSizeConstraints (ImVec2 (width, 0.0f), ImVec2 (FLT_MAX, FLT_MAX));

  ImGui::PushStyleVar   (ImGuiStyleVar_PopupRounding,    8.0f * s);
  ImGui::PushStyleVar   (ImGuiStyleVar_PopupBorderSize,  1.0f * s);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,    V (4.0f, 4.0f));
  ImGui::PushStyleVar   (ImGuiStyleVar_ItemSpacing,      V (0.0f, 2.0f));
  ImGui::PushStyleVar   (ImGuiStyleVar_SelectableTextAlign, ImVec2 (0.0f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_PopupBg,       ImGui::ColorConvertU32ToFloat4 (P->cardHi));
  ImGui::PushStyleColor (ImGuiCol_Border,        ImGui::ColorConvertU32ToFloat4 (P->lineStrong));
  ImGui::PushStyleColor (ImGuiCol_Header,        ImGui::ColorConvertU32ToFloat4 (P->accentSoft));
  ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4 (P->line));
  ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImGui::ColorConvertU32ToFloat4 (P->accentSoft));
  ImGui::PushStyleColor (ImGuiCol_Text,          ImGui::ColorConvertU32ToFloat4 (P->ink));
  ImGui::PushStyleColor (ImGuiCol_NavHighlight,  ImVec4 (0, 0, 0, 0));
  ImGui::PushFont       (F13 ());

  if (ImGui::BeginPopup ("##list", ImGuiWindowFlags_NoMove))
  {
    for (auto& item : items)
    {
      bool selected = (*value == item.value);

      ImGui::SetCursorPosX (ImGui::GetCursorPosX () + 2.0f * s);

      if (ImGui::Selectable (item.label, selected, ImGuiSelectableFlags_None, ImVec2 (width - 12.0f * s, 30.0f * s)))
      {
        if (! selected)
          changed = true;

        *value = item.value;
      }
    }

    ImGui::EndPopup ();
  }

  ImGui::PopFont        ();
  ImGui::PopStyleColor  (7);
  ImGui::PopStyleVar    (5);
  ImGui::PopID          ();

  return changed;
}

#pragma endregion

#pragma region Shortcuts

struct kb_kv_s
{
  SK_KeybindMultiState*                           _key;
  SKIF_RegistrySettings::KeyValue <std::wstring>* _reg;
  std::function <void (SK_KeybindMultiState*)>    _callback;
  std::function <bool (void)>                     _active;  // Is the shortcut registered at all?
};

static kb_kv_s* recording     = nullptr;
static bool     recordingDone = false; // Swallow the key that finished the recording

static void
Shortcut_Start (kb_kv_s* kv)
{
  recording = kv;

  kv->_key->pending   = kv->_key->saved;
  kv->_key->assigning = true;

  // Unregister the shortcut while recording, so pressing it does not trigger a capture
  if (kv->_key->hasNewState () && kv->_active ())
    kv->_callback (kv->_key);
}

static void
Shortcut_Finish (bool cancel, bool clear, int vKey)
{
  if (recording == nullptr)
    return;

  auto& io  = ImGui::GetIO ();
  auto* key = recording->_key;

  if (cancel)
    key->pending = key->saved;

  else
  {
    key->pending.vKey  = (clear) ? 0     : static_cast <SHORT> (vKey);
    key->pending.ctrl  = (clear) ? false : io.KeyCtrl;
    key->pending.shift = (clear) ? false : io.KeyShift;
    key->pending.alt   = (clear) ? false : io.KeyAlt;
    key->pending.super = (clear) ? false : io.KeySuper;
    key->pending.makeMask ( );
    key->pending.update   ( );
  }

  key->assigning = false;

  if (key->applyChanges ( ))
    recording->_reg->putData (key->saved.human_readable);

  if (key->hasNewState ( ) && recording->_active ( ))
    recording->_callback (key);

  recording     = nullptr;
  recordingDone = true;
}

// Captures the next key press while a shortcut is being recorded
static void
Shortcut_Process (void)
{
  if (recording == nullptr)
    return;

  // Keeps Esc from closing the window while recording
  g_activeKeybindPopup = true;

  auto& io = ImGui::GetIO ();

  bool modifier = (io.KeyCtrl || io.KeyShift || io.KeyAlt || io.KeySuper);

  if (! modifier && ImGui::IsKeyPressed (ImGuiKey_Escape, false))
    return Shortcut_Finish (true, false, 0);

  if (! modifier && ImGui::IsKeyPressed (ImGuiKey_Backspace, false))
    return Shortcut_Finish (false, true, 0);

  for (ImGuiKey imKey = ImGuiKey_NamedKey_BEGIN; imKey < ImGuiKey_NamedKey_END; imKey = (ImGuiKey)(imKey + 1))
  {
    if ( imKey == ImGuiKey_LeftCtrl  || imKey == ImGuiKey_RightCtrl  ||
         imKey == ImGuiKey_LeftShift || imKey == ImGuiKey_RightShift ||
         imKey == ImGuiKey_LeftAlt   || imKey == ImGuiKey_RightAlt   ||
         imKey == ImGuiKey_LeftSuper || imKey == ImGuiKey_RightSuper ||
         imKey == ImGuiKey_MouseLeft || imKey == ImGuiKey_MouseRight ||
         imKey == ImGuiKey_MouseMiddle )
      continue;

    if (ImGui::IsKeyPressed (imKey, false))
    {
      extern int ImGui_ImplWin32_ImGuiKeyToVirtualKey (ImGuiKey);

      if (int vKey = ImGui_ImplWin32_ImGuiKeyToVirtualKey (imKey))
        return Shortcut_Finish (false, false, vKey);
    }
  }
}

static std::vector <std::string>
Shortcut_Caps (const SK_Keybind& kb)
{
  std::vector <std::string> caps;

  if (kb.vKey == 0)
    return caps;

  if (kb.ctrl)  caps.push_back ("Ctrl");
  if (kb.super) caps.push_back ("Win");
  if (kb.alt)   caps.push_back ("Alt");
  if (kb.shift) caps.push_back ("Shift");

  // The key itself is the last part of the human-readable name
  std::string name = kb.human_readable_utf8;
  size_t      plus = name.rfind ('+');

  if (plus != std::string::npos && plus + 1 < name.size ())
    name = name.substr (plus + 1);

  caps.push_back (name);

  return caps;
}

static float
Caps_Width (const std::vector <std::string>& caps, ImFont* font, float pad)
{
  float w = 0.0f;

  for (auto& cap : caps)
    w += std::max (TextSize (font, cap.c_str ()).x + pad * 2.0f, 28.0f * s) + 4.0f * s;

  return (caps.empty ()) ? 0.0f : w - 4.0f * s;
}

static void
Caps_Draw (const std::vector <std::string>& caps, ImVec2 pos, float height, ImFont* font, float pad, bool dim, ImU32 edge)
{
  float x = pos.x;

  for (auto& cap : caps)
  {
    float  w   = std::max (TextSize (font, cap.c_str ()).x + pad * 2.0f, 28.0f * s);
    ImVec2 min = ImVec2 (x, pos.y);
    ImVec2 max = ImVec2 (x + w, pos.y + height);

    dl->AddRectFilled (min, max, (dim) ? P->keyOff : P->key, 6.0f * s);
    dl->AddRectFilled (ImVec2 (min.x, max.y - 2.0f * s), max, P->keyShadow, 6.0f * s, ImDrawFlags_RoundCornersBottom);
    dl->AddRect       (min, max, edge, 6.0f * s, 0, 1.0f * s);

    ImVec2 ts = TextSize (font, cap.c_str ());
    Text (font, ImVec2 (x + (w - ts.x) * 0.5f, pos.y + (height - 2.0f * s - font->FontSize) * 0.5f), (dim) ? P->ink4 : P->ink, cap.c_str ());

    x += w + 4.0f * s;
  }
}

// The width a shortcut control will take
static float
Shortcut_Width (kb_kv_s* kv, bool compact)
{
  ImFont* font = (compact) ? SB11 () : SB12 ();
  float   pad  = ((compact) ? 7.0f : 8.0f) * s;

  if (recording == kv)
  {
    auto& io = ImGui::GetIO ();
    std::vector <std::string> held;
    if (io.KeyCtrl)  held.push_back ("Ctrl");
    if (io.KeySuper) held.push_back ("Win");
    if (io.KeyAlt)   held.push_back ("Alt");
    if (io.KeyShift) held.push_back ("Shift");

    float w = Caps_Width (held, font, pad) + TextSize (F12 (), (held.empty ()) ? "…" : "+ …").x + 24.0f * s;
    return w + ((held.empty ()) ? 0.0f : 6.0f * s);
  }

  auto caps = Shortcut_Caps (kv->_key->saved);

  if (caps.empty ())
    return TextSize (F12 (), TR ("Not set", "Не задано")).x + 24.0f * s;

  return Caps_Width (caps, font, pad) + 8.0f * s;
}

// A shortcut drawn as keycaps; click to record a new one
static void
Shortcut (const char* id, kb_kv_s* kv, ImVec2 pos, bool compact, bool enabled)
{
  ImFont* font   = (compact) ? SB11 () : SB12 ();
  float   pad    = ((compact) ? 7.0f  : 8.0f)  * s;
  float   cap_h  = ((compact) ? 24.0f : 28.0f) * s;
  float   width  = Shortcut_Width (kv, compact);
  float   height = cap_h + 8.0f * s;
  bool    hover  = false;

  ImVec2 max = ImVec2 (pos.x + width, pos.y + height);

  if (Hit (id, pos, ImVec2 (width, height), &hover, enabled && recording != kv))
  {
    if (recording != nullptr)
      Shortcut_Finish (true, false, 0);

    Shortcut_Start (kv);
  }

  if (recording == kv)
  {
    // Clicking anywhere else cancels the recording
    if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) && ! ImGui::IsMouseHoveringRect (pos, max))
    {
      Shortcut_Finish (true, false, 0);
      return;
    }

    auto& io = ImGui::GetIO ();
    std::vector <std::string> held;
    if (io.KeyCtrl)  held.push_back ("Ctrl");
    if (io.KeySuper) held.push_back ("Win");
    if (io.KeyAlt)   held.push_back ("Alt");
    if (io.KeyShift) held.push_back ("Shift");

    dl->AddRectFilled (pos, max, P->accentSoft, 8.0f * s);
    dl->AddRect       (pos, max, P->accent,     8.0f * s, 0, 1.0f * s);

    Caps_Draw (held, ImVec2 (pos.x + 12.0f * s, pos.y + 4.0f * s), cap_h, font, pad, false, P->accentLine);

    float x = pos.x + 12.0f * s + Caps_Width (held, font, pad) + ((held.empty ()) ? 0.0f : 6.0f * s);
    TextCentered (F12 (), ImVec2 (x, pos.y), height, P->accent, (held.empty ()) ? "…" : "+ …");
    return;
  }

  if (hover)
    dl->AddRectFilled (pos, max, P->cardHi, 8.0f * s);

  auto caps = Shortcut_Caps (kv->_key->saved);

  if (caps.empty ())
    TextCentered (F12 (), ImVec2 (pos.x + 12.0f * s, pos.y), height, (enabled) ? P->ink3 : P->ink4, TR ("Not set", "Не задано"));
  else
    Caps_Draw (caps, ImVec2 (pos.x + 4.0f * s, pos.y + 4.0f * s), cap_h, font, pad, ! enabled, P->keyEdge);
}

#pragma endregion

#pragma region Cards and rows

// Content of a card is drawn on channel 1, the card itself on channel 0 once
//   its height is known
struct card_s
{
  ImVec2 pos;
  float  width;
  float  y;      // Bottom of the last row
  int    rows;
};

static card_s
Card_Begin (ImVec2 pos, float width)
{
  dl->ChannelsSplit      (2);
  dl->ChannelsSetCurrent (1);

  return { pos, width, pos.y, 0 };
}

static float
Card_End (card_s& card)
{
  dl->ChannelsSetCurrent (0);

  ImVec2 max = ImVec2 (card.pos.x + card.width, card.y);

  dl->AddRectFilled (card.pos, max, P->card, 10.0f * s);
  dl->AddRect       (card.pos, max, P->line, 10.0f * s, 0, 1.0f * s);

  dl->ChannelsMerge ();

  return card.y - card.pos.y;
}

// Starts a new row: draws the divider and returns its top
static float
Card_Row (card_s& card)
{
  if (card.rows++ > 0)
    dl->AddLine (ImVec2 (card.pos.x, card.y), ImVec2 (card.pos.x + card.width, card.y), P->line, 1.0f * s);

  return card.y;
}

struct row_text_s
{
  const char* title;
  const char* desc;
  ImFont*     title_font;
  ImU32       title_col;
  ImU32       desc_col;
};

static float
RowText_Height (const row_text_s& t, float width)
{
  float h = t.title_font->FontSize * 1.3f;

  if (t.desc != nullptr && *t.desc != '\0')
    h += 3.0f * s + WrappedText (F12 (), 0, ImVec2 (), width, LH (F12 ()), t.desc, false);

  return h;
}

static void
RowText_Draw (const row_text_s& t, ImVec2 pos, float width)
{
  float title_h = t.title_font->FontSize * 1.3f;

  TextCentered (t.title_font, pos, title_h, t.title_col, t.title);

  if (t.desc != nullptr && *t.desc != '\0')
    WrappedText (F12 (), t.desc_col, ImVec2 (pos.x, pos.y + title_h + 3.0f * s), width, LH (F12 ()), t.desc);
}

// A row with text on the left and controls of the given size on the right;
//   returns where the controls go
static ImVec2
Row (card_s& card, const row_text_s& text, ImVec2 controls, float min_height = 58.0f, float lead = 0.0f)
{
  float top     = Card_Row (card);
  float pad_x   = 16.0f * s;
  float gap     = 14.0f * s;
  float text_x  = card.pos.x + pad_x + ((lead > 0.0f) ? lead + gap : 0.0f);
  float text_w  = card.pos.x + card.width - pad_x - controls.x - ((controls.x > 0.0f) ? gap : 0.0f) - text_x;
  float text_h  = RowText_Height (text, text_w);
  float height  = std::max (min_height * s, std::max (text_h, controls.y) + 24.0f * s);

  RowText_Draw (text, ImVec2 (text_x, top + (height - text_h) * 0.5f), text_w);

  card.y = top + height;

  return ImVec2 (card.pos.x + card.width - pad_x - controls.x, top + (height - controls.y) * 0.5f);
}

#pragma endregion

#pragma region Monitors

struct skiv_monitor_s
{
  RECT        rect;
  bool        primary;
  std::string name;
  int         width, height;
  bool        hdr;
  float       sdr_nits;
};

static std::vector <skiv_monitor_s> monitors;

static std::unordered_map <std::wstring, std::wstring>
Monitors_FriendlyNames (void)
{
  std::unordered_map <std::wstring, std::wstring> names;

  UINT32 path_count = 0, mode_count = 0;

  if (GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
    return names;

  std::vector <DISPLAYCONFIG_PATH_INFO> paths (path_count);
  std::vector <DISPLAYCONFIG_MODE_INFO> modes (mode_count);

  if (QueryDisplayConfig (QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data (), &mode_count, modes.data (), nullptr) != ERROR_SUCCESS)
    return names;

  for (UINT32 i = 0; i < path_count; i++)
  {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source = { };
    source.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size      = sizeof (source);
    source.header.adapterId = paths [i].sourceInfo.adapterId;
    source.header.id        = paths [i].sourceInfo.id;

    DISPLAYCONFIG_TARGET_DEVICE_NAME target = { };
    target.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size      = sizeof (target);
    target.header.adapterId = paths [i].targetInfo.adapterId;
    target.header.id        = paths [i].targetInfo.id;

    if (DisplayConfigGetDeviceInfo (&source.header) == ERROR_SUCCESS &&
        DisplayConfigGetDeviceInfo (&target.header) == ERROR_SUCCESS)
      names [source.viewGdiDeviceName] = target.monitorFriendlyDeviceName;
  }

  return names;
}

static void
Monitors_Refresh (void)
{
  monitors.clear ();

  auto names = Monitors_FriendlyNames ();

  EnumDisplayMonitors (NULL, NULL, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL
  {
    auto& names = *reinterpret_cast <std::unordered_map <std::wstring, std::wstring>*> (lParam);

    MONITORINFOEXW mi = { };
    mi.cbSize = sizeof (mi);

    if (! GetMonitorInfoW (hMonitor, &mi))
      return TRUE;

    skiv_monitor_s mon = { };
    mon.rect    = mi.rcMonitor;
    mon.primary = (mi.dwFlags & MONITORINFOF_PRIMARY);
    mon.width   = mi.rcMonitor.right  - mi.rcMonitor.left;
    mon.height  = mi.rcMonitor.bottom - mi.rcMonitor.top;

    DEVMODEW dm = { };
    dm.dmSize = sizeof (dm);

    if (EnumDisplaySettingsW (mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
    {
      mon.width  = dm.dmPelsWidth;
      mon.height = dm.dmPelsHeight;
    }

    auto name = names.find (mi.szDevice);

    if (name != names.end () && ! name->second.empty ())
      mon.name = SK_WideCharToUTF8 (name->second);
    else
      mon.name = TR ("Display", "Дисплей");

    mon.hdr      = SKIF_Util_IsHDRActive (hMonitor);
    mon.sdr_nits = (mon.hdr) ? SKIF_Util_GetSDRWhiteLevel (hMonitor) : 0.0f;

    monitors.push_back (mon);

    return TRUE;
  }, reinterpret_cast <LPARAM> (&names));
}

static bool
Monitors_AnyHDR (void)
{
  for (auto& mon : monitors)
    if (mon.hdr)
      return true;

  return false;
}

// The monitors in their real arrangement; returns the height used
static float
Monitors_Draw (ImVec2 pos, float width, float max_height)
{
  if (monitors.empty ())
    return 0.0f;

  RECT bounds = monitors [0].rect;

  for (auto& mon : monitors)
    UnionRect (&bounds, &bounds, &mon.rect);

  float total_w = static_cast <float> (bounds.right  - bounds.left);
  float total_h = static_cast <float> (bounds.bottom - bounds.top);
  float scale   = std::min (width / total_w, max_height / total_h);
  float gap     = 5.0f * s;

  for (auto& mon : monitors)
  {
    ImVec2 min = ImVec2 (pos.x + (mon.rect.left   - bounds.left) * scale + gap,
                         pos.y + (mon.rect.top    - bounds.top)  * scale + gap);
    ImVec2 max = ImVec2 (pos.x + (mon.rect.right  - bounds.left) * scale - gap,
                         pos.y + (mon.rect.bottom - bounds.top)  * scale - gap);

    dl->AddRectFilled (min, max, P->inset,      6.0f * s);
    dl->AddRect       (min, max, P->lineStrong, 6.0f * s, 0, 1.0f * s);

    dl->PushClipRect (min, max, true);

    float x = min.x + 12.0f * s;

    Text (SB12 (), ImVec2 (x, min.y + 9.0f * s), P->ink, mon.name.c_str ());

    std::string res = SK_FormatString ("%d × %d", mon.width, mon.height);

    if (mon.primary)
      res += TR (" · main", " · основной");

    float res_y = min.y + 9.0f * s + SB12 ()->FontSize + 2.0f * s;
    Text (F11 (), ImVec2 (x, res_y), P->ink3, res.c_str ());

    // Badge and SDR white level at the bottom
    float  badge_h = std::max (20.0f * s, SB11 ()->FontSize + 6.0f * s);
    float  badge_y = max.y - 10.0f * s - badge_h;
    const char* badge = (mon.hdr) ? "HDR" : "SDR";
    float  badge_w = TextSize (SB11 (), badge).x + 14.0f * s;

    if (badge_y > res_y + F11 ()->FontSize + 4.0f * s)
    {
      dl->AddRectFilled (ImVec2 (x, badge_y), ImVec2 (x + badge_w, badge_y + badge_h), (mon.hdr) ? P->hdrSoft : P->sdrBadge, 5.0f * s);
      TextCentered (SB11 (), ImVec2 (x + 7.0f * s, badge_y), badge_h, (mon.hdr) ? P->hdr : P->ink3, badge);

      if (mon.hdr && mon.sdr_nits > 0.0f)
      {
        std::string nits = SK_FormatString (TR ("SDR white %.0f nits", "белый SDR %.0f нит"), mon.sdr_nits);
        TextCentered (F11 (), ImVec2 (x + badge_w + 8.0f * s, badge_y), badge_h, P->ink2, nits.c_str ());
      }
    }

    dl->PopClipRect ();
  }

  return total_h * scale;
}

#pragma endregion

#pragma region Start with Windows

// The shortcut used to be called SKIV.lnk, before the fork was renamed to SKIS
static std::wstring
Startup_LinkPath (const wchar_t* name = L"SKIS.lnk")
{
  std::wstring path;
  PWSTR        folder = nullptr;

  if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Startup, 0, nullptr, &folder)))
    path = std::wstring (folder) + L"\\" + name;

  CoTaskMemFree (folder);

  return path;
}

static bool
Startup_IsEnabled (void)
{
  std::wstring link   = Startup_LinkPath ();
  std::wstring legacy = Startup_LinkPath (L"SKIV.lnk");

  return (! link.empty () && (PathFileExistsW (link.c_str ()) || PathFileExistsW (legacy.c_str ())));
}

static void
Startup_Set (bool enable)
{
  std::wstring link = Startup_LinkPath ();

  if (link.empty ())
    return;

  DeleteFileW (Startup_LinkPath (L"SKIV.lnk").c_str ());

  if (! enable)
  {
    DeleteFileW (link.c_str ());
    return;
  }

  wchar_t exe [MAX_PATH + 2] = { };
  GetModuleFileNameW (NULL, exe, MAX_PATH);

  std::wstring dir = std::filesystem::path (exe).parent_path ().wstring ();

  // "1" starts SKIS minimized to the notification area
  SKIF_Util_CreateShortcut (link.c_str (), exe, L"1", dir.c_str (), L"SKIS", exe);
}

#pragma endregion

#pragma region Pages

enum skiv_page_e
{
  Page_Capture,
  Page_Saving,
  Page_HDR,
  Page_App,
  Page_Advanced
};

static int page = Page_Capture;

static kb_kv_s kbToggleHDRDisplay, kbCaptureWindow, kbCaptureRegion, kbCaptureScreen;

// Page heading; returns its height
static float
Heading (ImVec2 pos, float width, const char* title, const char* subtitle)
{
  Text (SB20 (), pos, P->ink, title);

  float h = SB20 ()->FontSize * 1.3f + 4.0f * s;

  return h + WrappedText (F12 (), P->ink3, ImVec2 (pos.x, pos.y + h), width, LH (F12 ()), subtitle);
}

static float
Page_Capture_Draw (ImVec2 pos, float width)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  float y = pos.y + Heading (pos, width, TR ("Capture", "Захват"), TR ("Screenshots always go to the clipboard",
                                                                      "Снимок всегда копируется в буфер обмена")) + 18.0f * s;

  struct mode_s {
    CaptureMode  type;
    skiv_icon_e  icon;
    const char*  title;
    const char*  desc;
    kb_kv_s*     kb;
  } modes [] = {
    { CaptureMode_Region, Icon_Viewfinder, TR ("Region", "Область"), TR ("All monitors freeze, drag a frame on any of them", "Все мониторы замирают, рамку можно тянуть на любом"), &kbCaptureRegion },
    { CaptureMode_Window, Icon_Window,     TR ("Window", "Окно"),    TR ("The active window",                                "Активное окно"),                                      &kbCaptureWindow },
    { CaptureMode_Screen, Icon_Screen,     TR ("Screen", "Экран"),   TR ("The whole monitor under the cursor",               "Весь монитор под курсором"),                          &kbCaptureScreen }
  };

  card_s card = Card_Begin (ImVec2 (pos.x, y), width);

  for (auto& mode : modes)
  {
    bool primary   = (mode.type == CaptureMode_Region);
    bool on        = ((_registry.eScreenshotsHotkeys & mode.type) == mode.type);
    bool recording_this = (recording == mode.kb);

    const char* desc = mode.desc;

    if (recording_this)
      desc = TR ("Press the new shortcut · Esc to cancel", "Нажмите новое сочетание · Esc — отмена");
    else if (! on)
      desc = TR ("Off · the shortcut is free", "Выключен · сочетание свободно");

    row_text_s text = {
      mode.title, desc,
      (primary) ? SB15 () : SB14 (),
      (on) ? P->ink  : P->ink4,
      (on) ? P->ink2 : P->ink4
    };

    float  kb_w  = Shortcut_Width (mode.kb, false);
    ImVec2 ctrls = ImVec2 (kb_w + 14.0f * s + ToggleSize.x * s, 36.0f * s);
    ImVec2 at    = Row (card, text, ctrls, 68.0f, 36.0f * s);

    // Mode icon
    float  row_mid = at.y + ctrls.y * 0.5f;
    ImVec2 box     = ImVec2 (card.pos.x + 16.0f * s, row_mid - 18.0f * s);

    dl->AddRectFilled (box, ImVec2 (box.x + 36.0f * s, box.y + 36.0f * s), (primary && on) ? P->accentSoft : P->cardHi, 8.0f * s);
    Icon (mode.icon, ImVec2 (box.x + 9.0f * s, box.y + 9.0f * s), 18.0f * s, (! on) ? P->ink4 : (primary) ? P->accent : P->ink2);

    ImGui::PushID (mode.title);

    Shortcut ("##keys", mode.kb, ImVec2 (at.x, row_mid - 18.0f * s), false, on);

    if (Toggle ("##on", ImVec2 (at.x + kb_w + 14.0f * s, row_mid - 11.0f * s), &on))
    {
      if (on)
      {
        _registry.eScreenshotsHotkeys |= mode.type;
        mode.kb->_callback (mode.kb->_key);
      }

      else
      {
        _registry.eScreenshotsHotkeys &= ~mode.type;
        SKIF_Util_UnregisterHotKeyCapture (mode.type);
      }

      _registry.regKVScreenshotsHotkeys.putData (_registry.eScreenshotsHotkeys);
    }

    ImGui::PopID ();
  }

  y += Card_End (card) + 10.0f * s;

  y += WrappedText (F12 (), P->ink3, ImVec2 (pos.x + 2.0f * s, y), width, LH (F12 ()),
         TR ("Click a shortcut to change it. Backspace while recording removes it.",
             "Щёлкните по сочетанию клавиш, чтобы задать новое. Backspace при записи убирает его."));

  y += WrappedText (F12 (), P->ink3, ImVec2 (pos.x + 2.0f * s, y), width, LH (F12 ()),
         TR ("A mode that is off frees its shortcut for other apps.",
             "Выключенный режим освобождает своё сочетание для других программ."));

  return y - pos.y;
}

static std::string
Pattern_Preview (const std::wstring& pattern)
{
  SYSTEMTIME st;
  GetLocalTime (&st);

  std::wstring name = pattern;

  auto _Replace = [&](const std::wstring& token, const std::wstring& value)
  {
    for (size_t pos = name.find (token); pos != std::wstring::npos; pos = name.find (token, pos + value.length ()))
      name.replace (pos, token.length (), value);
  };

  wchar_t date [100] = { }, time [100] = { };
  GetDateFormatEx (LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 100, NULL);
  GetTimeFormatEx (LOCALE_NAME_USER_DEFAULT, 0,              &st, NULL, time, 100);

  _Replace (L"<app>",  L"Telegram");
  _Replace (L"<pro>",  L"Telegram Desktop");
  _Replace (L"<exe>",  L"Telegram");
  _Replace (L"<win>",  TRW (L"Window title", L"Заголовок окна"));
  _Replace (L"<date>", date);
  _Replace (L"<time>", time);

  // Same clean-up as when the file is saved
  std::replace (name.begin (), name.end (), L':', L'.');
  std::replace (name.begin (), name.end (), L'/', L'\\');

  name.erase (std::remove_if (name.begin (), name.end (), [](wchar_t c) {
    return (c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|');
  }), name.end ());

  return SK_WideCharToUTF8 (name) + ".png";
}

// Cuts text from the left to fit the width: "…\Pictures\SKIV"
static std::string
EllipsizeLeft (ImFont* font, const std::string& text, float width)
{
  if (TextSize (font, text.c_str ()).x <= width)
    return text;

  std::string tail = text;

  while (! tail.empty ())
  {
    // Drop one UTF-8 character
    size_t n = 1;
    while (n < tail.size () && (static_cast <unsigned char> (tail [n]) & 0xC0) == 0x80)
      n++;
    tail.erase (0, n);

    std::string candidate = "…" + tail;

    if (TextSize (font, candidate.c_str ()).x <= width)
      return candidate;
  }

  return "…";
}

static float
Page_Saving_Draw (ImVec2 pos, float width)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  float y = pos.y + Heading (pos, width, TR ("Saving", "Сохранение"), TR ("Besides the clipboard, a screenshot can go straight to a folder",
                                                                        "Помимо буфера обмена снимок можно сразу класть в папку")) + 18.0f * s;

  card_s card = Card_Begin (ImVec2 (pos.x, y), width);

  // Which modes also save a file
  {
    struct pill_s { CaptureMode type; const char* label; } pills [] = {
      { CaptureMode_Region, TR ("Region", "Область") },
      { CaptureMode_Window, TR ("Window", "Окно")    },
      { CaptureMode_Screen, TR ("Screen", "Экран")   }
    };

    float pills_w = 0.0f;

    for (auto& pill : pills)
    {
      bool on = ((_registry.eScreenshotsAutosave & pill.type) == pill.type);
      pills_w += TextSize (F12 (), pill.label).x + 24.0f * s + ((on) ? 18.0f * s : 0.0f) + 6.0f * s;
    }

    pills_w -= 6.0f * s;

    row_text_s text = { TR ("Save a file", "Сохранять файл"), TR ("For these capture modes", "Для этих режимов захвата"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (pills_w, 30.0f * s));

    float x = at.x;

    for (auto& pill : pills)
    {
      bool   on    = ((_registry.eScreenshotsAutosave & pill.type) == pill.type);
      float  w     = TextSize (F12 (), pill.label).x + 24.0f * s + ((on) ? 18.0f * s : 0.0f);
      ImVec2 min   = ImVec2 (x, at.y);
      ImVec2 max   = ImVec2 (x + w, at.y + 30.0f * s);
      bool   hover = false;

      if (Hit (pill.label, min, ImVec2 (w, 30.0f * s), &hover))
      {
        if (on) _registry.eScreenshotsAutosave &= ~pill.type;
        else    _registry.eScreenshotsAutosave |=  pill.type;

        _registry.regKVScreenshotsAutosave.putData (_registry.eScreenshotsAutosave);
      }

      if (on)
      {
        dl->AddRectFilled (min, max, P->accentSoft, 15.0f * s);
        dl->AddRect       (min, max, P->accentLine, 15.0f * s, 0, 1.0f * s);
        Icon (Icon_Check, ImVec2 (x + 12.0f * s, at.y + 9.0f * s), 12.0f * s, P->accent);
        TextCentered (F12 (), ImVec2 (x + 30.0f * s, at.y), 30.0f * s, P->accent, pill.label);
      }

      else
      {
        dl->AddRect (min, max, (hover) ? P->ink3 : P->lineStrong, 15.0f * s, 0, 1.0f * s);
        TextCentered (F12 (), ImVec2 (x + 12.0f * s, at.y), 30.0f * s, (hover) ? P->ink : P->ink2, pill.label);
      }

      x += w + 6.0f * s;
    }
  }

  float pad = 16.0f * s;
  float inner_w = width - pad * 2.0f;

  // Folder
  {
    float top = Card_Row (card) + 12.0f * s;
    float x   = card.pos.x + pad;

    TextCentered (SB14 (), ImVec2 (x, top), 20.0f * s, P->ink, TR ("Folder", "Папка"));

    float line_y   = top + 28.0f * s;
    const char* change = TR ("Change…", "Изменить…");
    const char* open   = TR ("Open",    "Открыть");
    float change_w = ButtonWidth (change);
    float open_w   = ButtonWidth (open, true, true);
    float input_w  = inner_w - change_w - open_w - 16.0f * s;

    ImVec2 in_min = ImVec2 (x, line_y);
    ImVec2 in_max = ImVec2 (x + input_w, line_y + 34.0f * s);

    dl->AddRectFilled (in_min, in_max, P->inset, 7.0f * s);
    dl->AddRect       (in_min, in_max, P->line,  7.0f * s, 0, 1.0f * s);

    std::string folder = EllipsizeLeft (F13 (), _path_cache.skiv_screenshotsA, input_w - 24.0f * s);
    TextCentered (F13 (), ImVec2 (x + 12.0f * s, line_y), 34.0f * s, P->ink, folder.c_str ());

    if (Button ("##change", ImVec2 (x + input_w + 8.0f * s, line_y), change))
    {
      std::wstring newPath = SKIF_Util_FileExplorer_BrowseForFolder (_path_cache.skiv_screenshots);

      if (PathFileExistsW (newPath.c_str ()))
      {
        if (newPath.back () != L'\\')
          newPath += L'\\';

        wcsncpy_s (_path_cache.skiv_screenshots,  MAX_PATH, newPath.c_str (), _TRUNCATE);
        strncpy_s (_path_cache.skiv_screenshotsA, MAX_PATH, SK_WideCharToUTF8 (_path_cache.skiv_screenshots).data (), _TRUNCATE);

        _registry.regKVPathScreenshots.putData (_path_cache.skiv_screenshots);

        PLOG_INFO << "Screenshots folder was changed: " << _path_cache.skiv_screenshots;
      }
    }

    if (Button ("##open", ImVec2 (x + input_w + change_w + 16.0f * s, line_y), open, true, true))
    {
      std::error_code ec;
      std::filesystem::create_directories (_path_cache.skiv_screenshots, ec);
      SKIF_Util_ExplorePath (_path_cache.skiv_screenshots);
    }

    card.y = line_y + 34.0f * s + 14.0f * s;
  }

  // File name
  {
    constexpr int maxChars = 260;
    static char   pattern [maxChars] = { };
    SK_RunOnce (strncpy_s (pattern, maxChars, SK_WideCharToUTF8 (_registry.wsScreenshotsPattern).data (), _TRUNCATE));

    auto _Save = [&](void)
    {
      StrTrimA (pattern, " \t\r\n");

      if (pattern [0] == '\0')
        strncpy_s (pattern, maxChars, "<app>_<date>_<time>", _TRUNCATE);

      _registry.wsScreenshotsPattern = SK_UTF8ToWideChar (pattern);
      _registry.regKVScreenshotsPattern.putData (_registry.wsScreenshotsPattern);
    };

    float top = Card_Row (card) + 12.0f * s;
    float x   = card.pos.x + pad;

    TextCentered (SB14 (), ImVec2 (x, top), 20.0f * s, P->ink, TR ("File name", "Имя файла"));

    // Live preview on the right of the title
    std::string preview = Pattern_Preview (SK_UTF8ToWideChar (pattern));
    std::string arrow   = "→ ";
    float title_w = TextSize (SB14 (), TR ("File name", "Имя файла")).x + 16.0f * s;
    preview = EllipsizeLeft (F12 (), preview, inner_w - title_w - TextSize (F12 (), arrow.c_str ()).x);

    float preview_x = card.pos.x + width - pad - TextSize (F12 (), preview.c_str ()).x;
    float arrow_x   = preview_x - TextSize (F12 (), arrow.c_str ()).x;
    TextCentered (F12 (), ImVec2 (arrow_x,   top), 20.0f * s, P->ink3, arrow.c_str ());
    TextCentered (F12 (), ImVec2 (preview_x, top), 20.0f * s, P->ink2, preview.c_str ());

    float line_y = top + 28.0f * s;

    ImGuiID input_id     = ImGui::GetID ("##pattern");
    bool    input_active = (ImGui::GetActiveID () == input_id);

    ImGui::SetCursorScreenPos (ImVec2 (x, line_y));
    ImGui::SetNextItemWidth   (inner_w);
    ImGui::PushFont           (F13 ());
    ImGui::PushStyleVar       (ImGuiStyleVar_FramePadding,    ImVec2 (12.0f * s, (34.0f * s - F13 ()->FontSize) * 0.5f));
    ImGui::PushStyleVar       (ImGuiStyleVar_FrameRounding,   7.0f * s);
    ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 1.0f * s);
    ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImGui::ColorConvertU32ToFloat4 (P->inset));
    ImGui::PushStyleColor     (ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4 (P->inset));
    ImGui::PushStyleColor     (ImGuiCol_FrameBgActive,  ImGui::ColorConvertU32ToFloat4 (P->inset));
    ImGui::PushStyleColor     (ImGuiCol_Border,         ImGui::ColorConvertU32ToFloat4 ((input_active) ? P->accent : P->line));
    ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImGui::ColorConvertU32ToFloat4 (P->accentLine));
    // While not editing, the text is drawn on top with the variables highlighted
    ImGui::PushStyleColor     (ImGuiCol_Text, (input_active) ? ImGui::ColorConvertU32ToFloat4 (P->ink) : ImVec4 (0, 0, 0, 0));

    if (ImGui::InputText ("##pattern", pattern, maxChars, ImGuiInputTextFlags_EnterReturnsTrue))
      _Save ();

    if (ImGui::IsItemDeactivatedAfterEdit ())
      _Save ();

    if (ImGui::IsItemActive ())
      allowShortcutCtrlA = false;

    ImGui::PopStyleColor (6);
    ImGui::PopStyleVar   (3);
    ImGui::PopFont       ();

    if (! input_active)
    {
      float  tx   = x + 12.0f * s;
      float  ty   = line_y;
      const char* p = pattern;

      while (*p != '\0')
      {
        const char* open_tag  = strchr (p, '<');
        const char* close_tag = (open_tag != nullptr) ? strchr (open_tag, '>') : nullptr;

        std::string plain = (open_tag  != nullptr) ? std::string (p, open_tag) : std::string (p);
        TextCentered (F13 (), ImVec2 (tx, ty), 34.0f * s, P->ink, plain.c_str ());
        tx += TextSize (F13 (), plain.c_str ()).x;

        if (open_tag == nullptr)
          break;

        std::string tag = (close_tag != nullptr) ? std::string (open_tag, close_tag + 1) : std::string (open_tag);
        TextCentered (F13 (), ImVec2 (tx, ty), 34.0f * s, P->accent, tag.c_str ());
        tx += TextSize (F13 (), tag.c_str ()).x;

        if (close_tag == nullptr)
          break;

        p = close_tag + 1;
      }
    }

    // Insert a variable
    float chip_y = line_y + 34.0f * s + 8.0f * s;
    float cx     = x;

    TextCentered (F12 (), ImVec2 (cx, chip_y), 24.0f * s, P->ink3, TR ("Insert", "Вставить"));
    cx += TextSize (F12 (), TR ("Insert", "Вставить")).x + 8.0f * s;

    struct chip_s { const char* label; const char* token; } chips [] = {
      { TR ("+ app",          "+ программа"),      "<app>"  },
      { TR ("+ window title", "+ заголовок окна"), "<win>"  },
      { TR ("+ date",         "+ дата"),           "<date>" },
      { TR ("+ time",         "+ время"),          "<time>" }
    };

    for (auto& chip : chips)
    {
      float  w     = TextSize (F12 (), chip.label).x + 18.0f * s;

      if (cx + w > x + inner_w)
      {
        cx      = x;
        chip_y += 30.0f * s;
      }

      bool   hover = false;
      ImVec2 min   = ImVec2 (cx, chip_y);
      ImVec2 max   = ImVec2 (cx + w, chip_y + 24.0f * s);

      if (Hit (chip.token, min, ImVec2 (w, 24.0f * s), &hover))
      {
        size_t len = strlen (pattern);

        if (len > 0 && pattern [len - 1] != '_' && pattern [len - 1] != ' ' && pattern [len - 1] != '\\')
          strncat_s (pattern, maxChars, "_", _TRUNCATE);

        strncat_s (pattern, maxChars, chip.token, _TRUNCATE);
        _Save ();
      }

      dl->AddRectFilled (min, max, (hover) ? P->lineStrong : P->cardHi, 6.0f * s);
      TextCentered (F12 (), ImVec2 (cx + 9.0f * s, chip_y), 24.0f * s, (hover) ? P->ink : P->ink2, chip.label);

      cx += w + 6.0f * s;
    }

    card.y = chip_y + 24.0f * s + 14.0f * s;

    // Without any variable every new screenshot overwrites the previous one
    if (! StrStrA (pattern, "<app>") && ! StrStrA (pattern, "<pro>") && ! StrStrA (pattern, "<exe>") &&
        ! StrStrA (pattern, "<win>") && ! StrStrA (pattern, "<date>") && ! StrStrA (pattern, "<time>"))
    {
      card.y -= 4.0f * s;
      card.y += WrappedText (F12 (), P->hdr, ImVec2 (x, card.y), inner_w, LH (F12 ()),
                  TR ("No variables: every new screenshot will overwrite the previous file.",
                      "Без переменных каждый новый снимок перезапишет предыдущий файл.")) + 14.0f * s;
    }
  }

  y += Card_End (card);

  return y - pos.y;
}

static float
Page_HDR_Draw (ImVec2 pos, float width)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  float y = pos.y + Heading (pos, width, "HDR", TR ("Your monitors as they are arranged",
                                                    "Мониторы в их настоящей раскладке")) + 18.0f * s;

  card_s card = Card_Begin (ImVec2 (pos.x, y), width);

  float pad   = 16.0f * s;
  float inner = width - pad * 2.0f;

  // Monitor map
  {
    float top = Card_Row (card) + 16.0f * s;
    float h   = Monitors_Draw (ImVec2 (card.pos.x + pad - 5.0f * s, top - 5.0f * s), inner + 10.0f * s, 140.0f * s);

    card.y = top + h + 2.0f * s;

    if (kbToggleHDRDisplay._active ())
    {
      float  kb_w = Shortcut_Width (&kbToggleHDRDisplay, true);
      float  t_w  = inner - kb_w - 12.0f * s;
      const char* hint = TR ("Turn HDR on or off for the monitor under the cursor",
                             "Включить или выключить HDR на мониторе под курсором");
      float  t_h  = WrappedText (F12 (), 0, ImVec2 (), t_w, LH (F12 ()), hint, false);
      float  row  = std::max (t_h, 32.0f * s);

      WrappedText (F12 (), P->ink2, ImVec2 (card.pos.x + pad, card.y + (row - t_h) * 0.5f), t_w, LH (F12 ()), hint);
      Shortcut ("##hdr_keys", &kbToggleHDRDisplay, ImVec2 (card.pos.x + width - pad - kb_w, card.y + (row - 32.0f * s) * 0.5f), true, true);

      card.y += row;
    }

    card.y += 14.0f * s;
  }

  // What happens to a screenshot of an HDR monitor
  {
    float top = Card_Row (card) + 12.0f * s;
    float x   = card.pos.x + pad;

    TextCentered (SB14 (), ImVec2 (x, top), 20.0f * s, P->ink, TR ("Screenshot of an HDR monitor", "Снимок с HDR-монитора"));

    std::vector <seg_opt_s> opts = {
      { TR ("Convert to SDR", "Перевести в SDR"), 1 },
      { TR ("Keep HDR",       "Оставить HDR"),    0 },
      { TR ("Auto",           "Авто"),            2 }
    };

    if (Segmented ("##tonemap", ImVec2 (x, top + 28.0f * s), opts, &_registry._SnippingTonemapsHDR))
      _registry.regKVSnippingHDR.putData (_registry._SnippingTonemapsHDR);

    std::vector <rich_s> desc;

    switch (_registry._SnippingTonemapsHDR)
    {
    case 0:
      desc = { { SB12 (), P->ink,  TR ("Full HDR as a PNG. ", "Полный HDR в PNG. ") },
               { F12  (), P->ink2, TR ("Only Chromium-based apps show it correctly when pasted; elsewhere it looks washed out.",
                                       "Правильно его покажут только программы на Chromium, в остальных снимок будет выбеленным.") } };
      break;
    case 2:
      desc = { { SB12 (), P->ink,  TR ("Decided per screenshot. ", "Решает по снимку. ") },
               { F12  (), P->ink2, TR ("Nothing brighter than SDR white means SDR, otherwise the screenshot stays in HDR.",
                                       "Если на нём нет ничего ярче белого SDR, переводит в SDR, иначе оставляет HDR.") } };
      break;
    default:
      desc = { { SB12 (), P->ink,  TR ("Looks the same as on screen ", "Выглядит как на экране ") },
               { F12  (), P->ink2, TR ("and pastes correctly anywhere: Telegram, Word, browsers. Bright highlights are rolled off gently instead of clipped.",
                                       "и правильно вставляется куда угодно: Telegram, Word, браузеры. Яркие блики мягко сжимаются, а не обрезаются.") } };
      break;
    }

    float desc_y = top + 28.0f * s + 36.0f * s + 10.0f * s;
    card.y = desc_y + RichText (ImVec2 (x, desc_y), inner, LH (F12 ()), desc) + 14.0f * s;
  }

  y += Card_End (card);

  return y - pos.y;
}

static float
Page_App_Draw (ImVec2 pos, float width)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  static bool startup = Startup_IsEnabled ();

  SK_RunOnce (startup = Startup_IsEnabled ());

  float y = pos.y + Heading (pos, width, TR ("App", "Приложение"), TR ("The close button hides the window to the tray, SKIS keeps working",
                                                                     "Крестик в заголовке прячет окно в трей, SKIS продолжает работать")) + 18.0f * s;

  card_s card = Card_Begin (ImVec2 (pos.x, y), width);

  // Start with Windows
  {
    row_text_s text = { TR ("Start with Windows", "Запускать вместе с Windows"), TR ("Waits in the notification area, no window", "Ждёт в области уведомлений, без окна"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, V (ToggleSize.x, ToggleSize.y));

    if (Toggle ("##startup", at, &startup))
    {
      Startup_Set (startup);
      startup = Startup_IsEnabled ();
    }
  }

  // Language
  {
    std::string system = std::string (TR ("Follow Windows", "Как в системе"));
    std::string system_lang = (SKIV_System_IsRussian ()) ? " · русский" : " · English";
    std::string system_full = system + system_lang; // Must outlive the items below

    std::vector <seg_opt_s> items = {
      { system_full.c_str (),            SKIV_Language_System  },
      { "Русский",                       SKIV_Language_Russian },
      { "English",                       SKIV_Language_English }
    };

    const char* preview     = (_registry.iLanguage == SKIV_Language_Russian) ? "Русский"
                            : (_registry.iLanguage == SKIV_Language_English) ? "English"
                            : system.c_str ();
    const char* preview_dim = (_registry.iLanguage == SKIV_Language_System)  ? system_lang.c_str () : nullptr;

    float w = std::max (150.0f * s, TextSize (F13 (), system_full.c_str ()).x + 46.0f * s);

    row_text_s text = { TR ("Language", "Язык"), TR ("Follows the Windows display language by default", "По умолчанию берётся из языка Windows"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (w, 32.0f * s));

    if (Dropdown ("##language", at, w, preview, preview_dim, items, &_registry.iLanguage))
      _registry.regKVLanguage.putData (_registry.iLanguage);
  }

  // Theme
  {
    std::vector <seg_opt_s> opts = {
      { TR ("Dark",           "Тёмная"),        UIStyle_SKIF_Dark  },
      { TR ("Light",          "Светлая"),       UIStyle_SKIF_Light },
      { TR ("Follow Windows", "Как в системе"), UIStyle_Dynamic    }
    };

    row_text_s text = { TR ("Theme", "Тема"), nullptr, SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (SegmentedWidth (opts), 36.0f * s));

    int style = _registry.iStyle;

    // The style is applied at the start of the next frame
    if (Segmented ("##theme", at, opts, &style))
      _registry.iStyleTemp = style;
  }

  y += Card_End (card);

  return y - pos.y;
}

static float
Page_Advanced_Draw (ImVec2 pos, float width)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  float y = pos.y + Heading (pos, width, TR ("Advanced", "Дополнительно"), TR ("Usually best left alone: useful when something breaks",
                                                                             "Обычно трогать не нужно: пригодится, если что-то сломалось")) + 18.0f * s;

  card_s card = Card_Begin (ImVec2 (pos.x, y), width);

  // Rendering mode
  {
    std::vector <seg_opt_s> opts;

    if (SKIF_Util_IsWindows10OrGreater ( ))
      opts.push_back ({ "VRR", 2 });

    opts.push_back ({ TR ("Normal", "Обычный"),   1 });
    opts.push_back ({ TR ("Safe",   "Безопасный"), 0 });

    row_text_s text = { TR ("Rendering mode", "Режим отрисовки"),
                        TR ("“Safe” helps if the selection overlay flickers or stays black",
                            "«Безопасный» помогает, если затемнение мерцает или остаётся чёрным"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (SegmentedWidth (opts), 36.0f * s));

    if (Segmented ("##uimode", at, opts, &_registry.iUIMode))
    {
      _registry.regKVUIMode.putData (_registry.iUIMode);
      RecreateSwapChains = true;
    }
  }

  // Log
  {
    std::vector <seg_opt_s> levels = {
      { "None", 0 }, { "Fatal", 1 }, { "Error", 2 }, { "Warning", 3 }, { "Info", 4 }, { "Debug", 5 }, { "Verbose", 6 }
    };

    const char* open   = TR ("Open", "Открыть");
    float       open_w = ButtonWidth (open, false, true);
    float       w      = DropdownWidth (levels, 110.0f);

    row_text_s text = { TR ("Log", "Журнал"), TR ("How much gets written", "Подробность записи"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (w + 8.0f * s + open_w, 34.0f * s));

    int level = std::clamp (_registry.iLogging, 0, 6);

    if (Dropdown ("##log", ImVec2 (at.x, at.y + 1.0f * s), w, levels [level].label, nullptr, levels, &_registry.iLogging))
    {
      _registry.regKVLogging.putData (_registry.iLogging);
      plog::get()->setMaxSeverity ((plog::Severity)_registry.iLogging);

      ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                ? ImGuiDebugLogFlags_EventMask_
                                                : ImGuiDebugLogFlags_EventViewport);
    }

    if (Button ("##openlog", ImVec2 (at.x + w + 8.0f * s, at.y), open, true))
      SKIF_Util_OpenURI (SK_FormatStringW (LR"(%ws\SKIV.log)", _path_cache.skiv_userdata));
  }

  // Diagnostics
  {
    std::vector <seg_opt_s> items = {
      { TR ("None",     "Нет"),         0 },
      { TR ("Normal",   "Обычные"),     1 },
      { TR ("Enhanced", "Расширенные"), 2 }
    };

    float w = DropdownWidth (items, 140.0f);
    int   current = std::clamp (_registry.iDiagnostics, 0, 2);

    row_text_s text = { TR ("Crash reports", "Отчёты о сбоях"), TR ("Sent to the Special K team", "Уходят команде Special K"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, ImVec2 (w, 32.0f * s));

    if (Dropdown ("##diag", at, w, items [current].label, nullptr, items, &_registry.iDiagnostics))
      _registry.regKVDiagnostics.putData (_registry.iDiagnostics);
  }

  // Efficiency mode
  {
    row_text_s text = { TR ("Efficiency mode", "Режим экономии"), TR ("Lower CPU priority while SKIS waits in the tray", "Низкий приоритет процессора, пока SKIS ждёт в трее"), SB14 (), P->ink, P->ink2 };
    ImVec2 at = Row (card, text, V (ToggleSize.x, ToggleSize.y));

    if (Toggle ("##efficiency", at, &_registry.bEfficiencyMode))
      _registry.regKVEfficiencyMode.putData (_registry.bEfficiencyMode);
  }

  y += Card_End (card);

  return y - pos.y;
}

#pragma endregion

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  static bool init = false;

  if (! init)
  {
    init = true;

    kbToggleHDRDisplay = { &_registry.kbToggleHDRDisplay, &_registry.regKVHotkeyToggleHDRDisplay,
      [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeyHDRToggle (ptr->getKeybind ()); },
      []() { return (SKIF_Util_IsWindows10v1709OrGreater ( ) && SKIF_Util_IsHDRSupported (NULL)); } };

    kbCaptureWindow    = { &_registry.kbCaptureWindow,    &_registry.regKVHotkeyCaptureWindow,
      [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeyCapture (CaptureMode_Window, ptr->getKeybind ()); },
      []() { return ((_registry.eScreenshotsHotkeys & CaptureMode_Window) == CaptureMode_Window); } };

    kbCaptureRegion    = { &_registry.kbCaptureRegion,    &_registry.regKVHotkeyCaptureRegion,
      [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeyCapture (CaptureMode_Region, ptr->getKeybind ()); },
      []() { return ((_registry.eScreenshotsHotkeys & CaptureMode_Region) == CaptureMode_Region); } };

    kbCaptureScreen    = { &_registry.kbCaptureScreen,    &_registry.regKVHotkeyCaptureScreen,
      [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeyCapture (CaptureMode_Screen, ptr->getKeybind ()); },
      []() { return ((_registry.eScreenshotsHotkeys & CaptureMode_Screen) == CaptureMode_Screen); } };
  }

  s  = SKIF_ImGui_GlobalDPIScale;
  dl = ImGui::GetWindowDrawList ();
  P  = &SKIV_Palette ();

  // Refresh the monitors whenever the window reappears, and every two seconds
  static int    lastFrame   = -10;
  static double lastRefresh = 0.0;

  if (ImGui::GetFrameCount () - lastFrame > 1 || ImGui::GetTime () - lastRefresh > 2.0)
  {
    Monitors_Refresh ();
    lastRefresh = ImGui::GetTime ();
  }

  lastFrame = ImGui::GetFrameCount ();

  recordingDone = false;
  Shortcut_Process ();

  // Esc while typing must not close the window
  if (ImGui::GetIO ().WantTextInput || recordingDone)
    g_activeKeybindPopup = true;

  ImGuiWindow* window = ImGui::GetCurrentWindow ();
  ImVec2       wpos   = window->Pos;
  ImVec2       wsize  = window->Size;
  ImVec2       wmax   = ImVec2 (wpos.x + wsize.x, wpos.y + wsize.y);

  dl->AddRectFilled (wpos, wmax, P->canvas);

  // Title bar

  float bar_h = 44.0f * s;

  Icon (Icon_Viewfinder, ImVec2 (wpos.x + 16.0f * s, wpos.y + 13.0f * s), 18.0f * s, P->accent);
  TextCentered (SB13 (), ImVec2 (wpos.x + 44.0f * s, wpos.y), bar_h, P->ink,  "SKIS");
  TextCentered (F13  (), ImVec2 (wpos.x + 44.0f * s + TextSize (SB13 (), "SKIS").x + 10.0f * s, wpos.y), bar_h, P->ink3, TR ("Settings", "Настройки"));

  {
    ImVec2 close_pos  = ImVec2 (wmax.x - 46.0f * s, wpos.y);
    ImVec2 close_size = ImVec2 (46.0f * s, bar_h - 1.0f * s);
    bool   hover      = false;

    ImGui::SetCursorScreenPos (close_pos);
    bool pressed = ImGui::InvisibleButton ("##skiv_close", close_size);
    hover = ImGui::IsItemHovered ();

    if (hover)
      dl->AddRectFilled (close_pos, ImVec2 (close_pos.x + close_size.x, close_pos.y + close_size.y), P->close, 0.0f);

    Icon (Icon_Close, ImVec2 (close_pos.x + 16.0f * s, wpos.y + 15.0f * s), 14.0f * s, (hover) ? IM_COL32_WHITE : P->ink2);

    if (pressed)
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_MINIMIZE, 0x0, 0x0);
  }

  dl->AddLine (ImVec2 (wpos.x, wpos.y + bar_h - 0.5f * s), ImVec2 (wmax.x, wpos.y + bar_h - 0.5f * s), P->lineCanvas, 1.0f * s);

  // Section list

  float nav_w = 200.0f * s;

  dl->AddLine (ImVec2 (wpos.x + nav_w - 0.5f * s, wpos.y + bar_h), ImVec2 (wpos.x + nav_w - 0.5f * s, wmax.y), P->lineCanvas, 1.0f * s);

  struct nav_s { int page; skiv_icon_e icon; const char* label; } nav [] = {
    { Page_Capture,  Icon_Viewfinder, TR ("Capture",  "Захват")        },
    { Page_Saving,   Icon_Folder,     TR ("Saving",   "Сохранение")    },
    { Page_HDR,      Icon_Sun,        "HDR"                            },
    { Page_App,      Icon_App,        TR ("App",      "Приложение")    },
    { Page_Advanced, Icon_Sliders,    TR ("Advanced", "Дополнительно") }
  };

  float ny = wpos.y + bar_h + 12.0f * s;

  for (auto& item : nav)
  {
    if (item.page == Page_Advanced)
    {
      ny += 8.0f * s;
      dl->AddLine (ImVec2 (wpos.x + 14.0f * s, ny), ImVec2 (wpos.x + nav_w - 14.0f * s, ny), P->lineCanvas, 1.0f * s);
      ny += 9.0f * s;
    }

    ImVec2 min   = ImVec2 (wpos.x + 10.0f * s, ny);
    ImVec2 max   = ImVec2 (wpos.x + nav_w - 10.0f * s, ny + 36.0f * s);
    bool   on    = (page == item.page);
    bool   hover = false;

    if (Hit (item.label, min, ImVec2 (max.x - min.x, max.y - min.y), &hover))
      page = item.page;

    if (on)
    {
      dl->AddRectFilled (min, max, P->cardHi, 7.0f * s);
      dl->AddRectFilled (min, ImVec2 (min.x + 2.0f * s, max.y), P->accent, 7.0f * s, ImDrawFlags_RoundCornersLeft);
    }

    else if (hover)
      dl->AddRectFilled (min, max, P->card, 7.0f * s);

    Icon (item.icon, ImVec2 (min.x + 10.0f * s, ny + 10.0f * s), 16.0f * s, (on) ? P->accent : P->ink3);
    TextCentered (F13 (), ImVec2 (min.x + 36.0f * s, ny), 36.0f * s, (on || hover) ? P->ink : P->ink2, item.label);

    // HDR is on somewhere
    if (item.page == Page_HDR && Monitors_AnyHDR ())
    {
      const char* tag   = TR ("ON", "ВКЛ");
      float       tag_w = TextSize (SB11 (), tag).x + 12.0f * s;
      ImVec2      t_min = ImVec2 (max.x - 10.0f * s - tag_w, ny + 9.0f * s);
      ImVec2      t_max = ImVec2 (max.x - 10.0f * s, ny + 27.0f * s);

      dl->AddRectFilled (t_min, t_max, P->hdrSoft, 4.0f * s);
      TextCentered (SB11 (), ImVec2 (t_min.x + 6.0f * s, t_min.y), 18.0f * s, P->hdr, tag);
    }

    ny += 38.0f * s;
  }

  // Version and quit at the bottom of the list
  {
    const char* quit   = TR ("Quit SKIS", "Выйти из SKIS");
    ImVec2      q_min  = ImVec2 (wpos.x + 16.0f * s, wmax.y - 14.0f * s - 32.0f * s);
    ImVec2      q_size = ImVec2 (nav_w - 32.0f * s, 32.0f * s);
    bool        hover  = false;

    std::string version = std::string ("SKIS " SKIV_VERSION_STR_A) + TR (" · screenshot build", " · сборка для скриншотов");
    float       ver_h   = WrappedText (F11 (), 0, ImVec2 (), q_size.x, LH (F11 ()), version.c_str (), false);

    // Hide the version when the window is too short for it
    if (q_min.y - 10.0f * s - ver_h > ny + 8.0f * s)
      WrappedText (F11 (), P->ink4, ImVec2 (q_min.x, q_min.y - 10.0f * s - ver_h), q_size.x, LH (F11 ()), version.c_str ());

    if (Hit ("##quit", q_min, q_size, &hover))
      bKeepWindowAlive = false;

    ImVec2 q_max = ImVec2 (q_min.x + q_size.x, q_min.y + q_size.y);

    if (hover)
      dl->AddRectFilled (q_min, q_max, P->card, 7.0f * s);

    dl->AddRect (q_min, q_max, P->lineStrong, 7.0f * s, 0, 1.0f * s);
    Icon (Icon_Power, ImVec2 (q_min.x + 10.0f * s, q_min.y + 9.0f * s), 14.0f * s, (hover) ? P->ink : P->ink2);
    TextCentered (F12 (), ImVec2 (q_min.x + 32.0f * s, q_min.y), q_size.y, (hover) ? P->ink : P->ink2, quit);
  }

  // Active section, scrolling on its own when the window is short

  ImGui::SetCursorScreenPos (ImVec2 (wpos.x + nav_w, wpos.y + bar_h));

  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,     ImVec2 (0.0f, 0.0f));
  ImGui::PushStyleVar   (ImGuiStyleVar_ScrollbarSize,     8.0f * s);
  ImGui::PushStyleVar   (ImGuiStyleVar_ScrollbarRounding, 4.0f * s);
  ImGui::PushStyleColor (ImGuiCol_ScrollbarBg,          ImVec4 (0, 0, 0, 0));
  ImGui::PushStyleColor (ImGuiCol_ScrollbarGrab,        ImGui::ColorConvertU32ToFloat4 (P->scroll));
  ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabHovered, ImGui::ColorConvertU32ToFloat4 (P->ink3));
  ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabActive,  ImGui::ColorConvertU32ToFloat4 (P->ink2));

  bool content = ImGui::BeginChild ("###SKIV_SettingsContent", ImVec2 (wsize.x - nav_w, wsize.y - bar_h), ImGuiChildFlags_None,
                                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NavFlattened);

  ImGui::PopStyleColor (4);
  ImGui::PopStyleVar   (3);

  if (content)
  {
    dl = ImGui::GetWindowDrawList ();

    ImVec2 origin = ImGui::GetCursorScreenPos ();
    float  width  = ImGui::GetContentRegionAvail ().x - 56.0f * s;
    ImVec2 pos    = ImVec2 (origin.x + 28.0f * s, origin.y + 20.0f * s);
    float  height = 0.0f;

    ImGui::PushID (page);

    switch (page)
    {
    case Page_Capture:  height = Page_Capture_Draw  (pos, width); break;
    case Page_Saving:   height = Page_Saving_Draw   (pos, width); break;
    case Page_HDR:      height = Page_HDR_Draw      (pos, width); break;
    case Page_App:      height = Page_App_Draw      (pos, width); break;
    case Page_Advanced: height = Page_Advanced_Draw (pos, width); break;
    }

    ImGui::PopID ();

    // Lets the child know how tall the section is, so it can scroll
    ImGui::SetCursorScreenPos (ImVec2 (origin.x, pos.y + height + 24.0f * s));
    ImGui::Dummy (ImVec2 (1.0f, 1.0f));
  }

  ImGui::EndChild ();

  dl = ImGui::GetWindowDrawList ();

  ImGui::SetCursorScreenPos (wpos);
  ImGui::Dummy (ImVec2 (0.0f, 0.0f));
}

#pragma region Snipping toolbar

// A small dark tooltip in the style of the settings window
static void
Tooltip (const char* text, const char* keys = nullptr)
{
  if (! ImGui::IsItemHovered (ImGuiHoveredFlags_DelayShort))
    return;

  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,    V (10.0f, 7.0f));
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowRounding,   7.0f * s);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowBorderSize, 1.0f * s);
  ImGui::PushStyleColor (ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4 (P->cardHi));
  ImGui::PushStyleColor (ImGuiCol_Border,  ImGui::ColorConvertU32ToFloat4 (P->lineStrong));
  ImGui::PushStyleColor (ImGuiCol_Text,    ImGui::ColorConvertU32ToFloat4 (P->ink));
  ImGui::PushFont       (F12 ());

  ImGui::BeginTooltip    ();
  ImGui::TextUnformatted (text);

  if (keys != nullptr)
  {
    ImGui::SameLine       (0.0f, 10.0f * s);
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4 (P->ink3));
    ImGui::TextUnformatted (keys);
    ImGui::PopStyleColor  ();
  }

  ImGui::EndTooltip     ();

  ImGui::PopFont        ();
  ImGui::PopStyleColor  (3);
  ImGui::PopStyleVar    (3);
}

struct chip_spec_s
{
  skiv_icon_e icon;
  const char* label;
};

static float
Chip_Width (const chip_spec_s& chip)
{
  return 12.0f * s + 14.0f * s + 8.0f * s + TextSize (F13 (), chip.label).x + 14.0f * s;
}

// A toggle chip: icon + label, filled with the accent when on
static bool
Chip (const char* id, ImVec2 pos, const chip_spec_s& chip, bool on, bool enabled)
{
  ImVec2 size  = ImVec2 (Chip_Width (chip), 32.0f * s);
  ImVec2 max   = ImVec2 (pos.x + size.x, pos.y + size.y);
  bool   hover = false;
  bool   pressed = Hit (id, pos, size, &hover, enabled);

  ImU32 col = (! enabled) ? P->ink4 : (on) ? P->accent : (hover) ? P->ink : P->ink2;

  if (on && enabled)
  {
    dl->AddRectFilled (pos, max, P->accentSoft, 8.0f * s);
    dl->AddRect       (pos, max, P->accentLine, 8.0f * s, 0, 1.0f * s);
  }

  else
  {
    if (hover)
      dl->AddRectFilled (pos, max, P->cardHi, 8.0f * s);

    dl->AddRect (pos, max, (enabled) ? P->lineStrong : P->line, 8.0f * s, 0, 1.0f * s);
  }

  Icon ((on && enabled) ? Icon_Check : chip.icon, ImVec2 (pos.x + 12.0f * s, pos.y + 9.0f * s), 14.0f * s, col);
  TextCentered (F13 (), ImVec2 (pos.x + 34.0f * s, pos.y), size.y, col, chip.label);

  return pressed;
}

// The toolbar shown at the top of the monitor while selecting a region;
//   returns true while the mouse is over it, so no selection starts there
bool
SKIV_UI_DrawSnipToolbar (bool* save_to_disk, bool* select_file, bool show_hdr, bool hotkey_save, bool hotkey_select)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  s = SKIF_ImGui_GlobalDPIScale;
  P = &SKIV_Palette ();

  if (hotkey_save)
    *save_to_disk = ! *save_to_disk;

  if (hotkey_select && *save_to_disk)
    *select_file  = ! *select_file;

  const char* title = TR ("Select an area",  "Выделите область");
  const char* hint  = TR ("Esc to cancel",   "Esc — отмена");

  chip_spec_s save   = { Icon_Folder,   TR ("Save a file",        "Сохранить файл")   };
  chip_spec_s select = { Icon_External, TR ("Show in its folder", "Показать в папке") };

  std::vector <seg_opt_s> hdr_opts = {
    { TR ("Convert to SDR", "Перевести в SDR"), 1 },
    { TR ("Keep HDR",       "Оставить HDR"),    0 },
    { TR ("Auto",           "Авто"),            2 }
  };

  const char* hdr_label = TR ("HDR screenshot", "Снимок HDR");

  // Measure
  float pad     = 8.0f  * s;
  float gap     = 8.0f  * s;
  float sep     = 25.0f * s; // 12 + 1 + 12
  float height  = 48.0f * s;
  float lead_w  = 12.0f * s + 18.0f * s + 10.0f * s + TextSize (SB13 (), title).x + 10.0f * s + TextSize (F12 (), hint).x + 4.0f * s;
  float width   = pad + lead_w + sep + Chip_Width (save) + gap + Chip_Width (select);

  if (show_hdr)
    width += sep + TextSize (F12 (), hdr_label).x + 10.0f * s + SegmentedWidth (hdr_opts);

  width += sep + 32.0f * s + pad;

  ImGuiWindow* parent = ImGui::GetCurrentWindow ();

  ImGui::SetNextWindowPos (ImVec2 (floorf (parent->Pos.x + (parent->Size.x - width) * 0.5f),
                                   floorf (parent->Pos.y + 24.0f * s)));

  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0.0f, 0.0f));

  ImGui::BeginChild ("###SnippingToolbar", ImVec2 (width, height), ImGuiChildFlags_None,
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDecoration);

  ImGui::PopStyleVar ();

  ImGui::BringWindowToDisplayFront (ImGui::GetCurrentWindow ());

  dl = ImGui::GetWindowDrawList ();

  ImVec2 pos = ImGui::GetWindowPos ();
  ImVec2 max = ImVec2 (pos.x + width, pos.y + height);

  dl->AddRectFilled (pos, max, P->canvas,     12.0f * s);
  dl->AddRect       (pos, max, P->lineStrong, 12.0f * s, 0, 1.0f * s);

  float x   = pos.x + pad;
  float mid = pos.y + height * 0.5f;

  auto _Separator = [&](void)
  {
    x += 12.0f * s;
    dl->AddLine (ImVec2 (x, mid - 12.0f * s), ImVec2 (x, mid + 12.0f * s), P->lineStrong, 1.0f * s);
    x += 13.0f * s;
  };

  // What to do
  x += 12.0f * s;
  Icon (Icon_Viewfinder, ImVec2 (x, mid - 9.0f * s), 18.0f * s, P->accent);
  x += 28.0f * s;
  TextCentered (SB13 (), ImVec2 (x, pos.y), height, P->ink, title);
  x += TextSize (SB13 (), title).x + 10.0f * s;
  TextCentered (F12 (), ImVec2 (x, pos.y), height, P->ink3, hint);
  x += TextSize (F12 (), hint).x + 4.0f * s;

  _Separator ();

  // Save to disk, and show the file afterwards
  if (Chip ("##save", ImVec2 (x, mid - 16.0f * s), save, *save_to_disk, true))
    *save_to_disk = ! *save_to_disk;

  {
    std::string folder = std::string (TR ("Folder: ", "Папка: ")) + SKIF_CommonPathsCache::GetInstance ( ).skiv_screenshotsA;
    Tooltip (folder.c_str (), "Ctrl+S");
  }

  x += Chip_Width (save) + gap;

  if (Chip ("##select", ImVec2 (x, mid - 16.0f * s), select, *select_file, *save_to_disk))
    *select_file = ! *select_file;

  Tooltip (TR ("Open the folder with the file selected after the capture", "После снимка открыть папку и выделить файл"), "Ctrl+E");

  x += Chip_Width (select);

  // HDR handling, only when the monitor is in HDR
  if (show_hdr)
  {
    _Separator ();

    TextCentered (F12 (), ImVec2 (x, pos.y), height, P->ink3, hdr_label);
    x += TextSize (F12 (), hdr_label).x + 10.0f * s;

    if (Segmented ("##snip_hdr", ImVec2 (x, mid - 18.0f * s), hdr_opts, &_registry._SnippingTonemapsHDR))
      _registry.regKVSnippingHDR.putData (_registry._SnippingTonemapsHDR);

    x += SegmentedWidth (hdr_opts);
  }

  _Separator ();

  // Cancel
  {
    ImVec2 c_pos  = ImVec2 (x, mid - 16.0f * s);
    ImVec2 c_size = V (32.0f, 32.0f);
    bool   hover  = false;

    if (Hit ("##snip_close", c_pos, c_size, &hover))
      _registry._SnippingModeExit = true;

    if (hover)
      dl->AddRectFilled (c_pos, ImVec2 (c_pos.x + c_size.x, c_pos.y + c_size.y), P->close, 8.0f * s);

    Icon (Icon_Close, ImVec2 (c_pos.x + 9.0f * s, c_pos.y + 9.0f * s), 14.0f * s, (hover) ? IM_COL32_WHITE : P->ink2);
    Tooltip (TR ("Cancel", "Отмена"), "Esc");
  }

  bool hovered = (ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
                  (ImGui::GetActiveID () != 0 && GImGui->ActiveIdWindow == ImGui::GetCurrentWindow ()));

  ImGui::EndChild ();

  dl = ImGui::GetWindowDrawList ();

  return hovered;
}

#pragma endregion
