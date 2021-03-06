/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Leo Franchi <lfranchi@kde.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "XspfUpdater.h"

#include "playlist.h"
#include "utils/xspfloader.h"
#include "tomahawksettings.h"
#include "pipeline.h"
#include "utils/tomahawkutils.h"

#include <QTimer>

#ifndef ENABLE_HEADLESS
#include <QCheckBox>
#endif

using namespace Tomahawk;

PlaylistUpdaterInterface*
XspfUpdaterFactory::create( const playlist_ptr &pl, const QString& settingsKey )
{
    const bool autoUpdate = TomahawkSettings::instance()->value( QString( "%1/autoupdate" ).arg( settingsKey ) ).toBool();
    const int interval = TomahawkSettings::instance()->value( QString( "%1/interval" ).arg( settingsKey ) ).toInt();
    const QString url = TomahawkSettings::instance()->value( QString( "%1/xspfurl" ).arg( settingsKey ) ).toString();

    XspfUpdater* updater = new XspfUpdater( pl, interval, autoUpdate, url );

    return updater;
}


XspfUpdater::XspfUpdater( const playlist_ptr& pl, int interval, bool autoUpdate, const QString& xspfUrl )
    : PlaylistUpdaterInterface( pl )
    , m_timer( new QTimer( this ) )
    , m_autoUpdate( autoUpdate )
    , m_url( xspfUrl )
{
    m_timer->setInterval( interval );

    connect( m_timer, SIGNAL( timeout() ), this, SLOT( updateNow() ) );

#ifndef ENABLE_HEADLESS
    m_toggleCheckbox = new QCheckBox( );
    m_toggleCheckbox->setText( tr( "Automatically update" ) );
    m_toggleCheckbox->setLayoutDirection( Qt::RightToLeft );
    m_toggleCheckbox->setChecked( m_autoUpdate );
    m_toggleCheckbox->hide();

    connect( m_toggleCheckbox, SIGNAL( toggled( bool ) ), this, SLOT( setAutoUpdate( bool ) ) );
#endif
}


XspfUpdater::~XspfUpdater()
{}


QWidget*
XspfUpdater::configurationWidget() const
{
    return m_toggleCheckbox;
}


void
XspfUpdater::updateNow()
{
    if ( m_url.isEmpty() )
    {
        qWarning() << "XspfUpdater not updating because we have an empty url...";
        return;
    }

    XSPFLoader* l = new XSPFLoader( false, false );
    l->setAutoResolveTracks( false );
    l->setErrorTitle( playlist()->title() );
    l->load( m_url );
    connect( l, SIGNAL( tracks( QList<Tomahawk::query_ptr> ) ), this, SLOT( playlistLoaded( QList<Tomahawk::query_ptr> ) ) );
}


void
XspfUpdater::playlistLoaded( const QList<Tomahawk::query_ptr>& newEntries )
{
    QList< query_ptr > tracks;
    foreach ( const plentry_ptr ple, playlist()->entries() )
        tracks << ple->query();

    bool changed = false;
    QList< query_ptr > mergedTracks = TomahawkUtils::mergePlaylistChanges( tracks, newEntries, changed );

    if ( !changed )
        return;

    QList<Tomahawk::plentry_ptr> el = playlist()->entriesFromQueries( mergedTracks, true );
    playlist()->createNewRevision( uuid(), playlist()->currentrevision(), el );
}


void
XspfUpdater::saveToSettings( const QString& group ) const
{
    TomahawkSettings::instance()->setValue( QString( "%1/autoupdate" ).arg( group ), m_autoUpdate );
    TomahawkSettings::instance()->setValue( QString( "%1/interval" ).arg( group ),  m_timer->interval() );
    TomahawkSettings::instance()->setValue( QString( "%1/xspfurl" ).arg( group ), m_url );
}


void
XspfUpdater::removeFromSettings( const QString& group ) const
{
    TomahawkSettings::instance()->remove( QString( "%1/autoupdate" ).arg( group ) );
    TomahawkSettings::instance()->remove( QString( "%1/interval" ).arg( group ) );
    TomahawkSettings::instance()->remove( QString( "%1/xspfurl" ).arg( group ) );
}


void
XspfUpdater::setAutoUpdate( bool autoUpdate )
{
    m_autoUpdate = autoUpdate;

    if ( m_autoUpdate )
        m_timer->start();
    else
        m_timer->stop();

    const QString key = QString( "playlistupdaters/%1/autoupdate" ).arg( playlist()->guid() );
    TomahawkSettings::instance()->setValue( key, m_autoUpdate );

    // Update immediately as well
    if ( m_autoUpdate )
        QTimer::singleShot( 0, this, SLOT( updateNow() ) );
}

void
XspfUpdater::setInterval( int intervalMsecs )
{
    const QString key = QString( "playlistupdaters/%1/interval" ).arg( playlist()->guid() );
    TomahawkSettings::instance()->setValue( key, intervalMsecs );

    if ( !m_timer )
        m_timer = new QTimer( this );

    m_timer->setInterval( intervalMsecs );
}
