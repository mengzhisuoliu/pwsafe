/*
 * Copyright (c) 2003-2025 Rony Shapiro <ronys@pwsafe.org>.
 * All rights reserved. Use of the code is allowed under the
 * Artistic License 2.0 terms, as specified in the LICENSE file
 * distributed with this code, or available from
 * http://www.opensource.org/licenses/artistic-license-2.0.php
 */

/** \file PWSafeApp.cpp
*
*/

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#ifdef __WXMSW__
#include <wx/msw/msvcrt.h>
#endif

////@begin includes
#include <wx/cmdline.h>
#include <wx/fs_arc.h>
#include <wx/html/helpctrl.h>
#include <wx/propdlg.h>
#include <wx/snglinst.h>
#include <wx/taskbar.h>
#include <wx/textfile.h>
#include <wx/timer.h>
#include <wx/tokenzr.h>
#include <wx/spinctrl.h>

#if wxCHECK_VERSION(3, 1, 6)
#include <wx/uilocale.h>
#endif

#if wxCHECK_VERSION(2,9,2)
#include <wx/richmsgdlg.h>
#endif
////@end includes

#include "os/cleanup.h"
#include "os/dir.h"
#include "os/file.h"
#include "core/PWSfile.h"
#include "core/PWSLog.h"
#include "core/PWSprefs.h"
#include "core/PWSrand.h"
#include "core/SysInfo.h"
#include "core/PWSdirs.h"
#include "wxUtilities.h"

#if defined(__X__) || defined(__WXGTK__) || defined(__WXOSX__)
#include "Clipboard.h"
#endif
#include "CryptKeyEntryDlg.h"
#include "GridShortcutsValidator.h"
#include "PasswordSafeFrame.h"
#include "PWSafeApp.h"
#include "version.h"
#include "wxMessages.h"
#include "Clipboard.h"
#ifndef NO_YUBI
#include "YubiMixin.h"
#endif

#include <iostream> // currently for debugging
#include <clocale>  // to get the locales specified by the environment 
                    // for used functions like wcstombs in src/os/file.h
#include <functional>

using namespace std;

////@begin XPM images
#include "graphics/pwsafe16.xpm"
#include "graphics/pwsafe32.xpm"
#include "graphics/pwsafe48.xpm"
////@end XPM images

#if wxCHECK_VERSION(2,9,0)
#define STR(s) s
#else
#define STR(s) wxT(s)
#endif

// wx debug messages (a) don't really interest us, and
// (b) manage to crash wx 3.0.2 under Windows due to some
// odd initialization sequence error, so we just shut them up.
// Need to do this in a global as well, as we die in another
// global trace in Release Windows build

class RestrainLog {
public:
  RestrainLog(wxLogLevel level) {
    wxLog::SetComponentLevel(wxT("wx"), level);
  }
};

static RestrainLog restLog(wxLOG_Info);

