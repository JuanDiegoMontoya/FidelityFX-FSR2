// This file is part of the FidelityFX SDK.
//
// Copyright (c) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "../ffx_fsr2.h"
#include "ffx_fsr2_gl.h"
#include "glad/gl.h"
#include "shaders/ffx_fsr2_shaders_gl.h"  // include all the precompiled VK shaders for the FSR2 passes
#include "../ffx_fsr2_private.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <codecvt>

// prototypes for functions in the interface
FfxErrorCode GetDeviceCapabilitiesGL(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* deviceCapabilities, FfxDevice device);
FfxErrorCode CreateBackendContextGL(FfxFsr2Interface* backendInterface, FfxDevice device);
FfxErrorCode DestroyBackendContextGL(FfxFsr2Interface* backendInterface);
FfxErrorCode CreateResourceGL(FfxFsr2Interface* backendInterface, const FfxCreateResourceDescription* desc, FfxResourceInternal* outResource);
FfxErrorCode RegisterResourceGL(FfxFsr2Interface* backendInterface, const FfxResource* inResource, FfxResourceInternal* outResourceInternal);
FfxErrorCode UnregisterResourcesGL(FfxFsr2Interface* backendInterface);
FfxResourceDescription GetResourceDescriptorGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
FfxErrorCode DestroyResourceGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
FfxErrorCode CreatePipelineGL(FfxFsr2Interface* backendInterface, FfxFsr2Pass passId, const FfxPipelineDescription* desc, FfxPipelineState* outPass);
FfxErrorCode DestroyPipelineGL(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline);
FfxErrorCode ScheduleGpuJobGL(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job);
FfxErrorCode ExecuteGpuJobsGL(FfxFsr2Interface* backendInterface, FfxCommandList commandList);

#define FSR2_MAX_QUEUED_FRAMES              ( 4)
#define FSR2_MAX_RESOURCE_COUNT             (64)
#define FSR2_MAX_STAGING_RESOURCE_COUNT     ( 8)
#define FSR2_MAX_BARRIERS                   (16)
#define FSR2_MAX_GPU_JOBS                   (32)
#define FSR2_MAX_IMAGE_COPY_MIPS            (32)
#define FSR2_MAX_SAMPLERS                   ( 2)
#define FSR2_MAX_UNIFORM_BUFFERS            ( 4)
#define FSR2_MAX_IMAGE_VIEWS                (32)
#define FSR2_MAX_BUFFERED_DESCRIPTORS       (FFX_FSR2_PASS_COUNT * FSR2_MAX_QUEUED_FRAMES)
#define FSR2_UBO_RING_BUFFER_SIZE           (FSR2_MAX_BUFFERED_DESCRIPTORS * FSR2_MAX_UNIFORM_BUFFERS)
#define FSR2_UBO_MEMORY_BLOCK_SIZE          (FSR2_UBO_RING_BUFFER_SIZE * 256)

namespace GL
{
  struct Texture { GLuint id; };
  struct Buffer { GLuint id; };
  struct Sampler { GLuint id; };
}

struct BackendContext_GL {

  enum class Aspect { UNDEFINED, COLOR, DEPTH };

  // store for resources and resourceViews
  struct Resource
  {
#ifdef _DEBUG
    char                    resourceName[64] = {};
#endif
    FfxResourceDescription  resourceDescription;
    FfxResourceStates       state;
    //ResourceVariant resource;

    GL::Buffer buffer = {};

    GL::Texture textureAllMipsView = {};
    GL::Texture textureSingleMipViews[FSR2_MAX_IMAGE_VIEWS] = {};
    Aspect textureAspect = {};
  };

  struct UniformBuffer
  {
    GL::Buffer bufferResource;
    uint8_t* pData;
  };

  struct GLFunctionTable
  {
    ffx_glGetProcAddress glGetProcAddress = nullptr;
    PFNGLGETINTEGERVPROC glGetIntegerv = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
    PFNGLOBJECTLABELPROC glObjectLabel = nullptr;
    PFNGLCREATESAMPLERSPROC glCreateSamplers = nullptr;
    PFNGLSAMPLERPARAMETERIPROC glSamplerParameteri = nullptr;
    PFNGLSAMPLERPARAMETERFPROC glSamplerParameterf = nullptr;
    PFNGLCREATEBUFFERSPROC glCreateBuffers = nullptr;
    PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage = nullptr;
    PFNGLCREATETEXTURESPROC glCreateTextures = nullptr;
    PFNGLGENTEXTURESPROC glGenTextures = nullptr;
    PFNGLTEXTUREVIEWPROC glTextureView = nullptr;
    PFNGLTEXTURESTORAGE1DPROC glTextureStorage1D = nullptr;
    PFNGLTEXTURESTORAGE2DPROC glTextureStorage2D = nullptr;
    PFNGLTEXTURESTORAGE3DPROC glTextureStorage3D = nullptr;
    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLSHADERBINARYPROC glShaderBinary = nullptr;
    PFNGLSPECIALIZESHADERPROC glSpecializeShader = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
    PFNGLDELETETEXTURESPROC glDeleteTextures = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
    PFNGLDELETESAMPLERSPROC glDeleteSamplers = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader = nullptr;
    PFNGLMAPNAMEDBUFFERRANGEPROC glMapNamedBufferRange = nullptr;
    PFNGLUNMAPNAMEDBUFFERPROC glUnmapNamedBuffer = nullptr;
    PFNGLMEMORYBARRIERPROC glMemoryBarrier = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
    PFNGLBINDTEXTUREUNITPROC glBindTextureUnit = nullptr;
    PFNGLBINDSAMPLERPROC glBindSampler = nullptr;
    PFNGLBINDBUFFERRANGEPROC glBindBufferRange = nullptr;
    PFNGLBINDIMAGETEXTUREPROC glBindImageTexture = nullptr;
    PFNGLDISPATCHCOMPUTEPROC glDispatchCompute = nullptr;
    PFNGLCOPYNAMEDBUFFERSUBDATAPROC glCopyNamedBufferSubData = nullptr;
    PFNGLCOPYIMAGESUBDATAPROC glCopyImageSubData = nullptr;
    PFNGLTEXTURESUBIMAGE1DPROC glTextureSubImage1D = nullptr;
    PFNGLTEXTURESUBIMAGE2DPROC glTextureSubImage2D = nullptr;
    PFNGLTEXTURESUBIMAGE3DPROC glTextureSubImage3D = nullptr;
    PFNGLCLEARTEXIMAGEPROC glClearTexImage = nullptr;
  };

  GLFunctionTable         glFunctionTable = {};

  uint32_t                gpuJobCount = 0;
  FfxGpuJobDescription    gpuJobs[FSR2_MAX_GPU_JOBS] = {};

  uint32_t                nextStaticResource = 0;
  uint32_t                nextDynamicResource = 0;
  uint32_t                stagingResourceCount = 0;
  Resource                resources[FSR2_MAX_RESOURCE_COUNT] = {};
  FfxResourceInternal     stagingResources[FSR2_MAX_STAGING_RESOURCE_COUNT] = {};

  GL::Sampler             pointSampler = {};
  GL::Sampler             linearSampler = {};

  UniformBuffer           uboRingBuffer[FSR2_UBO_RING_BUFFER_SIZE] = {};
  uint32_t                uboRingBufferIndex = 0;
};

FFX_API size_t ffxFsr2GetScratchMemorySizeGL()
{
  return sizeof(BackendContext_GL);
}

