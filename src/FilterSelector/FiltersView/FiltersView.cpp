/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FiltersView.cpp
 *
 *  Copyright 2017 Sebastien Fourey
 *
 *  This file is part of G'MIC-Qt, a generic plug-in for raster graphics
 *  editors, offering hundreds of filters thanks to the underlying G'MIC
 *  image processing framework.
 *
 *  gmic_qt is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  gmic_qt is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gmic_qt.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "FilterSelector/FiltersView/FiltersView.h"
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QSettings>
#include <QStandardItem>
#include <QStringList>
#include "Common.h"
#include "FilterSelector/FiltersView/FilterTreeFolder.h"
#include "FilterSelector/FiltersView/FilterTreeItem.h"
#include "FilterSelector/FiltersView/FilterTreeItemDelegate.h"
#include "FilterSelector/FiltersView/FilterTreeNullItem.h"
#include "FilterSelector/FiltersVisibilityMap.h"
#include "Globals.h"
#include "Utils.h"
#include "ui_filtersview.h"

const QString FiltersView::FilterTreePathSeparator("\t");

// TODO : Handler subfolder everywhere

FiltersView::FiltersView(QWidget * parent) : QWidget(parent), ui(new Ui::FiltersView), _isInSelectionMode(false)
{
  ui->setupUi(this);
  ui->treeView->setModel(&_emptyModel);
  _faveFolder = nullptr;
  _cachedFolder = _model.invisibleRootItem();
  _itemEditionDelegate = new FilterTreeItemDelegate(ui->treeView);
  ui->treeView->setItemDelegate(_itemEditionDelegate);
  ui->treeView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
  ui->treeView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

  connect(_itemEditionDelegate, SIGNAL(commitData(QWidget *)), this, SLOT(onRenameFinished(QWidget *)));
  connect(ui->treeView, SIGNAL(returnKeyPressed()), this, SLOT(onReturnKeyPressedInFiltersTree()));
  connect(ui->treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(onItemClicked(QModelIndex)));
  connect(&_model, SIGNAL(itemChanged(QStandardItem *)), this, SLOT(onItemChanged(QStandardItem *)));

  ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->treeView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCustomContextMenu(QPoint)));

  _faveContextMenu = new QMenu(this);
  QAction * action;
  action = _faveContextMenu->addAction(tr("Rename fave"));
  connect(action, SIGNAL(triggered(bool)), this, SLOT(onContextMenuRenameFave()));
  action = _faveContextMenu->addAction(tr("Remove fave"));
  connect(action, SIGNAL(triggered(bool)), this, SLOT(onContextMenuRemoveFave()));
  action = _faveContextMenu->addAction(tr("Clone fave"));
  connect(action, SIGNAL(triggered(bool)), this, SLOT(onContextMenuAddFave()));

  _faveSubFolderContextMenu = new QMenu(this);
  _createFaveSubFolderAction = _faveSubFolderContextMenu->addAction(tr("Create subfolder"));
  connect(_createFaveSubFolderAction, SIGNAL(triggered(bool)), this, SLOT(onContextMenuCreateFaveSubfolder()));
  _renameFaveSubFolderAction = _faveSubFolderContextMenu->addAction(tr("Rename folder"));
  connect(_renameFaveSubFolderAction, SIGNAL(triggered(bool)), this, SLOT(onContextMenuRenameFaveSubfolder()));

  _filterContextMenu = new QMenu(this);
  action = _filterContextMenu->addAction(tr("Add fave"));
  connect(action, SIGNAL(triggered(bool)), this, SLOT(onContextMenuAddFave()));

  ui->treeView->installEventFilter(this);

  // TODO : Allow rename folder name in this view
}

FiltersView::~FiltersView()
{
  delete ui;
}

void FiltersView::enableModel()
{
  if (_isInSelectionMode) {
    uncheckFullyUncheckedFolders();
    _model.setHorizontalHeaderItem(1, new QStandardItem(QObject::tr("Visible")));
    _model.setColumnCount(2);
  }
  ui->treeView->setModel(&_model);
  if (_isInSelectionMode) {
    QStandardItem * headerItem = _model.horizontalHeaderItem(1);
    QString title = QString("_%1_").arg(headerItem->text());
    QFont font;
    QFontMetrics fm(font);
#if QT_VERSION_GTE(5, 11)
    int w = fm.horizontalAdvance(title);
#else
    int w = fm.width(title);
#endif
    ui->treeView->setColumnWidth(0, ui->treeView->width() - 2 * w);
    ui->treeView->setColumnWidth(1, w);
  }
}

