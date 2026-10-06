#include <QtTest>

#include "domain/cards/WorkingSet.h"

using namespace tcgprint::cards;

namespace {

WorkingCard card(
    std::string id,
    std::uint32_t quantity,
    int order
)
{
    return WorkingCard{
        .id = std::move(id),
        .displayName = "Card",
        .quantity = quantity,
        .order = order,
    };
}

} // namespace

class WorkingSetTest final : public QObject
{
    Q_OBJECT

private slots:

    void normalizesOrderAndSelection()
    {
        const WorkingSetState state = createWorkingSetState({
            card("b", 1, 20),
            card("a", 1, 10),
        });

        QCOMPARE(state.cards.size(), std::size_t{2});
        QCOMPARE(
            QString::fromStdString(state.cards[0].id),
            QStringLiteral("a")
        );
        QCOMPARE(state.cards[0].order, 0);
        QCOMPARE(state.cards[1].order, 1);
        QVERIFY(state.selectedCardId.has_value());
        QCOMPARE(
            QString::fromStdString(*state.selectedCardId),
            QStringLiteral("a")
        );
    }

    void changesQuantityAndPreservesRetainedInstanceIds()
    {
        WorkingSetState state = createWorkingSetState({
            card("a", 2, 0),
            card("b", 1, 1),
        });

        state = reorderWorkingCardPhysicalInstance(
            state,
            PhysicalInstanceId{3},
            PhysicalInstanceId{1},
            InsertPosition::Before
        );

        const WorkingSetState increased =
            setWorkingCardQuantity(state, "a", 3);

        QCOMPARE(
            increased.physicalOrder.instances[0].id,
            PhysicalInstanceId{3}
        );
        QCOMPARE(
            increased.physicalOrder.instances[1].id,
            PhysicalInstanceId{1}
        );
        QCOMPARE(
            increased.physicalOrder.instances[2].id,
            PhysicalInstanceId{2}
        );
        QCOMPARE(
            increased.physicalOrder.instances[3].id,
            PhysicalInstanceId{4}
        );

        const WorkingSetState decreased =
            setWorkingCardQuantity(increased, "a", 1);

        QCOMPARE(decreased.cards[0].quantity, std::uint32_t{1});
        QCOMPARE(
            decreased.physicalOrder.instances.size(),
            std::size_t{2}
        );
        QCOMPARE(
            decreased.physicalOrder.instances[0].id,
            PhysicalInstanceId{3}
        );
        QCOMPARE(
            decreased.physicalOrder.instances[1].id,
            PhysicalInstanceId{1}
        );
    }

    void duplicatesOnePhysicalCopyAsIndependentEntry()
    {
        WorkingSetState state = createWorkingSetState({
            card("a", 2, 0),
            card("b", 1, 1),
        });

        const WorkingSetState duplicated =
            duplicatePhysicalInstanceAsEntry(
                state,
                PhysicalInstanceId{1},
                "a-copy"
            );

        QCOMPARE(duplicated.cards.size(), std::size_t{3});
        QCOMPARE(
            QString::fromStdString(duplicated.cards[1].id),
            QStringLiteral("a-copy")
        );
        QCOMPARE(duplicated.cards[1].quantity, std::uint32_t{1});
        QCOMPARE(
            duplicated.physicalOrder.instances[1].workingCardId,
            std::string("a-copy")
        );
        QVERIFY(duplicated.selectedCardId.has_value());
        QCOMPARE(
            QString::fromStdString(*duplicated.selectedCardId),
            QStringLiteral("a-copy")
        );
    }

    void movesLogicalCardAndItsPhysicalGroup()
    {
        const WorkingSetState state = createWorkingSetState({
            card("a", 2, 0),
            card("b", 1, 1),
            card("c", 1, 2),
        });

        const WorkingSetState moved =
            moveWorkingCard(state, "c", 0);

        QCOMPARE(
            QString::fromStdString(moved.cards[0].id),
            QStringLiteral("c")
        );
        QCOMPARE(
            moved.physicalOrder.instances[0].workingCardId,
            std::string("c")
        );
        QCOMPARE(
            moved.physicalOrder.instances[1].workingCardId,
            std::string("a")
        );
    }

    void deletingSelectedCardSelectsNearestNeighbor()
    {
        WorkingSetState state = createWorkingSetState({
            card("a", 1, 0),
            card("b", 1, 1),
            card("c", 1, 2),
        });

        state = selectWorkingCard(state, "b");
        const WorkingSetState deleted =
            deleteWorkingCard(state, "b");

        QVERIFY(deleted.selectedCardId.has_value());
        QCOMPARE(
            QString::fromStdString(*deleted.selectedCardId),
            QStringLiteral("c")
        );
        QCOMPARE(deleted.cards.size(), std::size_t{2});
        QCOMPARE(
            deleted.physicalOrder.instances.size(),
            std::size_t{2}
        );
    }

    void historyRecordsEditorialStepsAndSupportsRedo()
    {
        const WorkingSetState initial = createWorkingSetState({
            card("a", 1, 0),
            card("b", 1, 1),
        });

        WorkingSetHistory history =
            createWorkingSetHistory(initial);

        const WorkingSetState selected =
            selectWorkingCard(initial, "b");

        history = updateWorkingSetHistoryPresent(
            history,
            selected
        );
        QVERIFY(history.past.empty());

        const WorkingSetState edited =
            setWorkingCardQuantity(selected, "b", 3);

        history = commitWorkingSetHistory(
            history,
            edited
        );

        QCOMPARE(history.past.size(), std::size_t{1});
        QCOMPARE(history.present.cards[1].quantity, std::uint32_t{3});

        history = undoWorkingSetHistory(history);
        QCOMPARE(history.present.cards[1].quantity, std::uint32_t{1});
        QCOMPARE(history.future.size(), std::size_t{1});

        history = redoWorkingSetHistory(history);
        QCOMPARE(history.present.cards[1].quantity, std::uint32_t{3});
        QVERIFY(history.future.empty());
    }

    void rejectsLimitAndDuplicateIds()
    {
        const WorkingSetState state = createWorkingSetState({
            card("a", 500, 0),
        });

        try {
            static_cast<void>(
                setWorkingCardQuantity(state, "a", 501)
            );
            QFAIL("Expected export limit failure.");
        } catch (const WorkingSetError& error) {
            QCOMPARE(
                error.code(),
                WorkingSetErrorCode::ExportLimitExceeded
            );
        }

        try {
            static_cast<void>(
                duplicatePhysicalInstanceAsEntry(
                    state,
                    PhysicalInstanceId{1},
                    "a"
                )
            );
            QFAIL("Expected duplicate ID failure.");
        } catch (const WorkingSetError& error) {
            QCOMPARE(
                error.code(),
                WorkingSetErrorCode::DuplicateId
            );
        }
    }
};

QTEST_APPLESS_MAIN(WorkingSetTest)

#include "WorkingSetTest.moc"
