#pragma once

#include <QtCore/QtGlobal>

#if defined(LTC_LIBRARY)
    #define LTC_EXPORT Q_DECL_EXPORT
#else
    #define LTC_EXPORT Q_DECL_IMPORT
#endif
