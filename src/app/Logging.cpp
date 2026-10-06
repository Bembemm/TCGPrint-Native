#include "app/Logging.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

QMutex logMutex;
std::unique_ptr<QFile> logFile;

void messageHandler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message
)
{
    const QString formatted = qFormatLogMessage(type, context, message);
    const QByteArray utf8 = formatted.toUtf8();

    {
        const QMutexLocker locker(&logMutex);

        if (logFile && logFile->isOpen()) {
            logFile->write(utf8);
            logFile->write("\n");
            logFile->flush();
        }
    }

    std::fwrite(utf8.constData(), 1, static_cast<std::size_t>(utf8.size()), stderr);
    std::fwrite("\n", 1, 1, stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

namespace tcgprint::logging {

QString initialize()
{
    const QString directoryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");

    QDir directory;
    if (!directory.mkpath(directoryPath)) {
        return {};
    }

    auto candidate = std::make_unique<QFile>(
        directoryPath + QStringLiteral("/tcgprint-native.log")
    );

    if (!candidate->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return {};
    }

    const QString path = candidate->fileName();

    {
        const QMutexLocker locker(&logMutex);
        logFile = std::move(candidate);
    }

    qInstallMessageHandler(messageHandler);
    return path;
}

void shutdown()
{
    qInstallMessageHandler(nullptr);

    const QMutexLocker locker(&logMutex);
    if (logFile) {
        logFile->flush();
        logFile->close();
        logFile.reset();
    }
}

} // namespace tcgprint::logging
