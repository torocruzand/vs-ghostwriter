﻿/***********************************************************************
 *
 * Copyright (C) 2016-2020 wereturtle
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#ifndef SESSIONSTATISTICSWIDGET_H
#define SESSIONSTATISTICSWIDGET_H

#include <QScopedPointer>

#include "abstractstatisticswidget.h"

namespace ghostwriter
{
/**
 * Widget to display writing session statistics
 */
class SessionStatisticsWidgetPrivate;
class SessionStatisticsWidget : public AbstractStatisticsWidget
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(SessionStatisticsWidget)

public:
    /**
     * Constructor.
     */
    SessionStatisticsWidget(QWidget *parent = nullptr);

    /**
     * Destructor.
     */
    virtual ~SessionStatisticsWidget();

public slots:
    /**
     * Sets the word count to display.
     */
    void setWordCount(int value);

    /**
     * Sets the page count to display.
     */
    void setPageCount(int value);

    /**
     * Sets the words per minute to display.
     */
    void setWordsPerMinute(int percentage);

    /**
     * Sets the time spent writing.
     */
    void setWritingTime(int minutes);

    /**
     * Sets the percentage of time spent idle (i.e., NOT writing).
     */
    void setIdleTime(int percentage);

private:
    QScopedPointer<SessionStatisticsWidgetPrivate> d_ptr;

};
} // namespace ghostwriter

#endif // SESSIONSTATISTICSWIDGET_H
