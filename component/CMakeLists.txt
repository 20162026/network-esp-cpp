cmake_minimum_required(VERSION 3.5)

set(srcs "pl_network_types.cpp" "pl_network_stream.cpp" 
"pl_network_interface.cpp" "pl_esp_network_interface.cpp" "pl_esp_ethernet.cpp" "pl_esp_wifi_station.cpp"
"pl_tcp_client.cpp" "pl_tcp_server.cpp" )
add_prefix(srcs "${CMAKE_CURRENT_LIST_DIR}/" ${srcs})

idf_component_register( SRCS ${srcs} 
                        INCLUDE_DIRS "include" 
                        REQUIRES esp_netif esp_eth esp_wifi pl_common)