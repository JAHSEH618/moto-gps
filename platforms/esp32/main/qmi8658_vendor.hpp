#pragma once

// waveshare/qmi8658 2.0.0 closes its `extern "C"` block after its include
// guard, so a second #include in one C++ translation unit emits a stray '}'
// ("expected declaration before '}' token"). The FIFO and latch readers both
// need it, so include the vendor header through this file only.
#ifndef QMI8658_H
#include "qmi8658.h"
#endif
