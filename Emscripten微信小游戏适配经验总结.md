# Emscripten 编译 C++ 渲染库迁移至微信小游戏 —— 经验总结

> 适用场景：将使用 OpenGL ES 的 C++ 跨平台渲染库，通过 Emscripten 编译为 WASM，先在 Web 浏览器中验证，再迁移到微信小游戏平台运行。

---

## 一、整体策略

**分阶段验证，先 Web 后小游戏。**

```
阶段1: 基础渲染环境搭建（WebGL Demo + OpenGL ES Demo）
       ↓
阶段2: 双渲染管线集成（WebGL 与 OpenGL ES 共享同一 Canvas）
       ↓
阶段3: 离屏渲染 RTT（FBO 创建 + 全屏输出）
       ↓
阶段4: 接入目标 C++ 渲染库
       ↓
阶段5: 微信小游戏平台适配迁移
```

在 Web 端把各阶段跑通后，再做小游戏适配，可以有效隔离平台差异造成的干扰，每个环节都可以独立验证。

---

## 二、Emscripten 编译关键参数

### 基础编译命令模板

```bash
emcc your_cpp.cpp RenderToTexture.cpp \
  -std=c++11 \
  -s GL_PREINITIALIZED_CONTEXT=1 \   # 关键：告诉 Emscripten 不要自己创建 GL 上下文
  -s WASM=1 -O3 \
  -s EXPORTED_FUNCTIONS='["_startWithWebGLContext","_renderToFramebuffer","_renderToFullScreen"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","GL"]' \
  -s ENVIRONMENT='web,shell' \       # 同时支持浏览器和微信小游戏 shell 环境
  -s DYNAMIC_EXECUTION=0 \           # 小游戏中禁止 eval，必须加
  -s MODULARIZE=1 \                  # 生成模块化封装，避免污染全局
  -s EXPORT_NAME="YourModule" \
  -o output.js
```

### 参数说明

| 参数 | 作用 | 备注 |
|------|------|------|
| `GL_PREINITIALIZED_CONTEXT=1` | 禁止 Emscripten 自行创建 canvas/context | 必须，否则无法共享 WebGL 上下文 |
| `ENVIRONMENT='web,shell'` | 同时支持两种环境 | 微信开发者工具是 web，真机是 shell |
| `DYNAMIC_EXECUTION=0` | 禁止 eval/Function | 微信小游戏安全限制，必须加 |
| `MODULARIZE=1` | 模块化封装 | 防止全局变量冲突 |

---

## 三、双渲染管线共享 WebGL 上下文

### 核心思路

WebGL（JS 端）和 OpenGL ES（C++ 端）共享**同一个 Canvas 的 GL 上下文**，渲染到同一张 RenderTarget。

### C++ 端：接收 JS 传来的 context handle

```cpp
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void startWithWebGLContext(int jsContextHandle) {
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE oglContextHandle =
            (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)jsContextHandle;
        EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(oglContextHandle);
        if (res != EMSCRIPTEN_RESULT_SUCCESS) {
            emscripten_log(EM_LOG_ERROR, "设置 WebGL 上下文失败，错误码: %d", res);
            return;
        }
        // 此后就可以调 OpenGL ES API 了
        initRendering();
    }
}
```

### JS 端：注册并传递 context

```javascript
const canvas = document.getElementById('canvas');  // 浏览器
// const canvas = wx.createCanvas();                // 小游戏

const gl = canvas.getContext('webgl');

YourModule({ canvas }).then(module => {
    const attrs = {
        alpha: true, depth: true, stencil: true,
        antialias: true, enableExtensionsByDefault: true,
        majorVersion: 1
    };

    // 把 JS 的 WebGL context 注册到 Emscripten GL 系统
    const ctxHandle = module.GL.registerContext(gl, attrs);
    module.GL.makeContextCurrent(ctxHandle);

    // 将 handle 传给 C++，C++ 端就能操作同一个 GL 上下文
    module.ccall('startWithWebGLContext', null, ['number'], [ctxHandle]);

    startRenderLoop(module);
});
```

