#include "theme.hpp"
#include <QApplication>
#include <QPainter>
#include <QProxyStyle>

void installLunaTheme(QApplication &app)
{
    app.setStyle(new QProxyStyle("Fusion"));
    QFont font("Tahoma");
    font.setFamilies({"Tahoma", "Liberation Sans", "Sans Serif"});
    font.setPixelSize(13);
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#eeeade"));
    palette.setColor(QPalette::Button, QColor("#eeeade"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Highlight, QColor("#316ac5"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QMainWindow { background: #ece9df; }
        QMenuBar { background: #ece9df; border-bottom: 1px solid #bcb8ac; padding-left: 4px; }
        QMenuBar::item { padding: 3px 7px; }
        QMenuBar::item:selected, QMenu::item:selected { background: #316ac5; color: white; }
        QMenu { background: #fffefa; border: 1px solid #8c8a7f; padding: 2px; }
        QToolBar { background: #ece9df; border-top: 1px solid white; border-bottom: 1px solid #bcb8ac; spacing: 3px; padding: 2px 5px; }
        QToolBar::separator { width: 2px; margin: 3px; background: #bab7ad; border-right: 1px solid white; }
        QToolButton { padding: 4px 6px; border: 1px solid transparent; }
        QToolButton:hover { border: 1px solid #a39e8d; background: #f8f5eb; }
        QToolButton:pressed, QToolButton:checked { border: 1px solid #7f9db9; background: #d9e8fa; }
        QLineEdit, QTreeWidget { background: white; border: 1px solid #7f9db9; padding: 2px; }
        QStatusBar QLabel { border: 1px inset #c9c5b8; padding: 1px 6px; }
        QTabBar::tab { padding: 4px 12px; background: #e6eaf4; border: 1px solid #a4afc4; }
        QTabBar::tab:selected { background: #fffefa; border-top: 2px solid #eeaf48; }
    )");
}

QIcon toolbarIcon(const QString &name)
{
    if (name == "web")
        return QIcon(":/explorer.svg");
    QIcon icon;
    for (const int size : {16, 22, 32, 44, 64}) {
        QPixmap pixel(size, size);
        pixel.fill(Qt::transparent);
        QPainter p(&pixel);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24.0, size / 24.0);
        auto shape = [&](const QColor &light, const QColor &dark, const QColor &edge) {
            QLinearGradient gradient(4, 3, 18, 22);
            gradient.setColorAt(0, light);
            gradient.setColorAt(1, dark);
            p.setPen(edge);
            p.setBrush(gradient);
        };
        if (name == "back" || name == "forward") {
            shape("#aee181", "#278642", "#1e6935");
            p.drawEllipse(QRectF(2, 2, 20, 20));
            p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const int direction = name == "back" ? 1 : -1;
            p.drawLine(12 - 5 * direction, 12, 12 + 5 * direction, 12);
            p.drawLine(12 - 5 * direction, 12, 12 - direction, 8);
            p.drawLine(12 - 5 * direction, 12, 12 - direction, 16);
        } else if (name == "favorite" || name == "folder") {
            shape("#fff1a0", "#e7a32f", "#935b19");
            if (name == "folder") {
                p.drawPolygon(QPolygonF{{2, 6}, {10, 6}, {13, 9}, {22, 9}, {20, 20}, {2, 20}});
            } else {
                p.drawPolygon(QPolygonF{{12, 2}, {15, 8}, {22, 9}, {17, 14}, {18, 21}, {12, 18},
                                        {6, 21}, {7, 14}, {2, 9}, {9, 8}});
            }
        } else if (name == "home") {
            shape("#fff4b8", "#d9aa5d", "#856637");
            p.drawRect(QRectF(5, 10, 14, 11));
            p.drawRect(QRectF(10, 14, 4, 7));
            shape("#ffae6a", "#b4321e", "#8a2c24");
            p.drawPolygon(QPolygonF{{1, 11}, {12, 2}, {23, 11}, {20, 13}, {12, 7}, {4, 13}});
        } else if (name == "search") {
            p.setPen(QPen(QColor("#744b2b"), 4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(14, 14, 21, 21);
            shape("#ffffff", "#99c6e3", "#38698a");
            p.drawEllipse(QRectF(2, 2, 15, 15));
        } else if (name == "history") {
            shape("#fffbe7", "#c0daf4", "#38698a");
            p.drawEllipse(QRectF(2, 2, 20, 20));
            p.setPen(QPen(QColor("#345679"), 2));
            p.drawLine(12, 6, 12, 12);
            p.drawLine(12, 12, 16, 15);
        } else {
            shape("#ffffff", "#c4d4e3", "#60768d");
            p.drawRoundedRect(QRectF(4, 2, 16, 20), 1, 1);
            if (name == "print") {
                p.drawRoundedRect(QRectF(1, 8, 22, 10), 2, 2);
                p.drawRect(QRectF(6, 14, 12, 8));
            } else if (name == "stop") {
                p.setPen(QPen(QColor("#c1372b"), 3, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(8, 8, 16, 16);
                p.drawLine(16, 8, 8, 16);
            } else {
                p.setPen(QPen(QColor("#358447"), 2));
                p.drawArc(QRectF(7, 7, 10, 10), 25 * 16, 300 * 16);
                p.drawLine(16, 7, 16, 12);
                p.drawLine(16, 12, 12, 12);
            }
        }
        p.end();
        icon.addPixmap(pixel);
    }
    return icon;
}
