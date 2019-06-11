/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018, Jonas Kvinge <jonas@jkvinge.net>
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

#include "player.h"

#include <memory>

#include <QtGlobal>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QSettings>
#include <QtDebug>

#include "core/logging.h"

#include "song.h"
#include "timeconstants.h"
#include "urlhandler.h"
#include "application.h"

#include "engine/enginebase.h"
#include "engine/enginetype.h"

#ifdef HAVE_GSTREAMER
#  include "engine/gstengine.h"
#  include "engine/gststartup.h"
#endif
#ifdef HAVE_XINE
#  include "engine/xineengine.h"
#endif
#ifdef HAVE_PHONON
#  include "engine/phononengine.h"
#endif
#ifdef HAVE_VLC
#  include "engine/vlcengine.h"
#endif

#include "collection/collectionbackend.h"
#include "playlist/playlist.h"
#include "playlist/playlistitem.h"
#include "playlist/playlistmanager.h"
#include "playlist/playlistsequence.h"
#include "equalizer/equalizer.h"
#include "analyzer/analyzercontainer.h"
#include "settings/backendsettingspage.h"
#include "settings/behavioursettingspage.h"
#include "settings/playlistsettingspage.h"
#include "internet/internetservices.h"
#include "internet/internetservice.h"
#include "scrobbler/audioscrobbler.h"

using std::shared_ptr;
using std::unique_ptr;

const char *Player::kSettingsGroup = "Player";

Player::Player(Application *app, QObject *parent)
    : PlayerInterface(parent),
    app_(app),
    engine_(nullptr),
#ifdef HAVE_GSTREAMER
    gst_startup_(new GstStartup(this)),
#endif
    analyzer_(nullptr),
    equalizer_(nullptr),
    stream_change_type_(Engine::First),
    last_state_(Engine::Empty),
    nb_errors_received_(0),
    volume_before_mute_(100),
    last_pressed_previous_(QDateTime::currentDateTime()),
    continue_on_error_(false),
    greyout_(true),
    menu_previousmode_(PreviousBehaviour_DontRestart),
    seek_step_sec_(10),
    volume_control_(true)
    {

  settings_.beginGroup(kSettingsGroup);

  QSettings s;
  s.beginGroup(BackendSettingsPage::kSettingsGroup);
  Engine::EngineType enginetype = Engine::EngineTypeFromName(s.value("engine", EngineName(Engine::GStreamer)).toString().toLower());
  s.endGroup();
  CreateEngine(enginetype);

}

Player::~Player() {
  settings_.endGroup();
}

Engine::EngineType Player::CreateEngine(Engine::EngineType enginetype) {

  Engine::EngineType use_enginetype(Engine::None);

  for (int i = 0 ; use_enginetype == Engine::None ; i++) {
    switch(enginetype) {
      case Engine::None:
#ifdef HAVE_GSTREAMER
      case Engine::GStreamer:{
        use_enginetype=Engine::GStreamer;
        unique_ptr<GstEngine> gst_engine(new GstEngine(app_->task_manager()));
        gst_engine->SetStartup(gst_startup_);
        engine_.reset(gst_engine.release());
        break;
      }
#endif
#ifdef HAVE_XINE
      case Engine::Xine:
        use_enginetype=Engine::Xine;
        engine_.reset(new XineEngine(app_->task_manager()));
        break;
#endif
#ifdef HAVE_VLC
      case Engine::VLC:
        use_enginetype=Engine::VLC;
        engine_.reset(new VLCEngine(app_->task_manager()));
        break;
#endif
#ifdef HAVE_PHONON
      case Engine::Phonon:
        use_enginetype=Engine::Phonon;
        engine_.reset(new PhononEngine(app_->task_manager()));
        break;
#endif
      default:
        if (i > 0) { qFatal("No engine available!"); }
        enginetype = Engine::None;
        break;
    }
  }

  if (use_enginetype != enginetype) { // Engine was set to something else. Reset output and device.
    QSettings s;
    s.beginGroup(BackendSettingsPage::kSettingsGroup);
    s.setValue("engine", EngineName(use_enginetype));
    s.setValue("output", engine_->DefaultOutput());
    s.setValue("device", QVariant());
    s.endGroup();
  }

  if (!engine_) {
    qFatal("Failed to create engine!");
  }

  emit EngineChanged(use_enginetype);

  return use_enginetype;

}

