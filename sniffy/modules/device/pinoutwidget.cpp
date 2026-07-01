#include "pinoutwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFontMetricsF>
#include <QtMath>
#include <QPen>
#include <QMouseEvent>
#include <QWheelEvent>

#include "../../graphics/graphics.h"

namespace {
constexpr int kMinimumWidgetWidth = 360;
constexpr int kMinimumWidgetHeight = 260;
constexpr int kDefaultCanvasWidth = 1000;
constexpr int kDefaultCanvasHeight = 700;
constexpr int kDefaultConnectorRows = 1;
constexpr int kDefaultConnectorPinCount = 1;
constexpr int kSingleConnectorRows = 1;
constexpr int kDualConnectorRows = 2;
constexpr int kFirstConnectorRow = 0;
constexpr int kSecondConnectorRow = 1;
constexpr int kFirstPinIndex = 0;
constexpr int kInvalidIndex = -1;
constexpr int kFirstConnectorIndex = 0;
constexpr int kStringCompareEqual = 0;
constexpr int kPinNumberRowsPerIndex = 2;
constexpr int kOneBasedPinOffset = 1;
constexpr float kDefaultConnectorPitch = 30.0f;
constexpr float kDefaultConnectorRowSpacing = 20.0f;
constexpr float kDefaultConnectorRow0XOffset = 14.0f;
constexpr float kDefaultConnectorPin0YOffset = 16.0f;
constexpr float kUnsetPinCoordinate = 0.0f;
constexpr float kLogicalScale = 1.0f;
constexpr float kPadExtraLogicalPx = 1.0f;
constexpr float kIconSizeLogicalPx = 22.0f;
constexpr float kOuterStubLengthLogicalPx = 18.0f;
constexpr float kSeparatedOuterStubLengthLogicalPx = 9.0f;
constexpr float kStubStartGapLogicalPx = 0.9f;
constexpr float kGroupGapLogicalPx = 10.0f;
constexpr float kColumnGapLogicalPx = 10.0f;
constexpr float kOverlayGapLogicalPx = 8.0f;
constexpr float kOuterTextGapLogicalPx = 7.0f;
constexpr float kInnerTextGapLogicalPx = 7.0f;
constexpr float kMinimumUiScale = 1.0f;
constexpr int kOverlayTextGapSpaces = 2;
constexpr float kOverlayTextGapMinLogicalPx = 3.0f;
constexpr int kLabelFontLogicalPx = 22;
constexpr int kLabelFontMinimumPx = 7;
constexpr int kConnectorFontLogicalPx = 28;
constexpr int kConnectorFontMinimumPx = 8;
constexpr int kPinNumberFontLogicalPx = 18;
constexpr int kPinNumberFontMinimumPx = 6;
constexpr int kOverlayLabelFontLogicalPx = 16;
constexpr int kOverlayLabelFontMinimumPx = 7;
constexpr qreal kConnectorLabelOffsetYLogicalPx = 24.0;
constexpr qreal kFallbackArduinoPadLogicalPx = 24.0;
constexpr qreal kHalfDivisor = 2.0;
constexpr qreal kZeroLogicalPx = 0.0;
constexpr qreal kInitialBestLinkDistance = 1.0e9;
constexpr qreal kMorphoLinkMaxDistance = 20.0;
constexpr qreal kMinimumAvailablePaintSize = 1.0;
constexpr qreal kFitMarginPx = 8.0;
constexpr qreal kMaxOverlayIconWidthFactor = 2.0;
constexpr qreal kUnlinkedLineHorizontalFactor = 0.42;
constexpr qreal kUnlinkedLineSlopeDivisor = 1.75;
constexpr qreal kStubLineWidthLogicalPx = 3.0;
constexpr qreal kPinOutlineWidthLogicalPx = 1.3;
constexpr qreal kPanSensitivity = 1.0;
constexpr int kWheelAngleDeltaDegreesDivisor = 8;
constexpr qreal kWheelDegreesPerStep = 15.0;
constexpr qreal kWheelZoomBase = 1.15;
constexpr qreal kMinZoom = 0.35;
constexpr qreal kMaxZoom = 8.0;
constexpr int kArduinoLabelRed = 235;
constexpr int kArduinoLabelGreen = 0;
constexpr int kArduinoLabelBlue = 220;
constexpr int kActiveFunctionRed = 50;
constexpr int kActiveFunctionGreen = 220;
constexpr int kActiveFunctionBlue = 50;
constexpr int kBoxOutlineComponent = 245;
constexpr int kDarkStubColorRed = 196;
constexpr int kDarkStubColorGreen = 202;
constexpr int kDarkStubColorBlue = 214;
constexpr int kLightStubColorRed = 110;
constexpr int kLightStubColorGreen = 116;
constexpr int kLightStubColorBlue = 126;
constexpr qreal kDarkBackgroundLightnessLimit = 0.45;
constexpr int kMorphoConnectorRed = 24;
constexpr int kMorphoConnectorGreen = 32;
constexpr int kMorphoConnectorBlue = 230;
constexpr int kPinNumberTextComponent = 245;
constexpr int kInactivePortLabelDarkerFactor = 120;
constexpr int kOverlayChannelCaptureGroup = 1;

const QString kDefaultTemplateId = QStringLiteral("NUCLEO-64");
const QString kNucleo144TemplateId = QStringLiteral("NUCLEO-144");
const QString kNucleo64LeftMorphoId = QStringLiteral("CN7");
const QString kNucleo64RightMorphoId = QStringLiteral("CN10");
const QString kNucleo64LeftArduinoTopId = QStringLiteral("CN6");
const QString kNucleo64LeftArduinoBotId = QStringLiteral("CN8");
const QString kNucleo64RightArduinoTopId = QStringLiteral("CN5");
const QString kNucleo64RightArduinoBotId = QStringLiteral("CN9");
const QString kNucleo144LeftMorphoId = QStringLiteral("CN11");
const QString kNucleo144RightMorphoId = QStringLiteral("CN12");
const QString kNucleo144LeftArduinoTopId = QStringLiteral("CN8");
const QString kNucleo144LeftArduinoBotId = QStringLiteral("CN9");
const QString kNucleo144RightArduinoTopId = QStringLiteral("CN7");
const QString kNucleo144RightArduinoBotId = QStringLiteral("CN10");
}

