#include <math.h>
#include "rend/TexCache.h"
#include "cfg/cfg.h"
#include "rend/gui.h"
#include <algorithm>

#ifdef TARGET_PANDORA
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
int fbdev = -1;
#endif

/*
*Optimisation notes*
Keep stuff in packed ints
Keep data as small as possible
Don't depend on dynamic allocation, or any 'complex' feature
  as it is likely to be problematic/slow
Do we really want to enable striping joins?

*Design notes*
Follow same architecture as the GLES renderer for now
Render to texture, keep track of textures in memory
Direct flip to screen (no vlbank/fb emulation)
Render contexts
Free over time? manage ram usage here?
Limit max resource size? for psp 48k verts worked just fine

FB:
Pixel clip, mapping

SPG/VO:
mapping

TA:
Tile clip

*/

#include "oslib/oslib.h"
#include "input/gamepad.h"
#include "softrend.h"

Context ctx;
Cache cache;
GL::Context gl;

Elements _elements;

Vertex* vtx_sort_base;

float fb_scale_x, fb_scale_y;
float scale_x, scale_y;

int screen_width;
int screen_height;

static vector<SortTrigDrawParam>  pidx_sort;

static void cleanup() {
  glDeleteBuffers(1, &ctx.vbo.geometry);
  ctx.vbo.geometry = 0;
  glDeleteBuffers(1, &ctx.vbo.idxs);
  glDeleteBuffers(1, &ctx.vbo.idxs2);
  cache.DeleteTextures(1, &fbTextureId);
  fbTextureId = 0;
  free_osd_resources();
  free_output_framebuffer();
  
  os_gl_term();
}

bool create_resources() {
  if (ctx.vbo.geometry != 0)
    // Assume the resources have already been created
    return true;
    
  //create vbos
  glGenBuffers(1, &ctx.vbo.geometry);
  glGenBuffers(1, &ctx.vbo.idxs);
  glGenBuffers(1, &ctx.vbo.idxs2);
  
  load_osd_resources();
  
  gui_init();
  
  return true;
}

void SetCull(u32 CulliMode) {
  if (CullMode[CulliMode] == GL::NONE) {
    cache.Disable(GL::CULL_FACE);
  } else {
    cache.Enable(GL::CULL_FACE);
    cache.CullFace(CullMode[CulliMode]); //GL::FRONT/GL::BACK, ...
  }
}