void FiltersView::disableModel()
{
  ui->treeView->setModel(&_emptyModel);
}

void FiltersView::createFolder(const QList<QString> & path)
{
  createFolder(_model.invisibleRootItem(), path);
}

FilterTreeFolder * FiltersView::createFaveSubfolder(const QList<QString> & path)
{
  if (!_faveFolder) {
    createFaveFolder();
  }
  QStandardItem * item = createFolder(_faveFolder, path);
  auto folder = dynamic_cast<FilterTreeFolder *>(item);
  folder->setEditable(true);
  if (folder) {
    updateNullItemInFaveSubfolder(folder);
    sortFaves();
  }
  return folder;
}

void FiltersView::addFilter(const QString & text, const QString & hash, const QList<QString> & path, bool warning)
{
  const bool filterIsVisible = FiltersVisibilityMap::filterIsVisible(hash);
  if (!_isInSelectionMode && !filterIsVisible) {
    return;
  }
  QStandardItem * folder = getFolderFromPath(path);
  if (!folder) {
    folder = createFolder(_model.invisibleRootItem(), path);
  }
  auto item = new FilterTreeItem(text);
  item->setHash(hash);
  item->setWarningFlag(warning);
  if (_isInSelectionMode) {
    addStandardItemWithCheckbox(folder, item);
    item->setVisibility(filterIsVisible);
  } else {
    folder->appendRow(item);
  }
}

void FiltersView::addFave(const QString & text, const QString & hash, const QList<QString> & path)
{
  const bool faveIsVisible = FiltersVisibilityMap::filterIsVisible(hash);
  if (!_isInSelectionMode && !faveIsVisible) {
    return;
  }
  if (!_faveFolder) {
    createFaveFolder();
  }
  FilterTreeFolder * folder = getFaveSubfolderFromPath(path);
  if (!folder) {
    folder = createFaveSubfolder(path);
    auto parentFolder = dynamic_cast<FilterTreeFolder *>(folder->parent());
    updateNullItemInFaveSubfolder(parentFolder);
  }

  // Empty text is for dummy Fave to keep trace of empty folders
  // TODO : Handle selection mode and visibility
  if (text.isEmpty()) {
    updateNullItemInFaveSubfolder(folder);
    return;
  }

  auto item = new FilterTreeItem(text);
  item->setHash(hash);
  item->setWarningFlag(false);
  item->setFaveFlag(true);
  if (_isInSelectionMode) {
    addStandardItemWithCheckbox(folder, item);
    item->setVisibility(faveIsVisible);
  } else {
    folder->appendRow(item);
  }
  updateNullItemInFaveSubfolder(folder);
}

void FiltersView::selectFave(const QString & hash)
{
  // Select the fave if the model is enabled
  if (ui->treeView->model() == &_model) {
    FilterTreeItem * fave = findFave(hash);
    if (fave) {
      // TODO : NOW Expand folders
      ui->treeView->setCurrentIndex(fave->index());
      ui->treeView->scrollTo(fave->index(), QAbstractItemView::PositionAtCenter);
    }
  }
}

void FiltersView::selectActualFilter(const QString & hash, const QList<QString> & path)
{
  QStandardItem * folder = getFolderFromPath(path);
  if (folder) {
    for (int row = 0; row < folder->rowCount(); ++row) {
      auto filter = dynamic_cast<FilterTreeItem *>(folder->child(row));
      if (filter && (filter->hash() == hash)) {
        ui->treeView->setCurrentIndex(filter->index());
        ui->treeView->scrollTo(filter->index(), QAbstractItemView::PositionAtCenter);
        return;
      }
    }
  }
}

void FiltersView::removeFave(const QString & hash)
{
  FilterTreeItem * fave = findFave(hash);
  if (fave) {
    FilterTreeFolder * parentFolder = dynamic_cast<FilterTreeFolder *>(fave->parent());
    _model.removeRow(fave->row(), fave->index().parent());
    updateNullItemInFaveSubfolder(parentFolder);
    if (_faveFolder->rowCount() == 0) {
      removeFaveFolder();
    }
  }
}

