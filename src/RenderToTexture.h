#ifndef RENDER_TO_TEXTURE_H
#define RENDER_TO_TEXTURE_H

#include <GLES2/gl2.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void initRenderToTexture(int width, int height);
void destroyRenderToTexture();

GLuint getFrameBuffer();
void bindFramebuffer();
void unbindFramebuffer();

void clearFramebuffer(int width, int height);
void renderToScreen(int width, int height);

#ifdef __cplusplus
}
#endif

#endif
