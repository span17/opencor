/*******************************************************************************

Licensed to the OpenCOR team under one or more contributor license agreements.
See the NOTICE.txt file distributed with this work for additional information
regarding copyright ownership. The OpenCOR team licenses this file to you under
the Apache License, Version 2.0 (the "License"); you may not use this file
except in compliance with the License. You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.

*******************************************************************************/

//==============================================================================
// Single cell view graph panel plot widget
//==============================================================================

#include "cellmlfileruntime.h"
#include "singlecellviewgraphpanelplotwidget.h"

//==============================================================================

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopWidget>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

//==============================================================================

#include <QtNumeric>

//==============================================================================

#include <qnamespace.h>

//==============================================================================

#include <float.h>

//==============================================================================

#include "qwt_painter.h"
#include "qwt_plot_canvas.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_directpainter.h"
#include "qwt_plot_grid.h"
#include "qwt_plot_layout.h"
#include "qwt_plot_renderer.h"
#include "qwt_scale_div.h"
#include "qwt_scale_engine.h"

//==============================================================================

#include "ui_singlecellviewgraphpanelplotwidget.h"

//==============================================================================

namespace OpenCOR {
namespace SingleCellView {

//==============================================================================

SingleCellViewGraphPanelPlotGraph::SingleCellViewGraphPanelPlotGraph(CellMLSupport::CellmlFileRuntimeParameter *pParameterX,
                                                                     CellMLSupport::CellmlFileRuntimeParameter *pParameterY) :
    QwtPlotCurve(),
    mSelected(true),
    mFileName(QString()),
    mParameterX(pParameterX),
    mParameterY(pParameterY)
{
    // Customise ourselves a bit

    setPen(QPen(Qt::darkBlue));
    setRenderHint(QwtPlotItem::RenderAntialiased);
}

//==============================================================================

bool SingleCellViewGraphPanelPlotGraph::isValid() const
{
    // Return whether we are valid

    return   !mFileName.isEmpty()
           && mParameterX && mParameterY;
}

//==============================================================================

bool SingleCellViewGraphPanelPlotGraph::isSelected() const
{
    // Return whether we are selected

    return mSelected;
}

//==============================================================================

void SingleCellViewGraphPanelPlotGraph::setSelected(const bool &pSelected)
{
    // Set our selected state

    mSelected = pSelected;
}

//==============================================================================

QString SingleCellViewGraphPanelPlotGraph::fileName() const
{
    // Return our file name

    return mFileName;
}

//==============================================================================

void SingleCellViewGraphPanelPlotGraph::setFileName(const QString &pFileName)
{
    // Set our file name

    mFileName = pFileName;
}

//==============================================================================

CellMLSupport::CellmlFileRuntimeParameter * SingleCellViewGraphPanelPlotGraph::parameterX() const
{
    // Return our parameter X

    return mParameterX;
}

//==============================================================================

void SingleCellViewGraphPanelPlotGraph::setParameterX(CellMLSupport::CellmlFileRuntimeParameter *pParameterX)
{
    // Set our parameter X

    mParameterX = pParameterX;
}

//==============================================================================

CellMLSupport::CellmlFileRuntimeParameter * SingleCellViewGraphPanelPlotGraph::parameterY() const
{
    // Return our parameter Y

    return mParameterY;
}

//==============================================================================

void SingleCellViewGraphPanelPlotGraph::setParameterY(CellMLSupport::CellmlFileRuntimeParameter *pParameterY)
{
    // Set our parameter Y

    mParameterY = pParameterY;
}

//==============================================================================

SingleCellViewGraphPanelPlotOverlayWidget::SingleCellViewGraphPanelPlotOverlayWidget(SingleCellViewGraphPanelPlotWidget *pParent) :
    QWidget(pParent),
    mOwner(pParent),
    mOriginPoint(QPoint()),
    mPoint(QPoint())
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    setFocusPolicy(Qt::NoFocus);
}

//==============================================================================

QPoint SingleCellViewGraphPanelPlotOverlayWidget::optimisedPoint(const QPoint &pPoint) const
{
    // Optimise the given point so that it fits within our owner's local ranges

    QPoint realPoint = pPoint-mOwner->plotLayout()->canvasRect().topLeft().toPoint();

    QwtScaleMap canvasMapX = mOwner->canvasMap(QwtPlot::xBottom);
    QwtScaleMap canvasMapY = mOwner->canvasMap(QwtPlot::yLeft);

    return QPoint(qMin(qRound(canvasMapX.transform(mOwner->localMaxX())),
                       qMax(qRound(canvasMapX.transform(mOwner->localMinX())),
                            realPoint.x())),
                  qMin(qRound(canvasMapY.transform(mOwner->localMinY())),
                       qMax(qRound(canvasMapY.transform(mOwner->localMaxY())),
                            realPoint.y())));
}

//==============================================================================

void SingleCellViewGraphPanelPlotOverlayWidget::paintEvent(QPaintEvent *pEvent)
{
    // Accept the event

    pEvent->accept();

    // Check whether an action is to be carried out

    if (mOwner->action() == SingleCellViewGraphPanelPlotWidget::None)
        return;

    // Paint the overlay, if any is needed

    QPainter painter(this);

    QRectF canvasRect = mOwner->plotLayout()->canvasRect();

    painter.translate(canvasRect.x(), canvasRect.y());

    switch (mOwner->action()) {
    case SingleCellViewGraphPanelPlotWidget::ShowCoordinates: {
        // Draw the two dashed lines that show the coordinates, using a dark
        // cyan pen

        QPen pen = painter.pen();
        QColor penColor = Qt::darkCyan;

        penColor.setAlphaF(0.69);

        pen.setColor(penColor);
        pen.setStyle(Qt::DashLine);

        painter.setPen(pen);

        QPoint point = optimisedPoint(mPoint);
        // Note: see drawCoordinates() as why we use QPoint rather than
        //       QPointF...

        painter.drawLine(0, point.y(), canvasRect.width(), point.y());
        painter.drawLine(point.x(), 0, point.x(), canvasRect.height());

        // Draw the coordinates of our point

        drawCoordinates(&painter, point, penColor, Qt::white, BottomRight);

        break;
    }
    case SingleCellViewGraphPanelPlotWidget::ZoomRegion: {
        // Draw the region to be zoomed

        QColor penColor = Qt::darkRed;
        QColor brushColor = Qt::yellow;

        penColor.setAlphaF(0.69);
        brushColor.setAlphaF(0.19);

        painter.setPen(penColor);

        QRect zoomRegionRect = zoomRegion();
        // Note: see drawCoordinates() as why we use QRect rather than QRectF...

        painter.fillRect(zoomRegionRect, brushColor);
        painter.drawRect(zoomRegionRect);

        // Draw the two sets of coordinates

        drawCoordinates(&painter, zoomRegionRect.topLeft(),
                        penColor, Qt::white, BottomRight, false);
        drawCoordinates(&painter, zoomRegionRect.topLeft()+QPoint(zoomRegionRect.width(), zoomRegionRect.height()),
                        penColor, Qt::white, TopLeft, false);

        break;
    }
    default:
        // Either no action or not an action we know how to handle

        ;
    }
}

//==============================================================================

void SingleCellViewGraphPanelPlotOverlayWidget::setOriginPoint(const QPoint &pOriginPoint)
{
    // Set our point of origin

    mOriginPoint = pOriginPoint;
    // Note: unlike for setPoint(), we must not repaint ourselves since a call
    //       to setOriginPoint() should always be followed by one to setPoint(),
    //       in which case we don't want to repaint ourselves twice, not least
    //       because the first time we may not have a correct mPoint value...
}

//==============================================================================

void SingleCellViewGraphPanelPlotOverlayWidget::setPoint(const QPoint &pPoint)
{
    // Set our point

    mPoint = pPoint;

    repaint();
}

//==============================================================================

QRect SingleCellViewGraphPanelPlotOverlayWidget::zoomRegion() const
{
    // Return the region to be zoomed based on mOriginPoint and mPoint
    // Note: by default, we assume that we are already fully zoomed in in both
    //       directions...

    QwtScaleMap canvasMapX = mOwner->canvasMap(QwtPlot::xBottom);
    QwtScaleMap canvasMapY = mOwner->canvasMap(QwtPlot::yLeft);

    double xMin = canvasMapX.transform(mOwner->localMinX());
    double xMax = canvasMapX.transform(mOwner->localMaxX());
    double yMin = canvasMapY.transform(mOwner->localMinY());
    double yMax = canvasMapY.transform(mOwner->localMaxY());

    if (mOwner->canZoomInX() || mOwner->canZoomInY()) {
        QPoint originPoint = optimisedPoint(mOriginPoint);
        QPoint point = optimisedPoint(mPoint);

        if (mOwner->canZoomInX()) {
            xMin = qMin(originPoint.x(), point.x());
            xMax = qMax(originPoint.x(), point.x());
        }

        if (mOwner->canZoomInY()) {
            yMin = qMin(originPoint.y(), point.y());
            yMax = qMax(originPoint.y(), point.y());
        }
    }

    return QRect(xMin, yMin, xMax-xMin, yMax-yMin);
}

//==============================================================================

void SingleCellViewGraphPanelPlotOverlayWidget::drawCoordinates(QPainter *pPainter,
                                                                const QPoint &pPoint,
                                                                const QColor &pBackgroundColor,
                                                                const QColor &pForegroundColor,
                                                                const Location &pLocation,
                                                                const bool &pCanMoveLocation)
{
    // Retrieve the size of coordinates as they will appear on the screen,
    // which means using the same font as the one used for the axes
    // Note: normally, pPoint would be a QPointF, but we want the coordinates to
    //       be drawn relative to something (see paintEvent()) and the only way
    //       to guarantee that everything will be painted as expected is to use
    //       QPoint. Indeed, if we were to use QPointF, then QPainter would have
    //       to do some rouding and though everything should be fine (since we
    //       always add/subtract a rounded number), it happens that it's not
    //       always the case. Indeed, we should always have a gap of one pixel
    //       between the coordinates and pPoint, but it could happen that we
    //       have either no gap or one of two pixels, so...

    pPainter->setFont(mOwner->axisFont(QwtPlot::xBottom));

    QPointF point = mOwner->canvasPoint(pPoint, false);
    QString coordinates = QString("X: %1\nY: %2").arg(QLocale().toString(point.x(), 'g', 15),
                                                      QLocale().toString(point.y(), 'g', 15));
    QRect coordinatesRect = pPainter->boundingRect(qApp->desktop()->availableGeometry(), 0, coordinates);

    // Determine where the coordinates and its background should be drawn

    switch (pLocation) {
    case TopLeft:
        coordinatesRect.moveTo(pPoint.x()-(coordinatesRect.left()+coordinatesRect.width())-1,
                               pPoint.y()-(coordinatesRect.top()+coordinatesRect.height())-1);

        break;
    case TopRight:
        coordinatesRect.moveTo(pPoint.x()+2,
                               pPoint.y()-(coordinatesRect.top()+coordinatesRect.height())-1);

        break;
    case BottomLeft:
        coordinatesRect.moveTo(pPoint.x()-(coordinatesRect.left()+coordinatesRect.width())-1,
                               pPoint.y()+2);

        break;
    case BottomRight:
        coordinatesRect.moveTo(pPoint.x()+2,
                               pPoint.y()+2);

        break;
    }

    if (pCanMoveLocation) {
        QwtScaleMap canvasMapX = mOwner->canvasMap(QwtPlot::xBottom);
        QwtScaleMap canvasMapY = mOwner->canvasMap(QwtPlot::yLeft);

        QPoint topLeftPoint = QPoint(canvasMapX.transform(mOwner->localMinX()),
                                     canvasMapY.transform(mOwner->localMaxY()));
        QPoint bottomRightPoint = QPoint(canvasMapX.transform(mOwner->localMaxX()),
                                         canvasMapY.transform(mOwner->localMinY()));

        if (coordinatesRect.top() < topLeftPoint.y())
            coordinatesRect.moveTop(pPoint.y()+2);
        else if (coordinatesRect.top()+coordinatesRect.height()-1 > bottomRightPoint.y())
            coordinatesRect.moveTop(pPoint.y()-coordinatesRect.height()-1);

        if (coordinatesRect.left() < topLeftPoint.x())
            coordinatesRect.moveLeft(pPoint.x()+2);
        else if (coordinatesRect.left()+coordinatesRect.width()-1 > bottomRightPoint.x())
            coordinatesRect.moveLeft(pPoint.x()-coordinatesRect.width()-1);

        // Note: the -1 for the else-if tests is because fillRect() below works
        //       on (0, 0; width-1, height-1)...
    }

    // Draw a filled rectangle to act as the background for the coordinates
    // we are to show

    pPainter->fillRect(coordinatesRect, pBackgroundColor);

    // Draw the text for the coordinates, using a white pen

    QPen pen = pPainter->pen();

    pen.setColor(pForegroundColor);

    pPainter->setPen(pen);

    pPainter->drawText(coordinatesRect, coordinates);
}

//==============================================================================

QwtText SingleCellViewGraphPanelPlotScaleDraw::label(double pValue) const
{
    if (qFuzzyCompare(pValue+1.0, 1.0))
        pValue = 0.0;

    return QLocale().toString(pValue, 'g', 15);
}

//==============================================================================

static const double DblMaxAxis = 0.3*DBL_MAX;
// Note: normally, we would use DBL_MAX, but this means that our maximum axis
//       range would be 2*DBL_MAX, which would cause problems with QwtPlot (e.g.
//       to create ticks), so instead we use a value that results in a range
//       that we know will work...

static const double MinAxis = -DblMaxAxis;
static const double MaxAxis =  DblMaxAxis;

static const double MaxAxisRange = MaxAxis-MinAxis;
static const double MinAxisRange = 1.0e-11;
// Note: to use a smaller minimum axis range would make the coordinates to be
//       shown as "0", so...

static const double DefMinAxis =    0.0;
static const double DefMaxAxis = 1000.0;

//==============================================================================

SingleCellViewGraphPanelPlotWidget::SingleCellViewGraphPanelPlotWidget(QWidget *pParent) :
    QwtPlot(pParent),
    CommonWidget(pParent),
    mGui(new Ui::SingleCellViewGraphPanelPlotWidget),
    mGraphs(QList<SingleCellViewGraphPanelPlotGraph *>()),
    mAction(None),
    mPoint(QPoint()),
    mNeedMinX(0.0),
    mNeedMaxX(0.0),
    mNeedMinY(0.0),
    mNeedMaxY(0.0),
    mCanZoomInX(true),
    mCanZoomOutX(true),
    mCanZoomInY(true),
    mCanZoomOutY(true),
    mNeedContextMenu(false)
{
    // Set up the GUI

    mGui->setupUi(this);

    // Get ourselves a direct painter

    mDirectPainter = new QwtPlotDirectPainter(this);

    // Speedup painting on X11 systems
    // Note: this can only be done on X11 systems...

    if (QwtPainter::isX11GraphicsSystem())
        canvas()->setAttribute(Qt::WA_PaintOnScreen, true);

    // Customise ourselves a bit

    setCanvasBackground(Qt::white);

    setAxisScaleDraw(QwtPlot::xBottom, new SingleCellViewGraphPanelPlotScaleDraw());
    setAxisScaleDraw(QwtPlot::yLeft, new SingleCellViewGraphPanelPlotScaleDraw());

    // We don't want a frame around ourselves

    qobject_cast<QwtPlotCanvas *>(canvas())->setFrameShape(QFrame::NoFrame);

    // Set our axes' local minimum/maximum values

    setLocalMinMaxX(DefMinAxis, DefMaxAxis);
    setLocalMinMaxY(DefMinAxis, DefMaxAxis);

    // Attach a grid to ourselves

    QwtPlotGrid *grid = new QwtPlotGrid();

    grid->setMajorPen(Qt::gray, 0, Qt::DotLine);

    grid->attach(this);

    // Create our overlay widget

    mOverlayWidget = new SingleCellViewGraphPanelPlotOverlayWidget(this);

    // Create our context menu

    mContextMenu = new QMenu(this);

    mContextMenu->addAction(mGui->actionCopy);
    mContextMenu->addSeparator();
    mContextMenu->addAction(mGui->actionZoomIn);
    mContextMenu->addAction(mGui->actionZoomOut);
    mContextMenu->addSeparator();
    mContextMenu->addAction(mGui->actionResetZoom);
}

//==============================================================================

SingleCellViewGraphPanelPlotWidget::~SingleCellViewGraphPanelPlotWidget()
{
    // Delete some internal objects

    delete mDirectPainter;

    foreach (SingleCellViewGraphPanelPlotGraph *graph, mGraphs)
        delete graph;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::retranslateUi()
{
    // Retranslate our GUI

    mGui->retranslateUi(this);
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::eventFilter(QObject *pObject,
                                                     QEvent *pEvent)
{
    // Default handling of the event

    bool res = QwtPlot::eventFilter(pObject, pEvent);

    // We want to handle a double mouse click, but for some reasons to override
    // mouseDoubleClickEvent() doesn't work, so...

    if (pEvent->type() == QEvent::MouseButtonDblClick)
        handleMouseDoubleClickEvent(static_cast<QMouseEvent *>(pEvent));

    // We are all done, so...

    return res;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::handleMouseDoubleClickEvent(QMouseEvent *pEvent)
{
    // Reset the zoom level, in case we double-clicked using the left mouse
    // button with no modifiers

    if (   (pEvent->button() == Qt::LeftButton)
        && (pEvent->modifiers() == Qt::NoModifier))
        on_actionResetZoom_triggered();
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::setAxes(const double &pMinX,
                                                 const double &pMaxX,
                                                 const double &pMinY,
                                                 const double &pMaxY)
{
    // Set our needed minimum/maximum X/Y values

    mNeedMinX = pMinX;
    mNeedMaxX = pMaxX;
    mNeedMinY = pMinY;
    mNeedMaxY = pMaxY;

    // Effectively update our axes by trying to set them

    setLocalAxes(0.0, 0.0, 0.0, 0.0, true, true, false, true);
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::updateActions()
{
    // Update our actions

    double xRange = localMaxX()-localMinX();
    double yRange = localMaxY()-localMinY();

    mCanZoomInX  = xRange > MinAxisRange;
    mCanZoomOutX = xRange < MaxAxisRange;

    mCanZoomInY  = yRange > MinAxisRange;
    mCanZoomOutY = yRange < MaxAxisRange;

    // Update the enabled status of our actions

    mGui->actionZoomIn->setEnabled(mCanZoomInX || mCanZoomInY);
    mGui->actionZoomOut->setEnabled(mCanZoomOutX || mCanZoomOutY);

    mGui->actionResetZoom->setEnabled(true);
//---GRY--- WE SHOULD 'PROPERLY' SET THE ENABLED STATE OF THE RESET ZOOM
//          ACTION...
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::setLocalAxis(const int &pAxis,
                                                      const double &pMin,
                                                      const double &pMax)
{
    // Set our local axis
    // Note #1: to use setAxisScale() on its own is not sufficient unless we
    //          were to replot ourselves immediately after, but we don't want to
    //          do that, so instead we also use setAxisScaleDiv() to make sure
    //          that our local axis is indeed taken into account (i.e. we can
    //          retrieve them using localMinX(), localMaxX(), localMinY() and
    //          localMaxY()). Also, we must call setAxisScaleDiv() before
    //          setAxisScale() to make sure that the axis data is not considered
    //          as valid which is important when it comes to plotting
    //          ourselves...
    // Note #2: the way QwtPlot create ticks for an axis means that it cannot
    //          handle an axis that would go from -DBL_MAX to DBL_MAX, even
    //          though it would be a valid axis, so we check that the axis fits
    //          within what we know works fine with QwtPlot...

    setAxisScaleDiv(pAxis, QwtScaleDiv(qMax(-DblMaxAxis, pMin),
                                       qMin( DblMaxAxis, pMax)));
    setAxisScale(pAxis, qMax(-DblMaxAxis, pMin),
                        qMin( DblMaxAxis, pMax));

    // Make sure that our actions are up-to-date

    updateActions();
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::checkLocalAxisValues(double &pMin,
                                                              double &pMax)
{
    // Make sure that the minimum/maximum values of our local axis are valid

    double range = pMax-pMin;

    if (pMin < MinAxis) {
        // The minimum value is too small, so reset it

        pMin = MinAxis;
        pMax = pMin+range;
    } else if (pMax > MaxAxis) {
        // The maximum value is too big, so reset it

        pMax = MaxAxis;
        pMin = pMax-range;
    }

    if (range > MaxAxisRange) {
        // The range is too big, so reset our minimum/maximum values

        pMin = MinAxis;
        pMax = MaxAxis;
    } else if (range < MinAxisRange) {
        // The range is too small, reset our minimum/maximum values

        pMin = qMax(MinAxis, 0.5*(pMin+pMax-range));
        pMax = qMin(MaxAxis, pMin+range);
        pMin = pMax-range;
        // Note: the last statement is in case pMax has been set to MaxAxis, in
        //       which case we need to re-reset pMin...
    }
}

//==============================================================================

SingleCellViewGraphPanelPlotWidget::Action SingleCellViewGraphPanelPlotWidget::action() const
{
    // Return our action

    return mAction;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::resetAction()
{
    // Reset our action and our overlay widget, by repainting it

    mAction = None;

    mOverlayWidget->repaint();
}

//==============================================================================

double SingleCellViewGraphPanelPlotWidget::localMinX() const
{
    // Return our local minimum X value

    return axisScaleDiv(QwtPlot::xBottom).lowerBound();
}

//==============================================================================

double SingleCellViewGraphPanelPlotWidget::localMaxX() const
{
    // Return our local maximum X value

    return axisScaleDiv(QwtPlot::xBottom).upperBound();
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::setLocalMinMaxX(const double &pLocalMinX,
                                                         const double &pLocalMaxX)
{
    // Set our local minimum X value

    double locMinX = pLocalMinX;
    double locMaxX = pLocalMaxX;

    checkLocalAxisValues(locMinX, locMaxX);

    setLocalAxis(QwtPlot::xBottom, locMinX, locMaxX);
}

//==============================================================================

double SingleCellViewGraphPanelPlotWidget::localMinY() const
{
    // Return our local minimum Y value

    return axisScaleDiv(QwtPlot::yLeft).lowerBound();
}

//==============================================================================

double SingleCellViewGraphPanelPlotWidget::localMaxY() const
{
    // Return our local maximum Y value

    return axisScaleDiv(QwtPlot::yLeft).upperBound();

}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::canZoomInX() const
{
    // Return whether we can zoom in on the X axis

    return mCanZoomInX;
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::canZoomOutX() const
{
    // Return whether we can zoom out on the X axis

    return mCanZoomOutX;
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::canZoomInY() const
{
    // Return whether we can zoom in on the Y axis

    return mCanZoomInY;
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::canZoomOutY() const
{
    // Return whether we can zoom out on the Y axis

    return mCanZoomOutY;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::setLocalMinMaxY(const double &pLocalMinY,
                                                         const double &pLocalMaxY)
{
    // Set our local minimum Y value

    double locMinY = pLocalMinY;
    double locMaxY = pLocalMaxY;

    checkLocalAxisValues(locMinY, locMaxY);

    setLocalAxis(QwtPlot::yLeft, locMinY, locMaxY);
}

//==============================================================================

QList<SingleCellViewGraphPanelPlotGraph *> SingleCellViewGraphPanelPlotWidget::graphs() const
{
    // Return all our graphs

    return mGraphs;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::checkAnyAxesValues(double &pMinX,
                                                            double &pMaxX,
                                                            double &pMinY,
                                                            double &pMaxY)
{
    // Make sure that the minimum/maximum values of our axes have finite values

    if (!qIsFinite(pMinX))
        pMinX = -DBL_MAX;

    if (!qIsFinite(pMaxX))
        pMaxX = DBL_MAX;

    if (!qIsFinite(pMinY))
        pMinY = -DBL_MAX;

    if (!qIsFinite(pMaxY))
        pMaxY = DBL_MAX;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::setLocalAxes(const double &pLocalMinX,
                                                      const double &pLocalMaxX,
                                                      const double &pLocalMinY,
                                                      const double &pLocalMaxY,
                                                      const bool &pCanReplot,
                                                      const bool &pForceMinMaxValues,
                                                      const bool &pUpdateMinMaxValues,
                                                      const bool &pResetMinMaxValues)
{
    // Update our axes

    double oldLocalMinX = localMinX();
    double oldLocalMaxX = localMaxX();
    double oldLocalMinY = localMinY();
    double oldLocalMaxY = localMaxY();

    double newLocalMinX = pLocalMinX;
    double newLocalMaxX = pLocalMaxX;
    double newLocalMinY = pLocalMinY;
    double newLocalMaxY = pLocalMaxY;

    // Retrieve the bounding rectangle for all our graphs (but only for those
    // that have some data)

    QRectF boundingRect = QRectF();

    foreach (SingleCellViewGraphPanelPlotGraph *graph, mGraphs)
        if (graph->isValid() && graph->isSelected() && graph->dataSize())
            boundingRect |= graph->boundingRect();

    // Take into account the needed minimum/maximum values for our X and Y axes,
    // if any, or use DefMinAxis/DefMaxAxis if we have null bounding rectangle

    bool isBoundingRectNull = boundingRect.isNull();
    bool needMinMaxX = mNeedMinX != mNeedMaxX;
    bool needMinMaxY = mNeedMinY != mNeedMaxY;

    if (needMinMaxX) {
        double needWidth = mNeedMaxX-mNeedMinX;

        boundingRect.setLeft(isBoundingRectNull?mNeedMinX:qMin(boundingRect.left(), mNeedMinX));
        boundingRect.setWidth(isBoundingRectNull?needWidth:qMax(boundingRect.width(), needWidth));
    } else if (isBoundingRectNull) {
        boundingRect.setLeft(DefMinAxis);
        boundingRect.setWidth(DefMaxAxis-DefMinAxis);
    }

    if (needMinMaxY) {
        double needHeight = mNeedMaxY-mNeedMinY;

        boundingRect.setTop(isBoundingRectNull?mNeedMinY:qMin(boundingRect.top(), mNeedMinY));
        boundingRect.setHeight(isBoundingRectNull?needHeight:qMax(boundingRect.height(), needHeight));
    } else if (isBoundingRectNull) {
        boundingRect.setTop(DefMinAxis);
        boundingRect.setHeight(DefMaxAxis-DefMinAxis);
    }

    // Update the minimum/maximum values of our axes, should we have retrieved a
    // non-null bounding rectangle

    double xRealMin = MinAxis;
    double xRealMax = MaxAxis;
    double yRealMin = MinAxis;
    double yRealMax = MaxAxis;

    if (boundingRect.isValid()) {
        // Optimise our bounding rectangle by first retrieving the
        // minimum/maximum values of our axes

        double xMin = boundingRect.left();
        double xMax = xMin+boundingRect.width();
        double yMin = boundingRect.top();
        double yMax = yMin+boundingRect.height();

        // Make sure that the minimum/maximum values of our axes have finite
        // values

        checkAnyAxesValues(xMin, xMax, yMin, yMax);

        // Optimise the minimum/maximum values of our axes by rounding them
        // down/up, respectively

        uint xBase = axisScaleEngine(QwtPlot::xBottom)->base();
        double xInterval = xMax-xMin;
        double xMinReal = xMin;
        double xMaxReal = xMax;

        if (!xInterval) {
            xInterval = pow(10.0, qFloor(log10(qAbs(xMin))));

            xMinReal = xMin-0.5*xInterval;
            xMaxReal = xMinReal+xInterval;
        }

        double xMajorStep = QwtScaleArithmetic::divideInterval(xInterval,
                                                               axisMaxMajor(QwtPlot::xBottom),
                                                               xBase);
        double xMinorStep = QwtScaleArithmetic::divideInterval(xMajorStep,
                                                               axisMaxMinor(QwtPlot::xBottom),
                                                               axisScaleEngine(QwtPlot::xBottom)->base());

        uint yBase = axisScaleEngine(QwtPlot::yLeft)->base();
        double yInterval = yMax-yMin;
        double yMinReal = yMin;
        double yMaxReal = yMax;

        if (!yInterval) {
            yInterval = pow(10.0, qFloor(log10(qAbs(yMin))));

            yMinReal = yMin-0.5*yInterval;
            yMaxReal = yMinReal+yInterval;
        }

        double yMajorStep = QwtScaleArithmetic::divideInterval(yInterval,
                                                               axisMaxMajor(QwtPlot::yLeft),
                                                               yBase);
        double yMinorStep = QwtScaleArithmetic::divideInterval(yMajorStep,
                                                               axisMaxMinor(QwtPlot::yLeft),
                                                               yBase);

        xMin = (needMinMaxX && (xMin == mNeedMinX))?mNeedMinX:qFloor(xMinReal/xMinorStep)*xMinorStep;
        xMax = (needMinMaxX && (xMax == mNeedMaxX))?mNeedMaxX:qCeil(xMaxReal/xMinorStep)*xMinorStep;
        yMin = (needMinMaxY && (yMin == mNeedMinY))?mNeedMinY:qFloor(yMinReal/yMinorStep)*yMinorStep;
        yMax = (needMinMaxY && (yMax == mNeedMaxY))?mNeedMaxY:qCeil(yMaxReal/yMinorStep)*yMinorStep;

        // Make sure that the optimised minimum/maximum values of our axes have
        // finite values

        checkAnyAxesValues(xMin, xMax, yMin, yMax);

        // Update the minimum/maximum values of our axes, if required

        if (pResetMinMaxValues) {
            xRealMin = xMin;
            xRealMax = xMax;
        } else if (pUpdateMinMaxValues) {
            xRealMin = qMin(xRealMin, xMin);
            xRealMax = qMax(xRealMax, xMax);
        }

        if (pResetMinMaxValues) {
            yRealMin = yMin;
            yRealMax = yMax;
        } else if (pUpdateMinMaxValues) {
            yRealMin = qMin(yRealMin, yMin);
            yRealMax = qMax(yRealMax, yMax);
        }
    }

    // Make sure that the new minimum/maximum values of our local axes fit
    // within the minimum/maximum values of our axes

    if (pForceMinMaxValues) {
        newLocalMinX = xRealMin;
        newLocalMaxX = xRealMax;
        newLocalMinY = yRealMin;
        newLocalMaxY = yRealMax;
    } else {
        newLocalMinX = qMax(newLocalMinX, xRealMin);
        newLocalMaxX = qMin(newLocalMaxX, xRealMax);
        newLocalMinY = qMax(newLocalMinY, yRealMin);
        newLocalMaxY = qMin(newLocalMaxY, yRealMax);
    }

    // Make sure that the new minimum/maximum values of our local axes have
    // finite values

    checkAnyAxesValues(newLocalMinX, newLocalMaxX, newLocalMinY, newLocalMaxY);

    // Update the minimum/maximum values of our local axes, if needed, or make
    // sure that the new minimum/maximum values of our local axes have a valid
    // zoom factor

    bool needReplot = false;

    if ((newLocalMinX != oldLocalMinX) || (newLocalMaxX != oldLocalMaxX)) {
        setLocalMinMaxX(newLocalMinX, newLocalMaxX);

        needReplot = true;
    } else {
        checkLocalAxisValues(newLocalMinX, newLocalMaxX);
    }

    if ((newLocalMinY != oldLocalMinY) || (newLocalMaxY != oldLocalMaxY)) {
        setLocalMinMaxY(newLocalMinY, newLocalMaxY);

        needReplot = true;
    } else {
        checkLocalAxisValues(newLocalMinY, newLocalMaxY);
    }

    // Replot ourselves, if needed and allowed

    if (needReplot && pCanReplot)
        replotNow();
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::scaleLocalAxes(const double &pScalingFactorX,
                                                        const double &pScalingFactorY)
{
    // Rescale our X axis, but only if zooming in/out is possible on that axis

    bool needRescaling = false;

    QPointF originPoint = canvasPoint(mPoint);

    double xMin = localMinX();
    double xMax = localMaxX();

    if (   ((pScalingFactorX < 1.0) && mCanZoomInX)
        || ((pScalingFactorX > 1.0) && mCanZoomOutX)) {
        double range = pScalingFactorX*(xMax-xMin);

        xMin = qMax(MinAxis, originPoint.x()-0.5*range);
        xMax = qMin(MaxAxis, xMin+range);
        xMin = xMax-range;
        // Note: the last statement is in case xMax has been set to mMaxX, in
        //       which case we need to update xMin...

        needRescaling = true;
    }

    // Rescale our Y axis, but only if zooming in/out is possible on that axis

    double yMin = localMinY();
    double yMax = localMaxY();

    if (   ((pScalingFactorY < 1.0) && mCanZoomInY)
        || ((pScalingFactorY > 1.0) && mCanZoomOutY)) {
        double range = pScalingFactorY*(yMax-yMin);

        yMin = qMax(MinAxis, originPoint.y()-0.5*range);
        yMax = qMin(MaxAxis, yMin+range);
        yMin = yMax-range;
        // Note: the last statement is in case yMax has been set to mMaxY, in
        //       which case we need to update yMin...

        needRescaling = true;
    }

    // Rescale our local axes, if needed

    if (needRescaling)
        setLocalAxes(xMin, xMax, yMin, yMax);
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::checkLocalAxes(const bool &pCanReplot,
                                                        const bool &pForceMinMaxValues,
                                                        const bool &pUpdateMinMaxValues)
{
    // Check our local axes by trying to set them

    setLocalAxes(localMinX(), localMaxX(), localMinY(), localMaxY(),
                 pCanReplot, pForceMinMaxValues, pUpdateMinMaxValues);
}

//==============================================================================

static const double NoScalingFactor     = 1.0;
static const double ScalingInFactor     = 0.9;
static const double ScalingOutFactor    = 1.0/ScalingInFactor;
static const double BigScalingInFactor  = 0.5*ScalingInFactor;
static const double BigScalingOutFactor = 1.0/BigScalingInFactor;

//==============================================================================

QPointF SingleCellViewGraphPanelPlotWidget::canvasPoint(const QPoint &pPoint,
                                                        const bool pNeedOffset) const
{
    // Return the mouse position using canvas coordinates, making sure that they
    // are within our local ranges

    QPointF realPoint = pPoint;

    if (pNeedOffset)
        realPoint -= plotLayout()->canvasRect().topLeft();

    return QPointF(qMin(localMaxX(), qMax(localMinX(), canvasMap(QwtPlot::xBottom).invTransform(realPoint.x()))),
                   qMin(localMaxY(), qMax(localMinY(), canvasMap(QwtPlot::yLeft).invTransform(realPoint.y()))));
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::mouseMoveEvent(QMouseEvent *pEvent)
{
    // Default handling of the event

    QwtPlot::mouseMoveEvent(pEvent);

    // Carry out the action

    switch (mAction) {
    case Pan: {
        // Determine the X/Y shifts for our panning

        QPointF originPoint = canvasPoint(mPoint);
        QPointF currentPoint = canvasPoint(pEvent->pos());

        double shiftX = currentPoint.x()-originPoint.x();
        double shiftY = currentPoint.y()-originPoint.y();

        mPoint = pEvent->pos();

        // Determine our new local minimum/maximum values for our axes

        double newLocalMinX = localMinX()-shiftX;
        double newLocalMaxX = localMaxX()-shiftX;
        double newLocalMinY = localMinY()-shiftY;
        double newLocalMaxY = localMaxY()-shiftY;

        // Make sure that our new local minimum/maximum values for our axes
        // are within our local minimum/maximum values

        if (newLocalMinX < MinAxis) {
            newLocalMinX = MinAxis;
            newLocalMaxX = newLocalMinX+localMaxX()-localMinX();
        } else if (newLocalMaxX > MaxAxis) {
            newLocalMaxX = MaxAxis;
            newLocalMinX = newLocalMaxX-localMaxX()+localMinX();
        }

        if (newLocalMinY < MinAxis) {
            newLocalMinY = MinAxis;
            newLocalMaxY = newLocalMinY+localMaxY()-localMinY();
        } else if (newLocalMaxY > MaxAxis) {
            newLocalMaxY = MaxAxis;
            newLocalMinY = newLocalMaxY-localMaxY()+localMinY();
        }

        // Set our new local minimum/maximum values for our local axes, which
        // will replot ourselves as a result

        setLocalAxes(newLocalMinX, newLocalMaxX, newLocalMinY, newLocalMaxY);

        break;
    }
    case ShowCoordinates:
        // Update the point of our overlay widget

        mOverlayWidget->setPoint(pEvent->pos());

        break;
    case Zoom: {
        // Determine our X/Y delta values

        QPointF originPoint = canvasPoint(mPoint);
        QPointF currentPoint = canvasPoint(pEvent->pos());

        double deltaX = currentPoint.x()-originPoint.x();
        double deltaY = currentPoint.y()-originPoint.y();

        mPoint = pEvent->pos();

        // Rescale ourselves
        // Note: this will automatically replot ourselves...

        scaleLocalAxes(deltaX?
                           (deltaX > 0)?
                               ScalingInFactor:
                               ScalingOutFactor:
                           NoScalingFactor,
                       deltaY?
                           (deltaY < 0)?
                               ScalingInFactor:
                               ScalingOutFactor:
                           NoScalingFactor);

        break;
    }
    case ZoomRegion:
        // Update our zoom region by updating the point of our overlay widget

        mOverlayWidget->setPoint(pEvent->pos());

        break;
    default:
        // None

        ;
    }

    // The mouse has moved, so we definitely won't need to show our context menu

    mNeedContextMenu = false;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::mousePressEvent(QMouseEvent *pEvent)
{
    // Default handling of the event

    QwtPlot::mousePressEvent(pEvent);

    // Check that the position of the mouse is over our canvas

    if (!plotLayout()->canvasRect().contains(pEvent->pos()))
        return;

    // Make sure that we are not already carrying out an action (e.g. we were
    // zooming in/out and then pressed on the left mouse button) and if so, then
    // cancel it by resetting our action

    if (mAction != None) {
        resetAction();

        return;
    }

    // Check which action we can carry out

    if (   (pEvent->button() == Qt::LeftButton)
        && (pEvent->modifiers() == Qt::NoModifier)) {
        // We want to pan, but only do this if we are not completely zoomed out

        if (mCanZoomOutX || mCanZoomOutY) {
            mAction = Pan;

            mPoint = pEvent->pos();
        }
    } else if (   (pEvent->button() == Qt::LeftButton)
               && (pEvent->modifiers() == Qt::ShiftModifier)) {
        // We want to show the coordinates

        mAction = ShowCoordinates;

        mOverlayWidget->setPoint(pEvent->pos());
    } else if (   (pEvent->button() == Qt::RightButton)
               && (pEvent->modifiers() == Qt::NoModifier)) {
        // We want to zoom in/out

        mAction = Zoom;

        mPoint = pEvent->pos();
    } else if (   (pEvent->button() == Qt::RightButton)
               && (pEvent->modifiers() == Qt::ControlModifier)) {
        // We want to zoom a region, but only allow it if we are not already
        // fully zoomed in

        if (mCanZoomInX || mCanZoomInY) {
            mAction = ZoomRegion;

            mOverlayWidget->setOriginPoint(pEvent->pos());
            mOverlayWidget->setPoint(pEvent->pos());
        }
    }

    // Check whether we might need to show our context menu

    mNeedContextMenu = pEvent->button() == Qt::RightButton;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::mouseReleaseEvent(QMouseEvent *pEvent)
{
    // Default handling of the event

    QwtPlot::mouseReleaseEvent(pEvent);

    // Check whether we need to carry out an action

    if (mAction == None)
        return;

    // Finish carrying out the action

    switch (mAction) {
    case ZoomRegion: {
        // Zoom our region, so update the point of our overlay widget and
        // retrieve the zoom region

        mOverlayWidget->setPoint(pEvent->pos());

        QRect zoomRegionRect = mOverlayWidget->zoomRegion();

        // Reset our action

        resetAction();

        // Effectively zoom our region, if possible, by updating our local axes

        QRectF zoomRegion = QRectF(canvasPoint(zoomRegionRect.topLeft(), false),
                                   canvasPoint(zoomRegionRect.topLeft()+QPoint(zoomRegionRect.width(), zoomRegionRect.height()), false));

        if (zoomRegion.width() && zoomRegion.height())
            setLocalAxes(zoomRegion.left(), zoomRegion.left()+zoomRegion.width(),
                         zoomRegion.top()+zoomRegion.height(), zoomRegion.top());

        break;
    }
    default:
        // An action that doesn't require anything specific to be done, except
        // to reset our action

        resetAction();
    }

    // Show our context menu, if still needed

    if (mNeedContextMenu)
        mContextMenu->exec(QCursor::pos());
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::resizeEvent(QResizeEvent *pEvent)
{
    // Default handling of the event

    QwtPlot::resizeEvent(pEvent);

    // Update the size of our overlay widget

    mOverlayWidget->resize(pEvent->size());
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::wheelEvent(QWheelEvent *pEvent)
{
    // Default handling of the event

    QwtPlot::wheelEvent(pEvent);

    // Check whether we are already carrying out an action

    if (mAction != None)
        return;

    // Make sure that we have actually got a delta that will tell us about the
    // kind of zooming we need to do

    if (!pEvent->delta())
        return;

    // The only action we support using the wheel is zooming in/out, but this
    // requires no modifiers being used

    if (pEvent->modifiers() != Qt::NoModifier)
        return;

    // Zoom in/out by scaling our two local axes

    double scalingFactor = (pEvent->delta() > 0)?ScalingInFactor:ScalingOutFactor;

    mPoint = pEvent->pos();

    scaleLocalAxes(scalingFactor, scalingFactor);
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::replotNow()
{
    // Replot ourselves

    replot();

    // Make sure that the replotting occurs immediately
    // Note: this is needed when running a simulation since, otherwise,
    //       replotting won't occur immediately (because of threading)...

    qApp->processEvents();
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::addGraph(SingleCellViewGraphPanelPlotGraph *pGraph)
{
    // Make sure that the given graph is not already attached to us

    if (mGraphs.contains(pGraph))
        return false;

    // Attach the given graph to ourselves and keep track of it

    pGraph->attach(this);

    mGraphs << pGraph;

    return true;
}

//==============================================================================

bool SingleCellViewGraphPanelPlotWidget::removeGraph(SingleCellViewGraphPanelPlotGraph *pGraph)
{
    // Check that the given graph is attached to us

    if (!mGraphs.contains(pGraph))
        return false;

    // Detach the given graph from ourselves, stop tracking it and delete it

    pGraph->detach();

    mGraphs.removeOne(pGraph);

    delete pGraph;

    return true;
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::drawGraphSegment(SingleCellViewGraphPanelPlotGraph *pGraph,
                                                          const qulonglong &pFrom,
                                                          const qulonglong &pTo)
{
    // Make sure that we have a graph segment to draw

    if (pFrom == pTo)
        return;

    // Reset our local axes and replot ourselves, if it is our first graph
    // segment, or carry on as normal

    if (!pFrom) {
        // It is our first graph segment, so reset our local axes by trying to
        // set them
        // Note: we always want to replot, hence our first boolean being false
        //       in our call to setLocalAxes()...

        setLocalAxes(0.0, 0.0, 0.0, 0.0, false, true, false, true);

        replotNow();
    } else {
        // It's not our first graph segment, so determine the minimum/maximum
        // X/Y values of our new data

        double xMin = 0.0;
        double xMax = 0.0;
        double yMin = 0.0;
        double yMax = 0.0;

        for (qulonglong i = pFrom; i <= pTo; ++i)
            if (i == pFrom) {
                xMin = xMax = pGraph->data()->sample(i).x();
                yMin = yMax = pGraph->data()->sample(i).y();
            } else {
                double xVal = pGraph->data()->sample(i).x();
                double yVal = pGraph->data()->sample(i).y();

                xMin = qMin(xMin, xVal);
                xMax = qMax(xMax, xVal);

                yMin = qMin(yMin, yVal);
                yMax = qMax(yMax, yVal);
            }

        // Check whether our X/Y axis can handle the minimum/maximum X/Y values
        // of our new data

        if (   (xMin < MinAxis) || (xMax > MaxAxis)
            || (yMin < MinAxis) || (yMax > MaxAxis))
            // Our X/Y axis cannot handle the minimum/maximum X/Y values of our
            // new data, so check our local axes

            checkLocalAxes(true, true, true);
        else
            // Our X/Y axis can handle the X/Y min/max of our new data, so just
            // draw our new graph segment

            mDirectPainter->drawSeries(pGraph, pFrom, pTo);
    }
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::on_actionCopy_triggered()
{
    // Copy our contents to the clipboard

    QApplication::clipboard()->setPixmap(grab());
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::on_actionZoomIn_triggered()
{
    // Zoom in by scaling our two local axes

    mPoint = mapFromGlobal(QCursor::pos());

    scaleLocalAxes(BigScalingInFactor, BigScalingInFactor);
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::on_actionZoomOut_triggered()
{
    // Zoom out by scaling our two local axes

    mPoint = mapFromGlobal(QCursor::pos());

    scaleLocalAxes(BigScalingOutFactor, BigScalingOutFactor);
}

//==============================================================================

void SingleCellViewGraphPanelPlotWidget::on_actionResetZoom_triggered()
{
    // Reset the zoom level by resetting our local axes, but only if the reset
    // zoom action is enabled
    // Note: we check for the reset zoom action to be enabled since we may call
    //       this method directly...

    if (mGui->actionResetZoom->isEnabled())
        setLocalAxes(0.0, 1000.0, 0.0, 1000.0);
//---GRY--- WE SHOULD 'PROPERLY' SET THE LOCAL AXES...
}

//==============================================================================

}   // namespace SingleCellView
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