void FiltersView::clear()
{
  removeFaveFolder();
  _model.invisibleRootItem()->removeRows(0, _model.invisibleRootItem()->rowCount());
  _model.setColumnCount(1);
  _cachedFolder = _model.invisibleRootItem();
  _cachedFolderPath.clear();
}

void FiltersView::sort()
{
  _model.invisibleRootItem()->sortChildren(0);
}

void FiltersView::sortFaves()
{
  if (_faveFolder) {
    _faveFolder->sortChildren(0);
  }
}

void FiltersView::updateFaveItem(const QString & currentHash, const QString & newHash, const QString & newName)
{
  FilterTreeItem * item = findFave(currentHash);
  if (!item) {
    return;
  }
  item->setText(newName);
  item->setHash(newHash);
}

void FiltersView::setHeader(const QString & header)
{
  _model.setHorizontalHeaderItem(0, new QStandardItem(header));
}

FilterTreeItem * FiltersView::selectedItem() const
{
  QModelIndex index = ui->treeView->currentIndex();
  return filterTreeItemFromIndex(index);
}

FilterTreeFolder * FiltersView::selectedFolder() const
{
  QModelIndex index = ui->treeView->currentIndex();
  return filterTreeFolderFromIndex(index);
}

QStandardItem * FiltersView::filterTreeStandardItemFromIndex(QModelIndex index) const
{
  // Get standard item even if it is the checkbox which is actually selected
  if (!index.isValid()) {
    return nullptr;
  }
  QStandardItem * item = _model.itemFromIndex(index);
  if (item) {
    int row = index.row();
    QStandardItem * parentFolder = item->parent();
    // parent is 0 for top level items
    if (!parentFolder) {
      parentFolder = _model.invisibleRootItem();
    }
    QStandardItem * leftItem = parentFolder->child(row, 0);
    if (leftItem) {
      return leftItem;
    }
  }
  return nullptr;
}

FilterTreeItem * FiltersView::filterTreeItemFromIndex(QModelIndex index) const
{
  QStandardItem * item = filterTreeStandardItemFromIndex(index);
  if (item) {
    return dynamic_cast<FilterTreeItem *>(item);
  }
  return nullptr;
}

FilterTreeFolder * FiltersView::filterTreeFolderFromIndex(QModelIndex index) const
{
  QStandardItem * item = filterTreeStandardItemFromIndex(index);
  if (item) {
    return dynamic_cast<FilterTreeFolder *>(item);
  }
  return nullptr;
}

QString FiltersView::selectedFilterHash() const
{
  FilterTreeItem * item = selectedItem();
  return item ? item->hash() : QString();
}

bool FiltersView::aFaveIsSelected() const
{
  FilterTreeItem * item = selectedItem();
  return item && item->isFave();
}

void FiltersView::preserveExpandedFolders()
{
  if (ui->treeView->model() == &_emptyModel) {
    return;
  }
  _expandedFolderPaths.clear();
  preserveExpandedFolders(_model.invisibleRootItem(), _expandedFolderPaths);
}

void FiltersView::restoreExpandedFolders()
{
  expandFolders(_expandedFolderPaths);
}

void FiltersView::loadSettings(const QSettings &)
{
  FiltersVisibilityMap::load();
}

void FiltersView::saveSettings(QSettings & settings)
{
  if (_isInSelectionMode) {
    saveFiltersVisibility(_model.invisibleRootItem());
  }
  preserveExpandedFolders();
  settings.setValue("Config/ExpandedFolders", QStringList(_expandedFolderPaths));
  FiltersVisibilityMap::save();
}

void FiltersView::enableSelectionMode()
{
  _isInSelectionMode = true;
}

void FiltersView::disableSelectionMode()
{
  _model.setHorizontalHeaderItem(1, nullptr);
  _isInSelectionMode = false;
  saveFiltersVisibility(_model.invisibleRootItem());
}

void FiltersView::uncheckFullyUncheckedFolders()
{
  uncheckFullyUncheckedFolders(_model.invisibleRootItem());
}

void FiltersView::adjustTreeSize()
{
  ui->treeView->adjustSize();
}

void FiltersView::expandFolders(QList<QString> & folderPaths)
{
  expandFolders(folderPaths, _model.invisibleRootItem());
}

