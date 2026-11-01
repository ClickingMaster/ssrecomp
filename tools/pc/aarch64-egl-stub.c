/* A stand-in libEGL.so.1 for linking the ARM64 Linux build on an x86_64 machine.
 * The download does not ship this file: the program asks the system for libEGL.so.1
 * at run time (Mesa on the handheld). The names have to match frontend/glcontext.cpp,
 * which is what the PC build calls; the bodies are never run. */
void* eglGetProcAddress(const char* name) { (void)name; return 0; }
unsigned eglQueryAPI(void) { return 0; }
void* eglGetCurrentDisplay(void) { return 0; }
void* eglGetCurrentSurface(int which) { (void)which; return 0; }
void* eglGetCurrentContext(void) { return 0; }
unsigned eglBindAPI(unsigned api) { (void)api; return 0; }
unsigned eglMakeCurrent(void* display, void* draw, void* read, void* context)
{
    (void)display; (void)draw; (void)read; (void)context; return 0;
}
const char* eglQueryString(void* display, int name) { (void)display; (void)name; return ""; }
void* eglGetDisplay(void* native) { (void)native; return 0; }
unsigned eglInitialize(void* display, int* major, int* minor)
{
    (void)display; (void)major; (void)minor; return 0;
}
unsigned eglChooseConfig(void* display, const int* attribs, void* configs, int size, int* count)
{
    (void)display; (void)attribs; (void)configs; (void)size; (void)count; return 0;
}
void* eglCreateContext(void* display, void* config, void* share, const int* attribs)
{
    (void)display; (void)config; (void)share; (void)attribs; return 0;
}
unsigned eglGetConfigs(void* display, void* configs, int size, int* count)
{
    (void)display; (void)configs; (void)size; (void)count; return 0;
}
void* eglCreatePbufferSurface(void* display, void* config, const int* attribs)
{
    (void)display; (void)config; (void)attribs; return 0;
}
int eglGetError(void) { return 0x3000; }
unsigned eglDestroyContext(void* display, void* context)
{
    (void)display; (void)context; return 0;
}
