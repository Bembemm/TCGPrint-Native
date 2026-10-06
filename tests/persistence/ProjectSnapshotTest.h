#pragma once

#include <QtTest>

class ProjectSnapshotTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsV6AndPreservesDurableRawFields();
    void promotesV1ToCurrentAndDerivesPhysicalOrder();
    void derivesLegacySimpleCardBackMode();
    void derivesLegacyDfcBackModeFromProviderMetadata();
    void rejectsIncompleteCurrentWorkingCardShape();
    void rejectsCardFieldsThatDidNotExistInSchema();
    void rejectsArtworkForMissingFace();
    void rejectsUnsafeArtworkCandidateId();
    void rejectsIdentityConfidenceOutsideRange();
    void rejectsConfirmedResolutionWithoutIdentity();
    void rejectsManualBackWithoutSource();
    void rejectsInvalidBackLibraryDigest();
    void rejectsMpcReferenceForMissingFace();
    void serializesCanonicalV6WithoutLosingDurableState();
    void promotesLegacySnapshotToSerializedV6();
    void rejectsSettingsFieldsThatDidNotExistInSchema();
    void serializerRejectsBrokenPhysicalOrder();
    void rejectsFutureSchema();
    void rejectsPhysicalOrderThatDoesNotMatchQuantities();
    void rejectsDuplicateWorkingCardIds();
};
