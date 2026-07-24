#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <Version.h>
#include <dialogs/EditResourceDialog.h>
#include <dialogs/IngressDetailsDialog.h>
#include <dialogs/JobDetailsDialog.h>
#include <dialogs/LoginDialog.h>
#include <dialogs/LogsDialog.h>
#include <dialogs/NodeDetailsDialog.h>
#include <dialogs/PodDetailsDialog.h>
#include <dialogs/ServiceDetailsDialog.h>
#include <kubectl/KubectlClient.h>
#include <onelogin/OneLoginAuth.h>
#include <tables/GenericTable.h>
#include <tables/JobsTable.h>
#include <tables/KubeTable.h>
#include <tables/NamespacesTable.h>
#include <tables/NodesTable.h>
#include <tables/PodsTable.h>
#include <tables/ServicesTable.h>
#include <utils/Configuration.h>
#include <utils/IconUtils.h>
#include <utils/Logging.h>
#include <utils/UpdateChecker.h>

namespace {
    // Ensures a small local config file exists so Configuration/IconUtils resolve the dark icon set.
    QString ensureConfigFile() {
        const QString dir = QDir::homePath() + "/.kubewatch";
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(nullptr, "Create config directory","Could not create directory: " + dir);
            return {};
        }
        const QString path = dir + "/kubewatch.json";
        if (!QFile::exists(path)) {
            const QString installedDefault = QCoreApplication::applicationDirPath() + "/kubewatch.json";
            if (QFile::exists(installedDefault)) {
                QFile::copy(installedDefault, path);
            } else if (QFile file(path); file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(R"({"ui": {"style-type": "dark", "page-size": 25, "busy-indicator-delay-ms": 500}})");
                file.close();
            }
        }
        return path;
    }
} // namespace

namespace {
    enum class PageKind { Placeholder, Generic, Pods, Jobs, Services, Ingresses, Nodes, Namespaces, Settings };

    class MainWindow : public QMainWindow {
    public:
        MainWindow() {
            setWindowTitle("KubeWatch");
            resize(1800, 1200);

            KubectlClient::SetBusyOverlay(new BusyOverlay(this));

            statusLabel_ = new QLabel("Last refresh: never");
            statusBar()->addPermanentWidget(statusLabel_);

            auto *toolbar = addToolBar("Main");
            toolbar->setMovable(false);
            toolbar->addWidget(new QLabel(" Context: "));
            contextBox_ = new QComboBox();
            toolbar->addWidget(contextBox_);
            toolbar->addWidget(new QLabel(" Namespace: "));
            namespaceBox_ = new QComboBox();
            toolbar->addWidget(namespaceBox_);

            auto *spacer = new QWidget();
            spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            toolbar->addWidget(spacer);

            auto *refreshAction = new QAction(QIcon(":/icons/dark/refresh.svg"), QString(), this);
            refreshAction->setToolTip("Refresh");
            connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshCurrentPage);
            toolbar->addAction(refreshAction);

            auto *updateAction = new QAction(IconUtils::GetIcon("update"), QString(), this);
            updateAction->setToolTip("Check for Update");
            connect(updateAction, &QAction::triggered, this, [this] { updateChecker_->checkForUpdates(); });
            toolbar->addAction(updateAction);

            auto *central = new QWidget(this);
            auto *mainLayout = new QVBoxLayout(central);

            auto *splitter = new QSplitter(Qt::Horizontal);

            tree_ = new QTreeWidget();
            tree_->setHeaderHidden(true);
            splitter->addWidget(tree_);

            pages_ = new QStackedWidget();
            splitter->addWidget(pages_);
            splitter->setStretchFactor(1, 1);

            buildTree();

            logWidget_ = new QListWidget();
            logWidget_->setUniformItemSizes(true);
            logWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            logWidget_->setDragEnabled(true);

            autoScrollLogsButton_ = new QToolButton();
            autoScrollLogsButton_->setIcon(IconUtils::GetIcon("scroll"));
            autoScrollLogsButton_->setCheckable(true);
            autoScrollLogsButton_->setChecked(true);
            autoScrollLogsButton_->setToolTip("Auto-scroll to bottom");

