#include <omp.h>
#include "hw/pvr/Renderer_if.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/pvr/pvr_regs.h"
#include "oslib/oslib.h"
#include <algorithm>


#include <cmath>

#include "rend/gles/gles.h"
#include "deps/FritzGL/FritzGL.hpp"

using std::clamp;

//typedef VO_BORDER_COL_type Color;

u32 decoded_colors[3][65536];

#define MAX_RENDER_WIDTH 640
#define MAX_RENDER_HEIGHT 480
#define MAX_RENDER_PIXELS (MAX_RENDER_WIDTH * MAX_RENDER_HEIGHT)

#define STRIDE_PIXEL_OFFSET MAX_RENDER_WIDTH
#define Z_BUFFER_PIXEL_OFFSET MAX_RENDER_PIXELS

#define SHUFFL(v) v

#if HOST_OS == OS_WINDOWS
#define FLIP_Y 479 -
#else
#define FLIP_Y
#endif

DECL_ALIGN(32) u32 render_buffer[MAX_RENDER_PIXELS * 2]; //Color + depth
DECL_ALIGN(32) u32 pixels[MAX_RENDER_PIXELS];

#if HOST_OS != OS_WINDOWS

struct RECT {
  int left, top, right, bottom;
};

#include     <X11/Xlib.h>
#endif



#if HOST_OS == OS_WINDOWS
BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER), 0, 0, 1, 32, BI_RGB };
#endif


struct softrend : Renderer {
#if HOST_OS == OS_WINDOWS
  HWND hWnd;
  HBITMAP hBMP = 0, holdBMP;
  HDC hmem;
#endif

  virtual bool Init() {
    //const_setAlpha = _mm_set1_epi32(0xFF000000);
    u8 ushuffle[] = { 0x0E, 0x80, 0x0E, 0x80, 0x0E, 0x80, 0x0E, 0x80, 0x06, 0x80, 0x06, 0x80, 0x06, 0x80, 0x06, 0x80};
    memcpy(&shuffle_alpha, ushuffle, sizeof(shuffle_alpha));

#if HOST_OS == OS_WINDOWS
    hWnd = (HWND)libPvr_GetRenderTarget();

    bi.biWidth = 640;
    bi.biHeight = 480;

    RECT rect;

    GetClientRect(hWnd, &rect);

    HDC hdc = GetDC(hWnd);

    FillRect(hdc, &rect, (HBRUSH)(COLOR_BACKGROUND));

    bi.biSizeImage = bi.biWidth * bi.biHeight * 4;

    hBMP = CreateCompatibleBitmap(hdc, bi.biWidth, bi.biHeight);
    hmem = CreateCompatibleDC(hdc);
    holdBMP = (HBITMAP)SelectObject(hmem, hBMP);
    ReleaseDC(hWnd, hdc);
#endif

#define REP_16(x) ((x)* 16 + (x))
#define REP_32(x) ((x)* 8 + (x)/4)
#define REP_64(x) ((x)* 4 + (x)/16)

    for (int c = 0; c < 65536; c++) {
      //565
      decoded_colors[0][c] = 0xFF000000 | (REP_32((c >> 11) % 32) << 16) | (REP_64((c >> 5) % 64) << 8) | (REP_32((c >> 0) % 32) << 0);
      //1555
      decoded_colors[1][c] = ((c >> 0) % 2 * 255 << 24) | (REP_32((c >> 11) % 32) << 16) | (REP_32((c >> 6) % 32) << 8) | (REP_32((c >> 1) % 32) << 0);
      //4444
      decoded_colors[2][c] = (REP_16((c >> 0) % 16) << 24) | (REP_16((c >> 12) % 16) << 16) | (REP_16((c >> 8) % 16) << 8) | (REP_16((c >> 4) % 16) << 0);
    }

    // ...

    return true;
  }

  virtual void Resize(int w, int h) {

  }

  virtual void Term() {
#if HOST_OS == OS_WINDOWS
    if (hBMP) {
      DeleteObject(SelectObject(hmem, holdBMP));
      DeleteDC(hmem);
    }
#endif
  }

  virtual bool Render() {
    bool is_rtt = pvrrc.isRTT;

    memset(render_buffer, 0, sizeof(render_buffer));

    if (pvrrc.verts.used() < 3)
      return false;

    if (pvrrc.render_passes.head()[0].autosort)
      SortPParams(0, pvrrc.global_param_tr.used());

    int tcount = omp_get_num_procs() - 1;
    if (tcount == 0) tcount = 1;
    if (tcount > settings.pvr.MaxThreads) tcount = settings.pvr.MaxThreads;
    #pragma omp parallel num_threads(tcount)
    {
      int thd = omp_get_thread_num();
      int y_offs = 480 % omp_get_num_threads();
      int y_thd = 480 / omp_get_num_threads();
      int y_start = (!!thd) * y_offs + y_thd * thd;
      int y_end =  y_offs + y_thd * (thd + 1);

      RECT area = { 0, y_start, 640, y_end };
      RenderParamList<0>(&pvrrc.global_param_op, &area);      // opaque
      RenderParamList<1>(&pvrrc.global_param_pt, &area);      // punch-through
      RenderParamList<2>(&pvrrc.global_param_tr, &area);      // trig-sort
    }

    return !is_rtt;
  }