FfxErrorCode ffxFsr2GetInterfaceGL(
  FfxFsr2Interface* outInterface,
  void* scratchBuffer,
  size_t scratchBufferSize,
  ffx_glGetProcAddress getProcAddress)
{
  FFX_RETURN_ON_ERROR(
    outInterface,
    FFX_ERROR_INVALID_POINTER);
  FFX_RETURN_ON_ERROR(
    scratchBuffer,
    FFX_ERROR_INVALID_POINTER);
  FFX_RETURN_ON_ERROR(
    scratchBufferSize >= ffxFsr2GetScratchMemorySizeGL(),
    FFX_ERROR_INSUFFICIENT_MEMORY);

  outInterface->fpGetDeviceCapabilities = GetDeviceCapabilitiesGL;
  outInterface->fpCreateBackendContext = CreateBackendContextGL;
  outInterface->fpDestroyBackendContext = DestroyBackendContextGL;
  outInterface->fpCreateResource = CreateResourceGL;
  outInterface->fpRegisterResource = RegisterResourceGL;
  outInterface->fpUnregisterResources = UnregisterResourcesGL;
  outInterface->fpGetResourceDescription = GetResourceDescriptorGL;
  outInterface->fpDestroyResource = DestroyResourceGL;
  outInterface->fpCreatePipeline = CreatePipelineGL;
  outInterface->fpDestroyPipeline = DestroyPipelineGL;
  outInterface->fpScheduleGpuJob = ScheduleGpuJobGL;
  outInterface->fpExecuteGpuJobs = ExecuteGpuJobsGL;
  outInterface->scratchBuffer = scratchBuffer;
  outInterface->scratchBufferSize = scratchBufferSize;

  BackendContext_GL* context = (BackendContext_GL*)scratchBuffer;

  context->glFunctionTable.glGetProcAddress = getProcAddress;

  return FFX_OK;
}

static void loadGLFunctions(BackendContext_GL* backendContext, ffx_glGetProcAddress getProcAddress)
{
  FFX_ASSERT(NULL != backendContext);

  backendContext->glFunctionTable.glObjectLabel = (PFNGLOBJECTLABELPROC)getProcAddress("glObjectLabel");
  backendContext->glFunctionTable.glGetIntegerv = (PFNGLGETINTEGERVPROC)getProcAddress("glGetIntegerv");
  backendContext->glFunctionTable.glGetShaderiv = (PFNGLGETSHADERIVPROC)getProcAddress("glGetShaderiv");
  backendContext->glFunctionTable.glGetProgramiv = (PFNGLGETPROGRAMIVPROC)getProcAddress("glGetProgramiv");
  backendContext->glFunctionTable.glCreateSamplers = (PFNGLCREATESAMPLERSPROC)getProcAddress("glCreateSamplers");
  backendContext->glFunctionTable.glSamplerParameteri = (PFNGLSAMPLERPARAMETERIPROC)getProcAddress("glSamplerParameteri");
  backendContext->glFunctionTable.glSamplerParameterf = (PFNGLSAMPLERPARAMETERFPROC)getProcAddress("glSamplerParameterf");
  backendContext->glFunctionTable.glCreateBuffers = (PFNGLCREATEBUFFERSPROC)getProcAddress("glCreateBuffers");
  backendContext->glFunctionTable.glNamedBufferStorage = (PFNGLNAMEDBUFFERSTORAGEPROC)getProcAddress("glNamedBufferStorage");
  backendContext->glFunctionTable.glCreateTextures = (PFNGLCREATETEXTURESPROC)getProcAddress("glCreateTextures");
  backendContext->glFunctionTable.glGenTextures = (PFNGLGENTEXTURESPROC)getProcAddress("glGenTextures");
  backendContext->glFunctionTable.glTextureView = (PFNGLTEXTUREVIEWPROC)getProcAddress("glTextureView");
  backendContext->glFunctionTable.glTextureStorage1D = (PFNGLTEXTURESTORAGE1DPROC)getProcAddress("glTextureStorage1D");
  backendContext->glFunctionTable.glTextureStorage2D = (PFNGLTEXTURESTORAGE2DPROC)getProcAddress("glTextureStorage2D");
  backendContext->glFunctionTable.glTextureStorage3D = (PFNGLTEXTURESTORAGE3DPROC)getProcAddress("glTextureStorage3D");
  backendContext->glFunctionTable.glCreateShader = (PFNGLCREATESHADERPROC)getProcAddress("glCreateShader");
  backendContext->glFunctionTable.glShaderBinary = (PFNGLSHADERBINARYPROC)getProcAddress("glShaderBinary");
  backendContext->glFunctionTable.glSpecializeShader = (PFNGLSPECIALIZESHADERPROC)getProcAddress("glSpecializeShader");
  backendContext->glFunctionTable.glCompileShader = (PFNGLCOMPILESHADERPROC)getProcAddress("glCompileShader");
  backendContext->glFunctionTable.glCreateProgram = (PFNGLCREATEPROGRAMPROC)getProcAddress("glCreateProgram");
  backendContext->glFunctionTable.glAttachShader = (PFNGLATTACHSHADERPROC)getProcAddress("glAttachShader");
  backendContext->glFunctionTable.glLinkProgram = (PFNGLLINKPROGRAMPROC)getProcAddress("glLinkProgram");
  backendContext->glFunctionTable.glDeleteProgram = (PFNGLDELETEPROGRAMPROC)getProcAddress("glDeleteProgram");
  backendContext->glFunctionTable.glDeleteTextures = (PFNGLDELETETEXTURESPROC)getProcAddress("glDeleteTextures");
  backendContext->glFunctionTable.glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)getProcAddress("glDeleteBuffers");
  backendContext->glFunctionTable.glDeleteSamplers = (PFNGLDELETESAMPLERSPROC)getProcAddress("glDeleteSamplers");
  backendContext->glFunctionTable.glDeleteShader = (PFNGLDELETESHADERPROC)getProcAddress("glDeleteShader");
  backendContext->glFunctionTable.glMapNamedBufferRange = (PFNGLMAPNAMEDBUFFERRANGEPROC)getProcAddress("glMapNamedBufferRange");
  backendContext->glFunctionTable.glUnmapNamedBuffer = (PFNGLUNMAPNAMEDBUFFERPROC)getProcAddress("glUnmapNamedBuffer");
  backendContext->glFunctionTable.glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)getProcAddress("glMemoryBarrier");
  backendContext->glFunctionTable.glUseProgram = (PFNGLUSEPROGRAMPROC)getProcAddress("glUseProgram");
  backendContext->glFunctionTable.glBindTextureUnit = (PFNGLBINDTEXTUREUNITPROC)getProcAddress("glBindTextureUnit");
  backendContext->glFunctionTable.glBindSampler = (PFNGLBINDSAMPLERPROC)getProcAddress("glBindSampler");
  backendContext->glFunctionTable.glBindBufferRange = (PFNGLBINDBUFFERRANGEPROC)getProcAddress("glBindBufferRange");
  backendContext->glFunctionTable.glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)getProcAddress("glBindImageTexture");
  backendContext->glFunctionTable.glDispatchCompute = (PFNGLDISPATCHCOMPUTEPROC)getProcAddress("glDispatchCompute");
  backendContext->glFunctionTable.glCopyNamedBufferSubData = (PFNGLCOPYNAMEDBUFFERSUBDATAPROC)getProcAddress("glCopyNamedBufferSubData");
  backendContext->glFunctionTable.glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)getProcAddress("glCopyImageSubData");
  backendContext->glFunctionTable.glTextureSubImage1D = (PFNGLTEXTURESUBIMAGE1DPROC)getProcAddress("glTextureSubImage1D");
  backendContext->glFunctionTable.glTextureSubImage2D = (PFNGLTEXTURESUBIMAGE2DPROC)getProcAddress("glTextureSubImage2D");
  backendContext->glFunctionTable.glTextureSubImage3D = (PFNGLTEXTURESUBIMAGE3DPROC)getProcAddress("glTextureSubImage3D");
  backendContext->glFunctionTable.glClearTexImage = (PFNGLCLEARTEXIMAGEPROC)getProcAddress("glClearTexImage");
}

