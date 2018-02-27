/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "config.h"

#include "collectionview.h"

#include <QPainter>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QToolTip>
#include <QWhatsThis>

#include "collectiondirectorymodel.h"
#include "collectionfilterwidget.h"
#include "collectionmodel.h"
#include "collectionitem.h"
#include "collectionbackend.h"
#include "core/application.h"
#include "core/logging.h"
#include "core/mimedata.h"
#include "core/musicstorage.h"
#include "core/utilities.h"
#include "core/iconloader.h"
#include "device/devicemanager.h"
#include "device/devicestatefiltermodel.h"
#ifdef HAVE_GSTREAMER
#include "dialogs/organisedialog.h"
#include "dialogs/organiseerrordialog.h"
#endif
#include "settings/collectionsettingspage.h"

CollectionItemDelegate::CollectionItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void CollectionItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &opt, const QModelIndex &index) const {

  const bool is_divider = index.data(CollectionModel::Role_IsDivider).toBool();

  if (is_divider) {
    QString text(index.data().toString());

    painter->save();

    QRect text_rect(opt.rect);

    // Does this item have an icon?
    QPixmap pixmap;
    QVariant decoration = index.data(Qt::DecorationRole);
    if (!decoration.isNull()) {
      if (decoration.canConvert<QPixmap>()) {
        pixmap = decoration.value<QPixmap>();
      }
      else if (decoration.canConvert<QIcon>()) {
        pixmap = decoration.value<QIcon>().pixmap(opt.decorationSize);
      }
    }

    // Draw the icon at the left of the text rectangle
    if (!pixmap.isNull()) {
      QRect icon_rect(text_rect.topLeft(), opt.decorationSize);
      const int padding = (text_rect.height() - icon_rect.height()) / 2;
      icon_rect.adjust(padding, padding, padding, padding);
      text_rect.moveLeft(icon_rect.right() + padding + 6);

      if (pixmap.size() != opt.decorationSize) {
        pixmap = pixmap.scaled(opt.decorationSize, Qt::KeepAspectRatio);
      }

      painter->drawPixmap(icon_rect, pixmap);
    }
    else {
      text_rect.setLeft(text_rect.left() + 30);
    }

    // Draw the text
    QFont bold_font(opt.font);
    bold_font.setBold(true);

    painter->setPen(opt.palette.color(QPalette::Text));
    painter->setFont(bold_font);
    painter->drawText(text_rect, text);

    // Draw the line under the item
    QColor line_color = opt.palette.color(QPalette::Text);
    QLinearGradient grad_color(opt.rect.bottomLeft(), opt.rect.bottomRight());
    const double fade_start_end = (opt.rect.width()/3.0)/opt.rect.width();
    line_color.setAlphaF(0.0);
    grad_color.setColorAt(0, line_color);
    line_color.setAlphaF(0.5);
    grad_color.setColorAt(fade_start_end, line_color);
    grad_color.setColorAt(1.0 - fade_start_end, line_color);
    line_color.setAlphaF(0.0);
    grad_color.setColorAt(1, line_color);
    painter->setPen(QPen(grad_color, 1));
    painter->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());

    painter->restore();
  }
  else {
    QStyledItemDelegate::paint(painter, opt, index);
  }

}

bool CollectionItemDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) {

  Q_UNUSED(option);

  if (!event || !view) return false;

  QHelpEvent *he = static_cast<QHelpEvent*>(event);
  QString text = displayText(index.data(), QLocale::system());

  if (text.isEmpty() || !he) return false;

  switch (event->type()) {
    case QEvent::ToolTip: {
      QRect displayed_text;
      QSize real_text;
      bool is_elided = false;

      real_text = sizeHint(option, index);
      displayed_text = view->visualRect(index);
      is_elided = displayed_text.width() < real_text.width();

      if (is_elided) {
        QToolTip::showText(he->globalPos(), text, view);
      }
      else if (index.data(Qt::ToolTipRole).isValid()) {
        // If the item has a tooltip text, display it
        QString tooltip_text = index.data(Qt::ToolTipRole).toString();
        QToolTip::showText(he->globalPos(), tooltip_text, view);
      }
      else {
        // in case that another text was previously displayed
        QToolTip::hideText();
      }
      return true;
    }

    case QEvent::QueryWhatsThis:
      return true;

    case QEvent::WhatsThis:
      QWhatsThis::showText(he->globalPos(), text, view);
      return true;

    default:
      break;
  }
  return false;

}

