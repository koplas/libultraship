# OpenGL Backend Performance Optimization Plan

## Executive Summary
This plan addresses the performance gap between the OpenGL and DirectX11 backends in libultraship. The primary bottlenecks are GPU-CPU synchronization stalls, inefficient buffer management, and insufficient state caching. Implementing these optimizations should bring OpenGL performance close to DirectX11 levels.

---

## Priority Rankings
- **Critical (P0)**: Tasks 1-2 - Address major synchronization and buffer bottlenecks (~70% performance impact)
- **High (P1)**: Tasks 3-4 - State management improvements (~20% performance impact)
- **Medium (P2)**: Tasks 5-7 - Additional optimizations (~10% performance impact)
- **Infrastructure (P3)**: Task 8-9 - Measurement and validation

---

## Task 1: Remove glFlush() Synchronization Bottleneck [P0]
**File**: `src/fast/backends/gfx_opengl.cpp:698`

### Current Implementation
```cpp
void GfxRenderingAPIOGL::EndFrame() {
    glFlush();
}
```

### Problem
- Forces GPU to complete all pending commands before returning
- Creates a CPU-GPU pipeline stall at every frame end
- Prevents asynchronous command queue optimization
- Causes the CPU to wait for GPU completion

### Proposed Solution
```cpp
void GfxRenderingAPIOGL::EndFrame() {
    // Option 1: Remove entirely and let driver/swap chain handle flushing
    // (Preferred - let vsync handle synchronization)

    // Option 2: Only flush if explicitly needed (e.g., before CPU reads)
    // if (mNeedExplicitFlush) {
    //     glFlush();
    //     mNeedExplicitFlush = false;
    // }

    // Option 3: Use glFlush() only in debug mode for easier debugging
    #ifdef _DEBUG
    glFlush();
    #endif
}
```

### Implementation Steps
1. Remove `glFlush()` call from EndFrame()
2. Test for rendering artifacts or timing issues
3. Check if swap chain (SDL/GLFW) handles synchronization properly
4. Add explicit flush only for operations requiring CPU-GPU sync (ReadFramebufferToCPU)
5. Measure frame time improvement

### Expected Impact
**High** - 30-40% frame time reduction by eliminating forced synchronization

---

## Task 2: Implement Persistent Buffer Mapping for Vertex Data [P0]
**File**: `src/fast/backends/gfx_opengl.cpp:647-651`

### Current Implementation
```cpp
glBindBuffer(GL_ARRAY_BUFFER, mVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
```

### Problem
- Recreates buffer storage every frame with `glBufferData()`
- `GL_STREAM_DRAW` hints at single-use, causing allocation overhead
- Creates implicit CPU-GPU synchronization point
- No buffer pooling or orphaning strategy

### Proposed Solution: Buffer Orphaning (OpenGL 3.3+)
```cpp
// In header file (gfx_opengl.h)
class GfxRenderingAPIOGL : public GfxRenderingAPI {
private:
    GLuint mVBO;
    size_t mVBOSize;
    static const size_t INITIAL_VBO_SIZE = 1024 * 1024; // 1MB initial
};

// In implementation (gfx_opengl.cpp)
void GfxRenderingAPIOGL::DrawTriangles(uint32_t buf_vbo_len, uint32_t buf_vbo_num_tris) {
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);

    size_t required_size = sizeof(float) * buf_vbo_len;

    // Resize if needed (with headroom to avoid frequent resizes)
    if (required_size > mVBOSize) {
        mVBOSize = required_size * 2; // Double size for growth
        glBufferData(GL_ARRAY_BUFFER, mVBOSize, nullptr, GL_STREAM_DRAW);
    }

    // Orphan the buffer to avoid GPU stall
    glBufferData(GL_ARRAY_BUFFER, mVBOSize, nullptr, GL_STREAM_DRAW);

    // Upload new data
    glBufferSubData(GL_ARRAY_BUFFER, 0, required_size, buf_vbo);

    // ... rest of draw call
}
```

