#include "systemStdAfx.h"
#include "MainFrame.h"

#if defined(NV_LINUX_PLATFORM)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

#include <stdio.h>
#include <unistd.h>
#include <mutex>
#include <vector>

namespace
{
Display* g_display = nullptr;
::Window g_window = 0;
HINSTANCE g_instance = nullptr;
XVisualInfo* g_glVisual = nullptr;
Colormap g_colormap = 0;
GLXContext g_glContext = 0;
bool g_exit = false;
bool g_active = true;
bool g_notMinimized = true;
bool g_cursorVisible = true;
bool g_manageCursor = true;
HCURSOR g_currentCursor = nullptr;
HCURSOR g_hiddenCursor = nullptr;
Atom g_wmDeleteWindow = None;
NMainFrame::ICloseApplicationHandler* g_closeHandler = nullptr;
nstl::string g_exitCode;
void (*g_stepCallback)() = nullptr;

std::mutex g_messagesMutex;
std::vector<NMainFrame::SWindowsMsg> g_messages;
size_t g_nextMessage = 0;

unsigned long g_lastLeftClickMs = 0;
unsigned long g_lastMiddleClickMs = 0;
unsigned long g_lastRightClickMs = 0;

XVisualInfo* ChooseOpenGlVisual(Display* display)
{
  if (!display)
  {
    return nullptr;
  }

  const int screen = DefaultScreen(display);
  int rgbaDoubleBuffered[] =
  {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    None
  };

  XVisualInfo* visual = glXChooseVisual(display, screen, rgbaDoubleBuffered);
  if (visual)
  {
    return visual;
  }

  int rgbaBuffered[] =
  {
    GLX_RGBA,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    None
  };

  return glXChooseVisual(display, screen, rgbaBuffered);
}

inline HWND EncodeWindow(::Window window)
{
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(window));
}

inline ::Window DecodeWindow(HWND window)
{
  return static_cast<::Window>(reinterpret_cast<uintptr_t>(window));
}

inline HCURSOR EncodeCursor(::Cursor cursor)
{
  return reinterpret_cast<HCURSOR>(static_cast<uintptr_t>(cursor));
}

inline ::Cursor DecodeCursor(HCURSOR cursor)
{
  return static_cast<::Cursor>(reinterpret_cast<uintptr_t>(cursor));
}

unsigned long GetTickMs()
{
  NHPTimer::STime time = 0;
  NHPTimer::GetTime(time);
  return static_cast<unsigned long>(NHPTimer::Time2Milliseconds(time));
}

void PushMessage(const NMainFrame::SWindowsMsg& msg)
{
  std::lock_guard<std::mutex> lock(g_messagesMutex);
  if (g_nextMessage >= g_messages.size())
  {
    g_messages.clear();
    g_nextMessage = 0;
  }
  g_messages.push_back(msg);
}

void PushMouseMessage(NMainFrame::SWindowsMsg::EMsg msgType, int x, int y, unsigned long flags)
{
  NMainFrame::SWindowsMsg msg = {};
  NHPTimer::GetTime(msg.time);
  msg.msg = msgType;
  msg.x = x;
  msg.y = y;
  msg.dwFlags = flags;
  PushMessage(msg);
}

NMainFrame::SWindowsMsg::EMsg GetLocalCursorPos(int& x, int& y)
{
  x = 0;
  y = 0;

  if (!g_display || !g_window || !g_active || !g_notMinimized)
  {
    return NMainFrame::SWindowsMsg::MOUSE_DISABLED;
  }

  XWindowAttributes attributes = {};
  if (!XGetWindowAttributes(g_display, g_window, &attributes) || attributes.map_state != IsViewable)
  {
    return NMainFrame::SWindowsMsg::MOUSE_DISABLED;
  }

  ::Window root = 0;
  ::Window child = 0;
  int rootX = 0;
  int rootY = 0;
  int winX = 0;
  int winY = 0;
  unsigned int mask = 0;
  if (!XQueryPointer(g_display, g_window, &root, &child, &rootX, &rootY, &winX, &winY, &mask))
  {
    return NMainFrame::SWindowsMsg::MOUSE_DISABLED;
  }

  x = winX;
  y = winY;

  if (winX < 0 || winX >= attributes.width || winY < 0 || winY >= attributes.height)
  {
    return NMainFrame::SWindowsMsg::MOUSE_OUT;
  }

  return NMainFrame::SWindowsMsg::MOUSE_MOVE;
}

