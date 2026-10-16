/* Drives the whole EGL sequence through the guest stubs. Printed values are checked by the host
   test, which supplies the mock backends. Phase 7a Task 9.

   Phase 1: display/config/context, exactly the plan's original sequence.
   Phase 2 (only when a window handle is given as argv[1], in decimal): eglCreateWindowSurface,
   eglMakeCurrent, glClear and eglSwapBuffers, proving the window path end to end. The probe
   cannot call ANativeWindow_fromSurface itself (that needs a real Java Surface), so the host
   test creates the window handle through HostNativeWindow's mock backend and hands it to the
   probe here. */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) return 1;
    EGLint attribs[] = {EGL_RED_SIZE, 8, EGL_NONE};
    EGLConfig config = 0;
    EGLint count = 0;
    if (!eglChooseConfig(display, attribs, &config, 1, &count) || count < 1) return 2;
    EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) return 3;
    printf("egl %d.%d configs=%d\n", major, minor, count);

    if (argc > 1) {
        EGLNativeWindowType window = (EGLNativeWindowType)(unsigned long)strtoul(argv[1], NULL, 10);
        EGLSurface surface = eglCreateWindowSurface(display, config, window, NULL);
        if (surface == EGL_NO_SURFACE) return 4;
        if (!eglMakeCurrent(display, surface, surface, context)) return 5;
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(display, surface)) return 6;
        printf("window surface ok\n");
    }
    return 0;
}
