#pragma once
#include "device.h"

class Context : public Device {
protected:
  void drawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (!mState.currentProgram) {
      return error(GL_INVALID_OPERATION);
    }
    
    if (!indices && !mState.elementArrayBuffer) {
      return error(GL_INVALID_OPERATION);
    }
    
    PrimitiveType primitiveType;
    int primitiveCount;
    
    if (!es2sw::ConvertPrimitiveType(mode, count, primitiveType, primitiveCount))
      return error(GL_INVALID_ENUM);
      
    if (primitiveCount <= 0) {
      return;
    }
    
    if (!applyRenderTarget()) {
      return;
    }
    
    applyState(mode);
    
    TranslatedIndexData indexInfo;
    GLenum err = applyIndexBuffer(indices, count, mode, type, &indexInfo);
    if (err != GL_NO_ERROR) {
      return error(err);
    }
    
    GLsizei vertexCount = indexInfo.maxIndex - indexInfo.minIndex + 1;
    err = applyVertexBuffer(-(int)indexInfo.minIndex, indexInfo.minIndex, vertexCount);
    if (err != GL_NO_ERROR) {
      return error(err);
    }
    
    applyShaders();
    applyTextures();
    
    if (!getCurrentProgram()->validateSamplers(false)) {
      return error(GL_INVALID_OPERATION);
    }
    
    if (!cullSkipsDraw(mode)) {
      drawIndexedPrimitive(primitiveType, indexInfo.indexOffset, primitiveCount, IndexDataManager::typeSize(type));
    }
  }
  
public:
  void DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    if (count < 0) {
      return error(GL_INVALID_VALUE);
    }
    
    switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_UNSIGNED_SHORT:
    case GL_UNSIGNED_INT:
      break;
    default:
      return error(GL_INVALID_ENUM);
    }
    
    drawElements(mode, count, type, indices);
  }
};
