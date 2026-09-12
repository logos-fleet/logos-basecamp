#include <QtQuickTest/quicktest.h>
#include <QtQml/qqml.h>
#include <QtQml/QQmlEngine>

#include "AppsFilterProxy.h"
#include "InstallEnums.h"
#include "ModulesFilterProxy.h"

class Setup : public QObject {
    Q_OBJECT
public:
    Setup() = default;

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        qmlRegisterUncreatableType<InstallStage>("Basecamp.Backend", 1, 0,
            "InstallStage",
            QStringLiteral("Use InstallStage.Downloading etc.; not instantiable."));
        qmlRegisterUncreatableType<InstallStatus>("Basecamp.Backend", 1, 0,
            "InstallStatus",
            QStringLiteral("Use InstallStatus.Installed etc.; not instantiable."));
        qmlRegisterType<AppsFilterProxy>("Basecamp.Backend", 1, 0, "AppsFilterProxy");
        // The Settings inspectors declare one of these themselves, so the
        // views cannot be instantiated without it.
        qmlRegisterType<ModulesFilterProxy>("Basecamp.Backend", 1, 0, "ModulesFilterProxy");

        // Basecamp.Settings / Basecamp.Common, mirrored into the build tree by
        // this suite's CMakeLists — see the comment there.
        engine->addImportPath(QStringLiteral(BASECAMP_QML_IMPORT_PATH));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_tests, Setup)

#include "main.moc"
