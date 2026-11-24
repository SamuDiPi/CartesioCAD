cmake_minimum_required(VERSION 3.21)

project(CartesioCAD
    VERSION 0.6.0
    DESCRIPTION "CAD 2D open source"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CARTESIO_BUILD_APP   "Compila l'applicazione Qt"  ON)
option(CARTESIO_BUILD_TESTS "Compila i test del nucleo"  ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Nucleo: geometria, documento, comandi, I/O (nessuna dipendenza da Qt) ---
add_library(cartesio_core STATIC
    src/core/Entity.cpp
    src/core/Prim.cpp
    src/core/Document.cpp
    src/core/Annotation.cpp
    src/core/Compare.cpp
    src/core/Canalo.cpp
    src/core/Plant.cpp
    src/core/Ampacity.cpp
    src/core/Geometry.cpp
    src/core/Osnap.cpp
    src/core/TitleBlock.cpp
    src/core/Version.cpp
    src/core/Routing.cpp
    src/core/RunImport.cpp
    src/core/Plot.cpp
    src/core/SingleLine.cpp
    src/core/CircuitTags.cpp
    src/core/PointLegend.cpp
    src/core/HatchBoundary.cpp
    src/core/Import.cpp
    src/core/DocCompare.cpp
    src/core/Attributes.h
    src/core/Attributes.cpp
    src/core/FindReplace.h
    src/core/FindReplace.cpp
    src/core/QuickSelect.h
    src/core/QuickSelect.cpp
    src/core/InputParser.cpp
    src/core/commands/CommandManager.cpp
    src/core/commands/Selection.cpp
    src/core/commands/BuiltinCommands.cpp
    src/core/commands/ModifyCommands.cpp
    src/core/commands/AnnotateCommands.cpp
    src/core/commands/BlockCommands.cpp
    src/core/commands/LayoutCommands.cpp
    src/core/commands/SystemCommands.cpp
    src/core/commands/EditCommands.cpp
    src/core/commands/ExtraCommands.cpp
    src/core/io/DxfWriter.cpp
    src/core/io/DxfWriter2000.cpp
    src/core/io/DxfReader.cpp
)
target_include_directories(cartesio_core PUBLIC src)
# zlib (licenza Zlib, già distribuita con Qt): decompressione delle tabelle di stampa .ctb con un tetto.
find_package(ZLIB REQUIRED)
target_link_libraries(cartesio_core PRIVATE ZLIB::ZLIB)

# --- Poppler (facoltativo): lettura dei PDF degli schemi unifilari (poppler-cpp, GPL-2.0-or-later) ---
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(POPPLER_CPP IMPORTED_TARGET poppler-cpp)
endif()
if(POPPLER_CPP_FOUND)
    message(STATUS "Lettura PDF: Poppler ${POPPLER_CPP_VERSION}")
else()
    message(STATUS "Lettura PDF non inclusa: manca poppler-cpp (pacman -S mingw-w64-ucrt-x86_64-poppler)")
endif()

# --- Applicazione desktop Qt ---
if(CARTESIO_BUILD_APP)
    # Qt Quick: il disegno su GPU (scene graph con Direct3D/Vulkan/OpenGL tramite RHI).
    set(QT_NO_PRIVATE_MODULE_WARNING ON)   # QtGui privato: il triangolatore dei riempimenti
    find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets PrintSupport Qml Quick QuickWidgets GuiPrivate Network)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    qt_standard_project_setup()

    # Motore di CanaloCAD: il codice della pagina (plugins/CanaloCAD/engine, da scripts/update-canalo.sh)
    # eseguito con il motore JavaScript di Qt; incorporato nell'eseguibile.
    qt_add_library(cartesio_canalo_engine STATIC src/app/CanaloEngine.h src/app/CanaloEngine.cpp)
    qt_add_resources(cartesio_canalo_engine "canalo_engine" PREFIX "/canalo" BASE plugins/CanaloCAD/engine
        FILES plugins/CanaloCAD/engine/host.js plugins/CanaloCAD/engine/canalo-engine.js
              plugins/CanaloCAD/engine/cartesio-api.js)
    target_include_directories(cartesio_canalo_engine PUBLIC src)
    # HarfBuzz: larghezza dei testi come il browser (crenatura e unità esatte del font).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
    target_link_libraries(cartesio_canalo_engine PUBLIC Qt6::Gui Qt6::Qml PRIVATE PkgConfig::HARFBUZZ)

    qt_add_executable(CartesioCAD WIN32 MACOSX_BUNDLE
        src/app/main.cpp
        $<$<PLATFORM_ID:Windows>:src/app/cartesiocad.rc>
        src/app/MainWindow.h
        src/app/MainWindow.cpp
        src/app/MainWindowLayouts.cpp
        src/app/Plot.cpp
        src/app/PlotEngine.h
        src/app/PlotDialog.h
        src/app/PlotDialog.cpp
        src/app/MainWindowPlot.cpp
        src/app/AppOptions.h
        src/app/AppOptions.cpp
        src/app/OptionsDialog.h
        src/app/OptionsDialog.cpp
        src/app/FormDialog.h
        src/app/FormDialog.cpp
        src/app/UiControls.h
        src/app/UiControls.cpp
        src/app/ExternalRefs.h
        src/app/ExternalRefs.cpp
        src/app/OleImage.h
        src/app/OleImage.cpp
        src/app/MainWindowXref.cpp
        src/app/MainWindowAttributes.cpp
        src/app/MainWindowFind.cpp
        src/app/MainWindowQuickSelect.cpp
        src/app/MainWindowDiag.cpp
        src/app/MainWindowConvert.cpp
        src/app/MainWindowTransmit.cpp
        src/app/MainWindowUpdate.cpp
        src/app/MainWindowClaude.cpp
        src/app/ClaudeLink.h
        src/app/ClaudeLink.cpp
        src/app/ClaudeChannel.h
        src/app/PlantRoutes.cpp
        src/app/PlantRunImport.cpp
        src/app/Updater.cpp
        src/app/MainWindowTextEditor.cpp
        src/app/MainWindowRibbonTabs.cpp
        src/app/MainWindowCompare.cpp
        src/app/SpellCheck.cpp
        src/app/StyleDialogs.cpp
        src/app/PlotStyleManager.cpp
        src/app/CanvasItem.h
        src/app/CanvasItem.cpp
        src/app/MainView.h
        src/app/MainView.cpp
        src/app/PlotTools.h
        src/app/PlotTools.cpp
        src/app/CanaloPlugin.h
        src/app/CanaloPlugin.cpp
        src/app/MainWindowCanalo.cpp
        src/app/CanaloTab.h
        src/app/CanaloTab.cpp
        src/app/WebView2Host.h
        src/app/WebView2Host.cpp
        src/app/MainWindowAbout.cpp
        src/app/PdfWords.h
        src/app/PdfWords.cpp
        src/app/CircuitsPanel.h
        src/app/CircuitsPanel.cpp
        src/app/PlantPanel.cpp
        src/app/CanaloPanel.h
        src/app/CanaloPanel.cpp
        src/app/GuidePanel.cpp
        src/app/MainWindowGuide.cpp
        src/app/PlantFile.cpp
        src/app/BlockLibrary.h
        src/app/BlockLibrary.cpp
        src/app/DimStyleDialog.cpp
        src/app/CadView.h
        src/app/CadView.cpp
        src/app/GpuView.h
        src/app/GpuView.cpp
        src/app/TextRender.h
        src/app/TextRender.cpp
        src/app/TextEditInPlace.h
        src/app/TextEditInPlace.cpp
        src/app/QmlUi.h
        src/app/QmlUi.cpp
        src/app/ImageLibrary.h
        src/app/ImageLibrary.cpp
        src/app/ColorDialog.h
        src/app/ColorDialog.cpp
        src/app/CommandLine.h
        src/app/CommandLine.cpp
        src/app/DraftSettings.h
        src/app/DrawingWidget.h
        src/app/DrawingWidget.cpp
        src/app/DwgConverter.h
        src/app/DwgConverter.cpp
        src/app/Icons.h
        src/app/Icons.cpp
        src/app/LayerManager.h
        src/app/LayerManager.cpp
        src/app/PropertiesPalette.h
        src/app/PropertiesPalette.cpp
        src/app/Ribbon.h
        src/app/Ribbon.cpp
        src/app/StartPage.h
        src/app/StartPage.cpp
        src/app/Thumbnails.h
        src/app/Thumbnails.cpp
        src/app/Theme.h
        src/app/Theme.cpp
    )
    # Guida: manuale (docs/manuale) e percorsi guidati, nelle risorse.
    file(GLOB manual_pages CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/docs/manuale/*.md")
    qt_add_resources(CartesioCAD "manuale" PREFIX "/manuale" BASE docs/manuale FILES ${manual_pages})
    qt_add_resources(CartesioCAD "percorsi" PREFIX "/guide" BASE src/app/guide FILES src/app/guide/percorsi.json)
    # Interfaccia in QML (barra multifunzione, schede, barra di stato, riga di comando), compilata nelle risorse.
    qt_add_resources(CartesioCAD "interfaccia_qml" PREFIX "/qml" BASE src/app/qml
        FILES src/app/qml/Theme.qml src/app/qml/Ribbon.qml src/app/qml/CommandLine.qml src/app/qml/FileTabs.qml
              src/app/qml/StatusBar.qml src/app/qml/PropertiesPalette.qml src/app/qml/LayerManager.qml
              src/app/qml/CircuitsPanel.qml src/app/qml/PlantPanel.qml src/app/qml/CanaloPanel.qml src/app/qml/GuidePanel.qml src/app/qml/BlockLibrary.qml src/app/qml/FormDialog.qml src/app/qml/StartPage.qml
              src/app/qml/PlotDialog.qml src/app/qml/MainView.qml
              src/app/qml/PlotPreview.qml src/app/qml/StyleEditor.qml src/app/qml/UiCheckBox.qml src/app/qml/UiComboBox.qml
              src/app/qml/UiSpinBox.qml src/app/qml/UiPushButton.qml src/app/qml/UiLineEdit.qml src/app/qml/UiBox.qml)
    target_link_libraries(CartesioCAD PRIVATE cartesio_core cartesio_canalo_engine Qt6::Widgets Qt6::PrintSupport Qt6::Qml Qt6::Quick Qt6::QuickWidgets
                          Qt6::GuiPrivate Qt6::Network OpenSSL::Crypto)
    if(WIN32)
        target_link_libraries(CartesioCAD PRIVATE gdi32 ole32)   # metafile degli oggetti OLE, correttore ortografico
    endif()
    if(POPPLER_CPP_FOUND)
        target_link_libraries(CartesioCAD PRIVATE PkgConfig::POPPLER_CPP)
        target_compile_definitions(CartesioCAD PRIVATE CARTESIO_HAS_POPPLER)
    endif()
    target_compile_definitions(CartesioCAD PRIVATE CARTESIO_VERSION="${PROJECT_VERSION}")
    # Commit e data della versione (finestra Informazioni: i sorgenti corrispondenti al programma). Si
    # aggiorna a ogni commit (riconfigurazione automatica) e ricompila solo MainWindowAbout.cpp.
    set(CARTESIO_COMMIT "")
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%h|%cs
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE CARTESIO_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD")
        endif()
    endif()
    set_source_files_properties(src/app/MainWindowAbout.cpp PROPERTIES
        COMPILE_DEFINITIONS "CARTESIO_COMMIT=\"${CARTESIO_COMMIT}\"")

    # Connettore per Claude Desktop (MCP su stdin/stdout): programma da console che inoltra le richieste a
    # CartesioCAD sul canale locale dell'utente. Usa solo QtCore e QtNetwork, già distribuite.
    add_executable(cartesio-mcp src/mcp/main.cpp src/app/ClaudeChannel.h)
    target_include_directories(cartesio-mcp PRIVATE src)
    target_link_libraries(cartesio-mcp PRIVATE Qt6::Core Qt6::Network)
    target_compile_definitions(cartesio-mcp PRIVATE CARTESIO_VERSION="${PROJECT_VERSION}")
    add_dependencies(CartesioCAD cartesio-mcp)

    # Supporto DWG: i convertitori di LibreDWG (compilati con le correzioni da scripts/build-libredwg.sh, o
    # quelli ufficiali di scripts/fetch-libredwg.sh) vengono copiati nella cartella "libredwg" accanto all'eseguibile.
    set(CARTESIO_LIBREDWG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libredwg-0.14.8413"
        CACHE PATH "Cartella con dwg2dxf e dxf2dwg (e libredwg-0.dll se non sono statici)")
    if(WIN32 AND EXISTS "${CARTESIO_LIBREDWG_DIR}/dwg2dxf.exe")
        set(libredwg_files
            "${CARTESIO_LIBREDWG_DIR}/dwg2dxf.exe"
            "${CARTESIO_LIBREDWG_DIR}/dxf2dwg.exe")
        foreach(extra libredwg-0.dll libiconv-2.dll COPYING)
            if(EXISTS "${CARTESIO_LIBREDWG_DIR}/${extra}")
                list(APPEND libredwg_files "${CARTESIO_LIBREDWG_DIR}/${extra}")
            endif()
        endforeach()
        add_custom_command(TARGET CartesioCAD POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CartesioCAD>/libredwg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${libredwg_files} "$<TARGET_FILE_DIR:CartesioCAD>/libredwg"
            COMMENT "Copia di LibreDWG (supporto DWG)"
            VERBATIM)
        install(FILES ${libredwg_files} DESTINATION libredwg)
        message(STATUS "Supporto DWG: LibreDWG in ${CARTESIO_LIBREDWG_DIR}")
    elseif(WIN32)
        message(STATUS "Supporto DWG non incluso: eseguire scripts/fetch-libredwg.sh")
    else()
        message(STATUS "Supporto DWG: dwg2dxf e dxf2dwg di LibreDWG nella cartella libredwg accanto al programma (pacchetto Flatpak)")
    endif()

    # Dove vanno i file che stanno accanto all'eseguibile: su Windows la radice della cartella da distribuire,
    # su Linux lib/cartesiocad (con un collegamento al programma in bin).
    if(WIN32)
        set(CARTESIO_APP_DEST ".")
        set(CARTESIO_PLUGIN_DEST "plugins/CanaloCAD")
    else()
        set(CARTESIO_APP_DEST "lib/cartesiocad")
        set(CARTESIO_PLUGIN_DEST "lib/cartesiocad/plugins/CanaloCAD")
    endif()

    # Plugin CanaloCAD: l'app web (un solo file HTML) viaggia con CartesioCAD, in
    # plugins/CanaloCAD accanto all'eseguibile. Si aggiorna con scripts/update-canalo.sh.
    set(canalo_files
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CanaloCAD/Canalo.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CanaloCAD/VERSIONE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CanaloCAD/OFL-Inter.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CanaloCAD/OFL-JetBrainsMono.txt")
    add_custom_command(TARGET CartesioCAD POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CartesioCAD>/plugins/CanaloCAD"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${canalo_files} "$<TARGET_FILE_DIR:CartesioCAD>/plugins/CanaloCAD"
        COMMENT "Copia del plugin CanaloCAD"
        VERBATIM)
    install(FILES ${canalo_files} DESTINATION ${CARTESIO_PLUGIN_DEST})

    # Scheda CanaloCAD su Windows: Microsoft Edge WebView2 (il motore è il WebView2 Runtime di Windows 10/11).
    # WebView2Loader.dll (BSD-3-Clause, pacchetto MSYS2 webview2-loader) si carica a richiesta, solo accanto al
    # programma; senza, CanaloCAD si apre nel browser.
    if(WIN32)
        find_file(CARTESIO_WEBVIEW2_LOADER WebView2Loader.dll HINTS "$<TARGET_FILE_DIR:Qt6::Core>" ENV PATH)
        get_filename_component(compiler_bin_wv2 "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_file(CARTESIO_WEBVIEW2_LOADER WebView2Loader.dll HINTS "${compiler_bin_wv2}")
        if(CARTESIO_WEBVIEW2_LOADER)
            add_custom_command(TARGET CartesioCAD POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARTESIO_WEBVIEW2_LOADER}" "$<TARGET_FILE_DIR:CartesioCAD>"
                COMMENT "Copia di WebView2Loader.dll (scheda CanaloCAD)"
                VERBATIM)
            install(FILES "${CARTESIO_WEBVIEW2_LOADER}" DESTINATION .)
            message(STATUS "Scheda CanaloCAD: ${CARTESIO_WEBVIEW2_LOADER}")
        else()
            message(STATUS "Scheda CanaloCAD non inclusa: manca WebView2Loader.dll (pacman -S mingw-w64-ucrt-x86_64-webview2-loader)")
        endif()
    endif()

    # Windows + MinGW: dopo ogni compilazione copia le DLL accanto all'exe (avvio con doppio clic),
    # e "cmake --install" crea una cartella pulita da distribuire.
    if(WIN32 AND MINGW AND CMAKE_OBJDUMP)
        get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(deploy_args
            -DPLATFORM_PLUGIN=$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
            "-DTLS_PLUGIN=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/../tls/qschannelbackend.dll"
            "-DSEARCH_DIRS=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${compiler_bin}"
            -DOBJDUMP=${CMAKE_OBJDUMP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake)

        add_custom_command(TARGET CartesioCAD POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:CartesioCAD> ${deploy_args}
            COMMENT "Copia delle DLL accanto a CartesioCAD.exe"
            VERBATIM)

        install(TARGETS CartesioCAD cartesio-mcp RUNTIME DESTINATION .)
        install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\"
            \"-DEXE=\${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:CartesioCAD>\"
            \"-DPLATFORM_PLUGIN=$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>\"
            \"-DTLS_PLUGIN=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/../tls/qschannelbackend.dll\"
            \"-DSEARCH_DIRS=$<TARGET_FILE_DIR:Qt6::Core>;${compiler_bin}\"
            \"-DOBJDUMP=${CMAKE_OBJDUMP}\"
            -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake\"
            COMMAND_ERROR_IS_FATAL ANY)")

        # Licenze: LICENSE di CartesioCAD, testi e avvisi dei componenti copiati (pacchetti MSYS2, LibreDWG,
        # CanaloCAD) e LICENZE-TERZE-PARTI.txt con versioni e indirizzi dei sorgenti. Senza, la cartella non
        # si può distribuire: se manca pacman l'installazione si ferma.
        get_filename_component(msys_root "${compiler_bin}/../.." ABSOLUTE)
        install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E env MSYS2_ROOT= \"${msys_root}/usr/bin/bash.exe\"
            \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/collect-licenses.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
            COMMAND_ERROR_IS_FATAL ANY)")
    elseif(UNIX AND NOT APPLE)
        # Linux (pacchetto Flatpak, packaging/flatpak): il programma con i suoi file in lib/cartesiocad e un
        # collegamento in bin; voce di menu, icona e scheda del programma nelle cartelle standard.
        set(CARTESIO_APP_ID "io.github.samudipi.CartesioCAD")
        install(TARGETS CartesioCAD RUNTIME DESTINATION ${CARTESIO_APP_DEST})
        install(FILES LICENSE DESTINATION ${CARTESIO_APP_DEST} RENAME LICENSE.txt)
        install(FILES packaging/linux/LICENZE-TERZE-PARTI.txt DESTINATION ${CARTESIO_APP_DEST})
        install(FILES packaging/linux/licenze/Apache-2.0.txt packaging/linux/licenze/MIT.txt
                DESTINATION ${CARTESIO_APP_DEST}/licenze/testi)
        install(FILES packaging/linux/${CARTESIO_APP_ID}.desktop DESTINATION share/applications)
        install(FILES packaging/linux/${CARTESIO_APP_ID}.metainfo.xml DESTINATION share/metainfo)
        install(FILES src/app/icons/cartesiocad.png DESTINATION share/icons/hicolor/256x256/apps
                RENAME ${CARTESIO_APP_ID}.png)
        install(CODE "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ../${CARTESIO_APP_DEST}/CartesioCAD
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/cartesiocad\" COMMAND_ERROR_IS_FATAL ANY)")
    endif()
endif()

# --- Test ---
if(CARTESIO_BUILD_TESTS)
    enable_testing()
    add_executable(cartesio_core_tests tests/core_tests.cpp)
    target_link_libraries(cartesio_core_tests PRIVATE cartesio_core ZLIB::ZLIB)
    target_compile_definitions(cartesio_core_tests PRIVATE CARTESIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME core_tests COMMAND cartesio_core_tests)

    # Motore di CanaloCAD: tavola DXF identica a quella della pagina (tests/canalo_dxf).
    if(TARGET cartesio_canalo_engine)
        add_executable(cartesio_canalo_engine_tests tests/canalo_engine_tests.cpp)
        target_link_libraries(cartesio_canalo_engine_tests PRIVATE cartesio_canalo_engine)
        target_compile_definitions(cartesio_canalo_engine_tests PRIVATE CARTESIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME canalo_engine_tests COMMAND cartesio_canalo_engine_tests)
    endif()

    # Prove con file malformati (tests/fuzz.cpp): da compilare a parte, con i controlli dei limiti, in una cartella
    # di compilazione apposta (cmake -DCARTESIO_FUZZ=ON -DCMAKE_CXX_FLAGS="-D_GLIBCXX_ASSERTIONS ...").
    option(CARTESIO_FUZZ "Programma di prova con file malformati" OFF)
    if(CARTESIO_FUZZ)
        add_executable(cartesio_fuzz tests/fuzz.cpp)
        target_link_libraries(cartesio_fuzz PRIVATE cartesio_core)
    endif()

    add_executable(cartesio_dxfinfo tests/dxf_info.cpp)
    target_link_libraries(cartesio_dxfinfo PRIVATE cartesio_core)

    if(POPPLER_CPP_FOUND)
        add_executable(cartesio_pdfcircuits tests/pdf_circuits.cpp src/app/PdfWords.cpp)
        target_link_libraries(cartesio_pdfcircuits PRIVATE cartesio_core PkgConfig::POPPLER_CPP)
        target_compile_definitions(cartesio_pdfcircuits PRIVATE CARTESIO_HAS_POPPLER)
    endif()
endif()
