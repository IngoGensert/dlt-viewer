add_library(messageidplugin MODULE
    messageidplugin.cpp)

target_link_libraries(messageidplugin qdlt ${QT_PREFIX}::Widgets)

add_plugin(messageidplugin)