CollectionView::CollectionView(QWidget *parent)
    : AutoExpandingTreeView(parent),
      app_(nullptr),
      filter_(nullptr),
      total_song_count_(-1),
      total_artist_count_(-1),
      total_album_count_(-1),
      nomusic_(":/pictures/nomusic.png"),
      context_menu_(nullptr),
      is_in_keyboard_search_(false)
  {

  setItemDelegate(new CollectionItemDelegate(this));
  setAttribute(Qt::WA_MacShowFocusRect, false);
  setHeaderHidden(true);
  setAllColumnsShowFocus(true);
  setDragEnabled(true);
  setDragDropMode(QAbstractItemView::DragOnly);
  setSelectionMode(QAbstractItemView::ExtendedSelection);

  setStyleSheet("QTreeView::item{padding-top:1px;}");

}

CollectionView::~CollectionView() {}

void CollectionView::SaveFocus() {

  QModelIndex current = currentIndex();
  QVariant type = model()->data(current, CollectionModel::Role_Type);
  if (!type.isValid() || !(type.toInt() == CollectionItem::Type_Song || type.toInt() == CollectionItem::Type_Container || type.toInt() == CollectionItem::Type_Divider)) {
    return;
  }

  last_selected_path_.clear();
  last_selected_song_ = Song();
  last_selected_container_ = QString();

  switch (type.toInt()) {
    case CollectionItem::Type_Song: {
      QModelIndex index = qobject_cast<QSortFilterProxyModel*>(model())->mapToSource(current);
      SongList songs = app_->collection_model()->GetChildSongs(index);
      if (!songs.isEmpty()) {
        last_selected_song_ = songs.last();
      }
      break;
    }

    case CollectionItem::Type_Container:
    case CollectionItem::Type_Divider: {
      QString text = model()->data(current, CollectionModel::Role_SortText).toString();
      last_selected_container_ = text;
      break;
    }

    default:
      return;
  }

  SaveContainerPath(current);

}

void CollectionView::SaveContainerPath(const QModelIndex &child) {

  QModelIndex current = model()->parent(child);
  QVariant type = model()->data(current, CollectionModel::Role_Type);
  if (!type.isValid() || !(type.toInt() == CollectionItem::Type_Container || type.toInt() == CollectionItem::Type_Divider)) {
    return;
  }

  QString text = model()->data(current, CollectionModel::Role_SortText).toString();
  last_selected_path_ << text;
  SaveContainerPath(current);

}

void CollectionView::RestoreFocus() {

  if (last_selected_container_.isEmpty() && last_selected_song_.url().isEmpty()) {
    return;
  }
  RestoreLevelFocus();

}

bool CollectionView::RestoreLevelFocus(const QModelIndex &parent) {

  if (model()->canFetchMore(parent)) {
    model()->fetchMore(parent);
  }
  int rows = model()->rowCount(parent);
  for (int i = 0; i < rows; i++) {
    QModelIndex current = model()->index(i, 0, parent);
    QVariant type = model()->data(current, CollectionModel::Role_Type);
    switch (type.toInt()) {
      case CollectionItem::Type_Song:
        if (!last_selected_song_.url().isEmpty()) {
          QModelIndex index = qobject_cast<QSortFilterProxyModel*>(model())->mapToSource(current);
          SongList songs = app_->collection_model()->GetChildSongs(index);
          for (const Song& song : songs) {
            if (song == last_selected_song_) {
              setCurrentIndex(current);
              return true;
            }
          }
        }
        break;

      case CollectionItem::Type_Container:
      case CollectionItem::Type_Divider: {
        QString text = model()->data(current, CollectionModel::Role_SortText).toString();
        if (!last_selected_container_.isEmpty() && last_selected_container_ == text) {
          emit expand(current);
          setCurrentIndex(current);
          return true;
        }
        else if (last_selected_path_.contains(text)) {
          emit expand(current);
          // If a selected container or song were not found, we've got into a wrong subtree
          //  (happens with "unknown" all the time)
          if (!RestoreLevelFocus(current)) {
            emit collapse(current);
          }
          else {
            return true;
          }
        }
        break;
      }
    }
  }
  return false;

}

