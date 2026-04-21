// RenderToTexture.cpp
// 极简 RTT 管线：
//   1. 创建一张纹理 + FBO
//   2. 对外暴露 bindFramebuffer / unbindFramebuffer 供"引擎模块"向 FBO 渲染
//   3. 最后用全屏 quad 把 FBO 纹理采样到屏幕

#include "RenderToTexture.h"
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

static GLuint s_framebuffer = 0;
static GLuint s_renderTexture = 0;

static GLuint s_fullscreenProgram = 0;
static GLint  s_fsPosLoc = -1;
static GLint  s_fsUvLoc  = -1;
static GLint  s_fsTexLoc = -1;

static GLuint s_quadVBO = 0;

static const GLfloat kQuad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f
};

static const char* kFullscreenVS =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_texcoord;\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static const char* kFullscreenFS =
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_texture, v_uv);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        emscripten_log(EM_LOG_ERROR, "[RTT] shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        emscripten_log(EM_LOG_ERROR, "[RTT] program link error: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

void initRenderToTexture(int width, int height) {
    glGenTextures(1, &s_renderTexture);
    glBindTexture(GL_TEXTURE_2D, s_renderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &s_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, s_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_renderTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        emscripten_log(EM_LOG_ERROR, "[RTT] FBO incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    s_fullscreenProgram = linkProgram(kFullscreenVS, kFullscreenFS);
    if (!s_fullscreenProgram) return;
    s_fsPosLoc = glGetAttribLocation (s_fullscreenProgram, "a_position");
    s_fsUvLoc  = glGetAttribLocation (s_fullscreenProgram, "a_texcoord");
    s_fsTexLoc = glGetUniformLocation(s_fullscreenProgram, "u_texture");

    glGenBuffers(1, &s_quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, s_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    emscripten_log(EM_LOG_CONSOLE, "[RTT] init OK, %dx%d", width, height);
}

void destroyRenderToTexture() {
    if (s_quadVBO)           { glDeleteBuffers(1, &s_quadVBO); s_quadVBO = 0; }
    if (s_fullscreenProgram) { glDeleteProgram(s_fullscreenProgram); s_fullscreenProgram = 0; }
    if (s_framebuffer)       { glDeleteFramebuffers(1, &s_framebuffer); s_framebuffer = 0; }
    if (s_renderTexture)     { glDeleteTextures(1, &s_renderTexture); s_renderTexture = 0; }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE GLuint getFrameBuffer() {
    return s_framebuffer;
}

EMSCRIPTEN_KEEPALIVE void bindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, s_framebuffer);
}

EMSCRIPTEN_KEEPALIVE void unbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

EMSCRIPTEN_KEEPALIVE void clearFramebuffer(int width, int height) {
    static float t = 0.0f;
    t += 0.02f;
    // 比前景三角形慢、饱和度更低的呼吸色，避免盖过 Module B 的输出
    float r = 0.15f + 0.10f * sinf(t * 0.7f);
    float g = 0.15f + 0.10f * sinf(t * 0.9f + 1.5f);
    float b = 0.20f + 0.10f * sinf(t * 1.1f + 3.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, s_framebuffer);
    glViewport(0, 0, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

EMSCRIPTEN_KEEPALIVE void renderToScreen(int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    glUseProgram(s_fullscreenProgram);
    glBindBuffer(GL_ARRAY_BUFFER, s_quadVBO);

    glEnableVertexAttribArray(s_fsPosLoc);
    glVertexAttribPointer(s_fsPosLoc, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), (void*)0);

    glEnableVertexAttribArray(s_fsUvLoc);
    glVertexAttribPointer(s_fsUvLoc, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat),
                          (void*)(2 * sizeof(GLfloat)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_renderTexture);
    glUniform1i(s_fsTexLoc, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}
