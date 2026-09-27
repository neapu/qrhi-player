#pragma once
#include <QPushButton>

namespace view {
class StopButton : public QPushButton {
    Q_OBJECT
public:
    explicit StopButton(QWidget* parent = nullptr);
};

} // namespace view