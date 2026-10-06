#include "persistence/projects/ProjectDatabase.h"
#include "persistence/projects/ProjectRepository.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QUuid>

#include <exception>

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

        {
            tcgprint::projects::ProjectRepository repository(database);
            const auto projects = repository.list();

            out << "TCGPrint Native project compatibility inspection\n";
            out << "Database: " << path << "\n";
            out << "Projects: " << projects.size() << "\n";

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
