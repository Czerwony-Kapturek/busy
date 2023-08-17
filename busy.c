//////////////////////////////
//   Keep Busy utility
//////////////////////////////



// Some magic needed for LoadIconMetric not to throw error while linking app
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


// shell objects - needed for common directories in Windows
#include <Shlobj.h>


#include <assert.h>

// Needed for logging timestamps
#include <time.h>

#include <stdio.h>

// Resources
#include "resource.h"




// Who we are...
const wchar_t* APP_NAME = L"Keep Busy";



// Constant for converting minutes to miliseconds
#define ONE_SECOND_MILLIS              1000

// Treshold for discovering if machine is idle in seconds
#define DEFAULT_IDLE_TRESHOLD          25

// ID of timer for checking if machine is idle
#define EVENT_CHECK_IDLE   0x510A

// Message from our tray icon
#define MY_TRAY_MESSAGE                 WM_USER + 1

// Try icon menu IDs
#define MENU_ID_EXIT                    1
#define MENU_ID_SETTINGS                2
#define MENU_ID_TOGGLE                  3


#pragma region "DEBUG LOGGER"

// A slow, primitive, always flushing to disk, non thread safe file logger

#define MAX_MESSAGE_LEN 1024

boolean g_LoggingEnabled = TRUE;
WCHAR g_LogPath[MAX_PATH];

void initLogger(const WCHAR* name, boolean enable) {
    PWSTR logDir;
    SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &logDir);
    wsprintf(g_LogPath, L"%s\\%s.log", logDir, name);
    CoTaskMemFree(logDir);
    g_LoggingEnabled = enable;
}

void enableLogging(boolean enable) {
    g_LoggingEnabled = enable;
}


void ll(const WCHAR* fmt, ...) {
        if (!g_LoggingEnabled)
            return;

        WCHAR messageBuf[MAX_MESSAGE_LEN];
        time_t rawtime;
        struct tm timeinfo;
        time(&rawtime);
        localtime_s(&timeinfo, &rawtime);
        size_t used = wcsftime(messageBuf, 80, L"%F %T   ", &timeinfo);

        va_list args;
        va_start(args, fmt);
        size_t used2 = vswprintf_s(&messageBuf[used], MAX_MESSAGE_LEN - used - 2, fmt, args);
        messageBuf[used + used2] = L'\n';
        messageBuf[used + used2 + 1] = L'\0';

        FILE* f;
        errno_t err = _wfopen_s(&f, g_LogPath, L"a");
        if (!err) {
            fputws(messageBuf, f);
            fclose(f);
        }

        if (IsDebuggerPresent()) {
            OutputDebugStringW(messageBuf);
        }
}

#pragma endregion



#pragma region "GENERIC SETTINGS UTILITIES"



typedef enum {
    Integer,
    Boolean,
    String
} SettingsType;

typedef struct {
    int id;
    WCHAR* iniName;
    SettingsType type;
    union {
        boolean boolValue;
        int     intValue;
        WCHAR*  strValue;
    };
} Setting;


WCHAR g_iniPath[MAX_PATH];


typedef enum {
    Off = 0,
    On,
    Last
} AppMode;


typedef enum {
    CurrentMode,
    StartMode,
    Interval,
    EnableDebugLog
} AppSettings;

// Important: order of settings follows enum above.
Setting g_Settings[] = {
    {CurrentMode,        L"CurrentMode",         Integer,   Off},
    {StartMode,          L"StartMode",           Integer,   Off},
    {Interval,           L"Frequency",           Integer,   (int)35},
    {EnableDebugLog,     L"EnableDebugLog",      Boolean,   FALSE}
};


int settingsGetInt(AppSettings id) {
    return g_Settings[id].intValue;
}

boolean settingsGetBool(AppSettings id) {
    return g_Settings[id].boolValue;
}

WCHAR const* settingsGetStr(AppSettings id) {
    return g_Settings[id].strValue;
}

void settingsSetInt(AppSettings id, int val) {
    g_Settings[id].intValue = val;
}

void settingsSetBool(AppSettings id, boolean val) {
    g_Settings[id].boolValue = val;
}

void settingsSetStr(AppSettings id, const WCHAR* val) {
    assert(g_Settings[id].strValue);
    free(g_Settings[id].strValue);
    size_t len = wcslen(val);
    g_Settings[id].strValue = (WCHAR*)malloc((len + 1) * sizeof(WCHAR));
    wcscpy_s(g_Settings[id].strValue, len, val);
}

