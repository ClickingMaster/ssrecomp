/* Stand-in libX11 for the ARM64 cross build. SDL's configure step links a few
 * X11 calls to see what the headers can do; the real library is opened later
 * by name (libX11.so.6) and this file is not shipped. */
void XNextEvent(void* display, void* event) { (void)display; (void)event; }
int XGetEventData(void* display, void* cookie) { (void)display; (void)cookie; return 0; }
void XFreeEventData(void* display, void* cookie) { (void)display; (void)cookie; }
unsigned long XkbKeycodeToKeysym(void* display, unsigned int keycode, int group, int level)
{
    (void)display; (void)keycode; (void)group; (void)level; return 0;
}