void CollectionView::ReloadSettings() {

  //qLog(Debug) << __PRETTY_FUNCTION__;

  QSettings settings;

  settings.beginGroup(CollectionSettingsPage::kSettingsGroup);
  SetAutoOpen(settings.value("auto_open", true).toBool());

  if (app_ != nullptr) {
    app_->collection_model()->set_pretty_covers(settings.value("pretty_covers", true).toBool());
    app_->collection_model()->set_show_dividers(settings.value("show_dividers", true).toBool());
  }
  
  settings.endGroup();

}

void CollectionView::SetApplication(Application *app) {

  //qLog(Debug) << __PRETTY_FUNCTION__;

  app_ = app;

  ReloadSettings();

}

void CollectionView::SetFilter(CollectionFilterWidget *filter) { filter_ = filter; }

void CollectionView::TotalSongCountUpdated(int count) {

  //qLog(Debug) << __FUNCTION__ << count;

  bool old = total_song_count_;
  total_song_count_ = count;
  if (old != total_song_count_) update();

  if (total_song_count_ == 0)
    setCursor(Qt::PointingHandCursor);
  else
    unsetCursor();
  
  emit TotalSongCountUpdated_();

}

void CollectionView::TotalArtistCountUpdated(int count) {

  //qLog(Debug) << __FUNCTION__ << count;

  bool old = total_artist_count_;
  total_artist_count_ = count;
  if (old != total_artist_count_) update();

  if (total_artist_count_ == 0)
    setCursor(Qt::PointingHandCursor);
  else
    unsetCursor();
  
  emit TotalArtistCountUpdated_();

}

void CollectionView::TotalAlbumCountUpdated(int count) {

  //qLog(Debug) << __FUNCTION__ << count;

  bool old = total_album_count_;
  total_album_count_ = count;
  if (old != total_album_count_) update();

  if (total_album_count_ == 0)
    setCursor(Qt::PointingHandCursor);
  else
    unsetCursor();
  
  emit TotalAlbumCountUpdated_();

}

void CollectionView::paintEvent(QPaintEvent *event) {

  //qLog(Debug) << __FUNCTION__;

  if (total_song_count_ == 0) {
    QPainter p(viewport());
    QRect rect(viewport()->rect());

    // Draw the confused strawberry
    QRect image_rect((rect.width() - nomusic_.width()) / 2, 50, nomusic_.width(), nomusic_.height());
    p.drawPixmap(image_rect, nomusic_);

    // Draw the title text
    QFont bold_font;
    bold_font.setBold(true);
    p.setFont(bold_font);

    QFontMetrics metrics(bold_font);

    QRect title_rect(0, image_rect.bottom() + 20, rect.width(), metrics.height());
    p.drawText(title_rect, Qt::AlignHCenter, tr("Your collection is empty!"));

    // Draw the other text
    p.setFont(QFont());

    QRect text_rect(0, title_rect.bottom() + 5, rect.width(), metrics.height());
    p.drawText(text_rect, Qt::AlignHCenter, tr("Click here to add some music"));
  }
  else {
    QTreeView::paintEvent(event);
  }

}

void CollectionView::mouseReleaseEvent(QMouseEvent *e) {
  
  QTreeView::mouseReleaseEvent(e);

  if (total_song_count_ == 0) {
    emit ShowConfigDialog();
  }

}

