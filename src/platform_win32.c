/* Windows implementation of platform.h: registry store, CPU/network counters,
 * the screensaver preview child window, and the settings dialog. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>   /* ws2ipdef.h must precede iphlpapi.h for MIB_IF_ROW2 */
#include <ws2ipdef.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include "platform.h"
#include "settings.h"
#include "resource.h"

/* ------------------------------------------------------------------ log -- */
static FILE *g_log;

void plat_log_set_file(const char *path) {
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
}

void plat_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ------------------------------------------------------------- registry -- */
static const wchar_t *const REG_PATH = L"Software\\TheBlackWall";

static void to_wide(const char *s, wchar_t *out, int cap) {
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
}

int plat_store_read_int(const char *key, int *out) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD type = 0, val = 0, size = sizeof val;
    LSTATUS st = RegQueryValueExW(h, wk, NULL, &type, (BYTE *)&val, &size);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || type != REG_DWORD) return 0;
    *out = (int)val;
    return 1;
}

int plat_store_write_int(const char *key, int value) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS)
        return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD val = (DWORD)value;
    LSTATUS st = RegSetValueExW(h, wk, 0, REG_DWORD, (const BYTE *)&val, sizeof val);
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- stats -- */
static uint64_t ft_to_u64(FILETIME ft) {
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

int plat_stats_read(uint64_t *cpu_busy, uint64_t *cpu_total, uint64_t *net_bytes) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0;
    uint64_t i = ft_to_u64(idle), k = ft_to_u64(kernel), u = ft_to_u64(user);
    *cpu_total = k + u;            /* kernel time includes idle time */
    *cpu_busy  = (k + u) - i;

    /* Sum traffic over physical adapters; fall back to every non-loopback
     * adapter that is up (e.g. inside a VM with only virtual NICs). */
    uint64_t hw = 0, all = 0;
    PMIB_IF_TABLE2 table = NULL;
    if (GetIfTable2(&table) == NO_ERROR && table) {
        for (ULONG r = 0; r < table->NumEntries; ++r) {
            const MIB_IF_ROW2 *row = &table->Table[r];
            if (row->Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (row->OperStatus != IfOperStatusUp) continue;
            uint64_t b = row->InOctets + row->OutOctets;
            all += b;
            if (row->InterfaceAndOperStatusFlags.HardwareInterface) hw += b;
        }
        FreeMibTable(table);
    }
    *net_bytes = hw ? hw : all;
    return 1;
}

/* -------------------------------------------------------------- windows -- */
void plat_win32_virtual_screen(int *x, int *y, int *w, int *h) {
    *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (*w <= 0 || *h <= 0) { *x = 0; *y = 0; *w = 1280; *h = 720; }
}

static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(h, m, wp, lp);
}

void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h) {
    HWND parent = (HWND)parent_hwnd;
    if (!IsWindow(parent)) return NULL;
    RECT rc;
    GetClientRect(parent, &rc);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"TheBlackWallPreview";
    RegisterClassW(&wc);
    HWND child = CreateWindowExW(0, wc.lpszClassName, L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, rc.right, rc.bottom, parent, NULL, wc.hInstance, NULL);
    *w = rc.right > 0 ? rc.right : 1;
    *h = rc.bottom > 0 ? rc.bottom : 1;
    return child;
}

int plat_win32_window_alive(void *hwnd) { return IsWindow((HWND)hwnd) ? 1 : 0; }

/* -------------------------------------------------------- settings dialog -- */
static Settings g_s;

static HWND item(HWND dlg, int id) { return GetDlgItem(dlg, id); }

