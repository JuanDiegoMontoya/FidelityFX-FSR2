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

// @defgroup GL

#pragma once

#include <glad/gl.h>
#include "../ffx_fsr2_interface.h"

#if defined(__cplusplus)
extern "C" {
#endif // #if defined(__cplusplus)

    typedef void(*ffx_glProc)();
    typedef ffx_glProc(*ffx_glGetProcAddress)(const char*);

    /// Query how much memory is required for the Vulkan backend's scratch buffer.
    ///
    /// @returns
    /// The size (in bytes) of the required scratch memory buffer for the GL backend.
    FFX_API size_t ffxFsr2GetScratchMemorySizeGL();

    /// Populate an interface with pointers for the GL backend.
    ///
    /// @param [out] outInterface              A pointer to a <c><i>FfxFsr2Interface</i></c> structure to populate with pointers.
    /// @param [in] scratchBuffer               A pointer to a buffer of memory which can be used by the DirectX(R)12 backend.
    /// @param [in] scratchBufferSize           The size (in bytes) of the buffer pointed to by <c><i>scratchBuffer</i></c>.
    /// @param [in] getProcAddress           A function pointer to vkGetDeviceProcAddr which is used to obtain all the other Vulkan functions.
    /// 
    /// @retval
    /// FFX_OK                                  The operation completed successfully.
    /// @retval
    /// FFX_ERROR_CODE_INVALID_POINTER          The <c><i>interface</i></c> pointer was <c><i>NULL</i></c>.
    /// 
    /// @ingroup FSR2 GL
    FFX_API FfxErrorCode ffxFsr2GetInterfaceGL(
        FfxFsr2Interface* outInterface,
        void* scratchBuffer,
        size_t scratchBufferSize,
        ffx_glGetProcAddress getProcAddress);

    /// Create a <c><i>FfxResource</i></c> from a <c><i>VkImage</i></c>.
    ///
    /// @param [in] context                     A pointer to a <c><i>FfxFsr2Context</i></c> structure.
    /// @param [in] imgVk                       A Vulkan image resource.
    /// @param [in] imageView                   An image view of the given image resource.
    /// @param [in] width                       The width of the image resource.
    /// @param [in] height                      The height of the image resource.
    /// @param [in] imgFormat                   The format of the image resource.
    /// @param [in] name                        (optional) A name string to identify the resource in debug mode.
    /// @param [in] state                       The state the resource is currently in.
    /// 
    /// @returns
    /// An abstract FidelityFX resources.
    /// 
    /// @ingroup FSR2 GL
    FFX_API FfxResource ffxGetTextureResourceGL(FfxFsr2Context* context, 
        GLuint imageGL, 
        uint32_t width, 
        uint32_t height, 
        GLenum imgFormat, 
        const wchar_t* name = nullptr, 
        FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);

    /// Create a <c><i>FfxResource</i></c> from a <c><i>VkBuffer</i></c>.
    ///
    /// @param [in] context                     A pointer to a <c><i>FfxFsr2Context</i></c> structure.
    /// @param [in] bufVk                       A Vulkan buffer resource.
    /// @param [in] size                        The size of the buffer resource.
    /// @param [in] name                        (optional) A name string to identify the resource in debug mode.
    /// @param [in] state                       The state the resource is currently in.
    /// 
    /// @returns
    /// An abstract FidelityFX resources.
    /// 
    /// @ingroup FSR2 GL
    FFX_API FfxResource ffxGetBufferResourceGL(FfxFsr2Context* context, 
        GLuint bufferGL, 
        uint32_t size, 
        const wchar_t* name = nullptr, 
        FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);

    /// Convert a <c><i>FfxResource</i></c> value to a <c><i>VkImage</i></c>.
    ///
    /// @param [in] context                     A pointer to a <c><i>FfxFsr2Context</i></c> structure.
    /// @param [in] resId                       A resourceID.
    /// 
    /// @returns
    /// A <c><i>VkImage</i></c>.
    /// 
    /// @ingroup FSR2 GL
    FFX_API GLuint ffxGetGLImage(FfxFsr2Context* context, uint32_t resId);

#if defined(__cplusplus)
}
#endif // #if defined(__cplusplus)
