#include "ui/OscMonitor.h"

#include "osc/OscMessage.h"

#include <QCheckBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

QString hexDump(const QByteArray &b)
{
    QString out;
    out.reserve(b.size() * 4);
    for (int row = 0; row < b.size(); row += 16) {
        QString hex, ascii;
        for (int i = 0; i < 16 && row + i < b.size(); ++i) {
            const auto byte = static_cast<quint8>(b[row + i]);
            hex += QStringLiteral("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
            ascii += (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QChar('.');
        }
        out += QStringLiteral("%1  %2  %3\n")
            .arg(row, 4, 16, QChar('0'))
            .arg(hex.leftJustified(48, ' '))
            .arg(ascii);
    }
    return out;
}

QString argSummary(const osc::Message &m)
{
    using T = osc::Argument::Tag;
    QString out;
    for (const auto &a : m.args) {
        if (!out.isEmpty()) out += QStringLiteral(", ");
        switch (a.tag) {
        case T::Int32:    out += QString::number(std::get<qint32>(a.value)); break;
        case T::Float32:  out += QString::number(std::get<float>(a.value), 'g', 6); break;
        case T::String:   out += QStringLiteral("\"%1\"").arg(std::get<QString>(a.value)); break;
        case T::Blob:     out += QStringLiteral("<blob %1B>").arg(std::get<QByteArray>(a.value).size()); break;
        case T::Int64:    out += QString::number(std::get<qint64>(a.value)) + QStringLiteral("L"); break;
        case T::TimeTag:  out += QStringLiteral("@%1").arg(std::get<osc::TimeTag>(a.value).ntp, 16, 16, QChar('0')); break;
        case T::Double:   out += QString::number(std::get<double>(a.value), 'g', 12) + QStringLiteral("d"); break;
        case T::Symbol:   out += QStringLiteral("`%1").arg(std::get<QString>(a.value)); break;
        case T::Char:     out += QStringLiteral("'%1'").arg(QChar(std::get<qint32>(a.value))); break;
        case T::RgbaColor: {
            const auto c = std::get<QColor>(a.value);
            out += QStringLiteral("#%1%2%3%4")
                .arg(c.red(),   2, 16, QChar('0'))
                .arg(c.green(), 2, 16, QChar('0'))
                .arg(c.blue(),  2, 16, QChar('0'))
                .arg(c.alpha(), 2, 16, QChar('0'));
            break;
        }
        case T::Midi: {
            const auto mm = std::get<osc::MidiMessage>(a.value);
            out += QStringLiteral("midi[%1 %2 %3 %4]")
                .arg(mm.portId).arg(mm.status).arg(mm.data1).arg(mm.data2);
            break;
        }
        case T::True:      out += QStringLiteral("true"); break;
        case T::False:     out += QStringLiteral("false"); break;
        case T::Nil:       out += QStringLiteral("nil"); break;
        case T::Infinitum: out += QStringLiteral("inf"); break;
        case T::Array:     out += QStringLiteral("[…]"); break;
        }
    }
    return out;
}

QString transportLabel(osc::Transport t)
{
    switch (t) {
    case osc::Transport::Udp:       return QStringLiteral("UDP");
    case osc::Transport::TcpSlip:   return QStringLiteral("TCP");
    case osc::Transport::WebSocket: return QStringLiteral("WS");
    }
    return {};
}

} // namespace

OscMonitor::OscMonitor(osc::OscEngine *engine, QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
{
    setWindowTitle(tr("OSC Monitor"));
    resize(960, 640);

    auto *outer = new QVBoxLayout(this);
    // 16 px outer margins to match the other dialogs (Preferences,
    // Preflight, Notifications). Spacing 14 between the listener
    // group, the table/detail splitter, and the filter row keeps
    // sections visually separated without floating apart.
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(14);

    // Top: listener controls
    auto *listenGroup = new QGroupBox(tr("Inbound listeners"), this);
    auto *listenRow = new QHBoxLayout(listenGroup);
    listenRow->setSpacing(8);

    m_udpPort = new QSpinBox(listenGroup);
    m_udpPort->setRange(1, 65535);
    m_udpPort->setValue(53000);
    m_udpToggle = new QPushButton(tr("Listen UDP"), listenGroup);
    m_udpToggle->setCheckable(true);
    listenRow->addWidget(new QLabel(tr("UDP"), listenGroup));
    listenRow->addWidget(m_udpPort);
    listenRow->addWidget(m_udpToggle);

    m_tcpPort = new QSpinBox(listenGroup);
    m_tcpPort->setRange(1, 65535);
    m_tcpPort->setValue(53001);
    m_tcpToggle = new QPushButton(tr("Listen TCP"), listenGroup);
    m_tcpToggle->setCheckable(true);
    listenRow->addSpacing(12);
    listenRow->addWidget(new QLabel(tr("TCP"), listenGroup));
    listenRow->addWidget(m_tcpPort);
    listenRow->addWidget(m_tcpToggle);

    m_wsPort = new QSpinBox(listenGroup);
    m_wsPort->setRange(1, 65535);
    m_wsPort->setValue(53002);
    m_wsToggle = new QPushButton(tr("Listen WS"), listenGroup);
    m_wsToggle->setCheckable(true);
    listenRow->addSpacing(12);
    listenRow->addWidget(new QLabel(tr("WS"), listenGroup));
    listenRow->addWidget(m_wsPort);
    listenRow->addWidget(m_wsToggle);

    listenRow->addStretch(1);
    m_statusLabel = new QLabel(tr("All listeners stopped"), listenGroup);
    listenRow->addWidget(m_statusLabel);

    outer->addWidget(listenGroup);

    // Middle: split table over detail
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_table = new QTableWidget(0, 6, splitter);
    m_table->setHorizontalHeaderLabels({
        tr("Time"), tr("Dir"), tr("Transport"), tr("Peer"), tr("Address"), tr("Args")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Uniform rows + divider — same treatment as the main cue list
    // and the Preflight results panel. The table already has the
    // QHeaderView::section styling from the central QSS.
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);

    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setPlaceholderText(tr("Select a row to see decoded payload and hex dump."));
    QFont mono = m_detail->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("JetBrains Mono"));
    m_detail->setFont(mono);

    splitter->addWidget(m_table);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);

    // Bottom: filter / pause / clear
    auto *bottom = new QHBoxLayout();
    bottom->setSpacing(8);
    auto *filterLabel = new QLabel(tr("Filter:"), this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("substring of address — e.g. /cue or eos"));
    m_filterEdit->setClearButtonEnabled(true);
    m_pauseBtn = new QPushButton(tr("Pause"), this);
    m_pauseBtn->setCheckable(true);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    bottom->addWidget(filterLabel);
    bottom->addWidget(m_filterEdit, 1);
    bottom->addWidget(m_pauseBtn);
    bottom->addWidget(m_clearBtn);
    outer->addLayout(bottom);

    connect(m_engine, &osc::OscEngine::packetSeen, this, &OscMonitor::onPacketSeen);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &OscMonitor::onRowSelected);
    connect(m_clearBtn, &QPushButton::clicked, this, &OscMonitor::onClearClicked);
    connect(m_pauseBtn, &QPushButton::toggled, this, &OscMonitor::onPauseToggled);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &t){
        m_filter = t;
        applyFilter();
    });
    connect(m_udpToggle, &QPushButton::toggled, this, &OscMonitor::onUdpToggle);
    connect(m_tcpToggle, &QPushButton::toggled, this, &OscMonitor::onTcpToggle);
    connect(m_wsToggle,  &QPushButton::toggled, this, &OscMonitor::onWsToggle);
}

