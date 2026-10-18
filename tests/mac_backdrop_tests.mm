#include "core/MacWindowBackdrop.h"
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <memory>
#import <AppKit/AppKit.h>

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QDir::current().filePath("qml/Next/GlassTheme.qml")));
  std::unique_ptr<QObject> theme(component.create());
  const auto check = [](bool ok, const char *message) {
    if (!ok) qFatal("FAIL: %s", message);
  };
  check(theme != nullptr, "load actual application theme");
  QQuickWindow window;
  window.setColor(Qt::transparent);
  window.setFlags(Qt::Window | Qt::FramelessWindowHint);
  window.resize(640, 480);
  window.show();
  QTest::qWait(100);
  auto *backdrop = new MacWindowBackdrop(&window, theme.get());
  if (app.platformName() != "cocoa") {
    check(!theme->property("nativeBackdropAvailable").toBool(), "non-Cocoa fallback stays opaque");
    return 0;
  }
  check(theme->property("nativeBackdropAvailable").toBool(), "native material installed");
  NSView *qtView = reinterpret_cast<NSView *>(window.winId());
  NSView *content = qtView.window.contentView;
  NSVisualEffectView *effect = nil;
  for (NSView *view in qtView.superview.subviews)
    if ([view isKindOfClass:NSVisualEffectView.class]) effect = (NSVisualEffectView *)view;
  check(effect != nil && effect.superview == qtView.superview, "material is Qt view's sibling");
  check(effect.blendingMode == NSVisualEffectBlendingModeBehindWindow, "material samples behind the window");
  check([effect hitTest:NSMakePoint(100, 100)] == nil, "material cannot intercept input");
  check(!qtView.window.opaque, "native window permits translucency");
  theme->setProperty("reducedTransparency", true);
  check(effect.hidden && !theme->property("nativeGlass").toBool(), "reduce transparency disables material");
  check(theme->metaObject()->indexOfProperty("isDark") == -1 && theme->metaObject()->indexOfProperty("followSystem") == -1,
        "new workspace has no theme switching state");
  check([effect.appearance.name isEqualToString:NSAppearanceNameDarkAqua], "material stays dark");
  theme->setProperty("reducedTransparency", false);
  check(effect.hidden == NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency,
        "system accessibility preference respected");
  window.resize(850, 600);
  QTest::qWait(100);
  check(NSEqualRects(effect.frame, qtView.frame), "native material tracks window size");
  check(effect.maskImage && effect.maskImage.capInsets.top == 18, "native material has a resizable rounded mask");
  window.showMaximized();
  QTest::qWait(100);
  check(effect.maskImage == nil, "maximized material fills square window edges");
  window.showNormal();
  QTest::qWait(100);
  check(effect.maskImage != nil, "normal window restores rounded material");
  const NSUInteger count = qtView.superview.subviews.count;
  delete backdrop;
  check(qtView.superview.subviews.count == count - 1 && qtView.window.contentView == content,
        "cleanup removes only the material, preserving Qt contentView");
  check(!theme->property("nativeGlass").toBool(), "cleanup restores fallback background");
  qInfo("PASS: native behind-window material, input pass-through, appearance, accessibility, resize and cleanup");
  return 0;
}
