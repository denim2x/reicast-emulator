# Reicast Software renderer

## Hardware architecture

**HOLLY** can process the following types of polygon lists:
1. Opaque
   - non-textured polygon with no alpha blending, or
   - textured polygon with no alpha blending with all-opaque texels
2. Punch-Through
   - textured polygon with no alpha blending with all texels transparent  or opaque
3. Opaque Modifier Volume
   - used to create three-dimensional aspects (shadows etc.)
4. Translucent
   - textured and non-textured polygon with apha blending, or
   - textured polygon with no alpha blending with translucent texels
5. Translucent Modifier Volume

For each Tile, the polygon lists are drawn in order, starting with (1). The
ISP processes the exact number of Opaque polygons found in each Tile. Likewise, Punch-Through polygons are sorted by the ISP for each Tile, beginning with the front. This processing goes on until all pixels in the Tile have been drawn. Also, the ISP draws the product of the number of Translucent polygons found in the Tile, multiplied by the number of overlapping polygons (for Auto Sort). After each polygon drawing, the TSP computes textures and shading on the visible pixels.

*Note*
> Drawing translucent polygons can be more computationally intensive than drawing Opaque polygons.

### Triangle Setup
Calculates the polygon surface equations and the texture shading parameters

#### ISP (Image Synthesis Processor)
Depth-sorts triangles, in the absence of an external *Z-buffer*; works on 32 × 32 Tiles. Pixel processing for a Tile is parallelized.

Processed pixels are taken over by the *Span RLC*, performing (parallel) *Run Length Encoding* on 32 pixels, passing the results over to the *Span Sorter*. With this approach, *ISP* ↔ *TSP* data transfer is accelerated, obviating the need for buffering.

*Span RLC* architecture:
> ISP Core → Run Lenght Encoder → Span Sorter → TSP Core

With the *Span Sorter* in place, run length encoded spans are regrouped in the triangle sequence and then passed over to the *TSP* in one shot.

##### Components
- PE Array
- Depth Accumulation Buffer
- Span RLC
- Precalc Unit
- Span Sorter

##### Computation
Calculates the parameters A, B, and C for the surface equation Ax + By + C from the coordinates of three vertices based on the following adjoint matrix:
```
           x0 x1 x2
 (A, B, C)(y0 y1 y2) = (z0, z1, z2)
           1  1  1
```           
Solving this adjoint matrix yields the values of A, B, and C needed in order to describe the plane that passes through the three provided vertices; the result:
```
 (A, B, C) = (z0, z1, z2)(1/∆)Adj
```
Therefore:
        y1 - y2   x2 - x1   x1y2 - x2y1
 Adj = (y2 - y0   x0 - x2   x2y0 - x0y2)
        y0 - y1   x1 - x0   x0y1 - x1y0
 ∆ = x0(y1 - y2) + x1(y2 - y0) + x2(y0 - y1)
The resuting ∆ ('det') could be used to perform culling processing for very small polygons.

*Note*
> The x, y, and z values from above are all screen coordinates; they are equivalent to (fX, fY, fInvW). Within a homogenous coordinate system (the screen), the above values are all multiplied by 1/W.
Clock cycles: 14.

#### TSP (Texture and Shading Processor)
Computes texture and shading properties, using the Tile accumulation buffer. After drawing all Tiles, the accumulation buffer's data is transferred to texture memory. Parameter calculations make of a local 'param cache' that leverages certain consistencies amongst visible polygons.

A texture cache (64 × 64-bit) serves computations on normal texels or the VQ texture code book; another texture cache (64 × 64-bit) stores the VQ texture indices, totalling  128 × 64 bits. Perspective compensation is performed on all texture and shading elements, U, V, Alpha, R, G, B, and Fog.

##### Components
- Texture Cache
- Param Cache
- Iterator Array
- Micro Tile Accumulation Buffer
- Precalc Unit
- Pixel Processing Engine

##### Computation
Calculates the surface equations Px + Qy + R for shading and texture, respectively. The number of parameters actually calculated depends on whether the calculations are being made in texture mode or shading mode.

Output parameter computations make use of a local cache.

Clock cycles: 48 to 70.
