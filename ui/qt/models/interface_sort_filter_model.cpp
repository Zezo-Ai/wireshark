/* interface_sort_filter_model.cpp
 * Proxy model for the display of interface data for the interface tree
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ui/qt/models/interface_tree_model.h>
#include <ui/qt/models/interface_tree_cache_model.h>
#include <ui/qt/models/interface_sort_filter_model.h>

#include <epan/prefs.h>
#include <ui/preference_utils.h>
#include <ui/qt/utils/qt_ui_utils.h>

#include "main_application.h"

#include <QAbstractItemModel>

InterfaceSortFilterModel::InterfaceSortFilterModel(QObject *parent) :
        QSortFilterProxyModel(parent)
{
    resetAllFilter();
}

void InterfaceSortFilterModel::resetAllFilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
    _filterHidden = true;
    _filterTypes = true;
    _invertTypeFilter = false;
    _storeOnChange = false;
    _sortByActivity = false;
#ifdef HAVE_PCAP_REMOTE
    _remoteDisplay = true;
#endif

    /* Adding all columns, to have a default setting */
    for (int col = 0; col < IFTREE_COL_MAX; col++)
        _columns.append((InterfaceTreeColumns)col);

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange();
#else
    invalidateFilter();
#endif
    invalidate();
}

void InterfaceSortFilterModel::setStoreOnChange(bool storeOnChange)
{
    _storeOnChange = storeOnChange;
    if (storeOnChange)
    {
        connect(mainApp, &MainApplication::preferencesChanged, this, &InterfaceSortFilterModel::resetPreferenceData);
        resetPreferenceData();
    }
}

void InterfaceSortFilterModel::setFilterHidden(bool filter)
{
    _filterHidden = filter;

    invalidate();
}


void InterfaceSortFilterModel::setSortByActivity(bool sort)
{
    _sortByActivity = sort;
    invalidate();
}

bool InterfaceSortFilterModel::sortByActivity() const
{
    return _sortByActivity;
}

#ifdef HAVE_PCAP_REMOTE
void InterfaceSortFilterModel::setRemoteDisplay(bool remoteDisplay)
{
    _remoteDisplay = remoteDisplay;

    invalidate();
}

bool InterfaceSortFilterModel::remoteDisplay()
{
    return _remoteDisplay;
}

void InterfaceSortFilterModel::toggleRemoteDisplay()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
    _remoteDisplay = ! _remoteDisplay;

    if (_storeOnChange)
    {
        prefs.gui_interfaces_remote_display = ! _remoteDisplay;

        prefs_main_write();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    invalidate();
}

bool InterfaceSortFilterModel::remoteInterfacesExist()
{
    bool exist = false;

    if (sourceModel()->rowCount() == 0)
        return exist;

    for (int idx = 0; idx < sourceModel()->rowCount() && ! exist; idx++)
        exist = ((InterfaceTreeModel *)sourceModel())->isRemote(idx);

    return exist;
}
#endif

void InterfaceSortFilterModel::setFilterByType(bool filter, bool invert)
{
    _filterTypes = filter;
    _invertTypeFilter = invert;

    invalidate();
}

void InterfaceSortFilterModel::resetPreferenceData()
{
    displayHiddenTypes.clear();
    QString stored_prefs(prefs.gui_interfaces_hide_types);
    if (stored_prefs.length() > 0)
    {
        QStringList ifTypesStored = stored_prefs.split(',');
        QStringList::const_iterator it = ifTypesStored.constBegin();
        while (it != ifTypesStored.constEnd())
        {
            int i_val = (*it).toInt();
            if (! displayHiddenTypes.contains(i_val))
                displayHiddenTypes.append(i_val);
            ++it;
        }
    }

    _filterHidden = ! prefs.gui_interfaces_show_hidden;
#ifdef HAVE_PCAP_REMOTE
    _remoteDisplay = prefs.gui_interfaces_remote_display;
#endif

    invalidate();
}

bool InterfaceSortFilterModel::filterHidden() const
{
    return _filterHidden;
}

void InterfaceSortFilterModel::toggleFilterHidden()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
    _filterHidden = ! _filterHidden;

    if (_storeOnChange)
    {
        prefs.gui_interfaces_show_hidden = ! _filterHidden;

        prefs_main_write();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    invalidate();
}

bool InterfaceSortFilterModel::filterByType() const
{
    return _filterTypes;
}

int InterfaceSortFilterModel::interfacesHidden()
{
#ifdef HAVE_LIBPCAP
    if (! global_capture_opts.all_ifaces)
        return 0;
#endif

    return sourceModel()->rowCount() - rowCount();
}

QList<int> InterfaceSortFilterModel::typesDisplayed()
{
    QList<int> shownTypes;

    if (sourceModel()->rowCount() == 0)
        return shownTypes;

    for (int idx = 0; idx < sourceModel()->rowCount(); idx++)
    {
        int type = ((InterfaceTreeModel *)sourceModel())->getColumnContent(idx, IFTREE_COL_TYPE).toInt();
        bool hidden = ((InterfaceTreeModel *)sourceModel())->getColumnContent(idx, IFTREE_COL_HIDDEN).toBool();

        if (! hidden)
        {
            if (! shownTypes.contains(type))
                shownTypes.append(type);
        }
    }

    return shownTypes;
}

