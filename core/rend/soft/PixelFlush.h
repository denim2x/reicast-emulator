#pragma once

#include "util.h"

__m128i shuffle_alpha;
__m128i const_setAlpha;

struct PixelFlush {
protected:
  bool pp_UseAlpha, pp_Texture, pp_IgnoreTexA, pp_Offset;
  int alpha_mode, pp_ShadInstr;

public:
  PixelFlush(int alpha_mode, bool pp_UseAlpha, bool pp_Texture, bool pp_IgnoreTexA, int pp_ShadInstr, bool pp_Offset) :
      alpha_mode(alpha_mode), pp_UseAlpha(pp_UseAlpha), pp_Texture(pp_Texture), pp_IgnoreTexA(pp_IgnoreTexA),
      pp_ShadInstr(pp_ShadInstr), pp_Offset(pp_Offset) { }
      
  __inline void operator()(bool useoldmsk, PolyParam *pp, text_info *texture, __m128 x, __m128 y, u8 *cb, __m128 oldmask, 
      IPs &ip) {
    _apply(useoldmsk, pp, texture, x, y, cb, oldmask, ip);
  }
  
protected:
  virtual void _apply(bool useoldmsk, PolyParam *pp, text_info *texture, __m128 x, __m128 y, u8 *cb, __m128 oldmask, IPs &ip) {
    x = _mm_shuffle_ps(x, x, 0);
    __m128 invW = ip.ZUV.Ip(x, y);
    __m128 u = ip.ZUV.InStep(invW);
    __m128 v = ip.ZUV.InStep(u);
    __m128 ws = ip.ZUV.InStep(v);

    _MM_TRANSPOSE4_PS(invW, u, v, ws);

    u = u / invW;
    v = v / invW;

    //invW : {z1,z2,z3,z4}
    //u    : {u1,u2,u3,u4}
    //v    : {v1,v2,v3,v4}
    //wx   : {?,?,?,?}

    __m128 *zb = (__m128 * ) & cb[Z_BUFFER_PIXEL_OFFSET * 4];

    __m128 ZMask = _mm_cmpge_ps(invW, *zb);
    if (useoldmsk)
      ZMask = oldmask & ZMask;
    u32 msk = _mm_movemask_ps(ZMask);//0xF

    if (msk == 0)
      return;

    __m128i rv;

    {
      __m128 a = ip.Col.Ip(x, y);
      __m128 b = ip.Col.InStep(a);
      __m128 c = ip.Col.InStep(b);
      __m128 d = ip.Col.InStep(c);

      //we need :

      __m128i ab = _mm_packs_epi32(_mm_cvttps_epi32(a), _mm_cvttps_epi32(b));
      __m128i cd = _mm_packs_epi32(_mm_cvttps_epi32(c), _mm_cvttps_epi32(d));

      rv = _mm_packus_epi16(ab, cd);

      if (!pp_UseAlpha) {
        rv = _mm_or_si128(rv, const_setAlpha);
      }

      if (pp_Texture) {

        __m128i ui = _mm_cvttps_epi32(u);
        __m128i vi = _mm_cvttps_epi32(v);

        __m128 uf = _mm_sub_ps(u, _mm_cvtepi32_ps(ui));
        __m128 vf = _mm_sub_ps(v, _mm_cvtepi32_ps(vi));

        __m128i ufi = _mm_cvttps_epi32(uf * _mm_set1_ps(256));
        __m128i vfi = _mm_cvttps_epi32(vf * _mm_set1_ps(256));

        //(int)v<<x+(int)u
        m128i textadr;

        textadr.mm = _mm_add_epi32(_mm_slli_epi32(vi, 16), ui);//texture addresses ! 4x of em !
        m128i textel;

        for (int i = 0; i < 4; i++) {
          u32 u = textadr.m128i_i16[i * 2 + 0];
          u32 v = textadr.m128i_i16[i * 2 + 1];

          __m128i mufi_ = _mm_shuffle_epi32(ufi, _MM_SHUFFLE(0, 0, 0, 0));
          __m128i mufi_n = _mm_sub_epi32(_mm_set1_epi32(255), mufi_);

          __m128i mvfi_ = _mm_shuffle_epi32(vfi, _MM_SHUFFLE(0, 0, 0, 0));
          __m128i mvfi_n = _mm_sub_epi32(_mm_set1_epi32(255), mvfi_);

          ufi = _mm_shuffle_epi32(ufi, _MM_SHUFFLE(0, 3, 2, 1));
          vfi = _mm_shuffle_epi32(vfi, _MM_SHUFFLE(0, 3, 2, 1));

          u32 pixel;

          #if 0
          u32 textel_size = 2;

          u32 pixel00 = decoded_colors[texture->textype][texture->pdata[((u + 1) % texture->width + (v + 1) % texture->height * texture->width)]];
          u32 pixel01 = decoded_colors[texture->textype][texture->pdata[((u + 0) % texture->width + (v + 1) % texture->height * texture->width)]];
          u32 pixel10 = decoded_colors[texture->textype][texture->pdata[((u + 1) % texture->width + (v + 0) % texture->height * texture->width)]];
          u32 pixel11 = decoded_colors[texture->textype][texture->pdata[((u + 0) % texture->width + (v + 0) % texture->height * texture->width)]];


          for (int j = 0; j < 4; j++) {
            ((u8*)&pixel)[j] =

                (((u8*)&pixel00)[j] * uf.m128_f32[i] + ((u8*)&pixel01)[j] * (1 - uf.m128_f32[i])) * vf.m128_f32[i] + (((u8*)&pixel10)[j] * uf.m128_f32[i] + ((u8*)&pixel11)[j] * (1 - uf.m128_f32[i])) * (1 - vf.m128_f32[i]);
          }
          #endif

          __m128i px = ((__m128i *) texture->pdata)[((u + 0) % texture->width +
                                                    (v + 0) % texture->height * texture->width)];


          __m128i tex_00 = _mm_cvtepu8_epi32(px);
          __m128i tex_01 = _mm_cvtepu8_epi32(_mm_shuffle_epi32(px, _MM_SHUFFLE(0, 0, 0, 1)));
          __m128i tex_10 = _mm_cvtepu8_epi32(_mm_shuffle_epi32(px, _MM_SHUFFLE(0, 0, 0, 2)));
          __m128i tex_11 = _mm_cvtepu8_epi32(_mm_shuffle_epi32(px, _MM_SHUFFLE(0, 0, 0, 3)));

          tex_00 = _mm_add_epi32(_mm_mullo_epi32(tex_00, mufi_), _mm_mullo_epi32(tex_01, mufi_n));
          tex_10 = _mm_add_epi32(_mm_mullo_epi32(tex_10, mufi_), _mm_mullo_epi32(tex_10, mufi_n));

          tex_00 = _mm_add_epi32(_mm_mullo_epi32(tex_00, mvfi_), _mm_mullo_epi32(tex_10, mvfi_n));
          tex_00 = _mm_srli_epi32(tex_00, 16);

          tex_00 = _mm_packus_epi32(tex_00, tex_00);
          tex_00 = _mm_packus_epi16(tex_00, tex_00);
          pixel = _mm_cvtsi128_si32(tex_00);
          #if 0
          //top    = c0 * a + c1 * (1-a)
          //bottom = c2 * a + c3 * (1-a)

          //[c0 c2] [c1 c3]
          //[c0 c2]*a + [c1 c3] * (1 - a) = [cx cy]
          //[cx * d + cy * (1-d)]
          //cf
          _mm_unpacklo_epi8()
          __m128i y = _mm_cvtps_epi32(x);    // Convert them to 32-bit ints
          y = _mm_packus_epi32(y, y);        // Pack down to 16 bits
          y = _mm_packus_epi16(y, y);        // Pack down to 8 bits
          *(int*)out = _mm_cvtsi128_si32(y); // Store the lower 32 bits

          // 0x000000FF * 0x00010001 = 0x00FF00FF




          __m128i px = ((__m128i*)texture->pdata)[((u) & ( texture->width - 1) + (v) & (texture->height-1) * texture->width)];

          __m128i lo_px = _mm_cvtepu8_epi16(px);
          __m128i hi_px = _mm_cvtepu8_epi16(_mm_shuffle_epi32(px, _MM_SHUFFLE(1, 0, 3, 2)));
          #endif
          textel.m128i_i32[i] = pixel;
        }

        if (pp_IgnoreTexA) {
          textel.mm = _mm_or_si128(textel.mm, const_setAlpha);
        }

        if (pp_ShadInstr == 0) {
          //color.rgb = texcol.rgb;
          //color.a = texcol.a;
          rv = textel.mm;
        } else if (pp_ShadInstr == 1) {
          //color.rgb *= texcol.rgb;
          //color.a = texcol.a;

          //color.a = 1
          rv = _mm_or_si128(rv, const_setAlpha);

          //color *= texcol
          __m128i lo_rv = _mm_cvtepu8_epi16(rv);
          __m128i hi_rv = _mm_cvtepu8_epi16(_mm_shuffle_epi32(rv, _MM_SHUFFLE(1, 0, 3, 2)));


          __m128i lo_fb = _mm_cvtepu8_epi16(textel.mm);
          __m128i hi_fb = _mm_cvtepu8_epi16(_mm_shuffle_epi32(textel.mm, _MM_SHUFFLE(1, 0, 3, 2)));


          lo_rv = _mm_mullo_epi16(lo_rv, lo_fb);
          hi_rv = _mm_mullo_epi16(hi_rv, hi_fb);

          rv = _mm_packus_epi16(_mm_srli_epi16(lo_rv, 8), _mm_srli_epi16(hi_rv, 8));
        } else if (pp_ShadInstr == 2) {
          //color.rgb=mix(color.rgb,texcol.rgb,texcol.a);

          // a bit wrong atm, as it also mixes alphas
          __m128i lo_rv = _mm_cvtepu8_epi16(rv);
          __m128i hi_rv = _mm_cvtepu8_epi16(_mm_shuffle_epi32(rv, _MM_SHUFFLE(1, 0, 3, 2)));


          __m128i lo_fb = _mm_cvtepu8_epi16(textel.mm);
          __m128i hi_fb = _mm_cvtepu8_epi16(_mm_shuffle_epi32(textel.mm, _MM_SHUFFLE(1, 0, 3, 2)));

          __m128i lo_rv_alpha = _mm_shuffle_epi8(lo_fb, shuffle_alpha);
          __m128i hi_rv_alpha = _mm_shuffle_epi8(hi_fb, shuffle_alpha);

          __m128i lo_fb_alpha = _mm_sub_epi16(_mm_set1_epi16(255), lo_rv_alpha);
          __m128i hi_fb_alpha = _mm_sub_epi16(_mm_set1_epi16(255), hi_rv_alpha);


          lo_rv = _mm_mullo_epi16(lo_rv, lo_rv_alpha);
          hi_rv = _mm_mullo_epi16(hi_rv, hi_rv_alpha);

          lo_fb = _mm_mullo_epi16(lo_fb, lo_fb_alpha);
          hi_fb = _mm_mullo_epi16(hi_fb, hi_fb_alpha);

          rv = _mm_packus_epi16(_mm_srli_epi16(_mm_adds_epu16(lo_rv, lo_fb), 8),
              _mm_srli_epi16(_mm_adds_epu16(hi_rv, hi_fb), 8));
        } else if (pp_ShadInstr == 3) {
          //color*=texcol
          __m128i lo_rv = _mm_cvtepu8_epi16(rv);
          __m128i hi_rv = _mm_cvtepu8_epi16(_mm_shuffle_epi32(rv, _MM_SHUFFLE(1, 0, 3, 2)));


          __m128i lo_fb = _mm_cvtepu8_epi16(textel.mm);
          __m128i hi_fb = _mm_cvtepu8_epi16(_mm_shuffle_epi32(textel.mm, _MM_SHUFFLE(1, 0, 3, 2)));


          lo_rv = _mm_mullo_epi16(lo_rv, lo_fb);
          hi_rv = _mm_mullo_epi16(hi_rv, hi_fb);

          rv = _mm_packus_epi16(_mm_srli_epi16(lo_rv, 8), _mm_srli_epi16(hi_rv, 8));
        }

        if (pp_Offset) {
          //add offset
        }


        //textadr = _mm_add_epi32(textadr, _mm_setr_epi32(tex_addr, tex_addr, tex_addr, tex_addr));
        //rv = textel.mm; // _mm_xor_si128(rv, textadr);
      }
    }

    //__m128i rv=ip.col;//_mm_xor_si128(_mm_cvtps_epi32(x * Z.c),_mm_cvtps_epi32(y));

    //Alpha test
    if (alpha_mode == 1) {
      __m128i fb = *(__m128i *) cb;

      #if 1
      m128i mm_rv, mm_fb;
      mm_rv.mm = rv;
      mm_fb.mm = fb;
      //ALPHA_TEST
      for (int i = 0; i < 4; i++) {
        if (mm_rv.m128i_u8[i * 4 + 3] < PT_ALPHA_REF) {
          mm_rv.m128i_u32[i] = mm_fb.m128i_u32[i];
        }
      }

      rv = mm_rv.mm;
      #else
      __m128i ALPHA_TEST = _mm_set1_epi8(PT_ALPHA_REF);
      __m128i mask = _mm_cmplt_epi8(_mm_subs_epu16(ALPHA_TEST, rv), _mm_setzero_si128());

      mask = _mm_srai_epi32(mask, 31); //FF on the pixels we want to keep

      rv = _mm_or_si128(_mm_and_si128(rv, mask), _mm_andnot_si128(mask, cb));
    #endif

    } else if (alpha_mode == 2) {
      __m128i fb = *(__m128i *) cb;
      #if 0
      for (int i = 0; i < 16; i += 4) {
        u8 src_blend[4] = { rv.m128i_u8[i + 3], rv.m128i_u8[i + 3], rv.m128i_u8[i + 3], rv.m128i_u8[i + 3] };
        u8 dst_blend[4] = { 255 - rv.m128i_u8[i + 3], 255 - rv.m128i_u8[i + 3], 255 - rv.m128i_u8[i + 3], 255 - rv.m128i_u8[i + 3] };
        for (int j = 0; j < 4; j++) {
          rv.m128i_u8[i + j] = (rv.m128i_u8[i + j] * src_blend[j]) / 256 + (fb.m128i_u8[i + j] * dst_blend[j]) / 256;
        }
      }
      #else


      __m128i lo_rv = _mm_cvtepu8_epi16(rv);
      __m128i hi_rv = _mm_cvtepu8_epi16(_mm_shuffle_epi32(rv, _MM_SHUFFLE(1, 0, 3, 2)));


      __m128i lo_fb = _mm_cvtepu8_epi16(fb);
      __m128i hi_fb = _mm_cvtepu8_epi16(_mm_shuffle_epi32(fb, _MM_SHUFFLE(1, 0, 3, 2)));

      __m128i lo_rv_alpha = _mm_shuffle_epi8(lo_rv, shuffle_alpha);
      __m128i hi_rv_alpha = _mm_shuffle_epi8(hi_rv, shuffle_alpha);

      __m128i lo_fb_alpha = _mm_sub_epi16(_mm_set1_epi16(255), lo_rv_alpha);
      __m128i hi_fb_alpha = _mm_sub_epi16(_mm_set1_epi16(255), hi_rv_alpha);


      lo_rv = _mm_mullo_epi16(lo_rv, lo_rv_alpha);
      hi_rv = _mm_mullo_epi16(hi_rv, hi_rv_alpha);

      lo_fb = _mm_mullo_epi16(lo_fb, lo_fb_alpha);
      hi_fb = _mm_mullo_epi16(hi_fb, hi_fb_alpha);

      rv = _mm_packus_epi16(_mm_srli_epi16(_mm_adds_epu16(lo_rv, lo_fb), 8),
          _mm_srli_epi16(_mm_adds_epu16(hi_rv, hi_fb), 8));
    #endif
    }

    if (msk != 0xF) {
      rv = _mm_and_si128(rv, *(__m128i * ) & ZMask);
      rv = _mm_or_si128(_mm_andnot_si128(*(__m128i * ) & ZMask, *(__m128i *) cb), rv);

      invW = invW & ZMask;
      invW = _mm_or_ps(_mm_andnot_ps(ZMask, *zb), invW);

    }
    *zb = invW;
    *(__m128i *) cb = rv;
  }
};