static GLenum getGLFormatFromSurfaceFormat(FfxSurfaceFormat fmt)
{
  switch (fmt) {
  case(FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS):
    return GL_RGBA32F;
  case(FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT):
    return GL_RGBA32F;
  case(FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT):
    return GL_RGBA16F;
  case(FFX_SURFACE_FORMAT_R16G16B16A16_UNORM):
    return GL_RGBA16;
  case(FFX_SURFACE_FORMAT_R32G32_FLOAT):
    return GL_RG32F;
  case(FFX_SURFACE_FORMAT_R32_UINT):
    return GL_R32UI;
  case(FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS):
    return GL_RGBA8;
  case(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM):
    return GL_RGBA8;
  case(FFX_SURFACE_FORMAT_R11G11B10_FLOAT):
    return GL_R11F_G11F_B10F;
  case(FFX_SURFACE_FORMAT_R16G16_FLOAT):
    return GL_RG16F;
  case(FFX_SURFACE_FORMAT_R16G16_UINT):
    return GL_RG16UI;
  case(FFX_SURFACE_FORMAT_R16_FLOAT):
    return GL_R16F;
  case(FFX_SURFACE_FORMAT_R16_UINT):
    return GL_R16UI;
  case(FFX_SURFACE_FORMAT_R16_UNORM):
    return GL_R16;
  case(FFX_SURFACE_FORMAT_R16_SNORM):
    return GL_R16_SNORM;
  case(FFX_SURFACE_FORMAT_R8_UNORM):
    return GL_R8;
  case(FFX_SURFACE_FORMAT_R8G8_UNORM):
    return GL_RG8;
  case(FFX_SURFACE_FORMAT_R32_FLOAT):
    return GL_R32F;
  case(FFX_SURFACE_FORMAT_R8_UINT):
    return GL_R8UI;
  default:
    FFX_ASSERT_FAIL("");
    return 0;
  }
}

static GLenum getGLUploadFormatFromSurfaceFormat(FfxSurfaceFormat fmt)
{
  switch (fmt)
  {
  case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
  case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16B16A16_UNORM:
  case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
  case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
    return GL_RGBA;
  case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
    return GL_RGB;
  case FFX_SURFACE_FORMAT_R32G32_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16_UINT:
  case FFX_SURFACE_FORMAT_R8G8_UNORM:
    return GL_RG;
  case FFX_SURFACE_FORMAT_R16_FLOAT:
  case FFX_SURFACE_FORMAT_R16_UNORM:
  case FFX_SURFACE_FORMAT_R16_SNORM:
  case FFX_SURFACE_FORMAT_R8_UNORM:
  case FFX_SURFACE_FORMAT_R32_FLOAT:
    return GL_RED;
  case FFX_SURFACE_FORMAT_R8_UINT:
  case FFX_SURFACE_FORMAT_R16_UINT:
  case FFX_SURFACE_FORMAT_R32_UINT:
    return GL_RED_INTEGER;
  default: FFX_ASSERT_FAIL(""); return 0;
  }
}

static GLenum getGLUploadTypeFromSurfaceFormat(FfxSurfaceFormat fmt)
{
  switch (fmt)
  {
  case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
  case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
  case FFX_SURFACE_FORMAT_R32G32_FLOAT:
  case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
  case FFX_SURFACE_FORMAT_R16G16_FLOAT:
  case FFX_SURFACE_FORMAT_R16_FLOAT:
  case FFX_SURFACE_FORMAT_R32_FLOAT:
    return GL_FLOAT;
  case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
  case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
  case FFX_SURFACE_FORMAT_R8G8_UNORM:
  case FFX_SURFACE_FORMAT_R8_UNORM:
    return GL_UNSIGNED_BYTE;
  case FFX_SURFACE_FORMAT_R32_UINT:
    return GL_UNSIGNED_INT;
  case FFX_SURFACE_FORMAT_R16G16B16A16_UNORM:
  case FFX_SURFACE_FORMAT_R16_UNORM:
  case FFX_SURFACE_FORMAT_R16G16_UINT:
  case FFX_SURFACE_FORMAT_R16_UINT:
  case FFX_SURFACE_FORMAT_R8_UINT:
    return GL_UNSIGNED_SHORT;
  case FFX_SURFACE_FORMAT_R16_SNORM:
    return GL_SHORT;
  default: FFX_ASSERT_FAIL(""); return 0;
  }
}

FfxSurfaceFormat ffxGetSurfaceFormatGL(GLenum fmt)
{
  switch (fmt) {
  case(GL_RGBA32F):
    return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
  case(GL_RGBA16F):
    return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
  case(GL_RGBA16):
    return FFX_SURFACE_FORMAT_R16G16B16A16_UNORM;
  case(GL_RG32F):
    return FFX_SURFACE_FORMAT_R32G32_FLOAT;
  case(GL_R32UI):
    return FFX_SURFACE_FORMAT_R32_UINT;
  case(GL_RGBA8):
    return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
  case(GL_R11F_G11F_B10F):
    return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
  case(GL_RG16F):
    return FFX_SURFACE_FORMAT_R16G16_FLOAT;
  case(GL_RG16UI):
    return FFX_SURFACE_FORMAT_R16G16_UINT;
  case(GL_R16F):
    return FFX_SURFACE_FORMAT_R16_FLOAT;
  case(GL_R16UI):
    return FFX_SURFACE_FORMAT_R16_UINT;
  case(GL_R16):
    return FFX_SURFACE_FORMAT_R16_UNORM;
  case(GL_R16_SNORM):
    return FFX_SURFACE_FORMAT_R16_SNORM;
  case(GL_R8):
    return FFX_SURFACE_FORMAT_R8_UNORM;
  case(GL_R32F):
    return FFX_SURFACE_FORMAT_R32_FLOAT;
  case(GL_R8UI):
    return FFX_SURFACE_FORMAT_R8_UINT;
  default:
    return FFX_SURFACE_FORMAT_UNKNOWN;
  }
}

static BackendContext_GL::UniformBuffer accquireDynamicUBO(BackendContext_GL* backendContext, uint32_t size, const void* pData)
{
  // the ubo ring buffer is pre-populated with VkBuffer objects of 256-bytes to prevent creating buffers at runtime
  FFX_ASSERT(size <= 256);

  BackendContext_GL::UniformBuffer& ubo = backendContext->uboRingBuffer[backendContext->uboRingBufferIndex];

  if (pData)
  {
    memcpy(ubo.pData, pData, size);
  }

  backendContext->uboRingBufferIndex++;

  if (backendContext->uboRingBufferIndex >= FSR2_UBO_RING_BUFFER_SIZE)
  {
    backendContext->uboRingBufferIndex = 0;
  }

  return ubo;
}

static uint32_t getDefaultSubgroupSize(const BackendContext_GL* backendContext)
{
  //VkPhysicalDeviceVulkan11Properties vulkan11Properties = {};
  //vulkan11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;

  //VkPhysicalDeviceProperties2 deviceProperties2 = {};
  //deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  //deviceProperties2.pNext = &vulkan11Properties;
  //vkGetPhysicalDeviceProperties2(backendContext->physicalDevice, &deviceProperties2);
  //FFX_ASSERT(vulkan11Properties.subgroupSize == 32 || vulkan11Properties.subgroupSize == 64); // current desktop market

  //return vulkan11Properties.subgroupSize;

// @todo IMPLEMENT WHEN SUBGROUP OPS ARE ENABLED
  return 32;
}

FfxResource ffxGetTextureResourceGL(FfxFsr2Context* context,
  GLuint imageGL,
  uint32_t width,
  uint32_t height,
  GLenum imgFormat,
  const wchar_t* name,
  FfxResourceStates state)
{
  FfxResource resource = {};
  resource.resource = reinterpret_cast<void*>(imageGL);
  resource.state = state;
  resource.descriptorData = 0;
  resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
  resource.description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
  resource.description.width = width;
  resource.description.height = height;
  resource.description.depth = 1;
  resource.description.mipCount = 1;
  resource.description.format = ffxGetSurfaceFormatGL(imgFormat);

  switch (imgFormat)
  {
  case GL_DEPTH_COMPONENT16:
  case GL_DEPTH_COMPONENT32F:
  case GL_DEPTH24_STENCIL8:
  case GL_DEPTH32F_STENCIL8:
  {
    resource.isDepth = true;
    break;
  }
  default:
  {
    resource.isDepth = false;
    break;
  }
  }

#ifdef _DEBUG
  if (name) {
    wcscpy_s(resource.name, name);
  }
#endif

  return resource;
}

