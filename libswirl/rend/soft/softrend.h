#pragma once
#include "rend/rend.h"
#include "deps/libGL/libGL.hpp"

const size_t MAX_RENDER_WIDTH = 640;
const size_t MAX_RENDER_HEIGHT = 480;

extern GL::Context context;

enum ListType : u32 {
  Translucent, Punch_Through, Opaque
};

struct SortTrigDrawParam {
  PolyParam* ppid;
  u32 first;
  u32 count;
};

struct IndexTrig {
  u32 id[3];
  u16 pid;
  f32 z;
};

struct Color {
  void Clear(f32 red, f32 green, f32 blue, f32 alpha) {
    if (red != _clear_r || green != _clear_g || blue != _clear_b || alpha != _clear_a || _disable_cache) {
      _clear_r = red;
      _clear_g = green;
      _clear_b = blue;
      _clear_a = alpha;
      glClearColor(red, green, blue, alpha);
    }
  }
  
  void Reset() {
    _clear_r = -1.f;
    _clear_g = -1.f;
    _clear_b = -1.f;
    _clear_a = -1.f;
  }
  
protected:
  f32 _clear_r;
  f32 _clear_g;
  f32 _clear_b;
  f32 _clear_a;
};

class Cache {
protected:
  bool _disable_cache;
  Color color;
  
public:
  void Enable() {
    _disable_cache = false;
    Reset();
  }
  
  void Disable() {
    _disable_cache = true;
  }
  
  void CullFace(GLenum mode) {
    if (mode != _cull_face || _disable_cache) {
      _cull_face = mode;
      engine.setCullMode(mode);
    }
  }
  
  void Reset() {
    color.Reset();
    _texture = 0xFFFFFFFFu;
    _src_blend_factor = 0xFFFFFFFFu;
    _dst_blend_factor = 0xFFFFFFFFu;
    _en_blend = 0xFF;
    _en_cull_face = 0xFF;
    _en_depth_test = 0xFF;
    _en_scissor_test = 0xFF;
    _en_stencil_test = 0xFF;
    _cull_face = 0xFFFFFFFFu;
    _depth_func = 0xFFFFFFFFu;
    _depth_mask = 0xFF;
    _program = 0xFFFFFFFFu;
    _texture_cache_size = 0;
    _stencil_func = 0xFFFFFFFFu;
    _stencil_ref = -1;
    _stencil_fmask = 0;
    _stencil_sfail = 0xFFFFFFFFu;
    _stencil_dpfail = 0xFFFFFFFFu;
    _stencil_dppass = 0xFFFFFFFFu;
    _stencil_mask = 0;
  }
};

struct Context {
  std::unordered_map<u32, PipelineShader> shaders;
  bool rotate90;
  
  struct {
    GLuint program;
    GLuint scale;
  } OSD_SHADER;
  
  struct {
    GLuint geometry, idxs, idxs2;
    GLuint vao;
  } vbo;
  
  struct {
    u32 TexAddr;
    GLuint depthb;
    GLuint tex;
    GLuint fbo;
  } rtt;
  
  struct {
    GLuint depthb;
    GLuint colorb;
    GLuint tex;
    GLuint fbo;
    int width;
    int height;
  } ofbo;
  
  GLenum index_type;
  bool swap_buffer_not_preserved;
  
  size_t get_index_size() { return index_type == GL_UNSIGNED_INT ? sizeof(u32) : sizeof(u16); }
};

const u32 Zfunction[] = {
  GL::NEVER,               //0 Never
  GL::LESS,     /*EQUAL*/  //1 Less
  GL::EQUAL,               //2 Equal
  GL::LEQUAL,              //3 Less Or Equal
  GL::GREATER,  /*EQUAL*/  //4 Greater
  GL::NOTEQUAL,            //5 Not Equal
  GL::GEQUAL,              //6 Greater Or Equal
  GL::ALWAYS,              //7 Always
};

const static u32 CullMode[] = {
  GL::NONE,  //0   No culling          No culling
  GL::NONE,  //1   Cull if Small       Cull if ( |det| < fpu_cull_val )
  
  GL::FRONT, //2   Cull if Negative    Cull if ( |det| < 0 ) or ( |det| < fpu_cull_val )
  GL::BACK,  //3   Cull if Positive    Cull if ( |det| > 0 ) or ( |det| < fpu_cull_val )
};

