#include "video/CompositorWindow.h"

#include "video/Layer.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>

#include <algorithm>

namespace quewi::video {

namespace {

// Identity quad in normalised window space.
QPolygonF identityQuad() {
    return QPolygonF{ {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0} };
}

bool quadIsIdentity(const QPolygonF &q) {
    if (q.size() != 4) return true;
    auto fuzzy = [](const QPointF &a, const QPointF &b) {
        return std::abs(a.x() - b.x()) < 1e-4 && std::abs(a.y() - b.y()) < 1e-4;
    };
    const auto id = identityQuad();
    return fuzzy(q[0], id[0]) && fuzzy(q[1], id[1])
        && fuzzy(q[2], id[2]) && fuzzy(q[3], id[3]);
}

} // namespace

CompositorWindow::CompositorWindow(int screenIndex, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    , m_screenIndex(screenIndex)
    , m_pin(identityQuad())
    , m_pinIsIdentity(true)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setStyleSheet(QStringLiteral("background:#000000;"));
    // Follow display changes. These windows persist across the whole show
    // (hold-black keeps them alive after a cue ends), so without this they'd
    // keep stale geometry when a projector renegotiates resolution, a display
    // is added/removed, or screens are reordered — landing on the wrong
    // monitor or the operator's primary display.
    connect(qGuiApp, &QGuiApplication::screenAdded,    this, [this](QScreen *){ layoutOnScreen(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved,  this, [this](QScreen *){ layoutOnScreen(); });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *){ layoutOnScreen(); });
    layoutOnScreen();
}

CompositorWindow::~CompositorWindow() = default;

void CompositorWindow::addLayer(Layer *layer)
{
    if (!layer) return;
    layer->setParent(this);
    m_layers.append(layer);
    connect(layer, &Layer::frameAvailable, this, qOverload<>(&QWidget::update));
    connect(layer, &Layer::changed,        this, qOverload<>(&QWidget::update));
    update();
}

void CompositorWindow::removeLayer(Layer *layer)
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].data() == layer) {
            if (auto *l = m_layers[i].data()) {
                l->teardown();
                l->deleteLater();
            }
            m_layers.removeAt(i);
            update();
            return;
        }
    }
}

void CompositorWindow::setCornerPin(const QPolygonF &quad)
{
    if (quad.size() != 4) return;
    m_pin = quad;
    m_pinIsIdentity = quadIsIdentity(quad);
    rebuildPinTransform();
    update();
}

void CompositorWindow::resetCornerPin()
{
    setCornerPin(identityQuad());
}

void CompositorWindow::rebuildPinTransform()
{
    if (m_pinIsIdentity) {
        m_pinTransform = QTransform();
        return;
    }
    // Map the unit square corners to the user-supplied quad in window
    // pixel space. QTransform::quadToQuad does the perspective math.
    const QPolygonF src = identityQuad();
    QPolygonF dst;
    const QSizeF sz = size();
    for (const auto &p : m_pin)
        dst << QPointF(p.x() * sz.width(), p.y() * sz.height());

    QTransform t;
    if (QTransform::quadToQuad(QPolygonF{ {0,0}, {sz.width(),0},
                                          {sz.width(),sz.height()}, {0,sz.height()} },
                               dst, t)) {
        m_pinTransform = t;
    } else {
        m_pinTransform = QTransform();
        m_pinIsIdentity = true;
    }
}

void CompositorWindow::layoutOnScreen()
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return;
    const int idx = std::clamp(m_screenIndex, 0,
                               static_cast<int>(screens.size()) - 1);
    QScreen *target = screens.value(idx, screens.first());

    // Re-wire to THIS screen's geometryChanged — the target can change when
    // displays are reordered, and a projector waking up renegotiates its
    // resolution, which we must follow.
    QObject::disconnect(m_screenGeomConn);
    if (target)
        m_screenGeomConn = connect(target, &QScreen::geometryChanged, this,
                                   [this](const QRect &){ layoutOnScreen(); });

    setGeometry(target->geometry());
    rebuildPinTransform();
}

void CompositorWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // Smooth pixmap transform — without this, drawImage uses
    // nearest-neighbor scaling when the source resolution doesn't
    // match the layer's destination rect. That's noticeably blocky on
    // 1080p video projected at 4K, or anything corner-pinned. Bilinear
    // is the right default; the GPU handles it cheaply.
    // Antialiasing helps the cornpin warp's edges look clean too.
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing,          true);

    p.fillRect(rect(), Qt::black);

    if (!m_pinIsIdentity) {
        p.setTransform(m_pinTransform);
    }

    // Sort layers ascending by z-order so higher z paints last (top).
    auto sorted = m_layers;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPointer<Layer> &a, const QPointer<Layer> &b) {
                  return (a ? a->zOrder() : 0) < (b ? b->zOrder() : 0);
              });

    const QRectF winRect = rect();
    for (const auto &lp : sorted) {
        Layer *layer = lp.data();
        if (!layer) continue;
        const QImage frame = layer->currentFrame();
        if (frame.isNull()) continue;

        const QRectF g = layer->geometry();
        const QRectF dst(g.x() * winRect.width(),
                         g.y() * winRect.height(),
                         g.width()  * winRect.width(),
                         g.height() * winRect.height());

        p.setOpacity(layer->opacity());
        p.drawImage(dst, frame);
    }
    p.setOpacity(1.0);
}

} // namespace quewi::video
