/* xkey — эмуляция нажатия клавиши через XTEST без заголовков libXtst.
 * Нужен, чтобы хоткей RenderDoc (по умолчанию F12/PrtScrn) сработал внутри
 * захваченного процесса: слой опрашивает состояние клавиш XQueryKeymap
 * (renderdoc/os/posix/linux/linux_stringio.cpp:139) на том X-дисплее, который
 * он узнал из surface'а. Состояние клавиш хранится в X-сервере, поэтому
 * XTEST-событие с любого клиента видно слою.
 *
 * usage: DISPLAY=:97 xkey [keysym] [keycode]
 *        по умолчанию F12 (keycode подбирается XKeysymToKeycode)
 * build: cc -O2 -o xkey xkey.c -lX11 -ldl
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef Status (*fn_ext_query)(Display *, int, int *, int *);
typedef int (*fn_fake_key)(Display *, int, int, unsigned long);

/* индексы расширений в Xvfb: XTEST = 131 (XTestCode в libXtst), но точнее —
 * XListExtensions */
int main(int argc, char **argv)
{
  const char *keysymName = argc > 1 ? argv[1] : "F12";
  int keycode = argc > 2 ? atoi(argv[2]) : 0;

  Display *d = XOpenDisplay(NULL);
  if (!d) { fprintf(stderr, "xkey: cannot open display %s\n", XDisplayName(NULL)); return 2; }

  void *h = dlopen("libXtst.so.6", RTLD_NOW | RTLD_GLOBAL);
  if (!h) { fprintf(stderr, "xkey: dlopen libXtst: %s\n", dlerror()); return 3; }
  fn_fake_key fake = (fn_fake_key)dlsym(h, "XTestFakeKeyEvent");
  if (!fake) { fprintf(stderr, "xkey: dlsym XTestFakeKeyEvent: %s\n", dlerror()); return 4; }

  if (!keycode) {
    KeySym ks = XStringToKeysym(keysymName);
    if (!ks) { fprintf(stderr, "xkey: unknown keysym %s\n", keysymName); return 5; }
    keycode = XKeysymToKeycode(d, ks);
    if (!keycode) { fprintf(stderr, "xkey: no keycode for %s\n", keysymName); return 6; }
  }

  /* press, пауза, release — RenderDoc опрашивает клавиши раз на present */
  fake(d, keycode, 1, 0);
  XFlush(d);
  usleep(120000);
  fake(d, keycode, 0, 0);
  XFlush(d);
  printf("xkey: keysym=%s keycode=%d sent to %s\n", keysymName, keycode, XDisplayName(NULL));
  XCloseDisplay(d);
  return 0;
}