// --------------------------------------------------------------------------
PinoutWidget::PinoutWidget(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(kMinimumWidgetWidth, kMinimumWidgetHeight);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

// --------------------------------------------------------------------------
void PinoutWidget::setBoard(const QString &boardId)
{
    m_connectors.clear();
    m_pins.clear();
    clearPinFunctions();

    // -- Load board config ---------------------------------------------------
    const QString boardPath = ":/config/" + boardId + ".json";
    QFile bf(boardPath);
    if(!bf.open(QIODevice::ReadOnly)){
        qWarning("PinoutWidget: board config not found: %s", qPrintable(boardPath));
        update();
        return;
    }
    const QJsonObject boardObj = QJsonDocument::fromJson(bf.readAll()).object();
    const QString templateId = boardObj.value("template").toString(kDefaultTemplateId);
    m_templateId = templateId;

    // -- Load template -------------------------------------------------------
    const QString tmplPath = templatePathFor(templateId);
    QFile tf(tmplPath);
    if(!tf.open(QIODevice::ReadOnly)){
        qWarning("PinoutWidget: template not found: %s", qPrintable(tmplPath));
        update();
        return;
    }
    const QJsonObject tmplObj = QJsonDocument::fromJson(tf.readAll()).object();

    parseTemplate(tmplObj);
    parsePins(boardObj.value("pins").toArray());
    buildPinPositions();
    resetView();
    QList<PinoutBoardPin> boardPins;
    boardPins.reserve(m_pins.size());
    for(const PinDesc &pin : m_pins)
        boardPins.append({pin.port, pin.arduino});
    m_functionMap.setBoardPins(boardPins);
    update();
}

// --------------------------------------------------------------------------
void PinoutWidget::parseTemplate(const QJsonObject &tmpl)
{
    m_canvasW = tmpl.value("canvas_w").toInt(kDefaultCanvasWidth);
    m_canvasH = tmpl.value("canvas_h").toInt(kDefaultCanvasHeight);

    const QJsonArray conns = tmpl.value("connectors").toArray();
    m_connectors.reserve(conns.size());
    for(const QJsonValue &v : conns){
        const QJsonObject o = v.toObject();
        ConnectorDesc c;
        c.id         = o.value("id").toString();
        c.label      = o.value("label").toString(c.id);
        c.anchorX    = (float)o.value("anchor_x").toDouble();
        c.anchorY    = (float)o.value("anchor_y").toDouble();
        c.rows       = o.value("rows").toInt(kDefaultConnectorRows);
        c.pinCount   = o.value("pin_count").toInt(kDefaultConnectorPinCount);
        c.pitch      = (float)o.value("pitch").toDouble(kDefaultConnectorPitch);
        c.rowSpacing = (float)o.value("row_spacing").toDouble(kDefaultConnectorRowSpacing);
        c.row0X      = (float)o.value("row0_x").toDouble(c.anchorX + kDefaultConnectorRow0XOffset);
        c.row1X      = (float)o.value("row1_x").toDouble(c.row0X + c.rowSpacing);
        c.pin0Y      = (float)o.value("pin0_y").toDouble(c.anchorY + kDefaultConnectorPin0YOffset);
        m_connectors.append(c);
    }
}

// --------------------------------------------------------------------------
void PinoutWidget::parsePins(const QJsonArray &pinsArr)
{
    m_pins.reserve(pinsArr.size());
    for(const QJsonValue &v : pinsArr){
        const QJsonObject o = v.toObject();
        PinDesc p;
        p.connectorId = o.value("connector").toString();
        p.row         = o.value("row").toInt(kFirstConnectorRow);
        p.index       = o.value("index").toInt(kFirstPinIndex);
        p.port        = o.value("morpho").toString();
        p.arduino     = o.value("arduino").toString();
        p.cx = p.cy = kUnsetPinCoordinate;  // filled by buildPinPositions
        m_pins.append(p);
    }
}

// --------------------------------------------------------------------------
QPointF PinoutWidget::pinPos(const ConnectorDesc &c, int row, int index) const
{
    float x = (row == kFirstConnectorRow) ? c.row0X : c.row1X;
    float y = c.pin0Y + index * c.pitch;
    return {x, y};
}

// --------------------------------------------------------------------------
QString PinoutWidget::templatePathFor(const QString &templateId) const
{
    QString templateKey = templateId.toLower();
    templateKey.remove('-');
    templateKey.remove('_');
    return ":/config/" + templateKey + "_template.json";
}

// --------------------------------------------------------------------------
void PinoutWidget::buildPinPositions()
{
    const QHash<QString, int> connIdx = buildConnectorIndex();

    for(PinDesc &pin : m_pins){
        const int ci = connIdx.value(pin.connectorId, kInvalidIndex);
        if(ci < kFirstPinIndex) continue;
        const QPointF pos = pinPos(m_connectors[ci], pin.row, pin.index);
        pin.cx = (float)pos.x();
        pin.cy = (float)pos.y();
    }
}

// --------------------------------------------------------------------------
QHash<QString, int> PinoutWidget::buildConnectorIndex() const
{
    QHash<QString, int> connectorIndexById;
    connectorIndexById.reserve(m_connectors.size());
    for(int i = kFirstConnectorIndex; i < m_connectors.size(); ++i)
        connectorIndexById.insert(m_connectors[i].id, i);
    return connectorIndexById;
}

// --------------------------------------------------------------------------
const PinoutWidget::ConnectorDesc *PinoutWidget::connectorById(
    const QString &connectorId, const QHash<QString, int> &connectorIndexById) const
{
    const int idx = connectorIndexById.value(connectorId, kInvalidIndex);
    return (idx >= kFirstPinIndex) ? &m_connectors[idx] : nullptr;
}

// --------------------------------------------------------------------------
float PinoutWidget::padSideFor(const ConnectorDesc &connector, float invScale) const
{
    const float grid = (connector.rows == kDualConnectorRows)
                       ? qMin(connector.pitch, connector.rowSpacing)
                       : connector.pitch;
    return grid + kPadExtraLogicalPx * invScale;
}

// --------------------------------------------------------------------------
QRectF PinoutWidget::bodyRectFor(const ConnectorDesc &connector, float invScale) const
{
    const float padW = padSideFor(connector, invScale);
    const float padH = padSideFor(connector, invScale);
    const float bodyX = connector.row0X - padW / kHalfDivisor;
    const float bodyY = connector.pin0Y - padH / kHalfDivisor;
    const float bodyW = (connector.rows == kDualConnectorRows)
                        ? (connector.row1X - connector.row0X + padW)
                        : padW;
    const float bodyH = (connector.pinCount - kOneBasedPinOffset) * connector.pitch + padH;
    return QRectF(bodyX, bodyY, bodyW, bodyH);
}

// --------------------------------------------------------------------------
QRectF PinoutWidget::combinedBody(const QHash<QString, QRectF> &connectorBodies,
                                  const QString &first,
                                  const QString &second) const
{
    if(connectorBodies.contains(first) && connectorBodies.contains(second))
        return connectorBodies.value(first).united(connectorBodies.value(second));
    if(connectorBodies.contains(first))
        return connectorBodies.value(first);
    if(connectorBodies.contains(second))
        return connectorBodies.value(second);
    return QRectF();
}

// --------------------------------------------------------------------------
QList<const PinoutWidget::PinDesc*> PinoutWidget::collectPins(
    const std::function<bool(const PinDesc &)> &predicate) const
{
    QList<const PinDesc*> pins;
    pins.reserve(m_pins.size());
    for(const PinDesc &candidate : m_pins){
        if(predicate(candidate))
            pins.append(&candidate);
    }
    return pins;
}

// --------------------------------------------------------------------------
PinoutWidget::PinLinkMap PinoutWidget::buildMorphoLinks(
    const QList<const PinDesc*> &morphoPins,
    const QList<const PinDesc*> &arduinoPins) const
{
    PinLinkMap links;
    QList<const PinDesc*> usedArduinoPins;
    for(const PinDesc *morphoPin : morphoPins){
        const PinDesc *bestArduinoPin = nullptr;
        qreal bestDistance = kInitialBestLinkDistance;
        for(const PinDesc *arduinoPin : arduinoPins){
            if(usedArduinoPins.contains(arduinoPin))
                continue;
            if(morphoPin->port.compare(arduinoPin->port, Qt::CaseInsensitive) != kStringCompareEqual)
                continue;

            const qreal distance = qAbs(morphoPin->cy - arduinoPin->cy);
            if(distance < bestDistance){
                bestDistance = distance;
                bestArduinoPin = arduinoPin;
            }
        }

        if(bestArduinoPin && bestDistance <= kMorphoLinkMaxDistance){
            links.insert(morphoPin, bestArduinoPin);
            usedArduinoPins.append(bestArduinoPin);
        }
    }
    return links;
}

// --------------------------------------------------------------------------
qreal PinoutWidget::averageLinkDeltaY(const PinLinkMap &links)
{
    if(links.isEmpty())
        return kZeroLogicalPx;

    qreal totalDelta = kZeroLogicalPx;
    for(auto it = links.constBegin(); it != links.constEnd(); ++it)
        totalDelta += (it.value()->cy - it.key()->cy);
    return totalDelta / links.size();
}

// --------------------------------------------------------------------------
QTransform PinoutWidget::fitTransform(const QRectF &contentBounds, qreal marginPx) const
{
    const qreal availableW = qMax<qreal>(kMinimumAvailablePaintSize, width() - kHalfDivisor * marginPx);
    const qreal availableH = qMax<qreal>(kMinimumAvailablePaintSize, height() - kHalfDivisor * marginPx);
    const qreal fitScale = qMin(availableW / contentBounds.width(),
                                availableH / contentBounds.height());

    QTransform transform;
    transform.translate(marginPx + (availableW - fitScale * contentBounds.width()) / kHalfDivisor - fitScale * contentBounds.left(),
                        marginPx + (availableH - fitScale * contentBounds.height()) / kHalfDivisor - fitScale * contentBounds.top());
    transform.scale(fitScale, fitScale);
    return transform;
}

// --------------------------------------------------------------------------
QTransform PinoutWidget::interactiveTransform(const QRectF &contentBounds, qreal marginPx) const
{
    const QPointF center(width() / kHalfDivisor, height() / kHalfDivisor);
    QTransform transform = fitTransform(contentBounds, marginPx);
    QTransform view;
    view.translate(center.x() + m_panOffset.x(), center.y() + m_panOffset.y());
    view.scale(m_zoom, m_zoom);
    view.translate(-center.x(), -center.y());
    return transform * view;
}

// --------------------------------------------------------------------------
void PinoutWidget::resetView()
{
    m_zoom = kLogicalScale;
    m_panOffset = QPointF();
    m_lastDragPos = QPointF();
    m_dragging = false;
    update();
}

// --------------------------------------------------------------------------
QString PinoutWidget::overlayLabelText(const QString &label) const
{
    static const QRegularExpression channelSuffix(QStringLiteral("^(?:[A-Za-z]+\\s*)+(\\d+)\\s*$"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = channelSuffix.match(label.trimmed());
    if(match.hasMatch())
        return match.captured(kOverlayChannelCaptureGroup);

    return label;
}

// --------------------------------------------------------------------------
void PinoutWidget::setPinFunctions(const QList<PinFunctionInfo> &functions)
{
    m_functionMap.setFunctions(functions);
    m_iconCache.clear();
    update();
}

// --------------------------------------------------------------------------
void PinoutWidget::clearPinFunctions()
{
    m_functionMap.clear();
    m_iconCache.clear();
    update();
}

// --------------------------------------------------------------------------
void PinoutWidget::onThemeChanged()
{
    m_iconCache.clear();
    update();
}

// --------------------------------------------------------------------------
PinoutWidget::PaintColors PinoutWidget::buildPaintColors() const
{
    const ThemePalette &pal = Graphics::palette();
    const QColor bgColor(pal.windowWidget);

    PaintColors colors;
    colors.bgColor = bgColor;
    colors.portLabelColor = QColor(pal.textAll);
    colors.arduinoLabelColor = QColor(kArduinoLabelRed, kArduinoLabelGreen, kArduinoLabelBlue);
    colors.activeFunctionColor = QColor(kActiveFunctionRed, kActiveFunctionGreen, kActiveFunctionBlue);
    colors.boxOutlineColor = QColor(kBoxOutlineComponent, kBoxOutlineComponent, kBoxOutlineComponent);
    colors.stubColor = bgColor.lightnessF() < kDarkBackgroundLightnessLimit
                       ? QColor(kDarkStubColorRed, kDarkStubColorGreen, kDarkStubColorBlue)
                       : QColor(kLightStubColorRed, kLightStubColorGreen, kLightStubColorBlue);
    return colors;
}

// --------------------------------------------------------------------------
PinoutWidget::PaintMetrics PinoutWidget::buildPaintMetrics(const QFont &baseFont) const
{
    PaintMetrics metrics;
    metrics.canvasScale = kLogicalScale;
    if(metrics.canvasScale <= kZeroLogicalPx)
        return metrics;

    metrics.uiScale = kMinimumUiScale;
    metrics.invS = kLogicalScale;
    metrics.iconSize = kIconSizeLogicalPx * metrics.uiScale * metrics.invS;
    metrics.outerStubLen = (usesSeparatedPinLabels()
                            ? kSeparatedOuterStubLengthLogicalPx
                            : kOuterStubLengthLogicalPx) * metrics.uiScale * metrics.invS;
    metrics.stubStartGap = kStubStartGapLogicalPx * metrics.invS;
    metrics.groupGap = kGroupGapLogicalPx * metrics.uiScale * metrics.invS;
    metrics.columnGap = kColumnGapLogicalPx * metrics.uiScale * metrics.invS;
    metrics.overlayGap = kOverlayGapLogicalPx * metrics.uiScale * metrics.invS;
    metrics.outerTextGap = kOuterTextGapLogicalPx * metrics.uiScale * metrics.invS;
    metrics.innerTextGap = kInnerTextGapLogicalPx * metrics.uiScale * metrics.invS;
    metrics.showFunctionLabels = DISPLAY_PINOUT_CHANNEL;
    metrics.showFunctionIcons = true;

    metrics.labelFont = baseFont;
    metrics.labelFont.setPixelSize(qMax(kLabelFontMinimumPx,
                                        (int)qRound(kLabelFontLogicalPx * metrics.uiScale * metrics.invS)));
    metrics.labelFm = QFontMetricsF(metrics.labelFont);

    metrics.connectorFont = metrics.labelFont;
    metrics.connectorFont.setWeight(QFont::Normal);
    metrics.connectorFont.setPixelSize(qMax(kConnectorFontMinimumPx,
                                            (int)qRound(kConnectorFontLogicalPx * metrics.uiScale * metrics.invS)));
    metrics.connectorFm = QFontMetricsF(metrics.connectorFont);

    metrics.pinNumberFont = metrics.labelFont;
    metrics.pinNumberFont.setBold(true);
    metrics.pinNumberFont.setPixelSize(qMax(kPinNumberFontMinimumPx,
                                            (int)qRound(kPinNumberFontLogicalPx * metrics.uiScale * metrics.invS)));
    metrics.pinNumberFm = QFontMetricsF(metrics.pinNumberFont);

    metrics.overlayFont = metrics.labelFont;
    metrics.overlayFont.setPixelSize(qMax(kOverlayLabelFontMinimumPx,
                                          (int)qRound(kOverlayLabelFontLogicalPx * metrics.uiScale * metrics.invS)));
    metrics.overlayFm = QFontMetricsF(metrics.overlayFont);
    return metrics;
}

// --------------------------------------------------------------------------
QHash<QString, QRectF> PinoutWidget::buildConnectorBodies(float invScale) const
{
    QHash<QString, QRectF> connectorBodies;
    connectorBodies.reserve(m_connectors.size());
    for(const ConnectorDesc &connector : m_connectors)
        connectorBodies.insert(connector.id, bodyRectFor(connector, invScale));
    return connectorBodies;
}

// --------------------------------------------------------------------------
QString PinoutWidget::leftMorphoId() const      { return m_templateId == kNucleo144TemplateId ? kNucleo144LeftMorphoId : kNucleo64LeftMorphoId; }
QString PinoutWidget::rightMorphoId() const     { return m_templateId == kNucleo144TemplateId ? kNucleo144RightMorphoId : kNucleo64RightMorphoId; }
QString PinoutWidget::leftArduinoTopId() const  { return m_templateId == kNucleo144TemplateId ? kNucleo144LeftArduinoTopId : kNucleo64LeftArduinoTopId; }
QString PinoutWidget::leftArduinoBotId() const  { return m_templateId == kNucleo144TemplateId ? kNucleo144LeftArduinoBotId : kNucleo64LeftArduinoBotId; }
QString PinoutWidget::rightArduinoTopId() const { return m_templateId == kNucleo144TemplateId ? kNucleo144RightArduinoTopId : kNucleo64RightArduinoTopId; }
QString PinoutWidget::rightArduinoBotId() const { return m_templateId == kNucleo144TemplateId ? kNucleo144RightArduinoBotId : kNucleo64RightArduinoBotId; }

bool PinoutWidget::usesSeparatedPinLabels() const
{
    return m_templateId == kNucleo144TemplateId;
}

bool PinoutWidget::isLeftMorphoOuter(const PinDesc &pin) const {
    return pin.connectorId == leftMorphoId() && pin.row == kFirstConnectorRow;
}
bool PinoutWidget::isRightMorphoOuter(const PinDesc &pin) const {
    return pin.connectorId == rightMorphoId() && pin.row == kSecondConnectorRow;
}
bool PinoutWidget::isLeftMorphoInner(const PinDesc &pin) const {
    return pin.connectorId == leftMorphoId() && pin.row == kSecondConnectorRow;
}
bool PinoutWidget::isRightMorphoInner(const PinDesc &pin) const {
    return pin.connectorId == rightMorphoId() && pin.row == kFirstConnectorRow;
}
bool PinoutWidget::isLeftArduino(const PinDesc &pin) const {
    return pin.connectorId == leftArduinoTopId() || pin.connectorId == leftArduinoBotId();
}
bool PinoutWidget::isRightArduino(const PinDesc &pin) const {
    return pin.connectorId == rightArduinoTopId() || pin.connectorId == rightArduinoBotId();
}
bool PinoutWidget::isArduinoPin(const PinDesc &pin) const {
    return isLeftArduino(pin) || isRightArduino(pin);
}
QString PinoutWidget::separatedPinLabel(const PinDesc &pin) const {
    if(isArduinoPin(pin) && !pin.arduino.isEmpty())
        return pin.arduino;
    return pin.port;
}

// --------------------------------------------------------------------------
PinoutWidget::ColumnWidths PinoutWidget::computeColumnWidths(const QFontMetricsF &fontMetrics) const
{
    ColumnWidths widths;
    for(const PinDesc &pin : m_pins){
        if(isLeftMorphoOuter(pin))
            widths.leftMorphoWidth = qMax(widths.leftMorphoWidth, fontMetrics.horizontalAdvance(pin.port));
        else if(isRightMorphoOuter(pin))
            widths.rightMorphoWidth = qMax(widths.rightMorphoWidth, fontMetrics.horizontalAdvance(pin.port));
        else if(isLeftArduino(pin)){
            widths.leftPortWidth = qMax(widths.leftPortWidth, fontMetrics.horizontalAdvance(pin.port));
            widths.leftArduinoWidth = qMax(widths.leftArduinoWidth, fontMetrics.horizontalAdvance(pin.arduino));
        } else if(isRightArduino(pin)){
            widths.rightPortWidth = qMax(widths.rightPortWidth, fontMetrics.horizontalAdvance(pin.port));
            widths.rightArduinoWidth = qMax(widths.rightArduinoWidth, fontMetrics.horizontalAdvance(pin.arduino));
        }
    }
    return widths;
}

// --------------------------------------------------------------------------
PinoutWidget::LayoutState PinoutWidget::buildLayoutState(
    const PaintMetrics &metrics,
    const QHash<QString, int> &connectorIndexById) const
{
    LayoutState layout;
    layout.connectorBodies = buildConnectorBodies(metrics.invS);
    layout.columnWidths = computeColumnWidths(metrics.labelFm);

    layout.leftMorphoBody = layout.connectorBodies.value(leftMorphoId());
    layout.rightMorphoBody = layout.connectorBodies.value(rightMorphoId());
    layout.leftArduinoBody = combinedBody(layout.connectorBodies, leftArduinoTopId(), leftArduinoBotId());
    layout.rightArduinoBody = combinedBody(layout.connectorBodies, rightArduinoTopId(), rightArduinoBotId());

    layout.leftMorphoTextRight = layout.leftMorphoBody.left() - metrics.stubStartGap - metrics.outerStubLen - metrics.outerTextGap;
    layout.leftPortTextX = layout.leftArduinoBody.right() + metrics.groupGap;
    layout.leftArduinoTextX = layout.leftPortTextX + layout.columnWidths.leftPortWidth + metrics.columnGap;
    layout.rightPortTextRight = layout.rightArduinoBody.left() - metrics.groupGap;
    layout.rightArduinoTextRight = layout.rightPortTextRight - layout.columnWidths.rightPortWidth - metrics.columnGap;
    layout.rightMorphoTextX = layout.rightMorphoBody.right() + metrics.stubStartGap + metrics.outerStubLen + metrics.outerTextGap;

    layout.leftOuterOverlayX = layout.leftMorphoTextRight - layout.columnWidths.leftMorphoWidth - metrics.overlayGap - metrics.iconSize;
    layout.leftInnerOverlayX = layout.leftArduinoTextX + layout.columnWidths.leftArduinoWidth + metrics.overlayGap;
    layout.rightInnerOverlayX = layout.rightArduinoTextRight - layout.columnWidths.rightArduinoWidth - metrics.overlayGap - metrics.iconSize;
    layout.rightOuterOverlayX = layout.rightMorphoTextX + layout.columnWidths.rightMorphoWidth + metrics.overlayGap;
    layout.leftInnerMorphoTextX = layout.leftPortTextX;
    layout.rightInnerMorphoTextRight = layout.rightPortTextRight;

    const qreal labelOffsetY = kConnectorLabelOffsetYLogicalPx * metrics.uiScale * metrics.invS;
    layout.topLabelY = qMin(layout.leftMorphoBody.top(), layout.rightMorphoBody.top()) - labelOffsetY;
    layout.bottomLabelY = qMax(layout.leftArduinoBody.bottom(), layout.rightArduinoBody.bottom())
                          + metrics.connectorFm.ascent() + labelOffsetY;

    layout.leftInnerMorphoPins = collectPins([this](const PinDesc &candidate) {
        return isLeftMorphoInner(candidate);
    });
    layout.rightInnerMorphoPins = collectPins([this](const PinDesc &candidate) {
        return isRightMorphoInner(candidate);
    });
    layout.leftArduinoPins = collectPins([this](const PinDesc &candidate) {
        return isLeftArduino(candidate);
    });
    layout.rightArduinoPins = collectPins([this](const PinDesc &candidate) {
        return isRightArduino(candidate);
    });

    if(!usesSeparatedPinLabels()){
        layout.leftInnerMorphoLinks = buildMorphoLinks(layout.leftInnerMorphoPins, layout.leftArduinoPins);
        layout.rightInnerMorphoLinks = buildMorphoLinks(layout.rightInnerMorphoPins, layout.rightArduinoPins);
    }
    layout.leftInnerLineDeltaY = averageLinkDeltaY(layout.leftInnerMorphoLinks);
    layout.rightInnerLineDeltaY = averageLinkDeltaY(layout.rightInnerMorphoLinks);

    if(!usesSeparatedPinLabels()){
        for(const PinDesc &candidate : m_pins){
            if(!isRightMorphoInner(candidate))
                continue;
            if(layout.rightInnerMorphoLinks.contains(&candidate))
                continue;
            if(!layout.topRightUnlinkedPin || candidate.cy < layout.topRightUnlinkedPin->cy)
                layout.topRightUnlinkedPin = &candidate;
        }

        for(const PinDesc *candidate : layout.rightArduinoPins){
            if(candidate->connectorId != rightArduinoTopId())
                continue;
            if(!layout.topRightArduinoPin || candidate->cy < layout.topRightArduinoPin->cy)
                layout.topRightArduinoPin = candidate;
        }

        if(layout.topRightUnlinkedPin && layout.topRightArduinoPin){
            const ConnectorDesc *arduinoConnector = connectorById(layout.topRightArduinoPin->connectorId,
                                                                  connectorIndexById);
            if(arduinoConnector){
                layout.hasTopRightVirtualY = true;
                layout.topRightVirtualY = (qreal)layout.topRightArduinoPin->cy - arduinoConnector->pitch;
            }
        }
    }

    qreal maxOverlayLabelWidth = kZeroLogicalPx;
    if(DISPLAY_PINOUT_CHANNEL){
        for(const PinFunctionInfo &function : m_functionMap.functions()){
            maxOverlayLabelWidth = qMax(maxOverlayLabelWidth,
                                        metrics.overlayFm.horizontalAdvance(overlayLabelText(function.label)));
        }
    }

    const qreal minOverlayLabelWidth = metrics.overlayFm.horizontalAdvance(QStringLiteral("Ref"));
    layout.maxOverlayIconWidth = metrics.iconSize * kMaxOverlayIconWidthFactor;
    layout.overlayTextGap = qMax<qreal>(kOverlayTextGapMinLogicalPx * metrics.uiScale * metrics.invS,
                                        metrics.overlayFm.horizontalAdvance(QString(kOverlayTextGapSpaces,
                                                                                     QLatin1Char(' '))));
    layout.reservedOverlayLabelWidth = DISPLAY_PINOUT_CHANNEL
                                       ? qMax(maxOverlayLabelWidth, minOverlayLabelWidth)
                                       : kZeroLogicalPx;
    layout.reservedOverlayTextGap = DISPLAY_PINOUT_CHANNEL ? layout.overlayTextGap : kZeroLogicalPx;

    layout.contentBounds = layout.leftMorphoBody.united(layout.rightMorphoBody)
                                            .united(layout.leftArduinoBody)
                                            .united(layout.rightArduinoBody);
    if(usesSeparatedPinLabels()){
        const qreal overlayWidth = layout.maxOverlayIconWidth
                                   + layout.reservedOverlayTextGap
                                   + layout.reservedOverlayLabelWidth;
        for(const PinDesc &pin : m_pins){
            const QRectF body = layout.connectorBodies.value(pin.connectorId);
            if(body.isNull())
                continue;

            const QString label = separatedPinLabel(pin);
            const qreal labelWidth = metrics.labelFm.horizontalAdvance(label);
            const qreal baselineY = pinLabelBaseline(metrics.labelFm, pin.cy);
            const bool labelLeft = (pin.row == kFirstConnectorRow);
            const qreal labelX = labelLeft
                                 ? body.left() - metrics.stubStartGap - metrics.outerStubLen - metrics.outerTextGap - labelWidth
                                 : body.right() + metrics.stubStartGap + metrics.outerStubLen + metrics.outerTextGap;

            layout.contentBounds = layout.contentBounds.united(QRectF(labelX,
                                                                      baselineY - metrics.labelFm.ascent(),
                                                                      labelWidth,
                                                                      metrics.labelFm.height()));
            if(labelLeft){
                layout.contentBounds = layout.contentBounds.united(QRectF(labelX - metrics.overlayGap - overlayWidth,
                                                                          baselineY - metrics.overlayFm.ascent(),
                                                                          overlayWidth,
                                                                          metrics.overlayFm.height()));
            } else {
                layout.contentBounds = layout.contentBounds.united(QRectF(labelX + labelWidth + metrics.overlayGap,
                                                                          baselineY - metrics.overlayFm.ascent(),
                                                                          overlayWidth,
                                                                          metrics.overlayFm.height()));
            }
        }
    } else {
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.leftMorphoTextRight - layout.columnWidths.leftMorphoWidth,
                                                                  layout.leftMorphoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.leftMorphoWidth,
                                                                  layout.leftMorphoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.leftPortTextX,
                                                                  layout.leftArduinoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.leftPortWidth,
                                                                  layout.leftArduinoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.leftArduinoTextX,
                                                                  layout.leftArduinoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.leftArduinoWidth,
                                                                  layout.leftArduinoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightMorphoTextX,
                                                                  layout.rightMorphoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.rightMorphoWidth,
                                                                  layout.rightMorphoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightArduinoTextRight - layout.columnWidths.rightArduinoWidth,
                                                                  layout.rightArduinoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.rightArduinoWidth,
                                                                  layout.rightArduinoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightPortTextRight - layout.columnWidths.rightPortWidth,
                                                                  layout.rightArduinoBody.top() - metrics.labelFm.ascent(),
                                                                  layout.columnWidths.rightPortWidth,
                                                                  layout.rightArduinoBody.height() + metrics.labelFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.leftOuterOverlayX - layout.reservedOverlayLabelWidth - layout.reservedOverlayTextGap,
                                                                  layout.leftMorphoBody.top() - metrics.overlayFm.ascent(),
                                                                  layout.reservedOverlayLabelWidth + layout.reservedOverlayTextGap + layout.maxOverlayIconWidth,
                                                                  layout.leftMorphoBody.height() + metrics.overlayFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.leftInnerOverlayX,
                                                                  layout.leftArduinoBody.top() - metrics.overlayFm.ascent(),
                                                                  layout.maxOverlayIconWidth + layout.reservedOverlayTextGap + layout.reservedOverlayLabelWidth,
                                                                  layout.leftArduinoBody.height() + metrics.overlayFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightInnerOverlayX - layout.reservedOverlayLabelWidth - layout.reservedOverlayTextGap,
                                                                  layout.rightArduinoBody.top() - metrics.overlayFm.ascent(),
                                                                  layout.reservedOverlayLabelWidth + layout.reservedOverlayTextGap + layout.maxOverlayIconWidth,
                                                                  layout.rightArduinoBody.height() + metrics.overlayFm.height()));
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightOuterOverlayX,
                                                                  layout.rightMorphoBody.top() - metrics.overlayFm.ascent(),
                                                                  layout.maxOverlayIconWidth + layout.reservedOverlayTextGap + layout.reservedOverlayLabelWidth,
                                                                  layout.rightMorphoBody.height() + metrics.overlayFm.height()));
    }

    for(const ConnectorDesc &connector : m_connectors){
        const QRectF body = layout.connectorBodies.value(connector.id);
        const qreal textWidth = metrics.connectorFm.horizontalAdvance(connector.label);
        const qreal textX = body.center().x() - textWidth / kHalfDivisor;
        const qreal textY = (connector.id == leftArduinoBotId() || connector.id == rightArduinoBotId())
                            ? layout.bottomLabelY
                            : layout.topLabelY;
        layout.contentBounds = layout.contentBounds.united(QRectF(textX,
                                                                  textY - metrics.connectorFm.ascent(),
                                                                  textWidth,
                                                                  metrics.connectorFm.height()));
    }

    if(layout.topRightUnlinkedPin){
        const qreal topRightTextWidth = metrics.labelFm.horizontalAdvance(layout.topRightUnlinkedPin->port);
        const qreal topRightBaseline = rightInnerUnlinkedBaseline(layout,
                                                                  metrics.labelFm,
                                                                  *layout.topRightUnlinkedPin);
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightInnerMorphoTextRight - topRightTextWidth,
                                                                  topRightBaseline - metrics.labelFm.ascent(),
                                                                  topRightTextWidth,
                                                                  metrics.labelFm.height()));
    }

    if(layout.hasTopRightVirtualY){
        layout.contentBounds = layout.contentBounds.united(QRectF(layout.rightArduinoBody.left(),
                                                                  layout.topRightVirtualY,
                                                                  layout.rightArduinoBody.width(),
                                                                  layout.rightArduinoBody.top() - layout.topRightVirtualY));
    }

    layout.contentBounds.adjust(kZeroLogicalPx, kZeroLogicalPx, metrics.outerTextGap, kZeroLogicalPx);
    return layout;
}

