rc.exe /nologo busy.rc

:: Libraries:
::   user32.dll    - Majority of functions
::   Ole32.lib     - CoTaskMemFree
::   Advapi32.lib  - Registry functions
::   Shell32.lib   - Shell_NotifyIcon, SHGetKnownFolderPath
::   Comctl32.lib  - LoadIconMetric 
:: Compiler options:
::   /DUNICODE /D_UNICODE - wchar suport
::   /nologo - do not print compiler info
::   /MT - statically linked app
::   /W4 - highest warning level
::   /std:c11 - C language version, default would cause warning for union syntax
::   /link /MANISEST:EMBED - ask linker to embed manifest - without it app on start complains about missing functions

cl.exe busy.c busy.res user32.lib Ole32.lib Advapi32.lib Shell32.lib Comctl32.lib /DUNICODE /D_UNICODE /nologo /MT /W4 /std:c11 /link /MANIFEST:EMBED