void PushCursorMessage()
{
  int x = 0;
  int y = 0;
  const NMainFrame::SWindowsMsg::EMsg msg = GetLocalCursorPos(x, y);
  PushMouseMessage(msg, x, y, 0);
}

void PushKeyMessage(NMainFrame::SWindowsMsg::EMsg msgType, int key, int repeat)
{
  NMainFrame::SWindowsMsg msg = {};
  NHPTimer::GetTime(msg.time);
  msg.msg = msgType;
  msg.nKey = key;
  msg.nRep = repeat;
  msg.dwFlags = 0;
  PushMessage(msg);
}

HCURSOR CreateHiddenCursor()
{
  if (!g_display || !g_window)
  {
    return nullptr;
  }

  static const char emptyData[] = {0};
  Pixmap bitmap = XCreateBitmapFromData(g_display, g_window, emptyData, 1, 1);
  if (!bitmap)
  {
    return nullptr;
  }

  XColor black = {};
  ::Cursor cursor = XCreatePixmapCursor(g_display, bitmap, bitmap, &black, &black, 0, 0);
  XFreePixmap(g_display, bitmap);
  if (!cursor)
  {
    return nullptr;
  }

  return EncodeCursor(cursor);
}

void ApplyCursor()
{
  if (!g_display || !g_window)
  {
    return;
  }

  if (!g_manageCursor)
  {
    XUndefineCursor(g_display, g_window);
    XFlush(g_display);
    return;
  }

  if (g_cursorVisible)
  {
    ::Cursor cursor = DecodeCursor(g_currentCursor);
    if (cursor)
    {
      XDefineCursor(g_display, g_window, cursor);
    }
    else
    {
      XUndefineCursor(g_display, g_window);
    }
  }
  else
  {
    if (!g_hiddenCursor)
    {
      g_hiddenCursor = CreateHiddenCursor();
    }

    ::Cursor cursor = DecodeCursor(g_hiddenCursor);
    if (cursor)
    {
      XDefineCursor(g_display, g_window, cursor);
    }
  }

  XFlush(g_display);
}

void ResizeWindowInternal(unsigned long width, unsigned long height)
{
  if (!g_display || !g_window)
  {
    return;
  }

  XResizeWindow(g_display, g_window, width, height);
  XFlush(g_display);
}

void PrintBootstrapError(const char* message)
{
  fprintf(stderr, "%s\n", message);
}

Atom InternWindowManagerAtom(const char* name)
{
  if (!g_display || !name)
  {
    return None;
  }

  return XInternAtom(g_display, name, False);
}

void SetWindowCardinalProperty(const char* name, unsigned long value)
{
  const Atom property = InternWindowManagerAtom(name);
  if (!g_display || !g_window || property == None)
  {
    return;
  }

  XChangeProperty(
    g_display,
    g_window,
    property,
    XA_CARDINAL,
    32,
    PropModeReplace,
    reinterpret_cast<const unsigned char*>(&value),
    1
  );
}

void SetWindowAtomProperty(const char* name, Atom value)
{
  const Atom property = InternWindowManagerAtom(name);
  if (!g_display || !g_window || property == None || value == None)
  {
    return;
  }

  XChangeProperty(
    g_display,
    g_window,
    property,
    XA_ATOM,
    32,
    PropModeReplace,
    reinterpret_cast<const unsigned char*>(&value),
    1
  );
}

