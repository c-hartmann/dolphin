/* This file is part of the KDE project
   Copyright (C) 2005 Daniel Teske <teske@squorn.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "bookmarklistview.h"
#include "bookmarkmodel.h"
#include "toplevel.h"
#include "settings.h"
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QContextMenuEvent>
#include <QBrush>
#include <QPalette>
#include <QItemSelectionModel>

#include <kdebug.h>

BookmarkListView::BookmarkListView( QWidget * parent )
    :QTreeView( parent )
{
    dirtyGetSelectionAbilies = true;
}

BookmarkListView::~BookmarkListView()
{
    saveColumnSetting();
}

int BookmarkListView::min(int a, int b)
{
    return a < b? a : b;
}

int BookmarkListView::max(int a, int b)
{
    return a > b? a : b;
}

QRect BookmarkListView::merge(QRect a, QRect b)
{
    if(a.isNull())
        return b;
    if(b.isNull())
        return a;
    a.normalized();
    b.normalized();
    int left = min(a.left(), b.left());
    int top = min(a.top(), b.top());
    int width = max(a.right(), b.right()) - left + 1;
    int height = max(a.bottom(), b.bottom()) - top + 1;
    return QRect(left, top, width, height);
}

QRect BookmarkListView::rectForRow(QModelIndex index)
{
    QModelIndex parent = index.parent();
    int row = index.row();
    int columnCount = model()->columnCount(parent);

    QRect result;
    for(int i = 0; i<columnCount; ++i)
        result = merge( visualRect( parent.child(row, i) ), result);
    return result;
}

QRect BookmarkListView::rectForRowWithChildren(QModelIndex index)
{
    QRect rect = rectForRow(index);
    int rowCount = model()->rowCount(index);
    for(int i=0; i<rowCount; ++i)
        rect = merge(rect, rectForRowWithChildren( index.child(i, 0) ));
    return rect;
}

void BookmarkListView::deselectChildren( const QModelIndex & parent)
{
    int rowCount = model()->rowCount(parent);
    if(rowCount)
    {
        QItemSelection deselect;
        deselect.select( parent.child(0,0), parent.child(rowCount-1, model()->columnCount(parent)-1));
        selectionModel()->select(deselect, QItemSelectionModel::Deselect);
        
        for(int i=0; i<rowCount; ++i)
            deselectChildren(parent.child(i, 0));
    }
}

//FIXME check scalability of this code
void BookmarkListView::selectionChanged ( const QItemSelection & selected, const QItemSelection & deselected )
{
    kdDebug()<<"Selection changed "<<endl;
    dirtyGetSelectionAbilies = true;
    QTreeView::selectionChanged( selected, deselected );

    // deselect indexes which shouldn't have been selected
    QItemSelection deselectAgain; // selections which need to be undone
    const QModelIndexList & list = selected.indexes();
    QModelIndexList::const_iterator it, end;
    end = list.constEnd();
    for(it = list.constBegin(); it != end; ++it)
    {
        if( (*it).column() != 0)
            continue;
        if(parentSelected( *it ))
            deselectAgain.select( (*it), (*it).parent().child( (*it).row(), model()->columnCount() -1)  );
    }
    selectionModel()->select( deselectAgain, QItemSelectionModel::Deselect);

    //deselect children of selected items
    for(it = list.constBegin(); it != end; ++it)
    {
        if( (*it).column() !=0)
            continue;
        deselectChildren(*it);
    }

    // ensure that drawRow is called for all children
    const QModelIndexList & sellist = selected.indexes();
    end = sellist.constEnd();
    QRect rect;
    for(it = sellist.constBegin(); it != end; ++it)
    {
        if((*it).column() != 0)
            continue;
        if( static_cast<TreeItem *>((*it).internalPointer())->bookmark().address() == "") //FIXME
            continue;
        rect = merge(rect, rectForRowWithChildren(*it));
    }
    const QModelIndexList & desellist = deselected.indexes();
    end = desellist.constEnd();
    for(it = desellist.constBegin(); it != end; ++it)
    {
        if((*it).column() != 0)
            continue;
        if( static_cast<TreeItem *>((*it).internalPointer())->bookmark().address() == "") //FIXME
            continue;
        rect = merge(rect, rectForRowWithChildren(*it));
    }
    rect.setLeft(0);
    viewport()->update(rect);
}

QItemSelectionModel::SelectionFlags BookmarkListView::selectionCommand ( const QModelIndex & index, const QEvent * event ) const
{
    const QMouseEvent * qme = dynamic_cast<const QMouseEvent *>(event);
    if(qme && (qme->button() == Qt::RightButton ) && parentSelected(index)) //right click on a parentSelected index
        return QItemSelectionModel::NoUpdate; // don't modify selection, only show a context menu
    else
        return QTreeView::selectionCommand( index, event );
}

void BookmarkListView::contextMenuEvent ( QContextMenuEvent * e )
{
    QModelIndex index = indexAt(e->pos());
    KBookmark bk;
    if(index.isValid())
        bk = static_cast<TreeItem *>(index.internalPointer())->bookmark();

    QMenu* popup;
    if( !index.isValid()
       || (bk.address() == CurrentMgr::self()->root().address())
       || (bk.isGroup())) //FIXME add empty folder padder
    {
        popup = KEBApp::self()->popupMenuFactory("popup_folder");
    }
    else
    {
        popup = KEBApp::self()->popupMenuFactory("popup_bookmark");
    }
    if (popup)
        popup->popup(e->globalPos());
}

void BookmarkListView::drawRow ( QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index ) const
{
    QStyleOptionViewItem opt = option;
    if(parentSelected(index))
    {

        int base_h, base_s, base_v;
        opt.palette.base().color().getHsv(&base_h, &base_s, &base_v);

        int hilite_h, hilite_s, hilite_v;
        opt.palette.highlight().color().getHsv(&hilite_h, &hilite_s, &hilite_v);

        QColor col;
        col.setHsv(hilite_h,
                   (hilite_s + base_s + base_s ) / 3,
                   (hilite_v + base_v + base_v ) / 3);
        opt.palette.setBrush(QPalette::Base, QBrush( col ) );
    }
    QTreeView::drawRow( painter, opt, index );
}

bool BookmarkListView::parentSelected(const QModelIndex & idx ) const
{
    QModelIndex index = idx.parent();
    while(index.isValid())
    {
        QModelIndex parent = index.parent();
        if(selectionModel()->isRowSelected(index.row(), parent) && parent.isValid() )
            return true;
        else
            index = index.parent();
    }
    return false;
}

//FIXME clean up and remove unneeded things
SelcAbilities BookmarkListView::getSelectionAbilities() const
{
    if(dirtyGetSelectionAbilies)
    {
        const QModelIndexList & sel = selectionModel()->selectedIndexes();
        selctionAbilities.itemSelected   = false;
        selctionAbilities.group          = false;
        selctionAbilities.separator      = false;
        selctionAbilities.urlIsEmpty     = false;
        selctionAbilities.root           = false;
        selctionAbilities.multiSelect    = false;
        selctionAbilities.singleSelect   = false;
        selctionAbilities.notEmpty       = false;

        if ( sel .count() > 0) 
        {
            KBookmark nbk     = static_cast<TreeItem *>((*sel.constBegin()).internalPointer())->bookmark();
            selctionAbilities.itemSelected   = true;
            selctionAbilities.group          = nbk.isGroup();
            selctionAbilities.separator      = nbk.isSeparator();
            selctionAbilities.urlIsEmpty     = nbk.url().isEmpty();
            selctionAbilities.root           = nbk.address() == CurrentMgr::self()->root().address();
            selctionAbilities.multiSelect    = (sel.count() > BookmarkModel::self()->columnCount());
            selctionAbilities.singleSelect   = (!selctionAbilities.multiSelect && selctionAbilities.itemSelected);
        }
        //FIXME check next line, if it actually works
        selctionAbilities.notEmpty = CurrentMgr::self()->root().first().hasParent(); //FIXME that's insane, checks wheter there exists at least one bookmark

//         kdDebug()<<"sa.itemSelected "<<selctionAbilities.itemSelected<<"\nsa.group "<<selctionAbilities.group<<
//                    "\nsa.separator "<<selctionAbilities.separator<<"\nsa.urlIsEmpty "<<selctionAbilities.urlIsEmpty<<
//                    "\nsa.root "<<selctionAbilities.root<<"\nsa.multiSelect "<<selctionAbilities.multiSelect<<
//                    "\nsa.singleSelect "<<selctionAbilities.singleSelect<<endl;
        dirtyGetSelectionAbilies = false;
        return selctionAbilities;
    }
    return selctionAbilities;
}

void BookmarkListView::loadColumnSetting() 
{
    header()->resizeSection(KEBApp::NameColumn, KEBSettings::name());
    header()->resizeSection(KEBApp::UrlColumn, KEBSettings::uRL());
    header()->resizeSection(KEBApp::CommentColumn, KEBSettings::comment());
    header()->resizeSection(KEBApp::StatusColumn, KEBSettings::status());
}

void BookmarkListView::saveColumnSetting() 
{
    KEBSettings::setName( header()->sectionSize(KEBApp::NameColumn));
    KEBSettings::setURL( header()->sectionSize(KEBApp::UrlColumn));
    KEBSettings::setComment( header()->sectionSize(KEBApp::CommentColumn));
    KEBSettings::setStatus( header()->sectionSize(KEBApp::StatusColumn));
    KEBSettings::writeConfig();
}
