#pragma once

// Screenshot-only fork: the UI speaks Russian or English.
//   The language follows Windows unless the user picked one in the settings.

enum SKIV_Language
{
  SKIV_Language_System  = 0,
  SKIV_Language_English = 1,
  SKIV_Language_Russian = 2
};

bool           SKIV_System_IsRussian (void);
bool           SKIV_UI_IsRussian     (void);
const char*    SKIV_Tr               (const char*    en, const char*    ru);
const wchar_t* SKIV_TrW              (const wchar_t* en, const wchar_t* ru);

#define TR(en, ru)  SKIV_Tr  (en, ru)
#define TRW(en, ru) SKIV_TrW (en, ru)