### 渲染循环

```javascript
function startRenderLoop(module) {
    function renderLoop() {
        module.ccall('renderToFramebuffer', null, [], []);  // C++ 渲染到 FBO
        // 此处可插入 JS WebGL 渲染（共享同一 FBO）
        module.ccall('renderToFullScreen', null, [], []);   // FBO 输出到屏幕
        requestAnimationFrame(renderLoop);
    }
    renderLoop();
}
```

---

## 四、离屏渲染（RTT）C++ 实现要点

```cpp
// RenderToTexture.h
void initRenderToTexture(int width, int height);
void renderRTTOncePerFrame(int width, int height);
void renderToScreen(int width, int height);
GLuint getFrameBuffer();
```

```cpp
// 初始化：创建 FBO + 纹理
void initRenderToTexture(int width, int height) {
    glGenTextures(1, &renderTexture);
    glBindTexture(GL_TEXTURE_2D, renderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, renderTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 初始化全屏四边形 Shader（用于 FBO 输出到屏幕）
    fullscreenProgram = createProgram(fullscreenVS, fullscreenFS);
}

// 每帧：渲染到 FBO
void renderRTTOncePerFrame(int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glClearColor(0.2f, 0.4f, 0.7f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // ... 在此绘制内容
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 全屏输出
void renderToScreen(int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(fullscreenProgram);
    // 绑定 renderTexture 并绘制全屏四边形
}
```

---

## 五、微信小游戏平台适配

### 5.1 WASM 加载三处必改

Emscripten 生成的胶水层 JS 文件需要手动（或脚本）修改以下内容：

| # | 问题 | 修改方法 |
|---|------|---------|
| 1 | 微信封装了 `WXWebAssembly`，没有全局 `WebAssembly` | 全局替换：`WebAssembly` → `WXWebAssembly` |
| 2 | `WXWebAssembly.RuntimeError` 未实现 | 搜索 `WebAssembly.RuntimeError`，替换为 `Error` |
| 3 | `instantiateArrayBuffer` 使用 `fetch` 加载 wasm，小游戏没有 `fetch` | 参考下方代码，改用 `WXWebAssembly.instantiate` 直接实例化 |

```javascript
// 修改后的 instantiateArrayBuffer（小游戏版）
function instantiateArrayBuffer(binaryFile, imports, receiver) {
    // 用 wx.getFileSystemManager 读文件，再直接 instantiate
    const fs = wx.getFileSystemManager();
    try {
        const wasmBinary = fs.readFileSync(binaryFile);
        WXWebAssembly.instantiate(wasmBinary, imports).then(receiver);
    } catch(e) {
        console.error('wasm 加载失败:', e);
    }
}
```

**或者**用 `wasmBinary` 参数绕过整个加载流程（最稳妥）：

```javascript
const fs = wx.getFileSystemManager();
const wasmBinary = fs.readFileSync('your_module.wasm');

YourModule({
    canvas: canvas,
    wasmBinary: wasmBinary,          // 直接传二进制，跳过 fetch
    locateFile: (path) => path,      // wasm 路径配置
}).then(module => { ... });
```

### 5.2 locateFile 配置

Emscripten 通过 `locateFile(path)` 决定 wasm 文件路径，必须手动配置：

```javascript
YourModule({
    canvas: canvas,
    locateFile: (path) => {
        // path 是 "xxx.wasm" 这样的文件名
        // 返回实际路径，小游戏通常放在项目根目录
        return path;
        // 或者放在子目录：return `wasm/${path}`;
    }
});
```

### 5.3 document 适配层

Emscripten 内部会调用 `document.querySelector('#canvas')` 来查找画布元素，小游戏没有 `document`，需要 mock：

```javascript
// adapter/document.js
class Document {
    constructor() {
        this._canvas = null;
    }
    querySelector(query) {
        if (query === '#canvas' || query === 'canvas') return this._canvas;
        return null;
    }
    createElement(tag) {
        if (tag === 'canvas') {
            const c = wx.createCanvas();
            if (!this._canvas) this._canvas = c;
            return c;
        }
        return {};
    }
    getElementById(id) {
        if (id === 'canvas') return this._canvas;
        return null;
    }
    addEventListener() {}
    removeEventListener() {}
    get body() { return { appendChild() {}, addEventListener() {} }; }
    get head() { return { appendChild() {} }; }
}

module.exports = new Document();
```