            connect(&LogSignaler::instance(), &LogSignaler::newLog, this, [this](const QString &msg) {
                logWidget_->addItem(msg);
                if (logWidget_->count() > 5000) {
                    delete logWidget_->takeItem(0);
                }
                if (autoScrollLogsButton_->isChecked()) {
                    logWidget_->scrollToBottom();
                }
            });

            const auto *copyLogShortcut = new QShortcut(QKeySequence::Copy, logWidget_);
            connect(copyLogShortcut, &QShortcut::activated, this, [this] {
                QStringList lines;
                for (const QListWidgetItem *item : logWidget_->selectedItems()) {
                    lines << item->text();
                }
                if (!lines.isEmpty()) {
                    QApplication::clipboard()->setText(lines.join('\n'));
                }
            });

            auto *logContainer = new QWidget();
            auto *logContainerLayout = new QVBoxLayout(logContainer);
            logContainerLayout->setContentsMargins(0, 0, 0, 0);
            auto *logToolbar = new QHBoxLayout();
            logToolbar->addWidget(new QLabel("Log"));
            logToolbar->addStretch();
            logToolbar->addWidget(autoScrollLogsButton_);
            logContainerLayout->addLayout(logToolbar);
            logContainerLayout->addWidget(logWidget_);

            auto *verticalSplitter = new QSplitter(Qt::Vertical);
            verticalSplitter->addWidget(splitter);
            verticalSplitter->addWidget(logContainer);
            verticalSplitter->setStretchFactor(0, 1);
            verticalSplitter->setSizes({700, 200});

            mainLayout->addWidget(verticalSplitter);
            setCentralWidget(central);

            connect(tree_, &QTreeWidget::currentItemChanged, this, &MainWindow::onTreeSelectionChanged);
            connect(contextBox_, &QComboBox::currentIndexChanged, this, &MainWindow::onContextChanged);
            connect(namespaceBox_, &QComboBox::currentIndexChanged, this, &MainWindow::onNamespaceChanged);

            {
                const QSignalBlocker blocker(contextBox_);
                loadContexts();
                if (const auto savedContext = Configuration::instance().GetValue<QString>("ui.last-context", QString()); !savedContext.isEmpty()) {
                    if (const int idx = contextBox_->findText(savedContext); idx >= 0) contextBox_->setCurrentIndex(idx);
                }
            }

            {
                const QSignalBlocker blocker(namespaceBox_);
                loadNamespaces();
                const auto savedNamespace =
                    Configuration::instance().GetValue<QString>("ui.last-namespace", QString());
                if (!savedNamespace.isEmpty()) {
                    if (const int idx = namespaceBox_->findText(savedNamespace); idx >= 0) namespaceBox_->setCurrentIndex(idx);
                }
            }

            const auto savedNavItem = Configuration::instance().GetValue<QString>("ui.last-nav-item", QString());
            QTreeWidgetItem* restoredItem = savedNavItem.isEmpty() ? nullptr : findLeafByLabel(savedNavItem);
            tree_->setCurrentItem(restoredItem ? restoredItem : nodesItem_);

            // Show the login prompt once the event loop starts, so it appears on top of
            // the already-visible main window rather than blocking its own construction.
            QTimer::singleShot(0, this, &MainWindow::showLoginDialog);

            startUpdateChecker();
        }

    private:
        // Checks version.txt on GitHub Pages against APP_VERSION: once silently shortly
        // after startup (and periodically thereafter), and on demand via the toolbar
        // action (which also confirms when already up to date).
        void startUpdateChecker() {
            updateChecker_ = new UpdateChecker(this);
            connect(updateChecker_, &UpdateChecker::UpdateAvailable, this, [this](const QString &ver) {
                if (ver.isEmpty()) {
                    QMessageBox::information(this, "Check for Update", "You already have the latest version.");
                    return;
                }
                QMessageBox box(QMessageBox::Information, "Update Available",
                                 "kubewatch " + ver + " is available. You are running " + QString(APP_VERSION) + ".",
                                 QMessageBox::NoButton, this);
                box.addButton(QMessageBox::Close);
                QPushButton *downloadButton = box.addButton("Download", QMessageBox::ActionRole);
                box.exec();
                if (box.clickedButton() == downloadButton) {
                    QDesktopServices::openUrl(QUrl("https://jensvogt.github.io/kubewatch/"));
                }
            });
            updateChecker_->checkForUpdatesNoNotification();

            auto *updateTimer = new QTimer(this);
            const int intervalSeconds = Configuration::instance().GetValue<int>("general.update-check-period", 24 * 3600);
            updateTimer->setInterval(intervalSeconds * 1000);
            connect(updateTimer, &QTimer::timeout, this, [this] { updateChecker_->checkForUpdatesNoNotification(); });
            updateTimer->start();
        }

