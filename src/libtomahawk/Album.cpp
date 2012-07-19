/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *   Copyright 2010-2012, Jeff Mitchell <jeff@tomahawk-player.org>
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

#include "Album.h"

#include "Artist.h"
#include "AlbumPlaylistInterface.h"
#include "database/Database.h"
#include "database/DatabaseImpl.h"
#include "database/IdThreadWorker.h"
#include "Query.h"
#include "Source.h"

#include "utils/Logger.h"

#include <QReadWriteLock>

using namespace Tomahawk;

QHash< QString, album_ptr > Album::s_albumsByName = QHash< QString, album_ptr >();
QHash< unsigned int, album_ptr > Album::s_albumsById = QHash< unsigned int, album_ptr >();
QHash< QString, album_ptr > Album::s_albumsByCoverId = QHash< QString, album_ptr >();

static QMutex s_mutex;
static QReadWriteLock s_idMutex;


inline QString
albumCacheKey( const Tomahawk::artist_ptr& artist, const QString& albumName )
{
    return QString( "%1\t\t%2" ).arg( artist->name() ).arg( albumName );
}


Album::~Album()
{
    QMutexLocker lock( &s_mutex );
    s_albumsByName.remove( albumCacheKey( artist(), name() ) );
    s_albumsByCoverId.remove( coverId() );
/*    if ( id() > 0 )
        s_albumsById.remove( id() );*/

    m_ownRef.clear();

#ifndef ENABLE_HEADLESS
    delete m_cover;
#endif
}


album_ptr
Album::get( const Tomahawk::artist_ptr& artist, const QString& name, bool autoCreate )
{
    if ( !Database::instance() || !Database::instance()->impl() )
        return album_ptr();

    QMutexLocker l( &s_mutex );

    const QString key = albumCacheKey( artist, name );
    if ( s_albumsByName.contains( key ) )
    {
        return s_albumsByName[ key ];
    }

//     qDebug() << "LOOKING UP ALBUM:" << artist->name() << name;
    album_ptr album = album_ptr( new  Album( name, artist ) );
    album->setWeakRef( album.toWeakRef() );
    album->loadId( autoCreate );

    s_albumsByCoverId[ album->coverId() ] = album;
    s_albumsByName[ key ] = album;

    return album;
}


album_ptr
Album::get( unsigned int id, const QString& name, const Tomahawk::artist_ptr& artist )
{
    static QHash< unsigned int, album_ptr > s_albums;

    QMutexLocker lock( &s_mutex );
    if ( s_albumsById.contains( id ) )
    {
        return s_albumsById.value( id );
    }

    album_ptr a = album_ptr( new Album( id, name, artist ), &QObject::deleteLater );
    a->setWeakRef( a.toWeakRef() );

    s_albumsByCoverId[ a->coverId() ] = a;
    s_albumsByName[ albumCacheKey( artist, name ) ] = a;
    if ( id > 0 )
        s_albumsById.insert( id, a );

    return a;
}


album_ptr
Album::getByCoverId( const QString& uuid )
{
    QMutexLocker lock( &s_mutex );

    if ( s_albumsByCoverId.contains( uuid ) )
        return s_albumsByCoverId.value( uuid );

    return album_ptr();
}


Album::Album( unsigned int id, const QString& name, const Tomahawk::artist_ptr& artist )
    : QObject()
    , m_waitingForId( false )
    , m_id( id )
    , m_name( name )
    , m_artist( artist )
    , m_coverLoaded( false )
    , m_coverLoading( false )
#ifndef ENABLE_HEADLESS
    , m_cover( 0 )
#endif
{
    m_sortname = DatabaseImpl::sortname( name );
}


Album::Album( const QString& name, const Tomahawk::artist_ptr& artist )
    : QObject()
    , m_waitingForId( true )
    , m_name( name )
    , m_artist( artist )
    , m_coverLoaded( false )
    , m_coverLoading( false )
#ifndef ENABLE_HEADLESS
    , m_cover( 0 )
#endif
{
    m_sortname = DatabaseImpl::sortname( name );
}

void
Album::onTracksLoaded( Tomahawk::ModelMode mode, const Tomahawk::collection_ptr& collection )
{
    emit tracksAdded( playlistInterface( mode, collection )->tracks(), mode, collection );
}


artist_ptr
Album::artist() const
{
    return m_artist;
}


void
Album::loadId( bool autoCreate )
{
    Q_ASSERT( m_waitingForId );
    IdThreadWorker::getAlbumId( m_ownRef.toStrongRef(), autoCreate );
}


void
Album::setIdFuture( QFuture<unsigned int> future )
{
    m_idFuture = future;
}


