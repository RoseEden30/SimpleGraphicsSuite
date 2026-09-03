# Deploys the default settings file once, on first install, without ever
# overwriting one a player (or a dev testing in game) has already tweaked.
if(NOT EXISTS "${DST}")
    file(COPY_FILE "${SRC}" "${DST}")
    message(STATUS "Copied default settings to ${DST}")
endif()