void InterfaceSortFilterModel::setInterfaceTypeVisible(int ifType, bool visible)
{
    if (visible && displayHiddenTypes.contains(ifType))
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
#endif
        displayHiddenTypes.removeAll(ifType);
    }
    else if (! visible && ! displayHiddenTypes.contains(ifType))
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
#endif
        displayHiddenTypes.append(ifType);
    }
    else
        /* Nothing should have changed */
        return;

    if (_storeOnChange)
    {
        QString new_pref;
        QList<int>::const_iterator it = displayHiddenTypes.constBegin();
        while (it != displayHiddenTypes.constEnd())
        {
            new_pref.append(QStringLiteral("%1,").arg(*it));
            ++it;
        }
        if (new_pref.length() > 0)
            new_pref = new_pref.left(new_pref.length() - 1);

        prefs.gui_interfaces_hide_types = qstring_strdup(new_pref);

        prefs_main_write();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    invalidate();
}

void InterfaceSortFilterModel::toggleTypeVisibility(int ifType)
{
    bool checked = isInterfaceTypeShown(ifType);

    setInterfaceTypeVisible(ifType, checked ? false : true);
}

bool InterfaceSortFilterModel::isInterfaceTypeShown(int ifType) const
{
    bool result = false;

    if (displayHiddenTypes.size() == 0)
        result = true;
    else if (! displayHiddenTypes.contains(ifType))
        result = true;

    return ((_invertTypeFilter && ! result) || (! _invertTypeFilter && result) );
}

bool InterfaceSortFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex & sourceParent) const
{
    QModelIndex realIndex = sourceModel()->index(sourceRow, 0, sourceParent);

    if (! realIndex.isValid())
        return false;

#ifdef HAVE_LIBPCAP
    int idx = realIndex.row();

    /* No data loaded, we do not display anything */
    if (sourceModel()->rowCount() == 0)
        return false;

    int type = -1;
    bool hidden = false;

    InterfaceTreeCacheModel* cacheModel = qobject_cast<InterfaceTreeCacheModel*>(sourceModel());
    InterfaceTreeModel* treeModel = nullptr;

    if (cacheModel != nullptr)
    {
        type = cacheModel->getColumnContent(idx, IFTREE_COL_TYPE).toInt();
        hidden = cacheModel->getColumnContent(idx, IFTREE_COL_HIDDEN, Qt::UserRole).toBool();
    }
    else if ((treeModel = qobject_cast<InterfaceTreeModel*>(sourceModel())) != nullptr)
    {
        type = treeModel->getColumnContent(idx, IFTREE_COL_TYPE).toInt();
        hidden = treeModel->getColumnContent(idx, IFTREE_COL_HIDDEN, Qt::UserRole).toBool();
    }
    else
        return false;

    if (hidden && _filterHidden)
        return false;

#ifdef HAVE_PCAP_REMOTE
    bool isRemote = false;
    if (cacheModel && cacheModel->isRemote(realIndex)) {
        isRemote = true;
    } else if (treeModel && treeModel->isRemote(idx)) {
        isRemote = true;
    }
#endif

    if (_filterTypes && ! isInterfaceTypeShown(type))
    {
#ifdef HAVE_PCAP_REMOTE
        /* Remote interfaces have the if type IF_WIRED, therefore would be filtered if not explicitly checked here */
        if (type != IF_WIRED || !isRemote)
#endif
        return false;
    }

#ifdef HAVE_PCAP_REMOTE
    if (isRemote && !_remoteDisplay) {
            return false;
    }
#endif

#endif /* HAVE_LIBPCAP */

    return true;
}

bool InterfaceSortFilterModel::filterAcceptsColumn(int sourceColumn, const QModelIndex & sourceParent) const
{
    QModelIndex realIndex = sourceModel()->index(0, sourceColumn, sourceParent);

    if (! realIndex.isValid())
        return false;

    if (! _columns.contains((InterfaceTreeColumns)sourceColumn))
        return false;

    return true;
}

void InterfaceSortFilterModel::setColumns(QList<InterfaceTreeColumns> columns)
{
    _columns.clear();
    _columns.append(columns);
}

int InterfaceSortFilterModel::mapSourceToColumn(InterfaceTreeColumns mdlIndex)
{
    if (! _columns.contains(mdlIndex))
        return -1;

    return static_cast<int>(_columns.indexOf(mdlIndex, 0));
}

QModelIndex InterfaceSortFilterModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (! proxyIndex.isValid())
        return QModelIndex();

    if (! sourceModel())
        return QModelIndex();

    QModelIndex baseIndex = QSortFilterProxyModel::mapToSource(proxyIndex);
    QModelIndex newIndex = sourceModel()->index(baseIndex.row(), _columns.at(proxyIndex.column()));

    return newIndex;
}

QModelIndex InterfaceSortFilterModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (! sourceIndex.isValid())
        return QModelIndex();
    else if (! _columns.contains((InterfaceTreeColumns) sourceIndex.column()) )
        return QModelIndex();

    QModelIndex newIndex = QSortFilterProxyModel::mapFromSource(sourceIndex);

    return index(newIndex.row(), static_cast<int>(_columns.indexOf((InterfaceTreeColumns) sourceIndex.column())));
}

QString InterfaceSortFilterModel::interfaceError()
{
    QString result;

    InterfaceTreeModel * sourceModel = dynamic_cast<InterfaceTreeModel *>(this->sourceModel());
    if (sourceModel != NULL)
        result = sourceModel->interfaceError();

    if (result.size() == 0 && rowCount() == 0)
        result = tr("No interfaces to be displayed. %1 interfaces hidden.").arg(interfacesHidden());

    return result;
}

bool InterfaceSortFilterModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
    bool leftActive = source_left.sibling(source_left.row(), InterfaceTreeColumns::IFTREE_COL_ACTIVE).data(Qt::UserRole).toBool();
    bool rightActive = source_right.sibling(source_right.row(), InterfaceTreeColumns::IFTREE_COL_ACTIVE).data(Qt::UserRole).toBool();

    if (_sortByActivity && rightActive && ! leftActive)
        return true;

    return QSortFilterProxyModel::lessThan(source_left, source_right);
}