unsigned int
Album::id() const
{
    s_idMutex.lockForRead();
    const bool waiting = m_waitingForId;
    unsigned int finalId = m_id;
    s_idMutex.unlock();

    if ( waiting )
    {
        finalId = m_idFuture.result();

        s_idMutex.lockForWrite();
        m_id = finalId;
        m_waitingForId = false;

        if ( m_id > 0 )
        {
            QMutexLocker lock( &s_mutex );
            s_albumsById[ m_id ] = m_ownRef.toStrongRef();
        }

        s_idMutex.unlock();
    }

    return finalId;
}


#ifndef ENABLE_HEADLESS
QPixmap
Album::cover( const QSize& size, bool forceLoad ) const
{
    if ( !m_coverLoaded && !m_coverLoading )
    {
        if ( !forceLoad )
            return QPixmap();

        Tomahawk::InfoSystem::InfoStringHash trackInfo;
        trackInfo["artist"] = artist()->name();
        trackInfo["album"] = name();

        Tomahawk::InfoSystem::InfoRequestData requestData;
        requestData.caller = uniqueId();
        requestData.type = Tomahawk::InfoSystem::InfoAlbumCoverArt;
        requestData.input = QVariant::fromValue< Tomahawk::InfoSystem::InfoStringHash >( trackInfo );
        requestData.customData = QVariantMap();

        connect( Tomahawk::InfoSystem::InfoSystem::instance(),
                SIGNAL( info( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ),
                SLOT( infoSystemInfo( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ) );

        connect( Tomahawk::InfoSystem::InfoSystem::instance(),
                SIGNAL( finished( QString ) ),
                SLOT( infoSystemFinished( QString ) ) );

        Tomahawk::InfoSystem::InfoSystem::instance()->getInfo( requestData );

        m_coverLoading = true;
    }

    if ( !m_cover && !m_coverBuffer.isEmpty() )
    {
        m_cover = new QPixmap();
        m_cover->loadFromData( m_coverBuffer );
    }

    if ( m_cover && !m_cover->isNull() && !size.isEmpty() )
    {
        if ( m_coverCache.contains( size.width() ) )
        {
            return m_coverCache.value( size.width() );
        }

        QPixmap scaledCover;
        scaledCover = m_cover->scaled( size, Qt::KeepAspectRatio, Qt::SmoothTransformation );
        m_coverCache.insert( size.width(), scaledCover );
        return scaledCover;
    }

    if ( m_cover )
        return *m_cover;
    else
        return QPixmap();
}
#endif


void
Album::infoSystemInfo( const Tomahawk::InfoSystem::InfoRequestData& requestData, const QVariant& output )
{
    if ( requestData.caller != uniqueId() ||
         requestData.type != Tomahawk::InfoSystem::InfoAlbumCoverArt )
    {
        return;
    }

    if ( output.isNull() )
    {
        m_coverLoaded = true;
    }
    else if ( output.isValid() )
    {
        QVariantMap returnedData = output.value< QVariantMap >();
        const QByteArray ba = returnedData["imgbytes"].toByteArray();
        if ( ba.length() )
        {
            m_coverBuffer = ba;
        }

        m_coverLoaded = true;
        s_albumsByCoverId.remove( coverId() );
        m_coverId = uuid();
        s_albumsByCoverId[ m_coverId ] = m_ownRef.toStrongRef();
        emit coverChanged();
    }
}


void
Album::infoSystemFinished( const QString& target )
{
    if ( target != uniqueId() )
        return;

    disconnect( Tomahawk::InfoSystem::InfoSystem::instance(), SIGNAL( info( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ),
                this, SLOT( infoSystemInfo( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ) );

    disconnect( Tomahawk::InfoSystem::InfoSystem::instance(), SIGNAL( finished( QString ) ),
                this, SLOT( infoSystemFinished( QString ) ) );

    m_coverLoading = false;

    emit updated();
}


Tomahawk::playlistinterface_ptr
Album::playlistInterface( ModelMode mode, const Tomahawk::collection_ptr& collection )
{
    playlistinterface_ptr pli = m_playlistInterface[ mode ][ collection ];

    if ( pli.isNull() )
    {
        pli = Tomahawk::playlistinterface_ptr( new Tomahawk::AlbumPlaylistInterface( this, mode, collection ) );
        connect( pli.data(), SIGNAL( tracksLoaded( Tomahawk::ModelMode, Tomahawk::collection_ptr ) ),
                               SLOT( onTracksLoaded( Tomahawk::ModelMode, Tomahawk::collection_ptr ) ) );

        m_playlistInterface[ mode ][ collection ] = pli;
    }

    return pli;
}


QList<Tomahawk::query_ptr>
Album::tracks( ModelMode mode, const Tomahawk::collection_ptr& collection )
{
    return playlistInterface( mode, collection )->tracks();
}


QString
Album::uniqueId() const
{
    if ( m_uuid.isEmpty() )
        m_uuid = uuid();

    return m_uuid;
}


QString
Album::coverId() const
{
    if ( m_coverId.isEmpty() )
        m_coverId = uuid();

    return m_coverId;
}
