#include "engine.hpp"
#include <QWebEngineCertificateError>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePermission>
#include <QWebEngineSettings>
#include <QWebEngineDownloadRequest>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>

Engine::Engine(bool privateMode)
    : session(privateMode ? new QWebEngineProfile : new QWebEngineProfile("Default"))
{
    if (!privateMode) {
        session->setPersistentStoragePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profile");
        session->setCachePath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/http");
    }
    const QSettings settings;
    if (!privateMode)
        session->setPersistentCookiesPolicy(settings.value("persistentCookies", true).toBool() ?
            QWebEngineProfile::AllowPersistentCookies : QWebEngineProfile::NoPersistentCookies);
    session->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled,
                                      settings.value("javascript", true).toBool());
    session->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);
    session->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    session->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    session->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    session->settings()->setUnknownUrlSchemePolicy(QWebEngineSettings::DisallowUnknownUrlSchemes);
    QObject::connect(session.get(), &QWebEngineProfile::downloadRequested, session.get(),
                     [this](auto *request) { download(request); });
}

QWebEngineView *Engine::createView(QWidget *parent)
{
    auto *view = new QWebEngineView(session.get(), parent);
    auto *page = view->page();
    QObject::connect(page, &QWebEnginePage::certificateError, view, [this, view](QWebEngineCertificateError error) {
        failures[view] = error.description();
        error.rejectCertificate();
    });
    QObject::connect(page, &QWebEnginePage::loadingChanged, view, [this, view](const QWebEngineLoadingInfo &info) {
        if (info.status() == QWebEngineLoadingInfo::LoadStartedStatus)
            failures.remove(view);
        if (info.status() == QWebEngineLoadingInfo::LoadFailedStatus && !info.isDownload() && !failures.contains(view))
            failures[view] = info.errorString();
    });
    QObject::connect(view, &QObject::destroyed, session.get(), [this, view] { failures.remove(view); });
    QObject::connect(page, &QWebEnginePage::permissionRequested, view, [view](QWebEnginePermission permission) {
        const auto origin = permission.origin();
        const auto type = permission.permissionType();
        QString label;
        using Type = QWebEnginePermission::PermissionType;
        switch (type) {
        case Type::MediaAudioCapture: label = "microphone"; break;
        case Type::MediaVideoCapture: label = "camera"; break;
        case Type::MediaAudioVideoCapture: label = "camera and microphone"; break;
        case Type::Geolocation: label = "location"; break;
        case Type::Notifications: label = "notifications"; break;
        default: permission.deny(); return;
        }
        QMessageBox prompt(QMessageBox::Question, "Site permission",
                           QString("Allow %1 to use %2?").arg(origin.toString(QUrl::FullyEncoded), label),
                           QMessageBox::Yes | QMessageBox::No, view->window());
        prompt.setTextFormat(Qt::PlainText);
        prompt.setDefaultButton(QMessageBox::No);
        QPointer<QWebEnginePage> requester(view->page());
        if (prompt.exec() == QMessageBox::Yes && requester && requester->url().scheme() == origin.scheme() &&
            requester->url().host() == origin.host() && requester->url().port() == origin.port())
            permission.grant();
        else
            permission.deny();
    });
    return view;
}

void Engine::download(QWebEngineDownloadRequest *request)
{
    if (request->isSavePageDownload()) {
        downloads.append(request);
        return;
    }
    QPointer<QWebEngineDownloadRequest> pending(request);
    const auto folder = QSettings().value("downloads",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    const auto path = QFileDialog::getSaveFileName(QApplication::activeWindow(), "Save As",
        folder + "/" + QFileInfo(request->downloadFileName()).fileName());
    if (!pending)
        return;
    if (path.isEmpty()) {
        request->cancel();
        return;
    }
    const QFileInfo target(path);
    request->setDownloadDirectory(target.absolutePath());
    request->setDownloadFileName(target.fileName());
    downloads.append(request);
    request->accept();
}