void settingsLoad() {
    FILE* f;
    errno_t err = _wfopen_s(&f, g_iniPath, L"r");
    if (!err) {
        int settingsCount = sizeof(g_Settings) / sizeof(g_Settings[0]);
        WCHAR buff[MAX_PATH]; // Assuming max len of the line in ini file.
        WCHAR* line = fgetws(buff, MAX_PATH, f);
        while (line) {
            WCHAR* pos = wcschr(line, L'=');
            if (pos <= 0) {
                continue;
            }
            buff[wcslen(line) - 1] = L'\0';
            *pos = L'\0';
            WCHAR* key = line;
            WCHAR* value = ++pos;
            // Find the key in our settings and update settings.
            for (int i = 0; i < settingsCount; i++) {
                if (wcscmp(key, g_Settings[i].iniName) == 0) {
                    switch (g_Settings[i].type) {
                    case Integer:
                    {
                        int val = _wtol(value);
                        g_Settings[i].intValue = val;
                    }
                    break;

                    case Boolean:
                    {
                        boolean val = wcscmp(L"true", value) ? FALSE : TRUE;
                        g_Settings[i].boolValue = val;
                    }
                    break;

                    case String:
                    {
                        settingsSetStr((AppSettings)i, value);
                    }
                    break;

                    }
                    ll(L"Setting read: %s == %s", key, value);
                    break;
                }
            }
            line = fgetws(buff, MAX_PATH, f);

        }
        fclose(f);
    }
}


void settingsSave() {
    FILE* f;
    errno_t err = _wfopen_s(&f, g_iniPath, L"w");
    if (!err) {
        int settingsCount = sizeof(g_Settings) / sizeof(g_Settings[0]);
        WCHAR buff[MAX_PATH]; // Assuming max len of the line in ini file.

        for (int i = 0; i < settingsCount; i++) {
            switch (g_Settings[i].type) {
            case Integer:
                wsprintf(buff, L"%s=%i\n", g_Settings[i].iniName, g_Settings[i].intValue);
                break;

            case Boolean:
                wsprintf(buff, L"%s=%s\n", g_Settings[i].iniName, g_Settings[i].boolValue ? L"true" : L"false");
                break;

            case String:
                wsprintf(buff, L"%s=%s\n", g_Settings[i].iniName, g_Settings[i].strValue);
                break;
            }
            fputws(buff, f);
        }
        fclose(f);
    }
}


void settingsInitialize(const WCHAR* fileName) {
    // Strings in g_Settings are initialized using literals = change to allocated memory
    // so we can free it and alloc again when settings are changing.
    int settingsCount = sizeof(g_Settings) / sizeof(g_Settings[0]);
    for (int i = 0; i < settingsCount; i++) {
        if (g_Settings[i].type == String) {
            size_t len = wcslen(g_Settings[i].strValue);
            WCHAR* buff = (WCHAR*)malloc((len + 1) * sizeof(WCHAR));
            wcscpy_s(buff, len, g_Settings[i].strValue);
            g_Settings[i].strValue = buff;
        }
    }

    PWSTR settingsDir;
    SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &settingsDir);
    wsprintf(g_iniPath, L"%s\\%s.ini", settingsDir, fileName);
    CoTaskMemFree(settingsDir);

    settingsLoad();
    settingsSave();
}


void settingsCleanup() {
    // Strings in g_Settings are allocated on heap - free them.
    int settingsCount = sizeof(g_Settings) / sizeof(g_Settings[0]);
    for (int i = 0; i < settingsCount; i++) {
        if (g_Settings[i].type == String) {
            free(g_Settings[i].strValue);
        }
    }
}



#pragma endregion



#pragma region "APPLICATION SPECIFIC SETTINGS + CONFIG DIALOG"


void enableAutoStart(boolean ena) {
    HKEY key = NULL;
    RegCreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &key);
    if (ena) {
        wchar_t exePath[MAX_PATH + 1];
        GetModuleFileName(NULL, exePath, MAX_PATH + 1);
        RegSetValueEx(key, APP_NAME, 0, REG_SZ, (BYTE*)exePath, (DWORD)(wcslen(exePath) + 1) * 2);
    }
    else {
        RegDeleteValue(key, APP_NAME);
    }
}