### Alternative Solution: Persistent Mapped Buffers (OpenGL 4.4+)
```cpp
// More advanced - only if minimum OpenGL version supports it
#ifdef GL_ARB_buffer_storage

class GfxRenderingAPIOGL : public GfxRenderingAPI {
private:
    GLuint mVBO;
    void* mMappedVBO;
    size_t mVBOSize;
    GLsync mFence;
    static const int BUFFER_RING_SIZE = 3; // Triple buffering
    int mCurrentBufferIndex;
};

void GfxRenderingAPIOGL::Init() {
    // Create persistent mapped buffer
    glGenBuffers(1, &mVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);

    GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glBufferStorage(GL_ARRAY_BUFFER, mVBOSize, nullptr, flags);

    mMappedVBO = glMapBufferRange(GL_ARRAY_BUFFER, 0, mVBOSize, flags);
}

void GfxRenderingAPIOGL::DrawTriangles(uint32_t buf_vbo_len, uint32_t buf_vbo_num_tris) {
    // Wait for GPU to finish with this section of the buffer
    if (mFence) {
        glClientWaitSync(mFence, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(mFence);
    }

    // Copy directly to mapped memory
    size_t offset = (mCurrentBufferIndex * mVBOSize) / BUFFER_RING_SIZE;
    memcpy((uint8_t*)mMappedVBO + offset, buf_vbo, sizeof(float) * buf_vbo_len);

    // Bind correct region
    glBindBufferRange(GL_ARRAY_BUFFER, 0, mVBO, offset, sizeof(float) * buf_vbo_len);

    // ... draw call ...

    // Fence for next frame
    mFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    mCurrentBufferIndex = (mCurrentBufferIndex + 1) % BUFFER_RING_SIZE;
}

#endif
```

### Implementation Steps
1. **Phase 1**: Implement buffer orphaning strategy (works on OpenGL 3.3+)
   - Add `mVBOSize` tracking to class
   - Modify DrawTriangles to use orphaning pattern
   - Test with various buffer sizes

2. **Phase 2**: (Optional) Implement persistent mapping if min version allows
   - Check OpenGL version at runtime
   - Add fallback to orphaning for older systems
   - Implement ring buffer strategy

3. **Phase 3**: Apply same pattern to other buffers (EBO if used)

### Expected Impact
**High** - 20-30% improvement by eliminating per-frame buffer allocations and sync points

---

## Task 3: Add Comprehensive State Caching System [P1]
**File**: `src/fast/backends/gfx_opengl.cpp` (throughout)

### Problem
- Basic state caching exists but is incomplete
- Multiple redundant GL state changes per frame
- No tracking for: bound textures, active texture unit, VAO, bound shaders, viewport

### Current Partial Implementation
```cpp
// Only caches depth test state
if (mCurrentDepthTest != mLastDepthTest || mCurrentDepthMask != mLastDepthMask) {
    mLastDepthTest = mCurrentDepthTest;
    mLastDepthMask = mCurrentDepthMask;
    // ... state changes
}
```