void GenSorted(int first, int count) {
  u32 tess_gen = 0;
  
  pidx_sort.clear();
  
  if (pvrrc.verts.used() == 0 || count <= 1)
    return;
    
  Vertex* vtx_base = pvrrc.verts.head();
  u32* idx_base = pvrrc.idx.head();
  
  PolyParam* pp_base = &pvrrc.global_param_tr.head()[first];
  PolyParam* pp = pp_base;
  PolyParam* pp_end = pp + count;
  
  Vertex* vtx_arr = vtx_base + idx_base[pp->first];
  vtx_sort_base = vtx_base;
  
  static u32 vtx_cnt;
  
  int vtx_count = idx_base[pp_end[-1].first + pp_end[-1].count - 1] - idx_base[pp->first];
  if (vtx_count > vtx_cnt) {
    vtx_cnt = vtx_count;
  }
    
#if PRINT_SORT_STATS
  printf("TVTX: %d || %d\n", vtx_cnt, vtx_count);
#endif
  
  if (vtx_count <= 0)
    return;
    
  //make lists of all triangles, with their pid and vid
  static vector<IndexTrig> lst;
  
  lst.resize(vtx_count * 4);
  
  
  int pfsti = 0;
  
  while (pp != pp_end) {
    u32 ppid = (pp - pp_base);
    
    if (pp->count > 2) {
      u32* idx = idx_base + pp->first;
      
      Vertex* vtx = vtx_base + idx[0];
      Vertex* vtx_end = vtx_base + idx[pp->count - 1] - 1;
      u32 flip = 0;
      while (vtx != vtx_end) {
        Vertex* v0, * v1, * v2, * v3, * v4, * v5;
        
        if (flip) {
          v0 = &vtx[1];
          v1 = &vtx[0];
          v2 = &vtx[2];
        } else {
          v0 = &vtx[0];
          v1 = &vtx[1];
          v2 = &vtx[2];
        }

        fill_id(lst[pfsti].id, v0, v1, v2, vtx_base);
        lst[pfsti].pid = ppid ;
        lst[pfsti].z = minZ(vtx_base, lst[pfsti].id);
        pfsti++;
        
        flip ^= 1;
        
        vtx++;
      }
    }
    pp++;
  }
  
  u32 aused = pfsti;
  
  lst.resize(aused);
  
  //sort them
  std::stable_sort(lst.begin(), lst.end());
  
  //Merge pids/draw cmds if two different pids are actually equal
  if (true) {
    for (u32 k = 1; k < aused; k++) {
      if (lst[k].pid != lst[k - 1].pid) {
        if (&pp_base[lst[k].pid] == &pp_base[lst[k - 1].pid]) {
          lst[k].pid = lst[k - 1].pid;
        }
      }
    }
  }
  
  //re-assemble them into drawing commands
  static vector<u32> vidx_sort;
  
  vidx_sort.resize(aused * 3);
  
  int idx = -1;
  
  for (u32 i = 0; i < aused; i++) {
    int pid = lst[i].pid;
    u32* midx = lst[i].id;
    
    vidx_sort[i * 3 + 0] = midx[0];
    vidx_sort[i * 3 + 1] = midx[1];
    vidx_sort[i * 3 + 2] = midx[2];
    
    if (idx != pid) {
      SortTrigDrawParam stdp = { pp_base + pid, i * 3, 0 };
      
      if (idx != -1) {
        SortTrigDrawParam* last = &pidx_sort[pidx_sort.size() - 1];
        last->count = stdp.first - last->first;
      }
      
      pidx_sort.push_back(stdp);
      idx = pid;
    }
  }
  
  SortTrigDrawParam* stdp = &pidx_sort[pidx_sort.size() - 1];
  stdp->count = aused * 3 - stdp->first;
  
#if PRINT_SORT_STATS
  printf("Reassembled into %d from %d\n", pidx_sort.size(), pp_end - pp_base);
#endif
  
  if (pidx_sort.size()) {
    //Bind and upload sorted index buffer
    engine.bindElementArrayBuffer(ctx.vbo.idxs2);
    if (ctx.index_type == GL::UNSIGNED_SHORT) {
      static bool overrun;
      static List<u16> short_vidx;
      if (short_vidx.daty != NULL)
        short_vidx.Free();
      short_vidx.Init(vidx_sort.size(), &overrun, NULL);
      for (int i = 0; i < vidx_sort.size(); i++) {
        *(short_vidx.Append()) = vidx_sort[i];
      }
      glBufferData(GL::ELEMENT_ARRAY_BUFFER, short_vidx.bytes(), short_vidx.head(), GL::STREAM_DRAW);
    } else {
      glBufferData(GL::ELEMENT_ARRAY_BUFFER, vidx_sort.size() * sizeof(u32), &vidx_sort[0], GL::STREAM_DRAW);
    }
    
    if (tess_gen) printf("Generated %.2fK Triangles !\n", tess_gen / 1000.0);
  }
}

