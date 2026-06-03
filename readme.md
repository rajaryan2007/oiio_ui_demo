# Ultra-Lightweight OpenImageIO Interactive App Shell

A high-performance, cross-platform interactive image viewer prototype implementing a highly direct, modern UI stack. This repository serves as a proof-of-concept demonstrating how to replace legacy, high-maintenance UI architectures with an ultra-compact footprint (~280 lines of hand-crafted C++20).

![Application Viewport Prototype](assets/demo.png)

## Demo

<video> src="assets/untitled.mp4" controls="controls" style="max-width: 100%;">
  Your browser does not support the video tag.
</video>

## Architectural Thesis

This implementation directly validates two key architectural principles for core graphics tooling:
1. **Zero UI Bloat:** By utilizing immediate-mode UI execution, the entire runtime shell, window mapping, and custom pipeline controls are managed cleanly without thousands of lines of automated or heavily duplicated layout boilerplate.
2. **Host-Side Execution Sufficiency:** Interactive image manipulation parameters (e.g., real-time exposure processing) are calculated natively on the CPU using `OIIO::ImageBufAlgo::mul`. By streaming updated frames via in-place memory writes (`glTexSubImage2D`), the application maintains a locked, stutter-free **60 FPS** on standard consumer hardware, proving that complex, platform-specific GLSL shader pipelines are unnecessary for core tool manipulation tasks.

---

## Technology Stack

* **Core Language Standard:** Modern C++20 / ISO C
* **Media Infrastructure:** OpenImageIO (OIIO) v3.x (`ImageBuf`, `ImageBufAlgo`)
* **Windowing & Native Surface Abstraction:** SDL3 (Wayland / X11 / Win32 / AppKit)
* **Graphics API Function Loader:** Glad (OpenGL 4.1 Core Profile)
* **Immediate-Mode UI Layer:** Dear ImGui (Modular Vendor Compilation)

---

## Technical Implementation Highlights

### 1. In-Place VRAM Streaming Optimization
To prevent hardware handle leaks and memory fragmentation, texture updates bypass complete re-generation loops. The application allocates a single GPU texture target via `glGenTextures` upon asset resolution changes and streams real-time CPU-calculated pixel modifications utilizing optimized spatial sub-allocations:
```cpp
glBindTexture(GL_TEXTURE_2D, gl_image_texture);
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, gl_fmt, GL_UNSIGNED_BYTE, update_pixels.data());
```
