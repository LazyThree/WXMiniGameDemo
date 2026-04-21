// triangle.cpp
// 模块 B：最简"引擎"模块 —— 占据参考工程中 PxKit 的位置。
// 职责：
//   - 从 JS 拿到与模块 A 相同的 WebGL context handle
//   - 在外部（JS）将 FBO 绑定好后，由该模块向**共享 FBO** 画一个彩色三角形
//
// 之所以保留"两个独立 WASM 模块、共用同一 context"这种略显啰嗦的写法，
// 是为了保留原始双 WASM 架构的验证意图；业务上你完全可以把它塞进 webglbind.cpp。

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static GLuint s_program = 0;
static GLuint s_vbo     = 0;
static GLint  s_posLoc  = -1;
static GLint  s_colorLoc = -1;

static float s_time = 0.0f;

static const GLfloat kTriangle[] = {
     0.0f,  0.7f,
    -0.7f, -0.6f,
     0.7f, -0.6f
};

static const char* kVS =
    "attribute vec2 a_position;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static const char* kFS =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "  gl_FragColor = u_color;\n"
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
        emscripten_log(EM_LOG_ERROR, "[Triangle] shader: %s", log);
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
        emscripten_log(EM_LOG_ERROR, "[Triangle] link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void initTriangle(int jsContextHandle) {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)jsContextHandle;
    EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(ctx);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        emscripten_log(EM_LOG_ERROR,
            "[Triangle] make_context_current failed: %d", res);
        return;
    }

    s_program = linkProgram(kVS, kFS);
    if (!s_program) return;
    s_posLoc   = glGetAttribLocation (s_program, "a_position");
    s_colorLoc = glGetUniformLocation(s_program, "u_color");

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTriangle), kTriangle, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    emscripten_log(EM_LOG_CONSOLE, "[Triangle] init OK");
}

EMSCRIPTEN_KEEPALIVE
void drawTriangle() {
    if (!s_program) return;

    s_time += 0.016f;
    float r = 0.5f + 0.5f * __builtin_sinf(s_time * 1.3f);
    float g = 0.5f + 0.5f * __builtin_sinf(s_time * 1.7f + 2.0f);
    float b = 0.5f + 0.5f * __builtin_sinf(s_time * 2.1f + 4.0f);

    glUseProgram(s_program);
    glUniform4f(s_colorLoc, r, g, b, 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(s_posLoc);
    glVertexAttribPointer(s_posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}
