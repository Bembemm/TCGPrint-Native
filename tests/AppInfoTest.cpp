#include <QtTest>

#include "app/AppInfo.h"

class AppInfoTest final : public QObject
{
    Q_OBJECT

private slots:

    void exposesProductName()
    {
        QCOMPARE(
            tcgprint::productName(),
            QStringLiteral("TCGPrint Native")
        );
    }

    void exposesCurrentMilestone()
    {
        QCOMPARE(
            tcgprint::nativeMilestone(),
            QStringLiteral("N1")
        );
    }
};

QTEST_APPLESS_MAIN(AppInfoTest)

#include "AppInfoTest.moc"
