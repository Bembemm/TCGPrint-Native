#include <QtTest>

#include "domain/cards/PhysicalOrder.h"

#include <vector>

using namespace tcgprint::cards;

class PhysicalOrderTest final : public QObject
{
    Q_OBJECT

private slots:

    void createsDeterministicLegacyOrder()
    {
        const std::vector<WorkingCard> cards{
            WorkingCard{
                .id = "later",
                .displayName = "Later",
                .quantity = 1,
                .order = 20,
            },
            WorkingCard{
                .id = "first",
                .displayName = "First",
                .quantity = 2,
                .order = 10,
            },
        };

        const PhysicalOrder order = makeLegacyPhysicalOrder(cards);

        QCOMPARE(order.instances.size(), std::size_t{3});
        QCOMPARE(order.nextInstanceId, PhysicalInstanceId{4});

        QCOMPARE(order.instances[0].id, PhysicalInstanceId{1});
        QCOMPARE(order.instances[0].workingCardId, std::string("first"));
        QCOMPARE(order.instances[1].id, PhysicalInstanceId{2});
        QCOMPARE(order.instances[1].workingCardId, std::string("first"));
        QCOMPARE(order.instances[2].id, PhysicalInstanceId{3});
        QCOMPARE(order.instances[2].workingCardId, std::string("later"));

        QCOMPARE(
            physicalInstanceIdString(order.instances[1].id),
            std::string("instance-2")
        );
    }

    void reordersOnePhysicalCopyWithoutChangingItsIdentity()
    {
        const std::vector<WorkingCard> cards{
            WorkingCard{
                .id = "island",
                .displayName = "Island",
                .quantity = 2,
                .order = 0,
            },
            WorkingCard{
                .id = "mountain",
                .displayName = "Mountain",
                .quantity = 1,
                .order = 1,
            },
        };

        PhysicalOrder order = makeLegacyPhysicalOrder(cards);

        QVERIFY(
            movePhysicalInstance(
                order,
                PhysicalInstanceId{3},
                PhysicalInstanceId{1},
                InsertPosition::Before
            )
        );

        QCOMPARE(order.instances[0].id, PhysicalInstanceId{3});
        QCOMPARE(order.instances[0].workingCardId, std::string("mountain"));
        QCOMPARE(order.instances[1].id, PhysicalInstanceId{1});
        QCOMPARE(order.instances[2].id, PhysicalInstanceId{2});
        QCOMPARE(order.nextInstanceId, PhysicalInstanceId{4});
    }

    void rejectsUnknownOrSelfMoves()
    {
        PhysicalOrder order = makeLegacyPhysicalOrder({
            WorkingCard{
                .id = "card",
                .displayName = "Card",
                .quantity = 1,
                .order = 0,
            },
        });

        QVERIFY(!movePhysicalInstance(
            order,
            PhysicalInstanceId{1},
            PhysicalInstanceId{1},
            InsertPosition::After
        ));

        QVERIFY(!movePhysicalInstance(
            order,
            PhysicalInstanceId{99},
            PhysicalInstanceId{1},
            InsertPosition::After
        ));

        QCOMPARE(order.instances.size(), std::size_t{1});
        QCOMPARE(order.instances[0].id, PhysicalInstanceId{1});
    }
};

QTEST_APPLESS_MAIN(PhysicalOrderTest)

#include "PhysicalOrderTest.moc"