static const wxCmdLineEntryDesc cmdLineDesc[] = {
  {wxCMD_LINE_SWITCH, STR("?"), STR("help"),
   STR("displays command line usage"),
   wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP},
  {wxCMD_LINE_SWITCH, STR("r"), STR("read-only"),
   STR("open database read-only"),
   wxCMD_LINE_VAL_NONE, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("v"), STR("validate"),
   STR("validate (and repair) database"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("e"), STR("encrypt"),
   STR("encrypt a file"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("d"), STR("decrypt"),
   STR("decrypt a file"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_SWITCH, STR("c"), STR("close"),
   STR("start 'closed', without prompting for database"),
   wxCMD_LINE_VAL_NONE, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_SWITCH, STR("s"), STR("silent"),
   STR("start 'silently', minimized and with no database"),
   wxCMD_LINE_VAL_NONE, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_SWITCH, STR("m"), STR("minimized"),
   STR("like '-c', plus start minimized"),
   wxCMD_LINE_VAL_NONE, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("u"), STR("username"),
   STR("use specified user preferences instead of current user"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("h"), STR("hostname"),
   STR("use specified host preferences instead of current host"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  {wxCMD_LINE_OPTION, STR("g"), STR("config_file"),
   STR("use specified configuration file instead of default"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
#ifndef NO_YUBI
  {wxCMD_LINE_OPTION, nullptr, STR("yubi-polling-interval"),
   STR("use specified polling interval (in ms) for YubiKey instead of default (900ms), or 0 to disable polling."),
   wxCMD_LINE_VAL_NUMBER, wxCMD_LINE_PARAM_OPTIONAL},
#endif
  {wxCMD_LINE_PARAM, nullptr, nullptr, STR("database"),
   wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
  wxCMD_LINE_DESC_END
};

#undef STR

static wxReporter aReporter;
static wxAsker    anAsker;

static void cleanup_handler(int WXUNUSED(signum), void *p)
{
  // Called if we get a signal - don't try to save, since we don't
  // know what's valid, if anything. Just unlock file, if any.
  PWScore *pcore = static_cast<PWScore *>(p);
  pcore->SafeUnlockCurFile();
  exit(1);
}

/*!
 * Application instance implementation
 */

////@begin implement app
IMPLEMENT_APP( PWSafeApp )
////@end implement app

/*!
 * PWSafeApp type definition
 */

IMPLEMENT_CLASS( PWSafeApp, wxApp )

/*!
 * PWSafeApp event table definition
 */

  BEGIN_EVENT_TABLE( PWSafeApp, wxApp )

////@begin PWSafeApp event table entries
////@end PWSafeApp event table entries
  EVT_TIMER(IDLE_TIMER_ID, PWSafeApp::OnIdleTimer)
  EVT_CUSTOM(wxEVT_GUI_DB_PREFS_CHANGE, wxID_ANY, PWSafeApp::OnDBGUIPrefsChange)
  END_EVENT_TABLE()

/*!
 * Constructor for PWSafeApp
 */

PWSafeApp::PWSafeApp() : m_idleTimer(new wxTimer(this, IDLE_TIMER_ID)),
                         m_frame(nullptr), m_recentDatabases(nullptr),
                         m_locale(nullptr)
{
  Init();
}

/*!
 * Destructor for PWSafeApp
 */
PWSafeApp::~PWSafeApp()
{
  delete m_idleTimer;
  delete m_recentDatabases;

  PWSprefs::DeleteInstance();
  PWSrand::DeleteInstance();
  PWSLog::DeleteLog();
  Clipboard::DeleteInstance();

  delete m_locale;
}

/*!
 * Member initialisation
 */

void PWSafeApp::Init()
{
  pws_os::install_cleanup_handler(cleanup_handler, &m_core);
  m_locale = new wxLocale;
  wxLocale::AddCatalogLookupPathPrefix(L"/usr/share/locale");
  wxLocale::AddCatalogLookupPathPrefix(L"/usr");
  wxLocale::AddCatalogLookupPathPrefix(L"/usr/local");
#if defined(__WXDEBUG__) || defined(_DEBUG) || defined(DEBUG)
  wxLocale::AddCatalogLookupPathPrefix(L"../I18N/mos");
  wxLocale::AddCatalogLookupPathPrefix(L"src/ui/wxWidgets/I18N/mos"); // located in cmake's build directory
#endif

////@begin PWSafeApp member initialisation
////@end PWSafeApp member initialisation
}

#ifdef __WXDEBUG__
void PWSafeApp::OnAssertFailure(const wxChar *file, int line, const wxChar *func,
                const wxChar *cond, const wxChar *msg)
{
  if (m_locale)
    wxApp::OnAssertFailure(file, line, func, cond, msg);
  else
      std::wcerr << file << L'(' << line << L"):"
                 << L" assert \"" << cond << L"\" failed in " << (func? func: L"") << L"(): "
                 << (msg? msg: L"") << endl;
}
#endif

/** Activate help subsystem for given language
* @param language help language for activation (if not found, default will be used)
*/
bool PWSafeApp::ActivateHelp(wxLanguage language) {
  wxString fileNameBase = L"help", fileExt=L".zip", defaultSuffix=L"EN"+fileExt;
  wxString langSuffix = wxLocale::GetLanguageCanonicalName(language);
  // Get only two letters
  if (langSuffix.length() >= 2) {
    langSuffix = langSuffix.substr(0, 2).Upper() + fileExt;
  }
  else { // English is default
    langSuffix = defaultSuffix;
  }
  
  helpFileNamePath.Clear();
  
  wxFileName helpFileName = wxFileName(towxstring(pws_os::gethelpdir()), fileNameBase+langSuffix);
  if (!helpFileName.IsFileReadable()) {
    pws_os::Trace(L"Help file for selected language %ls unavailable. Will retry with default.", ToStr(helpFileName.GetFullPath()));
    helpFileName = wxFileName(towxstring(pws_os::gethelpdir()), fileNameBase+defaultSuffix);
    if (!helpFileName.IsFileReadable()) {
      pws_os::Trace(L"Help file for default language %ls unavailable.", ToStr(helpFileName.GetFullPath()));
      return false;
    }
  }
  
  helpFileNamePath = helpFileName.GetFullPath();

  return true;
}

/*!
 * Initialisation for PWSafeApp
 */

bool PWSafeApp::OnInit()
{
  // Get the locale environment variable 'LC_CTYPE' specified by the environment
  // For instance, the behavior of function 'wcstombs' depends on the LC_CTYPE 
  // category of the selected C locale.
#if defined(__WXMAC__)
  const char *lcCType = setlocale(LC_CTYPE, NULL);

  if (lcCType && strcmp(lcCType, "UTF-8")) { // MAC OS only have UTF-8 as default, but conversion with this string is not running well
    setlocale(LC_CTYPE, "");
  }
  else {
    const char *env_lc_cType = std::getenv("LC_CTYPE");
    
    if(env_lc_cType) {
      setlocale(LC_CTYPE, env_lc_cType);
    }
    else {
      setlocale(LC_CTYPE, "en_US.UTF-8");
    }
  }
  // This value must be set for mac OS starting with version 11, but is no problem for earlier versions, see
  // https://trac.wxwidgets.org/ticket/19023
  setlocale(LC_NUMERIC, "C");
#else
  setlocale(LC_CTYPE, "");
#endif
  setlocale(LC_TIME, "");
  
  //Used by help subsystem
  wxFileSystem::AddHandler(new wxArchiveFSHandler);

  SetAppName(pwsafeAppName);
  PWSprefs::SetReporter(&aReporter);
  PWScore::SetReporter(&aReporter);
  PWScore::SetAsker(&anAsker);

#if wxUSE_XPM
  wxImage::AddHandler(new wxXPMHandler);
#endif
#if wxUSE_LIBPNG
  wxImage::AddHandler(new wxPNGHandler);
#endif
#if wxUSE_LIBJPEG
  wxImage::AddHandler(new wxJPEGHandler);
#endif
#if wxUSE_GIF
  wxImage::AddHandler(new wxGIFHandler);
#endif
  // Get progname
  wxString progName(argv[0]);
  progName = progName.AfterLast(wxChar('/'));
  // Parse command line
  wxCmdLineParser cmdParser(cmdLineDesc, argc, argv);
  int res = cmdParser.Parse();
  if (res != 0)
    return false;

  // Don't allow ptrace or gdump on release build
  if (!pws_os::DisableDumpAttach())
    return false;

  // Parse command line options:
  wxString cmd_filename, user, host, cfg_file;
  bool cmd_ro = cmdParser.Found(wxT("r"));

  m_cmd_closed = cmdParser.Found(wxT("c"));
  m_cmd_silent = cmdParser.Found(wxT("s"));
  m_cmd_minimized = cmdParser.Found(wxT("m"));

  bool cmd_encrypt = cmdParser.Found(wxT("e"), &cmd_filename);
  bool cmd_decrypt = cmdParser.Found(wxT("d"), &cmd_filename);
  bool cmd_user = cmdParser.Found(wxT("u"), &user);
  bool cmd_host = cmdParser.Found(wxT("h"), &host);
  bool cmd_cfg = cmdParser.Found(wxT("g"), &cfg_file);
#ifndef NO_YUBI
  long int yubi_polling_interval;
  bool cmd_yubi_polling_interval = cmdParser.Found(wxT("yubi-polling-interval"), &yubi_polling_interval);
#endif
  m_file_in_cmd = false;
  size_t count = cmdParser.GetParamCount();
  if (count == 1) {
    cmd_filename = cmdParser.GetParam();
    m_file_in_cmd = true;
  }
  else if (count > 1) {
    cmdParser.Usage();
    return false;
  }
  // check for mutually exclusive options
  if ((cmd_encrypt && cmd_decrypt) || (m_cmd_closed && m_cmd_silent && m_cmd_minimized)) {
    cmdParser.Usage();
    return false;
  }

  if (cmd_user) {
    SysInfo::GetInstance()->SetEffectiveUser(tostdstring(user));
  }
  if (cmd_host) {
    SysInfo::GetInstance()->SetEffectiveHost(tostdstring(host));
  }
  if (cmd_cfg) {
    PWSprefs::SetConfigFile(tostdstring(cfg_file));
  }
#ifndef NO_YUBI
  if (cmd_yubi_polling_interval) {
    YubiMixin::SetPollingInterval(static_cast<int>(yubi_polling_interval));
  }
#endif

  m_core.SetReadOnly(cmd_ro);
  // OK to load prefs now
  PWSprefs *prefs = PWSprefs::GetInstance();

  // Initialize language only after parsing cmd_cfg and instantiating prefs,
  // otherwise GetSelectedLanguage()&Co will instantiate prefs singleton and it
  // will ignore config file parameter
  wxLanguage selectedLang = GetSelectedLanguage();
  m_locale->Init(selectedLang);
  ActivateLanguage(selectedLang, false);

 // Process encryption/decryption command line arguments
  // Note that this is done after language is set, addressing GH1572
  if ((cmd_encrypt || cmd_decrypt) && !cmd_filename.IsEmpty()) {

    auto processCryption = [&](CryptKeyEntryDlg::Mode mode, std::function<bool(const stringT &fn, const StringX &passwd, stringT &errmess)> func) {
      stringT errstr;

      CryptKeyEntryDlg dialog(mode);

      if (dialog.ShowModal() == wxID_OK) {
        if (!func(tostdstring(cmd_filename), dialog.getCryptKey(), errstr)) {
          wxMessageDialog messageBox(
            nullptr, errstr, _("Error"), wxOK | wxICON_ERROR
          );
          messageBox.ShowModal();
        }
      }
    };

    if (cmd_encrypt) {
      processCryption(
        CryptKeyEntryDlg::Mode::ENCRYPT,
        PWSfile::Encrypt
      );
    }
    else {
      processCryption(
        CryptKeyEntryDlg::Mode::DECRYPT,
        PWSfile::Decrypt
      );
    }
    return false;
  }



  // if filename passed in command line, it takes precedence
  // over that in preference:
  if (cmd_filename.empty()) {
    cmd_filename =  prefs->GetPref(PWSprefs::CurrentFile).c_str();
  } else {
    recentDatabases().AddFileToHistory(cmd_filename);
  }
  m_core.SetCurFile(tostringx(cmd_filename));
  m_core.SetApplicationNameAndVersion(tostdstring(progName),
                                      MAKELONG(MINORVERSION, MAJORVERSION));

  static wxSingleInstanceChecker appInstance;
  if (!prefs->GetPref(PWSprefs::MultipleInstances) &&
      (appInstance.Create(wxT("pwsafe.lck"), towxstring(pws_os::getuserprefsdir())) &&
       appInstance.IsAnotherRunning()))
  {
    wxMessageBox(_("Another instance of Password Safe is already running"), _("Password Safe"),
                          wxOK|wxICON_INFORMATION);
    return false;
  }

#if defined(__X__) || defined(__WXGTK__)
  Clipboard::GetInstance()->UsePrimarySelection(prefs->GetPref(PWSprefs::UsePrimarySelectionForClipboard));
#endif

  // here if we're the child
  recentDatabases().Load();

  if (m_cmd_closed || m_cmd_minimized) {
    m_core.SetCurFile(L"");
  }
  if (m_cmd_silent) {
    if ( IsTaskBarIconAvailable() ) {
      // start silent implies use system tray.
      // Note that if UseSystemTray is already true, then pwsafe will try to run silently anyway
      PWSprefs::GetInstance()->SetPref(PWSprefs::UseSystemTray, true);
    }
    else {
      // We don't want to bring up a UI if running silently
      std::wcerr << L"There appears to be no system tray support in your current environment.  pwsafe may not work as expected in silent mode." << std::endl;
    }
  }

  m_appIcons.AddIcon(pwsafe16);
  m_appIcons.AddIcon(pwsafe32);
  m_appIcons.AddIcon(pwsafe48);

  if (!isHelpActivated) { // set on language activation by ActivateHelp
    std::wcerr << L"Could not initialize help subsystem." << std::endl;
    if (!prefs->GetPref(PWSprefs::IgnoreHelpLoadError) && !m_cmd_silent) {
#if wxCHECK_VERSION(2,9,2)
      wxRichMessageDialog dlg(nullptr,
                              _("Could not initialize help subsystem. Help will not be available."),
                              _("Password Safe: Error initializing help"), wxCENTRE|wxOK|wxICON_EXCLAMATION);
      dlg.ShowCheckBox(_("Don't show this warning again"));
      dlg.ShowModal();
      if (dlg.IsCheckBoxChecked()) {
        prefs->SetPref(PWSprefs::IgnoreHelpLoadError, true);
        prefs->SaveApplicationPreferences();
      }
#else
      wxMessageBox(_("Could not initialize help subsystem. Help will not be available."),
                   _("Password Safe: Error initializing help"), wxOK | wxICON_ERROR);
#endif
    }
  }

/*
 * On macOS, Finder GUI file actions, such as double-clicking a file to open it, are sent
 * to the app via calls to MacOpenFiles() and MacNewFile().  However, those messages
 * are not processed until OnInit() returns.  This hack splits OnInit() into two parts,
 * FinishInit() is called from those functions so that MacOpenFiles() has a chance
 * to set the file name before the password entry dialog is displayed.
 * All other information needed is stored in the PWSafeApp object.
 * On Linux/Unix, just call the second part direct, so this is still like one big happy function.
 */
#ifndef __WXMAC__
  PWSafeApp::FinishInit();
#endif
  return true;
}

void PWSafeApp::FinishInit() {
  if (!m_cmd_closed && !m_cmd_silent && !m_cmd_minimized) {
    // Get the file, r/w mode and password from user
    // Note that file may be new
    DestroyWrapper<SafeCombinationEntryDlg> initWindowWrapper(nullptr, m_core);
    SafeCombinationEntryDlg *initWindow = initWindowWrapper.Get();
    int returnValue = initWindow->ShowModal();

    if (returnValue != wxID_OK) {
      return;
    }
    wxASSERT_MSG(!m_frame, wxT("Frame window created unexpectedly"));
    m_frame = new PasswordSafeFrame(nullptr, m_core);
    m_frame->Load(initWindow->GetPassword());
  }
  else {
    wxASSERT_MSG(!m_frame, wxT("Frame window created unexpectedly"));
    m_frame = new PasswordSafeFrame(nullptr, m_core);
  }

  RestoreFrameCoords();

  // Load the default filter, if any
  PWSprefs::ConfigOption configoption;
  std::wstring wsCnfgFile = PWSprefs::GetConfigFile(configoption);
  std::wstring wsAutoLoad;
  if (configoption == PWSprefs::CF_NONE ||
      configoption == PWSprefs::CF_REGISTRY) {
     // Need to use configuration directory instead
    wsAutoLoad = PWSdirs::GetConfigDir() + L"/" + L"autoload_filters.xml";
  } else {
    std::wstring wsCnfgDrive, wsCnfgDir, wsCnfgFileName, wsCnfgExt;
    pws_os::splitpath(wsCnfgFile, wsCnfgDrive, wsCnfgDir, wsCnfgFileName, wsCnfgExt);
    wsAutoLoad = pws_os::makepath(wsCnfgDrive, wsCnfgDir, L"autoload_filters", L".xml");
  }
  
  if (pws_os::FileExists(wsAutoLoad)) {
    stringT strErrors = L"";
    int rc = m_frame->ImportFilterXMLFile(FPOOL_AUTOLOAD, L"", wsAutoLoad,
                                          L"", strErrors, &anAsker, nullptr); // Only summary will be reported
    if (rc != PWScore::SUCCESS) {
      wxMessageBox(towxstring(strErrors), _("Unable to import \"autoload_filters.xml\""), wxOK | wxICON_ERROR);
    }
  }

  if (!m_cmd_silent)
    m_frame->Show();
  if (m_cmd_minimized)
    m_frame->Iconize();
  else if (m_cmd_silent) {
    // Hide UI enumerates top-level windows and its children and hide them,
    // so we need to set top windows first and only then call hideUI
    SetTopWindow(m_frame);
    wxSafeYield();
    m_frame->HideUI(false);
    if (m_file_in_cmd) {
       // set locked status if file was passed from command line
       m_frame->SetTrayStatus(true);
    }
    else {
       m_core.SetCurFile(L"");
       m_frame->SetTrayClosed();
    }
  } else {
    SetTopWindow(m_frame);
  }
  if (PWSprefs::GetInstance()->GetPref(PWSprefs::UseSystemTray))
    m_frame->ShowTrayIcon();

  m_frame->Register(); // register modal dialog hook
  m_initComplete = true;
  return;
}

/*!
 * Initializing the language support means currently
 * to determine the system default language and
 * activate this one for the application.
 */
wxLanguage PWSafeApp::GetSystemLanguage()
{
  int language = wxLocale::GetSystemLanguage();

#if defined(__UNIX__) && !defined(__WXMAC__) && wxCHECK_VERSION( 2, 8, 0 )
  // Workaround for wx bug 15006 that has been fixed in wxWidgets 2.8
  if (language == wxLANGUAGE_UNKNOWN) {
    std::wcerr << L"Couldn't detect locale. Trying to skip empty env. variables." << std::endl;
    // Undefine empty environment variables and try again
    wxString langFull;
    if (wxGetEnv(wxT("LC_ALL"), &langFull) && !langFull) {
      wxUnsetEnv(wxT("LC_ALL"));
      std::wcerr << L"Empty LC_ALL variable was dropped" << std::endl;
    }
    if (wxGetEnv(wxT("LC_MESSAGES"), &langFull) && !langFull) {
      wxUnsetEnv(wxT("LC_MESSAGES"));
      std::wcerr << L"Empty LC_MESSAGES variable was dropped" << std::endl;
    }
    if (wxGetEnv(wxT("LANG"), &langFull) && !langFull) {
      wxUnsetEnv(wxT("LANG"));
      std::wcerr << L"Empty LANG variable was dropped" << std::endl;
    }
    language = wxLocale::GetSystemLanguage();
  }
#endif

  return static_cast<wxLanguage>(language);
}

/* Get selected language (user preference or system) */
wxLanguage PWSafeApp::GetSelectedLanguage()
{
  StringX sxUserLang = PWSprefs::GetInstance()->GetPref(PWSprefs::LanguageFile);

  const wxLanguageInfo *langInfo = nullptr;

  if (sxUserLang.empty()) {
    langInfo = wxLocale::GetLanguageInfo(wxLANGUAGE_DEFAULT);
  }
  else {
    langInfo = wxLocale::FindLanguageInfo(towxstring(sxUserLang));
  }

  if (langInfo && ActivateLanguage(static_cast<wxLanguage>(langInfo->Language), true)) {
    pws_os::Trace(L"Found user-preferred language: id= %d, name= %ls\n", langInfo->Language, sxUserLang.c_str());
    return static_cast<wxLanguage>(langInfo->Language);
  }
  else {
    // language settings found, but wasn't activated
#ifdef DEBUG
    if (langInfo) {
      pws_os::Trace(L"User-preferred language can't be activated: id= %d, name= %ls\n", langInfo->Language, sxUserLang.c_str());
    }
#endif
    return GetSystemLanguage();
  }
}

/*!
 * Activates a language.
 *
 * \param language the wxWidgets specific enumeration of type wxLanguage representing a supported language
 * \see http://docs.wxwidgets.org/trunk/language_8h.html#a7d1c74ce43b2fb7acf7a6fa438c0ee86
 * \param tryOnly only try to load locale without resettings global instance
 */
bool PWSafeApp::ActivateLanguage(wxLanguage language, bool tryOnly)
{
  static wxString DOMAIN_(L"pwsafe");

  if (tryOnly && language == wxLANGUAGE_ENGLISH) {
    return true;
  }

  bool bRes = true;

  wxTranslations *translations = new wxTranslations;
  translations->SetLanguage(language);

  if (language != wxLANGUAGE_ENGLISH) {
    if ( !translations->AddStdCatalog() ) {
      pws_os::Trace(L"Couldn't load default language catalog for %ls\n", ToStr(wxLocale::GetLanguageName(language)));
    }

    if ( !translations->AddCatalog(DOMAIN_) ) {
      pws_os::Trace(L"Couldn't load %ls language catalog for %ls\n", ToStr(DOMAIN_), ToStr(wxLocale::GetLanguageName(language)));
      translations->SetLanguage(wxLANGUAGE_ENGLISH);
      language = wxLANGUAGE_ENGLISH; // Back to default language english
    }
    bRes = translations->IsLoaded(DOMAIN_);
  }

  if (tryOnly) {
    delete translations;
  }
  else {
    // (re)set global translation and take care of occupied memory by wxTranslations
    wxTranslations::Set(translations);
    isHelpActivated = ActivateHelp(language);

    const wxLanguageInfo *langInfo = nullptr;
    langInfo = wxLocale::GetLanguageInfo(language);
    if(langInfo) {

#if defined(__WXMAC__)
#if wxCHECK_VERSION(3, 2, 2)
      // Some languages have multiple locale variations.  (e.g. en_US, en_GB, etc.)
      // Just using the two letter languane identifier (e.g. en.UTF-8) does not work
      // on macOS, there needs to be a region as well (e.g. en_US.UTF-8), not doing
      // so causes some inconsistent results. Specifically, the date format seems to
      // default to en_GB in WX but not in native macOS controls, such as the date picker.
      wxString envString;
      wxLocaleIdent sysLocaleId = wxUILocale::GetSystemLocaleId();
      if (langInfo->CanonicalName == sysLocaleId.GetLanguage() && !sysLocaleId.GetRegion().empty()) {
        envString = sysLocaleId.GetName();

      } else if (!langInfo->CanonicalRef.empty()) {
        envString = langInfo->CanonicalRef;
      }
      if (!envString.empty()) {
        envString += ".UTF-8";
        setlocale(LC_CTYPE, envString.c_str());
        setlocale(LC_TIME, envString.c_str());
      }
#endif // wxCHECK_VERSION
      // This value must be set for mac OS starting with version 11, but is no problem for earlier versions, see
      // https://github.com/wxWidgets/wxWidgets/issues/19023
      // https://docs.wxwidgets.org/3.2/classwx_locale.html
      setlocale(LC_NUMERIC, "C");
#else // __WXMAC__
      wxString envString = langInfo->CanonicalName + ".UTF-8";
      setlocale(LC_CTYPE, envString.c_str());
      setlocale(LC_TIME, envString.c_str());
#endif // __WXMAC__

    }
  }
  return bRes;
}

#ifdef __WXMAC__
// macOS specific functions for UI interactions...

// This enables file unlock and UI restore upon left-click of the dock icon.
// Not to be confused with the system tray (menu bar) icon.
void PWSafeApp::MacReopenApp()
{
  if (m_initComplete)
    GetPasswordSafeFrame()->UnlockSafe(true, false);
}

// This is called when a file, of a type associated with the app, is double-clicked
// or drag-and-dropped onto the dock icon.  We need to set the current file name before
// showing the SafeCombinationEntryDlg dialog.  (Which happens in FinishInit().)  If
// FinishInit() does not create the PasswordSafeFrame, (i.e. the user clicked cancel)
// the the framework will call OnExit() for us.
void PWSafeApp::MacOpenFiles(const wxArrayString& fileNames) {
  StringX filename;
  if (fileNames.IsEmpty())
    return;

  // Really, we can only open one file at a time, so just use the first one.
  filename = fileNames[0];

  // Only allow one pass through FinishInit(). While SafeCombinationEntryDlg is displayed,
  // clicking another file causes another pass through here, which resulted in a second
  // SafeCombinationEntryDlg window!  The mutex ignores subsequent events until the first
  // file is open.  However, in this situation, the mutex is only advisory.
  wxMutexLocker lock(m_MacFileEventMutex);
  if (lock.IsOk()) {
    // After the first open, subsequent file double-clicks are equvalent to the
    // File->Open menu action.
    if (m_initComplete) {
      int rc = m_frame->Open(filename.c_str());
      if (rc == PWScore::SUCCESS) {
        m_frame->FinishGoodOpen();
      }
    } else {
      m_core.SetCurFile(filename);
      FinishInit();
    }
  }
}

// This is called when the app is started by itself (not by clicking a data file)
// Just finish the init.
void PWSafeApp::MacNewFile() {
  wxMutexLocker lock(m_MacFileEventMutex);
  if (lock.IsOk()) {
    FinishInit();
  }
}
#endif // __WXMAC__

/*!
 * Cleanup for PWSafeApp
 */

int PWSafeApp::OnExit()
{
  // OnExit() is only called after OnInit() returns true.
  // Now that it's in two parts, we want to make sure the
  // second part is also complete.
  if (m_initComplete) {
    m_idleTimer->Stop();
    recentDatabases().Save();
    PWSprefs *prefs = PWSprefs::GetInstance();
    if (m_core.IsDbFileSet()) {
      prefs->SetPref(PWSprefs::CurrentFile, m_core.GetCurFile());
      // Don't leave dangling locks!
      m_core.SafeUnlockCurFile();
    }
    // Save Application related preferences
    prefs->SaveApplicationPreferences();
    // Save shortcuts, if changed
    PWSMenuShortcuts::GetShortcutsManager()->SaveUserShortcuts();
    PWSMenuShortcuts::DestroyShortcutsManager();
  }

////@begin PWSafeApp cleanup
  return wxApp::OnExit();
////@end PWSafeApp cleanup
}

void PWSafeApp::ConfigureIdleTimer()
{
  const PWSprefs *prefs =   PWSprefs::GetInstance();

  bool isRunning = m_idleTimer->IsRunning();
  bool shouldBeRunning = prefs->GetPref(PWSprefs::LockDBOnIdleTimeout);
  int timeOut = shouldBeRunning ? prefs->GetPref(PWSprefs::IdleTimeout) : 0;

  if (!isRunning) {
    if (shouldBeRunning && timeOut != 0) {
      m_idleTimer->Start(timeOut * 60 * 1000, wxTIMER_CONTINUOUS);
    } else {
      // not running, nor should it be - nop
    }
  } else { // running
    if (!shouldBeRunning || timeOut == 0) {
      m_idleTimer->Stop();
    } else if (timeOut != 0) { // Restart
      m_idleTimer->Start(timeOut * 60 * 1000, wxTIMER_CONTINUOUS);
    }
  }
}

void PWSafeApp::OnIdleTimer(wxTimerEvent &evt)
{
  if (evt.GetId() == IDLE_TIMER_ID && PWSprefs::GetInstance()->GetPref(PWSprefs::LockDBOnIdleTimeout)) {
    if (m_frame != nullptr && !m_frame->IsClosed() && !m_frame->IsLocked()) {
      m_idleTimer->Stop(); // Stop as with modal dialog a dialog will stay open
      m_frame->IconizeOrHideAndLock();
      if(!m_frame->IsLocked()) // If not locked, start timer again
        ConfigureIdleTimer();
    }
  }
}

void PWSafeApp::OnDBGUIPrefsChange(wxEvent& evt)
{
  UNREFERENCED_PARAMETER(evt);
  ConfigureIdleTimer();
}

RecentDbList &PWSafeApp::recentDatabases()
{
  // we create an instance of m_recentDatabases
  // as late as possible in order to make
  // sure that prefs' is set correctly (user, machine, etc.)
  if (m_recentDatabases == nullptr) {
    m_recentDatabases = new RecentDbList;
#if wxCHECK_VERSION(3,1,5)
    m_recentDatabases->SetMenuPathStyle(wxFH_PATH_SHOW_ALWAYS);
#endif
  }
  return *m_recentDatabases;
}

void PWSafeApp::ResizeRecentDatabases()
{
  if (m_recentDatabases != nullptr) {
    m_recentDatabases->Save();
    delete m_recentDatabases;
    m_recentDatabases = new RecentDbList;
#if wxCHECK_VERSION(3,1,5)
    m_recentDatabases->SetMenuPathStyle(wxFH_PATH_SHOW_ALWAYS);
#endif
    m_recentDatabases->Load();
  }
}

void PWSafeApp::SaveFrameCoords(void)
{
  if (m_frame->IsMaximized()) {
    PWSprefs::GetInstance()->SetPrefRect(-1, -1, -1, -1);
  }
  else if (m_frame->IsIconized() || !m_frame->IsShown()) {
    //if we save coords when minimized/hidden, pwsafe could be hidden or off
    //screen next time it runs
    return;
  }
  else {
    wxRect rc = m_frame->GetScreenRect();
    PWSprefs::GetInstance()->SetPrefRect(rc.GetTop(), rc.GetBottom(), rc.GetLeft(), rc.GetRight());
  }
}

void PWSafeApp::RestoreFrameCoords(void)
{
  long top, bottom, left, right;
  PWSprefs::GetInstance()->GetPrefRect(top, bottom, left, right);
  if (!(left == -1 && top == -1 && right == -1 && bottom == -1)) {
    wxRect rcApp(static_cast<int>(left), static_cast<int>(top), 
                 static_cast<int>(right - left + 1), static_cast<int>(bottom - top + 1));

    int displayWidth, displayHeight;
    ::wxDisplaySize(&displayWidth, &displayHeight);
    wxRect rcDisplay(0, 0, displayWidth, displayHeight);

    if (!rcApp.IsEmpty() && rcDisplay.Contains(rcApp))
      m_frame->SetSize(rcApp);
  }
}

int PWSafeApp::FilterEvent(wxEvent& evt) {
  const wxEventType et = evt.GetEventType();

  // Clear idle flag for lock-on-idle timer
  // We need a whitelist rather than a blacklist because
  // undocumented events are passed through here as well...
  if ((et == wxEVT_COMMAND_BUTTON_CLICKED) ||
      (et == wxEVT_COMMAND_CHECKBOX_CLICKED) ||
      (et == wxEVT_COMMAND_CHOICE_SELECTED) ||
      (et == wxEVT_COMMAND_LISTBOX_SELECTED) ||
      (et == wxEVT_COMMAND_LISTBOX_DOUBLECLICKED) ||
      (et == wxEVT_COMMAND_TEXT_UPDATED) ||
      (et == wxEVT_COMMAND_TEXT_ENTER) ||
      (et == wxEVT_COMMAND_MENU_SELECTED) ||
      (et == wxEVT_COMMAND_SLIDER_UPDATED) ||
      (et == wxEVT_COMMAND_RADIOBOX_SELECTED) ||
      (et == wxEVT_COMMAND_RADIOBUTTON_SELECTED) ||
      (et == wxEVT_COMMAND_SCROLLBAR_UPDATED) ||
      (et == wxEVT_COMMAND_VLBOX_SELECTED) ||
      (et == wxEVT_COMMAND_COMBOBOX_SELECTED) ||
      (et == wxEVT_COMMAND_TOOL_RCLICKED) ||
      (et == wxEVT_COMMAND_TOOL_ENTER) ||
      (et == wxEVT_COMMAND_SPINCTRL_UPDATED) ||
      (et == wxEVT_LEFT_DOWN) ||
      (et == wxEVT_LEFT_UP) ||
      (et == wxEVT_MIDDLE_DOWN) ||
      (et == wxEVT_MIDDLE_UP) ||
      (et == wxEVT_RIGHT_DOWN) ||
      (et == wxEVT_RIGHT_UP) ||
      (et == wxEVT_MOTION) ||
      (et == wxEVT_ENTER_WINDOW) ||
      (et == wxEVT_LEAVE_WINDOW) ||
      (et == wxEVT_LEFT_DCLICK) ||
      (et == wxEVT_MIDDLE_DCLICK) ||
      (et == wxEVT_RIGHT_DCLICK) ||
      (et == wxEVT_SET_FOCUS) ||
      (et == wxEVT_MOUSEWHEEL) ||
      (et == wxEVT_NAVIGATION_KEY) ||
      (et == wxEVT_KEY_DOWN) ||
      (et == wxEVT_KEY_UP) ||
      (et == wxEVT_SCROLL_TOP) ||
      (et == wxEVT_SCROLL_BOTTOM) ||
      (et == wxEVT_SCROLL_LINEUP) ||
      (et == wxEVT_SCROLL_LINEDOWN) ||
      (et == wxEVT_SCROLL_PAGEUP) ||
      (et == wxEVT_SCROLL_PAGEDOWN) ||
      (et == wxEVT_SCROLL_THUMBTRACK) ||
      (et == wxEVT_SCROLL_THUMBRELEASE) ||
      (et == wxEVT_SCROLL_CHANGED) ||
      (et == wxEVT_SIZE) ||
      (et == wxEVT_MOVE) ||
      (et == wxEVT_ACTIVATE_APP) ||
      (et == wxEVT_ACTIVATE) ||
      (et == wxEVT_SHOW) ||
      (et == wxEVT_ICONIZE) ||
      (et == wxEVT_MAXIMIZE) ||
      (et == wxEVT_MENU_OPEN) ||
      (et == wxEVT_MENU_CLOSE) ||
      (et == wxEVT_MENU_HIGHLIGHT) ||
      (et == wxEVT_CONTEXT_MENU) ||
      (et == wxEVT_JOY_BUTTON_DOWN) ||
      (et == wxEVT_JOY_BUTTON_UP) ||
      (et == wxEVT_JOY_MOVE) ||
      (et == wxEVT_JOY_ZMOVE) ||
      (et == wxEVT_DROP_FILES) ||
      (et == wxEVT_COMMAND_TEXT_COPY) ||
      (et == wxEVT_COMMAND_TEXT_CUT) ||
      (et == wxEVT_COMMAND_TEXT_PASTE) ||
      (et == wxEVT_COMMAND_LEFT_CLICK) ||
      (et == wxEVT_COMMAND_LEFT_DCLICK) ||
      (et == wxEVT_COMMAND_RIGHT_CLICK) ||
      (et == wxEVT_COMMAND_RIGHT_DCLICK) ||
      (et == wxEVT_COMMAND_ENTER) ||
      (et == wxEVT_HELP) ||
      (et == wxEVT_DETAILED_HELP)) {
    RestartIdleTimer();
  }
  if (evt.IsCommandEvent() && evt.GetId() == wxID_HELP &&
      (et == wxEVT_COMMAND_BUTTON_CLICKED ||
       et == wxEVT_COMMAND_MENU_SELECTED)) {
    OnHelp(*wxDynamicCast(&evt, wxCommandEvent));
    return Event_Processed;
  }
  return wxApp::FilterEvent(evt);
}

void PWSafeApp::OnHelp(wxCommandEvent& evt)
{
  if (!isHelpActivated)
    return;
  
  wxWindow* window = wxDynamicCast(evt.GetEventObject(), wxWindow);
  
  if (window) {
    //The window associated with the event is typically the Help button.  Fail if
    //we can't get to its parent
    if (window->GetId() == wxID_HELP && ((window = window->GetParent()) == nullptr))
      return;

    wxString keyName, msg;
    //Is this a property sheet?
    wxPropertySheetDialog* propSheet = wxDynamicCast(window, wxPropertySheetDialog);
    if (propSheet) {
      const wxString dlgName = window->GetClassInfo()->GetClassName();
      const wxString pageName = propSheet->GetBookCtrl()->GetPageText(propSheet->GetBookCtrl()->GetSelection());
      keyName = dlgName + wxT('#') + pageName;
      msg << _("Missing help definition for page \"") << pageName
          << _("\" of \"") << dlgName
          << wxT("\".\n");
    }
    else { // !propSheet
      keyName = window->GetClassInfo()->GetClassName();
      msg << _("Missing help definition for window \"") << keyName
          << wxT("\".\n");
    }

    StringToStringMap& helpmap = GetHelpMap();
    StringToStringMap::iterator itr = helpmap.find(keyName);
    if (itr != helpmap.end()) {
      wxHtmlModalHelp help(window, helpFileNamePath, itr->second, wxHF_DEFAULT_STYLE);
    }
    else {
#ifdef __WXDEBUG__
      msg << _("Please inform the developers.");
      wxMessageBox(msg, _("Help Undefined"), wxOK | wxICON_EXCLAMATION);
#endif
    } // keyName not found in map
  }
  else {
    //just display the main page.  Could happen if the click came from a menu instead of
    //a button, like for the top-level frame
    wxHtmlModalHelp help(wxGetApp().GetTopWindow(), helpFileNamePath, wxT(""), wxHF_DEFAULT_STYLE);
  }
}

PWSafeApp::StringToStringMap& PWSafeApp::GetHelpMap()
{
  //may be its worth defining a class and doing all this in its ctor, but
  //I don't want to introduce yet another set of source/header files just for this
  static StringToStringMap helpMap;
  static bool initialized = false;

  if (!initialized) {
#define DLG_HELP(dlgname, htmlfile) helpMap[wxSTRINGIZE_T(dlgname)] = wxSTRINGIZE_T(htmlfile);
#define PROPSHEET_HELP(sheet, page, htmlfile) helpMap[wxString(wxSTRINGIZE_T(sheet) wxT("#")) + page] = wxSTRINGIZE_T(htmlfile);
#include "./HelpMap.h"
#undef DLG_HELP
#undef PROPSHEET_HELP
    initialized = true;
  }
  return helpMap;
}

/**
 * Restart timer using the same notification interval
*/
void PWSafeApp::RestartIdleTimer()
{
  if (m_idleTimer->IsRunning()) {
    int interval = m_idleTimer->GetInterval();
    m_idleTimer->Stop();
    m_idleTimer->Start(interval, wxTIMER_CONTINUOUS);
  }
}

bool PWSafeApp::IsCloseInProgress() const {
  return m_frame->IsCloseInProgress();
}