### Proposed Solution
```cpp
// In header (gfx_opengl.h)
class GfxRenderingAPIOGL : public GfxRenderingAPI {
private:
    // Existing state cache
    bool mLastDepthTest = false;
    bool mLastDepthMask = false;

    // New state cache members (mirroring DX11 backend)
    ShaderProgram* mLastBoundShaderProgram = nullptr;
    GLuint mLastBoundTextures[SHADER_MAX_TEXTURES] = {0};
    GLenum mLastActiveTextureUnit = GL_TEXTURE0;
    GLuint mLastBoundVAO = 0;
    GLuint mLastBoundVBO = 0;
    GLuint mLastBoundEBO = 0;

    struct BlendState {
        bool enabled = false;
        GLenum srcRGB = GL_ONE;
        GLenum dstRGB = GL_ZERO;
        GLenum srcAlpha = GL_ONE;
        GLenum dstAlpha = GL_ZERO;

        bool operator!=(const BlendState& other) const {
            return enabled != other.enabled || srcRGB != other.srcRGB ||
                   dstRGB != other.dstRGB || srcAlpha != other.srcAlpha ||
                   dstAlpha != other.dstAlpha;
        }
    };
    BlendState mLastBlendState;

    struct ScissorState {
        bool enabled = false;
        GLint x = 0, y = 0;
        GLsizei width = 0, height = 0;

        bool operator!=(const ScissorState& other) const {
            return enabled != other.enabled || x != other.x ||
                   y != other.y || width != other.width || height != other.height;
        }
    };
    ScissorState mLastScissorState;

    struct ViewportState {
        GLint x = 0, y = 0;
        GLsizei width = 0, height = 0;

        bool operator!=(const ViewportState& other) const {
            return x != other.x || y != other.y ||
                   width != other.width || height != other.height;
        }
    };
    ViewportState mLastViewportState;

    // Helper methods
    void BindTextureIfNeeded(int slot, GLuint texture);
    void UseShaderProgramIfNeeded(ShaderProgram* program);
    void SetBlendStateIfNeeded(const BlendState& state);
    void SetScissorStateIfNeeded(const ScissorState& state);
    void SetViewportIfNeeded(const ViewportState& state);
};

// In implementation
void GfxRenderingAPIOGL::BindTextureIfNeeded(int slot, GLuint texture) {
    if (mLastBoundTextures[slot] != texture || mLastActiveTextureUnit != GL_TEXTURE0 + slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, texture);
        mLastActiveTextureUnit = GL_TEXTURE0 + slot;
        mLastBoundTextures[slot] = texture;
    }
}

void GfxRenderingAPIOGL::UseShaderProgramIfNeeded(ShaderProgram* program) {
    if (mLastBoundShaderProgram != program) {
        glUseProgram(program->opengl_program_id);
        mLastBoundShaderProgram = program;
    }
}

void GfxRenderingAPIOGL::SetBlendStateIfNeeded(const BlendState& state) {
    if (mLastBlendState != state) {
        if (state.enabled) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(state.srcRGB, state.dstRGB, state.srcAlpha, state.dstAlpha);
        } else {
            glDisable(GL_BLEND);
        }
        mLastBlendState = state;
    }
}

// Apply throughout DrawTriangles, SetShaderProgram, etc.
void GfxRenderingAPIOGL::DrawTriangles(uint32_t buf_vbo_len, uint32_t buf_vbo_num_tris) {
    // Use cached state instead of direct calls
    UseShaderProgramIfNeeded(mCurrentShaderProgram);

    // Bind textures only if changed
    for (int i = 0; i < mCurrentShaderProgram->num_inputs; i++) {
        BindTextureIfNeeded(i, mTextures[mCurrentTextureIds[i]].texture_id);
    }

    // ... rest of function
}
```

### Implementation Steps
1. Add state tracking members to header file
2. Implement helper methods for state changes
3. Refactor existing code to use helpers instead of direct GL calls
4. Add debug validation mode to verify state cache correctness
5. Benchmark improvement

### Expected Impact
**Medium-High** - 10-15% improvement by reducing redundant state changes

---

## Task 4: Migrate to Uniform Buffer Objects (UBOs) [P1]
**File**: `src/fast/backends/gfx_opengl.cpp:678-695`

### Current Implementation
```cpp
void GfxRenderingAPIOGL::SetUniforms(ShaderProgram* prg) const {
    if (prg->used_noise) {
        glUniform1i(prg->frameCountLocation, mFrameCount);
        glUniform1f(prg->noiseScaleLocation, mCurrentNoiseScale);
    }

    for (int i = 0; i < prg->num_inputs; i++) {
        // ... texture dimension uniforms
        glUniform2f(prg->textureSizeLocation[i], (float)width, (float)height);
    }

    for (int i = 0; i < prg->num_floats; i++) {
        glUniform1f(prg->uniform_locations[i], prg->uniform_values[i]);
    }
}
```

