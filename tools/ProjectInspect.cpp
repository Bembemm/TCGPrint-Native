#include "persistence/projects/ProjectDatabase.h"
#include "persistence/projects/ProjectRepository.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>

#include <exception>

namespace {

int scalarInt(QSqlDatabase& database, const QString& sql)
{
    QSqlQuery query(database);
    if (!query.exec(sql) || !query.next()) {
        return -1;
    }

    return query.value(0).toInt();
}

QStringList tableNames(QSqlDatabase& database)
{
    QSqlQuery query(database);
    QStringList result;

    if (!query.exec(QStringLiteral(
        "SELECT name FROM sqlite_master "
        "WHERE type = 'table' ORDER BY name"
    ))) {
        return result;
    }

    while (query.next()) {
        result.push_back(query.value(0).toString());
    }

    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList arguments = app.arguments();
    if (arguments.size() != 2) {
        err << "Usage: tcgprint_project_inspect <projects.sqlite>\n";
        return 2;
    }

    const QString path = arguments.at(1);
    const QString connectionName =
        QStringLiteral("tcgprint-inspect-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlDatabase database;

    try {
        database = tcgprint::projects::openProjectDatabaseReadOnly(
            path,
            connectionName
        );

        const int userVersion =
            scalarInt(database, QStringLiteral("PRAGMA user_version"));
        const int recoveryCount =
            scalarInt(
                database,
                QStringLiteral("SELECT COUNT(*) FROM project_recovery")
            );
        const QStringList tables = tableNames(database);

        {
            tcgprint::projects::ProjectRepository repository(database);
            const auto projects = repository.list();

            out << "TCGPrint Native project compatibility inspection\n";
            out << "Database: " << path << "\n";
            out << "SQLite user_version: " << userVersion << "\n";
            out << "Tables: " << tables.join(QStringLiteral(", ")) << "\n";
            out << "Projects: " << projects.size() << "\n";
            out << "Recovery candidates: " << recoveryCount << "\n";

            if (projects.empty()) {
                out << "Status: compatible database opened successfully, "
                       "but the projects table is empty.\n";
            }

            for (const auto& metadata : projects) {
                const auto project = repository.open(metadata.id);

                out
                    << "- "
                    << QString::fromStdString(metadata.name)
                    << " | id="
                    << QString::fromStdString(metadata.id)
                    << " | revision="
                    << metadata.revision
                    << " | schema="
                    << project.snapshot.projectSchemaVersion
                    << " | cards="
                    << project.snapshot.cards.size()
                    << " | physical="
                    << project.snapshot.physicalOrder.instances.size()
                    << "\n";
            }
        }

        tcgprint::projects::closeProjectDatabase(database);
        return 0;
    } catch (const std::exception& error) {
        if (database.isValid()) {
            tcgprint::projects::closeProjectDatabase(database);
        }

        err << "Inspection failed: " << error.what() << "\n";
        return 1;
    }
}
