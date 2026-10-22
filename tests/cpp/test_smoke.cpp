#include <QtTest/QtTest>

#include "AppMode.h"

class SonoSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaultModeIsWebUnderlay() {
        QCOMPARE(appModeName(AppMode::WebUnderlay), QStringLiteral("webunderlay"));
    }
};

QTEST_MAIN(SonoSmokeTest)
#include "test_smoke.moc"
