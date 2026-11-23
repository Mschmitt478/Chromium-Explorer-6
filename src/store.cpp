#include "store.hpp"
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <stdexcept>

bool supportedUrl(const QUrl &url)
{
    if (!url.isValid() || !url.userInfo().isEmpty())
        return false;
    if (url.isLocalFile())
        return url.host().isEmpty() && url.path().startsWith('/');
    return (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
}

QUrl searchUrl(const QString &text, const QString &provider)
{
    QUrl url(provider == "Google" ? "https://www.google.com/search" : "https://duckduckgo.com/");
    url.setQuery("q=" + QString::fromLatin1(QUrl::toPercentEncoding(text)), QUrl::StrictMode);
    return url;
}

QUrl interpretAddress(QString text, const QString &provider)
{
    text = text.trimmed();
    if (text.isEmpty())
        return {};
    if (text.startsWith('/'))
        return QUrl::fromLocalFile(text);
    const QUrl explicitUrl(text, QUrl::StrictMode);
    if (supportedUrl(explicitUrl))
        return explicitUrl;
    static const QRegularExpression host(
        R"(^(localhost|(?:[a-z0-9-]+\.)+[a-z0-9-]+|\[[0-9a-f:]+\])(?::[0-9]+)?(?:[/?#].*)?$)",
        QRegularExpression::CaseInsensitiveOption);
    if (!text.contains(' ') && host.match(text).hasMatch()) {
        const QUrl url("https://" + text, QUrl::StrictMode);
        if (supportedUrl(url))
            return url;
    }
    if (text.contains("://") || text.startsWith("file:", Qt::CaseInsensitive) ||
        text.startsWith("javascript:", Qt::CaseInsensitive) || text.startsWith("data:", Qt::CaseInsensitive))
        return {};
    return searchUrl(text, provider);
}

namespace {
QSqlQuery run(const QSqlDatabase &db, const QString &statement, const QVariantList &values = {})
{
    QSqlQuery query(db);
    if (query.prepare(statement)) {
        for (const auto &value : values)
            query.addBindValue(value);
        if (query.exec())
            return query;
    }
    throw std::runtime_error(query.lastError().text().toStdString());
}
}

Store::Store(const QString &path)
    : db(QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString()))
{
    db.setDatabaseName(path);
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    try {
        if (path != ":memory:" && !QDir().mkpath(QFileInfo(path).absolutePath()))
            throw std::runtime_error("Cannot create browser data directory");
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
        run(db, "CREATE TABLE IF NOT EXISTS visits (id INTEGER PRIMARY KEY, title TEXT, url TEXT, visited INTEGER)");
        run(db, "CREATE TABLE IF NOT EXISTS favorites (id INTEGER PRIMARY KEY, title TEXT, url TEXT, folder TEXT, UNIQUE(url, folder))");
    } catch (...) {
        const auto name = db.connectionName();
        db = {};
        QSqlDatabase::removeDatabase(name);
        throw;
    }
}

Store::~Store()
{
    const auto name = db.connectionName();
    db = {};
    QSqlDatabase::removeDatabase(name);
}

void Store::visit(const QUrl &url, const QString &title)
{
    if (supportedUrl(url))
        run(db, "INSERT INTO visits(title,url,visited) VALUES (?,?,?)",
            {title, url.toString(QUrl::FullyEncoded), QDateTime::currentSecsSinceEpoch()});
}

void Store::favorite(const QUrl &url, const QString &title, const QString &folder)
{
    if (supportedUrl(url))
        run(db, "INSERT INTO favorites(title,url,folder) VALUES (?,?,?) ON CONFLICT(url,folder) DO UPDATE SET title=excluded.title",
            {title, url.toString(QUrl::FullyEncoded), folder});
}

QList<Entry> Store::entries(bool favorites)
{
    auto query = run(db, favorites ?
        "SELECT id,title,url,folder FROM favorites ORDER BY folder,title COLLATE NOCASE" :
        "SELECT id,title,url,visited FROM visits ORDER BY id DESC LIMIT 5000");
    QList<Entry> rows;
    while (query.next()) {
        rows.append({
            query.value(0).toLongLong(), query.value(1).toString(), QUrl(query.value(2).toString()),
            favorites ? query.value(3).toString() :
                QDateTime::fromSecsSinceEpoch(query.value(3).toLongLong()).date().toString(Qt::ISODate)
        });
    }
    return rows;
}

void Store::remove(bool favorites, qint64 id)
{
    run(db, favorites ? "DELETE FROM favorites WHERE id=?" : "DELETE FROM visits WHERE id=?", {id});
}

void Store::organize(qint64 id, const QString &title, const QString &folder)
{
    run(db, "UPDATE favorites SET title=?,folder=? WHERE id=?", {title, folder, id});
}

void Store::clearHistory()
{
    run(db, "DELETE FROM visits");
}
