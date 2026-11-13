/*
 * Headless OpenGL 3.3 core context via EGL (Mesa llvmpipe / swrast).
 * Used to exercise raylib's GPU vertex-buffer morph path without a display server.
 *
 * Usage: call egl_headless_init() before any rlgl/raylib graphics call,
 * then rlLoadExtensions(eglGetProcAddress) + rlglInit(), and rlglClose()
 * before egl_headless_shutdown() at the end.
 */
#ifndef EGL_HEADLESS_H
#define EGL_HEADLESS_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_OPENGL_CORE_PROFILE_BIT
#define EGL_OPENGL_CORE_PROFILE_BIT 0x00000004
#endif
#ifndef EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif

static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
static EGLConfig g_eglConfig = NULL;
static EGLContext g_eglContext = EGL_NO_CONTEXT;
static EGLSurface g_eglSurface = EGL_NO_SURFACE;

static bool egl_headless_init(int width, int height)
{
    // Prefer the surfaceless platform extension; fall back to the default display
    typedef EGLDisplay (*PFN_eglGetPlatformDisplayEXT)(EGLenum, void *, const EGLint *);
    PFN_eglGetPlatformDisplayEXT pGetPlatformDisplay =
        (PFN_eglGetPlatformDisplayEXT)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (pGetPlatformDisplay != NULL)
    {
        g_eglDisplay = pGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    }

    if (g_eglDisplay == EGL_NO_DISPLAY) g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_eglDisplay == EGL_NO_DISPLAY) { fprintf(stderr, "EGL: no display\n"); return false; }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_eglDisplay, &major, &minor))
    {
        fprintf(stderr, "EGL: initialize failed 0x%x\n", eglGetError());
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API))
    {
        fprintf(stderr, "EGL: bind OpenGL API failed 0x%x\n", eglGetError());
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_CORE_PROFILE_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_CONFORMANT, EGL_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(g_eglDisplay, configAttribs, &g_eglConfig, 1, &numConfigs) || (numConfigs < 1))
    {
        fprintf(stderr, "EGL: choose config failed 0x%x\n", eglGetError());
        return false;
    }

    EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (g_eglContext == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "EGL: create context failed 0x%x\n", eglGetError());
        return false;
    }

    EGLint pbufferAttribs[] = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE };
    g_eglSurface = eglCreatePbufferSurface(g_eglDisplay, g_eglConfig, pbufferAttribs);
    if (g_eglSurface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "EGL: pbuffer failed 0x%x\n", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext))
    {
        fprintf(stderr, "EGL: make current failed 0x%x\n", eglGetError());
        return false;
    }

    return true;
}

static void egl_headless_shutdown(void)
{
    if (g_eglDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_eglSurface != EGL_NO_SURFACE) eglDestroySurface(g_eglDisplay, g_eglSurface);
        if (g_eglContext != EGL_NO_CONTEXT) eglDestroyContext(g_eglDisplay, g_eglContext);
        eglTerminate(g_eglDisplay);
    }

    g_eglDisplay = EGL_NO_DISPLAY;
    g_eglContext = EGL_NO_CONTEXT;
    g_eglSurface = EGL_NO_SURFACE;
}

#endif // EGL_HEADLESS_H