FfxResource ffxGetBufferResourceGL(FfxFsr2Context* context, GLuint bufferGL, uint32_t size, const wchar_t* name, FfxResourceStates state)
{
  FfxResource resource = {};
  resource.resource = reinterpret_cast<void*>(bufferGL);
  resource.state = state;
  resource.descriptorData = 0;
  resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
  resource.description.type = FFX_RESOURCE_TYPE_BUFFER;
  resource.description.width = size;
  resource.description.height = 1;
  resource.description.depth = 1;
  resource.description.mipCount = 1;
  resource.description.format = FFX_SURFACE_FORMAT_UNKNOWN;
  resource.isDepth = false;

#ifdef _DEBUG
  if (name) {
    wcscpy_s(resource.name, name);
  }
#endif

  return resource;
}

GLuint ffxGetGLImage(FfxFsr2Context* context, uint32_t resId)
{
  FFX_ASSERT(NULL != context);

  FfxFsr2Context_Private* contextPrivate = (FfxFsr2Context_Private*)(context);
  BackendContext_GL* backendContext = (BackendContext_GL*)(contextPrivate->contextDescription.callbacks.scratchBuffer);

  int32_t internalIndex = contextPrivate->uavResources[resId].internalIndex;

  return (internalIndex == -1) ? 0 : backendContext->resources[internalIndex].textureAllMipsView.id;
}

FfxErrorCode RegisterResourceGL(
  FfxFsr2Interface* backendInterface,
  const FfxResource* inFfxResource,
  FfxResourceInternal* outFfxResourceInternal
)
{
  FFX_ASSERT(NULL != backendInterface);

  BackendContext_GL* backendContext = (BackendContext_GL*)(backendInterface->scratchBuffer);

  if (inFfxResource->resource == nullptr) {

    outFfxResourceInternal->internalIndex = FFX_FSR2_RESOURCE_IDENTIFIER_NULL;
    return FFX_OK;
  }

  FFX_ASSERT(backendContext->nextDynamicResource > backendContext->nextStaticResource);
  outFfxResourceInternal->internalIndex = backendContext->nextDynamicResource--;

  BackendContext_GL::Resource* backendResource = &backendContext->resources[outFfxResourceInternal->internalIndex];

  backendResource->resourceDescription = inFfxResource->description;
  backendResource->state = inFfxResource->state;

#ifdef _DEBUG
  size_t retval = 0;
  wcstombs_s(&retval, backendResource->resourceName, sizeof(backendResource->resourceName), inFfxResource->name, sizeof(backendResource->resourceName));
  if (retval >= 64) backendResource->resourceName[63] = '\0';
#endif

  if (inFfxResource->description.type == FFX_RESOURCE_TYPE_BUFFER)
  {
    const GLuint buffer = reinterpret_cast<GLuint>(inFfxResource->resource);

    backendResource->buffer = { buffer };
  }
  else
  {
    const GLuint texture = reinterpret_cast<GLuint>(inFfxResource->resource);

    backendResource->textureAllMipsView = { texture };
    backendResource->textureSingleMipViews[0] = { texture };

    if (texture) {
      if (inFfxResource->isDepth)
      {
        backendResource->textureAspect = BackendContext_GL::Aspect::DEPTH;
      }
      else
      {
        backendResource->textureAspect = BackendContext_GL::Aspect::COLOR;
      }

    }
  }

  return FFX_OK;
}

// dispose dynamic resources: This should be called at the end of the frame
FfxErrorCode UnregisterResourcesGL(FfxFsr2Interface* backendInterface)
{
  FFX_ASSERT(NULL != backendInterface);

  BackendContext_GL* backendContext = (BackendContext_GL*)(backendInterface->scratchBuffer);

  backendContext->nextDynamicResource = FSR2_MAX_RESOURCE_COUNT - 1;

  return FFX_OK;
}

FfxErrorCode GetDeviceCapabilitiesGL(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* deviceCapabilities, FfxDevice device)
{
  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  const uint32_t defaultSubgroupSize = getDefaultSubgroupSize(backendContext);

  // no shader model in vulkan so assume the minimum
  deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_5_1;
  deviceCapabilities->waveLaneCountMin = defaultSubgroupSize;
  deviceCapabilities->waveLaneCountMax = defaultSubgroupSize;
  deviceCapabilities->fp16Supported = false;
  deviceCapabilities->raytracingSupported = false;

  // check if extensions are enabled

// @todo CHECK EXTENSIONS
// GL_KHR_shader_subgroup
  //for (uint32_t i = 0; i < backendContext->numDeviceExtensions; i++)
  //{
  //    if (strcmp(backendContext->extensionProperties[i].extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0)
  //    {
  //        // check if we the max subgroup size allows us to use wave64
  //        VkPhysicalDeviceSubgroupSizeControlProperties subgroupSizeControlProperties = {};
  //        subgroupSizeControlProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;

  //        VkPhysicalDeviceProperties2 deviceProperties2 = {};
  //        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  //        deviceProperties2.pNext = &subgroupSizeControlProperties;
  //        vkGetPhysicalDeviceProperties2(backendContext->physicalDevice, &deviceProperties2);

  //        // NOTE: It's important to check requiredSubgroupSizeStages flags (and it's required by the spec).
  //        // As of August 2022, AMD's Vulkan drivers do not support subgroup size selection through Vulkan API
  //        // and this information is reported through requiredSubgroupSizeStages flags.
  //        if (subgroupSizeControlProperties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT)
  //        {
  //            deviceCapabilities->waveLaneCountMin = subgroupSizeControlProperties.minSubgroupSize;
  //            deviceCapabilities->waveLaneCountMax = subgroupSizeControlProperties.maxSubgroupSize;
  //        }
  //    }
  //    if (strcmp(backendContext->extensionProperties[i].extensionName, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) == 0)
  //    {
  //        // check for fp16 support
  //        VkPhysicalDeviceShaderFloat16Int8Features shaderFloat18Int8Features = {};
  //        shaderFloat18Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;

  //        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
  //        physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  //        physicalDeviceFeatures2.pNext = &shaderFloat18Int8Features;

  //        vkGetPhysicalDeviceFeatures2(backendContext->physicalDevice, &physicalDeviceFeatures2);

  //        deviceCapabilities->fp16Supported = (bool)shaderFloat18Int8Features.shaderFloat16;
  //    }
  //    if (strcmp(backendContext->extensionProperties[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
  //    {
  //        // check for ray tracing support 
  //        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
  //        accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

  //        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
  //        physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  //        physicalDeviceFeatures2.pNext = &accelerationStructureFeatures;

  //        vkGetPhysicalDeviceFeatures2(backendContext->physicalDevice, &physicalDeviceFeatures2);

  //        deviceCapabilities->raytracingSupported = (bool)accelerationStructureFeatures.accelerationStructure;
  //    }
  //}
  return FFX_OK;
}