// --------------------------------------------------------------------------
QColor PinoutWidget::connectorColor(const QString &connectorId) const
{
    if(connectorId == leftMorphoId() || connectorId == rightMorphoId())
        return QColor(kMorphoConnectorRed, kMorphoConnectorGreen, kMorphoConnectorBlue);
    return QColor(kArduinoLabelRed, kArduinoLabelGreen, kArduinoLabelBlue);
}

// --------------------------------------------------------------------------
qreal PinoutWidget::pinLabelBaseline(const QFontMetricsF &fontMetrics, qreal centerY) const
{
    return centerY + (fontMetrics.ascent() - fontMetrics.descent()) / kHalfDivisor;
}

// --------------------------------------------------------------------------
qreal PinoutWidget::rightInnerUnlinkedCenterY(const LayoutState &layout, const PinDesc &pin) const
{
    if(&pin == layout.topRightUnlinkedPin && layout.hasTopRightVirtualY)
        return layout.topRightVirtualY;
    return (qreal)pin.cy + layout.rightInnerLineDeltaY;
}

// --------------------------------------------------------------------------
qreal PinoutWidget::rightInnerUnlinkedBaseline(const LayoutState &layout,
                                               const QFontMetricsF &fontMetrics,
                                               const PinDesc &pin) const
{
    return pinLabelBaseline(fontMetrics, rightInnerUnlinkedCenterY(layout, pin));
}

