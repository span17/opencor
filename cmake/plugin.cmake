INCLUDE(${CMAKE_FILES_DIR}/common.cmake)

MACRO(INITIALISE_PLUGIN TARGET_NAME HAS_RESOURCES)
    SET(PLUGIN_NAME ${TARGET_NAME})

    SET(SOURCES)
    SET(HEADERS)
    SET(HEADERS_MOC)
    SET(UIS)

    IF(${HAS_RESOURCES})
        SET(RESOURCES
            res/${PLUGIN_NAME}.qrc
        )
    ELSE()
        SET(RESOURCES)
    ENDIF()
ENDMACRO()

MACRO(BUILD_PLUGIN)
    # Update the translation (.ts) files and generate the language (.qm) files
    # which will later on be embedded in the plugin itself

    IF(NOT "${RESOURCES}" STREQUAL "")
        UPDATE_LANGUAGE_FILES(${PLUGIN_NAME})
    ENDIF()

    # Rules to build the plugin

    IF("${HEADERS_MOC}" STREQUAL "")
        SET(SOURCES_MOC)
    ELSE()
        QT4_WRAP_CPP(SOURCES_MOC ${HEADERS_MOC})
    ENDIF()

    IF("${UIS}" STREQUAL "")
        SET(SOURCES_UIS)
    ELSE()
        QT4_WRAP_UI(SOURCES_UIS ${UIS})
    ENDIF()

    IF("${RESOURCES}" STREQUAL "")
        SET(SOURCES_RCS)
    ELSE()
        QT4_ADD_RESOURCES(SOURCES_RCS ${RESOURCES})
    ENDIF()

    ADD_LIBRARY(${PROJECT_NAME} SHARED
        ${SOURCES}
        ${SOURCES_MOC}
        ${SOURCES_UIS}
        ${SOURCES_RCS}
    )

    TARGET_LINK_LIBRARIES(${PROJECT_NAME}
        ${ARGN}
    )

    # Linker settings
    # Note: by default "lib" will be prepended to the name of the target file.
    #       However, this is not common practice on Windows, so...

    IF(WIN32)
        SET_TARGET_PROPERTIES(${PROJECT_NAME}
            PROPERTIES PREFIX ""
        )
    ENDIF()

    SET_TARGET_PROPERTIES(${PROJECT_NAME}
        PROPERTIES OUTPUT_NAME ${PLUGIN_NAME} LINK_FLAGS "${LINK_FLAGS_PROPERTIES}"
    )
ENDMACRO()

MACRO(PACKAGE_PLUGIN)
    #---GRY---

    # Deployed location:
    # ------------------
    # The plugin is currently deployed in the same location as third-party
    # libraries, since OpenCOR doesn't currently support plugins as such.
    # However, once it does, the plugin will have to be deployed in its correct
    # location, i.e. the "plugins" folder. For the time being, though, the
    # current approach allows to test things, so...

    # Translation files:
    # ------------------
    # There is currently no 'proper' support for the internationalisation of
    # the plugin. This is therefore something that will have to be added at
    # some point.

    IF(WIN32)
#        INSTALL(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin/plugins)
        INSTALL(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
    ELSEIF(NOT APPLE)
#        INSTALL(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION plugins)
        INSTALL(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
    ENDIF()

MESSAGE("")
MESSAGE("********* WARNING **********")
MESSAGE("Call to PACKAGE_PLUGIN (in [OpenCOR]/cmake/plugin.cmake) for the ${PROJECT_NAME} plugin: please note that the plugin is not currently being deployed in the right location. Also, there is currently no 'proper' support for the internationalisation of the plugin.")
MESSAGE("****************************")
MESSAGE("")
ENDMACRO()