  virtual bool Process(TA_context* ctx) {
    //disable RTTs for now ..
    if (ctx->rend.isRTT)
      return false;

    ctx->rend_inuse.Lock();

    if (!ta_parse_vdrc(ctx))
      return false;

    return true;
  }

  virtual void Present() {
    const int stride = STRIDE_PIXEL_OFFSET / 4;
    for (int y = 0; y < MAX_RENDER_HEIGHT; y += 4) {
      for (int x = 0; x < MAX_RENDER_WIDTH; x += 4) {
        pixels[(FLIP_Y (y + 0))*stride + x / 4] = SHUFFL(*render_buffer++);
        pixels[(FLIP_Y (y + 1))*stride + x / 4] = SHUFFL(*render_buffer++);
        pixels[(FLIP_Y (y + 2))*stride + x / 4] = SHUFFL(*render_buffer++);
        pixels[(FLIP_Y (y + 3))*stride + x / 4] = SHUFFL(*render_buffer++);
      }
    }

#if HOST_OS == OS_WINDOWS
    SetDIBits(hmem, hBMP, 0, 480, pixels, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    RECT clientRect;

    GetClientRect(hWnd, &clientRect);

    HDC hdc = GetDC(hWnd);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;
    int x = (w - 640) / 2;
    int y = (h - 480) / 2;

    BitBlt(hdc, x, y, 640 , 480 , hmem, 0, 0, SRCCOPY);
    ReleaseDC(hWnd, hdc);
#elif defined(SUPPORT_X11)
    extern Window x11_win;
    extern Display* x11_disp;
    extern Visual* x11_vis;

    int width = 640;
    int height = 480;

    extern int x11_width;
    extern int x11_height;

    XImage* ximage = XCreateImage(x11_disp, x11_vis, 24, ZPixmap, 0, (char *)pixels, width, height, 32, width * 4);

    GC gc = XCreateGC(x11_disp, x11_win, 0, 0);
    XPutImage(x11_disp, x11_win, gc, ximage, 0, 0, (x11_width - width) / 2, (x11_height - height) / 2, width, height);
    XFree(ximage);
    XFreeGC(x11_disp, gc);
#else
    // TODO softrend without X11 (SDL f.e.)
#error Cannot use softrend without X11
#endif
  }

  template <int alpha_mode>
  void RenderParamList(List<PolyParam>* param_list, RECT* area) {

    Vertex* verts = pvrrc.verts.head();
    u32* idx = pvrrc.idx.head();

    PolyParam* params = param_list->head();
    int param_count = param_list->used();

    for (int i = 0; i < param_count; i++) {
      //int vertex_count = params[i].count - 2;

      u32* poly_idx = &idx[params[i].first];

      render_list<alpha_mode>(params[i], area, poly_idx);   /*
      for (int v = 0; v < vertex_count; v++) {
        ////<alpha_blend, pp_UseAlpha, pp_Texture, pp_IgnoreTexA, pp_ShadInstr, pp_Offset >
        RendtriangleFn fn = RendtriangleFns[alpha_mode][params[i].tsp.UseAlpha][params[i].pcw.Texture][params[i].tsp.IgnoreTexA][params[i].tsp.ShadInstr][params[i].pcw.Offset];

        fn<alpha_mode>(params[i], v, verts[poly_idx[v]], verts[poly_idx[v + 1]], verts[poly_idx[v + 2]], render_buffer, area);
      }                                                      */
    }
  }

protected:
  void render_list<alpha_mode>(PolyParam pp, RECT* area, u32* idx) {
    auto&& verts = pvrrc.verts.head();
    auto vertex_count = pp.count - 2;

    auto&& pp_UseAlpha = pp.tsp.UseAlpha;
    auto&& pp_Texture = pp.pcw.Texture;
    auto&& pp_IgnoreTexA = pp.tsp.IgnoreTexA;
    auto&& pp_ShadInstr = pp.tsp.ShadInstr;
    auto&& pp_Offset = pp.pcw.Offset;

    text_info texture { 0 };

    if (pp_Texture) {
      #pragma omp critical (texture_lookup)
      {
        texture = raw_GetTexture(pp->tsp, pp->tcw);
      }
    }

  }
};

Renderer* rend_softrend() {
  return new(_mm_malloc(sizeof(softrend), 32)) softrend();
}
