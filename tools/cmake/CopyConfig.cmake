# Preserve user settings across rebuilds.
if(NOT EXISTS "${OUTPUT}")
	configure_file("${INPUT}" "${OUTPUT}" COPYONLY)
endif()
