#include "core/popupplacement.h"

#include <QTest>

namespace {

constexpr int kOffset = 14;
const QSize kPopupSize(460, 260);
const QRect kScreen(0, 0, 1920, 1040);

} // namespace

class TestPopupPlacement : public QObject
{
    Q_OBJECT

private slots:
    void normalPositionIsBottomRight()
    {
        const QPoint position =
            PopupPlacement::positionNear(QPoint(200, 200), kPopupSize, kScreen, kOffset);
        QCOMPARE(position, QPoint(200 + kOffset, 200 + kOffset));
    }

    void flipsLeftNearRightEdge()
    {
        const QPoint anchor(1900, 300);
        const QPoint position =
            PopupPlacement::positionNear(anchor, kPopupSize, kScreen, kOffset);
        QCOMPARE(position.x(), anchor.x() - kPopupSize.width() - kOffset);
        QVERIFY(position.x() >= kScreen.left());
        QVERIFY(position.x() + kPopupSize.width() <= kScreen.right() + 1);
    }

    void flipsUpNearBottomEdge()
    {
        const QPoint anchor(300, 1020);
        const QPoint position =
            PopupPlacement::positionNear(anchor, kPopupSize, kScreen, kOffset);
        QCOMPARE(position.y(), anchor.y() - kPopupSize.height() - kOffset);
        QVERIFY(position.y() >= kScreen.top());
        QVERIFY(position.y() + kPopupSize.height() <= kScreen.bottom() + 1);
    }

    void clampsWhenBothSidesOverflow()
    {
        const QPoint anchor(1910, 1030);
        const QPoint position =
            PopupPlacement::positionNear(anchor, kPopupSize, kScreen, kOffset);
        QVERIFY(position.x() >= kScreen.left());
        QVERIFY(position.y() >= kScreen.top());
        QVERIFY(position.x() + kPopupSize.width() <= kScreen.right() + 1);
        QVERIFY(position.y() + kPopupSize.height() <= kScreen.bottom() + 1);
    }

    void popupLargerThanScreenStartsAtTopLeft()
    {
        const QPoint position = PopupPlacement::positionNear(QPoint(100, 100), QSize(3000, 2000),
                                                             kScreen, kOffset);
        QCOMPARE(position, kScreen.topLeft());
    }

    void anchorOutsideScreenIsClamped()
    {
        const QPoint position =
            PopupPlacement::positionNear(QPoint(-500, -400), kPopupSize, kScreen, kOffset);
        QCOMPARE(position, kScreen.topLeft());

        const QPoint farRight = PopupPlacement::positionNear(QPoint(5000, 4000), kPopupSize,
                                                             kScreen, kOffset);
        QVERIFY(farRight.x() + kPopupSize.width() <= kScreen.right() + 1);
        QVERIFY(farRight.y() + kPopupSize.height() <= kScreen.bottom() + 1);
    }

    void negativeSecondMonitorGeometry()
    {
        const QRect secondScreen(-1920, -200, 1920, 1080);
        const QPoint anchor(-1000, 100);
        const QPoint position =
            PopupPlacement::positionNear(anchor, kPopupSize, secondScreen, kOffset);
        QVERIFY(secondScreen.contains(QRect(position, kPopupSize)));
        QCOMPARE(position, QPoint(anchor.x() + kOffset, anchor.y() + kOffset));

        // Near the right edge of the left monitor the popup flips to the left.
        const QPoint nearRightEdge(-10, 200);
        const QPoint flipped =
            PopupPlacement::positionNear(nearRightEdge, kPopupSize, secondScreen, kOffset);
        QVERIFY(secondScreen.contains(QRect(flipped, kPopupSize)));
        QVERIFY(flipped.x() + kPopupSize.width() <= secondScreen.right() + 1);
    }

    void worksWithUnusualOffsets()
    {
        const QPoint position =
            PopupPlacement::positionNear(QPoint(0, 0), kPopupSize, kScreen, 0);
        QCOMPARE(position, QPoint(0, 0));
    }
};

QTEST_GUILESS_MAIN(TestPopupPlacement)
#include "tst_popupplacement.moc"