        [[nodiscard]]
        QStringList baseArgs() const { return {"--kubeconfig", KubectlClient::Kubeconfig(), "--context", contextBox_->currentText()}; }

        [[nodiscard]]
        QStringList resourceArgs(const QString &resource) const {
            QStringList args = baseArgs();
            args << "get" << resource;
            const QString ns = namespaceBox_->currentText();
            if (ns.isEmpty() || ns == "All namespaces") {
                args << "--all-namespaces";
            } else {
                args << "-n" << ns;
            }
            args << "-o" << "json";
            return args;
        }

        QTreeWidgetItem *addLeaf(QTreeWidgetItem *parent, const QString &label, const PageKind kind, const QString &kubectlName = QString()) {
            QWidget *page = nullptr;
            switch (kind) {
                case PageKind::Settings:
                    page = new QLabel("Kubeconfig: " + KubectlClient::Kubeconfig());
                    break;
                case PageKind::Placeholder:
                    page = new QLabel("Select a resource from the tree on the left.");
                    break;
                case PageKind::Generic:
                case PageKind::Ingresses:
                    page = new GenericTable(kubectlName);
                    break;
                case PageKind::Pods:
                    page = new PodsTable();
                    break;
                case PageKind::Jobs:
                    page = new JobsTable();
                    break;
                case PageKind::Services:
                    page = new ServicesTable();
                    break;
                case PageKind::Nodes:
                    page = new NodesTable();
                    break;
                case PageKind::Namespaces:
                    page = new NamespacesTable();
                    break;
            }

            const int pageIndex = pages_->addWidget(page);

            if (auto *table = qobject_cast<KubeTable *>(page)) {
                table->SetArgsProviders([this](const QString &resource) { return resourceArgs(resource); },
                                         [this] { return baseArgs(); });
                connect(table, &PageableTable::ReloadTable, this, &MainWindow::refreshCurrentPage);
                connect(table, &PageableTable::ContextMenuRequested, this,
                        [this, pageIndex](const QPoint &pos) { showTableContextMenu(pageIndex, pos); });
                if (kind == PageKind::Jobs) {
                    connect(table, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showJobDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Pods) {
                    connect(table, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showPodDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Services) {
                    connect(table, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showServiceDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Ingresses) {
                    connect(table, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showIngressDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Nodes) {
                    connect(table, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showNodeDetailsForRow(pageIndex, index); });
                }
            }

            auto *item = parent ? new QTreeWidgetItem(parent, {label}) : new QTreeWidgetItem(tree_, {label});
            item->setData(0, Qt::UserRole, pageIndex);
            return item;
        }

        void buildTree() {
            pages_->addWidget(new QLabel("Select a resource from the tree on the left."));

            auto *workloads = new QTreeWidgetItem(tree_, {"Workloads"});
            addLeaf(workloads, "Pods", PageKind::Pods, "pods");
            addLeaf(workloads, "Deployments", PageKind::Generic, "deployments");
            addLeaf(workloads, "StatefulSets", PageKind::Generic, "statefulsets");
            addLeaf(workloads, "DaemonSets", PageKind::Generic, "daemonsets");
            addLeaf(workloads, "Jobs", PageKind::Jobs, "jobs");

            auto *service = new QTreeWidgetItem(tree_, {"Service"});
            addLeaf(service, "Services", PageKind::Services, "services");
            addLeaf(service, "Ingresses", PageKind::Ingresses, "ingresses");

            auto *configStorage = new QTreeWidgetItem(tree_, {"Config and Storage"});
            addLeaf(configStorage, "ConfigMaps", PageKind::Generic, "configmaps");
            addLeaf(configStorage, "Secrets", PageKind::Generic, "secrets");
            addLeaf(configStorage, "PersistentVolumeClaims", PageKind::Generic, "persistentvolumeclaims");

            auto *cluster = new QTreeWidgetItem(tree_, {"Cluster"});
            nodesItem_ = addLeaf(cluster, "Nodes", PageKind::Nodes, "nodes");
            addLeaf(cluster, "Namespaces", PageKind::Namespaces, "namespaces");

            addLeaf(nullptr, "Settings", PageKind::Settings);

            tree_->expandAll();
        }

        void loadContexts() const {
            const QString result = KubectlClient::runKubectl({"--kubeconfig", KubectlClient::Kubeconfig(), "config", "get-contexts", "-o", "name"});
            contextBox_->addItems(result.split('\n', Qt::SkipEmptyParts));

            const QString current = KubectlClient::runKubectl({"--kubeconfig", KubectlClient::Kubeconfig(), "config", "current-context"}).trimmed();
            if (const int index = contextBox_->findText(current); index >= 0) {
                contextBox_->setCurrentIndex(index);
            }
        }

        void loadNamespaces() const {
            // Clearing/repopulating the combo changes its current index as a side effect,
            // which would otherwise fire onNamespaceChanged (and a spurious extra page
            // refresh) for each such change -- callers already trigger their own single
            // deliberate refresh once this returns.
            const QSignalBlocker blocker(namespaceBox_);
            namespaceBox_->clear();
            namespaceBox_->addItem("All namespaces");
            QStringList args = baseArgs();
            args << "get" << "namespaces" << "-o" << "json";
            for (const auto &item: KubectlClient::fetchItems(args)) {
                namespaceBox_->addItem(item.toObject()["metadata"].toObject()["name"].toString());
            }
        }

        void onContextChanged() {
            Configuration::instance().SetValue("ui.last-context", contextBox_->currentText());
            // showLoginDialog() already refreshes credentials/namespaces/the current page
            // on success; only do it here as a fallback if login was cancelled or failed,
            // so switching context doesn't leave stale data on screen either way.
            if (!showLoginDialog()) {
                BusyGuard busyGuard;
                updateActiveAwsCredentials();
                loadNamespaces();
                refreshCurrentPage();
            }
        }

        // Derives which OneLogin/AWS account ("int" or "prod") the currently selected
        // kubectl context belongs to.
        [[nodiscard]] QString currentAwsAccountKey() const {
            return contextBox_->currentText().contains("prod", Qt::CaseInsensitive) ? "prod" : "int";
        }

        // Points KubectlClient's active credentials at whatever cached session (if any)
        // applies to the currently selected context, so runKubectlCommand picks it up.
        void updateActiveAwsCredentials() const {
            const auto it = awsCredentialsByAccount_.find(currentAwsAccountKey());
            KubectlClient::SetActiveAwsCredentials(it != awsCredentialsByAccount_.end() ? it.value() : AwsSessionCredentials{});
        }

        void onNamespaceChanged() const {
            Configuration::instance().SetValue("ui.last-namespace", namespaceBox_->currentText());
            refreshCurrentPage();
        }

        [[nodiscard]] QTreeWidgetItem* findLeafByLabel(const QString& label) const {
            QTreeWidgetItemIterator it(tree_);
            while (*it) {
                if ((*it)->text(0) == label && (*it)->data(0, Qt::UserRole).isValid()) {
                    return *it;
                }
                ++it;
            }
            return nullptr;
        }

        void onTreeSelectionChanged(const QTreeWidgetItem *current, QTreeWidgetItem *) const {
            if (!current) return;
            const QVariant data = current->data(0, Qt::UserRole);
            if (!data.isValid()) {
                pages_->setCurrentIndex(0);
                return;
            }
            Configuration::instance().SetValue("ui.last-nav-item", current->text(0));
            const int pageIndex = data.toInt();
            pages_->setCurrentIndex(pageIndex);
            loadPage(pageIndex);
        }

        void refreshCurrentPage() const {
            const QTreeWidgetItem *current = tree_->currentItem();
            if (!current) return;
            const QVariant data = current->data(0, Qt::UserRole);
            if (!data.isValid()) return;
            loadPage(data.toInt());
        }

        void loadPage(const int pageIndex) const {
            if (auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex))) {
                table->Refresh();
                statusLabel_->setText("Last refresh: " + QDateTime::currentDateTime().toString("HH:mm:ss"));
            }
        }

        void showTableContextMenu(const int pageIndex, const QPoint &pos) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table) return;

