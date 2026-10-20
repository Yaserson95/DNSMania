cmake_minimum_required(VERSION 3.16)
project(DNSMania VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SOURCES
    src/main.cpp
    src/core/JsonUtil.h
    src/core/JsonUtil.cpp
    src/core/PsClient.h
    src/core/PsClient.cpp
    src/core/ServerStore.h
    src/core/ServerStore.cpp
    src/core/SecureStore.h
    src/core/SecureStore.cpp
    src/core/DnsEntityService.h
    src/core/DnsEntityService.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/core/Journal.h
    src/core/Journal.cpp
    src/core/ZoneService.h
    src/core/ZoneService.cpp
    src/core/DnsZoneScope.h
    src/core/ZoneScopeService.h
    src/core/ZoneScopeService.cpp
    src/core/RecordService.h
    src/core/RecordService.cpp
    src/core/SubnetService.h
    src/core/SubnetService.cpp
    src/core/DnsPolicy.h
    src/core/DnsPolicy.cpp
    src/core/PolicyService.h
    src/core/PolicyService.cpp
    src/core/DnsService.h
    src/core/DnsService.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TreeIcons.h
    src/ui/TreeIcons.cpp
    src/ui/controls/LabeledLineEdit.h
    src/ui/controls/LabeledLineEdit.cpp
    src/ui/controls/LineListEdit.h
    src/ui/controls/LineListEdit.cpp
    src/ui/dialogs/BaseDialog.h
    src/ui/dialogs/BaseDialog.cpp
    src/ui/dialogs/ServerDialog.h
    src/ui/dialogs/ServerDialog.cpp
    src/ui/dialogs/ServerSelectDialog.h
    src/ui/dialogs/ServerSelectDialog.cpp
    src/ui/dialogs/SaveDialog.h
    src/ui/dialogs/SaveDialog.cpp
    src/ui/dialogs/ZoneDialog.h
    src/ui/dialogs/ZoneDialog.cpp
    src/ui/dialogs/ZoneEditDialog.h
    src/ui/dialogs/ZoneEditDialog.cpp
    src/ui/dialogs/ZoneScopeDialog.h
    src/ui/dialogs/ZoneScopeDialog.cpp
    src/ui/dialogs/ScopePropsDialog.h
    src/ui/dialogs/ScopePropsDialog.cpp
    src/ui/dialogs/RecordDialog.h
    src/ui/dialogs/RecordDialog.cpp
    src/ui/dialogs/SubnetDialog.h
    src/ui/dialogs/SubnetDialog.cpp
    src/ui/dialogs/PolicyDialog.h
    src/ui/dialogs/PolicyDialog.cpp
)

# Windows executable resource: embeds the icon into the .exe file itself.
if(WIN32)
    list(APPEND SOURCES src/resources/app.rc)
endif()

qt_add_executable(DNSMania ${SOURCES})

# Qt resource with the application icon (used for the window/taskbar icon).
qt_add_resources(DNSMania "app_icons"
    PREFIX "/"
    FILES resources/dnsmania.ico
)

# Build a GUI application (no console window).
set_target_properties(DNSMania PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(DNSMania PRIVATE src)

target_link_libraries(DNSMania PRIVATE Qt6::Widgets crypt32)

if(MSVC)
    target_compile_options(DNSMania PRIVATE /W3)
else()
    target_compile_options(DNSMania PRIVATE -Wall -Wextra)
endif()