void Player::Init() {

  QSettings s;

  if (!engine_.get()) {
    s.beginGroup(BackendSettingsPage::kSettingsGroup);
    Engine::EngineType enginetype = Engine::EngineTypeFromName(s.value("engine", EngineName(Engine::GStreamer)).toString().toLower());
    s.endGroup();
    CreateEngine(enginetype);
  }

  if (!engine_->Init()) { qFatal("Error initialising audio engine"); }

  analyzer_->SetEngine(engine_.get());

  connect(engine_.get(), SIGNAL(Error(QString)), SIGNAL(Error(QString)));
  connect(engine_.get(), SIGNAL(FatalError()), SLOT(FatalError()));
  connect(engine_.get(), SIGNAL(ValidSongRequested(QUrl)), SLOT(ValidSongRequested(QUrl)));
  connect(engine_.get(), SIGNAL(InvalidSongRequested(QUrl)), SLOT(InvalidSongRequested(QUrl)));
  connect(engine_.get(), SIGNAL(StateChanged(Engine::State)), SLOT(EngineStateChanged(Engine::State)));
  connect(engine_.get(), SIGNAL(TrackAboutToEnd()), SLOT(TrackAboutToEnd()));
  connect(engine_.get(), SIGNAL(TrackEnded()), SLOT(TrackEnded()));
  connect(engine_.get(), SIGNAL(MetaData(Engine::SimpleMetaBundle)), SLOT(EngineMetadataReceived(Engine::SimpleMetaBundle)));

  // Equalizer
  qLog(Debug) << "Creating equalizer";
  connect(equalizer_, SIGNAL(ParametersChanged(int,QList<int>)), app_->player()->engine(), SLOT(SetEqualizerParameters(int,QList<int>)));
  connect(equalizer_, SIGNAL(EnabledChanged(bool)), app_->player()->engine(), SLOT(SetEqualizerEnabled(bool)));
  connect(equalizer_, SIGNAL(StereoBalanceChanged(float)), app_->player()->engine(), SLOT(SetStereoBalance(float)));

  engine_->SetEqualizerEnabled(equalizer_->is_enabled());
  engine_->SetEqualizerParameters(equalizer_->preamp_value(), equalizer_->gain_values());
  engine_->SetStereoBalance(equalizer_->stereo_balance());

  s.beginGroup(BackendSettingsPage::kSettingsGroup);
  volume_control_ = s.value("volume_control", true).toBool();
  s.endGroup();

  if (volume_control_) {
    int volume = settings_.value("volume", 100).toInt();
    SetVolume(volume);
  }

  ReloadSettings();

}

void Player::ReloadSettings() {

  QSettings s;

  s.beginGroup(PlaylistSettingsPage::kSettingsGroup);
  continue_on_error_ = s.value("continue_on_error", false).toBool();
  greyout_ = s.value("greyout_songs_play", true).toBool();
  menu_previousmode_ = PreviousBehaviour(s.value("menu_previousmode", PreviousBehaviour_DontRestart).toInt());
  s.endGroup();

  s.beginGroup(BehaviourSettingsPage::kSettingsGroup);
  seek_step_sec_ = s.value("seek_step_sec", 10).toInt();
  s.endGroup();

  s.beginGroup(BackendSettingsPage::kSettingsGroup);
  bool volume_control = s.value("volume_control", true).toBool();
  if (!volume_control && GetVolume() != 100) SetVolume(100);
  s.endGroup();

  engine_->ReloadSettings();

}

