#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("LogAnalyQt Preview");
    app.setApplicationDisplayName(QStringLiteral("通联工坊预览"));
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl("qrc:/preview/PreviewMain.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    const QStringList args = app.arguments();
    const int capture = args.indexOf("--screenshot");
    if (capture >= 0) {
        if (capture + 1 >= args.size()) return 2;
        const int size = args.indexOf("--size");
        if (size >= 0 && size + 2 < args.size())
            window->resize(args[size + 1].toInt(), args[size + 2].toInt());
        QTimer::singleShot(1200, &app, [&, window] {
            const QImage frame = window->grabWindow();
            app.exit(!frame.isNull() && frame.save(args[capture + 1]) ? 0 : 3);
        });
    }
    return app.exec();
}
