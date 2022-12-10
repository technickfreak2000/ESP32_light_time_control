idf_component_register(SRCS "softap_example_main.c" "dns_server.c"
                    INCLUDE_DIRS "include"
                    EMBED_FILES root.html style.css script.js)

# ULP support additions to component CMakeLists.txt.
# set(ulp_app_name ulp_${COMPONENT_NAME})
# set(ulp_s_sources "ulp/ulp.S")
# set(ulp_exp_dep_srcs "softap_example_main.c")

# ulp_embed_binary(${ulp_app_name} ${ulp_s_sources} ${ulp_exp_dep_srcs})