void CollectionView::contextMenuEvent(QContextMenuEvent *e) {

  if (!context_menu_) {
    context_menu_ = new QMenu(this);
    add_to_playlist_ = context_menu_->addAction(IconLoader::Load("media-play"), tr("Append to current playlist"), this, SLOT(AddToPlaylist()));
    load_ = context_menu_->addAction(IconLoader::Load("media-play"), tr("Replace current playlist"), this, SLOT(Load()));
    open_in_new_playlist_ = context_menu_->addAction(IconLoader::Load("document-new"), tr("Open in new playlist"), this, SLOT(OpenInNewPlaylist()));

    context_menu_->addSeparator();
    add_to_playlist_enqueue_ = context_menu_->addAction(IconLoader::Load("go-next"), tr("Queue track"), this, SLOT(AddToPlaylistEnqueue()));

#ifdef HAVE_GSTREAMER
    context_menu_->addSeparator();
    organise_ = context_menu_->addAction(IconLoader::Load("edit-copy"), tr("Organise files..."), this, SLOT(Organise()));
    copy_to_device_ = context_menu_->addAction(IconLoader::Load("device"), tr("Copy to device..."), this, SLOT(CopyToDevice()));
    //delete_ = context_menu_->addAction(IconLoader::Load("edit-delete"), tr("Delete from disk..."), this, SLOT(Delete()));
#endif

    context_menu_->addSeparator();
    edit_track_ = context_menu_->addAction(IconLoader::Load("edit-rename"), tr("Edit track information..."), this, SLOT(EditTracks()));
    edit_tracks_ = context_menu_->addAction(IconLoader::Load("edit-rename"), tr("Edit tracks information..."), this, SLOT(EditTracks()));
    show_in_browser_ = context_menu_->addAction(IconLoader::Load("document-open-folder"), tr("Show in file browser..."), this, SLOT(ShowInBrowser()));

    context_menu_->addSeparator();
    show_in_various_ = context_menu_->addAction( tr("Show in various artists"), this, SLOT(ShowInVarious()));
    no_show_in_various_ = context_menu_->addAction( tr("Don't show in various artists"), this, SLOT(NoShowInVarious()));

    context_menu_->addSeparator();

    context_menu_->addMenu(filter_->menu());

#ifdef HAVE_GSTREAMER
    copy_to_device_->setDisabled(app_->device_manager()->connected_devices_model()->rowCount() == 0);
    connect(app_->device_manager()->connected_devices_model(), SIGNAL(IsEmptyChanged(bool)), copy_to_device_, SLOT(setDisabled(bool)));
#endif

  }

  context_menu_index_ = indexAt(e->pos());
  if (!context_menu_index_.isValid()) return;

  context_menu_index_ = qobject_cast<QSortFilterProxyModel*>(model())->mapToSource(context_menu_index_);

  QModelIndexList selected_indexes = qobject_cast<QSortFilterProxyModel*>(model())->mapSelectionToSource(selectionModel()->selection()).indexes();

  int regular_elements = 0;
  int regular_editable = 0;

  for (const QModelIndex& index : selected_indexes) {
    regular_elements++;
    if(app_->collection_model()->data(index, CollectionModel::Role_Editable).toBool()) {
      regular_editable++;
    }
  }

  // TODO: check if custom plugin actions should be enabled / visible
  //const int songs_selected     = smart_playlists + smart_playlists_header + regular_elements;
  const int songs_selected = regular_elements;
  const bool regular_elements_only = songs_selected == regular_elements && regular_elements > 0;

  // in all modes
  load_->setEnabled(songs_selected);
  add_to_playlist_->setEnabled(songs_selected);
  open_in_new_playlist_->setEnabled(songs_selected);
  add_to_playlist_enqueue_->setEnabled(songs_selected);

  // if neither edit_track not edit_tracks are available, we show disabled edit_track element
  //edit_track_->setVisible(!smart_playlists_only && (regular_editable <= 1));
  edit_track_->setVisible(regular_editable <= 1);
  edit_track_->setEnabled(regular_editable == 1);

  // only when no smart playlists selected
#ifdef HAVE_GSTREAMER
  organise_->setVisible(regular_elements_only);
  copy_to_device_->setVisible(regular_elements_only);
  //delete_->setVisible(regular_elements_only);
#endif
  show_in_various_->setVisible(regular_elements_only);
  no_show_in_various_->setVisible(regular_elements_only);

  // only when all selected items are editable
#ifdef HAVE_GSTREAMER
  organise_->setEnabled(regular_elements == regular_editable);
  copy_to_device_->setEnabled(regular_elements == regular_editable);
  //delete_->setEnabled(regular_elements == regular_editable);
#endif

  context_menu_->popup(e->globalPos());

}

void CollectionView::ShowInVarious() { ShowInVarious(true); }

void CollectionView::NoShowInVarious() { ShowInVarious(false); }

