#pragma once
#include <QSettings>
#include <QSqlDatabase>
#include <QUrl>
#include <QList>

struct Entry {
    qint64 id;
    QString title;
    QUrl url;
    QString group;
};

bool supportedUrl(const QUrl &url);
QUrl searchUrl(const QString &text, const QString &provider);
QUrl interpretAddress(QString text, const QString &provider);

class Store {
public:
    explicit Store(const QString &path);
    ~Store();
    Store(const Store &) = delete;
    void visit(const QUrl &url, const QString &title);
    void favorite(const QUrl &url, const QString &title, const QString &folder);
    QList<Entry> entries(bool favorites);
    void remove(bool favorites, qint64 id);
    void organize(qint64 id, const QString &title, const QString &folder);
    void clearHistory();
private:
    QSqlDatabase db;
};