FfxErrorCode CreateBackendContextGL(FfxFsr2Interface* backendInterface, FfxDevice device)
{
  FFX_ASSERT(NULL != backendInterface);

  // set up some internal resources we need (space for resource views and constant buffers)
  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  backendContext->nextStaticResource = 0;
  backendContext->nextDynamicResource = FSR2_MAX_RESOURCE_COUNT - 1;

  // load vulkan functions
  loadGLFunctions(backendContext, backendContext->glFunctionTable.glGetProcAddress);

  // enumerate all the device extensions 
  //backendContext->numDeviceExtensions = 0;
  //vkEnumerateDeviceExtensionProperties(backendContext->physicalDevice, nullptr, &backendContext->numDeviceExtensions, nullptr);
  //vkEnumerateDeviceExtensionProperties(backendContext->physicalDevice, nullptr, &backendContext->numDeviceExtensions, backendContext->extensionProperties);

  backendContext->glFunctionTable.glCreateSamplers(1, &backendContext->pointSampler.id);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->pointSampler.id, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->pointSampler.id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->pointSampler.id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->pointSampler.id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->pointSampler.id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->pointSampler.id, GL_TEXTURE_MIN_LOD, -1000);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->pointSampler.id, GL_TEXTURE_MAX_LOD, 1000);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->pointSampler.id, GL_TEXTURE_MAX_ANISOTROPY, 1);

  backendContext->glFunctionTable.glCreateSamplers(1, &backendContext->linearSampler.id);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->linearSampler.id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->linearSampler.id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->linearSampler.id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->linearSampler.id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameteri(backendContext->linearSampler.id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->linearSampler.id, GL_TEXTURE_MIN_LOD, -1000);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->linearSampler.id, GL_TEXTURE_MAX_LOD, 1000);
  backendContext->glFunctionTable.glSamplerParameterf(backendContext->linearSampler.id, GL_TEXTURE_MAX_ANISOTROPY, 1);

  // allocate ring buffer of uniform buffers
  for (uint32_t i = 0; i < FSR2_UBO_RING_BUFFER_SIZE; i++)
  {
    BackendContext_GL::UniformBuffer& ubo = backendContext->uboRingBuffer[i];
    backendContext->glFunctionTable.glCreateBuffers(1, &ubo.bufferResource.id);
    constexpr GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    backendContext->glFunctionTable.glNamedBufferStorage(ubo.bufferResource.id, 256, nullptr, mapFlags);

    // map the memory block 
    ubo.pData = (uint8_t*)backendContext->glFunctionTable.glMapNamedBufferRange(ubo.bufferResource.id, 0, 256, mapFlags);

    if (!ubo.pData)
    {
      return FFX_ERROR_BACKEND_API_ERROR;
    }
  }

  backendContext->gpuJobCount = 0;
  backendContext->stagingResourceCount = 0;
  backendContext->uboRingBufferIndex = 0;

  return FFX_OK;
}

FfxErrorCode DestroyBackendContextGL(FfxFsr2Interface* backendInterface)
{
  FFX_ASSERT(NULL != backendInterface);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  for (uint32_t i = 0; i < backendContext->stagingResourceCount; i++)
  {
    DestroyResourceGL(backendInterface, backendContext->stagingResources[i]);
  }

  for (uint32_t i = 0; i < FSR2_UBO_RING_BUFFER_SIZE; i++)
  {
    BackendContext_GL::UniformBuffer& ubo = backendContext->uboRingBuffer[i];

    // buffer is implicitly unmapped by deleting it
    backendContext->glFunctionTable.glDeleteBuffers(1, &ubo.bufferResource.id);

    ubo.bufferResource = {};
    ubo.pData = nullptr;
  }

  backendContext->glFunctionTable.glDeleteSamplers(1, &backendContext->pointSampler.id);
  backendContext->glFunctionTable.glDeleteSamplers(1, &backendContext->linearSampler.id);
  backendContext->pointSampler = {};
  backendContext->linearSampler = {};

  return FFX_OK;
}

// create a internal resource that will stay alive until effect gets shut down
FfxErrorCode CreateResourceGL(
  FfxFsr2Interface* backendInterface,
  const FfxCreateResourceDescription* createResourceDescription,
  FfxResourceInternal* outResource)
{
  FFX_ASSERT(NULL != backendInterface);
  FFX_ASSERT(NULL != createResourceDescription);
  FFX_ASSERT(NULL != outResource);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  FFX_ASSERT(backendContext->nextStaticResource + 1 < backendContext->nextDynamicResource);
  outResource->internalIndex = backendContext->nextStaticResource++;
  BackendContext_GL::Resource* res = &backendContext->resources[outResource->internalIndex];
  res->resourceDescription = createResourceDescription->resourceDescription;
  res->resourceDescription.mipCount = createResourceDescription->resourceDescription.mipCount;

  if (res->resourceDescription.mipCount == 0)
  {
    res->resourceDescription.mipCount = (uint32_t)(1 + floor(log2(FFX_MAXIMUM(FFX_MAXIMUM(createResourceDescription->resourceDescription.width, createResourceDescription->resourceDescription.height), createResourceDescription->resourceDescription.depth))));
  }

#ifdef _DEBUG
  size_t retval = 0;
  wcstombs_s(&retval, res->resourceName, sizeof(res->resourceName), createResourceDescription->name, sizeof(res->resourceName));
  if (retval >= 64) res->resourceName[63] = '\0';
#endif

  switch (createResourceDescription->resourceDescription.type)
  {
  case FFX_RESOURCE_TYPE_BUFFER:
  {
    if (createResourceDescription->initData)
    {
      FFX_ASSERT(createResourceDescription->resourceDescription.width == createResourceDescription->initDataSize);
    }

    backendContext->glFunctionTable.glCreateBuffers(1, &res->buffer.id);
    backendContext->glFunctionTable.glNamedBufferStorage(
      res->buffer.id,
      createResourceDescription->resourceDescription.width,
      createResourceDescription->initData,
      0);

#ifdef _DEBUG
    backendContext->glFunctionTable.glObjectLabel(GL_BUFFER, res->buffer.id, -1, res->resourceName);
#endif
    break;
  }
  case FFX_RESOURCE_TYPE_TEXTURE1D:
  {
    backendContext->glFunctionTable.glCreateTextures(GL_TEXTURE_1D, 1, &res->textureAllMipsView.id);
    backendContext->glFunctionTable.glTextureStorage1D(
      res->textureAllMipsView.id,
      res->resourceDescription.mipCount,
      getGLFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
      createResourceDescription->resourceDescription.width);

    if (createResourceDescription->initData)
    {
      backendContext->glFunctionTable.glad_glTextureSubImage1D(
        res->textureAllMipsView.id,
        0,
        0,
        createResourceDescription->resourceDescription.width,
        getGLUploadFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        getGLUploadTypeFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        createResourceDescription->initData);
    }

    break;
  }
  case FFX_RESOURCE_TYPE_TEXTURE2D:
  {
    backendContext->glFunctionTable.glCreateTextures(GL_TEXTURE_2D, 1, &res->textureAllMipsView.id);
    backendContext->glFunctionTable.glTextureStorage2D(
      res->textureAllMipsView.id,
      res->resourceDescription.mipCount,
      getGLFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
      createResourceDescription->resourceDescription.width,
      createResourceDescription->resourceDescription.height);

    if (createResourceDescription->initData)
    {
      backendContext->glFunctionTable.glad_glTextureSubImage2D(
        res->textureAllMipsView.id,
        0,
        0,
        0,
        createResourceDescription->resourceDescription.width,
        createResourceDescription->resourceDescription.height,
        getGLUploadFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        getGLUploadTypeFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        createResourceDescription->initData);
    }

    break;
  }
  case FFX_RESOURCE_TYPE_TEXTURE3D:
  {
    backendContext->glFunctionTable.glCreateTextures(GL_TEXTURE_3D, 1, &res->textureAllMipsView.id);
    backendContext->glFunctionTable.glTextureStorage3D(
      res->textureAllMipsView.id,
      res->resourceDescription.mipCount,
      getGLFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
      createResourceDescription->resourceDescription.width,
      createResourceDescription->resourceDescription.height,
      createResourceDescription->resourceDescription.depth);

    if (createResourceDescription->initData)
    {
      backendContext->glFunctionTable.glad_glTextureSubImage3D(
        res->textureAllMipsView.id,
        0,
        0,
        0,
        0,
        createResourceDescription->resourceDescription.width,
        createResourceDescription->resourceDescription.height,
        createResourceDescription->resourceDescription.depth,
        getGLUploadFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        getGLUploadTypeFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        createResourceDescription->initData);
    }
    break;
  }
  default:;
  }

  if (createResourceDescription->resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
  {
    GLenum type = 0;
    switch (createResourceDescription->resourceDescription.type)
    {
    case FFX_RESOURCE_TYPE_TEXTURE1D: type = GL_TEXTURE_1D; break;
    case FFX_RESOURCE_TYPE_TEXTURE2D: type = GL_TEXTURE_2D; break;
    case FFX_RESOURCE_TYPE_TEXTURE3D: type = GL_TEXTURE_3D; break;
    }

    res->textureAspect = BackendContext_GL::Aspect::COLOR;

    for (uint32_t i = 0; i < res->resourceDescription.mipCount; i++)
    {
      backendContext->glFunctionTable.glGenTextures(1, &res->textureSingleMipViews[i].id);
      backendContext->glFunctionTable.glTextureView(
        res->textureSingleMipViews[i].id,
        type,
        res->textureAllMipsView.id,
        getGLFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format),
        i,
        1,
        0,
        1);

      // texture view name
#ifdef _DEBUG
      backendContext->glFunctionTable.glObjectLabel(GL_TEXTURE, res->textureAllMipsView.id, -1, res->resourceName);
#endif
    }

    // texture name
#ifdef _DEBUG
    backendContext->glFunctionTable.glObjectLabel(GL_TEXTURE, res->textureAllMipsView.id, -1, res->resourceName);
#endif
  }

  /*
    if (createResourceDescription->initData)
    {
        // only allow copies directy into mapped memory for buffer resources since all texture resources are in optimal tiling
        if (createResourceDescription->heapType == FFX_HEAP_TYPE_UPLOAD && createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
        {
            void* data = NULL;

            if (backendContext->vkFunctionTable.vkMapMemory(backendContext->device, res->deviceMemory, 0, createResourceDescription->initDataSize, 0, &data) != VK_SUCCESS) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }

            memcpy(data, createResourceDescription->initData, createResourceDescription->initDataSize);

            // flush mapped range if memory type is not coherant
            if ((res->memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            {
                VkMappedMemoryRange memoryRange = {};
                memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                memoryRange.memory = res->deviceMemory;
                memoryRange.size = createResourceDescription->initDataSize;

                backendContext->vkFunctionTable.vkFlushMappedMemoryRanges(backendContext->device, 1, &memoryRange);
            }

            backendContext->vkFunctionTable.vkUnmapMemory(backendContext->device, res->deviceMemory);
        }
        else
        {
            FfxResourceInternal copySrc;
            FfxCreateResourceDescription uploadDesc = { *createResourceDescription };
            uploadDesc.heapType = FFX_HEAP_TYPE_UPLOAD;
            uploadDesc.resourceDescription.type = FFX_RESOURCE_TYPE_BUFFER;
            uploadDesc.resourceDescription.width = createResourceDescription->initDataSize;
            uploadDesc.usage = FFX_RESOURCE_USAGE_READ_ONLY;
            uploadDesc.initalState = FFX_RESOURCE_STATE_GENERIC_READ;
            uploadDesc.initData = createResourceDescription->initData;
            uploadDesc.initDataSize = createResourceDescription->initDataSize;

            backendInterface->fpCreateResource(backendInterface, &uploadDesc, &copySrc);

            // setup the upload job
            FfxGpuJobDescription copyJob =
            {
                FFX_GPU_JOB_COPY
            };
            copyJob.copyJobDescriptor.src = copySrc;
            copyJob.copyJobDescriptor.dst = *outResource;

            backendInterface->fpScheduleGpuJob(backendInterface, &copyJob);

            // add to the list of staging resources to delete later
            uint32_t stagingResIdx = backendContext->stagingResourceCount++;

            FFX_ASSERT(backendContext->stagingResourceCount < FSR2_MAX_STAGING_RESOURCE_COUNT);

            backendContext->stagingResources[stagingResIdx] = copySrc;
        }
    }
    */

  return FFX_OK;
}

