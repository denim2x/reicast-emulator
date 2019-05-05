#pragma once

#include "util.h"

struct PlaneStepper {
  __m128 ddx, ddy;
  __m128 c;

  void Setup(const Vertex &v1, const Vertex &v2, const Vertex &v3, int minx, int miny, int q, float v1_a, float v2_a,
             float v3_a, float v1_b, float v2_b, float v3_b, float v1_c, float v2_c, float v3_c, float v1_d, float v2_d,
             float v3_d) {
    //      float v1_z=v1.z,v2_z=v2.z,v3_z=v3.z;
    float Aa = ((v3_a - v1_a) * (v2.y - v1.y) - (v2_a - v1_a) * (v3.y - v1.y));
    float Ba = ((v3.x - v1.x) * (v2_a - v1_a) - (v2.x - v1.x) * (v3_a - v1_a));

    float Ab = ((v3_b - v1_b) * (v2.y - v1.y) - (v2_b - v1_b) * (v3.y - v1.y));
    float Bb = ((v3.x - v1.x) * (v2_b - v1_b) - (v2.x - v1.x) * (v3_b - v1_b));

    float Ac = ((v3_c - v1_c) * (v2.y - v1.y) - (v2_c - v1_c) * (v3.y - v1.y));
    float Bc = ((v3.x - v1.x) * (v2_c - v1_c) - (v2.x - v1.x) * (v3_c - v1_c));

    float Ad = ((v3_d - v1_d) * (v2.y - v1.y) - (v2_d - v1_d) * (v3.y - v1.y));
    float Bd = ((v3.x - v1.x) * (v2_d - v1_d) - (v2.x - v1.x) * (v3_d - v1_d));

    float C = ((v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y));
    float ddx_s_a = -Aa / C;
    float ddy_s_a = -Ba / C;

    float ddx_s_b = -Ab / C;
    float ddy_s_b = -Bb / C;

    float ddx_s_c = -Ac / C;
    float ddy_s_c = -Bc / C;

    float ddx_s_d = -Ad / C;
    float ddy_s_d = -Bd / C;

    ddx = _mm_load_ps_r(ddx_s_a, ddx_s_b, ddx_s_c, ddx_s_d);
    ddy = _mm_load_ps_r(ddy_s_a, ddy_s_b, ddy_s_c, ddy_s_d);

    float c_s_a = (v1_a - ddx_s_a * v1.x - ddy_s_a * v1.y);
    float c_s_b = (v1_b - ddx_s_b * v1.x - ddy_s_b * v1.y);
    float c_s_c = (v1_c - ddx_s_c * v1.x - ddy_s_c * v1.y);
    float c_s_d = (v1_d - ddx_s_d * v1.x - ddy_s_d * v1.y);

    c = _mm_load_ps_r(c_s_a, c_s_b, c_s_c, c_s_d);

    //z = z1 + dzdx * (minx - v1.x) + dzdy * (minx - v1.y);
    //z = (z1 - dzdx * v1.x - v1.y*dzdy) +  dzdx*inx + dzdy *iny;
  }

  __forceinline __m128
  Ip(__m128
  x,
  __m128 y
  ) const
  {
    __m128 p1 = x * ddx;
    __m128 p2 = y * ddy;

    __m128 s1 = p1 + p2;
    return s1 + c;
  }

  __forceinline __m128
  InStep(__m128
  bas) const
  {
    return bas + ddx;
  }
};

struct IPs {
  PlaneStepper ZUV;
  PlaneStepper Col;

  void
  Setup(PolyParam *pp, text_info *texture, const Vertex &v1, const Vertex &v2, const Vertex &v3, int minx, int miny,
        int q) {
    u32 w = 0, h = 0;
    if (texture) {
      w = texture->width;
      h = texture->height;
    }

    ZUV.Setup(v1, v2, v3, minx, miny, q,
              v1.z, v2.z, v3.z,
              v1.u * w * v1.z, v2.u * w * v2.z, v3.u * w * v3.z,
              v1.v * h * v1.z, v2.v * h * v2.z, v3.v * h * v3.z,
              0, -1, 1);

    Col.Setup(v1, v2, v3, minx, miny, q,
              v1.col[2], v2.col[2], v3.col[2],
              v1.col[1], v2.col[1], v3.col[1],
              v1.col[0], v2.col[0], v3.col[0],
              v1.col[3], v2.col[3], v3.col[3]
    );
  }
};
