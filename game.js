// miniprogram/game.js  —— 仿 Demo/PxKitDemo/game.js 的结构

// 小游戏侧让未改过的 emcc 胶水能跑起来的最小兼容
globalThis.WebAssembly = WXWebAssembly;
if (typeof WXWebAssembly.RuntimeError !== 'function') {
    WXWebAssembly.RuntimeError = Error;
}

const RTTModule      = require('./build/webglbind.js');
const TriangleModule = require('./build/triangle.js');

const canvas = wx.createCanvas();
const gl = canvas.getContext('webgl');

let RenderModule   = null;   // 模块 A：FBO + blit
let TriModule      = null;   // 模块 B：画三角形
let isRTTModuleInit      = false;
let isTriangleModuleInit = false;
let currentContextHandle = 0;

const attrs = {
    alpha: true,
    depth: true,
    stencil: true,
    antialias: true,
    enableExtensionsByDefault: true,
    majorVersion: 1
};

// emcc 胶水在小游戏里走 instantiateWasm 钩子按路径加载 .wasm
function instantiateWasmHook(wasmPath) {
    return function (imports, successCallback) {
        WXWebAssembly.instantiate(wasmPath, imports)
            .then(res => successCallback(res.instance, res.module))
            .catch(err => console.error('[WX] instantiate failed:', wasmPath, err));
        return {};
    };
}

RTTModule({
    canvas: canvas,
    instantiateWasm: instantiateWasmHook('build/webglbind.wasm')
}).then(function (module) {
    RenderModule = module;
    const ctxHandle = RenderModule.GL.registerContext(gl, attrs);
    RenderModule.GL.makeContextCurrent(ctxHandle);
    currentContextHandle = ctxHandle;

    RenderModule.ccall('setCanvasSize', null, ['number', 'number'],
                       [canvas.width, canvas.height]);
    RenderModule.ccall('startWithWebGLContext', null, ['number'], [ctxHandle]);
    isRTTModuleInit = true;
});

TriangleModule({
    canvas: canvas,
    instantiateWasm: instantiateWasmHook('build/triangle.wasm')
}).then(function (module) {
    TriModule = module;
    const ctxHandle = TriModule.GL.registerContext(gl, attrs);
    TriModule.GL.makeContextCurrent(ctxHandle);
    TriModule.ccall('initTriangle', null, ['number'], [ctxHandle]);
    isTriangleModuleInit = true;
});

function renderLoop() {
    gl.viewport(0, 0, canvas.width, canvas.height);

    if (isRTTModuleInit && isTriangleModuleInit) {
        RenderModule.ccall('renderToFramebuffer', null, [], []);

        RenderModule.ccall('bindFramebuffer',   null, [], []);
        TriModule   .ccall('drawTriangle',      null, [], []);
        RenderModule.ccall('unbindFramebuffer', null, [], []);

        RenderModule.ccall('renderToFullScreen', null, [], []);
    }

    requestAnimationFrame(renderLoop);
}

renderLoop();