void Player::HandleLoadResult(const UrlHandler::LoadResult &result) {

  // Might've been an async load, so check we're still on the same item
  shared_ptr<PlaylistItem> item = app_->playlist_manager()->active()->current_item();
  if (!item) {
    loading_async_ = QUrl();
    return;
  }

  if (item->Url() != result.original_url_) return;

  switch (result.type_) {
    case UrlHandler::LoadResult::Error:
      loading_async_ = QUrl();
      EngineStateChanged(Engine::Error);
      FatalError();
      emit Error(result.error_);
      break;
    case UrlHandler::LoadResult::NoMoreTracks:
      qLog(Debug) << "URL handler for" << result.original_url_ << "said no more tracks";

      loading_async_ = QUrl();
      PlayNextItem(stream_change_type_);
      break;

    case UrlHandler::LoadResult::TrackAvailable: {

      qLog(Debug) << "URL handler for" << result.original_url_ << "returned" << result.media_url_;

      Song song = item->Metadata();
      bool update(false);

      // If there was no filetype in the song's metadata, use the one provided by URL handler, if there is one
      if (
        (item->Metadata().filetype() == Song::FileType_Unknown && result.filetype_ != Song::FileType_Unknown)
        ||
        (item->Metadata().filetype() == Song::FileType_Stream && result.filetype_ != Song::FileType_Stream)
         )
      {
        song.set_filetype(result.filetype_);
        update = true;
      }
      // If there was no length info in song's metadata, use the one provided by URL handler, if there is one
      if (item->Metadata().length_nanosec() <= 0 && result.length_nanosec_ != -1) {
        song.set_length_nanosec(result.length_nanosec_);
        update = true;
      }
      if (update) {
        item->SetTemporaryMetadata(song);
        app_->playlist_manager()->active()->InformOfCurrentSongChange();
      }
      engine_->Play(result.media_url_, result.original_url_, stream_change_type_, item->Metadata().has_cue(), item->Metadata().beginning_nanosec(), item->Metadata().end_nanosec());

      current_item_ = item;
      loading_async_ = QUrl();
      break;
    }

    case UrlHandler::LoadResult::WillLoadAsynchronously:
      qLog(Debug) << "URL handler for" << result.original_url_ << "is loading asynchronously";

      // We'll get called again later with either NoMoreTracks or TrackAvailable
      loading_async_ = result.original_url_;
      break;
  }

}

void Player::Next() { NextInternal(Engine::Manual); }

void Player::NextInternal(Engine::TrackChangeFlags change) {

  if (HandleStopAfter()) return;

  if (app_->playlist_manager()->active()->current_item()) {
    const QUrl url = app_->playlist_manager()->active()->current_item()->Url();

    if (url_handlers_.contains(url.scheme())) {
      // The next track is already being loaded
      if (url == loading_async_) return;

      stream_change_type_ = change;
      HandleLoadResult(url_handlers_[url.scheme()]->LoadNext(url));
      return;
    }
  }

  PlayNextItem(change);

}

void Player::PlayNextItem(Engine::TrackChangeFlags change) {

  Playlist *active_playlist = app_->playlist_manager()->active();

  // If we received too many errors in auto change, with repeat enabled, we stop
  if (change == Engine::Auto) {
    const PlaylistSequence::RepeatMode repeat_mode = active_playlist->sequence()->repeat_mode();
    if (repeat_mode != PlaylistSequence::Repeat_Off) {
      if ((repeat_mode == PlaylistSequence::Repeat_Track && nb_errors_received_ >= 3) || (nb_errors_received_ >= app_->playlist_manager()->active()->proxy()->rowCount())) {
        // We received too many "Error" state changes: probably looping over a playlist which contains only unavailable elements: stop now.
        nb_errors_received_ = 0;
        Stop();
        return;
      }
    }
  }

  // Manual track changes override "Repeat track"
  const bool ignore_repeat_track = change & Engine::Manual;

  int i = active_playlist->next_row(ignore_repeat_track);
  if (i == -1) {
    app_->playlist_manager()->active()->set_current_row(i);
    emit PlaylistFinished();
    Stop();
    return;
  }

  PlayAt(i, change, false);

}

bool Player::HandleStopAfter() {

  if (app_->playlist_manager()->active()->stop_after_current()) {
    // Find what the next track would've been, and mark that one as current so it plays next time the user presses Play.
    const int next_row = app_->playlist_manager()->active()->next_row();
    if (next_row != -1) {
      app_->playlist_manager()->active()->set_current_row(next_row, true);
    }

    app_->playlist_manager()->active()->StopAfter(-1);

    Stop(true);
    return true;
  }
  return false;

}

void Player::TrackEnded() {

  if (HandleStopAfter()) return;

  if (current_item_ && current_item_->IsLocalCollectionItem() && current_item_->Metadata().id() != -1) {
    app_->playlist_manager()->collection_backend()->IncrementPlayCountAsync(current_item_->Metadata().id());
  }

  NextInternal(Engine::Auto);

}

void Player::PlayPause() {

  switch (engine_->state()) {
    case Engine::Paused:
      engine_->Unpause();
      break;

    case Engine::Playing: {
      if (current_item_->options() & PlaylistItem::PauseDisabled) {
        Stop();
      }
      else {
        engine_->Pause();
      }
      break;
    }

    case Engine::Empty:
    case Engine::Error:
    case Engine::Idle: {
      app_->playlist_manager()->SetActivePlaylist(app_->playlist_manager()->current_id());
      if (app_->playlist_manager()->active()->rowCount() == 0) break;
      int i = app_->playlist_manager()->active()->current_row();
      if (i == -1) i = app_->playlist_manager()->active()->last_played_row();
      if (i == -1) i = 0;
      PlayAt(i, Engine::First, true);
      break;
    }
  }

}

