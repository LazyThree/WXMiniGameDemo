// webglbind.cpp
// 模块 A：RTT 驱动模块（脱敏版）
//   1. 从 JS 接收 EMSCRIPTEN_WEBGL_CONTEXT_HANDLE，切到同一个 WebGL context
//   2. 初始化 RenderToTexture，暴露 FBO 给"引擎模块"
//   3. 对外暴露每帧两个操作：
//        - renderToFramebuffer()：清 FBO（为引擎模块准备一张干净画布）
//        - renderToFullScreen() ：把 FBO 采样到屏幕
//   4. bindFramebuffer / unbindFramebuffer 由 RenderToTexture.cpp 导出，
//      引擎模块（同一 JS Module 作用域内）通过 ccall 调用

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <stdio.h>

#include "RenderToTexture.h"

static int s_canvasWidth  = 1024;
static int s_canvasHeight = 1024;

extern "C" {

EMSCRIPTEN_KEEPALIVE
void setCanvasSize(int w, int h) {
    s_canvasWidth  = w;
    s_canvasHeight = h;
}

EMSCRIPTEN_KEEPALIVE
void startWithWebGLContext(int jsContextHandle) {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)jsContextHandle;

    EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(ctx);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        emscripten_log(EM_LOG_ERROR,
            "[RTT] make_context_current failed: %d", res);
        return;
    }
    emscripten_log(EM_LOG_CONSOLE,
        "[RTT] got ctx handle = %d", jsContextHandle);

    initRenderToTexture(s_canvasWidth, s_canvasHeight);
}

EMSCRIPTEN_KEEPALIVE
void renderToFramebuffer() {
    clearFramebuffer(s_canvasWidth, s_canvasHeight);
}

EMSCRIPTEN_KEEPALIVE
void renderToFullScreen() {
    renderToScreen(s_canvasWidth, s_canvasHeight);
}

}
