# generated from catkin/cmake/template/pkg.context.pc.in
CATKIN_PACKAGE_PREFIX = ""
PROJECT_PKG_CONFIG_INCLUDE_DIRS = "${prefix}/include".split(';') if "${prefix}/include" != "" else []
PROJECT_CATKIN_DEPENDS = "roscpp;actionlib;tf;tf2;tf2_ros;tf2_geometry_msgs;trajectory_msgs;tfr_msgs;message_runtime;sensor_msgs;geometry_msgs;nav_msgs".replace(';', ' ')
PKG_CONFIG_LIBRARIES_WITH_PREFIX = "-lstatus_code;-ltf_manipulator;-lstatus_publisher;-larm_manipulator".split(';') if "-lstatus_code;-ltf_manipulator;-lstatus_publisher;-larm_manipulator" != "" else []
PROJECT_NAME = "tfr_utilities"
PROJECT_SPACE_DIR = "/home/phillipov/NasaRmc2019/install"
PROJECT_VERSION = "0.0.0"
