#include "window.hpp"
#include "theme.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QCryptographicHash>
#include <QLocalSocket>
#include <QTimer>
#include <QWebEnginePage>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    umask(0077);
    QApplication app(argc, argv);
    app.setApplicationName("Internet Explorer XP");
    app.setOrganizationName("ChromiumExplorer");
    app.setApplicationVersion(EXPLORER_VERSION);
    app.setDesktopFileName("io.github.Mschmitt478.ChromiumExplorer");
    app.setWindowIcon(toolbarIcon("web"));
    installLunaTheme(app);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("url", "Open a web address or local file.");
    QCommandLineOption privateOption("private", "Use an isolated off-the-record profile.");
    parser.addOption(privateOption);
    parser.process(app);
    QUrl url;
    if (!parser.positionalArguments().isEmpty()) {
        const auto input = parser.positionalArguments().first();
        url = QFileInfo(input).isFile() ? QUrl::fromLocalFile(QFileInfo(input).absoluteFilePath()) :
              interpretAddress(input, QSettings().value("search", "DuckDuckGo").toString());
        if (!supportedUrl(url)) {
            QMessageBox::warning(nullptr, "Open", "This address is unsupported.");
            return 1;
        }
    }

    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(data))
        return 1;
    const bool privateMode = parser.isSet(privateOption);
    const auto socketPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
        "/chromium-explorer-" + QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    QLockFile profileLock(data + "/profile.lock");
    profileLock.setStaleLockTime(0);
    QLocalServer launcher;
    if (!privateMode) {
        QLocalSocket existing;
        existing.connectToServer(socketPath);
        if (existing.waitForConnected(250)) {
            const auto payload = url.isEmpty() ? QByteArray{} : url.toEncoded();
            if (payload.size() > 16384)
                return 1;
            existing.write(payload + '\n');
            return (existing.bytesToWrite() == 0 || existing.waitForBytesWritten(1000)) ? 0 : 1;
        }
        if (!profileLock.tryLock()) {
            QMessageBox::warning(nullptr, "Internet Explorer XP",
                "The browser profile is in use. Try again after the running browser has started or closed.");
            return 1;
        }
        QLocalServer::removeServer(socketPath);
        launcher.setSocketOptions(QLocalServer::UserAccessOption);
        if (!launcher.listen(socketPath))
            return 1;
    }
    try {
        Store store(privateMode ? ":memory:" : data + "/browser.sqlite");
        Engine engine(privateMode);
        Engine privateEngine(true);
        auto *window = new Window(engine, privateEngine, store, privateMode);
        window->show();
        window->start(url);
        QObject::connect(&launcher, &QLocalServer::newConnection, &app, [&] {
            while (auto *client = launcher.nextPendingConnection()) {
                client->setReadBufferSize(16386);
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QTimer::singleShot(2000, client, &QObject::deleteLater);
                auto read = [client, &engine, &privateEngine, &store] {
                    if (!client->canReadLine() && client->bytesAvailable() < 16386)
                        return;
                    const auto input = client->readLine(16386);
                    client->disconnectFromServer();
                    if (!input.endsWith('\n') || input.size() > 16385)
                        return;
                    const auto address = QUrl::fromEncoded(input.chopped(1), QUrl::StrictMode);
                    if (input.size() > 1 && !supportedUrl(address))
                        return;
                    auto *browser = new Window(engine, privateEngine, store);
                    browser->show();
                    browser->start(address, false);
                };
                QObject::connect(client, &QLocalSocket::readyRead, client, read);
                read();
            }
        });
        const int result = app.exec();
        QList<Window *> windows;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *browser = dynamic_cast<Window *>(widget))
                windows.append(browser);
        qDeleteAll(windows);
        return result;
    } catch (const std::exception &error) {
        const auto detail = QString::fromUtf8(error.what());
        QMessageBox box(QMessageBox::Critical, "Internet Explorer XP", detail, QMessageBox::Ok);
        box.setTextFormat(Qt::PlainText);
        box.exec();
        return 1;
    }
}