void SetWindowAtomListProperty(const char* name, const Atom* values, int count)
{
  const Atom property = InternWindowManagerAtom(name);
  if (!g_display || !g_window || property == None || !values || count <= 0)
  {
    return;
  }

  XChangeProperty(
    g_display,
    g_window,
    property,
    XA_ATOM,
    32,
    PropModeReplace,
    reinterpret_cast<const unsigned char*>(values),
    count
  );
}

void ApplyWindowManagerHints(const char* appName, int width, int height)
{
  if (!g_display || !g_window)
  {
    return;
  }

  XClassHint classHint = {};
  classHint.res_name = const_cast<char*>("PrimeWorldLinuxClient");
  classHint.res_class = const_cast<char*>("PrimeWorld");
  XSetClassHint(g_display, g_window, &classHint);

  XSizeHints sizeHints = {};
  sizeHints.flags = USSize | PSize | PMinSize | PMaxSize | PBaseSize;
  sizeHints.width = width;
  sizeHints.height = height;
  sizeHints.min_width = width;
  sizeHints.min_height = height;
  sizeHints.max_width = width;
  sizeHints.max_height = height;
  sizeHints.base_width = width;
  sizeHints.base_height = height;
  XSetWMNormalHints(g_display, g_window, &sizeHints);

  XWMHints wmHints = {};
  wmHints.flags = InputHint | StateHint;
  wmHints.input = True;
  wmHints.initial_state = NormalState;
  XSetWMHints(g_display, g_window, &wmHints);

  XTextProperty textProperty = {};
  char* windowNames[] = { const_cast<char*>(appName ? appName : "Prime World Classic") };
  if (XStringListToTextProperty(windowNames, 1, &textProperty) != 0)
  {
    XSetWMName(g_display, g_window, &textProperty);
    XSetWMIconName(g_display, g_window, &textProperty);
    XFree(textProperty.value);
  }

  SetWindowCardinalProperty("_NET_WM_PID", static_cast<unsigned long>(getpid()));
  const unsigned long allDesktops = 0xFFFFFFFFUL;
  SetWindowCardinalProperty("_NET_WM_DESKTOP", allDesktops);

  const Atom normalWindowType = InternWindowManagerAtom("_NET_WM_WINDOW_TYPE_NORMAL");
  SetWindowAtomProperty("_NET_WM_WINDOW_TYPE", normalWindowType);

  Atom windowStates[2] =
  {
    InternWindowManagerAtom("_NET_WM_STATE_ABOVE"),
    InternWindowManagerAtom("_NET_WM_STATE_STICKY")
  };
  if (windowStates[0] != None && windowStates[1] != None)
  {
    SetWindowAtomListProperty("_NET_WM_STATE", windowStates, 2);
  }
}

void RequestWindowForeground(int width, int height)
{
  if (!g_display || !g_window)
  {
    return;
  }

  const int screen = DefaultScreen(g_display);
  XMoveResizeWindow(
    g_display,
    g_window,
    0,
    0,
    static_cast<unsigned int>(width),
    static_cast<unsigned int>(height)
  );
  XMapRaised(g_display, g_window);
  XRaiseWindow(g_display, g_window);

  const Atom activeWindow = InternWindowManagerAtom("_NET_ACTIVE_WINDOW");
  if (activeWindow != None)
  {
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.display = g_display;
    event.xclient.window = g_window;
    event.xclient.message_type = activeWindow;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = CurrentTime;
    event.xclient.data.l[2] = 0;
    XSendEvent(
      g_display,
      RootWindow(g_display, screen),
      False,
      SubstructureRedirectMask | SubstructureNotifyMask,
      &event
    );
  }

  XSync(g_display, False);

  XWindowAttributes attributes = {};
  if (XGetWindowAttributes(g_display, g_window, &attributes) && attributes.map_state == IsViewable)
  {
    XSetInputFocus(g_display, g_window, RevertToPointerRoot, CurrentTime);
  }

  XFlush(g_display);
}