### Problem
- Each `glUniform*` call has CPU overhead
- Uniforms are set individually, not batched
- No caching of uniform values (set every draw even if unchanged)

### Proposed Solution
```cpp
// In header
class GfxRenderingAPIOGL : public GfxRenderingAPI {
private:
    // Match DirectX11's constant buffer structure
    struct PerFrameConstants {
        int32_t frameCount;
        float noiseScale;
        float padding[2]; // Align to 16 bytes
    };

    struct PerDrawConstants {
        float textureSizes[SHADER_MAX_TEXTURES * 2]; // width, height pairs
        float uniformValues[32]; // Max shader uniforms
        int32_t numTextures;
        int32_t numUniforms;
        float padding[2];
    };

    GLuint mPerFrameUBO;
    GLuint mPerDrawUBO;
    PerFrameConstants mCurrentPerFrameConstants;
    PerDrawConstants mCurrentPerDrawConstants;
    PerFrameConstants mLastPerFrameConstants;
    PerDrawConstants mLastPerDrawConstants;
};

// In initialization
void GfxRenderingAPIOGL::Init() {
    // Create UBOs
    glGenBuffers(1, &mPerFrameUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mPerFrameUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(PerFrameConstants), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mPerFrameUBO); // Binding point 0

    glGenBuffers(1, &mPerDrawUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mPerDrawUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(PerDrawConstants), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, mPerDrawUBO); // Binding point 1
}

// In shader compilation (gfx_opengl.cpp: shader creation)
// Modify shaders to use uniform blocks:
const char* ubo_declaration = R"(
    layout(std140, binding = 0) uniform PerFrameData {
        int u_FrameCount;
        float u_NoiseScale;
    };

    layout(std140, binding = 1) uniform PerDrawData {
        vec2 u_TextureSizes[8];
        float u_UniformValues[32];
    };
)";

// Upload constants only when changed
void GfxRenderingAPIOGL::UpdatePerFrameConstants() {
    if (memcmp(&mCurrentPerFrameConstants, &mLastPerFrameConstants,
               sizeof(PerFrameConstants)) != 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, mPerFrameUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerFrameConstants),
                       &mCurrentPerFrameConstants);
        mLastPerFrameConstants = mCurrentPerFrameConstants;
    }
}

void GfxRenderingAPIOGL::UpdatePerDrawConstants() {
    if (memcmp(&mCurrentPerDrawConstants, &mLastPerDrawConstants,
               sizeof(PerDrawConstants)) != 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, mPerDrawUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerDrawConstants),
                       &mCurrentPerDrawConstants);
        mLastPerDrawConstants = mCurrentPerDrawConstants;
    }
}
```

### Implementation Steps
1. Design UBO layout matching DirectX11 constant buffers
2. Modify shader generator to output uniform blocks instead of individual uniforms
3. Create UBOs during initialization
4. Update SetUniforms to populate UBO structs and upload when changed
5. Add memcmp-based caching to avoid redundant uploads
6. Test compatibility with existing shaders

### Expected Impact
**Medium** - 5-10% improvement from batched uniform updates and caching

---

## Task 5: Implement Sampler State Caching [P2]
**File**: `src/fast/backends/gfx_opengl.cpp:562-570`

### Current Implementation
```cpp
void GfxRenderingAPIOGL::SetSamplerParameters(int tile, bool linear_filter, bool clamp) {
    GLenum filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}
```

### Problem
- Sets sampler parameters via texture state (legacy approach)
- No caching - sets parameters every time texture is bound
- DirectX11 creates and caches sampler state objects