            const QModelIndex index = table->GetIndexFromPosition(pos);
            if (!index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const QString ns = table->NamespaceColumn() >= 0 ? table->GetValue<QString>(index, table->NamespaceColumn()) : QString();

            QMenu menu(this);
            const QAction *logsAction =
                table->SupportsLogs() ? menu.addAction(IconUtils::GetIcon("logs"), "Logs") : nullptr;
            const QAction *editAction = menu.addAction(IconUtils::GetIcon("edit"), "Edit");
            const QAction *deleteAction = menu.addAction(IconUtils::GetIcon("delete"), "Delete");

            if (const QAction *chosen = menu.exec(table->GetGlobalPosition(pos)); chosen == editAction) {
                editResource(table->ResourceName(), name, ns);
            } else if (chosen == deleteAction) {
                deleteResource(table->ResourceName(), name, ns);
            } else if (chosen == logsAction) {
                LogsDialog::Show(this, baseArgs(), table->ResourceName(), name, ns);
            }
        }

        void editResource(const QString &resource, const QString &name, const QString &ns) {
            if (EditResourceDialog::Show(this, baseArgs(), resource, name, ns)) {
                refreshCurrentPage();
            }
        }

        void deleteResource(const QString &resource, const QString &name, const QString &ns) {
            if (const QString displayName = ns.isEmpty() ? name : ns + "/" + name;
                QMessageBox::question(this, "Delete " + resource, "Delete " + resource + " \"" + displayName + "\"?") != QMessageBox::Yes) {
                return;
            }

            QStringList args = baseArgs();
            args << "delete" << resource << name;
            if (!ns.isEmpty()) args << "-n" << ns;

            if (const KubectlResult result = KubectlClient::runKubectlCommand(args); !result.success) {
                QMessageBox::warning(this, "Delete failed", result.error);
            }
            refreshCurrentPage();
        }

