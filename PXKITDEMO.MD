# Dual-WASM Shared-GL Demo（脱敏版 PxKitDemo）

## 1. 这是什么

一个**最小可运行**的 Web 演示，用来验证这样一个架构：

> **两个由 Emscripten 独立编译的 WASM 模块，共用同一张 `<canvas>` 的 WebGL context，以「离屏渲染 + 全屏 blit」的方式协作成帧。**

本仓库是在脱敏后公开的 Demo，**把原工程里"某跨平台渲染引擎"替换成一个最简的三角形渲染模块**。
核心验证点（共享 context、RTT、双 WASM、MODULARIZE、微信小游戏胶水改造思路等）都保留在这里，但不涉及任何专有渲染库的代码与符号。

---

## 2. 目录结构

```
.
├── src/
│   ├── webglbind.cpp          # 模块 A 入口：接收 ctx handle + 驱动 RTT
│   ├── RenderToTexture.cpp    # 模块 A 内部：FBO + 全屏 quad blit
│   ├── RenderToTexture.h
│   └── triangle.cpp           # 模块 B：占据原引擎位置的最简绘制器
├── web/
│   └── index.html             # 双模块 Web 演示页
├── build/                     # emcc 产物（编译后生成）
├── Reference/                 # 原始参考工程（只读，勿动）
├── build_all.bat              # 一键编译两个模块
└── PXKITDEMO.md               # 你正在读的这份文档
```

---

## 3. 运行方式

### 3.1 准备 Emscripten 环境

在**同一个 cmd 窗口**里先加载 emsdk，让 `emcc` 可用：

```bat
call D:\your\path\emsdk\emsdk_env.bat
```

> PowerShell 用户请改用 `. D:\your\path\emsdk\emsdk_env.ps1`（前面有点号+空格表示「点源」）。
> 环境只存在于当前这个 shell 进程里，换窗口要重新加载。

### 3.2 编译

```bat
build_all.bat
```

产物：

```
build\webglbind.js   build\webglbind.wasm      # 模块 A: RTTModule
build\triangle.js    build\triangle.wasm       # 模块 B: TriangleModule
```

### 3.3 起静态服务器浏览

任何静态文件服务器都可以，例如：

```bat
npx serve .
```

然后浏览器打开 `http://localhost:3000/web/index.html`（端口以实际为准）。
你应看到画布里一个持续变色的三角形，它是**被渲染到 FBO、再由全屏 quad 采样到屏幕**的。

---

## 4. 架构与数据流

```
      ┌─────────────────────────────────────────────────────┐
      │  JS 侧 (<canvas>)                                   │
      │                                                     │
      │   gl = canvas.getContext('webgl')                   │
      │                                                     │
      │   ctxHandle = RTTModule.GL.registerContext(gl,...)  │
      │              TriangleModule.GL.registerContext(gl,..)│
      │   ──> 两个模块 makeContextCurrent 到同一个 handle   │
      └───────────────┬─────────────────────────────────────┘
                      │
           ┌──────────▼───────────┐        ┌───────────────────────┐
           │  RTTModule (A)       │        │  TriangleModule (B)   │
           │                      │        │                       │
           │  initRenderToTexture │        │  initTriangle(ctx)    │
           │  FBO + texture       │        │  program + VBO        │
           │  fullscreen blit     │        │  drawTriangle()       │
           └──────────┬───────────┘        └──────────┬────────────┘
                      │                               │
                      │      ┌────────────────────────┘
                      ▼      ▼
          每帧顺序调用 (index.html 的 loop)：
             A.renderToFramebuffer()    // 清 FBO
             A.bindFramebuffer()        // 把 FBO 设为 current
             B.drawTriangle()           // 画进同一个 FBO
             A.unbindFramebuffer()      // 解绑
             A.renderToFullScreen()     // 采样 FBO → 上屏
```

### 关键点：为什么两个模块能共享 context

- Emscripten 的 `Module.GL` 背后是**同一个 WebGL context 句柄表**（进程级，沙箱内）。
- 不同模块各自 `registerContext(gl, ...)` 登记时，都是注册**同一个 JS `WebGLRenderingContext` 对象**，`makeContextCurrent` 之后底层绑定到同一块 GL 状态。
- 因此**纹理/FBO/Shader 这些对象在两个模块之间是可直接引用的**——「共享 Texture」这条路（类似 D3D11 的 surface 共享）在这里既无意义、也不可行；正确做法就是「绑同一个 context」。

