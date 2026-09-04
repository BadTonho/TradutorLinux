#pragma once

#include "tradutorlinux/win32/types.hpp"

namespace tradutorlinux {
extern "C" {

TL_MSABI void* tl_GetStockObject(int object) noexcept;
TL_MSABI int tl_TextOut(const void* dc, int x, int y, const char* text, int length) noexcept;
TL_MSABI int tl_Rectangle(const void* dc, int left, int top, int right, int bottom) noexcept;
TL_MSABI void* tl_CreateFontA(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const char* face_name) noexcept;
TL_MSABI void* tl_CreateSolidBrush(std::uint32_t color) noexcept;
TL_MSABI int tl_DeleteObject(const void* object) noexcept;
TL_MSABI std::uint32_t tl_SetBkColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI std::uint32_t tl_SetTextColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI int tl_GetDeviceCaps(const void* dc, int index) noexcept;
TL_MSABI void* tl_CreateCompatibleDC(const void* dc) noexcept;
TL_MSABI int tl_DeleteDC(const void* dc) noexcept;
TL_MSABI void* tl_CreateCompatibleBitmap(const void* dc, int width, int height) noexcept;
TL_MSABI int tl_BitBlt(const void* dest_dc, int x, int y, int width, int height,
                       const void* src_dc, int src_x, int src_y, std::uint32_t rop) noexcept;
TL_MSABI void* tl_SelectObject(const void* dc, const void* object) noexcept;
TL_MSABI int tl_SetBkMode(const void* dc, int mode) noexcept;
TL_MSABI void* tl_CreateFontIndirectA(const void* log_font) noexcept;
TL_MSABI void* tl_CreateFontIndirectW(const void* log_font) noexcept;
TL_MSABI void* tl_CreateFontW(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline, std::uint32_t strikeout,
                              std::uint32_t charset, std::uint32_t output_precision,
                              std::uint32_t clip_precision, std::uint32_t quality,
                              std::uint32_t pitch_and_family, const std::uint16_t* face_name) noexcept;
TL_MSABI std::uint32_t tl_SetDCBrushColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI std::uint32_t tl_SetDCPenColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI void* tl_CreateBitmap(int width, int height, std::uint32_t planes,
                               std::uint32_t bit_count, const void* bits) noexcept;
TL_MSABI int tl_StretchBlt(void* dest_dc, int x_dest, int y_dest, int w_dest, int h_dest,
                           const void* src_dc, int x_src, int y_src, int w_src, int h_src,
                           std::uint32_t rop) noexcept;
TL_MSABI int tl_GetObjectW(const void* hgdiobj, int buffer_size, void* object_buffer) noexcept;
TL_MSABI void* tl_CreateDIBSection(const void* dc, const void* pbmi, std::uint32_t usage,
                                   void** ppv_bits, void* section, std::uint32_t offset) noexcept;
TL_MSABI int tl_GetTextExtentPoint32W(void* hdc, const std::uint16_t* string,
                                      int length, void* size) noexcept;
TL_MSABI int tl_StartDocW(void* hdc, const void* doc_info) noexcept;
TL_MSABI int tl_EndDoc(void* hdc) noexcept;
TL_MSABI int tl_StartPage(void* hdc) noexcept;
TL_MSABI int tl_EndPage(void* hdc) noexcept;
TL_MSABI int tl_AbortDoc(void* hdc) noexcept;
TL_MSABI int tl_GetTextMetricsW(void* hdc, void* tm) noexcept;
TL_MSABI int tl_GetTextMetricsA(void* hdc, void* tm) noexcept;
TL_MSABI void* tl_CreatePen(int style, int width, std::uint32_t color) noexcept;
TL_MSABI int tl_ExtTextOutW(void* hdc, int x, int y, std::uint32_t options, const void* rect,
                            const std::uint16_t* string, std::uint32_t count, const int* dx) noexcept;
TL_MSABI int tl_ExtTextOutA(void* hdc, int x, int y, std::uint32_t options, const void* rect,
                            const char* string, std::uint32_t count, const int* dx) noexcept;
TL_MSABI int tl_MoveToEx(void* hdc, int x, int y, void* point) noexcept;
TL_MSABI int tl_LineTo(void* hdc, int x, int y) noexcept;
TL_MSABI int tl_Polyline(void* hdc, const void* points, int count) noexcept;
TL_MSABI int tl_Polygon(void* hdc, const void* points, int count) noexcept;
TL_MSABI void* tl_CreateRectRgn(int left, int top, int right, int bottom) noexcept;
TL_MSABI int tl_CombineRgn(void* dst, void* src1, void* src2, int mode) noexcept;
TL_MSABI int tl_SelectClipRgn(void* hdc, void* rgn) noexcept;
TL_MSABI int tl_GetClipBox(void* hdc, void* rect) noexcept;
TL_MSABI int tl_GetCharWidthW(void* hdc, std::uint32_t first, std::uint32_t last, int* buffer) noexcept;
TL_MSABI int tl_GetCharWidth32W(void* hdc, std::uint32_t first, std::uint32_t last, int* buffer) noexcept;
TL_MSABI int tl_GetTextExtentPoint32A(void* hdc, const char* string, int length, void* size) noexcept;
TL_MSABI std::uint32_t tl_SetTextAlign(void* hdc, std::uint32_t align) noexcept;
TL_MSABI std::uint32_t tl_GetTextAlign(void* hdc) noexcept;
TL_MSABI int tl_SetROP2(void* hdc, int rop2) noexcept;
TL_MSABI std::uint32_t tl_GetSystemPaletteEntries(void* hdc, std::uint32_t start, std::uint32_t count, void* entries) noexcept;
TL_MSABI void* tl_CreatePatternBrush(void* hbmp) noexcept;
TL_MSABI void* tl_CreateHatchBrush(int style, std::uint32_t color) noexcept;
TL_MSABI int tl_PatBlt(void* hdc, int x, int y, int w, int h, std::uint32_t rop) noexcept;
TL_MSABI int tl_MaskBlt(void* hdc_dest, int x_dest, int y_dest, int width, int height, void* hdc_src, int x_src, int y_src, void* mask_bmp, int x_mask, int y_mask, std::uint32_t rop) noexcept;
TL_MSABI int tl_PlgBlt(void* hdc_dest, const void* point, void* hdc_src, int x_src, int y_src, int width, int height, void* mask_bmp, int x_mask, int y_mask) noexcept;
TL_MSABI int tl_AlphaBlend(void* hdc_dest, int x_dest, int y_dest, int w_dest, int h_dest, void* hdc_src, int x_src, int y_src, int w_src, int h_src, std::uint32_t blend_function) noexcept;
TL_MSABI int tl_TransparentBlt(void* hdc_dest, int x_dest, int y_dest, int w_dest, int h_dest, void* hdc_src, int x_src, int y_src, int w_src, int h_src, std::uint32_t cr_transparent) noexcept;
TL_MSABI int tl_EnumFontFamiliesExW(void* hdc, const void* logfont, void* callback, std::intptr_t lparam, std::uint32_t flags) noexcept;
TL_MSABI int tl_EnumFontFamiliesExA(void* hdc, const void* logfont, void* callback, std::intptr_t lparam, std::uint32_t flags) noexcept;
TL_MSABI void* tl_CreatePolygonRgn(const void* points, int count, int mode) noexcept;
TL_MSABI int tl_FrameRgn(void* hdc, void* rgn, void* brush, int w, int h) noexcept;
TL_MSABI int tl_FillRgn(void* hdc, void* rgn, void* brush) noexcept;
TL_MSABI int tl_PaintRgn(void* hdc, void* rgn) noexcept;
TL_MSABI int tl_InvertRgn(void* hdc, void* rgn) noexcept;
TL_MSABI void* tl_CreatePalette(const void* logpalette) noexcept;
TL_MSABI int tl_ExcludeClipRect(void* hdc, int left, int top, int right, int bottom) noexcept;
TL_MSABI int tl_GetBkMode(void* hdc) noexcept;
TL_MSABI int tl_GetCharABCWidthsFloatA(void* hdc, std::uint32_t first, std::uint32_t last, void* abc) noexcept;
TL_MSABI int tl_GetCharWidth32A(void* hdc, std::uint32_t first, std::uint32_t last, int* buffer) noexcept;
TL_MSABI int tl_GetCharWidthA(void* hdc, std::uint32_t first, std::uint32_t last, int* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetCharacterPlacementW(void* hdc, const wchar_t* str, int count, int max, void* results, std::uint32_t flags) noexcept;
TL_MSABI void* tl_GetCurrentObject(void* hdc, std::uint32_t type) noexcept;
TL_MSABI int tl_GetDIBits(void* hdc, void* hbm, std::uint32_t start, std::uint32_t lines, void* bits, void* bi, std::uint32_t usage) noexcept;
TL_MSABI int tl_GetObjectA(void* hgdiobj, int cb_buffer, void* lpv_object) noexcept;
TL_MSABI std::uint32_t tl_GetOutlineTextMetricsA(void* hdc, std::uint32_t cb_data, void* otm) noexcept;
TL_MSABI std::uint32_t tl_GetPixel(void* hdc, int x, int y) noexcept;
TL_MSABI int tl_GetTextExtentExPointA(void* hdc, const char* str, int count, int max_extent, int* fit, int* dx, void* size) noexcept;
TL_MSABI int tl_GetTextExtentPointA(void* hdc, const char* str, int count, void* size) noexcept;
TL_MSABI int tl_IntersectClipRect(void* hdc, int left, int top, int right, int bottom) noexcept;
TL_MSABI std::uint32_t tl_RealizePalette(void* hdc) noexcept;
TL_MSABI void* tl_SelectPalette(void* hdc, void* hpal, int b_force_background) noexcept;
TL_MSABI int tl_SetMapMode(void* hdc, int mode) noexcept;
TL_MSABI std::uint32_t tl_SetPaletteEntries(void* hpal, std::uint32_t start, std::uint32_t count, const void* entries) noexcept;
TL_MSABI std::uint32_t tl_SetPixel(void* hdc, int x, int y, std::uint32_t color) noexcept;
TL_MSABI int tl_TranslateCharsetInfo(std::uint32_t* src, void* cs, std::uint32_t flags) noexcept;
TL_MSABI int tl_UnrealizeObject(void* hgdiobj) noexcept;
TL_MSABI int tl_UpdateColors(void* hdc) noexcept;
TL_MSABI int tl_SetWindowOrgEx(void* hdc, int x, int y, void* lppt) noexcept;
TL_MSABI int tl_SaveDC(void* hdc) noexcept;
TL_MSABI int tl_RestoreDC(void* hdc, int nSavedDC) noexcept;
TL_MSABI int tl_OffsetWindowOrgEx(void* hdc, int x, int y, void* lppt) noexcept;
TL_MSABI int tl_SetBrushOrgEx(void* hdc, int x, int y, void* lppt) noexcept;
TL_MSABI int tl_SetDIBits(void* hdc, void* hbm, std::uint32_t start, std::uint32_t lines, const void* lpBits, const void* lpbmi, std::uint32_t fuColorUse) noexcept;
TL_MSABI int tl_DPtoLP(void* hdc, void* lpPoints, int nCount) noexcept;
TL_MSABI int tl_GetTextExtentPointW(void* hdc, const wchar_t* lpString, int c, void* lpSize) noexcept;
TL_MSABI int tl_Ellipse(void* hdc, int left, int top, int right, int bottom) noexcept;
TL_MSABI void* tl_ExtCreatePen(std::uint32_t iPenStyle, std::uint32_t cWidth, const void* plbrush, std::uint32_t cStyle, const std::uint32_t* pstyle) noexcept;
TL_MSABI int tl_GdiAlphaBlend(void* hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, void* hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, std::uint32_t ftn) noexcept;
TL_MSABI int tl_GetTextExtentExPointW(void* hdc, const wchar_t* lpszStr, int cchString, int nMaxExtent, int* lpnFit, int* alpDx, void* lpSize) noexcept;
TL_MSABI int tl_GetROP2(void* hdc) noexcept;
TL_MSABI int tl_GetClipRgn(void* hdc, void* hrgn) noexcept;
TL_MSABI void* tl_CreateRectRgnIndirect(const void* lprect) noexcept;
TL_MSABI int tl_RoundRect(void* hdc, int left, int top, int right, int bottom, int width, int height) noexcept;
TL_MSABI int tl_Arc(void* hdc, int left, int top, int right, int bottom, int x_start, int y_start, int x_end, int y_end) noexcept;
TL_MSABI int tl_Pie(void* hdc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2) noexcept;
TL_MSABI int tl_GetTextCharacterExtra(void* hdc) noexcept;
TL_MSABI int tl_GetCharABCWidthsA(void* hdc, std::uint32_t first, std::uint32_t last, void* abc) noexcept;
TL_MSABI int tl_GetDeviceGammaRamp(void* hdc, void* ramp) noexcept;
TL_MSABI void* tl_CreateDCA(const char* driver, const char* device, const char* port, const void* dev_mode) noexcept;

}  // extern "C"
}  // namespace tradutorlinux