### Proposed Solution
```cpp
// In header
class GfxRenderingAPIOGL : public GfxRenderingAPI {
private:
    struct SamplerKey {
        bool linear_filter;
        bool clamp;

        bool operator==(const SamplerKey& other) const {
            return linear_filter == other.linear_filter && clamp == other.clamp;
        }
    };

    struct SamplerKeyHash {
        size_t operator()(const SamplerKey& key) const {
            return (key.linear_filter ? 1 : 0) | (key.clamp ? 2 : 0);
        }
    };

    std::unordered_map<SamplerKey, GLuint, SamplerKeyHash> mSamplerCache;
    GLuint mLastBoundSamplers[SHADER_MAX_TEXTURES] = {0};
};

// In implementation
GLuint GfxRenderingAPIOGL::GetOrCreateSampler(bool linear_filter, bool clamp) {
    SamplerKey key = {linear_filter, clamp};

    auto it = mSamplerCache.find(key);
    if (it != mSamplerCache.end()) {
        return it->second;
    }

    // Create new sampler object
    GLuint sampler;
    glGenSamplers(1, &sampler);

    GLenum filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);

    GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap);

    mSamplerCache[key] = sampler;
    return sampler;
}

void GfxRenderingAPIOGL::SetSamplerParameters(int slot, bool linear_filter, bool clamp) {
    GLuint sampler = GetOrCreateSampler(linear_filter, clamp);

    if (mLastBoundSamplers[slot] != sampler) {
        glBindSampler(slot, sampler);
        mLastBoundSamplers[slot] = sampler;
    }
}
```

### Implementation Steps
1. Add sampler object cache to class
2. Implement GetOrCreateSampler method
3. Replace glTexParameteri calls with glBindSampler
4. Track last bound samplers per texture unit
5. Clean up cache on shutdown

### Expected Impact
**Low-Medium** - 3-5% improvement from eliminating redundant sampler state changes

---

## Task 6: Add Texture Binding State Tracking [P2]
**File**: `src/fast/backends/gfx_opengl.cpp` (DrawTriangles)

### Problem
- Textures may be rebound unnecessarily
- No tracking of which texture is bound to which unit

### Solution
Already covered in Task 3 (State Caching System) via `BindTextureIfNeeded()`

### Implementation Steps
1. Implemented as part of Task 3
2. Add specific checks before texture binding operations
3. Track active texture unit to minimize `glActiveTexture()` calls

### Expected Impact
**Low** - 2-3% improvement (combined with Task 3)

---

## Task 7: Optimize Framebuffer Operations [P2]
**Files**:
- `src/fast/backends/gfx_opengl.cpp:813` (ReadFramebufferToCPU)
- `src/fast/backends/gfx_opengl.cpp:854` (CopyFramebuffer)
- `src/fast/backends/gfx_opengl.cpp:943` (resolve operation)

### Current Implementation
```cpp
void GfxRenderingAPIOGL::ReadFramebufferToCPU(...) {
    glDisable(GL_SCISSOR_TEST); // State change
    // ... operations ...
    // No restoration of previous state
}

void GfxRenderingAPIOGL::CopyFramebuffer(...) {
    glDisable(GL_SCISSOR_TEST); // State change
    // ... blit operations ...
    // No restoration
}
```

### Problem
- Disables scissor test without saving/restoring previous state
- Forces subsequent draws to re-enable scissor
- Multiple state changes per framebuffer operation

### Proposed Solution
```cpp
void GfxRenderingAPIOGL::ReadFramebufferToCPU(...) {
    // Save current state
    ScissorState savedScissor = mLastScissorState;

    // Temporarily disable scissor if needed
    if (savedScissor.enabled) {
        SetScissorStateIfNeeded({false, 0, 0, 0, 0});
    }

    // ... operations ...

    // Restore state
    SetScissorStateIfNeeded(savedScissor);

    // IMPORTANT: Add explicit flush here since CPU reads require sync
    glFlush();
}

void GfxRenderingAPIOGL::CopyFramebuffer(...) {
    ScissorState savedScissor = mLastScissorState;

    if (savedScissor.enabled) {
        SetScissorStateIfNeeded({false, 0, 0, 0, 0});
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_write);
    glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    SetScissorStateIfNeeded(savedScissor);
}
```

