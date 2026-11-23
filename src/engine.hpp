#pragma once
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QHash>
#include <QPointer>
#include <memory>

class Engine {
public:
    explicit Engine(bool privateMode);
    QWebEngineView *createView(QWidget *parent);
    QWebEngineProfile *profile() const { return session.get(); }
    void download(QWebEngineDownloadRequest *request);
    QList<QPointer<QWebEngineDownloadRequest>> downloads;
    QString failure(QWebEngineView *view) const { return failures.value(view); }
private:
    std::unique_ptr<QWebEngineProfile> session;
    QHash<QWebEngineView *, QString> failures;
};