void Player::RestartOrPrevious() {

  if (engine_->position_nanosec() < 8 * kNsecPerSec) return Previous();

  SeekTo(0);

}

void Player::Stop(bool stop_after) {
  engine_->Stop(stop_after);
  app_->playlist_manager()->active()->set_current_row(-1);
  current_item_.reset();
}

void Player::StopAfterCurrent() {
  app_->playlist_manager()->active()->StopAfter(app_->playlist_manager()->active()->current_row());
}

bool Player::PreviousWouldRestartTrack() const {
  // Check if it has been over two seconds since previous button was pressed
  return menu_previousmode_ == PreviousBehaviour_Restart && last_pressed_previous_.isValid() && last_pressed_previous_.secsTo(QDateTime::currentDateTime()) >= 2;
}

void Player::Previous() { PreviousItem(Engine::Manual); }

void Player::PreviousItem(Engine::TrackChangeFlags change) {

  const bool ignore_repeat_track = change & Engine::Manual;

  if (menu_previousmode_ == PreviousBehaviour_Restart) {
    // Check if it has been over two seconds since previous button was pressed
    QDateTime now = QDateTime::currentDateTime();
    if (last_pressed_previous_.isValid() && last_pressed_previous_.secsTo(now) >= 2) {
      last_pressed_previous_ = now;
      PlayAt(app_->playlist_manager()->active()->current_row(), change, false);
      return;
    }
    last_pressed_previous_ = now;
  }

  int i = app_->playlist_manager()->active()->previous_row(ignore_repeat_track);
  app_->playlist_manager()->active()->set_current_row(i);
  if (i == -1) {
    Stop();
    PlayAt(i, change, true);
    return;
  }

  PlayAt(i, change, false);

}

void Player::EngineStateChanged(Engine::State state) {

  if (Engine::Error == state) {
    nb_errors_received_++;
  }
  else {
    nb_errors_received_ = 0;
  }

  switch (state) {
    case Engine::Paused:
      emit Paused();
      break;
    case Engine::Playing:
      emit Playing();
      break;
    case Engine::Error:
      emit Error();
    case Engine::Empty:
    case Engine::Idle:
      emit Stopped();
      break;
  }
  last_state_ = state;

}

void Player::SetVolume(int value) {

  int old_volume = engine_->volume();

  int volume = qBound(0, value, 100);
  settings_.setValue("volume", volume);
  engine_->SetVolume(volume);

  if (volume != old_volume) {
    emit VolumeChanged(volume);
  }

}

int Player::GetVolume() const { return engine_->volume(); }

void Player::PlayAt(int index, Engine::TrackChangeFlags change, bool reshuffle) {

  if (current_item_ && change == Engine::Manual && engine_->position_nanosec() != engine_->length_nanosec()) {
    emit TrackSkipped(current_item_);
    const QUrl &url = current_item_->Url();
    if (url_handlers_.contains(url.scheme())) {
      url_handlers_[url.scheme()]->TrackSkipped();
    }
  }

  if (current_item_ && app_->playlist_manager()->active()->has_item_at(index) && current_item_->Metadata().IsOnSameAlbum(app_->playlist_manager()->active()->item_at(index)->Metadata())) {
    change |= Engine::SameAlbum;
  }

  if (reshuffle) app_->playlist_manager()->active()->ReshuffleIndices();
  app_->playlist_manager()->active()->set_current_row(index);
  if (app_->playlist_manager()->active()->current_row() == -1) {
    // Maybe index didn't exist in the playlist.
    return;
  }

  current_item_ = app_->playlist_manager()->active()->current_item();
  const QUrl url = current_item_->Url();

  if (url_handlers_.contains(url.scheme())) {
    // It's already loading
    if (url == loading_async_) return;

    stream_change_type_ = change;
    HandleLoadResult(url_handlers_[url.scheme()]->StartLoading(url));
  }
  else {
    loading_async_ = QUrl();
    engine_->Play(current_item_->Url(), current_item_->Url(), change, current_item_->Metadata().has_cue(), current_item_->Metadata().beginning_nanosec(), current_item_->Metadata().end_nanosec());
  }

}

