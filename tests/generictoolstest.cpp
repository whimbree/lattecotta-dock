/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the Settings::Generic free helpers in
// app/settings/generic/generictools.cpp (linked through lattedock-core) -
// the delegate toolkit every settings dialog paints with. Most helpers take
// a QStyleOption + QPainter; the cases feed real options and assert the
// observable outputs (state-flag predicates, alignment mapping, color-group
// priority, list subtraction, geometry rects) and paint the painters into a
// QImage under offscreen QPA so the real drawing paths execute.
//
// Transplanted from latte-dock-qt6 (tests/generictoolstest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and raised with the RTL flip case for the icon-slot geometry
// (QGuiApplication::setLayoutDirection is honored by the helpers, and no
// fork case ever drove it). The default-thick-margin equivalence case pins
// the four-site sentinel fix landed right before this test (fork parallel
// d3be9b71).

// local
#include "settings/generic/generictools.h"

// Qt
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QStringList>
#include <QStyleOption>
#include <QStyleOptionViewItem>
#include <QTest>

using namespace Latte;

namespace {
QStyleOptionViewItem makeOption(QStyle::State state, const QRect &rect = QRect(0, 0, 200, 30))
{
    QStyleOptionViewItem opt;
    opt.state = state;
    opt.rect = rect;
    return opt;
}

bool paintsAnyPixel(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) != 0) {
                return true;
            }
        }
    }
    return false;
}
}

class GenericToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void classifyStateFlags_data();
    void classifyStateFlags();
    void readCenteredTextFromDisplayAlignment();
    void mapHorizontalAlignment_data();
    void mapHorizontalAlignment();

    void resolveColorGroupByPriority_data();
    void resolveColorGroupByPriority();

    void subtractListsPerOriginalEntry();

    void keepScreenMaxLengthOdd_data();
    void keepScreenMaxLengthOdd();
    void clampScreenMaxLengthByMaxIcon();

    void reserveChangesIndicatorSlot();
    void reserveIconSlotLeftAligned();
    void resolveDefaultThickMarginLikeExplicit();
    void flipIconSlotUnderRightToLeft();
    void returnFullRectForCenteredLayoutIcon();
    void shrinkRemainedRectForScreenDrawing();

    void paintChangesIndicator();
    void paintScreenAndReturnAvailableRect();
    void paintFormattedTextSelected();
};

void GenericToolsTest::classifyStateFlags_data()
{
    QTest::addColumn<int>("state");
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("active");
    QTest::addColumn<bool>("selected");
    QTest::addColumn<bool>("hovered");
    QTest::addColumn<bool>("focused");

    QTest::newRow("none") << int(QStyle::State_None) << false << false << false << false << false;
    QTest::newRow("enabled") << int(QStyle::State_Enabled) << true << false << false << false << false;
    QTest::newRow("active") << int(QStyle::State_Active) << false << true << false << false << false;
    QTest::newRow("selected") << int(QStyle::State_Selected) << false << false << true << false << false;
    QTest::newRow("mouseover") << int(QStyle::State_MouseOver) << false << false << false << true << false;
    QTest::newRow("focus") << int(QStyle::State_HasFocus) << false << false << false << false << true;
    QTest::newRow("all")
        << int(QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Selected
               | QStyle::State_MouseOver | QStyle::State_HasFocus)
        << true << true << true << true << true;
}

void GenericToolsTest::classifyStateFlags()
{
    QFETCH(int, state);
    QFETCH(bool, enabled);
    QFETCH(bool, active);
    QFETCH(bool, selected);
    QFETCH(bool, hovered);
    QFETCH(bool, focused);

    const QStyleOptionViewItem opt = makeOption(QStyle::State(state));

    QCOMPARE(isEnabled(opt), enabled);
    QCOMPARE(isActive(opt), active);
    QCOMPARE(isSelected(opt), selected);
    QCOMPARE(isHovered(opt), hovered);
    QCOMPARE(isFocused(opt), focused);
}

void GenericToolsTest::readCenteredTextFromDisplayAlignment()
{
    QStyleOptionViewItem centered = makeOption(QStyle::State_Enabled);
    centered.displayAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
    QVERIFY(isTextCentered(centered));

    QStyleOptionViewItem left = makeOption(QStyle::State_Enabled);
    left.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    QVERIFY(!isTextCentered(left));
}

