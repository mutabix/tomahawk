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

#include "animatedsplitter.h"

#define ANIMATION_TIME 400


AnimatedSplitter::AnimatedSplitter( QWidget* parent )
    : QSplitter( parent )
    , m_greedyIndex( 0 )
{
    setHandleWidth( 1 );
}


void
AnimatedSplitter::show( int index, bool animate )
{
    QWidget* w = widget( index );
    emit shown( w, animate );
}


void
AnimatedSplitter::hide( int index, bool animate )
{
    QWidget* w = widget( index );
    emit hidden( w, animate );
}


void
AnimatedSplitter::addWidget( QWidget* widget )
{
    QSplitter::addWidget( widget );
}


void
AnimatedSplitter::addWidget( AnimatedWidget* widget )
{
    qDebug() << Q_FUNC_INFO << widget;
    QSplitter::addWidget( widget );

    connect( widget, SIGNAL( showWidget() ), SLOT( onShowRequest() ) );
    connect( widget, SIGNAL( hideWidget() ), SLOT( onHideRequest() ) );
    connect( this, SIGNAL( shown( QWidget*, bool ) ), widget, SLOT( onShown( QWidget*, bool ) ) );
    connect( this, SIGNAL( hidden( QWidget*, bool ) ), widget, SLOT( onHidden( QWidget*, bool ) ) );
}


void
AnimatedSplitter::onShowRequest()
{
    qDebug() << Q_FUNC_INFO << sender();

    AnimatedWidget* w = (AnimatedWidget*)(sender());
    if ( indexOf( w ) > 0 )
        show( indexOf( w ) );
    else
        qDebug() << "Could not find widget:" << sender();
}


void
AnimatedSplitter::onHideRequest()
{
    AnimatedWidget* w = (AnimatedWidget*)(sender());
    if ( indexOf( w ) > 0 )
        hide( indexOf( w ) );
    else
        qDebug() << "Could not find widget:" << sender();
}


void
AnimatedSplitter::setGreedyWidget( int index )
{
    m_greedyIndex = index;
    if( !widget( index ) )
        return;
    QSizePolicy policy = widget( m_greedyIndex )->sizePolicy();
    if( orientation() == Qt::Horizontal )
        policy.setHorizontalStretch( 1 );
    else
        policy.setVerticalStretch( 1 );
    widget( m_greedyIndex )->setSizePolicy( policy );

}


AnimatedWidget::AnimatedWidget( AnimatedSplitter* parent )
    : m_parent( parent )
    , m_isHidden( false )
{
    qDebug() << Q_FUNC_INFO;

    m_timeLine = new QTimeLine( ANIMATION_TIME, this );
    m_timeLine->setUpdateInterval( 5 );
    m_timeLine->setEasingCurve( QEasingCurve::OutBack );

    connect( m_timeLine, SIGNAL( frameChanged( int ) ), SLOT( onAnimationStep( int ) ) );
    connect( m_timeLine, SIGNAL( finished() ), SLOT( onAnimationFinished() ) );
}


AnimatedWidget::~AnimatedWidget()
{
}


void
AnimatedWidget::onShown( QWidget* widget, bool animated )
{
    if ( widget != this )
        return;

    qDebug() << Q_FUNC_INFO << this;

    m_animateForward = true;
    if ( animated )
    {
        if ( m_timeLine->state() == QTimeLine::Running )
            m_timeLine->stop();

        m_timeLine->setFrameRange( height(), sizeHint().height() );
        m_timeLine->setDirection( QTimeLine::Forward );
        m_timeLine->start();
    }
    else
    {
        onAnimationStep( sizeHint().height() );
        onAnimationFinished();
    }

    m_isHidden = false;
}


void
AnimatedWidget::onHidden( QWidget* widget, bool animated )
{
    if ( widget != this )
        return;

    qDebug() << Q_FUNC_INFO << this;

    m_animateForward = false;
    int minHeight = hiddenSize().height();

    if ( animated )
    {
        if ( m_timeLine->state() == QTimeLine::Running )
            m_timeLine->stop();

        m_timeLine->setFrameRange( minHeight, height() );
        m_timeLine->setDirection( QTimeLine::Backward );
        m_timeLine->start();
    }
    else
    {
        onAnimationStep( minHeight );
        onAnimationFinished();
    }

    m_isHidden = true;
}


void
AnimatedWidget::onAnimationStep( int frame )
{
    setFixedHeight( frame );
}


void
AnimatedWidget::onAnimationFinished()
{
    qDebug() << Q_FUNC_INFO;

    if ( m_animateForward )
    {
        setMinimumHeight( hiddenSize().height() );
        setMaximumHeight( QWIDGETSIZE_MAX );
    }
    else
    {
        setFixedHeight( hiddenSize().height() );
    }
}