bool FiltersView::eventFilter(QObject * watched, QEvent * event)
{
  if (watched != ui->treeView) {
    return QObject::eventFilter(watched, event);
  }
  if (event->type() == QEvent::KeyPress) {
    auto keyEvent = dynamic_cast<QKeyEvent *>(event);
    if (keyEvent && (keyEvent->key() == Qt::Key_Delete)) {
      FilterTreeItem * item = selectedItem();
      if (item && item->isFave()) {
        QMessageBox::StandardButton button;
        button = QMessageBox::question(this, tr("Remove fave"), QString(tr("Do you really want to remove the following fave?\n\n%1\n")).arg(item->text()));
        if (button == QMessageBox::Yes) {
          emit faveRemovalRequested(item->hash());
          return true;
        }
      }
    }
  }
  return QObject::eventFilter(watched, event);
}

void FiltersView::expandFolders(const QList<QString> & folderPaths, QStandardItem * folder)
{
  int rows = folder->rowCount();
  for (int row = 0; row < rows; ++row) {
    auto * subFolder = dynamic_cast<FilterTreeFolder *>(folder->child(row));
    if (subFolder) {
      if (folderPaths.contains(subFolder->path().join(FilterTreePathSeparator))) {
        ui->treeView->expand(subFolder->index());
      } else {
        ui->treeView->collapse(subFolder->index());
      }
      expandFolders(folderPaths, subFolder);
    }
  }
}

void FiltersView::editSelectedFaveName()
{
  FilterTreeItem * item = selectedItem();
  if (item && item->isFave()) {
    ui->treeView->edit(item->index());
  }
}

void FiltersView::expandAll()
{
  ui->treeView->expandAll();
}

void FiltersView::collapseAll()
{
  ui->treeView->collapseAll();
}

void FiltersView::expandFaveFolder()
{
  if (_faveFolder) {
    ui->treeView->expand(_faveFolder->index());
  }
}

void FiltersView::onCustomContextMenu(const QPoint & point)
{
  QModelIndex index = ui->treeView->indexAt(point);
  if (!index.isValid()) {
    return;
  }
  FilterTreeItem * item = filterTreeItemFromIndex(index);
  if (item) {
    onItemClicked(index);
    if (item->isFave()) {
      _faveContextMenu->exec(ui->treeView->mapToGlobal(point));
    } else {
      _filterContextMenu->exec(ui->treeView->mapToGlobal(point));
    }
  }
  FilterTreeFolder * folder = filterTreeFolderFromIndex(index);
  if (folder) {
    if (folder->isFaveFolder()) {
      _createFaveSubFolderAction->setData(_faveFolder->index());
      _renameFaveSubFolderAction->setData(_faveFolder->index());
      _faveSubFolderContextMenu->exec(ui->treeView->mapToGlobal(point));
    } else if (folder->isFaveSubFolder()) {
      _createFaveSubFolderAction->setData(folder->index());
      _renameFaveSubFolderAction->setData(folder->index());
      _faveSubFolderContextMenu->exec(ui->treeView->mapToGlobal(point));
    }
  }
}

void FiltersView::onRenameFinished(QWidget * editor)
{
  auto lineEdit = dynamic_cast<QLineEdit *>(editor);
  Q_ASSERT_X(lineEdit, "Rename Fave", "Editor is not a QLineEdit!");
  FilterTreeItem * item = selectedItem();
  if (item) {
    emit faveRenamed(item->hash(), lineEdit->text());
    return;
  }
  FilterTreeFolder * folder = selectedFolder();
  if (folder) {
    QStringList path = folder->path();
    path.pop_front();
    QString newName = path.back();
    path.pop_back();
    path.push_back(_itemEditionDelegate->textBeforeEditing());
    QString pathStr = path.join(FAVE_PATH_SEPATATOR);
    emit faveSubfolderRenamed(pathStr, newName);
    return;
  }
}

void FiltersView::onReturnKeyPressedInFiltersTree()
{
  FilterTreeItem * item = selectedItem();
  if (item) {
    emit filterSelected(item->hash());
  } else {
    QModelIndex index = ui->treeView->currentIndex();
    QStandardItem * item = _model.itemFromIndex(index);
    FilterTreeFolder * folder = item ? dynamic_cast<FilterTreeFolder *>(item) : nullptr;
    if (folder) {
      if (ui->treeView->isExpanded(index)) {
        ui->treeView->collapse(index);
      } else {
        ui->treeView->expand(index);
      }
    }
    emit filterSelected(QString());
  }
}