FfxResourceDescription GetResourceDescriptorGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
{
  FFX_ASSERT(NULL != backendInterface);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  if (resource.internalIndex == -1)
  {
    return {};
  }

  return backendContext->resources[resource.internalIndex].resourceDescription;
}

FfxErrorCode CreatePipelineGL(FfxFsr2Interface* backendInterface, FfxFsr2Pass pass, const FfxPipelineDescription* pipelineDescription, FfxPipelineState* outPipeline)
{
  FFX_ASSERT(NULL != backendInterface);
  FFX_ASSERT(NULL != pipelineDescription);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  // query device capabilities 
  FfxDeviceCapabilities deviceCapabilities;

  GetDeviceCapabilitiesGL(backendInterface, &deviceCapabilities, nullptr);
  const uint32_t defaultSubgroupSize = getDefaultSubgroupSize(backendContext);

  // check if we can force wave64
  bool canForceWave64 = false;
  bool useLut = false;

  if (defaultSubgroupSize == 32 && deviceCapabilities.waveLaneCountMax == 64)
  {
    useLut = true;
    canForceWave64 = true;
  }
  else if (defaultSubgroupSize == 64)
  {
    useLut = true;
  }

  // check if we have 16bit floating point.
  bool supportedFP16 = deviceCapabilities.fp16Supported;

  // @todo check if vendor string is nvidia, then disable FP16 for these passes
  /*
  if (pass == FFX_FSR2_PASS_ACCUMULATE || pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN)
  {
      VkPhysicalDeviceProperties physicalDeviceProperties = {};
      vkGetPhysicalDeviceProperties(backendContext->physicalDevice, &physicalDeviceProperties);

      // Workaround: Disable FP16 path for the accumulate pass on NVIDIA due to reduced occupancy and high VRAM throughput.
      if (physicalDeviceProperties.vendorID == 0x10DE)
          supportedFP16 = false;
  }
  */

  // work out what permutation to load.
  uint32_t flags = 0;
  flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE) ? FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT : 0;
  flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ? 0 : FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS;
  flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) ? FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS : 0;
  flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DEPTH_INVERTED) ? FSR2_SHADER_PERMUTATION_DEPTH_INVERTED : 0;
  flags |= (pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN) ? FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING : 0;
  flags |= (useLut) ? FSR2_SHADER_PERMUTATION_REPROJECT_USE_LANCZOS_TYPE : 0;
  flags |= (canForceWave64) ? FSR2_SHADER_PERMUTATION_FORCE_WAVE64 : 0;
  flags |= (supportedFP16 && (pass != FFX_FSR2_PASS_RCAS)) ? FSR2_SHADER_PERMUTATION_ALLOW_FP16 : 0;

  const Fsr2ShaderBlobGL shaderBlob = fsr2GetPermutationBlobByIndexGL(pass, flags);
  FFX_ASSERT(shaderBlob.data && shaderBlob.size);

  // populate the pass.
  outPipeline->srvCount = shaderBlob.sampledImageCount;
  outPipeline->uavCount = shaderBlob.storageImageCount;
  outPipeline->constCount = shaderBlob.uniformBufferCount;

  FFX_ASSERT(shaderBlob.storageImageCount < FFX_MAX_NUM_UAVS);
  FFX_ASSERT(shaderBlob.sampledImageCount < FFX_MAX_NUM_SRVS);
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

  for (uint32_t srvIndex = 0; srvIndex < outPipeline->srvCount; ++srvIndex)
  {
    outPipeline->srvResourceBindings[srvIndex].slotIndex = shaderBlob.boundSampledImageBindings[srvIndex];
    wcscpy_s(outPipeline->srvResourceBindings[srvIndex].name, converter.from_bytes(shaderBlob.boundSampledImageNames[srvIndex]).c_str());
  }
  for (uint32_t uavIndex = 0; uavIndex < outPipeline->uavCount; ++uavIndex)
  {
    outPipeline->uavResourceBindings[uavIndex].slotIndex = shaderBlob.boundStorageImageBindings[uavIndex];
    wcscpy_s(outPipeline->uavResourceBindings[uavIndex].name, converter.from_bytes(shaderBlob.boundStorageImageNames[uavIndex]).c_str());
  }
  for (uint32_t cbIndex = 0; cbIndex < outPipeline->constCount; ++cbIndex)
  {
    outPipeline->cbResourceBindings[cbIndex].slotIndex = shaderBlob.boundUniformBufferBindings[cbIndex];
    wcscpy_s(outPipeline->cbResourceBindings[cbIndex].name, converter.from_bytes(shaderBlob.boundUniformBufferNames[cbIndex]).c_str());
  }

  // create the shader module
  GLuint shader = backendContext->glFunctionTable.glCreateShader(GL_COMPUTE_SHADER);
  backendContext->glFunctionTable.glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, shaderBlob.data, shaderBlob.size);
  backendContext->glFunctionTable.glSpecializeShader(shader, "main", 0, nullptr, nullptr);

  GLint compileStatus{};
  backendContext->glFunctionTable.glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
  if (compileStatus == GL_FALSE)
  {
    return FFX_ERROR_BACKEND_API_ERROR;
  }

  /*
    // set wave64 if possible
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupSizeCreateInfo = {};

    if (canForceWave64) {

        subgroupSizeCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        subgroupSizeCreateInfo.requiredSubgroupSize = 64;

        shaderStageCreateInfo.pNext = &subgroupSizeCreateInfo;
    }
  */

  // create the compute pipeline
  GLuint program = backendContext->glFunctionTable.glCreateProgram();
  backendContext->glFunctionTable.glAttachShader(program, shader);
  backendContext->glFunctionTable.glLinkProgram(program);

  GLint linkStatus{};
  backendContext->glFunctionTable.glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
  if (linkStatus == GL_FALSE)
  {
    backendContext->glFunctionTable.glDeleteShader(shader);
    return FFX_ERROR_BACKEND_API_ERROR;
  }

  backendContext->glFunctionTable.glDeleteShader(shader);

  outPipeline->pipeline = reinterpret_cast<FfxPipeline>(program);
  outPipeline->rootSignature = nullptr;

  return FFX_OK;
}