void Player::CurrentMetadataChanged(const Song &metadata) {

  // those things might have changed (especially when a previously invalid song was reloaded) so we push the latest version into Engine
  engine_->RefreshMarkers(metadata.beginning_nanosec(), metadata.end_nanosec());

  // Send now playing to scrobble services
  if (app_->scrobbler()->IsEnabled() && engine_->state() == Engine::Playing) {
    Playlist *playlist = app_->playlist_manager()->active();
    current_item_ = playlist->current_item();
    if (playlist && current_item_ && !playlist->nowplaying() && current_item_->Metadata() == metadata && current_item_->Metadata().length_nanosec() > 0) {
      app_->scrobbler()->UpdateNowPlaying(metadata);
      playlist->set_nowplaying(true);
    }
  }

}

void Player::SeekTo(int seconds) {

  const qint64 length_nanosec = engine_->length_nanosec();

  // If the length is 0 then either there is no song playing, or the song isn't seekable.
  if (length_nanosec <= 0) {
    return;
  }

  const qint64 nanosec = qBound(0ll, qint64(seconds) * kNsecPerSec, length_nanosec);
  engine_->Seek(nanosec);

  qLog(Debug) << "Track seeked to" << nanosec << "ns - updating scrobble point";
  app_->playlist_manager()->active()->UpdateScrobblePoint(nanosec);

  emit Seeked(nanosec / 1000);

}

void Player::SeekForward() {
  SeekTo(engine()->position_nanosec() / kNsecPerSec + seek_step_sec_);
}

void Player::SeekBackward() {
  SeekTo(engine()->position_nanosec() / kNsecPerSec - seek_step_sec_);
}

void Player::EngineMetadataReceived(const Engine::SimpleMetaBundle &bundle) {

  PlaylistItemPtr item = app_->playlist_manager()->active()->current_item();
  if (!item) return;

  if (bundle.url != item->Metadata().url()) return;

  Engine::SimpleMetaBundle bundle_copy = bundle;

  // Maybe the metadata is from icycast and has "Artist - Title" shoved together in the title field.
  const int dash_pos = bundle_copy.title.indexOf('-');
  if (dash_pos != -1 && bundle_copy.artist.isEmpty()) {
    // Split on " - " if it exists, otherwise split on "-".
    const int space_dash_pos = bundle_copy.title.indexOf(" - ");
    if (space_dash_pos != -1) {
      bundle_copy.artist = bundle_copy.title.left(space_dash_pos).trimmed();
      bundle_copy.title = bundle_copy.title.mid(space_dash_pos + 3).trimmed();
    }
    else {
      bundle_copy.artist = bundle_copy.title.left(dash_pos).trimmed();
      bundle_copy.title = bundle_copy.title.mid(dash_pos + 1).trimmed();
    }
  }

  Song song = item->Metadata();
  song.MergeFromSimpleMetaBundle(bundle_copy);

  // Ignore useless metadata
  if (song.title().isEmpty() && song.artist().isEmpty()) return;

  app_->playlist_manager()->active()->SetStreamMetadata(item->Url(), song);

}

PlaylistItemPtr Player::GetItemAt(int pos) const {

  if (pos < 0 || pos >= app_->playlist_manager()->active()->rowCount())
    return PlaylistItemPtr();
  return app_->playlist_manager()->active()->item_at(pos);

}

void Player::Mute() {

  if (!volume_control_) return;

  const int current_volume = engine_->volume();

  if (current_volume == 0) {
    SetVolume(volume_before_mute_);
  }
  else {
    volume_before_mute_ = current_volume;
    SetVolume(0);
  }

}

void Player::Pause() { engine_->Pause(); }

void Player::Play() {
  switch (GetState()) {
    case Engine::Playing:
      SeekTo(0);
      break;
    case Engine::Paused:
      engine_->Unpause();
      break;
    default:
      PlayPause();
      break;
  }

}

void Player::ShowOSD() {
  if (current_item_) emit ForceShowOSD(current_item_->Metadata(), false);
}

void Player::TogglePrettyOSD() {
  if (current_item_) emit ForceShowOSD(current_item_->Metadata(), true);
}