// --------------------------------------------------------------------------
void PinoutWidget::drawFunctionOverlay(QPainter &p,
                                       const PaintColors &colors,
                                       const PaintMetrics &metrics,
                                       const LayoutState &layout,
                                       const PinDesc &pin,
                                       const PinFunctionInfo &function) const
{
    const bool isLeftMorpho = (pin.connectorId == leftMorphoId());
    const bool isRightMorpho = (pin.connectorId == rightMorphoId());
    const bool isLeftArduino = (pin.connectorId == leftArduinoTopId() || pin.connectorId == leftArduinoBotId());

    const QString iconKey = function.moduleName;
    const QString overlayLabel = DISPLAY_PINOUT_CHANNEL ? overlayLabelText(function.label)
                                                        : QString();
    if(!m_iconCache.contains(iconKey)){
        const QString iconPath = Graphics::getCommonPath() + "icon_" + iconKey + ".png";
        QPixmap src = Graphics::tintedPixmap(iconPath, colors.activeFunctionColor);
        m_iconCache.insert(iconKey, src);
    }

    const QPixmap &icon = m_iconCache.value(iconKey);
    qreal overlayCenterY = pin.cy;
    if(usesSeparatedPinLabels()){
        const QRectF body = layout.connectorBodies.value(pin.connectorId);
        const QString label = separatedPinLabel(pin);
        const qreal labelWidth = metrics.labelFm.horizontalAdvance(label);
        const bool overlayRight = (pin.row == kSecondConnectorRow);
        const qreal textX = overlayRight
                            ? body.right() + metrics.stubStartGap + metrics.outerStubLen + metrics.outerTextGap
                            : body.left() - metrics.stubStartGap - metrics.outerStubLen - metrics.outerTextGap - labelWidth;
        const qreal iconWidth = icon.isNull()
                                ? metrics.iconSize
                                : metrics.iconSize * ((qreal)icon.width() / icon.height());
        const qreal iconX = overlayRight
                            ? textX + labelWidth + metrics.overlayGap
                            : textX - metrics.overlayGap - iconWidth;
        const qreal iconY = overlayCenterY - metrics.iconSize / kHalfDivisor;

        if(metrics.showFunctionIcons && !icon.isNull()){
            p.drawPixmap(QRectF(iconX, iconY, iconWidth, metrics.iconSize),
                         icon,
                         QRectF(kZeroLogicalPx, kZeroLogicalPx, icon.width(), icon.height()));
        }

        if(metrics.showFunctionLabels){
            p.setFont(metrics.overlayFont);
            p.setPen(colors.activeFunctionColor);
            const qreal textBaseY = pinLabelBaseline(metrics.overlayFm, overlayCenterY);
            if(overlayRight){
                p.drawText(QPointF(iconX + iconWidth + layout.overlayTextGap, textBaseY), overlayLabel);
            } else {
                const qreal labelTextWidth = metrics.overlayFm.horizontalAdvance(overlayLabel);
                p.drawText(QPointF(iconX - layout.overlayTextGap - labelTextWidth, textBaseY), overlayLabel);
            }
            p.setFont(metrics.labelFont);
        }
        return;
    }

    if(isLeftMorpho && pin.row == kSecondConnectorRow && !layout.leftInnerMorphoLinks.contains(&pin))
        overlayCenterY += layout.leftInnerLineDeltaY;
    else if(isRightMorpho && pin.row == kFirstConnectorRow && !layout.rightInnerMorphoLinks.contains(&pin))
        overlayCenterY = rightInnerUnlinkedCenterY(layout, pin);

    bool overlayRight = true;
    qreal iconX = kZeroLogicalPx;
    if(isLeftMorpho){
        if(pin.row == kFirstConnectorRow){
            overlayRight = false;
            iconX = layout.leftOuterOverlayX;
        } else {
            iconX = layout.leftInnerOverlayX;
        }
    } else if(isRightMorpho){
        if(pin.row == kSecondConnectorRow){
            iconX = layout.rightOuterOverlayX;
        } else {
            overlayRight = false;
            iconX = layout.rightInnerOverlayX;
        }
    } else if(isLeftArduino){
        iconX = layout.leftInnerOverlayX;
    } else {
        overlayRight = false;
        iconX = layout.rightInnerOverlayX;
    }

    const qreal iconY = overlayCenterY - metrics.iconSize / kHalfDivisor;
    const qreal iconWidth = icon.isNull()
                            ? metrics.iconSize
                            : metrics.iconSize * ((qreal)icon.width() / icon.height());
    if(metrics.showFunctionIcons && !icon.isNull()){
        p.drawPixmap(QRectF(iconX, iconY, iconWidth, metrics.iconSize),
                     icon,
                     QRectF(kZeroLogicalPx, kZeroLogicalPx, icon.width(), icon.height()));
    }

    if(metrics.showFunctionLabels){
        p.setFont(metrics.overlayFont);
        p.setPen(colors.activeFunctionColor);
        const qreal textBaseY = pinLabelBaseline(metrics.overlayFm, overlayCenterY);
        if(overlayRight){
            p.drawText(QPointF(iconX + iconWidth + layout.overlayTextGap, textBaseY), overlayLabel);
        } else {
            const qreal labelWidth = metrics.overlayFm.horizontalAdvance(overlayLabel);
            p.drawText(QPointF(iconX - layout.overlayTextGap - labelWidth, textBaseY), overlayLabel);
        }
        p.setFont(metrics.labelFont);
    }
}

