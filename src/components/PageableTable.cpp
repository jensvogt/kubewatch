//
// Created by vogje01 on 2/15/26.
//

#include <components/PageableTable.h>
#include "ui_PageableTable.h"

PageableTable::PageableTable(QWidget *parent) : QWidget(parent), _ui(new Ui::PageableTable) {

    // Set default page size
    _pageSize = Configuration::instance().GetValue<int>("ui.page-size", 1000);

    // Setup component
    _ui->setupUi(this);

    // Prefix edit
    _ui->prefixEdit->setPlaceholderText(_searchFieldPlaceholder);
    _ui->prefixEdit->setEnabled(true);
    connect(_ui->prefixEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        _ui->prefixClearButton->setDisabled(text.isEmpty());
    });
    connect(_ui->prefixEdit, &QLineEdit::returnPressed, this, [this]() {
        _proxyModel->setFilterColumn(0);
        _proxyModel->setFilterPrefix(_ui->prefixEdit->text());
        emit ReloadTable();
    });

    // Prefix clear button
    _ui->prefixClearButton->setDisabled(true);
    _ui->prefixClearButton->setText(nullptr);
    _ui->prefixClearButton->setIcon(IconUtils::GetIcon("clear"));
    _ui->prefixClearButton->setToolTip("Clear the prefix field");
    connect(_ui->prefixClearButton, &QPushButton::clicked, this, [this]() {
        _proxyModel->clearFilter();
        _ui->prefixEdit->setText(nullptr);
        _ui->prefixClearButton->setDisabled(true);
        emit ReloadTable();
    });

    // Table
    _dataModel = new QStandardItemModel(this);
    _dataModel->setHorizontalHeaderLabels(_headerNames);
    _dataModel->setColumnCount(static_cast<int>(_headerNames.count()));

    // Proxy model for prefix filtering
    _proxyModel = new PrefixFilterProxyModel(this);
    _proxyModel->setSourceModel(_dataModel);
    _proxyModel->setDynamicSortFilter(true);

    // Table definition
    _ui->tableView->setModel(_proxyModel);
    _ui->tableView->setShowGrid(true);
    _ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    _ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _ui->tableView->setSortingEnabled(true);
    _ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Start button
    _ui->startButton->setText(nullptr);
    _ui->startButton->setIcon(IconUtils::GetIcon("begin"));
    connect(_ui->startButton, &QPushButton::clicked, this, [this]() {
        _pageIndex = 0;
        CalculatePageStatus();
    });

    _ui->previousButton->setText(nullptr);
    _ui->previousButton->setIcon(IconUtils::GetIcon("previous"));
    connect(_ui->previousButton, &QPushButton::clicked, this, [this]() {
        _pageIndex--;
        if (_pageIndex < 0) {
            _pageIndex = 0;
        }
        CalculatePageStatus();
        emit ReloadTable();
    });

    _ui->nextButton->setText(nullptr);
    _ui->nextButton->setIcon(IconUtils::GetIcon("next"));
    connect(_ui->nextButton, &QPushButton::clicked, this, [this]() {
        _pageIndex++;
        if (_pageIndex >= _maxPage) {
            _pageIndex = _maxPage;
        }
        CalculatePageStatus();
        emit ReloadTable();
    });

    _ui->endButton->setText(nullptr);
    _ui->endButton->setIcon(IconUtils::GetIcon("end"));
    connect(_ui->endButton, &QPushButton::clicked, this, [this]() {
        _pageIndex = _maxPage;
        CalculatePageStatus();
        emit ReloadTable();
    });

    _ui->pageSizeEdit->setText(QString::number(_pageSize));
    connect(_ui->pageSizeEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        _pageSize = text.toLong();
        _maxPage = (_totalSize + _pageSize - 1) / _pageSize;
        CalculatePageStatus();
        emit ReloadTable();
    });

    // Add context menu
    _ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_ui->tableView, &QTableView::customContextMenuRequested, this, [this](const QPoint pos) {
        emit ContextMenuRequested(pos);
    });

    // Single click proxy
    connect(_ui->tableView, &QTableView::clicked, this, [this](const QModelIndex &index) {
        emit SingleClick(index);
    });

    // Double click proxy
    connect(_ui->tableView, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        emit DoubleClicked(_proxyModel->mapToSource(index));
    });

    // Sorting header clicked
    connect(_ui->tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](const int logicalIndex, const Qt::SortOrder order) {
        _sortColumn = logicalIndex;
        _sortDirection = order == Qt::AscendingOrder ? 1 : -1;
        _sortAttribute = _headerNames[logicalIndex];
    });

    // Defaults
    _ui->pageStatusLabel->setText(QString("%1 - %2/%3").arg(0).arg(_pageSize).arg(_totalSize));
    SetLastUpdate();

    // Shortcuts
    auto *detailsAction = new QAction(this);
    detailsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    detailsAction->setShortcutContext(Qt::WidgetShortcut);
    _ui->tableView->addAction(detailsAction);
    connect(detailsAction, &QAction::triggered, this, &PageableTable::ShowDetails);

    // Reset message box
    _ui->messageLabel->setText(nullptr);

    // Timer
    connect(&EventBus::instance(), &EventBus::TimerSignal, this, [this](const QString &timerName, const qint64 elapsed) {
        if (_serviceApis.contains(timerName)) {
            _ui->statusLabel->setText("Last update: " + QDateTime::currentDateTime().toString("hh:mm:ss") + " [" + QString::number(elapsed) + "ms]");
        }
    });

}

