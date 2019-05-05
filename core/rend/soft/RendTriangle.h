#pragma once

#include "rend/util.h"
#include "rend/soft/PixelFlush.h"

struct RendTriangle : PixelFlush {
protected:
  //u32 nok,fok;   
  void _apply(PolyParam *pp, int vertex_offset, const Vertex &v1, const Vertex &v2, const Vertex &v3, u32 *colorBuffer,
                  RECT *area) override {
    text_info texture = {0};

    if (pp_Texture) {

#pragma omp critical (texture_lookup)
      {
        texture = raw_GetTexture(pp->tsp, pp->tcw);
      }

    }

    const int stride_bytes = STRIDE_PIXEL_OFFSET * 4;
    //Plane equation


    // 28.4 fixed-point coordinates
    const float Y1 = v1.y;// iround(16.0f * v1.y);
    const float Y2 = v2.y;// iround(16.0f * v2.y);
    const float Y3 = v3.y;// iround(16.0f * v3.y);

    const float X1 = v1.x;// iround(16.0f * v1.x);
    const float X2 = v2.x;// iround(16.0f * v2.x);
    const float X3 = v3.x;// iround(16.0f * v3.x);

    int sgn = 1;

    // Deltas
    {
      //area: (X1-X3)*(Y2-Y3)-(Y1-Y3)*(X2-X3)
      float area = ((X1 - X3) * (Y2 - Y3) - (Y1 - Y3) * (X2 - X3));

      if (area > 0)
        sgn = -1;

      if (pp->isp.CullMode != 0) {
        float abs_area = fabsf(area);

        if (abs_area < FPU_CULL_VAL)
          return;

        if (pp->isp.CullMode >= 2) {
          u32 mode = vertex_offset ^pp->isp.CullMode & 1;

          if (
              (mode == 0 && area < 0) ||
              (mode == 1 && area > 0)) {
            return;
          }
        }
      }
    }

    const float DX12 = sgn * (X1 - X2);
    const float DX23 = sgn * (X2 - X3);
    const float DX31 = sgn * (X3 - X1);

    const float DY12 = sgn * (Y1 - Y2);
    const float DY23 = sgn * (Y2 - Y3);
    const float DY31 = sgn * (Y3 - Y1);

    // Fixed-point deltas
    const float FDX12 = DX12;// << 4;
    const float FDX23 = DX23;// << 4;
    const float FDX31 = DX31;// << 4;

    const float FDY12 = DY12;// << 4;
    const float FDY23 = DY23;// << 4;
    const float FDY31 = DY31;// << 4;

    // Block size, standard 4x4 (must be power of two)
    const int q = 4;

    // Bounding rectangle
    int minx = iround(mmin(X1, X2, X3, area->left));// +0xF) >> 4;
    int miny = iround(mmin(Y1, Y2, Y3, area->top));// +0xF) >> 4;

    // Start in corner of block
    minx &= ~(q - 1);
    miny &= ~(q - 1);

    int spanx = iround(mmax(X1 + 0.5f, X2 + 0.5f, X3 + 0.5f, area->right)) - minx;
    int spany = iround(mmax(Y1 + 0.5f, Y2 + 0.5f, Y3 + 0.5f, area->bottom)) - miny;

    //Inside scissor area?
    if (spanx < 0 || spany < 0)
      return;


    // Half-edge constants
    float C1 = DY12 * X1 - DX12 * Y1;
    float C2 = DY23 * X2 - DX23 * Y2;
    float C3 = DY31 * X3 - DX31 * Y3;

    // Correct for fill convention
    if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
    if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
    if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

    float MAX_12, MAX_23, MAX_31, MIN_12, MIN_23, MIN_31;

    PlaneMinMax(MIN_12, MAX_12, DX12, DY12, q);
    PlaneMinMax(MIN_23, MAX_23, DX23, DY23, q);
    PlaneMinMax(MIN_31, MAX_31, DX31, DY31, q);

    const float FDqX12 = FDX12 * q;
    const float FDqX23 = FDX23 * q;
    const float FDqX31 = FDX31 * q;

    const float FDqY12 = FDY12 * q;
    const float FDqY23 = FDY23 * q;
    const float FDqY31 = FDY31 * q;

    const float FDX12mq = FDX12 + FDY12 * q;
    const float FDX23mq = FDX23 + FDY23 * q;
    const float FDX31mq = FDX31 + FDY31 * q;

    float hs12 = C1 + FDX12 * (miny + 0.5f) - FDY12 * (minx + 0.5f) + FDqY12 - MIN_12;
    float hs23 = C2 + FDX23 * (miny + 0.5f) - FDY23 * (minx + 0.5f) + FDqY23 - MIN_23;
    float hs31 = C3 + FDX31 * (miny + 0.5f) - FDY31 * (minx + 0.5f) + FDqY31 - MIN_31;

    MAX_12 -= MIN_12;
    MAX_23 -= MIN_23;
    MAX_31 -= MIN_31;

    float C1_pm = MIN_12;
    float C2_pm = MIN_23;
    float C3_pm = MIN_31;


    u8 *cb_y = (u8 *) colorBuffer;
    cb_y += miny * stride_bytes + minx * (q * 4);

    DECL_ALIGN(64)
    IPs ip;

    ip.Setup(pp, &texture, v1, v2, v3, minx, miny, q);


    __m128 y_ps = _mm_broadcast_float(miny);
    __m128 minx_ps = _mm_load_scaled_float(minx - q, 1);
    static DECL_ALIGN
    (16)
    float ones_ps[4] = {1, 1, 1, 1};
    static DECL_ALIGN
    (16)
    float q_ps[4] = {q, q, q, q};

    // Loop through blocks
    for (int y = spany; y > 0; y -= q) {
      float Xhs12 = hs12;
      float Xhs23 = hs23;
      float Xhs31 = hs31;
      u8 *cb_x = cb_y;
      __m128 x_ps = minx_ps;
      for (int x = spanx; x > 0; x -= q) {
        Xhs12 -= FDqY12;
        Xhs23 -= FDqY23;
        Xhs31 -= FDqY31;
        x_ps = x_ps + *(__m128 * q_ps);

        // Corners of block
        bool any = EvalHalfSpaceFAny(Xhs12, Xhs23, Xhs31);

        // Skip block when outside an edge
        if (!any) {
          cb_x += q * q * 4;
          continue;
        }

        bool all = EvalHalfSpaceFAll(Xhs12, Xhs23, Xhs31, MAX_12, MAX_23, MAX_31);

        // Accept whole block when totally covered
        if (all) {
          __m128 yl_ps = y_ps;
          for (int iy = q; iy > 0; iy--) {
            PixelFlush::_apply(false, pp, &texture, x_ps, yl_ps, cb_x, x_ps, ip);
            yl_ps = yl_ps + *(__m128 * ones_ps);
            cb_x += sizeof(__m128);
          }
        } else {// Partially covered block
          float CY1 = C1_pm + Xhs12;
          float CY2 = C2_pm + Xhs23;
          float CY3 = C3_pm + Xhs31;

          __m128 pfdx12 = _mm_broadcast_float(FDX12);
          __m128 pfdx23 = _mm_broadcast_float(FDX23);
          __m128 pfdx31 = _mm_broadcast_float(FDX31);

          __m128 pcy1 = _mm_load_scaled_float(CY1, -FDY12);
          __m128 pcy2 = _mm_load_scaled_float(CY2, -FDY23);
          __m128 pcy3 = _mm_load_scaled_float(CY3, -FDY31);

          __m128 pzero = _mm_setzero_ps();

          //bool ok=false;
          __m128 yl_ps = y_ps;

          for (int iy = q; iy > 0; iy--) {
            __m128 mask1 = _mm_cmple_ps(pcy1, pzero);
            __m128 mask2 = _mm_cmple_ps(pcy2, pzero);
            __m128 mask3 = _mm_cmple_ps(pcy3, pzero);
            __m128 summary = _mm_or_ps(mask3, _mm_or_ps(mask2, mask1));

            __m128i a = _mm_cmpeq_epi32((__m128i &) summary, (__m128i &) pzero);
            int msk = _mm_movemask_ps((__m128 &) a);

            if (msk != 0xF) {
              PixelFlush::_apply(true, pp, &texture, x_ps, yl_ps, cb_x, *(__m128 * ) & a, ip);
            } else {
              PixelFlush::_apply(false, pp, &texture, x_ps, yl_ps, cb_x, *(__m128 * ) & a, ip);
            }

            yl_ps = yl_ps + *(__m128 * ones_ps);
            cb_x += sizeof(__m128);

            //CY1 += FDX12mq;
            //CY2 += FDX23mq;
            //CY3 += FDX31mq;
            pcy1 = pcy1 + pfdx12;
            pcy2 = pcy2 + pfdx23;
            pcy3 = pcy3 + pfdx31;
          }
#if 0
          if (!ok) {
            nok++;
          } else {
            fok++;
          }
#endif
        }
      }
next_y:
      hs12 += FDqX12;
      hs23 += FDqX23;
      hs31 += FDqX31;
      cb_y += stride_bytes * q;
      y_ps = y_ps + *(__m128 * q_ps);
    }
  }
};
