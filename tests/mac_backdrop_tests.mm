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
  window.setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
  window.show();
  QTest::qWait(100);
  backdrop = new MacWindowBackdrop(&window, theme.get());
  auto checkSidebarChrome = [&] {
    qtView = reinterpret_cast<NSView *>(window.winId());
    check((qtView.window.styleMask & NSWindowStyleMaskTitled) != 0, "native title bar is present");
    check((qtView.window.styleMask & NSWindowStyleMaskFullSizeContentView) != 0 && qtView.window.titlebarAppearsTransparent,
          "content extends through the transparent native title bar");
    check(qtView.window.titleVisibility == NSWindowTitleHidden, "native window title is hidden");
    check(qAbs(qtView.frame.size.height - qtView.window.frame.size.height) < 1,
          "native title bar does not reserve a strip above the content");
    for (NSWindowButton button : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
      NSButton *control = [qtView.window standardWindowButton:button];
      check(control && !control.hidden && control.enabled, "native traffic-light control is visible and enabled");
      const NSRect bounds = [control convertRect:control.bounds toView:qtView];
      const CGFloat top = qtView.flipped ? NSMinY(bounds) : qtView.bounds.size.height - NSMaxY(bounds);
      check(bounds.origin.x >= 0 && NSMaxX(bounds) < 120 && top >= 0 && top + bounds.size.height <= 46,
            "native traffic lights stay inside the reserved sidebar header");
    }
  };
  checkSidebarChrome();
  for (NSView *view in qtView.superview.subviews)
    if ([view isKindOfClass:NSVisualEffectView.class])
      check(((NSVisualEffectView *)view).maskImage == nil, "native frame owns the corner clipping");
  [[qtView.window standardWindowButton:NSWindowMiniaturizeButton] performClick:nil];
  QTest::qWait(100);
  check(qtView.window.miniaturized, "native minimize button minimizes the window");
  [qtView.window deminiaturize:nil];
  QTest::qWait(100);
  check(!qtView.window.miniaturized, "native window restores after minimization");
  window.resize(760, 520);
  QTest::qWait(100);
  check(window.size() == QSize(760, 520), "native window resizes");
  checkSidebarChrome();
  const QRect normalGeometry = window.geometry();
  bool fullscreenSettled = false;
  bool *settled = &fullscreenSettled;
  auto didTransition = ^(NSNotification *) { *settled = true; };
  auto *notifications = NSNotificationCenter.defaultCenter;
  id entered = [notifications addObserverForName:NSWindowDidEnterFullScreenNotification object:qtView.window queue:nil usingBlock:didTransition];
  id exited = [notifications addObserverForName:NSWindowDidExitFullScreenNotification object:qtView.window queue:nil usingBlock:didTransition];
  for (int cycle = 0; cycle < 2; ++cycle) {
    fullscreenSettled = false;
    window.showFullScreen();
    check(QTest::qWaitFor([&] { return fullscreenSettled; }, 8000), "native fullscreen entry finishes");
    check(window.visibility() == QWindow::FullScreen && (qtView.window.styleMask & NSWindowStyleMaskFullScreen),
          "native window enters fullscreen");
    fullscreenSettled = false;
    window.showNormal();
    check(QTest::qWaitFor([&] { return fullscreenSettled; }, 8000), "native fullscreen exit finishes");
    check(window.visibility() == QWindow::Windowed && !(qtView.window.styleMask & NSWindowStyleMaskFullScreen),
          "native window exits fullscreen");
    check(window.geometry() == normalGeometry, "fullscreen round trip preserves window geometry");
    checkSidebarChrome();
  }
  [notifications removeObserver:entered];
  [notifications removeObserver:exited];
  delete backdrop;
  qInfo("PASS: native material, sidebar traffic lights, minimize/restore, resize, two fullscreen round trips and cleanup");
  return 0;
}