FfxErrorCode ScheduleGpuJobGL(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job)
{
  FFX_ASSERT(NULL != backendInterface);
  FFX_ASSERT(NULL != job);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  FFX_ASSERT(backendContext->gpuJobCount < FSR2_MAX_GPU_JOBS);

  backendContext->gpuJobs[backendContext->gpuJobCount] = *job;

  if (job->jobType == FFX_GPU_JOB_COMPUTE) {

    // needs to copy SRVs and UAVs in case they are on the stack only
    FfxComputeJobDescription* computeJob = &backendContext->gpuJobs[backendContext->gpuJobCount].computeJobDescriptor;
    const uint32_t numConstBuffers = job->computeJobDescriptor.pipeline.constCount;
    for (uint32_t currentRootConstantIndex = 0; currentRootConstantIndex < numConstBuffers; ++currentRootConstantIndex)
    {
      computeJob->cbs[currentRootConstantIndex].uint32Size = job->computeJobDescriptor.cbs[currentRootConstantIndex].uint32Size;
      memcpy(computeJob->cbs[currentRootConstantIndex].data, job->computeJobDescriptor.cbs[currentRootConstantIndex].data, computeJob->cbs[currentRootConstantIndex].uint32Size * sizeof(uint32_t));
    }
  }

  backendContext->gpuJobCount++;

  return FFX_OK;
}

static void addBarrier(BackendContext_GL* backendContext, FfxResourceInternal* resource, FfxResourceStates newState)
{
  FFX_ASSERT(NULL != backendContext);
  FFX_ASSERT(NULL != resource);

  BackendContext_GL::Resource& ffxResource = backendContext->resources[resource->internalIndex];

  backendContext->resources[resource->internalIndex].state = newState;

  if (ffxResource.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
  {
    GLbitfield barriers = 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_UNORDERED_ACCESS) ? GL_SHADER_STORAGE_BARRIER_BIT : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COMPUTE_READ) ? GL_UNIFORM_BARRIER_BIT : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COPY_SRC) ? (GL_BUFFER_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT) : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COPY_DEST) ? (GL_BUFFER_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT) : 0;
    backendContext->glFunctionTable.glMemoryBarrier(barriers);
  }
  else
  {
    GLbitfield barriers = 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_UNORDERED_ACCESS) ? GL_SHADER_IMAGE_ACCESS_BARRIER_BIT : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COMPUTE_READ) ? (GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT) : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COPY_SRC) ? GL_TEXTURE_UPDATE_BARRIER_BIT : 0;
    barriers |= (newState & FfxResourceStates::FFX_RESOURCE_STATE_COPY_DEST) ? GL_TEXTURE_UPDATE_BARRIER_BIT : 0;
    backendContext->glFunctionTable.glMemoryBarrier(barriers);
  }
}

