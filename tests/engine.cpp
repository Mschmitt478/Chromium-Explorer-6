#include "window.hpp"
#include <QAction>
#include <cstdio>
#include <cstdlib>
#include <QComboBox>
#include <QLineEdit>
#include <QIcon>
#include <QApplication>
#include <QEventLoop>
#include <QFileDialog>
#include <QWebEngineDownloadRequest>
#include <QWebEngineExtensionManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineHistory>
#include <QWebEnginePage>

namespace {
void require(bool condition, const char *description)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

void waitForLoad(QWebEngineView &view, const std::function<void()> &start, bool expectedSuccess = true)
{
    QEventLoop loop;
    bool finished = false;
    bool success = false;
    const auto connection = QObject::connect(&view, &QWebEngineView::loadFinished, &loop, [&](bool ok) {
        finished = true;
        success = ok;
        loop.quit();
    });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    start();
    loop.exec();
    QObject::disconnect(connection);
    require(finished && success == expectedSuccess, "Unexpected result loading test document");
}
}

int main(int argc, char **argv)
{
    QTemporaryDir directory;
    require(directory.isValid(), "Temporary profile directory");
    qputenv("XDG_DATA_HOME", directory.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", directory.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.filePath("config").toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName("iexplore-engine-test");
    app.setDesktopFileName("io.github.Mschmitt478.ChromiumExplorer");
    app.setWindowIcon(QIcon(":/explorer.svg"));
    require(!app.windowIcon().pixmap(16).isNull(), "Test windows use the packaged artwork");

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Create a localhost HTTP fixture");
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *connection = server.nextPendingConnection()) {
            QObject::connect(connection, &QTcpSocket::disconnected, connection, &QObject::deleteLater);
            QObject::connect(connection, &QTcpSocket::readyRead, connection, [connection] {
                const QByteArray request = connection->property("request").toByteArray() + connection->readAll();
                if (!request.contains("\r\n\r\n")) {
                    connection->setProperty("request", request);
                    return;
                }
                if (request.startsWith("GET /download ")) {
                    connection->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                                      "Content-Disposition: attachment; filename=fixture.txt\r\n"
                                      "Content-Length: 8\r\nConnection: close\r\n\r\nfixture!");
                    connection->disconnectFromHost();
                    return;
                }
                if (request.startsWith("GET /search ")) {
                    connection->write("HTTP/1.1 302 Found\r\nLocation: /challenge?ticket=synthetic%26encoded#verify\r\n"
                                      "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    connection->disconnectFromHost();
                    return;
                }
                if (request.startsWith("GET /challenge?ticket=synthetic%26encoded ")) {
                    const QByteArray body = "<title>Verification required</title><h1>Complete verification</h1>";
                    connection->write("HTTP/1.1 429 Too Many Requests\r\nContent-Type: text/html\r\n"
                                      "Connection: close\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    connection->disconnectFromHost();
                    return;
                }
                const QByteArray html = request.startsWith("GET /first ") ?
                    "<title>Loading</title><script>localStorage.setItem('test','first');document.title='First';</script>" :
                    "<title>Loading</title><script>document.title=localStorage.getItem('test') || 'Isolated';</script>";
                connection->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " +
                                  QByteArray::number(html.size()) + "\r\n\r\n" + html);
                connection->disconnectFromHost();
            });
        }
    });
    const auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    Engine engine(false);
    std::unique_ptr<QWebEngineView> view(engine.createView(nullptr));
    view->show();
    waitForLoad(*view, [&] { view->load(QUrl(base + "/first")); });
    require(view->title() == "First", "Chromium executes JavaScript");
    waitForLoad(*view, [&] { view->load(QUrl(base + "/second")); });
    require(view->title() == "first", "Same-origin storage available");
    require(view->history()->canGoBack(), "Back is available after navigation");
    waitForLoad(*view, [&] { view->back(); });
    require(view->title() == "First" && view->history()->canGoForward(), "Back and forward state");

    Engine privateEngine(true);
    std::unique_ptr<QWebEngineView> privateView(privateEngine.createView(nullptr));
    require(privateEngine.profile()->isOffTheRecord(), "Private profile is off the record");
    waitForLoad(*privateView, [&] { privateView->load(QUrl(base + "/second")); });
    require(privateView->title() == "Isolated", "Private profile isolates site data");

    QEventLoop downloadLoop;
    QTimer poll;
    const auto target = directory.filePath("fixture.txt");
    QObject::connect(&poll, &QTimer::timeout, &downloadLoop, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            dialog->selectFile(target);
            QMetaObject::invokeMethod(dialog, "accept");
        }
        if (!engine.downloads.isEmpty() && engine.downloads.constLast() && engine.downloads.constLast()->isFinished())
            downloadLoop.quit();
    });
    QTimer::singleShot(15000, &downloadLoop, &QEventLoop::quit);
    poll.start(50);
    view->page()->download(QUrl(base + "/download"));
    downloadLoop.exec();
    require(!engine.downloads.isEmpty() && engine.downloads.constLast() &&
            engine.downloads.constLast()->state() == QWebEngineDownloadRequest::DownloadCompleted, "Download completes");
    poll.stop();
    QFile saved(target);
    require(saved.open(QIODevice::ReadOnly) && saved.readAll() == "fixture!", "Download bytes match fixture");

    Store store(":memory:");
    Window window(engine, privateEngine, store);
    window.show();
    auto *browser = window.addPage();
    waitForLoad(*browser, [&] { browser->load(QUrl(base + "/search")); }, false);
    require(browser->title() == "Verification required", "Chromium received the HTTP 429 response body");
    require(browser->isVisible(), "The site's verification page stays visible after HTTP 429");
    require(engine.failure(browser).isEmpty(), "An HTTP response is distinct from a failed connection");

    auto *address = window.findChild<QComboBox *>("addressBar");
    require(address, "Browser address control exists");
    QApplication::setActiveWindow(&window);
    browser->setFocus();
    require(address->currentText() == base + "/challenge", "Unfocused address hides query and fragment");
    address->lineEdit()->setFocus();
    require(address->currentText() == base + "/challenge?ticket=synthetic%26encoded#verify",
            "Focused address preserves the full URL, including encoded query and fragment");
    browser->setFocus();
    require(address->currentText() == base + "/challenge", "Unfocused address is compact again");

    QAction *go = nullptr;
    for (auto *action : window.findChildren<QAction *>())
        if (action->text() == "Go")
            go = action;
    require(go, "Go action exists");
    waitForLoad(*browser, [&] { go->trigger(); }, false);
    require(browser->title() == "Verification required", "Go from compact mode keeps the original query");

