#include <QtTest>

#include "domain/cards/PhysicalOrder.h"

#include <vector>

using namespace tcgprint::cards;

class PhysicalOrderTest final : public QObject
{
    Q_OBJECT

private slots:

    void persistsInstanceIdsInLegacyFormat()
    {
        QCOMPARE(
            QString::fromStdString(physicalInstanceIdString(42)),
            QStringLiteral("instance-42")
        );
        QCOMPARE(parsePhysicalInstanceIdString("instance-42").value(), PhysicalInstanceId{42});
        QVERIFY(!parsePhysicalInstanceIdString("instance-0"));
        QVERIFY(!parsePhysicalInstanceIdString("instance-01"));
        QVERIFY(!parsePhysicalInstanceIdString("card-42"));
        QVERIFY(!parsePhysicalInstanceIdString("instance-9007199254740992"));
    }

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
        QCOMPARE(QString::fromStdString(order.instances[0].workingCardId), QStringLiteral("first"));
        QCOMPARE(order.instances[1].id, PhysicalInstanceId{2});
        QCOMPARE(order.instances[2].id, PhysicalInstanceId{3});
        QCOMPARE(QString::fromStdString(order.instances[2].workingCardId), QStringLiteral("later"));
    }

    void validatesExactQuantityCoverage()
    {
        const std::vector<WorkingCard> cards{
            WorkingCard{ .id = "a", .quantity = 2, .order = 0 },
            WorkingCard{ .id = "b", .quantity = 1, .order = 1 },
        };

        const PhysicalOrder valid = makeLegacyPhysicalOrder(cards);
        QCOMPARE(validatePhysicalOrder(cards, valid), valid);

        PhysicalOrder broken = valid;
        broken.instances.pop_back();

        try {
            static_cast<void>(validatePhysicalOrder(cards, broken));
            QFAIL("Expected validation to fail.");
        } catch (const PhysicalOrderError& error) {
            QCOMPARE(error.code(), PhysicalOrderErrorCode::InvalidPhysicalOrder);
        }
    }

    void reconcilesQuantityWithoutChangingRetainedIds()
    {
        std::vector<WorkingCard> cards{
            WorkingCard{ .id = "a", .quantity = 2, .order = 0 },
            WorkingCard{ .id = "b", .quantity = 1, .order = 1 },
        };

        PhysicalOrder previous = makeLegacyPhysicalOrder(cards);
        previous = movePhysicalInstance(previous, 3, 1, InsertPosition::Before);

        cards[0].quantity = 3;
        const PhysicalOrder reconciled = reconcilePhysicalOrder(cards, previous);

        QCOMPARE(reconciled.instances.size(), std::size_t{4});
        QCOMPARE(reconciled.instances[0].id, PhysicalInstanceId{3});
        QCOMPARE(reconciled.instances[1].id, PhysicalInstanceId{1});
        QCOMPARE(reconciled.instances[2].id, PhysicalInstanceId{2});
        QCOMPARE(reconciled.instances[3].id, PhysicalInstanceId{4});
        QCOMPARE(reconciled.nextInstanceId, PhysicalInstanceId{5});

        QCOMPARE(validatePhysicalOrder(cards, reconciled), reconciled);
    }

    void addsRemovesAndReplacesPhysicalInstances()
    {
        const PhysicalOrder initial = makeLegacyPhysicalOrder({
            WorkingCard{ .id = "a", .quantity = 1, .order = 0 },
            WorkingCard{ .id = "b", .quantity = 1, .order = 1 },
        });

        const AddPhysicalInstanceResult added =
            addPhysicalInstance(initial, "a");

        QCOMPARE(added.instance.id, PhysicalInstanceId{3});
        QCOMPARE(added.order.instances.size(), std::size_t{3});
        QCOMPARE(added.order.instances[1].id, PhysicalInstanceId{3});

        const PhysicalOrder replaced =
            replacePhysicalInstanceCard(added.order, 3, "b");
        QCOMPARE(
            QString::fromStdString(replaced.instances[1].workingCardId),
            QStringLiteral("b")
        );

        const RemovePhysicalInstanceResult removed =
            removePhysicalInstance(replaced, 3);

        QCOMPARE(removed.removed.id, PhysicalInstanceId{3});
        QCOMPARE(removed.order.instances.size(), std::size_t{2});
        QCOMPARE(removed.order.nextInstanceId, PhysicalInstanceId{4});
    }

    void reordersAndMovesToEndWithoutChangingIdentity()
    {
        const PhysicalOrder initial = makeLegacyPhysicalOrder({
            WorkingCard{ .id = "island", .quantity = 2, .order = 0 },
            WorkingCard{ .id = "mountain", .quantity = 1, .order = 1 },
        });

        const PhysicalOrder before =
            movePhysicalInstance(initial, 3, 1, InsertPosition::Before);

        QCOMPARE(before.instances[0].id, PhysicalInstanceId{3});
        QCOMPARE(before.instances[1].id, PhysicalInstanceId{1});
        QCOMPARE(before.instances[2].id, PhysicalInstanceId{2});

        const PhysicalOrder end =
            movePhysicalInstance(before, 3, std::nullopt);

        QCOMPARE(end.instances[0].id, PhysicalInstanceId{1});
        QCOMPARE(end.instances[1].id, PhysicalInstanceId{2});
        QCOMPARE(end.instances[2].id, PhysicalInstanceId{3});
        QCOMPARE(end.nextInstanceId, PhysicalInstanceId{4});
    }

    void rejectsDuplicateCardsAndUnknownMoves()
    {
        try {
            static_cast<void>(makeLegacyPhysicalOrder({
                WorkingCard{ .id = "same", .quantity = 1, .order = 0 },
                WorkingCard{ .id = "same", .quantity = 1, .order = 1 },
            }));
            QFAIL("Expected duplicate card IDs to fail.");
        } catch (const PhysicalOrderError& error) {
            QCOMPARE(error.code(), PhysicalOrderErrorCode::InvalidPhysicalOrder);
        }

        const PhysicalOrder order = makeLegacyPhysicalOrder({
            WorkingCard{ .id = "card", .quantity = 1, .order = 0 },
        });

        try {
            static_cast<void>(
                movePhysicalInstance(order, 99, 1, InsertPosition::After)
            );
            QFAIL("Expected unknown source to fail.");
        } catch (const PhysicalOrderError& error) {
            QCOMPARE(error.code(), PhysicalOrderErrorCode::PhysicalInstanceNotFound);
        }
    }
};

QTEST_APPLESS_MAIN(PhysicalOrderTest)

#include "PhysicalOrderTest.moc"