void FiltersView::onItemClicked(QModelIndex index)
{
  FilterTreeItem * item = filterTreeItemFromIndex(index);
  if (item) {
    emit filterSelected(item->hash());
  } else {
    emit filterSelected(QString());
  }
}

void FiltersView::onItemChanged(QStandardItem * item)
{
  if (!item->isCheckable()) {
    return;
  }
  int row = item->index().row();
  QStandardItem * parentFolder = item->parent();
  if (!parentFolder) {
    // parent is 0 for top level items
    parentFolder = _model.invisibleRootItem();
  }
  QStandardItem * leftItem = parentFolder->child(row);
  auto folder = dynamic_cast<FilterTreeFolder *>(leftItem);
  if (folder) {
    folder->applyVisibilityStatusToFolderContents();
  }
  // Force an update of the view by triggering a call of
  // QStandardItem::emitDataChanged()
  leftItem->setData(leftItem->data());
}

void FiltersView::onContextMenuRemoveFave()
{
  emit faveRemovalRequested(selectedFilterHash());
}

void FiltersView::onContextMenuRenameFave()
{
  editSelectedFaveName();
}

void FiltersView::onContextMenuAddFave()
{
  emit faveAdditionRequested(selectedFilterHash());
}

void FiltersView::onContextMenuCreateFaveSubfolder()
{
  QModelIndex index = _createFaveSubFolderAction->data().toModelIndex();
  QStandardItem * item = filterTreeStandardItemFromIndex(index);
  FilterTreeFolder * folder = item ? dynamic_cast<FilterTreeFolder *>(item) : nullptr;
  QStringList existingNames;
  for (int i = 0; i < folder->rowCount(); ++i) {
    if (dynamic_cast<FilterTreeFolder *>(folder->child(i))) {
      existingNames.push_back(folder->child(i)->text());
    }
  }
  QString name = FAVE_NEW_FOLDER_TEXT;
  GmicQt::makeUniqueName(name, existingNames);
  QList<QString> path;
  if (folder == _faveFolder) {
    TSHOW("Create Root Fave Folder subfolder");
    path << name;
  } else if (folder && folder->isFaveSubFolder()) {
    TSHOW("Create Fave Folder subfolder");
    path = folder->path();
    path.pop_front();
    path.append(name);
  }
  TSHOW(path);
  if (!path.isEmpty()) {
    emit faveSubfolderCreationRequested(path.join(FAVE_PATH_SEPATATOR));
  }
}

void FiltersView::onContextMenuRenameFaveSubfolder()
{
  QModelIndex index = _renameFaveSubFolderAction->data().toModelIndex();
  QStandardItem * item = filterTreeStandardItemFromIndex(index);
  FilterTreeFolder * folder = item ? dynamic_cast<FilterTreeFolder *>(item) : nullptr;
  TSHOW(folder);
  if (folder) {
    ui->treeView->edit(index);
  }
}

void FiltersView::uncheckFullyUncheckedFolders(QStandardItem * folder)
{
  int rows = folder->rowCount();
  for (int row = 0; row < rows; ++row) {
    auto subFolder = dynamic_cast<FilterTreeFolder *>(folder->child(row));
    if (subFolder) {
      uncheckFullyUncheckedFolders(subFolder);
      if (subFolder->isFullyUnchecked()) {
        subFolder->setVisibility(false);
      }
    }
  }
}

void FiltersView::preserveExpandedFolders(QStandardItem * folder, QList<QString> & list)
{
  if (!folder) {
    return;
  }
  int rows = folder->rowCount();
  for (int row = 0; row < rows; ++row) {
    auto subFolder = dynamic_cast<FilterTreeFolder *>(folder->child(row));
    if (subFolder) {
      if (ui->treeView->isExpanded(subFolder->index())) {
        list.push_back(subFolder->path().join(FilterTreePathSeparator));
      }
      preserveExpandedFolders(subFolder, list);
    }
  }
}

void FiltersView::createFaveFolder()
{
  if (_faveFolder) {
    return;
  }
  _faveFolder = new FilterTreeFolder(tr(FAVE_FOLDER_TEXT));
  _faveFolder->setFaveFolderFlag(true);
  _model.invisibleRootItem()->appendRow(_faveFolder);
  _model.invisibleRootItem()->sortChildren(0);
}

void FiltersView::removeFaveFolder()
{
  if (!_faveFolder) {
    return;
  }
  _model.invisibleRootItem()->removeRow(_faveFolder->row());
  _faveFolder = nullptr;
}

