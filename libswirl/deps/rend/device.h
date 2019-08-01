#pragma once

class Device {
protected:
  void drawIndexedPrimitive(PrimitiveType type, unsigned int indexOffset, unsigned int primitiveCount, int indexSize) {
    if (!bindResources() || !primitiveCount) {
      return;
    }
    
    Context::DrawType drawType;
    
    if (indexSize == 4) {
      switch (type) {
      case DRAW_POINTLIST:     drawType = Context::DRAW_INDEXEDPOINTLIST32;     break;
      case DRAW_LINELIST:      drawType = Context::DRAW_INDEXEDLINELIST32;      break;
      case DRAW_LINESTRIP:     drawType = Context::DRAW_INDEXEDLINESTRIP32;     break;
      case DRAW_LINELOOP:      drawType = Context::DRAW_INDEXEDLINELOOP32;      break;
      case DRAW_TRIANGLELIST:  drawType = Context::DRAW_INDEXEDTRIANGLELIST32;  break;
      case DRAW_TRIANGLESTRIP: drawType = Context::DRAW_INDEXEDTRIANGLESTRIP32; break;
      case DRAW_TRIANGLEFAN:   drawType = Context::DRAW_INDEXEDTRIANGLEFAN32;   break;
      default: UNREACHABLE();
      }
    } else if (indexSize == 2) {
      switch (type) {
      case DRAW_POINTLIST:     drawType = Context::DRAW_INDEXEDPOINTLIST16;     break;
      case DRAW_LINELIST:      drawType = Context::DRAW_INDEXEDLINELIST16;      break;
      case DRAW_LINESTRIP:     drawType = Context::DRAW_INDEXEDLINESTRIP16;     break;
      case DRAW_LINELOOP:      drawType = Context::DRAW_INDEXEDLINELOOP16;      break;
      case DRAW_TRIANGLELIST:  drawType = Context::DRAW_INDEXEDTRIANGLELIST16;  break;
      case DRAW_TRIANGLESTRIP: drawType = Context::DRAW_INDEXEDTRIANGLESTRIP16; break;
      case DRAW_TRIANGLEFAN:   drawType = Context::DRAW_INDEXEDTRIANGLEFAN16;   break;
      default: UNREACHABLE();
      }
    } else if (indexSize == 1) {
      switch (type) {
      case DRAW_POINTLIST:     drawType = Context::DRAW_INDEXEDPOINTLIST8;     break;
      case DRAW_LINELIST:      drawType = Context::DRAW_INDEXEDLINELIST8;      break;
      case DRAW_LINESTRIP:     drawType = Context::DRAW_INDEXEDLINESTRIP8;     break;
      case DRAW_LINELOOP:      drawType = Context::DRAW_INDEXEDLINELOOP8;      break;
      case DRAW_TRIANGLELIST:  drawType = Context::DRAW_INDEXEDTRIANGLELIST8;  break;
      case DRAW_TRIANGLESTRIP: drawType = Context::DRAW_INDEXEDTRIANGLESTRIP8; break;
      case DRAW_TRIANGLEFAN:   drawType = Context::DRAW_INDEXEDTRIANGLEFAN8;   break;
      default: UNREACHABLE();
      }
    } else UNREACHABLE();
    
    draw(drawType, indexOffset, primitiveCount);
  }
};