void ProcessButtonPress(const XButtonEvent& event)
{
  const unsigned long nowMs = GetTickMs();

  switch (event.button)
  {
    case Button1:
      if (nowMs - g_lastLeftClickMs <= 250)
      {
        PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_LB_DBLCLK, event.x, event.y, 0);
      }
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_LB_DOWN, event.x, event.y, 0);
      g_lastLeftClickMs = nowMs;
      break;

    case Button3:
      if (nowMs - g_lastRightClickMs <= 250)
      {
        PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_RB_DBLCLK, event.x, event.y, 0);
      }
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_RB_DOWN, event.x, event.y, 0);
      g_lastRightClickMs = nowMs;
      break;

    case Button2:
      if (nowMs - g_lastMiddleClickMs <= 250)
      {
        PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_MB_DBLCLK, event.x, event.y, 0);
      }
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_MB_DOWN, event.x, event.y, 0);
      g_lastMiddleClickMs = nowMs;
      break;

    case Button4:
      PushMouseMessage(
        NMainFrame::SWindowsMsg::MOUSE_WHEEL,
        event.x,
        event.y,
        static_cast<unsigned long>(static_cast<unsigned short>(WHEEL_DELTA) << 16)
      );
      break;

    case Button5:
      PushMouseMessage(
        NMainFrame::SWindowsMsg::MOUSE_WHEEL,
        event.x,
        event.y,
        static_cast<unsigned long>(static_cast<unsigned short>(-WHEEL_DELTA) << 16)
      );
      break;

    default:
      break;
  }
}

void ProcessButtonRelease(const XButtonEvent& event)
{
  switch (event.button)
  {
    case Button1:
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_LB_UP, event.x, event.y, 0);
      break;

    case Button3:
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_RB_UP, event.x, event.y, 0);
      break;

    case Button2:
      PushMouseMessage(NMainFrame::SWindowsMsg::MOUSE_MB_UP, event.x, event.y, 0);
      break;

    default:
      break;
  }
}

void ProcessKeyEvent(XKeyEvent& event, bool pressed)
{
  KeySym keySym = NoSymbol;
  char buffer[32] = {0};
  const int textLen = XLookupString(&event, buffer, sizeof(buffer), &keySym, nullptr);

  PushKeyMessage(
    pressed ? NMainFrame::SWindowsMsg::KEY_DOWN : NMainFrame::SWindowsMsg::KEY_UP,
    static_cast<int>(keySym),
    1
  );

  if (pressed && textLen > 0)
  {
    for (int i = 0; i < textLen; ++i)
    {
      PushKeyMessage(NMainFrame::SWindowsMsg::KEY_CHAR, static_cast<unsigned char>(buffer[i]), 1);
    }
  }
}
}

