#include "store.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <cstdlib>

static void require(bool condition, const char *description)
{
    if (!condition)
        qFatal("%s", description);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    require(interpretAddress("example.org", {}) == QUrl("https://example.org"), "Bare domain uses HTTPS");
    require(interpretAddress("http://localhost:8000", {}) == QUrl("http://localhost:8000"), "Explicit local HTTP");
    require(interpretAddress("/tmp/a b.html", {}).isLocalFile(), "Absolute path with spaces");
    require(!interpretAddress("file://other/path", {}).isValid(), "Reject remote file authority");
    require(!interpretAddress("https://bank.test@other.test", {}).isValid(), "Reject user info");
    require(!interpretAddress("javascript:alert(1)", {}).isValid(), "Reject script URL");
    require(!interpretAddress("data:text/html,test", {}).isValid(), "Reject data URL");
    const auto search = interpretAddress("cats & dogs", "Google");
    require(search.host() == "www.google.com", "Configured provider");
    require(QUrlQuery(search).queryItemValue("q", QUrl::FullyDecoded) == "cats & dogs", "Encode query values");

    require(interpretAddress("C++", {}).toEncoded().endsWith("q=C%2B%2B"), "Encode literal plus signs");

    QTemporaryDir folder;
    require(folder.isValid(), "Temporary data directory");
    const auto path = folder.filePath("browser.sqlite");
    const QUrl url("https://example.org/");
    {
        Store store(path);
        store.visit(url, "Example");
        store.favorite(url, "Example", "Work");
        store.favorite(url, "Second title", "Work");
        require(store.entries(true).size() == 1, "Favorite deduplicated per folder");
    }
    {
        Store store(path);
        const auto favorite = store.entries(true).at(0);
        require(favorite.title == "Second title" && favorite.url == url, "Favorite persists");
        store.organize(favorite.id, "Renamed", "Personal");
        require(store.entries(true).at(0).group == "Personal", "Favorite moved");
        const auto visit = store.entries(false).at(0);
        require(visit.title == "Example" && !visit.group.isEmpty(), "History persists with date");
        store.remove(false, visit.id);
        require(store.entries(false).isEmpty(), "Delete visit");
        store.visit(url, "Example");
        store.clearHistory();
        require(store.entries(false).isEmpty(), "Clear history");
        require(store.entries(true).size() == 1, "Clear history preserves favorites");
        store.remove(true, favorite.id);
        require(store.entries(true).isEmpty(), "Delete favorite");
    }
}
