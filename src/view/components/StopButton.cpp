#include "StopButton.h"

namespace view {

StopButton::StopButton(QWidget* parent)
    : QPushButton(parent)
{
    setIcon(QIcon(":/icons/stop.svg"));
}

} // namespace view