在 `game.js` 入口注入：

```javascript
const _document = require('./adapter/document.js');
const canvas = wx.createCanvas();
_document._canvas = canvas;

if (typeof document === 'undefined') {
    globalThis.document = _document;
}
// 小游戏真机环境还需要：
if (typeof WXWebAssembly !== 'undefined') {
    globalThis.WebAssembly = WXWebAssembly;
    if (typeof WXWebAssembly.RuntimeError !== 'function') {
        WXWebAssembly.RuntimeError = Error;
    }
}
```

### 5.4 findEventTarget 修改

胶水层里的 `findEventTarget` 函数默认调用 `document.querySelector(target)`，即便有了 adapter，也推荐直接改为返回 `Module['canvas']`，更稳定：

```javascript
// 在胶水层 JS 里找到 findEventTarget，改为：
var findEventTarget = (target) => {
    target = maybeCStringToJsString(target);
    return specialHTMLTargets[target] || Module['canvas'] || null;
};
```

---

## 六、C++ 端通过 EM_ASM 调用小游戏文件接口

小游戏没有标准文件系统，C++ 读文件需要通过内嵌 JS 调用 `wx.getFileSystemManager`：

```cpp
int loadSuccess = EM_ASM_INT({
    const viewId    = $0;
    const urlPtr    = $1;
    const onLoaded  = $2;
    const isAsync   = $3;

    const fs     = wx.getFileSystemManager();
    const urlStr = UTF8ToString(urlPtr);

    if (isAsync) {
        fs.readFile({
            filePath: urlStr,
            encoding: 'binary',
            success(res) {
                const data    = new Uint8Array(res.data);
                const dataPtr = _malloc(data.length);
                HEAPU8.set(data, dataPtr);
                wasmTable.get(onLoaded)(viewId, urlPtr, dataPtr, data.length);
            },
            fail(err) {
                wasmTable.get(onLoaded)(viewId, urlPtr, 0, 0);
            }
        });
        return 0;
    } else {
        try {
            const res     = fs.readFileSync(urlStr, 'binary');
            const data    = new Uint8Array(res);
            const dataPtr = _malloc(data.length);
            HEAPU8.set(data, dataPtr);
            wasmTable.get(onLoaded)(viewId, urlPtr, dataPtr, data.length);
            return 1;
        } catch(e) {
            return 0;
        }
    }
}, viewID, url, onFileLoaded, async);
```

---

## 七、包体积优化

原始 WASM 体积可能达到几十 MB，微信小游戏主包限制 4MB，需压缩：

**第一步：编译时优化**

```cmake
target_compile_options(your_lib PRIVATE -g0)  # 剥离调试符号
target_compile_options(your_lib PRIVATE -Oz)  # 最大尺寸优化
```

经验数据：65MB → 8MB

**第二步：brotli 压缩**

```bash
brotli -q 11 your_module.wasm -o your_module.wasm.br
```

经验数据：8MB → 1.9MB，可放入主包。

微信小游戏会自动解压 `.wasm.br`，JS 端路径填写带 `.br` 后缀的文件名即可。

---

## 八、小游戏渲染循环 + 生命周期管理

```javascript
function startRenderLoop(module, renderer) {
    let isRendering = true;

    wx.onShow(() => { isRendering = true; requestAnimationFrame(renderFrame); });
    wx.onHide(() => { isRendering = false; });

    function renderFrame() {
        if (!isRendering) return;
        if (module && renderer) {
            module.ccall('renderToFramebuffer', null, [], []);
            module.ccall('renderToFullScreen', null, [], []);
        }
        requestAnimationFrame(renderFrame);
    }
    renderFrame();
}
```

---

## 九、Web 与小游戏差异速查表

