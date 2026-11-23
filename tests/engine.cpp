#include "engine.hpp"
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

void waitForLoad(QWebEngineView &view, const std::function<void()> &start)
{
    QEventLoop loop;
    bool success = false;
    const auto connection = QObject::connect(&view, &QWebEngineView::loadFinished, &loop, [&](bool ok) {
        success = ok;
        loop.quit();
    });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    start();
    loop.exec();
    QObject::disconnect(connection);
    require(success, "Timed out or failed to load test document");
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
    QFile saved(target);
    require(saved.open(QIODevice::ReadOnly) && saved.readAll() == "fixture!", "Download bytes match fixture");
}