boolean isAutoStartEnabled() {
    HKEY key = NULL;
    int err = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key);
    if (err == ERROR_SUCCESS) {
        DWORD type;
        err = RegQueryValueEx(key, APP_NAME, NULL, &type, NULL, NULL);
        if (err == ERROR_SUCCESS && type == REG_SZ) {
            return TRUE;
        }
    }
    return FALSE;
}


void initDlgAndLoadSettings(HWND window)
{
    // Most settings are accessed via SETTINGS global, autostart goes directly to/from registry
    CheckDlgButton(window, IDC_AUTO_START, isAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED);

    // Initial mode wen program starts
    const wchar_t* modesNames[] = {
        L"No", 
        L"Yes", 
        L"If it was avtive during last run", 
    };
    HWND comboDisplayMode = GetDlgItem(window, IDC_INITIAL_MODE);
    int sel = 0;
    AppMode mode = Off;
    for (int i = sizeof(modesNames) / sizeof(modesNames[0]) - 1; i >= 0; i--) {
        SendMessage(comboDisplayMode, CB_INSERTSTRING, 0, (LPARAM)modesNames[i]);
        SendMessage(comboDisplayMode, CB_SETITEMDATA, 0, mode + i);
        if (settingsGetInt(StartMode) == mode + i) {
            sel = i;
        }
    }
    SendMessage(comboDisplayMode, CB_SETCURSEL, sel, 0);

    SendMessage(GetDlgItem(window, IDC_INTERVAL_SPIN), UDM_SETRANGE32, 1, MAXINT32);
    // Setting buddy makes the editbox too narrow
    //SendMessage(GetDlgItem(window, IDC_INTERVAL_SPIN), UDM_SETBUDDY, (int)GetDlgItem(window, IDC_INTERVAL), 0);
    SetDlgItemInt(window, IDC_INTERVAL, settingsGetInt(Interval), FALSE);

    CheckDlgButton(window, IDC_DEBUG_LOG, settingsGetBool(EnableDebugLog) ? BST_CHECKED : BST_UNCHECKED);
}


boolean saveSettingsFromDlg(HWND window)
{
    boolean autoStart = (boolean)IsDlgButtonChecked(window, IDC_AUTO_START);

    int modePos = (int)SendMessage(GetDlgItem(window, IDC_INITIAL_MODE), CB_GETCURSEL, 0, 0);
    int initialMode = (int)SendMessage(GetDlgItem(window, IDC_INITIAL_MODE), CB_GETITEMDATA, modePos, 0);

    BOOL inputIntervalOk;
    int inputInterval = GetDlgItemInt(window, IDC_INTERVAL, &inputIntervalOk, FALSE);
    if (!inputIntervalOk) {
        // Valid integer required, notify that we have a problem
        MessageBox(window, L"Interval must be a number grater than 0", L"Wrong value", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    boolean debug = (boolean)IsDlgButtonChecked(window, IDC_DEBUG_LOG);

    // All data ok, save the settings
    enableAutoStart(autoStart);
    settingsSetInt(StartMode, initialMode);
    settingsSetInt(Interval, inputIntervalOk ? inputInterval : DEFAULT_IDLE_TRESHOLD);
    settingsSetBool(EnableDebugLog, debug);
    // In case of debug log configure logging 
    enableLogging(debug);

    settingsSave();

    return TRUE;
}


INT_PTR CALLBACK DialogProc(HWND window, unsigned int msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_INITDIALOG:
            initDlgAndLoadSettings(window);
            return TRUE;

        case WM_CLOSE:
            DestroyWindow(window);
            return TRUE;

        case WM_DESTROY:
            return TRUE;

        case WM_KEYDOWN:
            if (VK_CANCEL == wp) {
                EndDialog(window, IDCANCEL);
                return TRUE;
            }
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case IDCANCEL:
                EndDialog(window, IDCANCEL);
                return TRUE;
            case IDOK:
                if (saveSettingsFromDlg(window)) {
                    EndDialog(window, IDOK);
                }
                return TRUE;
            }
            break;

        case WM_NOTIFY:
        {
            int code = ((LPNMHDR)lp)->code;
            switch (code)
            {
                // Up/down arrows for the interval.
                case UDN_DELTAPOS:
                {
                    LPNMUPDOWN ud = (LPNMUPDOWN)lp;
                    BOOL autoChangeIntervalOk;
                    int autoChangeInterval = GetDlgItemInt(window, IDC_INTERVAL, &autoChangeIntervalOk, FALSE);
                    if (autoChangeIntervalOk) {
                        SetDlgItemInt(window, IDC_INTERVAL, autoChangeInterval + ud->iDelta, FALSE);
                    }
                    break;
                }
            }
        }
    }

    return FALSE;
}


