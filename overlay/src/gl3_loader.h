#pragma once
// Minimal GL3 FBO extension loader for Windows (wglGetProcAddress).
// Only loads what BreakersTool's overlay needs — not a general loader.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#define GL_FRAMEBUFFER              0x8D40
#define GL_READ_FRAMEBUFFER         0x8CA8
#define GL_DRAW_FRAMEBUFFER         0x8CA9
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_RENDERBUFFER             0x8D41
#define GL_DEPTH24_STENCIL8         0x88F0
#define GL_RGBA8                    0x8058

typedef void  (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)       (GLsizei, GLuint*);
typedef void  (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)       (GLenum, GLuint);
typedef void  (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)  (GLenum, GLenum, GLenum, GLuint, GLint);
typedef void  (APIENTRY* PFNGLGENRENDERBUFFERSPROC)      (GLsizei, GLuint*);
typedef void  (APIENTRY* PFNGLBINDRENDERBUFFERPROC)      (GLenum, GLuint);
typedef void  (APIENTRY* PFNGLRENDERBUFFERSTORAGEPROC)   (GLenum, GLenum, GLsizei, GLsizei);
typedef void  (APIENTRY* PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void  (APIENTRY* PFNGLBLITFRAMEBUFFERPROC)       (GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
typedef void  (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)    (GLsizei, const GLuint*);
typedef void  (APIENTRY* PFNGLDELETERENDERBUFFERSPROC)   (GLsizei, const GLuint*);

static PFNGLGENFRAMEBUFFERSPROC        glGenFramebuffers        = nullptr;
static PFNGLBINDFRAMEBUFFERPROC        glBindFramebuffer        = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC   glFramebufferTexture2D   = nullptr;
static PFNGLGENRENDERBUFFERSPROC       glGenRenderbuffers       = nullptr;
static PFNGLBINDRENDERBUFFERPROC       glBindRenderbuffer       = nullptr;
static PFNGLRENDERBUFFERSTORAGEPROC    glRenderbufferStorage    = nullptr;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
static PFNGLBLITFRAMEBUFFERPROC        glBlitFramebuffer        = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC     glDeleteFramebuffers     = nullptr;
static PFNGLDELETERENDERBUFFERSPROC    glDeleteRenderbuffers    = nullptr;

inline void LoadGL3Extensions() {
#define LOAD(T, name) name = (T)wglGetProcAddress(#name)
    LOAD(PFNGLGENFRAMEBUFFERSPROC,         glGenFramebuffers);
    LOAD(PFNGLBINDFRAMEBUFFERPROC,         glBindFramebuffer);
    LOAD(PFNGLFRAMEBUFFERTEXTURE2DPROC,    glFramebufferTexture2D);
    LOAD(PFNGLGENRENDERBUFFERSPROC,        glGenRenderbuffers);
    LOAD(PFNGLBINDRENDERBUFFERPROC,        glBindRenderbuffer);
    LOAD(PFNGLRENDERBUFFERSTORAGEPROC,     glRenderbufferStorage);
    LOAD(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
    LOAD(PFNGLBLITFRAMEBUFFERPROC,         glBlitFramebuffer);
    LOAD(PFNGLDELETEFRAMEBUFFERSPROC,      glDeleteFramebuffers);
    LOAD(PFNGLDELETERENDERBUFFERSPROC,     glDeleteRenderbuffers);
#undef LOAD
}