namespace NMainFrame
{

bool GetMessage(SWindowsMsg* result)
{
  std::lock_guard<std::mutex> lock(g_messagesMutex);
  if (g_nextMessage < g_messages.size())
  {
    *result = g_messages[g_nextMessage++];
    return true;
  }

  g_messages.clear();
  g_nextMessage = 0;
  result->msg = SWindowsMsg::TIME;
  NHPTimer::GetTime(result->time);
  return false;
}

bool IsAppActive()
{
  return g_active;
}

bool IsAppNotMinimized()
{
  return g_notMinimized;
}

bool IsExit()
{
  return g_exit;
}

void Exit()
{
  g_exit = true;
}

void Exit(const nstl::string& exitCode)
{
  SetExitCode(exitCode);
  Exit();
}

void SetExitCode(const nstl::string& exitCode)
{
  g_exitCode = exitCode;
}

const nstl::string& GetExitCode()
{
  return g_exitCode;
}

HWND GetWnd()
{
  return EncodeWindow(g_window);
}

void* GetNativeDisplay()
{
  return g_display;
}

void SetWnd(HWND window)
{
  g_window = DecodeWindow(window);
}

HINSTANCE GetInstance()
{
  return g_instance;
}

void PumpMessages()
{
  if (!g_display)
  {
    return;
  }

  PushCursorMessage();

  while (XPending(g_display) > 0)
  {
    XEvent event = {};
    XNextEvent(g_display, &event);

    switch (event.type)
    {
      case ClientMessage:
        if (static_cast<Atom>(event.xclient.data.l[0]) == g_wmDeleteWindow)
        {
          if (!g_closeHandler || g_closeHandler->OnCloseApplication())
          {
            g_exit = true;
          }
        }
        break;

      case ConfigureNotify:
        g_notMinimized = event.xconfigure.width > 0 && event.xconfigure.height > 0;
        break;

      case FocusIn:
        g_active = true;
        break;

      case FocusOut:
        g_active = false;
        break;

      case EnterNotify:
      case MotionNotify:
        PushMouseMessage(SWindowsMsg::MOUSE_MOVE, event.xmotion.x, event.xmotion.y, 0);
        break;

      case LeaveNotify:
        PushMouseMessage(SWindowsMsg::MOUSE_OUT, event.xcrossing.x, event.xcrossing.y, 0);
        break;

      case ButtonPress:
        ProcessButtonPress(event.xbutton);
        break;

      case ButtonRelease:
        ProcessButtonRelease(event.xbutton);
        break;

      case KeyPress:
        ProcessKeyEvent(event.xkey, true);
        break;

      case KeyRelease:
        ProcessKeyEvent(event.xkey, false);
        break;

      case DestroyNotify:
        g_exit = true;
        break;

      default:
        break;
    }
  }

  if (g_stepCallback)
  {
    g_stepCallback();
  }
}

bool InitApplication(
  HINSTANCE hInstance,
  const char* appName,
  const char* windowName,
  LPCWSTR,
  bool,
  int width,
  int height,
  HWND externalWindow
)
{
  g_instance = hInstance;
  g_exit = false;
  g_active = true;
  g_notMinimized = true;
  g_exitCode.clear();
  g_glVisual = nullptr;
  g_colormap = 0;

  if (externalWindow)
  {
    g_window = DecodeWindow(externalWindow);
    return g_window != 0;
  }

  g_display = XOpenDisplay(nullptr);
  if (!g_display)
  {
    PrintBootstrapError("NMainFrame::InitApplication failed: XOpenDisplay returned null");
    return false;
  }

  const int screen = DefaultScreen(g_display);
  g_glVisual = ChooseOpenGlVisual(g_display);
  if (g_glVisual)
  {
    XSetWindowAttributes attributes = {};
    attributes.border_pixel = 0;
    attributes.event_mask =
      ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
      PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask | StructureNotifyMask;
    attributes.colormap = XCreateColormap(
      g_display,
      RootWindow(g_display, screen),
      g_glVisual->visual,
      AllocNone
    );
    g_colormap = attributes.colormap;

    g_window = XCreateWindow(
      g_display,
      RootWindow(g_display, screen),
      0,
      0,
      static_cast<unsigned int>(width),
      static_cast<unsigned int>(height),
      0,
      g_glVisual->depth,
      InputOutput,
      g_glVisual->visual,
      CWBorderPixel | CWColormap | CWEventMask,
      &attributes
    );
  }
  else
  {
    g_window = XCreateSimpleWindow(
      g_display,
      RootWindow(g_display, screen),
      0,
      0,
      static_cast<unsigned int>(width),
      static_cast<unsigned int>(height),
      0,
      BlackPixel(g_display, screen),
      WhitePixel(g_display, screen)
    );
  }
  if (!g_window)
  {
    if (g_colormap)
    {
      XFreeColormap(g_display, g_colormap);
      g_colormap = 0;
    }
    if (g_glVisual)
    {
      XFree(g_glVisual);
      g_glVisual = nullptr;
    }
    XCloseDisplay(g_display);
    g_display = nullptr;
    PrintBootstrapError("NMainFrame::InitApplication failed: XCreateSimpleWindow returned 0");
    return false;
  }

  XStoreName(g_display, g_window, windowName ? windowName : appName);
  ApplyWindowManagerHints(appName, width, height);
  if (!g_glVisual)
  {
    XSelectInput(
      g_display,
      g_window,
      ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask | StructureNotifyMask
    );
  }

  g_wmDeleteWindow = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_display, g_window, &g_wmDeleteWindow, 1);