void showSettingsDialog(HWND window)
{
    static boolean showing = FALSE;
    if (showing) {
    }
    else {
        // Poorman's synchronization to avoid multiple copies of settings dialog.
        showing = TRUE;
        INT_PTR res = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), window, DialogProc);
        if (res == IDOK) {
            if (settingsGetInt(CurrentMode) == On) {
                // If keeping computer busy is enabled change timer to current settings.
                SetTimer(window, EVENT_CHECK_IDLE, ONE_SECOND_MILLIS * settingsGetInt(Interval), NULL);
            }
        }
        showing = FALSE;
    }
}

#pragma endregion




#pragma region "WINDOWS APPLICATION + GUI"

UINT g_WM_TASKBARCREATED;

boolean createNotificationIcon(HWND window)
{
    int iconId = settingsGetInt(CurrentMode) == On ? IDI_ACTIVE : IDI_INACTIVE;

    ll(L"Creating notification icon %i", iconId);

    HICON icon = 0;
    LoadIconMetric(GetModuleHandle(NULL), MAKEINTRESOURCE(iconId), LIM_SMALL, &icon);

    NOTIFYICONDATA nid;
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.uVersion = NOTIFYICON_VERSION_4;
    // System uses window handle and ID to identify the icon - we just have one so ID == 0
    nid.hWnd = window;
    nid.uID = 0;
    nid.hIcon = icon;
    // Following message will be sent to our main window when user interacts with the tray icon
    nid.uCallbackMessage = MY_TRAY_MESSAGE;
    const WCHAR* tip = settingsGetInt(CurrentMode) == On
        ? L"Active - keeping your computer busy\n\nDouble-click to deactivate\nRigth-click for menu"
        : L"Inactive\n\nDouble-click to activate\nRight-click for menu";
    wcscpy_s(nid.szTip, 128 - 1, tip);
    // Add the icon with tooltip and sending messagess to the parent window
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    boolean ret = (boolean)Shell_NotifyIcon(NIM_ADD, &nid);

    // Set the version
    // Said to be required but thisngs seem to work without it.
    //Shell_NotifyIcon(NIM_SETVERSION, &nid);

    return ret;
}



// Get rid of the icon in tray
boolean deleteNotificationIcon(HWND window)
{
    ll(L"Deleting notification icon");
    // System uses window handle and ID to identify the icon
    NOTIFYICONDATA nid;
    nid.hWnd = window;
    nid.uID = 0;
    // Delete the icon
    return (boolean)Shell_NotifyIcon(NIM_DELETE, &nid);
}



// Show context menu for the tray icon
void showContextMenu(HWND window)
{
    // They say it is needed...
    SetForegroundWindow(window);

    // Point at which to display the menu - not very carelully choosen..
    POINT point;
    GetCursorPos(&point);

    HMENU menu = CreatePopupMenu();
    AppendMenu(menu, MF_STRING, MENU_ID_TOGGLE, settingsGetInt(CurrentMode) == On ? L"Deactivate" : L"Activate");
    AppendMenu(menu, MF_STRING, MENU_ID_SETTINGS, L"Settings...");
    AppendMenu(menu, MF_STRING, MENU_ID_EXIT, L"Exit");
    TrackPopupMenu(menu, TPM_CENTERALIGN | TPM_VCENTERALIGN | TPM_LEFTBUTTON, point.x, point.y, 0, window, NULL);

    DestroyMenu(menu);
}


void toggleKeepBusy(HWND window) {
    int newMode = settingsGetInt(CurrentMode) == On ? Off : On;
    if (newMode == On) {
        SetTimer(window, EVENT_CHECK_IDLE, ONE_SECOND_MILLIS * settingsGetInt(Interval), NULL);
        ll(L"Activated");
    }
    else {
        KillTimer(window, EVENT_CHECK_IDLE);
        ll(L"Deactivated");
    }
    settingsSetInt(CurrentMode, newMode);
    settingsSave();
    deleteNotificationIcon(window);
    createNotificationIcon(window);
}


void moveMouse() {
    ll(L"Moving mouse");
    mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, (ULONG_PTR)NULL);
}

void pressNumLock() {
    ll(L"Pressing NumLock 2 times");
    // Key press and release
    keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
    keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    // ...and again - so the state of NumLock don't change
    keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
    keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}