void GenericToolsTest::mapHorizontalAlignment_data()
{
    QTest::addColumn<Qt::Alignment>("in");
    QTest::addColumn<Qt::AlignmentFlag>("out");

    QTest::newRow("hcenter") << Qt::Alignment(Qt::AlignHCenter | Qt::AlignVCenter) << Qt::AlignHCenter;
    QTest::newRow("right") << Qt::Alignment(Qt::AlignRight | Qt::AlignBottom) << Qt::AlignRight;
    QTest::newRow("left") << Qt::Alignment(Qt::AlignLeft | Qt::AlignTop) << Qt::AlignLeft;
    // no horizontal bit at all falls through to Left
    QTest::newRow("vertical only") << Qt::Alignment(Qt::AlignVCenter) << Qt::AlignLeft;
    // HCenter beats Right when both are set
    QTest::newRow("center beats right") << Qt::Alignment(Qt::AlignHCenter | Qt::AlignRight) << Qt::AlignHCenter;
}

void GenericToolsTest::mapHorizontalAlignment()
{
    QFETCH(Qt::Alignment, in);
    QFETCH(Qt::AlignmentFlag, out);
    QCOMPARE(horizontalAlignment(in), out);
}

void GenericToolsTest::resolveColorGroupByPriority_data()
{
    QTest::addColumn<int>("state");
    QTest::addColumn<int>("group");

    // disabled wins over everything
    QTest::newRow("disabled wins")
        << int(QStyle::State_Active | QStyle::State_Selected | QStyle::State_HasFocus)
        << int(QPalette::Disabled);
    // active is checked before the inactive+selected branch
    QTest::newRow("active before selected")
        << int(QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Selected)
        << int(QPalette::Active);
    // focus alone still maps to Active
    QTest::newRow("focused maps active")
        << int(QStyle::State_Enabled | QStyle::State_HasFocus)
        << int(QPalette::Active);
    // selected but not active/focused
    QTest::newRow("inactive selected")
        << int(QStyle::State_Enabled | QStyle::State_Selected)
        << int(QPalette::Inactive);
    QTest::newRow("normal")
        << int(QStyle::State_Enabled)
        << int(QPalette::Normal);
}

void GenericToolsTest::resolveColorGroupByPriority()
{
    QFETCH(int, state);
    QFETCH(int, group);
    QCOMPARE(int(colorGroup(makeOption(QStyle::State(state)))), group);
}

void GenericToolsTest::subtractListsPerOriginalEntry()
{
    // removed entries are reported
    QCOMPARE(subtracted({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                        {QStringLiteral("b")}),
             (QStringList{QStringLiteral("a"), QStringLiteral("c")}));

    // a superset current leaves nothing subtracted
    QVERIFY(subtracted({QStringLiteral("x"), QStringLiteral("y")},
                       {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}).isEmpty());

    // each original entry is tested independently, so duplicates survive
    QCOMPARE(subtracted({QStringLiteral("dup"), QStringLiteral("dup")}, {}),
             (QStringList{QStringLiteral("dup"), QStringLiteral("dup")}));
}

void GenericToolsTest::keepScreenMaxLengthOdd_data()
{
    QTest::addColumn<int>("height");
    QTest::newRow("h30") << 30;
    QTest::newRow("h31") << 31;
    QTest::newRow("h40") << 40;
    QTest::newRow("h100") << 100;
}

void GenericToolsTest::keepScreenMaxLengthOdd()
{
    QFETCH(int, height);
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, height));
    const int len = screenMaxLength(opt);
    // the helper guarantees an odd length (even results are decremented) and
    // scales ~1.7x the icon height within the odd-rounding slack
    QVERIFY(len % 2 == 1);
    QVERIFY(len <= int(height * 1.7));
    QVERIFY(len >= int(height * 1.7) - 1);
}

void GenericToolsTest::clampScreenMaxLengthByMaxIcon()
{
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, 100));
    const int unclamped = screenMaxLength(opt, -1);
    const int clamped = screenMaxLength(opt, 20);
    QVERIFY(clamped < unclamped);
    QVERIFY(clamped % 2 == 1);
}

