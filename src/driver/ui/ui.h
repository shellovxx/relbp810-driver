// Canon LBP-810 UI plug-in (CNLB810U.DLL): replaces Unidrv's document property pages with the
// three pages of the original Canon driver ("Параметры страницы", "Окончательная обработка",
// "Качество") and their dialogs. Layout and texts come from CAP1UIK.DLL (see dialogs.rc), the
// behaviour follows its decompiled code (function names sub_6A9xxxxx refer to it).
#pragma once
#include <windows.h>
#include <winspool.h>
#include <commctrl.h>
#include <winddiui.h>
#include <compstui.h>
#include <printoem.h>
#include <prcomoem.h>
#include <string>
#include <vector>

#include "../../common/lbpdm.h"
#include "resource.h"

namespace ui {

extern HMODULE g_module;

// Paper sizes in the order of Canon's "Page size" list.
struct PaperDef {
    const char* keyword;   // GPD PaperSize option
    short dmPaper;         // DMPAPER_* (0: driver defined)
    UINT nameId;           // long name (78..97); short name = nameId + 3122
    int width, height;     // 0.1 mm, portrait (CAP1UIK preview table)
};
extern const PaperDef kPapers[];
constexpr int kPaperCount = 10;          // standard sizes; Canon's "Form 1..3" are not offered yet
int DefaultPaper();                      // A4, or Letter in non-metric locales

// Everything the three pages and their dialogs edit.
struct Settings {
    int paper = 2;             // index into kPapers
    int output = 0;            // 0 = same as the page size, else 1 + index into kPapers
    int copies = 1;
    bool landscape = false;
    int layout = 0;            // "Page layout" index: 1, 2, 4, 8, 9 pages per sheet
    int order[4] = {};         // page order of the 2/4/8/9-up layouts (lbpdm encoding)
    bool manualScale = false;
    int scale = 100;           // scale shown: the automatic one unless manualScale
    bool collate = true;
    bool ignoreMismatch = false;
    bool emfSpool = false;
    bool res300 = false;
    int halftone = lbpdm::HT_PATTERN1;
    int brightness = 0, contrast = 0;   // -80..80, even
    int density = 2;           // LIGHT2, LIGHT1, STD, DARK1, DARK2
    bool tonerSave = false;
    bool refinement = true;
    std::wstring overlayFile;
    bool overlayFront = false, overlayInch = false;
    long overlayLeft = 0, overlayTop = 0, overlayWidth = 100, overlayHeight = 100;
};

int NupCount(int layout);
int AutoScale(const Settings& s);        // output size scaling, percent (sub_6A905482)
void Normalize(Settings& s);             // sub_6A904F50: automatic scale unless manual
int PreviewScale(const Settings& s);     // sub_6A916252: manual scale relative to the automatic one
int StoredAdjust(int value);             // brightness/contrast as CAP1UIK keeps it (0..100)
void ResetPageSetup(Settings& s);        // "Восст. параметры" of each page (sub_6A90554F...)
void ResetFinishing(Settings& s);
void ResetQuality(Settings& s);

// "Configurations" (profiles), kept per user and printer like CAP1UIK's PRESET_* values.
struct Profile {
    std::wstring name;         // up to 24 characters
    std::wstring notes;        // up to 160 characters
    int icon = 0;              // 0..35: icons 126..161
    Settings s;
};
struct ProfileList {
    std::wstring key;          // HKCU subkey
    std::vector<Profile> items;
    int selected = -1;         // profile the current settings came from
    bool modified = false;     // settings changed since it was selected
    bool enabled = true;       // CAP1UIK PRESET_PERMISSION (HKLM)
    static constexpr int kMax = 50;

    void Load(const std::wstring& printer);
    bool Save() const;
    void SaveSelection() const;
};

// State shared by the pages of one property sheet.
struct Sheet {
    OEMUIPSPARAM* param = nullptr;
    IPrintCoreHelper* core = nullptr;
    PFNCOMPROPSHEET pfnComPropSheet = nullptr;
    HANDLE hComPropSheet = nullptr;
    std::vector<HANDLE> pages;
    std::wstring printer;
    Settings cur;
    ProfileList profiles;
    HWND hwndPage[3] = {};
    bool updating = false;     // suppress notifications while filling controls
    bool saved = false;

    void Load();
    void Save();
    void Changed(HWND page, bool profileModified = true);   // sub_6A9033D1
};

// pages.cpp
INT_PTR CALLBACK PageSetupProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK FinishingProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK QualityProc(HWND, UINT, WPARAM, LPARAM);
Sheet* SheetOf(HWND page);
void SelectProfile(HWND page, Sheet* sh, int index);   // apply a configuration to all pages

// preview.cpp: custom controls of the Canon pages
bool RegisterControls(HINSTANCE inst);
void UnregisterControls(HINSTANCE inst);
constexpr UINT WM_LBP_SETSHEET = WM_USER + 0x20;   // lParam = Sheet*
std::wstring PreviewCaption(const Settings& s);  // "A4->A4 [Масштаб : Авто]"
void UpdatePreview(HWND page);                   // repaint image, icon and caption of a page
void PaintCollate(HWND ctl, HDC hdc, bool collate);   // finishing page picture (sub_6A907812)

// profiles.cpp
void InitProfileCombo(HWND page, Sheet* sh);
void FillProfileCombo(HWND page, Sheet* sh);
bool DrawProfileItem(HWND page, Sheet* sh, const DRAWITEMSTRUCT* di);
void OnProfileCombo(HWND page, Sheet* sh);
void AddProfileDialog(HWND owner, Sheet* sh);
void EditProfilesDialog(HWND owner, Sheet* sh);
void ViewSettingsDialog(HWND owner, Sheet* sh, int page);

// dialogs.cpp
bool RegisterOverlayClass(HINSTANCE inst);
void UnregisterOverlayClass(HINSTANCE inst);
INT_PTR OverlayDialog(HWND owner, Sheet* sh);
INT_PTR FinishingSettingsDialog(HWND owner, Sheet* sh);
INT_PTR QualityAdvancedDialog(HWND owner, Sheet* sh);
void AboutDialog(HWND owner);

std::wstring LoadStr(UINT id);
void Log(const char* fmt, ...);

} // namespace ui