### Implementation Steps
1. Audit all functions that change GL state temporarily
2. Save state before changes, restore after
3. Use state cache helpers to minimize redundant calls
4. Only add explicit flush for CPU read operations

### Expected Impact
**Low** - 1-2% improvement from reduced state thrashing

---

## Task 8: Add Performance Metrics and Profiling [P3]
**New File**: `src/fast/backends/gfx_performance_metrics.h`

### Proposed Implementation
```cpp
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

class GfxPerformanceMetrics {
public:
    struct FrameStats {
        uint32_t drawCalls = 0;
        uint32_t stateChanges = 0;
        uint32_t bufferUploads = 0;
        uint32_t textureBinds = 0;
        uint32_t shaderChanges = 0;
        double frameTimeMs = 0.0;
        double gpuTimeMs = 0.0;
    };

    void BeginFrame();
    void EndFrame();

    void RecordDrawCall() { mCurrentFrame.drawCalls++; }
    void RecordStateChange() { mCurrentFrame.stateChanges++; }
    void RecordBufferUpload() { mCurrentFrame.bufferUploads++; }
    void RecordTextureBinding() { mCurrentFrame.textureBinds++; }
    void RecordShaderChange() { mCurrentFrame.shaderChanges++; }

    const FrameStats& GetLastFrameStats() const { return mLastFrame; }
    const FrameStats& GetAverageStats() const { return mAverageStats; }

    void PrintSummary();

private:
    FrameStats mCurrentFrame;
    FrameStats mLastFrame;
    FrameStats mAverageStats;

    std::chrono::high_resolution_clock::time_point mFrameStartTime;
    uint32_t mFrameCount = 0;
};
```

### Integration Points
- Instrument state change functions to call `RecordStateChange()`
- Track draw calls, buffer uploads, texture bindings
- Add GPU timer queries for accurate GPU timing:

```cpp
// In GfxRenderingAPIOGL
GLuint mTimerQueries[2];

void GfxRenderingAPIOGL::BeginFrame() {
    glBeginQuery(GL_TIME_ELAPSED, mTimerQueries[mCurrentFrame % 2]);
    mMetrics.BeginFrame();
}

void GfxRenderingAPIOGL::EndFrame() {
    glEndQuery(GL_TIME_ELAPSED);

    // Get result from previous frame (to avoid stalling)
    GLuint64 gpuTime;
    glGetQueryObjectui64v(mTimerQueries[(mCurrentFrame + 1) % 2],
                         GL_QUERY_RESULT, &gpuTime);

    mMetrics.EndFrame();
    mMetrics.SetGPUTime(gpuTime / 1000000.0); // Convert to ms
}
```

### Implementation Steps
1. Create performance metrics class
2. Add metrics tracking to GfxRenderingAPIOGL
3. Instrument key operations
4. Add console command or config option to enable/disable tracking
5. Add periodic logging or on-demand reporting

### Expected Impact
**Infrastructure** - Enables measurement and validation of optimizations

---

## Task 9: Benchmark Against DirectX11 Baseline [P3]

### Methodology
1. Create identical test scene with known characteristics:
   - Multiple textured objects
   - Various shader programs
   - Mix of transparent and opaque geometry
   - State changes

2. Measure key metrics:
   - Frame time (CPU)
   - GPU time
   - Draw calls per frame
   - State changes per frame
   - Buffer uploads per frame

3. Compare OpenGL vs DirectX11:
   - Before optimizations (baseline)
   - After each optimization task
   - Final comparison

