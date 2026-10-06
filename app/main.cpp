#include "app/Logging.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("TCGPrint Native"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("TCGPrint"));

    qSetMessagePattern(
        QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] "
                       "[%{type}] [%{category}] %{message}")
    );

    const QString logPath = tcgprint::logging::initialize();

    qInfo().noquote()
        << QStringLiteral("Starting TCGPrint Native 0.1.0; log=%1")
               .arg(logPath.isEmpty() ? QStringLiteral("<stderr-only>") : logPath);

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] {
            QCoreApplication::exit(EXIT_FAILURE);
        },
        Qt::QueuedConnection
    );

    engine.loadFromModule("TCGPrint", "App");

    const int result = app.exec();
    tcgprint::logging::shutdown();
    return result;
}
