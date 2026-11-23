#include "window.hpp"
#include <QtWidgets>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QWebEngineCookieStore>
#include <QWebEngineDownloadRequest>
#include <QWebEngineSettings>

void Window::printPage(bool preview)
{
    QPointer<QWebEngineView> view(current());
    if (!view)
        return;
    if (preview) {
        QPointer<Window> window(this);
        view->page()->printToPdf([window](const QByteArray &pdf) {
            if (!window)
                return;
            if (pdf.isEmpty()) {
                QMessageBox::warning(window, "Print Preview", "Could not generate preview.");
                return;
            }
            auto *dialog = new QDialog(window, Qt::Window);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle("Print Preview");
            auto *layout = new QVBoxLayout(dialog);
            auto *previewView = window->engine.createView(dialog);
            layout->addWidget(previewView);
            previewView->setContent(pdf, "application/pdf");
            dialog->resize(820, 680);
            dialog->show();
        });
        return;
    }
    auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    QPrintDialog dialog(printer.get(), this);
    if (dialog.exec() != QDialog::Accepted || !view)
        return;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(view, &QWebEngineView::printFinished, view, [window = QPointer<Window>(this), printer, connection](bool success) {
        disconnect(*connection);
        if (!success && window)
            QMessageBox::warning(window, "Print", "Printing failed.");
    });
    view->print(printer.get());
}

void Window::options()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Internet Options");
    auto *layout = new QVBoxLayout(&dialog);
    auto *pages = new QTabWidget(&dialog);
    layout->addWidget(pages);
    auto section = [pages](const QString &label) {
        auto *widget = new QWidget(pages);
        pages->addTab(widget, label);
        return new QFormLayout(widget);
    };
    QSettings settings;
    auto *general = section("General");
    auto *homepage = new QLineEdit(settings.value("homepage", "https://duckduckgo.com/").toString());
    auto *provider = new QComboBox;
    provider->addItems({"DuckDuckGo", "Google"});
    provider->setCurrentText(settings.value("search", "DuckDuckGo").toString());
    general->addRow("Home page", homepage);
    general->addRow("Search engine", provider);

    auto *security = section("Security");
    security->addRow(new QLabel("Invalid certificates are rejected."));
    auto *privacy = section("Privacy");
    auto *cookies = new QCheckBox("Save persistent cookies between launches");
    cookies->setChecked(settings.value("persistentCookies", true).toBool());
    privacy->addRow(cookies);
    auto *clearCookies = new QPushButton("Delete Profile Cookies");
    privacy->addRow(clearCookies);
    connect(clearCookies, &QPushButton::clicked, this, [this, clearCookies] {
        engine.profile()->cookieStore()->deleteAllCookies();
        clearCookies->setEnabled(false);
    });
    auto *content = section("Content");
    auto *javascript = new QCheckBox("Enable JavaScript");
    javascript->setChecked(settings.value("javascript", true).toBool());
    content->addRow(javascript);
    auto *connections = section("Connections");
    auto *downloads = new QLineEdit(settings.value("downloads",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString());
    connections->addRow("Download directory", downloads);
    auto *chooseDirectory = new QPushButton("Browse…");
    connections->addRow(chooseDirectory);
    connect(chooseDirectory, &QPushButton::clicked, this, [this, downloads] {
        const auto path = QFileDialog::getExistingDirectory(this, "Download directory", downloads->text());
        if (!path.isEmpty())
            downloads->setText(path);
    });
    auto *programs = section("Programs");
    auto *defaults = new QPushButton("Set as Default Browser");
    programs->addRow(defaults);
    connect(defaults, &QPushButton::clicked, this, [this] {
        const QString id = QCoreApplication::instance()->property("desktopId").toString();
        const QString launcher = id.isEmpty() ? "io.github.Mschmitt478.ChromiumExplorer.desktop" : id;
        if (QProcess::execute("xdg-settings", {"set", "default-web-browser", launcher}) != 0)
            QMessageBox::information(this, "Default Browser", "Select Internet Explorer XP in KDE System Settings → Default Applications.");
    });
    auto *advanced = section("Advanced");
    auto *tabMode = new QCheckBox("Enable tabs in new windows");
    tabMode->setChecked(settings.value("tabs", false).toBool());
    advanced->addRow(tabMode);
    auto *restore = new QCheckBox("Restore tabs at startup");
    restore->setChecked(settings.value("restoreTabs", false).toBool());
    advanced->addRow(restore);
    auto *zoomLevel = new QSpinBox;
    zoomLevel->setRange(25, 500);
    zoomLevel->setSuffix("%");
    zoomLevel->setValue(qRound(settings.value("zoom", 1.0).toDouble() * 100));
    advanced->addRow("Default zoom", zoomLevel);
    if (privateMode)
        pages->setEnabled(false);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!privateMode && !supportedUrl(QUrl(homepage->text().trimmed(), QUrl::StrictMode))) {
            QMessageBox::warning(&dialog, "Internet Options", "Enter a complete HTTP, HTTPS, or local file URL.");
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(660, 330);
    if (dialog.exec() != QDialog::Accepted || privateMode)
        return;
    settings.setValue("homepage", homepage->text().trimmed());
    settings.setValue("search", provider->currentText());
    settings.setValue("downloads", downloads->text().trimmed());
    settings.setValue("tabs", tabMode->isChecked());
    settings.setValue("restoreTabs", restore->isChecked());
    settings.setValue("zoom", zoomLevel->value() / 100.0);
    settings.setValue("persistentCookies", cookies->isChecked());
    settings.setValue("javascript", javascript->isChecked());
    engine.profile()->setPersistentCookiesPolicy(cookies->isChecked() ?
        QWebEngineProfile::AllowPersistentCookies : QWebEngineProfile::NoPersistentCookies);
    engine.profile()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, javascript->isChecked());
    if (auto *view = current())
        view->setZoomFactor(zoomLevel->value() / 100.0);
}

void Window::showDownloads()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Downloads");
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTreeWidget(&dialog);
    table->setHeaderLabels({"File", "Status"});
    table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table);
    auto *cancel = new QPushButton("Cancel Selected Download", &dialog);
    cancel->setEnabled(false);
    layout->addWidget(cancel);
    QHash<QTreeWidgetItem *, QPointer<QWebEngineDownloadRequest>> requests;
    for (const auto &request : engine.downloads) {
        if (!request)
            continue;
        auto *row = new QTreeWidgetItem(table, {request->downloadFileName(), {}});
        requests.insert(row, request);
        auto update = [row, request] {
            if (!request)
                return;
            using Download = QWebEngineDownloadRequest;
            switch (request->state()) {
            case Download::DownloadCompleted: row->setText(1, "Completed"); break;
            case Download::DownloadCancelled: row->setText(1, "Cancelled"); break;
            case Download::DownloadInterrupted: row->setText(1, request->interruptReasonString()); break;
            default: row->setText(1, QString("%1 bytes received").arg(request->receivedBytes())); break;
            }
        };
        connect(request, &QWebEngineDownloadRequest::receivedBytesChanged, &dialog, update);
        connect(request, &QWebEngineDownloadRequest::stateChanged, &dialog, update);
        update();
    }
    connect(table, &QTreeWidget::itemSelectionChanged, &dialog, [&] {
        const auto request = requests.value(table->currentItem());
        cancel->setEnabled(request && !request->isFinished());
    });
    connect(cancel, &QPushButton::clicked, &dialog, [&] {
        if (auto request = requests.value(table->currentItem()))
            request->cancel();
        cancel->setEnabled(false);
    });
    dialog.resize(600, 300);
    dialog.exec();
}
