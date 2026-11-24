#pragma once
#include <QMainWindow>
#include "engine.hpp"
#include "store.hpp"

class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QTabWidget;
class QToolBar;
class QTreeWidget;

class Window final : public QMainWindow {
public:
    Window(Engine &engine, Engine &privateEngine, Store &store, bool privateMode = false);
    ~Window() override;
    QWebEngineView *addPage(const QUrl &url = {}, bool background = false);
    void start(const QUrl &url, bool mainWindow = true);
    void openWindow(const QUrl &url = {}, bool privateWindow = false);
private:
    Engine &engine;
    Engine &privateEngine;
    Store &store;
    const bool privateMode;
    const bool tabsEnabled;
    QTabWidget *tabs;
    QComboBox *address;
    QUrl currentAddress;
    bool addressEdited = false;
    QMetaObject::Connection addressFocusConnection;
    QToolBar *navigation;
    QToolBar *addressBar;
    QDockWidget *explorer;
    QTreeWidget *tree;
    QLineEdit *filter;
    QLabel *message;
    QLabel *zone;
    QProgressBar *progress;
    QAction *backAction;
    QAction *forwardAction;
    QAction *stopAction;
    QMenu *favoritesMenu;
    bool showingFavorites = true;
    bool restoring = false;
    bool managesSession = false;
    QWebEngineView *current() const;
    void buildChrome();
    void buildMenus();
    void updateChrome();
    void updateAddressItems();
    void updateAddressText(bool focused);
    void submitAddress();
    void navigate(const QString &text);
    void home();
    void showExplorer(bool favorites);
    void populateExplorer();
    void addFavorite();
    void editFavorite(qint64 id, const QString &title, const QString &folder);
    void printPage(bool preview);
    void options();
    void showDownloads();
    void showExtensions();
    void saveSession();
    void go(const QUrl &url);
    void zoom(double delta);
    void createFindBar();
    void perform(const std::function<void()> &operation);
protected:
    void closeEvent(QCloseEvent *event) override;
};
