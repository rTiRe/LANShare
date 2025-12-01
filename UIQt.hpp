#ifndef UIQT_HPP
#define UIQT_HPP

#include <QMainWindow>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>
#include <memory>
#include "SubnetListener.hpp"
#include "FileTransfer.hpp"
#include "SubnetBroadcaster.hpp"

class UIQt : public QMainWindow {
public:
    UIQt(SubnetListener& listener, FileTransfer& ft, SubnetBroadcaster& bc, QWidget* parent = nullptr);
    ~UIQt();

private:
    SubnetListener& listener_;
    FileTransfer& ft_;
    SubnetBroadcaster& bc_;

    QTableWidget* devicesTable_;
    QTableWidget* pendingTable_;
    QPushButton* acceptBtn_;
    QPushButton* rejectBtn_;
    QPushButton* rejectAllBtn_;
    QPushButton* sendBtn_;
    QTimer* refreshTimer_;

    void buildUi();
    void refresh();
    int firstUndecidedIndex(const std::vector<std::shared_ptr<PendingRequest>>& pending);
};

#endif // UIQT_HPP