        void showJobDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;
            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, table->NamespaceColumn());
            JobDetailsDialog::Show(this, baseArgs(), name, ns);
        }

        void showPodDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;
            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, table->NamespaceColumn());
            PodDetailsDialog::Show(this, baseArgs(), name, ns);
        }

        void showServiceDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;
            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, table->NamespaceColumn());
            ServiceDetailsDialog::Show(this, baseArgs(), name, ns);
        }

        void showIngressDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;
            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, table->NamespaceColumn());
            IngressDetailsDialog::Show(this, baseArgs(), name, ns);
        }

        void showNodeDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<KubeTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;
            const auto name = table->GetValue<QString>(index, 0);
            NodeDetailsDialog::Show(this, baseArgs(), name);
        }

        // Writes the new session credentials to ~/.aws/credentials under a profile named
        // after the account, and points that kubeconfig context's exec plugin at that
        // profile -- so kubectl/aws used outside kubewatch (a plain terminal, another
        // tool) also picks up the freshly-obtained session. Best-effort: a failure here
        // doesn't affect kubewatch's own kubectl calls, which already use the in-memory
        // credentials directly.
        void updateExternalAwsConfig(const QString &accountKey, const AwsSessionCredentials &credentials, const QString &contextName) const {
            const QList<QStringList> setCalls = {
                {"configure", "set", "aws_access_key_id", credentials.accessKeyId, "--profile", accountKey},
                {"configure", "set", "aws_secret_access_key", credentials.secretAccessKey, "--profile", accountKey},
                {"configure", "set", "aws_session_token", credentials.sessionToken, "--profile", accountKey},
            };
            for (const QStringList &args: setCalls) {
                QProcess process;
                process.start("aws", args);
                if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                    logWarning << "Failed to update ~/.aws/credentials profile" << accountKey << ":"
                               << QString::fromLocal8Bit(process.readAllStandardError());
                    return;
                }
            }
            logInfo << "Updated ~/.aws/credentials profile" << accountKey;

            const KubectlResult viewResult = KubectlClient::runKubectlCommand({"--kubeconfig", KubectlClient::Kubeconfig(), "config", "view", "-o", "json"});
            if (!viewResult.success) {
                logWarning << "Failed to read kubeconfig to update exec credentials:" << viewResult.error;
                return;
            }
            const QJsonArray contexts = QJsonDocument::fromJson(viewResult.output.toUtf8()).object()["contexts"].toArray();
            QString userName;
            for (const auto &contextValue: contexts) {
                if (const QJsonObject contextObj = contextValue.toObject(); contextObj["name"].toString() == contextName) {
                    userName = contextObj["context"].toObject()["user"].toString();
                    break;
                }
            }
            if (userName.isEmpty()) {
                logWarning << "Could not find kubeconfig user for context" << contextName;
                return;
            }

            const KubectlResult setCredsResult = KubectlClient::runKubectlCommand(
                {"--kubeconfig", KubectlClient::Kubeconfig(), "config", "set-credentials", userName, "--exec-env=AWS_PROFILE=" + accountKey});
            if (!setCredsResult.success) {
                logWarning << "Failed to update kubeconfig exec env for user" << userName << ":" << setCredsResult.error;
                return;
            }
            logInfo << "Updated kubeconfig user" << userName << "to use AWS profile" << accountKey;
        }

        // Returns true if login completed successfully (in which case credentials,
        // namespaces and the current page have already been refreshed), false if the
        // dialog was cancelled or login failed for any reason.
        bool showLoginDialog() {
            QStringList contexts;
            for (int i = 0; i < contextBox_->count(); ++i) {
                contexts << contextBox_->itemText(i);
            }

            const auto result = LoginDialog::Show(this, contexts, contextBox_->currentText(),
                                                   Configuration::instance().GetValue<QString>("onelogin.user", QString()));

            // Switch the main window to the chosen context too, so the user ends up
            // browsing whatever cluster they just signed in for -- even if login itself
            // then fails, same as before this was split out into LoginDialog.
            if (!result.selectedContext.isEmpty()) {
                if (const int idx = contextBox_->findText(result.selectedContext); idx >= 0 && idx != contextBox_->currentIndex()) {
                    contextBox_->setCurrentIndex(idx);
                }
            }
            if (!result.success) return false;

            {
                // Wrap the whole post-login sequence in one BusyGuard so the several
                // sequential kubectl/aws calls it triggers show a single spinner instead
                // of flickering hide/show between each one.
                BusyGuard busyGuard;
                awsCredentialsByAccount_[result.accountKey] = result.credentials;
                updateActiveAwsCredentials();
                loadNamespaces();
                refreshCurrentPage();
                Configuration::instance().SetValue("onelogin.user", result.username);
                updateExternalAwsConfig(result.accountKey, result.credentials, result.selectedContext);
            }

            QMessageBox::information(this, "Login",
                                      "Signed in for account \"" + result.accountKey + "\". Session valid until " +
                                          result.credentials.expiresAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") + ".");
            return true;
        }

        QComboBox *contextBox_;
        QComboBox *namespaceBox_;
        QLabel *statusLabel_;
        QTreeWidget *tree_;
        QStackedWidget *pages_;
        QListWidget *logWidget_;
        QToolButton *autoScrollLogsButton_;
        QTreeWidgetItem *nodesItem_ = nullptr;
        QMap<QString, AwsSessionCredentials> awsCredentialsByAccount_;
        UpdateChecker *updateChecker_ = nullptr;
    };
}

namespace {
    void applyDarkTheme(QApplication &app) {
        QApplication::setStyle("Fusion");

        QPalette palette;
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(35, 35, 35));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));

        QApplication::setPalette(palette);
    }
} // namespace

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setWindowIcon(IconUtils::GetCommonIcon("kubewatch"));
    applyDarkTheme(a);
    Configuration::instance().SetFilePath(ensureConfigFile());
    qInstallMessageHandler(myCustomMessageHandler);
    LogSignaler::instance().SetLevel(DEBUG);
    MainWindow window;
    window.show();
    return QApplication::exec();
}