> WebGL 与 OpenGL ES 的版本对应：WebGL1 ↔ GLES2，WebGL2 ↔ GLES3。两边一致才能共享。

---

## 5. 关键 emcc 参数说明

| 参数 | 作用 |
|------|------|
| `-s MODULARIZE=1` + `EXPORT_NAME="XxxModule"` | 多 WASM 模块共存时不污染全局，避免后装载的 `Module` 覆盖前一个 |
| `-s GL_PREINITIALIZED_CONTEXT=1` | 不让 Emscripten 自己创建 GL context，而是等 JS 传句柄进来 |
| `-s DYNAMIC_EXECUTION=0` | 关闭 `eval` / `new Function`；小游戏等强沙箱环境需要 |
| `-s ENVIRONMENT=web,shell` | 同时兼顾浏览器环境和「非浏览器」运行时（如小游戏真机） |
| `-s EXPORTED_RUNTIME_METHODS="[\"ccall\",\"cwrap\",\"GL\"]"` | JS 侧要用 `Module.ccall(...)` 和 `Module.GL.registerContext(...)` |
| `-s EXPORTED_FUNCTIONS=[...]` | 只导出真正需要跨语言调用的 `_foo` 符号，避免被 DCE 干掉 |

> `EXPORTED_FUNCTIONS` 里的符号名要以下划线开头（C 符号的 Emscripten 约定）。

---

## 6. 移植到微信小游戏的核心差异（仅做笔记，不在本仓库演示）

本仓库只发 Web 端 Demo，但原工程当初移植到微信小游戏时踩过的点值得留个档：

1. **`WebAssembly` → `WXWebAssembly`**：小游戏运行时把标准 `WebAssembly` 封装成了 `WXWebAssembly`，并且不支持 `WebAssembly.RuntimeError` 这类子类型。需要在 emcc 生成的胶水 JS 里做文本替换，并把 `RuntimeError` 替换成普通 `Error`。
2. **`instantiateArrayBuffer` 改写**：不再走 `fetch(wasm binary)` → `instantiate(binary, imports)` 的路径；而是直接 `WXWebAssembly.instantiate("path/to/xxx.wasm.br", imports)`，让运行时自己负责加载与解压。
3. **brotli 压缩产物**：真机走 `.wasm.br`，体积能降到原始的 ~3–5%。
4. **无 DOM**：不能 `document.getElementById('canvas')`；通过 `wx.createCanvas()` 拿到 canvas，然后同样走 `gl.getContext` + `Module.GL.registerContext`。
5. **文件系统**：小游戏没有标准 FS。如果 WASM 侧有读文件需求，常见做法是 C++ 侧提供 `allocate / setByte(i) / complete` 三个接口，JS 端用 `wx.getFileSystemManager()` 读到内容后**分块回灌**进 WASM 内存；或者直接在 JS 侧一次性 `HEAPU8.set(...)`。
6. **多模块兼容**：两个模块若都不开 `MODULARIZE`，后装载的会把全局 `Module` 覆盖掉，**必开**。

以上**只是移植要点清单**——本仓库不包含任何小游戏侧代码。

---

## 7. 开发心得（精简版）

- **分阶段验证**：先在浏览器里跑通共享 context 与 RTT，再考虑目标平台（小游戏 / 其他壳）。不要一步到位。
- **AI 协作边界**：AI 擅长把你已经明确方向的方案实现出来；不擅长帮你决定"OpenGL ES 和 WebGL 到底能不能共享 texture"这类原理问题。原理先由人工确定，再交给 AI 实现。
- **最小可复现**：每改一个参数就 build 一次、看一次效果。比同时改 3 处再一起排错省一个数量级的时间。

---

## 8. 参考

- `Reference/`：原始（未脱敏）工程代码，仅供对照。
- `Emscripten微信小游戏适配经验总结.md`：更完整的历史笔记，**内部文档，对外分享前再脱敏一次**。
