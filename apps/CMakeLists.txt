foreach(tool hello_pubsub sensor_publisher logger_subscriber dds_spy lossy_link_demo)
  add_executable(${tool} ${tool}.cpp)
  target_link_libraries(${tool} PRIVATE ldds_app_common)
endforeach()