void DrawSorted(bool multipass) {
  //if any drawing commands, draw them
  if (pidx_sort.size()) {
    u32 count = pidx_sort.size();
    
    {
      //set some 'global' modes for all primitives
      
      cache.Enable(GL::STENCIL_TEST);
      cache.StencilFunc(GL::ALWAYS, 0, 0);
      cache.StencilOp(GL::KEEP, GL::KEEP, GL::REPLACE);
      
      for (u32 p = 0; p < count; p++) {
        PolyParam* params = pidx_sort[p].ppid;
        if (pidx_sort[p].count > 2) { //this actually happens for some games. No idea why ..
          SetGPState<ListType_Translucent, true>(params);
          glDrawElements(GL::TRIANGLES, pidx_sort[p].count, ctx.index_type,
                         (GLvoid*)(ctx.get_index_size() * pidx_sort[p].first));
        }
        params++;
      }
      
      if (multipass && settings.rend.TranslucentPolygonDepthMask) {
        // Write to the depth buffer now. The next render pass might need it. (Cosmic Smash)
        glColorMask(GL::FALSE, GL::FALSE, GL::FALSE, GL::FALSE);
        cache.Disable(GL::BLEND);
        
        cache.StencilMask(0);
        
        cache.DepthFunc(GL::GEQUAL);
        cache.DepthMask(GL::TRUE);
        
        for (u32 p = 0; p < count; p++) {
          PolyParam* params = pidx_sort[p].ppid;
          if (pidx_sort[p].count > 2 && !params->isp.ZWriteDis) {
            SetCull(params->isp.CullMode ^ gcflip);
            
            glDrawElements(GL::TRIANGLES, pidx_sort[p].count, ctx.index_type,
                           (GLvoid*)(ctx.get_index_size() * pidx_sort[p].first));
          }
        }
        cache.StencilMask(0xFF);
        glColorMask(GL::TRUE, GL::TRUE, GL::TRUE, GL::TRUE);
      }
    }
    // Re-bind the previous index buffer for subsequent render passes
    glBindBuffer(GL::ELEMENT_ARRAY_BUFFER, ctx.vbo.idxs);
  }
}

template <u32 Type, bool SortingEnabled>
__forceinline
void SetGPState(const PolyParam* gp, u32 cflip = 0) {
  const u32 stencil = (gp->pcw.Shadow != 0) ? 0x80 : 0x0;
  
  cache.StencilFunc(GL::ALWAYS, stencil, stencil);
  
  cache.BindTexture(GL::TEXTURE_2D, gp->texid == -1 ? 0 : gp->texid);
  
  SetTextureRepeatMode(GL::TEXTURE_WRAP_S, gp->tsp.ClampU, gp->tsp.FlipU);
  SetTextureRepeatMode(GL::TEXTURE_WRAP_T, gp->tsp.ClampV, gp->tsp.FlipV);
  
  //set texture filter mode
  if (gp->tsp.FilterMode == 0) {
    //disable filtering, mipmaps
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, GL::NEAREST);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::NEAREST);
  } else {
    //bilinear filtering
    //PowerVR supports also trilinear via two passes, but we ignore that for now
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, (gp->tcw.MipMapped && settings.rend.UseMipmaps) ? GL::LINEAR_MIPMAP_NEAREST : GL::LINEAR);
    cache.TexParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::LINEAR);
  }
  
  if (Type == Translucent) {
    cache.Enable(GL::BLEND);
    cache.BlendFunc(SrcBlendGL[gp->tsp.SrcInstr], DstBlendGL[gp->tsp.DstInstr]);
  } else {
    cache.Disable(GL::BLEND);
  }
  
  //set cull mode !
  //cflip is required when exploding triangles for triangle sorting
  //gcflip is global clip flip, needed for when rendering to texture due to mirrored Y direction
  SetCull(gp->isp.CullMode ^ cflip ^ gcflip);
  
  //set Z mode, only if required
  if (Type == Punch_Through || (Type == Translucent && SortingEnabled)) {
    cache.DepthFunc(GL::GEQUAL);
  } else {
    cache.DepthFunc(Zfunction[gp->isp.DepthMode]);
  }
  
#if TRIG_SORT
  if (SortingEnabled) {
    cache.DepthMask(GL::FALSE);
  } else
#endif
    cache.DepthMask(!gp->isp.ZWriteDis);
}

template <ListType Type, bool SortingEnabled>
void DrawList(const Elements& elements, const List<PolyParam>& gply, int first, int count) {
//void DrawList(const List<PolyParam>& gply, int first, int count) {
  PolyParam* params = &gply.head()[first];
  
  
  if (count == 0)
    return;
  //we want at least 1 PParam
  
  
  //set some 'global' modes for all primitives
  
  gl.Enable(GL::STENCIL_TEST);
  gl.StencilFunc(GL::ALWAYS, 0, 0);
  gl.StencilOp(GL::KEEP, GL::KEEP, GL::REPLACE);

  while (count-- > 0) {
    if (params->count > 2) { //this actually happens for some games. No idea why ..
      SetGPState<Type, SortingEnabled>(params);
      elements.Draw(gl, GL::TRIANGLE_STRIP, params->count, (u32*)(sizeof(u32) * params->first));
    }
    
    params++;
  }
}

