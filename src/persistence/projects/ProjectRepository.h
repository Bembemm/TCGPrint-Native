#pragma once

#include "persistence/projects/ProjectSnapshot.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcgprint::projects {

inline constexpr int LegacyProjectDatabaseSchemaMax = 5;
inline constexpr int InitialProjectRevision = 1;

enum class ProjectRepositoryErrorCode
{
    ProjectNotFound,
    ProjectRevisionConflict,
    ProjectRecoveryNotFound,
    ProjectRecoveryExists,
    InvalidDatabaseSchema,
    DatabaseError,
};

class ProjectRepositoryError final : public std::runtime_error
{
public:
    ProjectRepositoryError(
        ProjectRepositoryErrorCode code,
        std::string message,
        std::optional<int> expectedRevision = std::nullopt,
        std::optional<int> actualRevision = std::nullopt
    );

    [[nodiscard]] ProjectRepositoryErrorCode code() const noexcept;
    [[nodiscard]] std::optional<int> expectedRevision() const noexcept;
    [[nodiscard]] std::optional<int> actualRevision() const noexcept;

private:
    ProjectRepositoryErrorCode code_;
    std::optional<int> expectedRevision_;
    std::optional<int> actualRevision_;
};

struct ProjectMetadata final
{
    std::string id;
    std::string name;
    int projectSchemaVersion{CurrentProjectSchemaVersion};
    int revision{InitialProjectRevision};
    std::string createdAt;
    std::string updatedAt;
    std::string autosavedAt;
};

struct ProjectRecord final
{
    ProjectMetadata metadata;
    ProjectSnapshotCompat snapshot;
};

struct ProjectRecoveryRecord final
{
    std::string projectId;
    int baseRevision{InitialProjectRevision};
    int projectSchemaVersion{CurrentProjectSchemaVersion};
    ProjectSnapshotCompat snapshot;
    std::string createdAt;
};

void initializeProjectDatabase(QSqlDatabase& database);

class ProjectRepository final
{
public:
    explicit ProjectRepository(QSqlDatabase database);

    [[nodiscard]] std::vector<ProjectMetadata> list() const;
    [[nodiscard]] std::optional<ProjectRecord> get(const std::string& projectId) const;
    [[nodiscard]] ProjectRecord open(const std::string& projectId) const;

    [[nodiscard]] ProjectRecord create(
        const ProjectSnapshotCompat& snapshot,
        std::string name = "Novo projeto"
    );

    [[nodiscard]] ProjectRecord save(
        const std::string& projectId,
        int expectedRevision,
        const ProjectSnapshotCompat& snapshot
    );

    void remove(const std::string& projectId);

    [[nodiscard]] ProjectRecoveryRecord stageRecovery(
        const std::string& projectId,
        int baseRevision,
        const ProjectSnapshotCompat& snapshot
    );

    [[nodiscard]] std::optional<ProjectRecoveryRecord> readRecovery(
        const std::string& projectId
    ) const;

    [[nodiscard]] ProjectRecord promoteRecovery(const std::string& projectId);
    void discardRecovery(const std::string& projectId);

private:
    QSqlDatabase database_;
};

} // namespace tcgprint::projects