static FfxErrorCode executeGpuJobCompute(BackendContext_GL* backendContext, FfxGpuJobDescription* job)
{
  // bind uavs (storage images)
  for (uint32_t uav = 0; uav < job->computeJobDescriptor.pipeline.uavCount; ++uav)
  {
    addBarrier(backendContext, &job->computeJobDescriptor.uavs[uav], FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    BackendContext_GL::Resource ffxResource = backendContext->resources[job->computeJobDescriptor.uavs[uav].internalIndex];

    backendContext->glFunctionTable.glBindImageTexture(
      job->computeJobDescriptor.pipeline.uavResourceBindings[uav].slotIndex,
      ffxResource.textureSingleMipViews[job->computeJobDescriptor.uavMip[uav]].id,
      0,
      true,
      0,
      GL_READ_WRITE,
      getGLFormatFromSurfaceFormat(ffxResource.resourceDescription.format)
    );
  }

  // bind srvs (sampled textures)
  for (uint32_t srv = 0; srv < job->computeJobDescriptor.pipeline.srvCount; ++srv)
  {
    addBarrier(backendContext, &job->computeJobDescriptor.srvs[srv], FFX_RESOURCE_STATE_COMPUTE_READ);

    BackendContext_GL::Resource ffxResource = backendContext->resources[job->computeJobDescriptor.srvs[srv].internalIndex];

    backendContext->glFunctionTable.glBindTextureUnit(job->computeJobDescriptor.pipeline.srvResourceBindings[srv].slotIndex, ffxResource.textureAllMipsView.id);
    backendContext->glFunctionTable.glBindSampler(job->computeJobDescriptor.pipeline.srvResourceBindings[srv].slotIndex, backendContext->linearSampler.id);
  }

  // update ubos (uniform buffers)
  for (uint32_t i = 0; i < job->computeJobDescriptor.pipeline.constCount; ++i)
  {
    auto ubo = accquireDynamicUBO(backendContext, job->computeJobDescriptor.cbs[i].uint32Size * sizeof(uint32_t), job->computeJobDescriptor.cbs[i].data);
    backendContext->glFunctionTable.glBindBufferRange(
      GL_UNIFORM_BUFFER,
      job->computeJobDescriptor.pipeline.cbResourceBindings[i].slotIndex,
      ubo.bufferResource.id,
      0,
      256
    );
  }

  backendContext->glFunctionTable.glUseProgram(reinterpret_cast<uintptr_t>(job->computeJobDescriptor.pipeline.pipeline));
  backendContext->glFunctionTable.glDispatchCompute(job->computeJobDescriptor.dimensions[0], job->computeJobDescriptor.dimensions[1], job->computeJobDescriptor.dimensions[2]);

  return FFX_OK;
}

static FfxErrorCode executeGpuJobCopy(BackendContext_GL* backendContext, FfxGpuJobDescription* job)
{
  FFX_ASSERT_FAIL("Copy job is not implemented in OpenGL backend");
  /*
    BackendContext_GL::Resource ffxResourceSrc = backendContext->resources[job->copyJobDescriptor.src.internalIndex];
    BackendContext_GL::Resource ffxResourceDst = backendContext->resources[job->copyJobDescriptor.dst.internalIndex];

    addBarrier(backendContext, &job->copyJobDescriptor.src, FFX_RESOURCE_STATE_COPY_SRC);
    addBarrier(backendContext, &job->copyJobDescriptor.dst, FFX_RESOURCE_STATE_COPY_DEST);

    if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER && ffxResourceDst.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
    {
      auto bufferSrc = ffxResourceSrc.resource.GetBuffer();
      auto bufferDst = ffxResourceDst.resource.GetBuffer();

      backendContext->glFunctionTable.glCopyNamedBufferSubData(bufferSrc.id, bufferDst.id, 0, 0, ffxResourceSrc.resourceDescription.width);
    }
    else if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER && ffxResourceDst.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
    {
        VkBuffer vkResourceSrc = ffxResourceSrc.bufferResource;
        VkImage vkResourceDst = ffxResourceDst.imageResource;

        VkImageSubresourceLayers subresourceLayers = {};

        subresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceLayers.baseArrayLayer = 0;
        subresourceLayers.layerCount = 1;
        subresourceLayers.mipLevel = 0;

        VkOffset3D offset = {};

        offset.x = 0;
        offset.y = 0;
        offset.z = 0;

        VkExtent3D extent = {};

        extent.width = ffxResourceDst.resourceDescription.width;
        extent.height = ffxResourceDst.resourceDescription.height;
        extent.depth = ffxResourceDst.resourceDescription.depth;

        VkBufferImageCopy bufferImageCopy = {};

        bufferImageCopy.bufferOffset = 0;
        bufferImageCopy.bufferRowLength = 0;
        bufferImageCopy.bufferImageHeight = 0;
        bufferImageCopy.imageSubresource = subresourceLayers;
        bufferImageCopy.imageOffset = offset;
        bufferImageCopy.imageExtent = extent;

        backendContext->vkFunctionTable.vkCmdCopyBufferToImage(vkCommandBuffer, vkResourceSrc, vkResourceDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
    }
    else
    {
        VkImageCopy             imageCopies[FSR2_MAX_IMAGE_COPY_MIPS];
        VkImage vkResourceSrc = ffxResourceSrc.imageResource;
        VkImage vkResourceDst = ffxResourceDst.imageResource;

        for (uint32_t mip = 0; mip < ffxResourceSrc.resourceDescription.mipCount; mip++)
        {
            VkImageSubresourceLayers subresourceLayers = {};

            subresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceLayers.baseArrayLayer = 0;
            subresourceLayers.layerCount = 1;
            subresourceLayers.mipLevel = mip;

            VkOffset3D offset = {};

            offset.x = 0;
            offset.y = 0;
            offset.z = 0;

            VkExtent3D extent = {};

            extent.width = ffxResourceSrc.resourceDescription.width / (mip + 1);
            extent.height = ffxResourceSrc.resourceDescription.height / (mip + 1);
            extent.depth = ffxResourceSrc.resourceDescription.depth / (mip + 1);

            VkImageCopy& copyRegion = imageCopies[mip];

            copyRegion.srcSubresource = subresourceLayers;
            copyRegion.srcOffset = offset;
            copyRegion.dstSubresource = subresourceLayers;
            copyRegion.dstOffset = offset;
            copyRegion.extent = extent;
        }

        backendContext->vkFunctionTable.vkCmdCopyImage(vkCommandBuffer, vkResourceSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkResourceDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ffxResourceSrc.resourceDescription.mipCount, imageCopies);
    }
    */

  return FFX_OK;
}

static FfxErrorCode executeGpuJobClearFloat(BackendContext_GL* backendContext, FfxGpuJobDescription* job)
{
  uint32_t idx = job->clearJobDescriptor.target.internalIndex;
  BackendContext_GL::Resource ffxResource = backendContext->resources[idx];

  if (ffxResource.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
  {
    addBarrier(backendContext, &job->clearJobDescriptor.target, FFX_RESOURCE_STATE_COPY_DEST);

    auto texture = ffxResource.textureAllMipsView;

    float clearColorValue[4] = {};
    clearColorValue[0] = job->clearJobDescriptor.color[0];
    clearColorValue[1] = job->clearJobDescriptor.color[1];
    clearColorValue[2] = job->clearJobDescriptor.color[2];
    clearColorValue[3] = job->clearJobDescriptor.color[3];

    for (uint32_t i = 0; i < ffxResource.resourceDescription.mipCount; i++)
    {
      backendContext->glFunctionTable.glClearTexImage(texture.id, i, GL_RGBA, GL_FLOAT, clearColorValue);
    }
  }

  return FFX_OK;
}

FfxErrorCode ExecuteGpuJobsGL(FfxFsr2Interface* backendInterface, FfxCommandList commandList)
{
  FFX_ASSERT(NULL != backendInterface);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  FfxErrorCode errorCode = FFX_OK;

  // execute all renderjobs
  for (uint32_t i = 0; i < backendContext->gpuJobCount; ++i)
  {
    FfxGpuJobDescription* gpuJob = &backendContext->gpuJobs[i];

    switch (gpuJob->jobType)
    {
    case FFX_GPU_JOB_CLEAR_FLOAT:
    {
      errorCode = executeGpuJobClearFloat(backendContext, gpuJob);
      break;
    }
    case FFX_GPU_JOB_COPY:
    {
      errorCode = executeGpuJobCopy(backendContext, gpuJob);
      break;
    }
    case FFX_GPU_JOB_COMPUTE:
    {
      errorCode = executeGpuJobCompute(backendContext, gpuJob);
      break;
    }
    default:;
    }
  }

  // check the execute function returned cleanly.
  FFX_RETURN_ON_ERROR(
    errorCode == FFX_OK,
    FFX_ERROR_BACKEND_API_ERROR);

  backendContext->gpuJobCount = 0;

  return FFX_OK;
}

FfxErrorCode DestroyResourceGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
{
  FFX_ASSERT(backendInterface != nullptr);

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  if (resource.internalIndex != -1)
  {
    BackendContext_GL::Resource& res = backendContext->resources[resource.internalIndex];

    if (res.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
    {
      if (res.buffer.id)
      {
        backendContext->glFunctionTable.glDeleteBuffers(1, &res.buffer.id);
        res.buffer = {};
      }
    }
    else
    {
      if (res.textureAllMipsView.id)
      {
        backendContext->glFunctionTable.glDeleteTextures(1, &res.textureAllMipsView.id);
        res.textureAllMipsView = {};
      }

      for (uint32_t i = 0; i < res.resourceDescription.mipCount; i++)
      {
        if (res.textureSingleMipViews[i].id)
        {
          backendContext->glFunctionTable.glDeleteTextures(1, &res.textureSingleMipViews[i].id);
          res.textureSingleMipViews[i] = {};
        }
      }
    }
  }

  return FFX_OK;
}

FfxErrorCode DestroyPipelineGL(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline)
{
  FFX_ASSERT(backendInterface != nullptr);
  if (!pipeline)
    return FFX_OK;

  BackendContext_GL* backendContext = (BackendContext_GL*)backendInterface->scratchBuffer;

  // destroy pipeline 
  GLuint program = reinterpret_cast<GLuint>(pipeline->pipeline);
  if (program) {
    backendContext->glFunctionTable.glDeleteProgram(program);
    pipeline->pipeline = nullptr;
  }

  return FFX_OK;
}