Elements& SetupMainVBO() {
  return _elements;
      engine.bindArrayBuffer(ctx.vbo.geometry);
      engine.bindElementArrayBuffer(ctx.vbo.idxs);
      
      glBufferData(GL::ARRAY_BUFFER, pvrrc.verts.bytes(), pvrrc.verts.head(), GL::STREAM_DRAW);
      

  //engine.bindArrayBuffer(ctx.vbo.geometry);
  //engine.bindElementArrayBuffer(ctx.vbo.idxs);
  
  //setup vertex buffers attrib pointers
  engine.setVertexAttribPointer(VERTEX_POS_ARRAY, 3, GL::FLOAT, GL::FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
  engine.setVertexAttribPointer(VERTEX_COL_BASE_ARRAY, 4, GL::UNSIGNED_BYTE, GL::TRUE, sizeof(Vertex), (void*)offsetof(Vertex, col));
  engine.setVertexAttribPointer(VERTEX_COL_OFFS_ARRAY, 4, GL::UNSIGNED_BYTE, GL::TRUE, sizeof(Vertex), (void*)offsetof(Vertex, spc));
  engine.setVertexAttribPointer(VERTEX_UV_ARRAY, 2, GL::FLOAT, GL::FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));


}

void DrawStrips() {
  let elements = SetupMainVBO();
  //Draw the strips !
  
  //We use sampler 0
  engine.setActiveTexture(GL::TEXTURE0);
  
  RenderPass previous_pass = {0};
  for (int render_pass = 0; render_pass < pvrrc.render_passes.used(); render_pass++) {
    const RenderPass& current_pass = pvrrc.render_passes.head()[render_pass];
    
    //initial state
    gl.Enable(GL::DEPTH_TEST);
    cache.DepthMask(GL::TRUE);
    
    //Opaque
    DrawList<Opaque, false>(elements, pvrrc.global_param_op, previous_pass.op_count, current_pass.op_count - previous_pass.op_count);
    
    //Alpha tested
    DrawList<Punch_Through, false>(elements, pvrrc.global_param_pt, previous_pass.pt_count, current_pass.pt_count - previous_pass.pt_count);
    
    //Alpha blended
    if (current_pass.autosort) {
#if TRIG_SORT
      GenSorted(previous_pass.tr_count, current_pass.tr_count - previous_pass.tr_count);
      DrawSorted(render_pass < pvrrc.render_passes.used() - 1);
#else
      SortPParams(previous_pass.tr_count, current_pass.tr_count - previous_pass.tr_count);
      DrawList<Translucent, true>(elements, pvrrc.global_param_tr, previous_pass.tr_count, current_pass.tr_count - previous_pass.tr_count);
#endif
    } else {
      DrawList<Translucent, false>(elements, pvrrc.global_param_tr, previous_pass.tr_count, current_pass.tr_count - previous_pass.tr_count);
    }
    previous_pass = current_pass;
  }
}