  RequestWindowForeground(width, height);
  return true;
}

bool InitOpenGLContext()
{
  if (!g_display || !g_window)
  {
    return false;
  }

  if (g_glContext)
  {
    return true;
  }

  XVisualInfo* visual = g_glVisual;
  XVisualInfo visualTemplate = {};
  int visualCount = 0;
  if (!visual)
  {
    XWindowAttributes attributes = {};
    if (!XGetWindowAttributes(g_display, g_window, &attributes))
    {
      return false;
    }

    visualTemplate.visualid = XVisualIDFromVisual(attributes.visual);
    visual = XGetVisualInfo(g_display, VisualIDMask, &visualTemplate, &visualCount);
    if (!visual)
    {
      return false;
    }
  }

  g_glContext = glXCreateContext(g_display, visual, 0, True);
  if (!g_glVisual && visual)
  {
    XFree(visual);
  }

  if (!g_glContext)
  {
    return false;
  }

  if (!glXMakeCurrent(g_display, g_window, g_glContext))
  {
    glXDestroyContext(g_display, g_glContext);
    g_glContext = 0;
    return false;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_SCISSOR_TEST);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
  return true;
}

void ShutdownOpenGLContext()
{
  if (!g_display || !g_glContext)
  {
    return;
  }

  glXMakeCurrent(g_display, None, 0);
  glXDestroyContext(g_display, g_glContext);
  g_glContext = 0;
}

bool MakeOpenGLContextCurrent()
{
  if (!g_display || !g_window || !g_glContext)
  {
    return false;
  }

  return glXMakeCurrent(g_display, g_window, g_glContext) == True;
}

void SwapOpenGLBuffers()
{
  if (!g_display || !g_window || !g_glContext)
  {
    return;
  }

  glXSwapBuffers(g_display, g_window);
}

void ShutdownApplication()
{
  ShutdownOpenGLContext();

  if (g_display && g_window)
  {
    XDestroyWindow(g_display, g_window);
    g_window = 0;
  }

  if (g_display && g_colormap)
  {
    XFreeColormap(g_display, g_colormap);
    g_colormap = 0;
  }

  if (g_glVisual)
  {
    XFree(g_glVisual);
    g_glVisual = nullptr;
  }

  if (g_display)
  {
    XCloseDisplay(g_display);
    g_display = nullptr;
  }

  if (g_hiddenCursor)
  {
    g_hiddenCursor = nullptr;
  }
}

void SetCursor(HCURSOR cursor)
{
  g_currentCursor = cursor;
  ApplyCursor();
}

void ShowCursor(bool show)
{
  g_cursorVisible = show;
  ApplyCursor();
}

HCURSOR GetCurrentCursor()
{
  return g_currentCursor;
}

void EnableCursorManagement(bool enable)
{
  g_manageCursor = enable;
  ApplyCursor();
}

bool UpdateCursorRectInt(const char*, const vector<wstring>&)
{
  return false;
}

void ResizeWindow(unsigned long width, unsigned long height, bool, bool)
{
  ResizeWindowInternal(width, height);
}

void DumpWindowStyle(DWORD style)
{
  fprintf(stderr, "NMainFrame::DumpWindowStyle style=%u\n", style);
}

void DumpExWindowStyle(DWORD style)
{
  fprintf(stderr, "NMainFrame::DumpExWindowStyle style=%u\n", style);
}

void ApplyNewParams(unsigned long width, unsigned long height, bool isFullScreen, bool isBorderless)
{
  ResizeWindow(width, height, isFullScreen, isBorderless);
}

void SetActualClipCursorRect()
{
}

void SetCloseHandler(ICloseApplicationHandler* handler)
{
  g_closeHandler = handler;
}

}

void SetStepCallback(void (*stepFunc)())
{
  g_stepCallback = stepFunc;
}

#endif
