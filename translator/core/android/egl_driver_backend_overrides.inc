// Generated once from core/include/zb/egl_backend.h for EglDriverBackend
// (Phase 7a Task 8), the same way gl_driver_backend_overrides.inc was for GlDriverBackend.
// Do not edit egl_backend.h and expect this to update: regenerate by hand if the EGL
// surface changes (see tools/gen_egl.py).
    EGLBoolean eglBindAPI(EGLenum api) override { return static_cast<EGLBoolean>(::eglBindAPI(api)); }
    EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) override { return static_cast<EGLBoolean>(::eglBindTexImage(dpy, surface, buffer)); }
    EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config) override { return static_cast<EGLBoolean>(::eglChooseConfig(dpy, attrib_list, configs, config_size, num_config)); }
    EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) override { return static_cast<EGLint>(::eglClientWaitSync(dpy, sync, flags, timeout)); }
    EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target) override { return static_cast<EGLBoolean>(::eglCopyBuffers(dpy, surface, reinterpret_cast<::EGLNativePixmapType>(target))); }
    EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) override { return reinterpret_cast<EGLContext>(::eglCreateContext(dpy, config, share_context, attrib_list)); }
    EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list) override { return reinterpret_cast<EGLImage>(::eglCreateImage(dpy, ctx, target, buffer, attrib_list)); }
    EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreatePbufferFromClientBuffer(dpy, buftype, buffer, config, attrib_list)); }
    EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreatePbufferSurface(dpy, config, attrib_list)); }
    EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreatePixmapSurface(dpy, config, reinterpret_cast<::EGLNativePixmapType>(pixmap), attrib_list)); }
    EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreatePlatformPixmapSurface(dpy, config, native_pixmap, attrib_list)); }
    EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreatePlatformWindowSurface(dpy, config, native_window, attrib_list)); }
    EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list) override { return reinterpret_cast<EGLSync>(::eglCreateSync(dpy, type, attrib_list)); }
    EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list) override { return reinterpret_cast<EGLSurface>(::eglCreateWindowSurface(dpy, config, reinterpret_cast<::EGLNativeWindowType>(win), attrib_list)); }
    EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) override { return static_cast<EGLBoolean>(::eglDestroyContext(dpy, ctx)); }
    EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image) override { return static_cast<EGLBoolean>(::eglDestroyImage(dpy, image)); }
    EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) override { return static_cast<EGLBoolean>(::eglDestroySurface(dpy, surface)); }
    EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) override { return static_cast<EGLBoolean>(::eglDestroySync(dpy, sync)); }
    EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value) override { return static_cast<EGLBoolean>(::eglGetConfigAttrib(dpy, config, attribute, value)); }
    EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config) override { return static_cast<EGLBoolean>(::eglGetConfigs(dpy, configs, config_size, num_config)); }
    EGLContext eglGetCurrentContext(void) override { return reinterpret_cast<EGLContext>(::eglGetCurrentContext()); }
    EGLDisplay eglGetCurrentDisplay(void) override { return reinterpret_cast<EGLDisplay>(::eglGetCurrentDisplay()); }
    EGLSurface eglGetCurrentSurface(EGLint readdraw) override { return reinterpret_cast<EGLSurface>(::eglGetCurrentSurface(readdraw)); }
    EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) override { return reinterpret_cast<EGLDisplay>(::eglGetDisplay(display_id)); }
    EGLint eglGetError(void) override { return static_cast<EGLint>(::eglGetError()); }
    EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display, const EGLAttrib *attrib_list) override { return reinterpret_cast<EGLDisplay>(::eglGetPlatformDisplay(platform, native_display, attrib_list)); }
    __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname) override { return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(::eglGetProcAddress(procname)); }
    EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib *value) override { return static_cast<EGLBoolean>(::eglGetSyncAttrib(dpy, sync, attribute, value)); }
    EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) override { return static_cast<EGLBoolean>(::eglInitialize(dpy, major, minor)); }
    EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) override { return static_cast<EGLBoolean>(::eglMakeCurrent(dpy, draw, read, ctx)); }
    EGLenum eglQueryAPI(void) override { return static_cast<EGLenum>(::eglQueryAPI()); }
    EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value) override { return static_cast<EGLBoolean>(::eglQueryContext(dpy, ctx, attribute, value)); }
    const char * eglQueryString(EGLDisplay dpy, EGLint name) override { return reinterpret_cast<const char *>(::eglQueryString(dpy, name)); }
    EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value) override { return static_cast<EGLBoolean>(::eglQuerySurface(dpy, surface, attribute, value)); }
    EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) override { return static_cast<EGLBoolean>(::eglReleaseTexImage(dpy, surface, buffer)); }
    EGLBoolean eglReleaseThread(void) override { return static_cast<EGLBoolean>(::eglReleaseThread()); }
    EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) override { return static_cast<EGLBoolean>(::eglSurfaceAttrib(dpy, surface, attribute, value)); }
    EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) override { return static_cast<EGLBoolean>(::eglSwapBuffers(dpy, surface)); }
    EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) override { return static_cast<EGLBoolean>(::eglSwapInterval(dpy, interval)); }
    EGLBoolean eglTerminate(EGLDisplay dpy) override { return static_cast<EGLBoolean>(::eglTerminate(dpy)); }
    EGLBoolean eglWaitClient(void) override { return static_cast<EGLBoolean>(::eglWaitClient()); }
    EGLBoolean eglWaitGL(void) override { return static_cast<EGLBoolean>(::eglWaitGL()); }
    EGLBoolean eglWaitNative(EGLint engine) override { return static_cast<EGLBoolean>(::eglWaitNative(engine)); }
    EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) override { return static_cast<EGLBoolean>(::eglWaitSync(dpy, sync, flags)); }