boolean isIdle() {
    LASTINPUTINFO lastInput;
    lastInput.cbSize = sizeof(lastInput);
    GetLastInputInfo(&lastInput);
    int idleSeconds = (GetTickCount() - lastInput.dwTime) / 1000;
    // Assue if idle if no input since last timer event (minus one second) 
    return idleSeconds > settingsGetInt(Interval) - 1;
}


LRESULT __stdcall WndProc(HWND window, unsigned int msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KILLFOCUS:
            ShowWindow(window, SW_HIDE);
            return 0;

        case WM_TIMER:
            ll(L"WM_TIMER");
            if (wp == EVENT_CHECK_IDLE) {
                if (isIdle()) {
                    ll(L"machine is idle - send some input");
                    moveMouse();
                    pressNumLock();
                }
                else {
                    ll(L"machine IS NOT idle - do nothing");
                }
            }
            return 0;

        case WM_COMMAND:
            if (HIWORD(wp) == 0) {
                switch (LOWORD(wp)) {
                    case MENU_ID_EXIT:
                        PostQuitMessage(0);
                        break;
                    case MENU_ID_SETTINGS:
                        showSettingsDialog(window);
                        break;
                    case MENU_ID_TOGGLE:
                        toggleKeepBusy(window);
                        break;
                }
            }
            return 0;

        case MY_TRAY_MESSAGE:
            switch (lp)
            {
                case WM_LBUTTONDBLCLK:
                    toggleKeepBusy(window);
                    break;
                case WM_RBUTTONDOWN:
                case WM_CONTEXTMENU:
                    showContextMenu(window);
                    break;
            }
            return 0;
    }

    // Dynamically obtained message ID - cannot use in switch above
    if (msg == g_WM_TASKBARCREATED) {
        // Explorer was restarted - create the icon again (just in case delete it first)
        deleteNotificationIcon(window);
        createNotificationIcon(window);
        return 0;
    }

    return DefWindowProc(window, msg, wp, lp);
}



// Register window class and create main application window
HWND createWindow(const WCHAR* name, WNDPROC wndProc)
{
    WNDCLASSEX wndclass = { sizeof(WNDCLASSEX), CS_DBLCLKS, wndProc,
        0, 0, GetModuleHandle(0), LoadIcon(0,IDI_APPLICATION),
        LoadCursor(0,IDC_ARROW), 0, // HBRUSH set to 0
        0, name, LoadIcon(0,IDI_APPLICATION) };

    if (RegisterClassEx(&wndclass))
    {
        UINT flags = WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME;
        HWND window = CreateWindowEx(0, name, name,
            flags & ~WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            200, 200, 0, 0, GetModuleHandle(0), 0);

        return window;
    }

    return NULL;
}



int __stdcall WinMain(_In_ HINSTANCE hInstance,
    _In_ HINSTANCE hPrevInstance,
    _In_ LPSTR     lpCmdLine,
    _In_ int       nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    settingsInitialize(APP_NAME);

    initLogger(APP_NAME, settingsGetBool(EnableDebugLog));
    ll(L"========= BEGIN =========");

    // Allow only single instance of the application
    HWND oldWindow = FindWindow(APP_NAME, APP_NAME);
    if (oldWindow != NULL) {
        ll(L"Activate previous instance and exit");
        PostMessage(oldWindow, WM_COMMAND, MENU_ID_TOGGLE, 0);
        return 0;
    }

    // Windows Explorer (not Windows OS!) message - needed to handle case Explorer is restarted.
    // We will need to create tray icon again.
    g_WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");

    // Create main window for message handling and tray icon
    HWND window = createWindow(APP_NAME, WndProc);

    // Check configured mode and last state
    int startMode = settingsGetInt(StartMode);
    int lastMode = settingsGetInt(CurrentMode);
    if (startMode == On ||
        (startMode == Last && lastMode == On)) {
        ll(L"Started as enabled");
        SetTimer(window, EVENT_CHECK_IDLE, ONE_SECOND_MILLIS * settingsGetInt(Interval), NULL);
        settingsSetInt(CurrentMode, On);
    }
    else
    {
        ll(L"Started as inactive");
        settingsSetInt(CurrentMode, Off);
    }
    settingsSave();

    // Create tray icon - the only way to interact with the program
    createNotificationIcon(window);

    // Main program loop
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) DispatchMessage(&msg);

    // Remove icon from tray
    deleteNotificationIcon(window);

    settingsCleanup();

    return 0;
}

#pragma endregion