| 能力 | Web 浏览器 | 微信小游戏 | 适配方法 |
|------|-----------|-----------|---------|
| `WebAssembly` | 原生支持 | `WXWebAssembly` | 全局替换 |
| `WebAssembly.RuntimeError` | 有 | 无 | 替换为 `Error` |
| `fetch` wasm 文件 | 支持 | 不支持 | 用 `wx.getFileSystemManager().readFileSync` |
| `document.querySelector` | 原生 | 无 | mock Document 类 |
| `canvas` 获取 | `document.getElementById` | `wx.createCanvas()` | adapter 统一封装 |
| 文件系统 | 标准 FS | `wx.getFileSystemManager` | `EM_ASM` 内嵌 JS 调用 |
| 渲染循环 | `requestAnimationFrame` | `requestAnimationFrame`（相同） | 加 `wx.onShow/onHide` 生命周期 |
| eval/Function | 支持 | 禁止 | 编译加 `-s DYNAMIC_EXECUTION=0` |

---

## 十、典型问题与解决方案

**Q: `both async and sync fetching of the wasm failed`**

原因：Emscripten 默认用 `fetch` 加载 wasm，小游戏没有 `fetch`。
解决：用 `wasmBinary` 参数或修改 `instantiateArrayBuffer` 改用 `WXWebAssembly.instantiate`。

**Q: `no such file or directory wasm/webglbind.wasm.br`**

原因：`locateFile` 没有配置，默认路径带了 `wasm/` 前缀。
解决：初始化时传 `locateFile: (path) => path`，或者实际创建 `wasm/` 子目录。

**Q: `WXWebAssembly.RuntimeError is not a constructor`**

原因：微信未实现该类。
解决：启动时加 `if (!WXWebAssembly.RuntimeError) WXWebAssembly.RuntimeError = Error;`

**Q: `emscripten_set_canvas_element_size` 返回错误**

原因：`findEventTarget` 找不到 canvas 元素（document.querySelector 失败或返回 null）。
解决：参见第五节 5.3/5.4，修改 `findEventTarget` 直接返回 `Module['canvas']`。

---

## 十一、遗漏补充：从 GameEngineSimulator.js 实际代码中发现的细节

### 11.1 `instantiateArrayBuffer` 的改法（实际做法）

KM 文档里只说"修改 instantiateArrayBuffer"，实际看代码，这个项目的做法是**直接把原逻辑注释掉，硬编码一行 `WXWebAssembly.instantiate` 调用**：

```javascript
// 改前（Emscripten 默认）
async function instantiateArrayBuffer(binaryFile, imports) {
    var binary = await getWasmBinary(binaryFile);
    return WebAssembly.instantiate(binary, imports);
}

// 改后（实际做法，硬编码路径，跳过所有 fetch 逻辑）
async function instantiateArrayBuffer(binaryFile, imports) {
    try {
        /* var binary = await getWasmBinary(binaryFile); */
        var instance = await WebAssembly.instantiate("html/libs/GameEngineSimulator.wasm", imports);
        return instance;
    } catch (reason) {
        err(`failed to asynchronously prepare wasm: ${reason}`);
        abort(reason);
    }
}
```

注意：这里的 `WebAssembly` 因为前面已经做了 `globalThis.WebAssembly = WXWebAssembly`，所以这里写 `WebAssembly` 即实际调用的是 `WXWebAssembly.instantiate`。**wasm 路径是硬编码的相对路径**，不依赖 `locateFile`。

### 11.2 `cppModule(gameEngineModule)` 的传参模式

这个项目的 `cppModule` 初始化方式是把一个**空对象 `gameEngineModule = {}`** 传进去，而不是传 `{ canvas }`：

```javascript
let gameEngineModule = {}

// 小游戏环境
globalThis.WebAssembly = WXWebAssembly;
cppModule = require('./libs/GameEngineSimulator.js');

// 初始化时传空对象
cppModule(gameEngineModule).then(function(Module) {
    // Module 就是 gameEngineModule，被填充了所有 API
    const ctxHandle = Module.GL.registerContext(webGLContext, attrs);
    Module.GL.makeContextCurrent(ctxHandle);
    cppEngineInitialized = true;
});
```

