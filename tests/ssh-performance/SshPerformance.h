#pragma once

#include <QStringList>
class QGuiApplication;
class QQmlApplicationEngine;

void registerSshPerformanceTerminal(const QString &root);
int runSshPerformance(QGuiApplication &app, const QStringList &args,
                      QQmlApplicationEngine *fullApplication = nullptr);
