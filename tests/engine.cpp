#include "window.hpp"
#include <QApplication>
#include <QEventLoop>
#include <QFileDialog>
#include <QWebEngineDownloadRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineHistory>
#include <QWebEnginePage>

namespace {
void require(bool condition, const char *description)
{
    if (!condition)
        qFatal("%s", description);
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
                    connection->write("HTTP/1.1 302 Found\r\nLocation: /challenge\r\n"
                                      "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    connection->disconnectFromHost();
                    return;
                }
                if (request.startsWith("GET /challenge ")) {
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

    QTcpServer unavailable;
    require(unavailable.listen(QHostAddress::LocalHost), "Reserve a port for the failed connection");
    const QUrl refused(QString("http://127.0.0.1:%1/").arg(unavailable.serverPort()));
    unavailable.close();
    waitForLoad(*browser, [&] { browser->load(refused); }, false);
    require(!browser->isVisible() && !engine.failure(browser).isEmpty(), "Connection failures still show the browser error");
}
