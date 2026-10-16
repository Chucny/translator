// Generated once from core/include/zb/gl_backend.h for GlDriverBackend (Phase 5 Task 8).
// Do not edit gl_backend.h and expect this to update: regenerate by hand if the GLES surface
// changes (see tools/gen_gles.py's HANDWRITTEN/manual lists for the source of truth signatures).
// glGetError is hand-written in gl_driver_backend.cpp, not generated here.
    void glActiveTexture(GLenum texture) override { ::glActiveTexture(texture); }
    void glAttachShader(GLuint program, GLuint shader) override { ::glAttachShader(program, shader); }
    void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) override { ::glBindAttribLocation(program, index, name); }
    void glBindBuffer(GLenum target, GLuint buffer) override { ::glBindBuffer(target, buffer); }
    void glBindFramebuffer(GLenum target, GLuint framebuffer) override { ::glBindFramebuffer(target, framebuffer); }
    void glBindRenderbuffer(GLenum target, GLuint renderbuffer) override { ::glBindRenderbuffer(target, renderbuffer); }
    void glBindTexture(GLenum target, GLuint texture) override { ::glBindTexture(target, texture); }
    void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) override { ::glBlendColor(red, green, blue, alpha); }
    void glBlendEquation(GLenum mode) override { ::glBlendEquation(mode); }
    void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) override { ::glBlendEquationSeparate(modeRGB, modeAlpha); }
    void glBlendFunc(GLenum sfactor, GLenum dfactor) override { ::glBlendFunc(sfactor, dfactor); }
    void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) override { ::glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha); }
    void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) override { ::glBufferData(target, size, data, usage); }
    void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) override { ::glBufferSubData(target, offset, size, data); }
    GLenum glCheckFramebufferStatus(GLenum target) override { return static_cast<GLenum>(::glCheckFramebufferStatus(target)); }
    void glClear(GLbitfield mask) override { ::glClear(mask); }
    void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) override { ::glClearColor(red, green, blue, alpha); }
    void glClearDepthf(GLfloat d) override { ::glClearDepthf(d); }
    void glClearStencil(GLint s) override { ::glClearStencil(s); }
    void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) override { ::glColorMask(red, green, blue, alpha); }
    void glCompileShader(GLuint shader) override { ::glCompileShader(shader); }
    void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) override { ::glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data); }
    void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) override { ::glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data); }
    void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) override { ::glCopyTexImage2D(target, level, internalformat, x, y, width, height, border); }
    void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) override { ::glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height); }
    GLuint glCreateProgram(void) override { return static_cast<GLuint>(::glCreateProgram()); }
    GLuint glCreateShader(GLenum type) override { return static_cast<GLuint>(::glCreateShader(type)); }
    void glCullFace(GLenum mode) override { ::glCullFace(mode); }
    void glDeleteBuffers(GLsizei n, const GLuint *buffers) override { ::glDeleteBuffers(n, buffers); }
    void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) override { ::glDeleteFramebuffers(n, framebuffers); }
    void glDeleteProgram(GLuint program) override { ::glDeleteProgram(program); }
    void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) override { ::glDeleteRenderbuffers(n, renderbuffers); }
    void glDeleteShader(GLuint shader) override { ::glDeleteShader(shader); }
    void glDeleteTextures(GLsizei n, const GLuint *textures) override { ::glDeleteTextures(n, textures); }
    void glDepthFunc(GLenum func) override { ::glDepthFunc(func); }
    void glDepthMask(GLboolean flag) override { ::glDepthMask(flag); }
    void glDepthRangef(GLfloat n, GLfloat f) override { ::glDepthRangef(n, f); }
    void glDetachShader(GLuint program, GLuint shader) override { ::glDetachShader(program, shader); }
    void glDisable(GLenum cap) override { ::glDisable(cap); }
    void glDisableVertexAttribArray(GLuint index) override { ::glDisableVertexAttribArray(index); }
    void glDrawArrays(GLenum mode, GLint first, GLsizei count) override { ::glDrawArrays(mode, first, count); }
    void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) override { ::glDrawElements(mode, count, type, indices); }
    void glEnable(GLenum cap) override { ::glEnable(cap); }
    void glEnableVertexAttribArray(GLuint index) override { ::glEnableVertexAttribArray(index); }
    void glFinish(void) override { ::glFinish(); }
    void glFlush(void) override { ::glFlush(); }
    void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) override { ::glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer); }
    void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) override { ::glFramebufferTexture2D(target, attachment, textarget, texture, level); }
    void glFrontFace(GLenum mode) override { ::glFrontFace(mode); }
    void glGenBuffers(GLsizei n, GLuint *buffers) override { ::glGenBuffers(n, buffers); }
    void glGenFramebuffers(GLsizei n, GLuint *framebuffers) override { ::glGenFramebuffers(n, framebuffers); }
    void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) override { ::glGenRenderbuffers(n, renderbuffers); }
    void glGenTextures(GLsizei n, GLuint *textures) override { ::glGenTextures(n, textures); }
    void glGenerateMipmap(GLenum target) override { ::glGenerateMipmap(target); }
    void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) override { ::glGetActiveAttrib(program, index, bufSize, length, size, type, name); }
    void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) override { ::glGetActiveUniform(program, index, bufSize, length, size, type, name); }
    void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) override { ::glGetAttachedShaders(program, maxCount, count, shaders); }
    GLint glGetAttribLocation(GLuint program, const GLchar *name) override { return static_cast<GLint>(::glGetAttribLocation(program, name)); }
    void glGetBooleanv(GLenum pname, GLboolean *data) override { ::glGetBooleanv(pname, data); }
    void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) override { ::glGetBufferParameteriv(target, pname, params); }
    void glGetFloatv(GLenum pname, GLfloat *data) override { ::glGetFloatv(pname, data); }
    void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) override { ::glGetFramebufferAttachmentParameteriv(target, attachment, pname, params); }
    void glGetIntegerv(GLenum pname, GLint *data) override { ::glGetIntegerv(pname, data); }
    void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) override { ::glGetProgramInfoLog(program, bufSize, length, infoLog); }
    void glGetProgramiv(GLuint program, GLenum pname, GLint *params) override { ::glGetProgramiv(program, pname, params); }
    void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) override { ::glGetRenderbufferParameteriv(target, pname, params); }
    void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) override { ::glGetShaderInfoLog(shader, bufSize, length, infoLog); }
    void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) override { ::glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision); }
    void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) override { ::glGetShaderSource(shader, bufSize, length, source); }
    void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) override { ::glGetShaderiv(shader, pname, params); }
    const GLubyte * glGetString(GLenum name) override { return static_cast<const GLubyte *>(::glGetString(name)); }
    void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) override { ::glGetTexParameterfv(target, pname, params); }
    void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) override { ::glGetTexParameteriv(target, pname, params); }
    GLint glGetUniformLocation(GLuint program, const GLchar *name) override { return static_cast<GLint>(::glGetUniformLocation(program, name)); }
    void glGetUniformfv(GLuint program, GLint location, GLfloat *params) override { ::glGetUniformfv(program, location, params); }
    void glGetUniformiv(GLuint program, GLint location, GLint *params) override { ::glGetUniformiv(program, location, params); }
    void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) override { ::glGetVertexAttribPointerv(index, pname, pointer); }
    void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) override { ::glGetVertexAttribfv(index, pname, params); }
    void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) override { ::glGetVertexAttribiv(index, pname, params); }
    void glHint(GLenum target, GLenum mode) override { ::glHint(target, mode); }
    GLboolean glIsBuffer(GLuint buffer) override { return static_cast<GLboolean>(::glIsBuffer(buffer)); }
    GLboolean glIsEnabled(GLenum cap) override { return static_cast<GLboolean>(::glIsEnabled(cap)); }
    GLboolean glIsFramebuffer(GLuint framebuffer) override { return static_cast<GLboolean>(::glIsFramebuffer(framebuffer)); }
    GLboolean glIsProgram(GLuint program) override { return static_cast<GLboolean>(::glIsProgram(program)); }
    GLboolean glIsRenderbuffer(GLuint renderbuffer) override { return static_cast<GLboolean>(::glIsRenderbuffer(renderbuffer)); }
    GLboolean glIsShader(GLuint shader) override { return static_cast<GLboolean>(::glIsShader(shader)); }
    GLboolean glIsTexture(GLuint texture) override { return static_cast<GLboolean>(::glIsTexture(texture)); }
    void glLineWidth(GLfloat width) override { ::glLineWidth(width); }
    void glLinkProgram(GLuint program) override { ::glLinkProgram(program); }
    void glPixelStorei(GLenum pname, GLint param) override { ::glPixelStorei(pname, param); }
    void glPolygonOffset(GLfloat factor, GLfloat units) override { ::glPolygonOffset(factor, units); }
    void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) override { ::glReadPixels(x, y, width, height, format, type, pixels); }
    void glReleaseShaderCompiler(void) override { ::glReleaseShaderCompiler(); }
    void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) override { ::glRenderbufferStorage(target, internalformat, width, height); }
    void glSampleCoverage(GLfloat value, GLboolean invert) override { ::glSampleCoverage(value, invert); }
    void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) override { ::glScissor(x, y, width, height); }
    void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length) override { ::glShaderBinary(count, shaders, binaryFormat, binary, length); }
    void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) override { ::glShaderSource(shader, count, string, length); }
    void glStencilFunc(GLenum func, GLint ref, GLuint mask) override { ::glStencilFunc(func, ref, mask); }
    void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) override { ::glStencilFuncSeparate(face, func, ref, mask); }
    void glStencilMask(GLuint mask) override { ::glStencilMask(mask); }
    void glStencilMaskSeparate(GLenum face, GLuint mask) override { ::glStencilMaskSeparate(face, mask); }
    void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) override { ::glStencilOp(fail, zfail, zpass); }
    void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) override { ::glStencilOpSeparate(face, sfail, dpfail, dppass); }
    void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) override { ::glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels); }
    void glTexParameterf(GLenum target, GLenum pname, GLfloat param) override { ::glTexParameterf(target, pname, param); }
    void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) override { ::glTexParameterfv(target, pname, params); }
    void glTexParameteri(GLenum target, GLenum pname, GLint param) override { ::glTexParameteri(target, pname, param); }
    void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) override { ::glTexParameteriv(target, pname, params); }
    void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) override { ::glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); }
    void glUniform1f(GLint location, GLfloat v0) override { ::glUniform1f(location, v0); }
    void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) override { ::glUniform1fv(location, count, value); }
    void glUniform1i(GLint location, GLint v0) override { ::glUniform1i(location, v0); }
    void glUniform1iv(GLint location, GLsizei count, const GLint *value) override { ::glUniform1iv(location, count, value); }
    void glUniform2f(GLint location, GLfloat v0, GLfloat v1) override { ::glUniform2f(location, v0, v1); }
    void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) override { ::glUniform2fv(location, count, value); }
    void glUniform2i(GLint location, GLint v0, GLint v1) override { ::glUniform2i(location, v0, v1); }
    void glUniform2iv(GLint location, GLsizei count, const GLint *value) override { ::glUniform2iv(location, count, value); }
    void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) override { ::glUniform3f(location, v0, v1, v2); }
    void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) override { ::glUniform3fv(location, count, value); }
    void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) override { ::glUniform3i(location, v0, v1, v2); }
    void glUniform3iv(GLint location, GLsizei count, const GLint *value) override { ::glUniform3iv(location, count, value); }
    void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) override { ::glUniform4f(location, v0, v1, v2, v3); }
    void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) override { ::glUniform4fv(location, count, value); }
    void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) override { ::glUniform4i(location, v0, v1, v2, v3); }
    void glUniform4iv(GLint location, GLsizei count, const GLint *value) override { ::glUniform4iv(location, count, value); }
    void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix2fv(location, count, transpose, value); }
    void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix3fv(location, count, transpose, value); }
    void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix4fv(location, count, transpose, value); }
    void glUseProgram(GLuint program) override { ::glUseProgram(program); }
    void glValidateProgram(GLuint program) override { ::glValidateProgram(program); }
    void glVertexAttrib1f(GLuint index, GLfloat x) override { ::glVertexAttrib1f(index, x); }
    void glVertexAttrib1fv(GLuint index, const GLfloat *v) override { ::glVertexAttrib1fv(index, v); }
    void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) override { ::glVertexAttrib2f(index, x, y); }
    void glVertexAttrib2fv(GLuint index, const GLfloat *v) override { ::glVertexAttrib2fv(index, v); }
    void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) override { ::glVertexAttrib3f(index, x, y, z); }
    void glVertexAttrib3fv(GLuint index, const GLfloat *v) override { ::glVertexAttrib3fv(index, v); }
    void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) override { ::glVertexAttrib4f(index, x, y, z, w); }
    void glVertexAttrib4fv(GLuint index, const GLfloat *v) override { ::glVertexAttrib4fv(index, v); }
    void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) override { ::glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
    void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) override { ::glViewport(x, y, width, height); }

    // GLES 3.0, generated the same way (2026-09-18).
    void glBeginQuery(GLenum target, GLuint id) override { ::glBeginQuery(target, id); }
    void glBeginTransformFeedback(GLenum primitiveMode) override { ::glBeginTransformFeedback(primitiveMode); }
    void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) override { ::glBindBufferBase(target, index, buffer); }
    void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) override { ::glBindBufferRange(target, index, buffer, offset, size); }
    void glBindSampler(GLuint unit, GLuint sampler) override { ::glBindSampler(unit, sampler); }
    void glBindTransformFeedback(GLenum target, GLuint id) override { ::glBindTransformFeedback(target, id); }
    void glBindVertexArray(GLuint array) override { ::glBindVertexArray(array); }
    void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) override { ::glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter); }
    void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) override { ::glClearBufferfi(buffer, drawbuffer, depth, stencil); }
    void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) override { ::glClearBufferfv(buffer, drawbuffer, value); }
    void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) override { ::glClearBufferiv(buffer, drawbuffer, value); }
    void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) override { ::glClearBufferuiv(buffer, drawbuffer, value); }
    GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) override { return static_cast<GLenum>(::glClientWaitSync(reinterpret_cast<::GLsync>(sync), flags, timeout)); }
    void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) override { ::glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, imageSize, data); }
    void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) override { ::glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data); }
    void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) override { ::glCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size); }
    void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) override { ::glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height); }
    void glDeleteQueries(GLsizei n, const GLuint *ids) override { ::glDeleteQueries(n, ids); }
    void glDeleteSamplers(GLsizei count, const GLuint *samplers) override { ::glDeleteSamplers(count, samplers); }
    void glDeleteSync(GLsync sync) override { ::glDeleteSync(reinterpret_cast<::GLsync>(sync)); }
    void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids) override { ::glDeleteTransformFeedbacks(n, ids); }
    void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) override { ::glDeleteVertexArrays(n, arrays); }
    void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) override { ::glDrawArraysInstanced(mode, first, count, instancecount); }
    void glDrawBuffers(GLsizei n, const GLenum *bufs) override { ::glDrawBuffers(n, bufs); }
    void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) override { ::glDrawElementsInstanced(mode, count, type, indices, instancecount); }
    void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) override { ::glDrawRangeElements(mode, start, end, count, type, indices); }
    void glEndQuery(GLenum target) override { ::glEndQuery(target); }
    void glEndTransformFeedback(void) override { ::glEndTransformFeedback(); }
    GLsync glFenceSync(GLenum condition, GLbitfield flags) override { return reinterpret_cast<GLsync>(::glFenceSync(condition, flags)); }
    void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) override { ::glFlushMappedBufferRange(target, offset, length); }
    void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) override { ::glFramebufferTextureLayer(target, attachment, texture, level, layer); }
    void glGenQueries(GLsizei n, GLuint *ids) override { ::glGenQueries(n, ids); }
    void glGenSamplers(GLsizei count, GLuint *samplers) override { ::glGenSamplers(count, samplers); }
    void glGenTransformFeedbacks(GLsizei n, GLuint *ids) override { ::glGenTransformFeedbacks(n, ids); }
    void glGenVertexArrays(GLsizei n, GLuint *arrays) override { ::glGenVertexArrays(n, arrays); }
    void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) override { ::glGetActiveUniformBlockName(program, uniformBlockIndex, bufSize, length, uniformBlockName); }
    void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params) override { ::glGetActiveUniformBlockiv(program, uniformBlockIndex, pname, params); }
    void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params) override { ::glGetActiveUniformsiv(program, uniformCount, uniformIndices, pname, params); }
    void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) override { ::glGetBufferParameteri64v(target, pname, params); }
    void glGetBufferPointerv(GLenum target, GLenum pname, void **params) override { ::glGetBufferPointerv(target, pname, params); }
    GLint glGetFragDataLocation(GLuint program, const GLchar *name) override { return static_cast<GLint>(::glGetFragDataLocation(program, name)); }
    void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) override { ::glGetInteger64i_v(target, index, data); }
    void glGetInteger64v(GLenum pname, GLint64 *data) override { ::glGetInteger64v(pname, data); }
    void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) override { ::glGetIntegeri_v(target, index, data); }
    void glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname, GLsizei count, GLint *params) override { ::glGetInternalformativ(target, internalformat, pname, count, params); }
    void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary) override { ::glGetProgramBinary(program, bufSize, length, binaryFormat, binary); }
    void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) override { ::glGetQueryObjectuiv(id, pname, params); }
    void glGetQueryiv(GLenum target, GLenum pname, GLint *params) override { ::glGetQueryiv(target, pname, params); }
    void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) override { ::glGetSamplerParameterfv(sampler, pname, params); }
    void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) override { ::glGetSamplerParameteriv(sampler, pname, params); }
    const GLubyte * glGetStringi(GLenum name, GLuint index) override { return ::glGetStringi(name, index); }
    void glGetSynciv(GLsync sync, GLenum pname, GLsizei count, GLsizei *length, GLint *values) override { ::glGetSynciv(reinterpret_cast<::GLsync>(sync), pname, count, length, values); }
    void glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) override { ::glGetTransformFeedbackVarying(program, index, bufSize, length, size, type, name); }
    GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) override { return static_cast<GLuint>(::glGetUniformBlockIndex(program, uniformBlockName)); }
    void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices) override { ::glGetUniformIndices(program, uniformCount, uniformNames, uniformIndices); }
    void glGetUniformuiv(GLuint program, GLint location, GLuint *params) override { ::glGetUniformuiv(program, location, params); }
    void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) override { ::glGetVertexAttribIiv(index, pname, params); }
    void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) override { ::glGetVertexAttribIuiv(index, pname, params); }
    void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments) override { ::glInvalidateFramebuffer(target, numAttachments, attachments); }
    void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height) override { ::glInvalidateSubFramebuffer(target, numAttachments, attachments, x, y, width, height); }
    GLboolean glIsQuery(GLuint id) override { return static_cast<GLboolean>(::glIsQuery(id)); }
    GLboolean glIsSampler(GLuint sampler) override { return static_cast<GLboolean>(::glIsSampler(sampler)); }
    GLboolean glIsSync(GLsync sync) override { return static_cast<GLboolean>(::glIsSync(reinterpret_cast<::GLsync>(sync))); }
    GLboolean glIsTransformFeedback(GLuint id) override { return static_cast<GLboolean>(::glIsTransformFeedback(id)); }
    GLboolean glIsVertexArray(GLuint array) override { return static_cast<GLboolean>(::glIsVertexArray(array)); }
    void * glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) override { return ::glMapBufferRange(target, offset, length, access); }
    void glPauseTransformFeedback(void) override { ::glPauseTransformFeedback(); }
    void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) override { ::glProgramBinary(program, binaryFormat, binary, length); }
    void glProgramParameteri(GLuint program, GLenum pname, GLint value) override { ::glProgramParameteri(program, pname, value); }
    void glReadBuffer(GLenum src) override { ::glReadBuffer(src); }
    void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) override { ::glRenderbufferStorageMultisample(target, samples, internalformat, width, height); }
    void glResumeTransformFeedback(void) override { ::glResumeTransformFeedback(); }
    void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) override { ::glSamplerParameterf(sampler, pname, param); }
    void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) override { ::glSamplerParameterfv(sampler, pname, param); }
    void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) override { ::glSamplerParameteri(sampler, pname, param); }
    void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) override { ::glSamplerParameteriv(sampler, pname, param); }
    void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) override { ::glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels); }
    void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) override { ::glTexStorage2D(target, levels, internalformat, width, height); }
    void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) override { ::glTexStorage3D(target, levels, internalformat, width, height, depth); }
    void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) override { ::glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels); }
    void glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const*varyings, GLenum bufferMode) override { ::glTransformFeedbackVaryings(program, count, varyings, bufferMode); }
    void glUniform1ui(GLint location, GLuint v0) override { ::glUniform1ui(location, v0); }
    void glUniform1uiv(GLint location, GLsizei count, const GLuint *value) override { ::glUniform1uiv(location, count, value); }
    void glUniform2ui(GLint location, GLuint v0, GLuint v1) override { ::glUniform2ui(location, v0, v1); }
    void glUniform2uiv(GLint location, GLsizei count, const GLuint *value) override { ::glUniform2uiv(location, count, value); }
    void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) override { ::glUniform3ui(location, v0, v1, v2); }
    void glUniform3uiv(GLint location, GLsizei count, const GLuint *value) override { ::glUniform3uiv(location, count, value); }
    void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) override { ::glUniform4ui(location, v0, v1, v2, v3); }
    void glUniform4uiv(GLint location, GLsizei count, const GLuint *value) override { ::glUniform4uiv(location, count, value); }
    void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) override { ::glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding); }
    void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix2x3fv(location, count, transpose, value); }
    void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix2x4fv(location, count, transpose, value); }
    void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix3x2fv(location, count, transpose, value); }
    void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix3x4fv(location, count, transpose, value); }
    void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix4x2fv(location, count, transpose, value); }
    void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) override { ::glUniformMatrix4x3fv(location, count, transpose, value); }
    GLboolean glUnmapBuffer(GLenum target) override { return static_cast<GLboolean>(::glUnmapBuffer(target)); }
    void glVertexAttribDivisor(GLuint index, GLuint divisor) override { ::glVertexAttribDivisor(index, divisor); }
    void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) override { ::glVertexAttribI4i(index, x, y, z, w); }
    void glVertexAttribI4iv(GLuint index, const GLint *v) override { ::glVertexAttribI4iv(index, v); }
    void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) override { ::glVertexAttribI4ui(index, x, y, z, w); }
    void glVertexAttribI4uiv(GLuint index, const GLuint *v) override { ::glVertexAttribI4uiv(index, v); }
    void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) override { ::glVertexAttribIPointer(index, size, type, stride, pointer); }
    void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) override { ::glWaitSync(reinterpret_cast<::GLsync>(sync), flags, timeout); }