f32 minZ(Vertex* v, u32* mod) {
  return min({ v[mod[0]].z, v[mod[1]].z, v[mod[2]].z });
}

void fill_id(u32* d, Vertex* v0, Vertex* v1, Vertex* v2,  Vertex* vb) {
  d[0] = v0 - vb;
  d[1] = v1 - vb;
  d[2] = v2 - vb;
}

bool operator<(const IndexTrig &left, const IndexTrig &right) {
  return left.z < right.z;
}

bool operator==(PolyParam* pp0, PolyParam* pp1) {
  return (pp0->pcw.full & PCW_DRAW_MASK) == (pp1->pcw.full & PCW_DRAW_MASK) &&
         pp0->isp.full == pp1->isp.full &&
         pp0->tcw.full == pp1->tcw.full &&
         pp0->tsp.full == pp1->tsp.full &&
         pp0->tileclip == pp1->tileclip;
}

bool operator<(const PolyParam &left, const PolyParam &right) {
  return left.zvZ < right.zvZ;
}

void SetTextureRepeatMode(u32 dir, u32 clamp, u32 mirror) {
  if (clamp) {
    cache.TexParameteri(GL::TEXTURE_2D, dir, GL::CLAMP_TO_EDGE);
  } else {
    cache.TexParameteri(GL::TEXTURE_2D, dir, mirror ? GL::MIRRORED_REPEAT : GL::REPEAT);
  }
}

//Sort based on min-z of each strip
void SortPParams(int first, int count) {
  if (pvrrc.verts.used() == 0 || count <= 1)
    return;
    
  Vertex* vtx_base = pvrrc.verts.head();
  u32* idx_base = pvrrc.idx.head();
  
  PolyParam* pp = &pvrrc.global_param_tr.head()[first];
  PolyParam* pp_end = pp + count;
  
  while (pp != pp_end) {
    if (pp->count < 2) {
      pp->zvZ = 0;
    } else {
      u32* idx = idx_base + pp->first;
      
      Vertex* vtx = vtx_base + idx[0];
      Vertex* vtx_end = vtx_base + idx[pp->count - 1] + 1;
      
      u32 zv = 0xFFFFFFFF;
      while (vtx != vtx_end) {
        zv = min(zv, (u32&)vtx->z);
        vtx++;
      }
      
      pp->zvZ = (f32&)zv;
    }
    pp++;
  }
  
  std::stable_sort(pvrrc.global_param_tr.head() + first, pvrrc.global_param_tr.head() + first + count);
}

u32 init_output_framebuffer(int width, int height) {
  if (width != ctx.ofbo.width || height != ctx.ofbo.height) {
    free_output_framebuffer();
    ctx.ofbo.width = width;
    ctx.ofbo.height = height;
  }
  if (ctx.ofbo.fbo == 0) {
    // Create the depth+stencil renderbuffer
    glGenRenderbuffers(1, &ctx.ofbo.depthb);
    glBindRenderbuffer(GL::RENDERBUFFER, ctx.ofbo.depthb);
    glRenderbufferStorage(GL::RENDERBUFFER, GL::DEPTH_COMPONENT16, width, height);
    
    // Create a texture for rendering to
    ctx.ofbo.tex = cache.GenTexture();
    cache.BindTexture(GL::TEXTURE_2D, ctx.ofbo.tex);
    
    glTexImage2D(GL::TEXTURE_2D, 0, GL::RGBA, width, height, 0, GL::RGBA, GL::UNSIGNED_BYTE, 0);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, GL::LINEAR);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::LINEAR);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_S, GL::CLAMP_TO_EDGE);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_T, GL::CLAMP_TO_EDGE);
    
    // Create the framebuffer
    glGenFramebuffers(1, &ctx.ofbo.fbo);
    glBindFramebuffer(GL::FRAMEBUFFER, ctx.ofbo.fbo);
    
    // Attach the depth buffer to our FBO.
    glFramebufferRenderbuffer(GL::FRAMEBUFFER, GL::DEPTH_ATTACHMENT, GL::RENDERBUFFER, ctx.ofbo.depthb);
    
    // Attach the texture/renderbuffer to the FBO
    glFramebufferTexture2D(GL::FRAMEBUFFER, GL::COLOR_ATTACHMENT0, GL::TEXTURE_2D, ctx.ofbo.tex, 0);
    
    // Check that our FBO creation was successful
    u32 uStatus = glCheckFramebufferStatus(GL::FRAMEBUFFER);
    
    verify(uStatus == GL::FRAMEBUFFER_COMPLETE);
    
    cache.Disable(GL::SCISSOR_TEST);
    cache.ClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL::COLOR_BUFFER_BIT);
  } else {
    glBindFramebuffer(GL::FRAMEBUFFER, ctx.ofbo.fbo);
  }
  
  glViewport(0, 0, width, height);
  glCheck();
  
  return ctx.ofbo.fbo;
}