OscMonitor::~OscMonitor() = default;

void OscMonitor::onPacketSeen(const osc::PacketEvent &event)
{
    if (m_paused) return;

    m_events.push_back(event);
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    const auto time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const auto dir  = event.direction == osc::Direction::Outbound ? QStringLiteral("→") : QStringLiteral("←");
    const auto peer = QStringLiteral("%1:%2").arg(event.peerHost, QString::number(event.peerPort));

    QString address, args;
    if (event.parseOk && std::holds_alternative<osc::Message>(event.parsed)) {
        const auto &m = std::get<osc::Message>(event.parsed);
        address = m.address;
        args = argSummary(m);
    } else if (event.parseOk && std::holds_alternative<osc::Bundle>(event.parsed)) {
        address = QStringLiteral("#bundle");
        const auto &b = std::get<osc::Bundle>(event.parsed);
        args = tr("%1 elements").arg(b.elements.size());
    } else {
        address = tr("(parse error)");
    }

    auto add = [&](int col, const QString &t, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
        auto *item = new QTableWidgetItem(t);
        item->setTextAlignment(align);
        m_table->setItem(row, col, item);
    };
    add(0, time);
    add(1, dir, Qt::AlignCenter);
    add(2, transportLabel(event.transport), Qt::AlignCenter);
    add(3, peer);
    add(4, address);
    add(5, args);

    // Hide newly-inserted row immediately if it doesn't match the filter,
    // otherwise the user sees rows blink in only to vanish on next event.
    if (!rowMatchesFilter(row)) m_table->setRowHidden(row, true);

    // Cap the table at 10k rows so a runaway sender doesn't eat memory.
    constexpr int kMaxRows = 10000;
    while (m_table->rowCount() > kMaxRows) {
        m_table->removeRow(0);
        if (!m_events.empty()) m_events.erase(m_events.begin());
    }

    // Auto-scroll to bottom unless the user has scrolled up.
    auto *bar = m_table->verticalScrollBar();
    if (bar->value() == bar->maximum()) {
        m_table->scrollToBottom();
    }
}

void OscMonitor::onRowSelected()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_detail->clear();
        return;
    }
    const int row = rows.first().row();
    if (row < 0 || row >= static_cast<int>(m_events.size())) return;
    appendDetailFor(m_events[row]);
}