bool setup() {
  if (!os_gl_init((void*)libPvr_GetRenderTarget(),
                  (void*)libPvr_GetRenderSurface()))
    return false;
    
  cache.Enable();
  
  if (!create_resources())
    return false;
    
  //clean up the buffer
  cache.color.Clear(0.f, 0.f, 0.f, 0.f);
  glClear(GL::COLOR_BUFFER_BIT);
  os_gl_swap();
  
  engine.setGenerateMipmapHint(GL::FASTEST);
  if (settings.rend.TextureUpscale > 1) {
    // Trick to preload the tables used by xBRZ
    u32 src[] { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    u32 dst[16];
    UpscalexBRZ(2, src, dst, 2, 2, false);
  }
  
  return true;
}

static void upload_vertex_indices() {
  if (ctx.index_type == GL::UNSIGNED_SHORT) {
    static bool overrun;
    static List<u16> short_idx;
    if (short_idx.daty != NULL) {
      short_idx.Free();
    }
    short_idx.Init(pvrrc.idx.used(), &overrun, NULL);
    for (u32 *p = pvrrc.idx.head(); p < pvrrc.idx.LastPtr(0); p++) {
      *(short_idx.Append()) = *p;
    }
    glBufferData(GL::ELEMENT_ARRAY_BUFFER, short_idx.bytes(), short_idx.head(), GL::STREAM_DRAW);
  } else {
    glBufferData(GL::ELEMENT_ARRAY_BUFFER, pvrrc.idx.bytes(), pvrrc.idx.head(), GL::STREAM_DRAW);
  }
 
}

static void DrawQuad(GLuint texId, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
  struct Vertex vertices[] = {
    { x,     y + h, 1, { 255, 255, 255, 255 }, { 0, 0, 0, 0 }, u0, v1 },
    { x,     y,     1, { 255, 255, 255, 255 }, { 0, 0, 0, 0 }, u0, v0 },
    { x + w, y + h, 1, { 255, 255, 255, 255 }, { 0, 0, 0, 0 }, u1, v1 },
    { x + w, y,     1, { 255, 255, 255, 255 }, { 0, 0, 0, 0 }, u1, v0 },
  };
  unsigned short indices[] = { 0, 1, 2, 1, 3 };
  
  cache.Disable(GL::SCISSOR_TEST);
  cache.Disable(GL::DEPTH_TEST);
  cache.Disable(GL::STENCIL_TEST);
  cache.Disable(GL::CULL_FACE);
  cache.Disable(GL::BLEND);
  
  PipelineShader *shader = GetProgram(0, 1, 1, 0, 1, 0, 0, 2, false, false, false, false);
  cache.UseProgram(shader->program);
  
  engine.setActiveTexture(GL::TEXTURE0);
  cache.BindTexture(GL::TEXTURE_2D, texId);
  
  let elements = SetupMainVBO();
  glBufferData(GL::ARRAY_BUFFER, sizeof(vertices), vertices, GL::STREAM_DRAW);
  glBufferData(GL::ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL::STREAM_DRAW);
  
  elements.Draw(gl, GL::TRIANGLE_STRIP, 5, (u16*)NULL);
}

void DrawFramebuffer(float w, float h) {
  DrawQuad(fbTextureId, 0, 0, MAX_RENDER_WIDTH, MAX_RENDER_HEIGHT, 0, 0, 1, 1);
  cache.DeleteTextures(1, &fbTextureId);
  fbTextureId = 0;
}

void killtex() {
  for (TexCacheIter i = TexCache.begin(); i != TexCache.end(); i++) {
    i->second.Delete();
  }
  
  TexCache.clear();
  printf("Texture cache cleared\n");
}

struct softrend : Renderer {
  inline bool Init() {
    return setup();
  }
  
  void Resize(int w, int h) {
    screen_width = w;
    screen_height = h;
  }
  void SetFBScale(float x, float y) {
    fb_scale_x = x;
    fb_scale_y = y;
  }
  
  void Term() {
    if (KillTex)
      killtex();
    cleanup();
  }
  
  bool Process(TA_context* ctx) {
    //disable RTTs for now ..
    if (ctx->rend.isRTT)
      return false;
      
    ctx->rend_inuse.Lock();
    
    if (KillTex)
      killtex();
      
    if (ctx->rend.isRenderFramebuffer) {
      RenderFramebuffer();
      ctx->rend_inuse.Unlock();
    } else {
      if (!ta_parse_vdrc(ctx))
        return false;
    }
    CollectCleanup();
    
    if (ctx->rend.Overrun) {
      printf("ERROR: TA gl overrun\n");
    }
    
    return !ctx->rend.Overrun;
  }
  bool Render() {
    DoCleanup();
    
    //float B=2/(min_invW-max_invW);
    //float A=-B*max_invW+vnear;
    
    //these should be adjusted based on the current PVR scaling etc params
    float dc_width = MAX_RENDER_WIDTH;
    float dc_height = MAX_RENDER_HEIGHT;
    
    gcflip = 0;
    
    scale_x = 1;
    scale_y = 1;
    
    float scissoring_scale_x = 1;
    
    if (!pvrrc.isRenderFramebuffer) {
      scale_x = fb_scale_x;
      scale_y = fb_scale_y;
      if (SCALER_CTL.interlace == 0 && SCALER_CTL.vscalefactor >= 0x400)
        scale_y *= SCALER_CTL.vscalefactor / 0x400;
        
      //work out scaling parameters !
      //Pixel doubling is on VO, so it does not affect any pixel operations
      //A second scaling is used here for scissoring
      if (VO_CONTROL.pixel_double) {
        scissoring_scale_x = 0.5f;
        scale_x *= 0.5f;
      }
      
      if (SCALER_CTL.hscale) {
        scissoring_scale_x /= 2;
        scale_x *= 2;
      }
    }
    
    dc_width  *= scale_x;
    dc_height *= scale_y;
    
    /*
      Handle Dc to screen scaling
    */
    float screen_stretching = settings.rend.ScreenStretching / 100.f;
    float screen_scaling = settings.rend.ScreenScaling / 100.f;
    
    float dc2s_scale_h;
    float ds2s_offs_x;
    
    //setup render target first
    if (settings.rend.ScreenScaling != 100 || ctx.swap_buffer_not_preserved) {
      init_output_framebuffer(screen_width * screen_scaling + 0.5f, screen_height * screen_scaling + 0.5f);
    } else {
#if HOST_OS != OS_DARWIN
      //Fix this in a proper way
      glBindFramebuffer(GL::FRAMEBUFFER, 0);
#endif
      glViewport(0, 0, screen_width, screen_height);
    }
    
    bool wide_screen_on = settings.rend.WideScreen
                          && pvrrc.fb_X_CLIP.min == 0
                          && (pvrrc.fb_X_CLIP.max + 1) / scale_x == MAX_RENDER_WIDTH
                          && pvrrc.fb_Y_CLIP.min == 0
                          && (pvrrc.fb_Y_CLIP.max + 1) / scale_y == MAX_RENDER_HEIGHT;
                          
    //Color is cleared by the background plane
    
    cache.Disable(GL::SCISSOR_TEST);
    
    cache.DepthMask(GL::TRUE);
    glClearDepthf(0.0);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL::STENCIL_BUFFER_BIT | GL::DEPTH_BUFFER_BIT);
    
    if (!pvrrc.isRenderFramebuffer) {
      _elements.clear();
      for (let vert : pvrrc.verts) {
        Element element;
        element.vertex = { vert.x, vert.y, vert.z };
        element.
        _elements
      }
      /*Main VBO
      engine.bindArrayBuffer(ctx.vbo.geometry);
      engine.bindElementArrayBuffer(ctx.vbo.idxs);
      
      glBufferData(GL::ARRAY_BUFFER, pvrrc.verts.bytes(), pvrrc.verts.head(), GL::STREAM_DRAW);
      upload_vertex_indices();
      */
      
      //not all scaling affects pixel operations, scale to adjust for that
      scale_x *= scissoring_scale_x;
      
      //trace();
      
      if (!wide_screen_on) {
        float width = (pvrrc.fb_X_CLIP.max - pvrrc.fb_X_CLIP.min + 1) / scale_x;
        float height = (pvrrc.fb_Y_CLIP.max - pvrrc.fb_Y_CLIP.min + 1) / scale_y;
        float min_x = pvrrc.fb_X_CLIP.min / scale_x;
        float min_y = pvrrc.fb_Y_CLIP.min / scale_y;
        
        if (SCALER_CTL.interlace && SCALER_CTL.vscalefactor >= 0x400) {
          // Clipping is done after scaling/filtering so account for that if enabled
          height *= SCALER_CTL.vscalefactor / 0x400;
          min_y *= SCALER_CTL.vscalefactor / 0x400;
        }
        if (settings.rend.Rotate90) {
          float t = width;
          width = height;
          height = t;
          t = min_x;
          min_x = min_y;
          min_y = MAX_RENDER_WIDTH - t - height;
        }
        // Add x offset for aspect ratio > 4/3
        min_x = (min_x * dc2s_scale_h * screen_stretching + ds2s_offs_x) * screen_scaling;
        // Invert y coordinates when rendering to screen
        min_y = (screen_height - (min_y + height) * dc2s_scale_h) * screen_scaling;
        width *= dc2s_scale_h * screen_stretching * screen_scaling;
        height *= dc2s_scale_h * screen_scaling;
        
        if (ds2s_offs_x > 0) {
          float scaled_offs_x = ds2s_offs_x * screen_scaling;
          
          cache.color.Clear(0.f, 0.f, 0.f, 0.f);
          cache.Enable(GL::SCISSOR_TEST);
          glScissor(0, 0, scaled_offs_x + 0.5f, screen_height * screen_scaling + 0.5f);
          glClear(GL::COLOR_BUFFER_BIT);
          glScissor(screen_width * screen_scaling - scaled_offs_x + 0.5f, 0, scaled_offs_x + 1.f, screen_height * screen_scaling + 0.5f);
          glClear(GL::COLOR_BUFFER_BIT);
        }
        
        glScissor(min_x + 0.5f, min_y + 0.5f, width + 0.5f, height + 0.5f);
        cache.Enable(GL::SCISSOR_TEST);
      }
      
      //restore scale_x
      scale_x /= scissoring_scale_x;
      
      DrawStrips();
    } else {
      cache.color.Clear(0.f, 0.f, 0.f, 0.f);
      glClear(GL::COLOR_BUFFER_BIT);
      DrawFramebuffer(dc_width, dc_height);
      glBufferData(GL::ARRAY_BUFFER, pvrrc.verts.bytes(), pvrrc.verts.head(), GL::STREAM_DRAW);
      upload_vertex_indices();
    }
#if HOST_OS==OS_WINDOWS
    //Sleep(40); //to test MT stability
#endif
    
    KillTex = false;
    
    if (settings.rend.ScreenScaling != 100 || ctx.swap_buffer_not_preserved) {
      RenderLastFrame();
    }
    
    return true;
  }
  bool RenderLastFrame() {
    cache.Disable(GL::SCISSOR_TEST);
    if (ctx.ofbo.fbo == 0)
      return false;
    glBindFramebuffer(GL::READ_FRAMEBUFFER, ctx.ofbo.fbo);
    glBindFramebuffer(GL::DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, ctx.ofbo.width, ctx.ofbo.height,
                      0, 0, screen_width, screen_height,
                      GL::COLOR_BUFFER_BIT, GL::LINEAR);
    glBindFramebuffer(GL::FRAMEBUFFER, 0);
    return true;
  }
  
  void Present() {
    os_gl_swap();
    glViewport(0, 0, screen_width, screen_height);
  }
  
  void DrawOSD(bool clear_screen) {
    glBindBuffer(GL::ARRAY_BUFFER, ctx.vbo.geometry);
    
    glVertexAttribPointer(VERTEX_POS_ARRAY, 3, GL::FLOAT, GL::FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glVertexAttribPointer(VERTEX_COL_BASE_ARRAY, 4, GL::UNSIGNED_BYTE, GL::TRUE, sizeof(Vertex), (void*)offsetof(Vertex, col));
    glVertexAttribPointer(VERTEX_UV_ARRAY, 2, GL::FLOAT, GL::FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    
    OSD_DRAW(clear_screen);
  }
  
  virtual u32 GetTexture(TSP tsp, TCW tcw) {
    TexCacheLookups++;
    
    //lookup texture
    TextureCacheData* tf = getTextureCacheData(tsp, tcw);
    
    if (tf->texID == 0) {
      tf->Create(true);
    }
    
    //update if needed
    if (tf->NeedsUpdate())
      tf->Update();
    else {
      tf->CheckCustomTexture();
      TexCacheHits++;
    }
    
    //update state for opts/stuff
    tf->Lookups++;
    
    //return ctx texture
    return tf->texID;
  }
};

// [FIXME] Why here ?
#include "hw/pvr/Renderer_if.h"

static auto softrend = RegisterRendererBackend(rendererbackend_t{ "soft", "Software Renderer (Per Triangle Sort)", 1, []() { return (Renderer*) new softrend(); } });