PageableTable::~PageableTable() {
    delete _ui;
}

void PageableTable::SetSearchFieldPlaceholder(const QString &placeholder) {
    _searchFieldPlaceholder = placeholder;
    _ui->prefixEdit->setPlaceholderText(placeholder);
}

void PageableTable::CalculatePageStatus() {
    long start = _pageIndex * _pageSize;
    if (start >= _totalSize) {
        _pageIndex = --_pageIndex > 0 ? _pageIndex : 0;
        start = _pageIndex * _pageSize;
    }
    long end = _pageIndex * _pageSize + _pageSize;
    if (end > _totalSize) {
        end = _totalSize;
    }
    _ui->pageStatusLabel->setText(QString("%1 - %2 / %3").arg(start).arg(end).arg(_totalSize));
    SetLastUpdate();
}

void PageableTable::UpdateSorting() const {
    _proxyModel->sort(_sortColumn, _sortDirection == 1 ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void PageableTable::SetHeaderNames(const QStringList &headerNames) {
    _headerNames = headerNames;
    _dataModel->setHorizontalHeaderLabels(_headerNames);
    _dataModel->setColumnCount(static_cast<int>(_headerNames.count()));
}

void PageableTable::SetResizeModes(const QList<QHeaderView::ResizeMode> &resizeModes) const {
    for (int i = 0; i < static_cast<int>(resizeModes.count()); i++) {
        _ui->tableView->horizontalHeader()->setSectionResizeMode(i, resizeModes.at(i));
    }
}

void PageableTable::SetHiddenColumns(const QList<int> &hiddenColumns) const {
    for (int i = 0; i < static_cast<int>(hiddenColumns.count()); i++) {
        _ui->tableView->setColumnHidden(hiddenColumns.at(i), true);
    }
}

void PageableTable::SetColumn(const int row, const int column, const QString &value, const Qt::Alignment &alignment) const {
    const auto item = new QStandardItem(value);
    item->setTextAlignment(alignment);
    const QModelIndex index = _dataModel->index(row, column);
    _dataModel->setData(index, QVariant(alignment));
    _dataModel->setData(index, value, Qt::EditRole);
    _dataModel->setData(index, value, Qt::DisplayRole);
    _dataModel->setItem(row, column, item);
}

void PageableTable::SetColumn(const int row, const int column, const QDateTime &value) const {
    _dataModel->setItem(row, column, new QStandardItem(DateTimeUtils::GetDateTimeFormat(value)));
}

void PageableTable::SetColumn(const int row, const int column, const long &value) const {
    const QModelIndex index = _dataModel->index(row, column);
    _dataModel->setData(index, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    _dataModel->setData(index, static_cast<qlonglong>(value), Qt::UserRole);
    _dataModel->setData(index, static_cast<qlonglong>(value), Qt::DisplayRole);
}

void PageableTable::SetColumn(const int row, const int column, const bool value, const QIcon &enabledIcon, const QIcon &disabledIcon) const {
    const QModelIndex index = _dataModel->index(row, column);

    // Set the icon (Decoration)
    _dataModel->setData(index, value ? enabledIcon : disabledIcon, Qt::DecorationRole);

    // Set the underlying value (UserRole) for sorting or logic
    _dataModel->setData(index, value, Qt::UserRole);

    // Ensure text is clear
    _dataModel->setData(index, "", Qt::DisplayRole);

    // Set checked state
    //_dataModel->setData(index, value ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
}

void PageableTable::SetHiddenColumn(const int row, const int column, const QString &value) const {
    const auto item = new QStandardItem(value);
    item->setData(value, Qt::EditRole);
    _dataModel->setItem(row, column, item);
}

void PageableTable::SetHiddenColumn(const int row, const int column, const bool value) const {
    const auto checkItem = new QStandardItem();
    checkItem->setCheckState(value ? Qt::Checked : Qt::Unchecked);
    checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    _dataModel->setItem(row, column, checkItem);
}

void PageableTable::SetStatus(const QString &message) const {
    //    _ui->statusLabel->setText(message);
}

void PageableTable::SetLastUpdate() const {
    //    const QString message = "Last update: " + DateTimeUtils::GetLogTimeFormat(QDateTime::currentDateTime());
    //    _ui->statusLabel->setText(message);
}

QModelIndex PageableTable::GetIndexFromPosition(const QPoint &pos) const {

    const QModelIndex proxyIndex = _ui->tableView->indexAt(pos);
    if (!proxyIndex.isValid())
        return {};

    return _proxyModel->mapToSource(proxyIndex);
}

QPoint PageableTable::GetGlobalPosition(const QPoint &tablePosition) const {
    return _ui->tableView->viewport()->mapToGlobal(tablePosition);
}

void PageableTable::RemoveRow(const QModelIndex &index) const {
    _dataModel->removeRow(index.row(), index.parent());
}

QModelIndex PageableTable::GetSourceIndex(const QModelIndex &index) const {
    return _proxyModel->mapToSource(index);
}

QModelIndexList PageableTable::GetSelectedRows() const {
    return _ui->tableView->selectionModel()->selectedRows();
}

void PageableTable::SetMultiRowSelection(const bool enabled) const {
    if (!enabled) {
        _ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    } else {
        _ui->tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
}

void PageableTable::SaveSelection() {
    const QItemSelectionModel *selectionModel = _ui->tableView->selectionModel();
    for (QModelIndexList selectedRows = selectionModel->selectedRows(); const QModelIndex &index: selectedRows) {
        // Assume column 0 contains a unique ID
        _savedIds.insert(index.data(Qt::DisplayRole).toString());
    }
}

void PageableTable::RestoreSelection() {

    if (_savedIds.isEmpty()) {
        return;
    }
    _ui->tableView->selectionModel()->clearSelection(); // Clear existing
    for (int i = 0; i < _dataModel->rowCount(); ++i) {
        if (QString currentId = _dataModel->index(i, 0).data(Qt::DisplayRole).toString(); _savedIds.contains(currentId)) {
            // Select the row
            _ui->tableView->selectionModel()->select(
                _dataModel->index(i, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows
            );
        }
    }
    _savedIds.clear();
}

void PageableTable::SetMessageLabel(const QString &message) const {
    _ui->messageLabel->setAlignment(Qt::AlignCenter);

    QTimer::singleShot(3000, this, [this]() {
        _ui->messageLabel->setText(nullptr);
    });
    _ui->messageLabel->show();
    _ui->messageLabel->raise();
    _ui->messageLabel->setText(message);
}

void PageableTable::ShowDetails() {

    if (QModelIndexList selection = _ui->tableView->selectionModel()->selectedRows(); !selection.isEmpty()) {
        // Map proxy index → source index
        const QModelIndex proxyIndex = selection.first();
        const QModelIndex sourceIndex = _proxyModel->mapToSource(proxyIndex);
        emit ShowDetailsSignal(sourceIndex);
    }
}