#if QT_CONFIG(webengine_extensions)
    auto *manager = engine.extensions();
    require(manager && !privateEngine.extensions(), "Extension manager stays outside private windows");
    QDir files(directory.filePath("extension"));
    require(QDir().mkpath(files.path()), "Create extension fixture");
    QFile manifest(files.filePath("manifest.json"));
    require(manifest.open(QIODevice::WriteOnly), "Write extension manifest");
    manifest.write(R"({"manifest_version":3,"name":"Extension fixture","version":"1.0","action":{"default_popup":"popup.html"}})");
    manifest.close();
    QFile popup(files.filePath("popup.html"));
    require(popup.open(QIODevice::WriteOnly), "Write extension popup");
    popup.write("<title>Extension popup</title><h1>Ready</h1>");
    popup.close();

    QEventLoop extensionLoop;
    QWebEngineExtensionInfo installed;
    QObject::connect(manager, &QWebEngineExtensionManager::installFinished, &extensionLoop,
                     [&](const auto &info) { installed = info; extensionLoop.quit(); });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &extensionLoop, &QEventLoop::quit);
    timeout.start(10000);
    manager->installExtension(files.path());
    extensionLoop.exec();
    if (!installed.error().isEmpty())
        std::fprintf(stderr, "Extension install error: %s\n", qPrintable(installed.error()));
    require(!installed.id().isEmpty() && installed.isInstalled() && installed.isLoaded() &&
            !installed.isEnabled(), "Extension folder installs disabled");
    engine.setExtensionEnabled(installed, true);
    require(installed.isEnabled(), "Extension enables on request");
    {
        auto url = installed.actionPopupUrl();
        for (const auto &extension : manager->extensions())
            if (extension.id() == installed.id())
                url = extension.actionPopupUrl();
        std::unique_ptr<QWebEngineView> popupView(engine.createView(nullptr));
        waitForLoad(*popupView, [&] { popupView->load(url); });
        require(popupView->title() == "Extension popup", "Enabled extension popup renders");
    }
    engine.setExtensionEnabled(installed, false);
    require(!installed.isEnabled(), "Extension disables on request");
    QObject::connect(manager, &QWebEngineExtensionManager::uninstallFinished, &extensionLoop,
                     &QEventLoop::quit);
    timeout.start(10000);
    manager->uninstallExtension(installed);
    extensionLoop.exec();
    require(!installed.isLoaded(), "Extension uninstalls from the profile");
#endif

    QTcpServer unavailable;
    require(unavailable.listen(QHostAddress::LocalHost), "Reserve a port for the failed connection");
    const QUrl refused(QString("http://127.0.0.1:%1/").arg(unavailable.serverPort()));
    unavailable.close();
    waitForLoad(*browser, [&] { browser->load(refused); }, false);
    require(!browser->isVisible() && !engine.failure(browser).isEmpty(), "Connection failures still show the browser error");
}