bool render_output_framebuffer() {
  cache.Disable(GL::SCISSOR_TEST);
  glViewport(0, 0, screen_width, screen_height);
  if (ctx.ofbo.tex == 0)
    return false;
  glBindFramebuffer(GL::FRAMEBUFFER, 0);
  f32 scl = 480.f / screen_height;
  f32 tx = (screen_width * scl - 640.f) / 2;
  DrawQuad(ctx.ofbo.tex, -tx, 0, 640.f + tx * 2, 480.f, 0, 1, 1, 0);
  
  return true;
}

void free_output_framebuffer() {
  if (ctx.ofbo.fbo != 0) {
    glDeleteFramebuffers(1, &ctx.ofbo.fbo);
    ctx.ofbo.fbo = 0;
    glDeleteRenderbuffers(1, &ctx.ofbo.depthb);
    ctx.ofbo.depthb = 0;
    if (ctx.ofbo.tex != 0) {
      cache.DeleteTextures(1, &ctx.ofbo.tex);
      ctx.ofbo.tex = 0;
    }
    if (ctx.ofbo.colorb != 0) {
      glDeleteRenderbuffers(1, &ctx.ofbo.colorb);
      ctx.ofbo.colorb = 0;
    }
  }
}

void trace() {
  printf("SS: %dx%d\n", screen_width, screen_height);
  printf("SCI: %d, %f\n", pvrrc.fb_X_CLIP.max, dc2s_scale_h);
  printf("SCI: %f, %f, %f, %f\n",
         offs_x + pvrrc.fb_X_CLIP.min / scale_x,
         (pvrrc.fb_Y_CLIP.min / scale_y)*dc2s_scale_h,
         (pvrrc.fb_X_CLIP.max - pvrrc.fb_X_CLIP.min + 1) / scale_x * dc2s_scale_h,
         (pvrrc.fb_Y_CLIP.max - pvrrc.fb_Y_CLIP.min + 1) / scale_y * dc2s_scale_h
        );
}

struct VertexShader : GL::VertexShader {
  bool pp_Gouraud;
  bool ROTATE_90;
  /* Vertex constants (uniform)*/
  vec4 scale;
  vec4 depth_scale;
  f32  extra_depth_scale;
  /* Vertex input (attribute) */
  vec4 in_pos;
  vec4 in_base;
  vec4 in_offs;
  vec2 in_uv;
  /* output (varying) */
  vec4 vtx_base;
  vec4 vtx_offs;
  vec2 vtx_uv;
  void main() override {
    vtx_base = in_base;
    vtx_offs = in_offs;
    vtx_uv = in_uv;
    vec4 vpos = in_pos;
    if (vpos.z < 0.0 || vpos.z > 3.4e37) {
      gl_Position = vec4(0.0, 0.0, 1.0, 1.0 / vpos.z);
      return;
    }
    
    vpos.w = extra_depth_scale / vpos.z;
    vpos.z = depth_scale.x + depth_scale.y * vpos.w;
    if (ROTATE_90) {
      vpos.xy = vec2(vpos.y, -vpos.x);
    }
    vpos.xy = vpos.xy * scale.xy - scale.zw;
    vpos.xy *= vpos.w;
    gl_Position = vpos;
  }
};

struct FragmentShader : GL::FragmentShader {
  u32 cp_AlphaTest;
  u32 pp_ClipTestMode;
  u32 pp_UseAlpha;
  u32 pp_Texture;
  u32 pp_IgnoreTexA;
  u32 pp_ShadInstr;
  u32 pp_Offset;
  u32 pp_FogCtrl;
  u32 pp_Gouraud;
  u32 pp_BumpMap;
  u32 FogClamping;
  u32 pp_TriLinear;
  