void Player::TrackAboutToEnd() {

  // If the current track was from a URL handler then it might have special behaviour to queue up a subsequent track.
  // We don't want to preload (and scrobble) the next item in the playlist if it's just going to be stopped again immediately after.
  if (app_->playlist_manager()->active()->current_item()) {
    const QUrl url = app_->playlist_manager()->active()->current_item()->Url();
    if (url_handlers_.contains(url.scheme())) {
      url_handlers_[url.scheme()]->TrackAboutToEnd();
      return;
    }
  }

  const bool has_next_row = app_->playlist_manager()->active()->next_row() != -1;
  PlaylistItemPtr next_item;

  if (has_next_row) {
    next_item = app_->playlist_manager()->active()->item_at(app_->playlist_manager()->active()->next_row());
  }

  if (engine_->is_autocrossfade_enabled()) {
    // Crossfade is on, so just start playing the next track.  The current one will fade out, and the new one will fade in

    // But, if there's no next track and we don't want to fade out, then do nothing and just let the track finish to completion.
    if (!engine_->is_fadeout_enabled() && !has_next_row) return;

    // If the next track is on the same album (or same cue file),
    // and the user doesn't want to crossfade between tracks on the same album, then don't do this automatic crossfading.
    if (engine_->crossfade_same_album() || !has_next_row || !next_item || !current_item_->Metadata().IsOnSameAlbum(next_item->Metadata())) {
      TrackEnded();
      return;
    }
  }

  // Crossfade is off, so start preloading the next track so we don't get a gap between songs.
  if (!has_next_row || !next_item) return;

  QUrl url = next_item->Url();

  // Get the actual track URL rather than the stream URL.
  if (url_handlers_.contains(url.scheme())) {
    UrlHandler::LoadResult result = url_handlers_[url.scheme()]->LoadNext(url);
    switch (result.type_) {
      case UrlHandler::LoadResult::Error:
        loading_async_ = QUrl();
        EngineStateChanged(Engine::Error);
        FatalError();
        emit Error(result.error_);
        return;
      case UrlHandler::LoadResult::NoMoreTracks:
        return;
      case UrlHandler::LoadResult::WillLoadAsynchronously:
        loading_async_ = url;
        return;
      case UrlHandler::LoadResult::TrackAvailable:
        url = result.media_url_;
        break;
    }
  }
  engine_->StartPreloading(url, next_item->Url(), next_item->Metadata().has_cue(), next_item->Metadata().beginning_nanosec(), next_item->Metadata().end_nanosec());

}

void Player::IntroPointReached() { NextInternal(Engine::Intro); }

void Player::FatalError() {
  nb_errors_received_ = 0;
  Stop();
}

void Player::ValidSongRequested(const QUrl &url) {
  emit SongChangeRequestProcessed(url, true);
}

void Player::InvalidSongRequested(const QUrl &url) {

  if (greyout_) emit SongChangeRequestProcessed(url, false);

  if (!continue_on_error_) {
    FatalError();
    return;
  }

  PlayNextItem(Engine::Auto);

}

void Player::RegisterUrlHandler(UrlHandler *handler) {

  const QString scheme = handler->scheme();

  if (url_handlers_.contains(scheme)) {
    qLog(Warning) << "Tried to register a URL handler for" << scheme << "but one was already registered";
    return;
  }

  qLog(Info) << "Registered URL handler for" << scheme;
  url_handlers_.insert(scheme, handler);
  connect(handler, SIGNAL(destroyed(QObject*)), SLOT(UrlHandlerDestroyed(QObject*)));
  connect(handler, SIGNAL(AsyncLoadComplete(UrlHandler::LoadResult)), SLOT(HandleLoadResult(UrlHandler::LoadResult)));

}

void Player::UnregisterUrlHandler(UrlHandler *handler) {

  const QString scheme = url_handlers_.key(handler);
  if (scheme.isEmpty()) {
    qLog(Warning) << "Tried to unregister a URL handler for" << handler->scheme() << "that wasn't registered";
    return;
  }

  qLog(Info) << "Unregistered URL handler for" << scheme;
  url_handlers_.remove(scheme);
  disconnect(handler, SIGNAL(destroyed(QObject*)), this, SLOT(UrlHandlerDestroyed(QObject*)));
  disconnect(handler, SIGNAL(AsyncLoadComplete(UrlHandler::LoadResult)), this, SLOT(HandleLoadResult(UrlHandler::LoadResult)));

}

const UrlHandler *Player::HandlerForUrl(const QUrl &url) const {

  QMap<QString, UrlHandler*>::const_iterator it = url_handlers_.constFind(url.scheme());
  if (it == url_handlers_.constEnd()) {
    return nullptr;
  }
  return *it;

}

void Player::UrlHandlerDestroyed(QObject *object) {

  UrlHandler *handler = static_cast<UrlHandler*>(object);
  const QString scheme = url_handlers_.key(handler);
  if (!scheme.isEmpty()) {
    url_handlers_.remove(scheme);
  }

}

void Player::HandleAuthentication() {
  emit Authenticated();
}