void OscMonitor::appendDetailFor(const osc::PacketEvent &event)
{
    QString text;
    text += tr("Direction: %1\n").arg(event.direction == osc::Direction::Outbound ? tr("Outbound") : tr("Inbound"));
    text += tr("Transport: %1\n").arg(transportLabel(event.transport));
    text += tr("Peer:      %1:%2\n").arg(event.peerHost).arg(event.peerPort);
    text += tr("Bytes:     %1\n").arg(event.rawBytes.size());
    text += QStringLiteral("\n");
    if (event.parseOk) {
        std::visit([&](const auto &x) {
            using U = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<U, osc::Message>) {
                text += tr("Address: %1\n").arg(x.address);
                text += tr("Args:    %1\n").arg(argSummary(x));
            } else if constexpr (std::is_same_v<U, osc::Bundle>) {
                text += tr("Bundle, %1 elements, time tag 0x%2\n")
                    .arg(x.elements.size())
                    .arg(x.timeTag.ntp, 16, 16, QChar('0'));
            }
        }, event.parsed);
    } else {
        text += tr("(failed to parse as OSC)\n");
    }
    text += QStringLiteral("\n");
    text += hexDump(event.rawBytes);
    m_detail->setPlainText(text);
}

void OscMonitor::onClearClicked()
{
    m_table->setRowCount(0);
    m_events.clear();
    m_detail->clear();
}

bool OscMonitor::rowMatchesFilter(int row) const
{
    if (m_filter.isEmpty()) return true;
    const auto *addrItem = m_table->item(row, 4);
    if (!addrItem) return true;
    return addrItem->text().contains(m_filter, Qt::CaseInsensitive);
}

void OscMonitor::applyFilter()
{
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->setRowHidden(r, !rowMatchesFilter(r));
}

void OscMonitor::onPauseToggled(bool paused)
{
    m_paused = paused;
    m_pauseBtn->setText(paused ? tr("Resume") : tr("Pause"));
}

void OscMonitor::onUdpToggle(bool checked)
{
    if (checked) {
        if (m_engine->listenUdp(static_cast<quint16>(m_udpPort->value()))) {
            m_udpListening = true;
            m_udpToggle->setText(tr("Stop UDP"));
        } else {
            m_udpToggle->setChecked(false);
            m_statusLabel->setText(tr("UDP bind failed"));
            return;
        }
    } else {
        m_engine->stopAllListeners();
        // stopAll resets every listener; re-apply the others to keep their state.
        m_udpListening = false;
        m_udpToggle->setText(tr("Listen UDP"));
        if (m_tcpToggle->isChecked()) onTcpToggle(true);
        if (m_wsToggle->isChecked())  onWsToggle(true);
    }
    m_statusLabel->setText(tr("UDP %1 / TCP %2 / WS %3")
        .arg(m_udpListening ? tr("on") : tr("off"))
        .arg(m_tcpListening ? tr("on") : tr("off"))
        .arg(m_wsListening  ? tr("on") : tr("off")));
}

void OscMonitor::onTcpToggle(bool checked)
{
    if (checked) {
        if (m_engine->listenTcpSlip(static_cast<quint16>(m_tcpPort->value()))) {
            m_tcpListening = true;
            m_tcpToggle->setText(tr("Stop TCP"));
        } else {
            m_tcpToggle->setChecked(false);
            m_statusLabel->setText(tr("TCP bind failed"));
            return;
        }
    } else {
        m_engine->stopAllListeners();
        m_tcpListening = false;
        m_tcpToggle->setText(tr("Listen TCP"));
        if (m_udpToggle->isChecked()) onUdpToggle(true);
        if (m_wsToggle->isChecked())  onWsToggle(true);
    }
    m_statusLabel->setText(tr("UDP %1 / TCP %2 / WS %3")
        .arg(m_udpListening ? tr("on") : tr("off"))
        .arg(m_tcpListening ? tr("on") : tr("off"))
        .arg(m_wsListening  ? tr("on") : tr("off")));
}

void OscMonitor::onWsToggle(bool checked)
{
    if (checked) {
        if (m_engine->listenWebSocket(static_cast<quint16>(m_wsPort->value()))) {
            m_wsListening = true;
            m_wsToggle->setText(tr("Stop WS"));
        } else {
            m_wsToggle->setChecked(false);
            m_statusLabel->setText(tr("WS bind failed"));
            return;
        }
    } else {
        m_engine->stopAllListeners();
        m_wsListening = false;
        m_wsToggle->setText(tr("Listen WS"));
        if (m_udpToggle->isChecked()) onUdpToggle(true);
        if (m_tcpToggle->isChecked()) onTcpToggle(true);
    }
    m_statusLabel->setText(tr("UDP %1 / TCP %2 / WS %3")
        .arg(m_udpListening ? tr("on") : tr("off"))
        .arg(m_tcpListening ? tr("on") : tr("off"))
        .arg(m_wsListening  ? tr("on") : tr("off")));
}

} // namespace quewi::ui