### Test Scenarios
```cpp
// Benchmark scene generator
void GenerateBenchmarkScene() {
    // Scenario 1: High draw call count (stress test state caching)
    for (int i = 0; i < 1000; i++) {
        DrawTexturedQuad(texture[i % 10], shader[i % 5]);
    }

    // Scenario 2: High vertex count (stress test buffer management)
    DrawMesh(highPolyModel, 100000 vertices);

    // Scenario 3: Shader switching (stress test program binding)
    for (int i = 0; i < 100; i++) {
        UseShader(shader[i % 10]);
        DrawQuad();
    }

    // Scenario 4: Texture thrashing (stress test texture binding)
    for (int i = 0; i < 500; i++) {
        BindTexture(0, texture[rand() % 20]);
        DrawQuad();
    }
}
```

### Reporting Format
```
=== OpenGL Performance Report ===
Scenario: High Draw Calls
  Before Optimization:
    Frame Time: 16.7ms (60 FPS)
    GPU Time: 14.2ms
    Draw Calls: 1000
    State Changes: 3500

  After Optimization:
    Frame Time: 8.3ms (120 FPS)
    GPU Time: 6.1ms
    Draw Calls: 1000
    State Changes: 850
    Improvement: 50% faster

DirectX11 Baseline:
    Frame Time: 7.9ms (126 FPS)
    GPU Time: 5.8ms
    Draw Calls: 1000
    State Changes: 820

Gap: OpenGL now within 5% of DirectX11 (was 100% slower)
```

### Implementation Steps
1. Create benchmark scenes
2. Integrate with existing test framework
3. Add command-line flags for running benchmarks
4. Generate comparison reports
5. Track performance regression in CI (optional)

---

## Implementation Order

### Phase 1: Critical Performance Wins (Week 1)
1. **Task 1**: Remove glFlush() - IMMEDIATE high impact
2. **Task 2**: Implement buffer orphaning/persistent mapping
3. **Task 8**: Add basic performance metrics (to measure improvements)

**Expected Result**: 50-70% performance improvement

### Phase 2: State Management (Week 2)
4. **Task 3**: Comprehensive state caching
5. **Task 4**: Migrate to UBOs

**Expected Result**: Additional 15-25% improvement

### Phase 3: Fine-Tuning (Week 3)
6. **Task 5**: Sampler state caching
7. **Task 6**: Already covered in Task 3
8. **Task 7**: Framebuffer operation optimization

**Expected Result**: Additional 5-10% improvement

### Phase 4: Validation (Week 4)
9. **Task 9**: Comprehensive benchmarking
10. Performance regression testing
11. Edge case validation

**Expected Result**: OpenGL within 5-10% of DirectX11 performance

---

## Risk Mitigation

### Compatibility Risks
- **Risk**: Older OpenGL versions may not support all features
- **Mitigation**: Runtime feature detection with fallbacks
- **Example**:
```cpp
bool GfxRenderingAPIOGL::SupportsPersistentMapping() {
    return GLAD_GL_ARB_buffer_storage ||
           (GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 4));
}

void GfxRenderingAPIOGL::InitBuffers() {
    if (SupportsPersistentMapping()) {
        InitPersistentMappedBuffers();
    } else {
        InitOrphaningBuffers();
    }
}
```

### Regression Risks
- **Risk**: Optimizations may introduce rendering artifacts
- **Mitigation**:
  - Keep original code paths behind compile-time flags during development
  - Extensive visual regression testing
  - Frame capture comparison (RenderDoc)

```cpp
#define USE_OPTIMIZED_BUFFERS 1

void GfxRenderingAPIOGL::DrawTriangles(...) {
#if USE_OPTIMIZED_BUFFERS
    DrawTrianglesOptimized(...);
#else
    DrawTrianglesLegacy(...);
#endif
}
```

