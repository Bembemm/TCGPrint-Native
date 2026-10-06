#include <QtTest>

#include "domain/cards/PhysicalOrder.h"

#include <vector>

using namespace tcgprint::cards;

class CoreBenchmarks final : public QObject
{
    Q_OBJECT

private slots:

    void buildFiveHundredPhysicalCopies()
    {
        std::vector<WorkingCard> cards;
        cards.reserve(100);

        for (int index = 0; index < 100; ++index) {
            cards.push_back(WorkingCard{
                .id = "card-" + std::to_string(index),
                .displayName = "Card",
                .quantity = 5,
                .order = index,
            });
        }

        QBENCHMARK {
            const PhysicalOrder order = makeLegacyPhysicalOrder(cards);
            Q_ASSERT(order.instances.size() == 500);
        }
    }

    void movePhysicalInstanceInFiveHundredCopyOrder()
    {
        std::vector<WorkingCard> cards{
            WorkingCard{
                .id = "card",
                .displayName = "Card",
                .quantity = 500,
                .order = 0,
            },
        };

        const PhysicalOrder baseline = makeLegacyPhysicalOrder(cards);

        QBENCHMARK {
            PhysicalOrder order = baseline;
            const bool moved = movePhysicalInstance(
                order,
                PhysicalInstanceId{500},
                PhysicalInstanceId{1},
                InsertPosition::Before
            );
            Q_ASSERT(moved);
        }
    }
};

QTEST_APPLESS_MAIN(CoreBenchmarks)

#include "CoreBenchmarks.moc"
