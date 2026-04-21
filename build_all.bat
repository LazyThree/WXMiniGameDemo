REM build_all.bat  ——  两个模块的 emcc 编译命令
REM 前置：先在本 cmd 里 call 过 emsdk_env.bat

setlocal
cd /d %~dp0
if not exist build mkdir build

echo === [1/2] building RTTModule (webglbind + RenderToTexture) ===
call emcc src\webglbind.cpp src\RenderToTexture.cpp -std=c++11 -O3 -s WASM=1 -s USE_WEBGL2=1 -s GL_PREINITIALIZED_CONTEXT=1 -s ASSERTIONS=2 -s ENVIRONMENT=web,shell -s DYNAMIC_EXECUTION=0 -s MODULARIZE=1 -s EXPORT_NAME="RTTModule" -s EXPORTED_FUNCTIONS="[\"_setCanvasSize\",\"_startWithWebGLContext\",\"_renderToFramebuffer\",\"_renderToFullScreen\",\"_bindFramebuffer\",\"_unbindFramebuffer\",\"_getFrameBuffer\"]" -s EXPORTED_RUNTIME_METHODS="[\"ccall\",\"cwrap\",\"GL\"]" -o build\webglbind.js

echo.
echo === [2/2] building TriangleModule (triangle) ===
call emcc src\triangle.cpp -std=c++11 -O3 -s WASM=1 -s USE_WEBGL2=1 -s GL_PREINITIALIZED_CONTEXT=1 -s ASSERTIONS=2 -s ENVIRONMENT=web,shell -s DYNAMIC_EXECUTION=0 -s MODULARIZE=1 -s EXPORT_NAME="TriangleModule" -s EXPORTED_FUNCTIONS="[\"_initTriangle\",\"_drawTriangle\"]" -s EXPORTED_RUNTIME_METHODS="[\"ccall\",\"cwrap\",\"GL\"]" -o build\triangle.js

endlocal
