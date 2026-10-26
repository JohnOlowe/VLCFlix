// Minimal shim for a Windows SDK header that WebView2.h includes.
// The real EventToken.h defines exactly this one type; mingw-w64 does not ship
// the Windows SDK's WinRT headers, so the definition is supplied here.
#pragma once
#ifndef DEFINE_EVENTTOKEN_H
#define DEFINE_EVENTTOKEN_H
typedef struct EventRegistrationToken { __int64 value; } EventRegistrationToken;
#endif