void GenericToolsTest::reserveChangesIndicatorSlot()
{
    // the indicator reserves a fixed slot (length 6 + 2*5 margins = 16);
    // LTR keeps x and shrinks the width by the slot
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(10, 5, 200, 30));
    const QRect r = remainedFromChangesIndicator(opt);
    QCOMPARE(r.x(), 10);
    QCOMPARE(r.y(), 5);
    QCOMPARE(r.height(), 30);
    QCOMPARE(r.width(), 200 - 16);
}

void GenericToolsTest::reserveIconSlotLeftAligned()
{
    // LTR left-aligned: the remaining rect starts after the icon slot
    // (iconsize + 2*lenmargin) and is narrower by the same amount
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, 30));
    const QRect r = remainedFromIcon(opt, Qt::AlignLeft);
    QVERIFY(r.x() > 0);
    QCOMPARE(r.width(), 200 - r.x());
    QCOMPARE(r.height(), 30);
}

void GenericToolsTest::resolveDefaultThickMarginLikeExplicit()
{
    // thickMargin defaults to -1, which must resolve to ICONMARGIN (1) for
    // BOTH the icon size and the offset - passing -1 must yield the same
    // rect as the resolved margin spelled out. The four-site sentinel fix
    // right before this test is what makes these equal; the inherited code
    // sized the icon from the raw -1 (height + 2 instead of height - 2).
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, 30));
    const QRect resolvedDefault = remainedFromIcon(opt, Qt::AlignLeft, -1, -1);
    const QRect explicitMargin = remainedFromIcon(opt, Qt::AlignLeft, -1, 1); // 1 == ICONMARGIN
    QCOMPARE(resolvedDefault, explicitMargin);
}

void GenericToolsTest::flipIconSlotUnderRightToLeft()
{
    // the helpers flip alignment with the application layout direction:
    // under RTL an AlignLeft request reserves the slot on the RIGHT, so the
    // remaining rect keeps the option's x instead of shifting past the slot
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, 30));
    const QRect ltr = remainedFromIcon(opt, Qt::AlignLeft);

    qApp->setLayoutDirection(Qt::RightToLeft);
    const QRect rtl = remainedFromIcon(opt, Qt::AlignLeft);
    qApp->setLayoutDirection(Qt::LeftToRight);

    QCOMPARE(rtl.x(), 0);
    QCOMPARE(rtl.width(), ltr.width()); // same slot size, opposite side
    QVERIFY(ltr.x() > 0);
}

void GenericToolsTest::returnFullRectForCenteredLayoutIcon()
{
    // centered alignment short-circuits to the full rect (no slot reserved)
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(3, 4, 200, 30));
    QCOMPARE(remainedFromLayoutIcon(opt, Qt::AlignHCenter), QRect(3, 4, 200, 30));
}

void GenericToolsTest::shrinkRemainedRectForScreenDrawing()
{
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 300, 40));
    const QRect r = remainedFromScreenDrawing(opt, false);
    QVERIFY(r.width() < 300);
    QVERIFY(r.x() > 0); // LTR: shifted right past the screen icon
    QCOMPARE(r.height(), 40);
}

void GenericToolsTest::paintChangesIndicator()
{
    QImage img(200, 30, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 200, 30));

    drawChangesIndicator(&p, opt);
    p.end();

    QVERIFY(paintsAnyPixel(img));
}

void GenericToolsTest::paintScreenAndReturnAvailableRect()
{
    QImage img(300, 40, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled, QRect(0, 0, 300, 40));

    const QRect avail = drawScreen(&p, opt, false, QRect(0, 0, 1920, 1080));
    p.end();

    // the returned available rect is non-empty and starts inside the option
    QVERIFY(avail.isValid());
    QVERIFY(avail.width() > 0);
    QVERIFY(avail.height() > 0);
    QVERIFY(opt.rect.contains(avail.topLeft()));
    QVERIFY(paintsAnyPixel(img));
}

void GenericToolsTest::paintFormattedTextSelected()
{
    QImage img(200, 30, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QStyleOptionViewItem opt = makeOption(QStyle::State_Enabled | QStyle::State_Selected, QRect(0, 0, 200, 30));
    opt.text = QStringLiteral("Hello");
    opt.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;

    // drives the HighlightedText branch (Selected) plus the QTextDocument render
    drawFormattedText(&p, opt, 1.0);
    p.end();

    QVERIFY(paintsAnyPixel(img));
}

QTEST_MAIN(GenericToolsTest)

#include "generictoolstest.moc"