  /* Shader parameters */
  f32 cp_AlphaTestValue;
  vec4 pp_ClipTest;
  vec3 sp_FOG_COL_RAM, sp_FOG_COL_VERT;
  f32 sp_FOG_DENSITY;
  sampler2D tex, fog_table;
  f32 trilinear_alpha;
  vec4 fog_clamp_min;
  vec4 fog_clamp_max;
  f32 extra_depth_scale;
  
  /* Vertex input (varying) */
  vec4 vtx_base;
  vec4 vtx_offs;
  vec2 vtx_uv;
  
  f32 fog_mode2(f32 w) {
    f32 z = clamp(w * extra_depth_scale * sp_FOG_DENSITY, 1.0, 255.9999);
    f32 exp = floor(log2(z));
    f32 m = z * 16.0 / pow(2.0, exp) - 16.0;
    f32 idx = floor(m) + exp * 16.0 + 0.5;
    vec4 fog_coef = texture2D(fog_table, vec2(idx / 128.0, 0.75 - (m - floor(m)) / 2.0));
    return fog_coef.a;
  }
  
  vec4 fog_clamp(vec4 col) {
    if (FogClamping == 1)
      return clamp(col, fog_clamp_min, fog_clamp_max);
    return col;
  }
  
  bool main() {
    /* Clip outside the box */
    if (pp_ClipTestMode == 1) {
      if (gl_FragCoord.x < pp_ClipTest.x || gl_FragCoord.x > pp_ClipTest.z
          || gl_FragCoord.y < pp_ClipTest.y || gl_FragCoord.y > pp_ClipTest.w) {
        return false;
      }
    }
    
    /* Clip inside the box */
    if (pp_ClipTestMode == -1) {
      if (gl_FragCoord.x >= pp_ClipTest.x && gl_FragCoord.x <= pp_ClipTest.z
          && gl_FragCoord.y >= pp_ClipTest.y && gl_FragCoord.y <= pp_ClipTest.w)
        return false;
    }
    
    vec4 color = vtx_base;
    if (pp_UseAlpha == 0) {
      color.a = 1.0;
    }
    
    if (pp_FogCtrl == 3) {
      color = vec4(sp_FOG_COL_RAM.rgb, fog_mode2(gl_FragCoord.w));
    }
    
    if (pp_Texture == 1) {
      vec4 texcol = texture2D(tex, vtx_uv);
      if (pp_BumpMap == 1) {
        float s = PI / 2.0 * (texcol.a * 15.0 * 16.0 + texcol.r * 15.0) / 255.0;
        float r = 2.0 * PI * (texcol.g * 15.0 * 16.0 + texcol.b * 15.0) / 255.0;
        texcol.a = clamp(vtx_offs.a + vtx_offs.r * sin(s) + vtx_offs.g * cos(s) * cos(r - 2.0 * PI * vtx_offs.b), 0.0, 1.0);
        texcol.rgb = vec3(1.0, 1.0, 1.0);
      } else {
        if (pp_IgnoreTexA == 1) {
          texcol.a = 1.0;
        }
        if (cp_AlphaTest == 1) {
          if (cp_AlphaTestValue > texcol.a)
            return false;
        }
      }
      if (pp_ShadInstr == 0) {
        color = texcol;
      }
      if (pp_ShadInstr == 1) {
        color.rgb *= texcol.rgb;
        color.a = texcol.a;
      }
      if (pp_ShadInstr == 2) {
        color.rgb = mix(color.rgb, texcol.rgb, texcol.a);
      }
      if (pp_ShadInstr == 3) {
        color *= texcol;
      }
      if (pp_Offset == 1 && pp_BumpMap == 0) {
        color.rgb += vtx_offs.rgb;
      }
    }
    color = fog_clamp(color);
    if (pp_FogCtrl == 0) {
      color.rgb = mix(color.rgb, sp_FOG_COL_RAM.rgb, fog_mode2(gl_FragCoord.w));
    }
    if (pp_FogCtrl == 1 && pp_Offset == 1 && pp_BumpMap == 0) {
      color.rgb = mix(color.rgb, sp_FOG_COL_VERT.rgb, vtx_offs.a);
    }
    if (pp_TriLinear == 1) {
      color *= trilinear_alpha;
    }
    if (cp_AlphaTest == 1) {
      color.a = 1.0;
    }
    gl_FragColor = color;
  }
};