**区别**：不在初始化时传 canvas，而是之后通过 `Module.GL.registerContext(gl, attrs)` 绑定已经外部创建好的 WebGL context。这种模式更灵活，canvas 生命周期由外部完全控制。

### 11.3 WebGL Context 需要加 `preserveDrawingBuffer: true`

注意获取 WebGL context 时的参数：

```javascript
webGLContext = webGLCanvas.getContext('webgl', { preserveDrawingBuffer: true });
```

`preserveDrawingBuffer: true` 的作用：防止浏览器在每帧结束后自动清除绘制缓冲区。如果不加这个，在做多帧累积渲染或截图功能时，缓冲区会被自动清空，导致画面闪烁或截图为空白。

### 11.4 document 适配层需要处理触摸事件映射

你的 KM 文档里的 `document.js` 只做了 `querySelector`/`createElement`，实际上完整的适配层还需要**把微信触摸事件（`wx.onTouchStart` 等）桥接成标准鼠标事件**，因为 Emscripten 内部的事件系统依赖 `mousedown`/`mousemove`/`mouseup`：

```javascript
_initWxMouseEvents() {
    if (!this._wxMouseBound && typeof wx !== 'undefined') {
        wx.onTouchStart(event => {
            const touch = event.touches[0];
            const mouseEvent = {
                type: 'mousedown',
                clientX: touch.clientX,
                clientY: touch.clientY,
                target: this,
                preventDefault: () => {},
                stopPropagation: () => {},
            };
            this._mouseDownListeners.forEach(listener => listener(mouseEvent));
        });
        // 同理处理 onTouchMove → mousemove，onTouchEnd → mouseup
        this._wxMouseBound = true;
    }
}
```

同时 `document.addEventListener` 也需要实现，用来接收 Emscripten 注册的事件监听器：

```javascript
addEventListener(type, listener) {
    if (type === 'mousedown') { this._mouseDownListeners.push(listener); return; }
    if (type === 'mousemove') { this._mouseMoveListeners.push(listener); return; }
    if (type === 'mouseup')   { this._mouseUpListeners.push(listener); return; }
    // 其他事件类型按需处理
}
```

**结论：如果你的 C++ 库有交互逻辑（点击/拖拽），必须加这个桥接；纯渲染无交互可以不加。**

### 11.5 画布尺寸需要显式通知 C++ 层

画布尺寸变化（横竖屏切换、设备适配）时，不能只改 JS 侧的 canvas.width/height，还需要通过 `ccall` 告知 C++ 层，否则 C++ 的 viewport 和渲染区域不会更新：

```javascript
function resetCanvasSize() {
    const { width, height } = GetDeviceScreenSize();
    canvas.width = width;
    canvas.height = height;
    webGLContext.viewport(0, 0, width, height);

    // 必须同步通知 C++ 层
    if (cppEngineInitialized) {
        gameEngineModule.ccall('SetCanvasSize', 'number', ['number', 'number'], [width, height]);
    }
}
// 屏幕旋转时调用
wx.onDeviceOrientationChange(() => resetCanvasSize());
```

---

## 十二、推荐项目结构

```
your-wx-minigame/
├── README.md
├── build.sh                   # emcc 编译脚本
│
├── cpp/                       # C++ 源码
│   ├── main.cpp               # EMSCRIPTEN_KEEPALIVE 导出函数
│   ├── RenderToTexture.cpp    # RTT 实现
│   └── RenderToTexture.h
│
├── web/                       # Web 端验证（浏览器直接打开）
│   ├── index.html
│   ├── output.js              # emcc 编译产物
│   └── output.wasm
│
└── wx-minigame/               # 微信小游戏端
    ├── game.js                # 入口（含适配注入）
    ├── game.json
    ├── output.js              # 同一份编译产物（需做三处胶水层修改）
    ├── output.wasm.br         # brotli 压缩后的 wasm
    └── adapter/
        └── document.js        # mock document
```
