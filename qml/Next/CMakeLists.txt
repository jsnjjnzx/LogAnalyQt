qt_add_library(nextUi STATIC)
set_source_files_properties(Icons.qml PlatformFonts.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(nextUi
    URI LogAnalyQt.Next
    VERSION 1.0
    PLUGIN_TARGET nextUiPlugin
    CLASS_NAME NextUiPlugin
    QML_FILES
        Main.qml GlassTheme.qml GlassSurface.qml GlassPage.qml Navigation.qml CompactSpinBox.qml AiAssistantPage.qml SystemSettingsPage.qml
        ConfigMigrationWindow.qml PersonalConfigPage.qml
        MqttPage.qml MqttConnectionDialog.qml MqttBrokerDialog.qml
        ControlSurface.qml GlassButton.qml GlassTextField.qml GlassComboBox.qml GlassCheckBox.qml GlassSwitch.qml
        GlassMenu.qml GlassDialog.qml
        TitleBar.qml Icons.qml PlatformFonts.qml
        LiveLogDialog.qml LogMergeDialog.qml ProtocolSyncDialog.qml SmartFramingDialog.qml
        SmartFraming/ProtocolSelector.qml SmartFraming/HexPreview.qml
        SmartFraming/FrameHeaderPanel.qml SmartFraming/AppMsgHeaderPanel.qml
        SmartFraming/AppMsgFramingPage.qml SmartFraming/OadPicker.qml
        SmartFraming/OadSelector.qml SmartFraming/FieldEditor.qml
        SmartFraming/RsdEditor.qml SmartFraming/RcsdEditor.qml
        SmartFraming/ServiceSelector.qml SmartFraming/ServiceTreeEditor.qml
    RESOURCES assets/glass-background.png assets/app-logo.png
)
target_link_libraries(nextUi PRIVATE Qt6::Quick Qt6::QuickControls2)