void FiltersView::addStandardItemWithCheckbox(QStandardItem * folder, FilterTreeAbstractItem * item)
{
  QList<QStandardItem *> items;
  items.push_back(item);
  auto checkBox = new QStandardItem;
  checkBox->setCheckable(true);
  checkBox->setEditable(false);
  item->setVisibilityItem(checkBox);
  items.push_back(checkBox);
  folder->appendRow(items);
}

QStandardItem * FiltersView::getFolderFromPath(const QList<QString> & path)
{
  if (path == _cachedFolderPath) {
    return _cachedFolder;
  }
  _cachedFolder = getFolderFromPath(_model.invisibleRootItem(), path);
  _cachedFolderPath = path;
  return _cachedFolder;
}

FilterTreeFolder * FiltersView::getFaveSubfolderFromPath(const QList<QString> & path)
{
  if (!_faveFolder) {
    return nullptr;
  }
  if (path.isEmpty()) {
    return _faveFolder;
  }
  return dynamic_cast<FilterTreeFolder *>(getFolderFromPath(_faveFolder, path));
}

QStandardItem * FiltersView::createFolder(QStandardItem * parent, QList<QString> path)
{
  Q_ASSERT_X(parent, "FiltersView", "Create folder path in null parent");
  if (path.isEmpty()) {
    return parent;
  }

  // Look for already existing base folder in parent
  for (int row = 0; row < parent->rowCount(); ++row) {
    auto folder = dynamic_cast<FilterTreeFolder *>(parent->child(row));
    if (folder && (folder->text() == FilterTreeAbstractItem::removeWarningPrefix(path.front()))) {
      path.pop_front();
      return createFolder(folder, path);
    }
  }
  // Folder does not exist, we create it
  auto folder = new FilterTreeFolder(path.front());
  path.pop_front();
  if (_isInSelectionMode) {
    addStandardItemWithCheckbox(parent, folder);
    folder->setVisibility(true);
  } else {
    parent->appendRow(folder);
  }
  return createFolder(folder, path);
}

QStandardItem * FiltersView::getFolderFromPath(QStandardItem * parent, QList<QString> path)
{
  Q_ASSERT_X(parent, "FiltersView", "Get folder path from null parent");
  if (path.isEmpty()) {
    return parent;
  }
  for (int row = 0; row < parent->rowCount(); ++row) {
    auto folder = dynamic_cast<FilterTreeFolder *>(parent->child(row));
    if (folder && (folder->text() == FilterTreeAbstractItem::removeWarningPrefix(path.front()))) {
      path.pop_front();
      return getFolderFromPath(folder, path);
    }
  }
  return nullptr;
}

void FiltersView::saveFiltersVisibility(QStandardItem * item)
{
  auto filterItem = dynamic_cast<FilterTreeItem *>(item);
  if (filterItem) {
    FiltersVisibilityMap::setVisibility(filterItem->hash(), filterItem->isVisible());
    return;
  }
  int rows = item->rowCount();
  for (int row = 0; row < rows; ++row) {
    saveFiltersVisibility(item->child(row));
  }
}

FilterTreeItem * FiltersView::findFave(const QString & hash, FilterTreeFolder * folder) const
{
  if (!folder) {
    return nullptr;
  }
  int rows = folder->rowCount();
  for (int row = 0; row < rows; ++row) {
    auto item = dynamic_cast<FilterTreeItem *>(folder->child(row));
    if (item && (item->hash() == hash)) {
      return item;
    }
    auto subfolder = dynamic_cast<FilterTreeFolder *>(folder->child(row));
    if (subfolder) {
      FilterTreeItem * item = findFave(hash, subfolder);
      if (item) {
        return item;
      }
    }
  }
  return nullptr;
}

FilterTreeItem * FiltersView::findFave(const QString & hash) const
{
  return findFave(hash, _faveFolder);
}

void FiltersView::updateNullItemInFaveSubfolder(FilterTreeFolder * folder)
{
  if (!folder) {
    return;
  }
  if (folder->rowCount() == 0) {
    folder->appendRow(new FilterTreeNullItem());
    return;
  }
  if ((folder->rowCount() > 1) && dynamic_cast<FilterTreeNullItem *>(folder->child(0))) {
    folder->removeRow(0);
    return;
  }
}
