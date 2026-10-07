cmake_minimum_required(VERSION 3.25)

project(
    TCGPrintNative
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
    Qt6 6.10 REQUIRED
    COMPONENTS
        Core
        Concurrent
        Gui
        Network
        Qml
        Quick
        QuickControls2
        Sql
        Test
)

find_package(ZLIB REQUIRED)

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()

if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_standard_project_setup()

add_library(tcgprint_domain STATIC
    src/domain/geometry/Millimeters.h
    src/domain/geometry/CardFormat.h
    src/domain/geometry/CardFormat.cpp
    src/domain/projects/ProjectSettings.h
    src/domain/projects/ProjectSettings.cpp
    src/domain/cards/WorkingCard.h
    src/domain/cards/WorkingSet.h
    src/domain/cards/WorkingSet.cpp
    src/domain/cards/PhysicalOrder.h
    src/domain/cards/PhysicalOrder.cpp
)

target_include_directories(tcgprint_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(tcgprint_domain PUBLIC cxx_std_23)

add_library(tcgprint_projects STATIC
    src/persistence/projects/ProjectSnapshot.h
    src/persistence/projects/ProjectSnapshot.cpp
    src/persistence/projects/WorkingCardCodec.h
    src/persistence/projects/WorkingCardCodec.cpp
    src/persistence/projects/ProjectSettingsCodec.h
    src/persistence/projects/ProjectSettingsCodec.cpp
    src/persistence/projects/ProjectSettingsReferenceValidation.h
    src/persistence/projects/ProjectSettingsReferenceValidation.cpp
    src/persistence/projects/PersistedWorkingCardValidation.h
    src/persistence/projects/PersistedWorkingCardValidation.cpp
    src/persistence/projects/ProjectRepository.h
    src/persistence/projects/ProjectRepository.cpp
    src/persistence/projects/ProjectDatabase.h
    src/persistence/projects/ProjectDatabase.cpp
)

target_include_directories(tcgprint_projects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_projects
    PUBLIC
        tcgprint_domain
        Qt6::Core
        Qt6::Sql
)

target_compile_features(tcgprint_projects PUBLIC cxx_std_23)

add_library(tcgprint_application STATIC
    src/application/ProjectSession.h
    src/application/ProjectSession.cpp
    src/application/ProjectAutosaveController.h
    src/application/ProjectAutosaveController.cpp
)

target_include_directories(tcgprint_application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_application
    PUBLIC
        tcgprint_projects
        Qt6::Core
)

target_compile_features(tcgprint_application PUBLIC cxx_std_23)


add_library(tcgprint_import STATIC
    src/import/ImportTypes.h
    src/import/ImportFailure.h
    src/import/ImportDetection.h
    src/import/ImportDetection.cpp
    src/import/ImportLimits.h
    src/import/ImportLimits.cpp
    src/import/SourcePath.h
    src/import/SourcePath.cpp
    src/import/TextImport.h
    src/import/TextImport.cpp
    src/import/CsvImport.h
    src/import/CsvImport.cpp
    src/import/JsonImport.h
    src/import/JsonImport.cpp
    src/import/XmlImport.h
    src/import/XmlImport.cpp
    src/import/ImageImport.h
    src/import/ImageImport.cpp
    src/import/ZipImport.h
    src/import/ZipImport.cpp
    src/import/UrlRegistry.h
    src/import/UrlRegistry.cpp
    src/import/UrlTransportPolicy.h
    src/import/UrlTransportPolicy.cpp
    src/import/UrlHttpTransport.h
    src/import/UrlHttpTransport.cpp
    src/import/DirectFileUrlImport.h
    src/import/DirectFileUrlImport.cpp
    src/import/ScryfallUrlImport.h
    src/import/ScryfallUrlImport.cpp
    src/import/ArchidektUrlImport.h
    src/import/ArchidektUrlImport.cpp
    src/import/CubeCobraUrlImport.h
    src/import/CubeCobraUrlImport.cpp
)

target_include_directories(tcgprint_import
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_import
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
    PRIVATE
        ZLIB::ZLIB
)

target_compile_features(tcgprint_import PUBLIC cxx_std_23)

add_library(tcgprint_runtime STATIC
    src/runtime/BackgroundJob.h
    src/runtime/BackgroundJob.cpp
)

target_include_directories(tcgprint_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_runtime
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
)

target_compile_features(tcgprint_runtime PUBLIC cxx_std_23)

qt_add_executable(tcgprint
    app/main.cpp
    src/app/AppInfo.cpp
    src/app/AppInfo.h
    src/app/Logging.cpp
    src/app/Logging.h
)

qt_add_qml_module(tcgprint
    URI TCGPrint
    VERSION 1.0
    QML_FILES
        app/qml/App.qml
)

target_include_directories(tcgprint
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint
    PRIVATE
        tcgprint_domain
        tcgprint_runtime
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(tcgprint PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

enable_testing()

qt_add_executable(tcgprint_tests
    tests/AppInfoTest.cpp
    src/app/AppInfo.cpp
    src/app/AppInfo.h
)

target_include_directories(tcgprint_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_tests
    PRIVATE
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_tests
    COMMAND tcgprint_tests
)

qt_add_executable(tcgprint_geometry_tests
    tests/domain/GeometryTest.cpp
)

target_link_libraries(tcgprint_geometry_tests
    PRIVATE
        tcgprint_domain
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_geometry_tests
    COMMAND tcgprint_geometry_tests
)

qt_add_executable(tcgprint_project_settings_tests
    tests/domain/ProjectSettingsTest.cpp
)

target_link_libraries(tcgprint_project_settings_tests
    PRIVATE
        tcgprint_domain
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_project_settings_tests
    COMMAND tcgprint_project_settings_tests
)

qt_add_executable(tcgprint_physical_order_tests
    tests/domain/PhysicalOrderTest.cpp
)

target_link_libraries(tcgprint_physical_order_tests
    PRIVATE
        tcgprint_domain
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_physical_order_tests
    COMMAND tcgprint_physical_order_tests
)

qt_add_executable(tcgprint_working_set_tests
    tests/domain/WorkingSetTest.cpp
)

target_link_libraries(tcgprint_working_set_tests
    PRIVATE
        tcgprint_domain
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_working_set_tests
    COMMAND tcgprint_working_set_tests
)

qt_add_executable(tcgprint_background_job_tests
    tests/runtime/BackgroundJobTest.cpp
)

target_link_libraries(tcgprint_background_job_tests
    PRIVATE
        tcgprint_runtime
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_background_job_tests
    COMMAND tcgprint_background_job_tests
)


qt_add_executable(tcgprint_import_detection_tests
    tests/import/ImportDetectionTest.cpp
)

target_link_libraries(tcgprint_import_detection_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_detection_tests
    COMMAND tcgprint_import_detection_tests
)

qt_add_executable(tcgprint_import_boundary_tests
    tests/import/ImportBoundaryTest.cpp
)

target_link_libraries(tcgprint_import_boundary_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_boundary_tests
    COMMAND tcgprint_import_boundary_tests
)


qt_add_executable(tcgprint_import_text_tests
    tests/import/TextImportTest.cpp
)

target_link_libraries(tcgprint_import_text_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_text_tests
    COMMAND tcgprint_import_text_tests
)


qt_add_executable(tcgprint_import_csv_tests
    tests/import/CsvImportTest.cpp
)

target_link_libraries(tcgprint_import_csv_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_csv_tests
    COMMAND tcgprint_import_csv_tests
)


qt_add_executable(tcgprint_import_json_tests
    tests/import/JsonImportTest.cpp
)

target_link_libraries(tcgprint_import_json_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_json_tests
    COMMAND tcgprint_import_json_tests
)


qt_add_executable(tcgprint_import_xml_tests
    tests/import/XmlImportTest.cpp
)

target_link_libraries(tcgprint_import_xml_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_xml_tests
    COMMAND tcgprint_import_xml_tests
)


qt_add_executable(tcgprint_import_image_tests
    tests/import/ImageImportTest.cpp
)

target_link_libraries(tcgprint_import_image_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)

add_test(
    NAME tcgprint_import_image_tests
    COMMAND tcgprint_import_image_tests
)


qt_add_executable(tcgprint_import_zip_tests
    tests/import/ZipImportTest.cpp
)

target_link_libraries(tcgprint_import_zip_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
        ZLIB::ZLIB
)

add_test(
    NAME tcgprint_import_zip_tests
    COMMAND tcgprint_import_zip_tests
)

qt_add_executable(tcgprint_import_url_registry_tests
    tests/import/UrlRegistryTest.cpp
)

target_link_libraries(tcgprint_import_url_registry_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_url_registry_tests
    COMMAND tcgprint_import_url_registry_tests
)

qt_add_executable(tcgprint_import_url_transport_policy_tests
    tests/import/UrlTransportPolicyTest.cpp
)

target_link_libraries(tcgprint_import_url_transport_policy_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME tcgprint_import_url_transport_policy_tests
    COMMAND tcgprint_import_url_transport_policy_tests
)

qt_add_executable(tcgprint_import_url_http_transport_tests
    tests/import/UrlHttpTransportTest.cpp
)

target_link_libraries(tcgprint_import_url_http_transport_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME tcgprint_import_url_http_transport_tests
    COMMAND tcgprint_import_url_http_transport_tests
)

qt_add_executable(tcgprint_import_direct_file_url_tests
    tests/import/DirectFileUrlImportTest.cpp
)

target_link_libraries(tcgprint_import_direct_file_url_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME tcgprint_import_direct_file_url_tests
    COMMAND tcgprint_import_direct_file_url_tests
)

qt_add_executable(tcgprint_import_scryfall_url_tests
    tests/import/ScryfallUrlImportTest.cpp
)

target_link_libraries(tcgprint_import_scryfall_url_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_import_scryfall_url_tests
    COMMAND tcgprint_import_scryfall_url_tests
)

qt_add_executable(tcgprint_import_archidekt_url_tests
    tests/import/ArchidektUrlImportTest.cpp
)

target_link_libraries(tcgprint_import_archidekt_url_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME tcgprint_import_archidekt_url_tests
    COMMAND tcgprint_import_archidekt_url_tests
)

qt_add_executable(tcgprint_import_cubecobra_url_tests
    tests/import/CubeCobraUrlImportTest.cpp
)

target_link_libraries(tcgprint_import_cubecobra_url_tests
    PRIVATE
        tcgprint_import
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME tcgprint_import_cubecobra_url_tests
    COMMAND tcgprint_import_cubecobra_url_tests
)

qt_add_executable(tcgprint_benchmarks
    benchmarks/CoreBenchmarks.cpp
)

target_link_libraries(tcgprint_benchmarks
    PRIVATE
        tcgprint_domain
        Qt6::Core
        Qt6::Test
)


qt_add_executable(tcgprint_project_snapshot_tests
    tests/persistence/ProjectSnapshotTest.h
    tests/persistence/ProjectSnapshotTest.cpp
)

target_link_libraries(tcgprint_project_snapshot_tests
    PRIVATE
        tcgprint_projects
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_project_snapshot_tests
    COMMAND tcgprint_project_snapshot_tests
)


qt_add_executable(tcgprint_project_repository_tests
    tests/persistence/ProjectRepositoryTest.cpp
)

target_link_libraries(tcgprint_project_repository_tests
    PRIVATE
        tcgprint_projects
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

add_test(
    NAME tcgprint_project_repository_tests
    COMMAND tcgprint_project_repository_tests
)


qt_add_executable(tcgprint_project_database_tests
    tests/persistence/ProjectDatabaseTest.cpp
)

target_link_libraries(tcgprint_project_database_tests
    PRIVATE
        tcgprint_projects
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

add_test(
    NAME tcgprint_project_database_tests
    COMMAND tcgprint_project_database_tests
)


qt_add_executable(tcgprint_project_inspect
    tools/ProjectInspect.cpp
)

target_link_libraries(tcgprint_project_inspect
    PRIVATE
        tcgprint_projects
        Qt6::Core
        Qt6::Sql
)


qt_add_executable(tcgprint_project_session_tests
    tests/application/ProjectSessionTest.cpp
)

target_link_libraries(tcgprint_project_session_tests
    PRIVATE
        tcgprint_application
        tcgprint_projects
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

add_test(
    NAME tcgprint_project_session_tests
    COMMAND tcgprint_project_session_tests
)

qt_add_executable(tcgprint_project_autosave_tests
    tests/application/ProjectAutosaveControllerTest.cpp
)

target_link_libraries(tcgprint_project_autosave_tests
    PRIVATE
        tcgprint_application
        tcgprint_projects
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

add_test(
    NAME tcgprint_project_autosave_tests
    COMMAND tcgprint_project_autosave_tests
)


qt_add_executable(tcgprint_project_migration_tests
    tests/persistence/ProjectSnapshotMigrationTest.cpp
)

target_link_libraries(tcgprint_project_migration_tests
    PRIVATE
        tcgprint_projects
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_project_migration_tests
    COMMAND tcgprint_project_migration_tests
)
