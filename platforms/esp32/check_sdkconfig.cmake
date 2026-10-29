# Fails the configure when the sdkconfig ESP-IDF builds from disagrees with a
# line in the defaults files. A saved sdkconfig wins over sdkconfig.defaults,
# so a default changed after the file was first generated never reaches the
# build: a checkout configured on 2026-10-27 kept app rollback off and the
# pre-e19c1b4 WiFi/TLS buffers long after the defaults changed, and its
# bootloader never moved an OTA image to PENDING_VERIFY.
#
# platforms/esp32/CMakeLists.txt runs it after project(), when kconfgen has
# rewritten the sdkconfig from the saved file and the defaults, so a fresh
# configure always passes. It also runs as a script:
#
#   cmake -DSDKCONFIG=<file> "-DSDKCONFIG_DEFAULTS=<a>;<b>" -P check_sdkconfig.cmake
#
# Configure with -DMOTO_ALLOW_SDKCONFIG_DRIFT=ON to keep a deliberate local
# override; the mismatch is then only a warning.

# Reads `CONFIG_X=v` and `# CONFIG_X is not set` (as `n`) into
# <prefix>_<key>, appending each key to <prefix>_keys. A macro, so the
# values land in the caller's scope.
macro(_moto_read_kconfig_file file prefix)
  file(STRINGS "${file}" _moto_lines
       REGEX "^(CONFIG_[A-Za-z0-9_]+=|# CONFIG_[A-Za-z0-9_]+ is not set$)")
  foreach(_moto_line IN LISTS _moto_lines)
    if(_moto_line MATCHES "^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
      set(${prefix}_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
      list(APPEND ${prefix}_keys "${CMAKE_MATCH_1}")
    elseif(_moto_line MATCHES "^# (CONFIG_[A-Za-z0-9_]+) is not set$")
      set(${prefix}_${CMAKE_MATCH_1} "n")
      list(APPEND ${prefix}_keys "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endmacro()

function(moto_check_sdkconfig sdkconfig defaults)
  if(NOT EXISTS "${sdkconfig}")
    message(FATAL_ERROR "sdkconfig not found: ${sdkconfig}")
  endif()

  # Later files win, as in kconfgen, and each may have a .<target> companion.
  set(want_keys "")
  foreach(file IN LISTS defaults)
    _moto_read_kconfig_file("${file}" want)
    if(IDF_TARGET AND EXISTS "${file}.${IDF_TARGET}")
      _moto_read_kconfig_file("${file}.${IDF_TARGET}" want)
    endif()
  endforeach()
  list(REMOVE_DUPLICATES want_keys)

  set(have_keys "")
  _moto_read_kconfig_file("${sdkconfig}" have)

  set(problems "")
  foreach(key IN LISTS want_keys)
    set(want "${want_${key}}")
    if(DEFINED have_${key})
      set(have "${have_${key}}")
    elseif(want STREQUAL "n")
      # An invisible bool is off, which is what the default asked for.
      continue()
    else()
      set(have "<absent>")
    endif()
    if(NOT have STREQUAL want)
      list(APPEND problems "  ${key}: defaults ${want}, sdkconfig ${have}")
    endif()
  endforeach()
  if(NOT problems)
    return()
  endif()

  list(JOIN problems "\n" detail)
  set(text
      "${sdkconfig} disagrees with the defaults files:\n${detail}\n"
      "A saved sdkconfig wins over the defaults, so these lines never reach "
      "this build. Move it aside and configure again:\n"
      "  mv '${sdkconfig}' '${sdkconfig}.old'\n"
      "To keep a deliberate local override, configure with "
      "-DMOTO_ALLOW_SDKCONFIG_DRIFT=ON.")
  if(MOTO_ALLOW_SDKCONFIG_DRIFT)
    message(WARNING ${text})
  else()
    message(FATAL_ERROR ${text})
  endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  moto_check_sdkconfig("${SDKCONFIG}" "${SDKCONFIG_DEFAULTS}")
endif()
