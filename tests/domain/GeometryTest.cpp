#include <QtTest>

#include "domain/geometry/CardFormat.h"
#include "domain/geometry/Millimeters.h"

#include <cmath>

using tcgprint::geometry::CardFormat;
using tcgprint::geometry::Millimeters;
using tcgprint::geometry::toPdfPoints;

class GeometryTest final : public QObject
{
    Q_OBJECT

private slots:

    void convertsPhysicalInchToPdfPoints()
    {
        const double points = toPdfPoints(Millimeters(25.4));

        QVERIFY(std::abs(points - 72.0) < 1e-12);
    }

    void preservesMagicStandardPhysicalContract()
    {
        const CardFormat format = CardFormat::magicStandard();

        QVERIFY(format.isValid());
        QCOMPARE(QString::fromStdString(format.id), QStringLiteral("magic-standard"));
        QCOMPARE(QString::fromStdString(format.name), QStringLiteral("Magic Standard"));
        QCOMPARE(format.trimWidth.value(), 63.5);
        QCOMPARE(format.trimHeight.value(), 88.9);
        QCOMPARE(format.cornerRadius.value(), 3.175);
    }
};

QTEST_APPLESS_MAIN(GeometryTest)

#include "GeometryTest.moc"
