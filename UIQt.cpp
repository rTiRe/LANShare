#include "UIQt.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>

UIQt::UIQt(SubnetListener& listener, FileTransfer& ft, SubnetBroadcaster& bc, QWidget* parent)
    : QMainWindow(parent), listener_(listener), ft_(ft), bc_(bc) {
    buildUi();
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &UIQt::refresh);
    refreshTimer_->start(1000);
}

UIQt::~UIQt() {}

void UIQt::buildUi() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    devicesTable_ = new QTableWidget(0, 3, this);
    devicesTable_->setHorizontalHeaderLabels({"IP", "Hostname", "Status"});
    devicesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(devicesTable_);

    pendingTable_ = new QTableWidget(0, 3, this);
    pendingTable_->setHorizontalHeaderLabels({"#", "From", "File"});
    pendingTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(pendingTable_);

    auto* h = new QHBoxLayout();
    acceptBtn_ = new QPushButton("Accept first", this);
    rejectBtn_ = new QPushButton("Reject first", this);
    rejectAllBtn_ = new QPushButton("Reject all", this);
    sendBtn_ = new QPushButton("Send file", this);
    h->addWidget(acceptBtn_);
    h->addWidget(rejectBtn_);
    h->addWidget(rejectAllBtn_);
    h->addWidget(sendBtn_);
    layout->addLayout(h);

    connect(acceptBtn_, &QPushButton::clicked, [this]() {
        auto pending = ft_.get_pending_requests();
        int idx = firstUndecidedIndex(pending);
        if (idx >= 0) ft_.decide_request_by_index(idx, true);
    });
    connect(rejectBtn_, &QPushButton::clicked, [this]() {
        auto pending = ft_.get_pending_requests();
        int idx = firstUndecidedIndex(pending);
        if (idx >= 0) ft_.decide_request_by_index(idx, false);
    });
    connect(rejectAllBtn_, &QPushButton::clicked, [this]() {
        auto pending = ft_.get_pending_requests();
        for (size_t i = 0; i < pending.size(); ++i) {
            if (pending[i]->decision.load() == -1) ft_.decide_request_by_index(i, false);
        }
    });
    connect(sendBtn_, &QPushButton::clicked, [this]() {
        QString ip = QInputDialog::getText(this, "Target IP", "Enter target IP:");
        if (ip.isEmpty()) return;
        QString path = QFileDialog::getOpenFileName(this, "Select file to send");
        if (path.isEmpty()) return;
        bool ok = ft_.request_send(ip.toStdString(), ft_.control_port(), path.toStdString(), 30000);
        if (!ok) {
            QMessageBox::warning(this, "Request", "Denied or timed out");
            return;
        }
        bool sent = ft_.send_file(ip.toStdString(), ft_.listen_port(), path.toStdString());
        if (!sent) QMessageBox::warning(this, "Send", "Send failed");
    });
}

int UIQt::firstUndecidedIndex(const std::vector<std::shared_ptr<PendingRequest>>& pending) {
    for (size_t i = 0; i < pending.size(); ++i) if (pending[i]->decision.load() == -1) return (int)i;
    return -1;
}

void UIQt::refresh() {
    auto devices = listener_.get_devices();
    devicesTable_->setRowCount(0);
    int r = 0;
    for (const auto& [ip, info] : devices) {
        devicesTable_->insertRow(r);
        devicesTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(ip)));
        devicesTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(info.hostname)));
        devicesTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(MessageCodec::name_for(info.lastMessage))));
        ++r;
    }

    auto pending = ft_.get_pending_requests();
    pendingTable_->setRowCount(0);
    for (size_t i = 0; i < pending.size(); ++i) {
        pendingTable_->insertRow((int)i);
        pendingTable_->setItem((int)i, 0, new QTableWidgetItem(QString::number(i+1)));
        pendingTable_->setItem((int)i, 1, new QTableWidgetItem(QString::fromStdString(pending[i]->peer_ip)));
        pendingTable_->setItem((int)i, 2, new QTableWidgetItem(QString::fromStdString(pending[i]->filename)));
    }
}
