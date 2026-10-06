#include <QtTest>

#include "import/ImportDetection.h"

#include <algorithm>
#include <initializer_list>

using namespace tcgprint::imports;

namespace {

QByteArray bytes(std::initializer_list<unsigned int> values)
{
    QByteArray result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const unsigned int value : values) {
        result.append(static_cast<char>(value));
    }
    return result;
}

ImportDetection detectText(
    const QString& text,
    std::optional<QString> fileName = std::nullopt,
    std::optional<QString> mediaType = std::nullopt,
    bool urlLike = false
)
{
    return detectImport(ImportDetectionInput{
        .bytes = std::nullopt,
        .text = text,
        .fileName = std::move(fileName),
        .mediaType = std::move(mediaType),
        .sourceUrl = std::nullopt,
        .adapterId = std::nullopt,
        .urlLike = urlLike,
    });
}

QString selectedKind(const ImportDetection& detection)
{
    return detection.selected.has_value()
        ? importKindName(detection.selected->kind)
        : QString();
}

bool hasCandidate(
    const ImportDetection& detection,
    ImportKind kind
)
{
    return std::any_of(
        detection.candidates.begin(),
        detection.candidates.end(),
        [kind](const ImportCandidate& candidate) {
            return candidate.kind == kind;
        }
    );
}

} // namespace

class ImportDetectionTest final : public QObject
{
    Q_OBJECT

private slots:
    void rasterAndArchiveSignaturesBeatExtension()
    {
        const QByteArray png =
            bytes({137, 80, 78, 71, 13, 10, 26, 10, 0});
        const QByteArray jpeg =
            bytes({0xff, 0xd8, 0xff, 0x00});
        const QByteArray zip =
            bytes({0x50, 0x4b, 0x03, 0x04, 0x00});

        QCOMPARE(
            selectedKind(detectImport(ImportDetectionInput{
                .bytes = png,
                .fileName = QStringLiteral("image.csv"),
            })),
            QStringLiteral("image")
        );
        const ImportDetection jpegDetection =
            detectImport(ImportDetectionInput{
                .bytes = jpeg,
                .fileName = QStringLiteral("scan.png"),
            });
        QCOMPARE(selectedKind(jpegDetection), QStringLiteral("image"));
        QVERIFY(!jpegDetection.reasons.isEmpty());
        QCOMPARE(
            selectedKind(detectImport(ImportDetectionInput{
                .bytes = zip,
                .fileName = QStringLiteral("cards.txt"),
            })),
            QStringLiteral("zip")
        );
    }

    void extensionAloneDoesNotSelectImporter()
    {
        const ImportDetection detection =
            detectImport(ImportDetectionInput{
                .bytes = bytes({0xff, 0x00, 0xff, 0x12}),
                .fileName = QStringLiteral("card.png"),
            });
        QCOMPARE(
            static_cast<int>(detection.status),
            static_cast<int>(ImportDetectionStatus::Unknown)
        );
        QVERIFY(!detection.selected.has_value());
        QCOMPARE(detection.candidates.size(), std::size_t{1});
        QCOMPARE(
            static_cast<int>(detection.candidates.front().kind),
            static_cast<int>(ImportKind::Unknown)
        );
        QCOMPARE(detection.candidates.front().confidence, 1.0);
    }

