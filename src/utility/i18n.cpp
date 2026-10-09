
#include <utility/i18n.h>
#include <utility/registry.h>
#include <Windows.h>

bool
SKIV_System_IsRussian (void)
{
  static const bool russian =
    (PRIMARYLANGID (GetUserDefaultUILanguage ()) == LANG_RUSSIAN);

  return russian;
}

bool
SKIV_UI_IsRussian (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  switch (_registry.iLanguage)
  {
  case SKIV_Language_English: return false;
  case SKIV_Language_Russian: return true;
  default:                    return SKIV_System_IsRussian ( );
  }
}

const char*
SKIV_Tr (const char* en, const char* ru)
{
  return (SKIV_UI_IsRussian ( )) ? ru : en;
}

const wchar_t*
SKIV_TrW (const wchar_t* en, const wchar_t* ru)
{
  return (SKIV_UI_IsRussian ( )) ? ru : en;
}
