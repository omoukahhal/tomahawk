/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Christian Muehlhaeuser <muesli@tomahawk-player.org>
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

#include "whatshotwidget.h"
#include "ui_whatshotwidget.h"

#include <QPainter>
#include <QStandardItemModel>
#include <QStandardItem>

#include "viewmanager.h"
#include "sourcelist.h"
#include "tomahawksettings.h"
#include "RecentPlaylistsModel.h"

#include "audio/audioengine.h"
#include "playlist/playlistmodel.h"
#include "playlist/treeproxymodel.h"
#include "widgets/overlaywidget.h"
#include "widgets/siblingcrumbbutton.h"
#include "widgets/kbreadcrumbselectionmodel.h"
#include "utils/tomahawkutils.h"
#include "utils/logger.h"
#include <dynamic/GeneratorInterface.h>

#define HISTORY_TRACK_ITEMS 25
#define HISTORY_PLAYLIST_ITEMS 10
#define HISTORY_RESOLVING_TIMEOUT 2500

using namespace Tomahawk;

static QString s_whatsHotIdentifier = QString( "WhatsHotWidget" );

WhatsHotWidget::WhatsHotWidget( QWidget* parent )
    : QWidget( parent )
    , ui( new Ui::WhatsHotWidget )
{
    ui->setupUi( this );

    TomahawkUtils::unmarginLayout( layout() );
    TomahawkUtils::unmarginLayout( ui->stackLeft->layout() );
    TomahawkUtils::unmarginLayout( ui->horizontalLayout->layout() );
    TomahawkUtils::unmarginLayout( ui->horizontalLayout_2->layout() );
    TomahawkUtils::unmarginLayout( ui->breadCrumbLeft->layout() );


    //set crumb widgets
    SiblingCrumbButtonFactory * crumbFactory = new SiblingCrumbButtonFactory;

    m_crumbModelLeft = new QStandardItemModel(this);

    ui->breadCrumbLeft->setButtonFactory(crumbFactory);
    ui->breadCrumbLeft->setModel(m_crumbModelLeft);
    ui->breadCrumbLeft->setRootIcon(QIcon( RESPATH "images/charts.png" ));
    //ui->breadCrumbLeft->setSelectionModel(selectionModelLeft);
    ui->breadCrumbLeft->setUseAnimation(true);

    connect(ui->breadCrumbLeft, SIGNAL(currentIndexChanged(QModelIndex)), SLOT(leftCrumbIndexChanged(QModelIndex)));

    /*ui->breadCrumbRight->setButtonFactory(crumbFactory);
    ui->breadCrumbRight->setRootIcon(QIcon( RESPATH "images/charts.png" ));
    ui->breadCrumbRight->setModel(m_crumbModelLeft);
    ui->breadCrumbRight->setUseAnimation(true);*/


    m_tracksModel = new PlaylistModel( ui->tracksViewLeft );
    m_tracksModel->setStyle( TrackModel::Short );

    ui->tracksViewLeft->setFrameShape( QFrame::NoFrame );
    ui->tracksViewLeft->setAttribute( Qt::WA_MacShowFocusRect, 0 );
    ui->tracksViewLeft->overlay()->setEnabled( false );
    ui->tracksViewLeft->setTrackModel( m_tracksModel );
    ui->tracksViewLeft->setHeaderHidden( true );
    ui->tracksViewLeft->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

    m_artistsModel = new TreeModel( this );
    m_artistsModel->setColumnStyle( TreeModel::TrackOnly );


    m_artistsProxy = new TreeProxyModel( ui->artistsViewLeft );
    m_artistsProxy->setFilterCaseSensitivity( Qt::CaseInsensitive );
    m_artistsProxy->setDynamicSortFilter( true );

    ui->artistsViewLeft->setProxyModel( m_artistsProxy );
    ui->artistsViewLeft->setTreeModel( m_artistsModel );
    ui->artistsViewLeft->setFrameShape( QFrame::NoFrame );
    ui->artistsViewLeft->setAttribute( Qt::WA_MacShowFocusRect, 0 );


    m_artistsProxy->sort( -1 ); // disable sorting, must be called after artistsViewLeft->setTreeModel

    ui->artistsViewLeft->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    ui->artistsViewLeft->header()->setVisible( false );

    m_timer = new QTimer( this );
    connect( m_timer, SIGNAL( timeout() ), SLOT( checkQueries() ) );


    connect( Tomahawk::InfoSystem::InfoSystem::instance(),
             SIGNAL( info( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ),
             SLOT( infoSystemInfo( Tomahawk::InfoSystem::InfoRequestData, QVariant ) ) );

    connect( Tomahawk::InfoSystem::InfoSystem::instance(), SIGNAL( finished( QString ) ), SLOT( infoSystemFinished( QString ) ) );


    QTimer::singleShot( 0, this, SLOT( fetchData() ) );
}


WhatsHotWidget::~WhatsHotWidget()
{
    delete ui;
}

void
WhatsHotWidget::fetchData()
{
    Tomahawk::InfoSystem::InfoCriteriaHash artistInfo;

    Tomahawk::InfoSystem::InfoRequestData requestData;
    requestData.caller = s_whatsHotIdentifier;
    requestData.customData = QVariantMap();
    requestData.input = QVariant::fromValue< Tomahawk::InfoSystem::InfoCriteriaHash >( artistInfo );

    requestData.type = Tomahawk::InfoSystem::InfoChartCapabilities;
    Tomahawk::InfoSystem::InfoSystem::instance()->getInfo( requestData );

    tDebug() << "WhatsHot: requested InfoChartCapabilities";
}


void
WhatsHotWidget::checkQueries()
{
    m_timer->stop();
    m_tracksModel->ensureResolved();
}


void
WhatsHotWidget::infoSystemInfo( Tomahawk::InfoSystem::InfoRequestData requestData, QVariant output )
{
    if ( requestData.caller != s_whatsHotIdentifier )
    {
        return;
    }

    tDebug() << "WhatsHot: got something...";
    QVariantMap returnedData = output.toMap();
    switch ( requestData.type )
    {
        case InfoSystem::InfoChartCapabilities:
        {
            tDebug() << "WhatsHot:: info chart capabilities";
            QStandardItem *rootItem= m_crumbModelLeft->invisibleRootItem();
            tDebug() << "WhatsHot:: " << returnedData.keys();

            foreach(const QString label, returnedData.keys()) {
                tDebug() << "WhatsHot:: parsing " << label;
                QStandardItem *childItem = parseNode( rootItem, label, returnedData[label] );
                tDebug() << "WhatsHot:: appending" << childItem->text();
                rootItem->appendRow(childItem);
            }
            KBreadcrumbSelectionModel *selectionModelLeft = new KBreadcrumbSelectionModel(new QItemSelectionModel(m_crumbModelLeft, this), this);
            ui->breadCrumbLeft->setSelectionModel(selectionModelLeft);
            //ui->breadCrumbRight->setSelectionModel(selectionModelLeft);
            //HACK ALERT - we want the second crumb to expand right away, so we
            //force it here. We should find a more elegant want to do this
            ui->breadCrumbLeft->currentChangedTriggered(m_crumbModelLeft->index(0,0).child(0,0));
            break;
        }
        case InfoSystem::InfoChart:
        {
            if( !returnedData.contains("type") )
                break;
            const QString type = returnedData["type"].toString();
            if( !returnedData.contains(type) )
                break;
            const QString side = requestData.customData["whatshot_side"].toString();
            tDebug() << "WhatsHot: got chart! " << type << " on " << side;
            if( type == "artists" ) {
                setLeftViewArtists();
                const QStringList artists = returnedData["artists"].toStringList();
                tDebug() << "WhatsHot: got artists! " << artists.size();
                m_artistsModel->clear();
                foreach ( const QString& artist, artists )
                    m_artistsModel->addArtists( Artist::get( artist ) );
            } else if( type == "tracks" ) {
                setLeftViewTracks();
                const QList<Tomahawk::InfoSystem::ArtistTrackPair> tracks = returnedData["tracks"].value<QList<Tomahawk::InfoSystem::ArtistTrackPair> >();
                tDebug() << "WhatsHot: got tracks! " << tracks.size();
                m_tracksModel->clear();
                foreach ( const Tomahawk::InfoSystem::ArtistTrackPair& track, tracks ) {
                    query_ptr query = Query::get( track.artist, track.track, QString(), uuid() );
                    m_tracksModel->append( query );
                }
            } else {
                tDebug() << "WhatsHot: got unknown chart type" << type;
            }
            break;
        }
        default:
            return;
    }
}


void
WhatsHotWidget::infoSystemFinished( QString target )
{
    Q_UNUSED( target );
}


void
WhatsHotWidget::leftCrumbIndexChanged( QModelIndex index )
{
    qDebug() << "WhatsHot:: left crumb changed" << index.data();
    QStandardItem* item = m_crumbModelLeft->itemFromIndex(index);
    if( !item )
        return;
    if( !item->data().isValid() )
        return;


    const QString chartId = item->data().toString();

    Tomahawk::InfoSystem::InfoCriteriaHash criteria;
    criteria.insert("chart_id", chartId);

    Tomahawk::InfoSystem::InfoRequestData requestData;
    QVariantMap customData;
    customData.insert("whatshot_side", "left");
    requestData.caller = s_whatsHotIdentifier;
    requestData.customData = customData;
    requestData.input = QVariant::fromValue< Tomahawk::InfoSystem::InfoCriteriaHash >( criteria );

    requestData.type = Tomahawk::InfoSystem::InfoChart;
    Tomahawk::InfoSystem::InfoSystem::instance()->getInfo( requestData );
}

void
WhatsHotWidget::changeEvent( QEvent* e )
{
    QWidget::changeEvent( e );
    switch ( e->type() )
    {
        case QEvent::LanguageChange:
            ui->retranslateUi( this );
            break;

        default:
            break;
    }
}


QStandardItem*
WhatsHotWidget::parseNode(QStandardItem* parentItem, const QString &label, const QVariant &data)
{
    tDebug() << "WhatsHot:: parsing " << label;

    QStandardItem *sourceItem = new QStandardItem(label);
    if( data.canConvert<QList<Tomahawk::InfoSystem::Chart> >() ) {
        QList<Tomahawk::InfoSystem::Chart> charts = data.value<QList<Tomahawk::InfoSystem::Chart> >();
        foreach( Tomahawk::InfoSystem::Chart chart, charts) {
            QStandardItem *childItem= new QStandardItem(chart.label);
            childItem->setData(chart.id);
            sourceItem->appendRow(childItem);
        }
    } else if( data.canConvert<QVariantMap>() ) {
        QVariantMap dataMap = data.toMap();
        foreach(const QString childLabel,dataMap.keys()) {
            QStandardItem *childItem  = parseNode( sourceItem, childLabel, dataMap[childLabel] );
            sourceItem->appendRow(childItem);
        }
    } else if ( data.canConvert<QVariantList>() ) {
        QVariantList dataList = data.toList();

        foreach(const QVariant value, dataList) {
            QStandardItem *childItem= new QStandardItem(value.toString());
            sourceItem->appendRow(childItem);
        }
    } else {
        QStandardItem *childItem= new QStandardItem(data.toString());
        sourceItem->appendRow(childItem);
    }
    return sourceItem;
}

void
WhatsHotWidget::setLeftViewArtists()
{
    ui->stackLeft->setCurrentIndex(1);
}

void
WhatsHotWidget::setLeftViewTracks()
{
    ui->stackLeft->setCurrentIndex(0);
}