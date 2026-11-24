#include "window.hpp"
#include <QWebEngineExtensionManager>

#if QT_CONFIG(webengine_extensions)
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QWebEngineNavigationRequest>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#endif

void Window::showExtensions()
{
#if QT_CONFIG(webengine_extensions)
    auto *manager = engine.extensions();
    if (privateMode || !manager)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle("Extensions");
    dialog.resize(540, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel("Install compatible Manifest V3 extensions from a folder containing manifest.json. "
                                  "Enabled extensions can access browsing data according to their permissions.", &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *list = new QListWidget(&dialog);
    list->setAccessibleName("Installed extensions");
    layout->addWidget(list, 1);
    auto *details = new QLabel(&dialog);
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout->addWidget(details);

    auto *installFolder = new QPushButton("Install &Folder…", &dialog);
    layout->addWidget(installFolder, 0, Qt::AlignLeft);

    auto *actions = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *toggle = actions->addButton("&Enable", QDialogButtonBox::ActionRole);
    auto *open = actions->addButton("&Open", QDialogButtonBox::ActionRole);
    auto *remove = actions->addButton("&Remove", QDialogButtonBox::ActionRole);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    bool busy = false;
    const auto selected = [list, manager] {
        const auto *item = list->currentItem();
        if (item) {
            const auto id = item->data(Qt::UserRole).toString();
            for (const auto &extension : manager->extensions())
                if (extension.id() == id)
                    return extension;
        }
        return QWebEngineExtensionInfo{};
    };
    const auto updateActions = [&] {
        const auto extension = selected();
        const bool installed = !extension.id().isEmpty();
        const bool enabled = installed && extension.isEnabled();
        const auto popup = extension.actionPopupUrl();
        installFolder->setEnabled(!busy);
        toggle->setText(enabled ? "&Disable" : "&Enable");
        toggle->setEnabled(!busy && installed);
        remove->setEnabled(!busy && installed);
        open->setEnabled(!busy && enabled && popup.scheme() == "chrome-extension" && popup.host() == extension.id());
        details->setText(extension.description());
    };
    const auto refresh = [&](const QString &preferred = QString()) {
        const auto id = preferred.isEmpty() ? selected().id() : preferred;
        const QSignalBlocker blocker(list);
        list->clear();
        for (const auto &extension : manager->extensions()) {
            if (!extension.isInstalled() || !extension.isLoaded())
                continue;
            auto *item = new QListWidgetItem(extension.name() +
                (extension.isEnabled() ? " (enabled)" : " (disabled)"), list);
            item->setData(Qt::UserRole, extension.id());
            if (extension.id() == id)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        updateActions();
    };
    connect(list, &QListWidget::currentItemChanged, &dialog, updateActions);

    connect(installFolder, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getExistingDirectory(&dialog, "Select the folder containing manifest.json");
        if (path.isEmpty())
            return;
        busy = true;
        updateActions();
        manager->installExtension(path);
    });

    const auto confirm = [&dialog](const QString &message) {
        QMessageBox prompt(QMessageBox::Question, "Extensions", message,
                           QMessageBox::Yes | QMessageBox::No, &dialog);
        prompt.setTextFormat(Qt::PlainText);
        prompt.setDefaultButton(QMessageBox::No);
        return prompt.exec() == QMessageBox::Yes;
    };
    connect(toggle, &QPushButton::clicked, &dialog, [&] {
        const auto extension = selected();
        if (extension.id().isEmpty())
            return;
        const bool enable = !extension.isEnabled();
        if (enable && !confirm(QString("Enable %1? Only enable extensions from authors you trust.")
                                   .arg(extension.name())))
            return;
        engine.setExtensionEnabled(extension, enable);
        refresh(extension.id());
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        const auto extension = selected();
        if (extension.id().isEmpty() || !confirm(QString("Remove %1?").arg(extension.name())))
            return;
        engine.setExtensionEnabled(extension, false);
        busy = true;
        updateActions();
        manager->uninstallExtension(extension);
    });
    connect(open, &QPushButton::clicked, &dialog, [&] {
        const auto extension = selected();
        const auto url = extension.actionPopupUrl();
        if (!extension.isEnabled() || url.scheme() != "chrome-extension" || url.host() != extension.id())
            return;
        QDialog popup(&dialog);
        popup.setWindowTitle(extension.name());
        popup.resize(420, 520);
        auto *popupLayout = new QVBoxLayout(&popup);
        auto *view = engine.createView(&popup);
        popupLayout->addWidget(view);
        connect(view->page(), &QWebEnginePage::navigationRequested, view,
                [id = extension.id()](QWebEngineNavigationRequest &request) {
            if (request.isMainFrame() && (request.url().scheme() != "chrome-extension" || request.url().host() != id))
                request.reject();
        });
        view->load(url);
        popup.exec();
    });

    const auto finished = [&](const QWebEngineExtensionInfo &extension) {
        busy = false;
        refresh(extension.id());
        if (!extension.error().isEmpty()) {
            QMessageBox error(QMessageBox::Warning, "Extension Error", extension.error(), QMessageBox::Ok, &dialog);
            error.setTextFormat(Qt::PlainText);
            error.exec();
        }
    };
    connect(manager, &QWebEngineExtensionManager::installFinished, &dialog, finished);
    connect(manager, &QWebEngineExtensionManager::uninstallFinished, &dialog, finished);
    connect(manager, &QWebEngineExtensionManager::loadFinished, &dialog, [&](const QWebEngineExtensionInfo &) {
        refresh();
    });
    refresh();
    dialog.exec();
#endif
}