static void set_slider(HWND dlg, int id, int mn, int mx, int pos) {
    HWND t = item(dlg, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(t, TBM_SETTICFREQ, (mx - mn) / 10, 0);
    SendMessageW(t, TBM_SETPAGESIZE, 0, (mx - mn) / 10);
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
}
static int  get_slider(HWND dlg, int id) { return (int)SendMessageW(item(dlg, id), TBM_GETPOS, 0, 0); }
static void set_check(HWND dlg, int id, int v) { CheckDlgButton(dlg, id, v ? BST_CHECKED : BST_UNCHECKED); }
static int  get_check(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static void set_label(HWND dlg, int id, const wchar_t *fmt, int v) {
    wchar_t b[64];
    wsprintfW(b, fmt, v);
    SetDlgItemTextW(dlg, id, b);
}

static void refresh_labels(HWND dlg) {
    set_label(dlg, IDC_SPEED_VAL,   L"%d %%", g_s.movement_speed);
    set_label(dlg, IDC_SENS_VAL,    L"%d %%", g_s.mouse_sensitivity);
    set_label(dlg, IDC_DENSITY_VAL, L"%d %%", g_s.density);
    set_label(dlg, IDC_PIXSIZE_VAL, L"%d %%", g_s.pixel_size);
    set_label(dlg, IDC_BLUR_VAL,    L"%d %%", g_s.blur);
    set_label(dlg, IDC_GHOST_VAL,   L"%d %%", g_s.ghost_tail);
    set_label(dlg, IDC_SHIMMER_VAL, L"%d %%", g_s.shimmer);
    set_label(dlg, IDC_FPS_VAL,     L"%d fps", g_s.target_fps);
}

/* Show/enable rules from the design:
 *  - movement speed only matters with side movement on;
 *  - "Exit on mouse move" and its children appear only when the mouse does
 *    not rotate the view; the children are enabled only when it is checked. */
static void refresh_dependencies(HWND dlg) {
    int side = get_check(dlg, IDC_SIDE);
    EnableWindow(item(dlg, IDC_SPEED_LBL), side);
    EnableWindow(item(dlg, IDC_SPEED), side);
    EnableWindow(item(dlg, IDC_SPEED_VAL), side);

    int rot = get_check(dlg, IDC_MOUSEROT);
    int show = rot ? SW_HIDE : SW_SHOW;
    static const int mouse_group[] = { IDC_EXITMOUSE, IDC_SENS_LBL, IDC_SENS, IDC_SENS_VAL, IDC_CLICKRESET };
    for (size_t i = 0; i < sizeof mouse_group / sizeof mouse_group[0]; ++i)
        ShowWindow(item(dlg, mouse_group[i]), show);

    int em = get_check(dlg, IDC_EXITMOUSE);
    EnableWindow(item(dlg, IDC_SENS_LBL), em);
    EnableWindow(item(dlg, IDC_SENS), em);
    EnableWindow(item(dlg, IDC_SENS_VAL), em);
    EnableWindow(item(dlg, IDC_CLICKRESET), em);
}

static void invalidate_colors(HWND dlg) {
    InvalidateRect(item(dlg, IDC_WALLCOL), NULL, TRUE);
    InvalidateRect(item(dlg, IDC_FLOORCOL), NULL, TRUE);
    InvalidateRect(item(dlg, IDC_SPACECOL), NULL, TRUE);
    InvalidateRect(item(dlg, IDC_CITYCOL), NULL, TRUE);
}

static void settings_to_controls(HWND dlg) {
    set_check(dlg, IDC_SIDE, g_s.side_movement);
    set_slider(dlg, IDC_SPEED, 0, 100, g_s.movement_speed);
    set_check(dlg, IDC_MOUSEROT, g_s.mouse_rotation);
    set_check(dlg, IDC_EXITMOUSE, g_s.exit_on_mouse_move);
    set_slider(dlg, IDC_SENS, 0, 100, g_s.mouse_sensitivity);
    set_check(dlg, IDC_CLICKRESET, g_s.click_resets_view);
    set_check(dlg, IDC_EXITKEY, g_s.exit_on_any_key);
    set_slider(dlg, IDC_DENSITY, 0, 100, g_s.density);
    set_slider(dlg, IDC_PIXSIZE, 0, 100, g_s.pixel_size);
    set_slider(dlg, IDC_BLUR, 0, 100, g_s.blur);
    set_slider(dlg, IDC_GHOST, 0, 100, g_s.ghost_tail);
    set_slider(dlg, IDC_SHIMMER, 0, 100, g_s.shimmer);
    CheckRadioButton(dlg, IDC_PT_SQUARE, IDC_PT_MATRIX, IDC_PT_SQUARE + g_s.pixel_type);
    set_check(dlg, IDC_HORIZON, g_s.horizon);
    set_check(dlg, IDC_GHOSTS, g_s.show_ghosts);
    set_slider(dlg, IDC_FPS, 10, 120, g_s.target_fps);
    refresh_labels(dlg);
    refresh_dependencies(dlg);
    invalidate_colors(dlg);
}

static void controls_to_settings(HWND dlg) {
    g_s.side_movement      = get_check(dlg, IDC_SIDE);
    g_s.movement_speed     = get_slider(dlg, IDC_SPEED);
    g_s.mouse_rotation     = get_check(dlg, IDC_MOUSEROT);
    g_s.exit_on_mouse_move = get_check(dlg, IDC_EXITMOUSE);
    g_s.mouse_sensitivity  = get_slider(dlg, IDC_SENS);
    g_s.click_resets_view  = get_check(dlg, IDC_CLICKRESET);
    g_s.exit_on_any_key    = get_check(dlg, IDC_EXITKEY);
    g_s.density            = get_slider(dlg, IDC_DENSITY);
    g_s.pixel_size         = get_slider(dlg, IDC_PIXSIZE);
    g_s.blur               = get_slider(dlg, IDC_BLUR);
    g_s.ghost_tail         = get_slider(dlg, IDC_GHOST);
    g_s.shimmer            = get_slider(dlg, IDC_SHIMMER);
    g_s.pixel_type = get_check(dlg, IDC_PT_MATRIX) ? PIXEL_MATRIX
                   : get_check(dlg, IDC_PT_ROUND) ? PIXEL_ROUND : PIXEL_SQUARE;
    g_s.horizon            = get_check(dlg, IDC_HORIZON);
    g_s.show_ghosts        = get_check(dlg, IDC_GHOSTS);
    g_s.target_fps         = get_slider(dlg, IDC_FPS);
    settings_clamp(&g_s);
}

static COLORREF to_colorref(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static unsigned from_colorref(COLORREF c) {
    return ((unsigned)GetRValue(c) << 16) | ((unsigned)GetGValue(c) << 8) | GetBValue(c);
}

static void pick_color(HWND dlg, unsigned *rgb, int ctl) {
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = dlg;
    cc.lpCustColors = custom;
    cc.rgbResult = to_colorref(*rgb);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) {
        *rgb = from_colorref(cc.rgbResult);
        InvalidateRect(item(dlg, ctl), NULL, TRUE);
    }
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        settings_load(&g_s);
        settings_to_controls(dlg);
        return TRUE;
    }
    case WM_HSCROLL:
        controls_to_settings(dlg);
        refresh_labels(dlg);
        return TRUE;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
        unsigned c = d->CtlID == IDC_WALLCOL ? g_s.wall_color
                   : d->CtlID == IDC_FLOORCOL ? g_s.floor_color
                   : d->CtlID == IDC_CITYCOL ? g_s.horizon_color : g_s.space_color;
        HBRUSH b = CreateSolidBrush(to_colorref(c));
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        FrameRect(d->hDC, &d->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
        if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            controls_to_settings(dlg);
            settings_save(&g_s);
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_DEFAULTS:
            settings_defaults(&g_s);
            settings_to_controls(dlg);
            return TRUE;
        case IDC_POSE:
            /* Hand over to the pose tool, run in this same process after the
             * dialog closes (a program relaunching itself looks suspicious
             * to antivirus heuristics). Values are saved first so the tool
             * renders the same look. */
            controls_to_settings(dlg);
            settings_save(&g_s);
            EndDialog(dlg, IDC_POSE);
            return TRUE;
        case IDC_WALLCOL:  pick_color(dlg, &g_s.wall_color, IDC_WALLCOL);   return TRUE;
        case IDC_FLOORCOL: pick_color(dlg, &g_s.floor_color, IDC_FLOORCOL); return TRUE;
        case IDC_SPACECOL: pick_color(dlg, &g_s.space_color, IDC_SPACECOL); return TRUE;
        case IDC_CITYCOL:  pick_color(dlg, &g_s.horizon_color, IDC_CITYCOL); return TRUE;
        case IDC_SIDE: case IDC_MOUSEROT: case IDC_EXITMOUSE: case IDC_CLICKRESET:
        case IDC_EXITKEY: case IDC_PT_SQUARE: case IDC_PT_ROUND: case IDC_PT_MATRIX:
        case IDC_HORIZON: case IDC_GHOSTS:
            controls_to_settings(dlg);
            refresh_dependencies(dlg);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

int plat_win32_config_dialog(void *parent_hwnd) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    HWND parent = IsWindow((HWND)parent_hwnd) ? (HWND)parent_hwnd : NULL;
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CONFIG), parent, dlg_proc, 0);
    return result == IDC_POSE ? 1 : 0;
}
