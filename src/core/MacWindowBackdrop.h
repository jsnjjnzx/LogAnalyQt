#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>

class MacWindowBackdrop : public QObject {
  Q_OBJECT
public:
  MacWindowBackdrop(QQuickWindow *window, QObject *theme);
  ~MacWindowBackdrop() override;

private slots:
  void refresh();

private:
  QPointer<QQuickWindow> m_window;
  QPointer<QObject> m_theme;
  void *m_effect = nullptr;
  void *m_observer = nullptr;
};