    void recognizesSvgWebpAndAllTiffSignatures()
    {
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"></svg>"
                ),
                QStringLiteral("art.png")
            )),
            QStringLiteral("svg")
        );

        QByteArray webp("RIFF", 4);
        webp.append(bytes({4, 0, 0, 0}));
        webp.append("WEBP", 4);
        webp.append("VP8 ", 4);
        QCOMPARE(
            selectedKind(detectImport(ImportDetectionInput{
                .bytes = webp,
                .fileName = QStringLiteral("synthetic.webp"),
            })),
            QStringLiteral("image")
        );

        const QList<QByteArray> signatures{
            bytes({0x49, 0x49, 0x2a, 0x00, 0, 0}),
            bytes({0x4d, 0x4d, 0x00, 0x2a, 0, 0}),
            bytes({0x49, 0x49, 0x2b, 0x00, 0, 0}),
            bytes({0x4d, 0x4d, 0x00, 0x2b, 0, 0}),
        };
        for (const QByteArray& tiff : signatures) {
            QCOMPARE(
                selectedKind(detectImport(ImportDetectionInput{
                    .bytes = tiff,
                    .fileName = QStringLiteral("x.bin"),
                })),
                QStringLiteral("image")
            );
        }
    }

    void distinguishesDeckTextStructures()
    {
        QCOMPARE(
            selectedKind(detectText(QStringLiteral("Sol Ring"))),
            QStringLiteral("simple-decklist")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("1 Sol Ring (CMM) 396\n10 Island")
            )),
            QStringLiteral("simple-decklist")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "Deck\n1 Sol Ring (CMM) 396\nSideboard\n1 Island (M21) 310"
                )
            )),
            QStringLiteral("arena-like")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("1 Sol Ring\nSB: 1 Island")
            )),
            QStringLiteral("mtgo-like")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "LAYOUT MAIN\n1 Sol Ring\nLAYOUT SIDEBOARD\n1 Island"
                )
            )),
            QStringLiteral("xmage-like")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "// Deck file for Magic Workstation\n4 [CMM] Sol Ring"
                ),
                QStringLiteral("list.mwDeck")
            )),
            QStringLiteral("mwdeck-like")
        );
    }

    void recognizesTabularAndStructuredText()
    {
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "name,quantity\n\"Sol Ring, Revised\",1\n"
                ),
                QStringLiteral("cards.csv")
            )),
            QStringLiteral("csv")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("name\tquantity\nSol Ring\t1\n"),
                QStringLiteral("cards.tsv")
            )),
            QStringLiteral("tsv")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "{\"cards\":[{\"name\":\"Sol Ring\"}]}"
                ),
                QStringLiteral("cards.json")
            )),
            QStringLiteral("json")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "<?xml version=\"1.0\"?><deck><card><name>Sol Ring</name></card></deck>"
                )
            )),
            QStringLiteral("generic-xml")
        );
    }

    void detectsMpcAutofillXmlWithoutProviderAccess()
    {
        const QString xml = QStringLiteral(
            "<order><details></details><fronts><card><id>art-7</id><slots>1</slots><name>Custom</name><query>Custom</query></card></fronts></order>"
        );
        QCOMPARE(
            selectedKind(detectText(
                xml,
                QStringLiteral("cards.xml")
            )),
            QStringLiteral("mpc-autofill-xml")
        );
    }

    void marksEquallyPlausibleDelimitedFormatsAmbiguous()
    {
        const ImportDetection detection =
            detectText(
                QStringLiteral(
                    "name, set\tquantity\nA, ABC\t1\nB, XYZ\t2\n"
                ),
                QStringLiteral("mixed.csv")
            );
        QCOMPARE(
            static_cast<int>(detection.status),
            static_cast<int>(ImportDetectionStatus::Ambiguous)
        );
        QVERIFY(!detection.selected.has_value());
        QVERIFY(hasCandidate(detection, ImportKind::Csv));
        QVERIFY(hasCandidate(detection, ImportKind::Tsv));
    }

    void detectsUrlIntentAndUnknownBinary()
    {
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("https://example.invalid/deck.txt")
            )),
            QStringLiteral("url")
        );
        QCOMPARE(
            selectedKind(detectText(QStringLiteral("https://"))),
            QStringLiteral("url")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("https:/example.invalid/deck.txt")
            )),
            QStringLiteral("url")
        );
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("ftp://example.invalid/deck.txt")
            )),
            QStringLiteral("url")
        );

        const ImportDetection detection =
            detectImport(ImportDetectionInput{
                .bytes = bytes({0xff, 0x00, 0xff, 0x12}),
                .fileName = QStringLiteral("unknown.data"),
            });
        QCOMPARE(
            static_cast<int>(detection.status),
            static_cast<int>(ImportDetectionStatus::Unknown)
        );
    }

    void policyAndConfidenceStayWithinContract()
    {
        QCOMPARE(DetectionPolicyDefault.autoSelectMinimum, 0.78);
        QCOMPARE(DetectionPolicyDefault.ambiguousMinimum, 0.50);
        QCOMPARE(DetectionPolicyDefault.ambiguityMargin, 0.12);
        QCOMPARE(DetectionPolicyDefault.extensionAdjustment, 0.04);

        const ImportDetection detection =
            detectText(QStringLiteral("1 Sol Ring\n"));
        for (const ImportCandidate& candidate : detection.candidates) {
            QVERIFY(candidate.confidence >= 0.0);
            QVERIFY(candidate.confidence <= 1.0);
            QVERIFY(!candidate.reasons.isEmpty());
        }
    }

    void contentTypeIsSecondaryEvidence()
    {
        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral("alpha,beta\n1,2\n"),
                std::nullopt,
                QStringLiteral("text/csv")
            )),
            QStringLiteral("csv")
        );

        QCOMPARE(
            selectedKind(detectText(
                QStringLiteral(
                    "Deck\n1 Sol Ring (CMM) 396\n"
                ),
                QStringLiteral("deck.txt"),
                QStringLiteral("text/csv")
            )),
            QStringLiteral("arena-like")
        );
    }

    void structuredContentWinsConflictingMime()
    {
        const ImportDetection json =
            detectText(
                QStringLiteral("{\"cards\":[]}"),
                QStringLiteral("cards.dat"),
                QStringLiteral("application/xml")
            );
        QCOMPARE(selectedKind(json), QStringLiteral("json"));
        QVERIFY(!hasCandidate(json, ImportKind::GenericXml));

        const ImportDetection xml =
            detectText(
                QStringLiteral("<deck><card /></deck>"),
                QStringLiteral("cards.dat"),
                QStringLiteral("application/json")
            );
        QCOMPARE(selectedKind(xml), QStringLiteral("generic-xml"));
        QVERIFY(!hasCandidate(xml, ImportKind::Json));
    }
};

QTEST_APPLESS_MAIN(ImportDetectionTest)

#include "ImportDetectionTest.moc"
