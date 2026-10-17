#ifndef WINDOWHELPER_H
#define WINDOWHELPER_H

#include <QColor>
#include <QObject>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

/**
 * @brief WindowHelper 类
 * 用于设置 Windows 平台下窗口标题栏的颜色，使其能够跟随应用主题
 * 以及提供无边框窗口的原生支持（边缘缩放等）
 */
class WindowHelper : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

public:
  explicit WindowHelper(QObject *parent = nullptr);

  /**
   * @brief 设置窗口标题栏颜色
   * @param window 目标窗口
   * @param color 标题栏背景颜色
   * @return 成功返回 true，失败返回 false
   */
  Q_INVOKABLE bool setTitleBarColor(QQuickWindow *window, const QColor &color);

  /**
   * @brief 设置标题栏是否使用深色模式
   * @param window 目标窗口
   * @param dark 是否深色模式
   * @return 成功返回 true，失败返回 false
   */
  Q_INVOKABLE bool setDarkTitleBar(QQuickWindow *window, bool dark);

  /**
   * @brief 设置窗口为无边框模式并启用原生边缘缩放
   * @param window 目标窗口
   * @return 成功返回 true，失败返回 false
   */
  Q_INVOKABLE bool setupFramelessWindow(QQuickWindow *window);

  /**
   * @brief 开始窗口系统拖拽移动
   * @param window 目标窗口
   */
  Q_INVOKABLE void startSystemMove(QQuickWindow *window);

  /**
   * @brief 开始窗口系统缩放
   * @param window 目标窗口
   * @param edges 缩放边缘 (Qt::Edges)
   */
  Q_INVOKABLE void startSystemResize(QQuickWindow *window, int edges);

  /**
   * @brief 获取鼠标在窗口边缘的位置类型
   * @param window 目标窗口
   * @param mouseX 鼠标X坐标（窗口相对）
   * @param mouseY 鼠标Y坐标（窗口相对）
   * @param borderWidth 边缘检测宽度
   * @return 边缘类型 (Qt::Edges)
   */
  Q_INVOKABLE int getResizeEdges(QQuickWindow *window, qreal mouseX,
                                 qreal mouseY, int borderWidth = 8);

  /**
   * @brief 获取适合边缘类型的鼠标光标形状
   * @param edges 边缘类型
   * @return 光标形状 (Qt::CursorShape)
   */
  Q_INVOKABLE int getCursorShapeForEdges(int edges);
};

#endif // WINDOWHELPER_H