// --------------------------------------------------------------------------
void PinoutWidget::drawConnectorLabels(QPainter &p,
                                       const PaintColors &colors,
                                       const PaintMetrics &metrics,
                                       const LayoutState &layout) const
{
    p.setFont(metrics.connectorFont);
    for(const ConnectorDesc &connector : m_connectors){
        if(!layout.connectorBodies.contains(connector.id))
            continue;

        const QRectF body = layout.connectorBodies.value(connector.id);
        const qreal textWidth = metrics.connectorFm.horizontalAdvance(connector.label);
        const qreal textX = body.center().x() - textWidth / kHalfDivisor;
        const bool isArduino = (connector.id == leftArduinoTopId() || connector.id == leftArduinoBotId()
                                || connector.id == rightArduinoTopId() || connector.id == rightArduinoBotId());
        const qreal textY = (connector.id == leftArduinoBotId() || connector.id == rightArduinoBotId())
                            ? layout.bottomLabelY
                            : layout.topLabelY;

        p.setPen(isArduino ? colors.arduinoLabelColor : colors.portLabelColor.darker(kInactivePortLabelDarkerFactor));
        p.drawText(QPointF(textX, textY), connector.label);
    }
}

// --------------------------------------------------------------------------
void PinoutWidget::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton){
        m_dragging = true;
        m_lastDragPos = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

// --------------------------------------------------------------------------
void PinoutWidget::mouseMoveEvent(QMouseEvent *event)
{
    if(m_dragging){
        const QPointF pos = event->position();
        m_panOffset += (pos - m_lastDragPos) * kPanSensitivity;
        m_lastDragPos = pos;
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

// --------------------------------------------------------------------------
void PinoutWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton && m_dragging){
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }

    if(event->button() == Qt::RightButton){
        emit togglePinoutRequested();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

// --------------------------------------------------------------------------
void PinoutWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton){
        resetView();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

// --------------------------------------------------------------------------
void PinoutWidget::wheelEvent(QWheelEvent *event)
{
    const QPoint numDegrees = event->angleDelta() / kWheelAngleDeltaDegreesDivisor;
    if(numDegrees.isNull()){
        QWidget::wheelEvent(event);
        return;
    }

    const qreal oldZoom = m_zoom;
    const qreal steps = numDegrees.y() / kWheelDegreesPerStep;
    const qreal zoomFactor = std::pow(kWheelZoomBase, steps);
    m_zoom = qBound<qreal>(kMinZoom, m_zoom * zoomFactor, kMaxZoom);

    const QPointF anchor = event->position();
    if(!qFuzzyCompare(oldZoom, kZeroLogicalPx) && !qFuzzyCompare(oldZoom, m_zoom)){
        const QPointF center(width() / kHalfDivisor, height() / kHalfDivisor);
        const qreal zoomRatio = m_zoom / oldZoom;
        m_panOffset = anchor - center - zoomRatio * (anchor - center - m_panOffset);
        update();
    }

    event->accept();
}

void PinoutWidget::paintEvent(QPaintEvent *)
{
    const PaintColors colors = buildPaintColors();

    QPainter p(this);
    if(m_connectors.isEmpty()){
        p.fillRect(rect(), colors.bgColor);
        return;
    }

    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    p.fillRect(rect(), colors.bgColor);

    const PaintMetrics metrics = buildPaintMetrics(p.font());
    if(metrics.canvasScale <= kZeroLogicalPx)
        return;

    p.setFont(metrics.labelFont);
    const QFontMetricsF &fm = metrics.labelFm;
    const QFontMetricsF &pinNoFm = metrics.pinNumberFm;

    const QHash<QString, int> connectorIndexById = buildConnectorIndex();

    const LayoutState layout = buildLayoutState(metrics, connectorIndexById);
    p.setTransform(interactiveTransform(layout.contentBounds, kFitMarginPx));

    auto drawLeftColumnText = [&](const QString &txt, qreal x, qreal baselineY, const QColor &col) {
        if(txt.isEmpty())
            return;
        p.setPen(col);
        p.drawText(QPointF(x, baselineY), txt);
    };

    auto drawRightColumnText = [&](const QString &txt, qreal rightX, qreal baselineY, const QColor &col) {
        if(txt.isEmpty())
            return;
        p.setPen(col);
        const qreal tw = fm.horizontalAdvance(txt);
        p.drawText(QPointF(rightX - tw, baselineY), txt);
    };

    // Keep clean visual style close to the official image: no cross-board net spaghetti.

    // ------ Draw pins -------------------------------------------------------
    for(const PinDesc &pin : m_pins){
        if(pin.cx == kUnsetPinCoordinate && pin.cy == kUnsetPinCoordinate) continue;

        const ConnectorDesc *connector = connectorById(pin.connectorId, connectorIndexById);
        if(!connector)
            continue;

        const float padW = padSideFor(*connector, metrics.invS);
        const float padH = padW;
        const bool isSingleRow = (connector->rows == kSingleConnectorRows);
        const bool isLeftArduino = (pin.connectorId == leftArduinoTopId() || pin.connectorId == leftArduinoBotId());
        const bool isRightArduino = (pin.connectorId == rightArduinoTopId() || pin.connectorId == rightArduinoBotId());
        const bool isLeftMorpho = (pin.connectorId == leftMorphoId());
        const bool isRightMorpho = (pin.connectorId == rightMorphoId());
        const bool isLinkedMorphoDuplicate = !usesSeparatedPinLabels()
            && ((isLeftMorpho && pin.row == kSecondConnectorRow && layout.leftInnerMorphoLinks.contains(&pin))
                || (isRightMorpho && pin.row == kFirstConnectorRow && layout.rightInnerMorphoLinks.contains(&pin)));

        const PinFunctionInfo *func = m_functionMap.pinFunction(pin.port, pin.arduino);
        const bool hasActiveFunction = (func != nullptr);

        // Draw the official-style connector lines first so the pads can sit on top.
        p.setPen(QPen(colors.stubColor, kStubLineWidthLogicalPx * metrics.uiScale * metrics.invS, Qt::SolidLine, Qt::RoundCap));

        if(usesSeparatedPinLabels() && !isSingleRow){
            if(pin.row == kFirstConnectorRow){
                p.drawLine(QPointF(pin.cx - padW / kHalfDivisor - metrics.stubStartGap, pin.cy),
                           QPointF(pin.cx - padW / kHalfDivisor - metrics.stubStartGap - metrics.outerStubLen, pin.cy));
            } else {
                p.drawLine(QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap, pin.cy),
                           QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap + metrics.outerStubLen, pin.cy));
            }
        } else if(!isSingleRow){
            if(isLeftMorpho && pin.row == kFirstConnectorRow){
                p.drawLine(QPointF(pin.cx - padW / kHalfDivisor - metrics.stubStartGap, pin.cy),
                           QPointF(pin.cx - padW / kHalfDivisor - metrics.stubStartGap - metrics.outerStubLen, pin.cy));
            } else if(isRightMorpho && pin.row == kSecondConnectorRow){
                p.drawLine(QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap, pin.cy),
                           QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap + metrics.outerStubLen, pin.cy));
            } else if(isLeftMorpho){
                const PinDesc *arduinoPin = layout.leftInnerMorphoLinks.value(&pin, nullptr);
                if(arduinoPin){
                    const ConnectorDesc *arduinoConnector = connectorById(arduinoPin->connectorId, connectorIndexById);
                    const float arduinoPadW = arduinoConnector ? padSideFor(*arduinoConnector, metrics.invS) : kFallbackArduinoPadLogicalPx * metrics.invS;
                    p.drawLine(QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap, pin.cy),
                               QPointF(arduinoPin->cx - arduinoPadW / kHalfDivisor - metrics.stubStartGap, arduinoPin->cy));
                } else {
                    p.drawLine(QPointF(pin.cx + padW / kHalfDivisor + metrics.stubStartGap, pin.cy),
                               QPointF(layout.leftPortTextX - metrics.innerTextGap,
                                       pin.cy + layout.leftInnerLineDeltaY));
                }
            } else if(isRightMorpho){
                const PinDesc *arduinoPin = layout.rightInnerMorphoLinks.value(&pin, nullptr);
                if(arduinoPin){
                    const ConnectorDesc *arduinoConnector = connectorById(arduinoPin->connectorId, connectorIndexById);
                    const float arduinoPadW = arduinoConnector ? padSideFor(*arduinoConnector, metrics.invS) : kFallbackArduinoPadLogicalPx * metrics.invS;
                    p.drawLine(QPointF(arduinoPin->cx + arduinoPadW / kHalfDivisor + metrics.stubStartGap, arduinoPin->cy),
                               QPointF(pin.cx - padW / kHalfDivisor - metrics.stubStartGap, pin.cy));
                } else {
                    const QPointF pinPoint(pin.cx - padW / kHalfDivisor - metrics.stubStartGap, pin.cy);
                    const qreal targetY = rightInnerUnlinkedCenterY(layout, pin);
                    const qreal lineEndX = layout.rightInnerMorphoTextRight + metrics.outerTextGap;
                    if(&pin == layout.topRightUnlinkedPin && layout.topRightArduinoPin && layout.hasTopRightVirtualY){
                        const ConnectorDesc *arduinoConnector = connectorById(layout.topRightArduinoPin->connectorId,
                                                                              connectorIndexById);
                        if(arduinoConnector){
                            const float arduinoPadW = padSideFor(*arduinoConnector, metrics.invS);
                            const QPointF bendPoint(layout.topRightArduinoPin->cx + arduinoPadW / kHalfDivisor + metrics.stubStartGap,
                                                    targetY);
                            const QPointF textPoint(lineEndX, targetY);
                            p.drawLine(pinPoint, bendPoint);
                            p.drawLine(bendPoint, textPoint);
                        }
                    } else {
                        const qreal angledRun = qMin((pinPoint.x() - lineEndX) * kUnlinkedLineHorizontalFactor,
                                                     qAbs(targetY - pin.cy) / kUnlinkedLineSlopeDivisor);
                        const qreal bendX = pinPoint.x() - angledRun;

                        const QPointF bendPoint(bendX, targetY);
                        const QPointF textPoint(lineEndX, targetY);
                        p.drawLine(pinPoint, bendPoint);
                        p.drawLine(bendPoint, textPoint);
                    }
                }
            }
        }

        // Draw numbered rectangular pin pad.
        const QColor padFill = hasActiveFunction ? colors.activeFunctionColor : connectorColor(pin.connectorId);
        const QRectF pinRect(pin.cx - padW / kHalfDivisor, pin.cy - padH / kHalfDivisor, padW, padH);
        p.setBrush(padFill);
        p.setPen(QPen(colors.boxOutlineColor, kPinOutlineWidthLogicalPx * metrics.invS));
        p.drawRect(pinRect);

        int pinNo = kFirstPinIndex;
        if(!isSingleRow)
            pinNo = pin.index * kPinNumberRowsPerIndex + pin.row + kOneBasedPinOffset;
        else if(isRightArduino)
            pinNo = connector->pinCount - pin.index;
        else
            pinNo = pin.index + kOneBasedPinOffset;

        p.setFont(metrics.pinNumberFont);
        p.setPen(QColor(kPinNumberTextComponent, kPinNumberTextComponent, kPinNumberTextComponent));
        const QString pinNoTxt = QString::number(pinNo);
        const qreal noW = pinNoFm.horizontalAdvance(pinNoTxt);
        const qreal noX = pin.cx - noW / kHalfDivisor;
        const qreal noY = pinLabelBaseline(pinNoFm, pin.cy);
        p.drawText(QPointF(noX, noY), pinNoTxt);
        p.setFont(metrics.labelFont);

        // --- Labels ---------------------------------------------------------
        const qreal centerBaseline = pinLabelBaseline(fm, pin.cy);

        if(usesSeparatedPinLabels() && !isSingleRow){
            const QRectF body = layout.connectorBodies.value(pin.connectorId);
            const QString label = separatedPinLabel(pin);
            const QColor labelColor = isArduinoPin(pin) ? colors.arduinoLabelColor : colors.portLabelColor;
            if(pin.row == kFirstConnectorRow){
                drawRightColumnText(label,
                                    body.left() - metrics.stubStartGap - metrics.outerStubLen - metrics.outerTextGap,
                                    centerBaseline,
                                    labelColor);
            } else {
                drawLeftColumnText(label,
                                   body.right() + metrics.stubStartGap + metrics.outerStubLen + metrics.outerTextGap,
                                   centerBaseline,
                                   labelColor);
            }
        } else if(!isSingleRow){
            if(isLeftMorpho && pin.row == kFirstConnectorRow)
                drawRightColumnText(pin.port, layout.leftMorphoTextRight, centerBaseline, colors.portLabelColor);
            else if(isLeftMorpho && !layout.leftInnerMorphoLinks.contains(&pin))
                drawLeftColumnText(pin.port,
                                   layout.leftInnerMorphoTextX,
                                   centerBaseline + layout.leftInnerLineDeltaY,
                                   colors.portLabelColor);
            else if(isRightMorpho && pin.row == kSecondConnectorRow)
                drawLeftColumnText(pin.port, layout.rightMorphoTextX, centerBaseline, colors.portLabelColor);
            else if(isRightMorpho && !layout.rightInnerMorphoLinks.contains(&pin))
                drawRightColumnText(pin.port,
                                    layout.rightInnerMorphoTextRight,
                                    rightInnerUnlinkedBaseline(layout, fm, pin),
                                    colors.portLabelColor);
        } else if(isLeftArduino){
            drawLeftColumnText(pin.port, layout.leftPortTextX, centerBaseline, colors.portLabelColor);
            drawLeftColumnText(pin.arduino, layout.leftArduinoTextX, centerBaseline, colors.arduinoLabelColor);
        } else if(isRightArduino){
            drawRightColumnText(pin.arduino, layout.rightArduinoTextRight, centerBaseline, colors.arduinoLabelColor);
            drawRightColumnText(pin.port, layout.rightPortTextRight, centerBaseline, colors.portLabelColor);
        }

        // --- Function overlay icon + channel label --------------------------
        if(func && !isLinkedMorphoDuplicate)
            drawFunctionOverlay(p, colors, metrics, layout, pin, *func);
    }

    drawConnectorLabels(p, colors, metrics, layout);
}
