#include "WindowHelper.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

// Windows 10/11 DWM 属性值
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Windows 10 早期版本使用的属性值
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

WindowHelper::WindowHelper(QObject *parent) : QObject(parent) {}

bool WindowHelper::setTitleBarColor(QQuickWindow *window, const QColor &color) {
#ifdef Q_OS_WIN
  if (!window) {
    return false;
  }

  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  if (!hwnd) {
    return false;
  }

  // 根据颜色亮度判断是否为深色模式
  bool isDark = (color.lightness() < 128);

  // 设置深色/浅色模式
  setDarkTitleBar(window, isDark);

  // 尝试设置标题栏颜色 (仅 Windows 11+ 支持)
  COLORREF captionColor = RGB(color.red(), color.green(), color.blue());
  HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor,
                                     sizeof(captionColor));

  // 强制刷新窗口帧以应用更改
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER);

  return SUCCEEDED(hr);
#else
  Q_UNUSED(window)
  Q_UNUSED(color)
  return false;
#endif
}

bool WindowHelper::setDarkTitleBar(QQuickWindow *window, bool dark) {
#ifdef Q_OS_WIN
  if (!window) {
    return false;
  }

  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  if (!hwnd) {
    return false;
  }

  BOOL useDarkMode = dark ? TRUE : FALSE;

  // 首先尝试使用 Windows 10 20H1+ 及 Windows 11 的属性
  HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                     &useDarkMode, sizeof(useDarkMode));

  // 如果失败，尝试使用旧版 Windows 10 的属性值 (19)
  if (FAILED(hr)) {
    hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
                               &useDarkMode, sizeof(useDarkMode));
  }

  // 强制刷新窗口帧
  if (SUCCEEDED(hr)) {
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER);
  }

  return SUCCEEDED(hr);
#else
  Q_UNUSED(window)
  Q_UNUSED(dark)
  return false;
#endif
}

bool WindowHelper::setupFramelessWindow(QQuickWindow *window) {
#ifdef Q_OS_WIN
  if (!window) {
    return false;
  }

  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  if (!hwnd) {
    return false;
  }

  // 获取当前窗口样式
  LONG style = GetWindowLong(hwnd, GWL_STYLE);

  // 移除标题栏但保留边框以支持阴影和缩放
  style &= ~(WS_CAPTION | WS_THICKFRAME);
  style |= WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

  SetWindowLong(hwnd, GWL_STYLE, style);

  // 扩展 DWM 帧到客户区以启用阴影效果
  MARGINS margins = {1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(hwnd, &margins);

  // 刷新窗口帧
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER);

  return true;
#else
  Q_UNUSED(window)
  return false;
#endif
}

void WindowHelper::startSystemMove(QQuickWindow *window) {
  if (!window) {
    return;
  }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  // Qt 5.15+ 提供了 startSystemMove
  window->startSystemMove();
#else
#ifdef Q_OS_WIN
  // 旧版本 Qt 使用原生 API
  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  if (hwnd) {
    ReleaseCapture();
    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
  }
#endif
#endif
}

void WindowHelper::startSystemResize(QQuickWindow *window, int edges) {
  if (!window || edges == 0) {
    return;
  }

#ifdef Q_OS_WIN
  // 使用原生 Windows API 实现窗口缩放
  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  if (hwnd) {
    // 将 Qt::Edges 转换为 Windows HT 值
    WPARAM hitTest = HTCLIENT;
    Qt::Edges e = static_cast<Qt::Edges>(edges);

    if (e == Qt::TopEdge)
      hitTest = HTTOP;
    else if (e == Qt::BottomEdge)
      hitTest = HTBOTTOM;
    else if (e == Qt::LeftEdge)
      hitTest = HTLEFT;
    else if (e == Qt::RightEdge)
      hitTest = HTRIGHT;
    else if (e == (Qt::TopEdge | Qt::LeftEdge))
      hitTest = HTTOPLEFT;
    else if (e == (Qt::TopEdge | Qt::RightEdge))
      hitTest = HTTOPRIGHT;
    else if (e == (Qt::BottomEdge | Qt::LeftEdge))
      hitTest = HTBOTTOMLEFT;
    else if (e == (Qt::BottomEdge | Qt::RightEdge))
      hitTest = HTBOTTOMRIGHT;

    ReleaseCapture();
    SendMessage(hwnd, WM_NCLBUTTONDOWN, hitTest, 0);
  }
#else
  // 非 Windows 平台使用 Qt API
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  window->startSystemResize(static_cast<Qt::Edges>(edges));
#endif
#endif
}

int WindowHelper::getResizeEdges(QQuickWindow *window, qreal mouseX,
                                 qreal mouseY, int borderWidth) {
  if (!window) {
    return 0;
  }

  int edges = 0;
  int width = window->width();
  int height = window->height();

  // 检测鼠标是否在边缘区域
  if (mouseX < borderWidth) {
    edges |= Qt::LeftEdge;
  } else if (mouseX > width - borderWidth) {
    edges |= Qt::RightEdge;
  }

  if (mouseY < borderWidth) {
    edges |= Qt::TopEdge;
  } else if (mouseY > height - borderWidth) {
    edges |= Qt::BottomEdge;
  }

  return edges;
}

int WindowHelper::getCursorShapeForEdges(int edges) {
  Qt::Edges e = static_cast<Qt::Edges>(edges);

  if (e == Qt::LeftEdge || e == Qt::RightEdge) {
    return Qt::SizeHorCursor;
  } else if (e == Qt::TopEdge || e == Qt::BottomEdge) {
    return Qt::SizeVerCursor;
  } else if (e == (Qt::TopEdge | Qt::LeftEdge) ||
             e == (Qt::BottomEdge | Qt::RightEdge)) {
    return Qt::SizeFDiagCursor;
  } else if (e == (Qt::TopEdge | Qt::RightEdge) ||
             e == (Qt::BottomEdge | Qt::LeftEdge)) {
    return Qt::SizeBDiagCursor;
  }

  return Qt::ArrowCursor;
}
