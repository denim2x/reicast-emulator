#pragma once
#include "device.h"

class Context : public Device {
protected:
  // Recorded errors
  bool mInvalidEnum;
  bool mInvalidValue;
  bool mInvalidOperation;
  bool mOutOfMemory;
  bool mInvalidFramebufferOperation;
  
  
public:
  enum Type {
    UNSIGNED_BYTE,
    UNSIGNED_SHORT,
    UNSIGNED_INT
  };
  
  void DrawElements(GLenum mode, GLsizei count, Type type, const void *indices) {
    if (count < 0) {
      return error(GL_INVALID_VALUE);
    }
    
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
  
// Records an error code
  void error(GLenum errorCode) {
    switch (errorCode) {
    case GL_INVALID_ENUM:
      mInvalidEnum = true;
      TRACE("\t! Error generated: invalid enum\n");
      break;
    case GL_INVALID_VALUE:
      mInvalidValue = true;
      TRACE("\t! Error generated: invalid value\n");
      break;
    case GL_INVALID_OPERATION:
      mInvalidOperation = true;
      TRACE("\t! Error generated: invalid operation\n");
      break;
    case GL_OUT_OF_MEMORY:
      mOutOfMemory = true;
      TRACE("\t! Error generated: out of memory\n");
      break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      mInvalidFramebufferOperation = true;
      TRACE("\t! Error generated: invalid framebuffer operation\n");
      break;
    default: UNREACHABLE();
    }
  }
  
  
};
