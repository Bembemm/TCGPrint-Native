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

    qInfo() << "Starting TCGPrint Native";

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

    return app.exec();
}
