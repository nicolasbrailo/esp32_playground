file(GLOB_RECURSE srcs "*.c")
file(GLOB_RECURSE provision_www "wifi_provision_www/*")
idf_component_register(SRCS "${srcs}"
                       PRIV_REQUIRES esp_wifi wpa_supplicant nvs_flash esp_http_server esp_netif esp_event lwip
                                     esp_driver_gpio esp_timer
                       INCLUDE_DIRS "."
                       EMBED_FILES "${provision_www}")

# Re-attach every transitive dep's includes as SYSTEM on this component, so
# GCC treats IDF / FreeRTOS / lwip / etc headers as system headers and skips
# warnings inside them. Without this, -Wextra etc. would fire on macros and
# code we don't control. Visited set guards against cycles in the IDF dep graph.
function(_absorb_dep_includes_as_system tgt out_var)
    set(_result "")
    get_property(_visited GLOBAL PROPERTY _absorb_visited)
    if(NOT TARGET ${tgt} OR "${tgt}" IN_LIST _visited)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY _absorb_visited "${tgt}")

    get_target_property(_deps ${tgt} INTERFACE_LINK_LIBRARIES)
    if(_deps)
        foreach(_d IN LISTS _deps)
            # Strip $<...> generator expressions (IDF wraps some deps).
            string(REGEX REPLACE "\\$<[^:>]*:([^>]+)>" "\\1" _d "${_d}")
            if(TARGET ${_d})
                get_target_property(_incs ${_d} INTERFACE_INCLUDE_DIRECTORIES)
                if(_incs)
                    list(APPEND _result ${_incs})
                endif()
                _absorb_dep_includes_as_system(${_d} _sub)
                list(APPEND _result ${_sub})
            endif()
        endforeach()
    endif()
    if(_result)
        list(REMOVE_DUPLICATES _result)
    endif()
    set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

set_property(GLOBAL PROPERTY _absorb_visited "")
_absorb_dep_includes_as_system(${COMPONENT_LIB} _sys_incs)
target_include_directories(${COMPONENT_LIB} SYSTEM PRIVATE ${_sys_incs})

# Hardened warnings — scoped to this component only so the ESP-IDF tree keeps
# building. If a flag fires on something you don't want to fix, drop it or
# silence the specific call with #pragma GCC diagnostic.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall                       # baseline (IDF already sets this)
    -Wextra                     # unused params, sign-compare, missing-field-init, ...
    -Werror                     # warnings become errors
    -Wformat=2                  # catches ESP_LOGI(TAG, "NVS=", x) — mismatched args
    -Wshadow                    # variable shadowing
    -Wundef                     # #if on undefined macros
    -Wnull-dereference          # provable null derefs
    -Wdouble-promotion          # accidental float -> double
    -Wstrict-prototypes         # `f()` vs `f(void)`
    -Wmissing-prototypes        # static helpers without a prior declaration
)