void CollectionView::ShowInVarious(bool on) {

  if (!context_menu_index_.isValid()) return;

  // Map is from album name -> all artists sharing that album name, built from each selected
  // song. We put through "Various Artists" changes one album at a time, to make sure the old album
  // node gets removed (due to all children removed), before the new one gets added
  QMultiMap<QString, QString> albums;
  for (const Song& song : GetSelectedSongs()) {
    if (albums.find(song.album(), song.artist()) == albums.end())
      albums.insert(song.album(), song.artist());
  }

  // If we have only one album and we are putting it into Various Artists, check to see
  // if there are other Artists in this album and prompt the user if they'd like them moved, too
  if (on && albums.keys().count() == 1) {
    const QString album = albums.keys().first();
    QList<Song> all_of_album = app_->collection_backend()->GetSongsByAlbum(album);
    QSet<QString> other_artists;
    for (const Song &s : all_of_album) {
      if (!albums.contains(album, s.artist()) &&
          !other_artists.contains(s.artist())) {
        other_artists.insert(s.artist());
      }
    }
    if (other_artists.count() > 0) {
      if (QMessageBox::question(this,
              tr("There are other songs in this album"),
              tr("Would you like to move the other songs in this album to Various Artists as well?"),
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::Yes) == QMessageBox::Yes) {
        for (const QString &s : other_artists) {
          albums.insert(album, s);
        }
      }
    }
  }

  for (const QString &album : QSet<QString>::fromList(albums.keys())) {
    app_->collection_backend()->ForceCompilation(album, albums.values(album), on);
  }

}

void CollectionView::Load() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData* mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->clear_first_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void CollectionView::AddToPlaylist() {

  //qLog(Debug) << __PRETTY_FUNCTION__;

  emit AddToPlaylistSignal(model()->mimeData(selectedIndexes()));

}

void CollectionView::AddToPlaylistEnqueue() {

  //qLog(Debug) << __PRETTY_FUNCTION__;

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData* mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->enqueue_now_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void CollectionView::OpenInNewPlaylist() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData* mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->open_in_new_playlist_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void CollectionView::keyboardSearch(const QString &search) {

  is_in_keyboard_search_ = true;
  QTreeView::keyboardSearch(search);
  is_in_keyboard_search_ = false;

}

void CollectionView::scrollTo(const QModelIndex &index, ScrollHint hint) {

  if (is_in_keyboard_search_)
    QTreeView::scrollTo(index, QAbstractItemView::PositionAtTop);
  else
    QTreeView::scrollTo(index, hint);

}

SongList CollectionView::GetSelectedSongs() const {

  QModelIndexList selected_indexes = qobject_cast<QSortFilterProxyModel*>(model())->mapSelectionToSource(selectionModel()->selection()).indexes();
  return app_->collection_model()->GetChildSongs(selected_indexes);

}

#ifdef HAVE_GSTREAMER
void CollectionView::Organise() {

  if (!organise_dialog_)
    organise_dialog_.reset(new OrganiseDialog(app_->task_manager()));

  organise_dialog_->SetDestinationModel(app_->collection_model()->directory_model());
  organise_dialog_->SetCopy(false);
  if (organise_dialog_->SetSongs(GetSelectedSongs()))
    organise_dialog_->show();
  else {
    QMessageBox::warning(this, tr("Error"), tr("None of the selected songs were suitable for copying to a device"));
  }
}
#endif

void CollectionView::EditTracks() {

  if (!edit_tag_dialog_) {
    edit_tag_dialog_.reset(new EditTagDialog(app_, this));
  }
  edit_tag_dialog_->SetSongs(GetSelectedSongs());
  edit_tag_dialog_->show();

}

#ifdef HAVE_GSTREAMER
void CollectionView::CopyToDevice() {

  if (!organise_dialog_)
    organise_dialog_.reset(new OrganiseDialog(app_->task_manager()));

  organise_dialog_->SetDestinationModel(app_->device_manager()->connected_devices_model(), true);
  organise_dialog_->SetCopy(true);
  organise_dialog_->SetSongs(GetSelectedSongs());
  organise_dialog_->show();

}
#endif

void CollectionView::FilterReturnPressed() {

  if (!currentIndex().isValid()) {
    // Pick the first thing that isn't a divider
    for (int row = 0; row < model()->rowCount(); ++row) {
      QModelIndex idx(model()->index(row, 0));
      if (idx.data(CollectionModel::Role_Type) != CollectionItem::Type_Divider) {
        setCurrentIndex(idx);
        break;
      }
    }
  }

  if (!currentIndex().isValid()) return;

  emit doubleClicked(currentIndex());
}

void CollectionView::ShowInBrowser() {
  QList<QUrl> urls;
  for (const Song &song : GetSelectedSongs()) {
    urls << song.url();
  }

  Utilities::OpenInFileBrowser(urls);
}

int CollectionView::TotalSongs() {
  return total_song_count_;
}
int CollectionView::TotalArtists() {
  return total_artist_count_;
}
int CollectionView::TotalAlbums() {
  return total_album_count_;
}