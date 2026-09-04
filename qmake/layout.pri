include($$PWD/version.pri)

# Keep generated compiler and Qt files inside the shadow build.
OMAHOUSE_OUTPUT_DIR = $$clean_path($$OUT_PWD)
OMAHOUSE_PROJECT_DIR = $$clean_path($$OMAHOUSE_PROJECT_DIR)
OMAHOUSE_SOURCE_ROOT = $$clean_path($$OMAHOUSE_SOURCE_ROOT)

equals(OMAHOUSE_OUTPUT_DIR, $$OMAHOUSE_SOURCE_ROOT)|equals(OMAHOUSE_OUTPUT_DIR, $$OMAHOUSE_PROJECT_DIR) {
    error("Building in the source tree is not supported. Use mise run build.")
}

OBJECTS_DIR = $$OUT_PWD/.qmake/obj
MOC_DIR = $$OUT_PWD/.qmake/moc
RCC_DIR = $$OUT_PWD/.qmake/rcc
UI_DIR = $$OUT_PWD/.qmake/ui