### Platform-Specific Risks
- **Risk**: Driver behavior varies (NVIDIA vs AMD vs Intel)
- **Mitigation**:
  - Test on all major GPU vendors
  - Add driver-specific workarounds if needed
  - Document known issues per vendor

---

## Success Criteria

### Performance Targets
- **Minimum**: OpenGL within 20% of DirectX11 frame time
- **Target**: OpenGL within 10% of DirectX11 frame time
- **Stretch**: OpenGL matches or exceeds DirectX11 (may happen on certain drivers)

### Quality Targets
- Zero rendering regressions
- All existing tests pass
- No new crashes or stability issues

### Code Quality Targets
- Maintain code readability
- Add comments explaining optimization rationale
- Keep fallback paths for compatibility

---

## Testing Checklist

### Functional Testing
- [ ] All existing rendering features work correctly
- [ ] Transparency rendering unchanged
- [ ] Framebuffer effects work (post-processing, etc.)
- [ ] Multiple windows/contexts supported
- [ ] Shader compilation successful for all shaders

### Performance Testing
- [ ] Frame time improved by at least 50%
- [ ] GPU utilization increased
- [ ] No CPU-GPU bubbles in profiler
- [ ] Memory usage acceptable
- [ ] No performance regressions in DirectX11 backend

### Compatibility Testing
- [ ] OpenGL 3.3 minimum version works
- [ ] OpenGL 4.4+ features properly detected
- [ ] Tested on NVIDIA, AMD, Intel GPUs
- [ ] Tested on Windows, Linux, macOS
- [ ] Older driver versions tested

### Edge Case Testing
- [ ] Resizing window during rendering
- [ ] Alt-tab / focus loss handling
- [ ] Very large textures
- [ ] High polygon count scenes
- [ ] Shader compilation errors handled gracefully

---

## Additional Optimizations (Future Work)

### Multi-threaded Command Generation
- Generate draw commands on separate thread
- Submit to GPU on render thread
- Requires careful synchronization

### Bindless Textures (ARB_bindless_texture)
- Eliminate texture binding overhead entirely
- Pass texture handles directly to shaders
- Requires modern GPU and drivers

### Multi-Draw Indirect
- Batch multiple draw calls into single GPU command
- Reduce CPU overhead dramatically
- Requires OpenGL 4.3+

### Geometry Instancing
- Reduce draw calls for repeated objects
- Requires scene graph changes

---

## Monitoring & Metrics

### Key Performance Indicators (KPIs)
Track these metrics before/after optimization:

1. **Frame Time (ms)** - Primary metric
2. **GPU Time (ms)** - Actual GPU work
3. **CPU Time (ms)** - CPU overhead
4. **Draw Calls / Frame** - Should remain constant
5. **State Changes / Frame** - Should decrease significantly
6. **Buffer Uploads / Frame** - Should remain constant
7. **Memory Usage (MB)** - Should remain similar

### Continuous Monitoring
```cpp
// Add to logging system
if (mFrameCount % 300 == 0) { // Every 5 seconds at 60fps
    auto stats = mMetrics.GetAverageStats();
    SPDLOG_INFO("OpenGL Performance: {:.2f}ms frame, {} draws, {} state changes",
                stats.frameTimeMs, stats.drawCalls, stats.stateChanges);
}
```

---

## Conclusion

This plan addresses the root causes of OpenGL's performance deficit compared to DirectX11:

1. **Synchronization bottlenecks** - Removed via glFlush() elimination
2. **Buffer management** - Fixed via orphaning/persistent mapping
3. **State thrashing** - Minimized via comprehensive caching
4. **Inefficient uniform updates** - Batched via UBOs

By implementing these changes in phases with proper testing and metrics, the OpenGL backend should achieve performance parity with DirectX11, providing users with a high-quality rendering experience regardless of their chosen API.

**Estimated Total Impact**: 70-100% performance improvement (making OpenGL comparable to or better than current DirectX11 implementation on most systems)
