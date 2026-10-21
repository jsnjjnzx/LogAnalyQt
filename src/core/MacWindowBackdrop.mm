#include "MacWindowBackdrop.h"
#include <QGuiApplication>
#include <QVariant>
#import <AppKit/AppKit.h>

// The material is decorative; Qt keeps all mouse and keyboard input.
@interface LogAnalyBackdropView : NSVisualEffectView
@end
@implementation LogAnalyBackdropView
- (NSView *)hitTest:(NSPoint)point { return nil; }
@end

MacWindowBackdrop::MacWindowBackdrop(QQuickWindow *window, QObject *theme)
    : QObject(window), m_window(window), m_theme(theme) {
  if (!window || !theme || QGuiApplication::platformName() != "cocoa") return;
  NSView *qtView = reinterpret_cast<NSView *>(window->winId());
  if (!qtView.window || !qtView.superview) return;
  if (window->flags().testFlag(Qt::ExpandedClientAreaHint))
    qtView.window.titleVisibility = NSWindowTitleHidden;
  auto *effect = [[LogAnalyBackdropView alloc] initWithFrame:qtView.frame];
  effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  effect.material = NSVisualEffectMaterialSidebar;
  effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  effect.state = NSVisualEffectStateActive;
  // Keep Qt's contentView and rendering layer intact; place AppKit underneath it.
  [qtView.superview addSubview:effect positioned:NSWindowBelow relativeTo:qtView];
  qtView.window.opaque = NO;
  qtView.window.backgroundColor = NSColor.clearColor;
  qtView.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  m_effect = effect;
  theme->setProperty("nativeBackdropAvailable", true);
  connect(theme, SIGNAL(reducedTransparencyChanged()), this, SLOT(refresh()));
  connect(window, &QWindow::widthChanged, this, &MacWindowBackdrop::refresh);
  connect(window, &QWindow::heightChanged, this, &MacWindowBackdrop::refresh);
  connect(window, &QWindow::visibilityChanged, this, &MacWindowBackdrop::refresh);
  QPointer<MacWindowBackdrop> guard(this);
  m_observer = [[NSWorkspace.sharedWorkspace.notificationCenter
      addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
      object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
        if (guard) guard->refresh();
      }] retain];
  refresh();
}

MacWindowBackdrop::~MacWindowBackdrop() {
  if (m_observer) {
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:(id)m_observer];
    [(id)m_observer release];
  }
  if (m_effect) {
    [(NSView *)m_effect removeFromSuperview];
    [(NSView *)m_effect release];
  }
  if (m_theme) m_theme->setProperty("nativeBackdropAvailable", false);
}

void MacWindowBackdrop::refresh() {
  if (!m_effect || !m_window || !m_theme) return;
  auto *effect = (NSVisualEffectView *)m_effect;
  const bool systemReduced = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency;
  m_theme->setProperty("systemReducedTransparency", systemReduced);
  effect.hidden = systemReduced || m_theme->property("reducedTransparency").toBool();
  effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  const auto visibility = m_window->visibility();
  if (!m_window->flags().testFlag(Qt::FramelessWindowHint) ||
      visibility == QWindow::Maximized || visibility == QWindow::FullScreen) {
    effect.maskImage = nil;
  } else if (!effect.maskImage) {
    // AppKit's mask preserves behind-window sampling while clipping the corners.
    NSImage *mask = [NSImage imageWithSize:NSMakeSize(37, 37) flipped:NO drawingHandler:^BOOL(NSRect rect) {
      [NSColor.whiteColor setFill];
      [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:18 yRadius:18] fill];
      return YES;
    }];
    mask.capInsets = NSEdgeInsetsMake(18, 18, 18, 18);
    mask.resizingMode = NSImageResizingModeStretch;
    effect.maskImage = mask;
  }
}
