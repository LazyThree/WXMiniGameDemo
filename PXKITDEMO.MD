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
├── miniprogram/               # 微信小游戏端（见第 6 节）
│   ├── game.js
│   ├── game.json
│   ├── project.config.json
│   └── wasm/                  # 由 build_all.bat 自动同步 build/ 产物
├── build/                     # emcc 产物（编译后生成）
├── Reference/                 # 原始参考工程（只读，勿动）
├── build_all.bat              # 一键编译两个模块 + 同步到 miniprogram/wasm/
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

## 6. 微信小游戏端（`miniprogram/`）

仓库里直接放了一个**可导入微信开发者工具**的 `miniprogram/` 子工程，渲染效果与 Web 端一致，**C++ 侧零改动**，emcc 胶水也**不改一个字**。

### 6.1 目录

```
miniprogram/
├── game.js              # 小游戏入口（对应 web/index.html）
├── game.json
├── project.config.json
└── wasm/                # 由 build_all.bat 从 build/ 自动同步
    ├── webglbind.js / .wasm
    └── triangle.js  / .wasm
```

### 6.2 导入 & 运行

1. 先 `build_all.bat`（会顺带把产物同步到 `miniprogram/wasm/`）。
2. 打开微信开发者工具 → 导入项目 → 选中本仓库的 **`miniprogram/`** 目录 → 项目类型选 **小游戏**。
3. 模拟器里应看到背景呼吸 + 三角形变色，与 Web 端一致。

### 6.3 做了哪几件关键事（代码见 `miniprogram/game.js`）

| 差异 | 做法 |
|---|---|
| 小游戏没有标准 `WebAssembly` 全局 | `globalThis.WebAssembly = WXWebAssembly` 一行别名，胶水无需改 |
| `WXWebAssembly` 没有 `RuntimeError` 子类 | `WXWebAssembly.RuntimeError = Error` 补齐 |
| `WXWebAssembly.instantiate` 要**路径**而不是 `ArrayBuffer` | 用 emcc 官方钩子 `Module.instantiateWasm(imports, cb)` 接管加载，里面直接 `WXWebAssembly.instantiate('wasm/xxx.wasm', imports)` |
| 没有 DOM | `wx.createCanvas()` + `getContext('webgl')` |
| `requestAnimationFrame` | 用 `canvas.requestAnimationFrame(loop)` |

### 6.4 与参考工程 `post_webglbind.js` 的不同

原始 `Reference/post_webglbind.js` 是**手改胶水**（把 `WebAssembly` 文本替换为 `WXWebAssembly`、把 `instantiateArrayBuffer` 改成直接按路径加载）。每次 emcc 重编都要重新改一遍。

本仓库改用**"胶水不动 + `instantiateWasm` 钩子 + 全局别名"**的做法：
- 可随时 `build_all.bat` 重编，产物**开箱即用**；
- 同一份胶水**既能跑浏览器也能跑小游戏**（浏览器端走胶水默认路径，小游戏端走钩子）。

### 6.5 真机体积优化（可选）

本仓库为了最小闭环，**没有**做 brotli。真机发布时建议：
- 用微信开发者工具自带的 `brotli.exe` 把 `.wasm` 压成 `.wasm.br`；
- `game.js` 里 `instantiateWasm` 的路径改成 `'wasm/xxx.wasm.br'`；
- 若有大资源，可进一步启用小游戏的分包/预下载策略。

---

## 7. 开发心得（精简版）

- **分阶段验证**：先在浏览器里跑通共享 context 与 RTT，再考虑目标平台（小游戏 / 其他壳）。不要一步到位。
- **AI 协作边界**：AI 擅长把你已经明确方向的方案实现出来；不擅长帮你决定"OpenGL ES 和 WebGL 到底能不能共享 texture"这类原理问题。原理先由人工确定，再交给 AI 实现。
- **最小可复现**：每改一个参数就 build 一次、看一次效果。比同时改 3 处再一起排错省一个数量级的时间。

---

## 8. 参考

- `Reference/`：原始（未脱敏）工程代码，仅供对照。
- `Emscripten微信小游戏适配经验总结.md`：更完整的历史笔记，**内部文档，对外分享前再脱敏一次**。
