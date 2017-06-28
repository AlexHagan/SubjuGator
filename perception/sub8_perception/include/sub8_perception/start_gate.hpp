#pragma once

#include <iostream>
#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

#include <cv_bridge/cv_bridge.h>
#include <image_geometry/pinhole_camera_model.h>
#include <image_transport/image_transport.h>
#include <ros/ros.h>
#include <std_srvs/SetBool.h>

#include <mil_vision_lib/cv_tools.hpp>

#include <eigen_conversions/eigen_msg.h>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <Eigen/StdVector>

#include <boost/accumulators/accumulators.hpp>
#include <boost/circular_buffer.hpp>

#include <visualization_msgs/Marker.h>

class Sub8StartGateDetector
{
public:
  Sub8StartGateDetector();
  mil_vision::ImageWithCameraInfo left_most_recent, right_most_recent;

private:
  // Combinations of k elements from a set of size n (indexes)
  void combinations(uint8_t n, uint8_t k, std::vector<std::vector<uint8_t>> &idx_array);
  void _increase_elements_after_level(std::vector<uint8_t> comb, std::vector<std::vector<uint8_t>> &comb_array,
                                      uint8_t n, uint8_t k, uint8_t level);

  void left_image_callback(const sensor_msgs::ImageConstPtr &image_msg_ptr,
                           const sensor_msgs::CameraInfoConstPtr &info_msg_ptr);
  void right_image_callback(const sensor_msgs::ImageConstPtr &image_msg_ptr,
                            const sensor_msgs::CameraInfoConstPtr &info_msg_ptr);
  bool set_active_enable_cb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res);
  void run();

  // Main algorithm that gets left and right image pointers and finds gate in 3D
  void determine_start_gate_position();
  // Helper function that returns the angle between 2 lines
  double get_angle(cv::Point a, cv::Point b, cv::Point c);
  // Helper function that checks if a contour is of a gate shape
  bool valid_contour(std::vector<cv::Point> &contour);
  // Helper function that does a blur and filters
  cv::Mat process_image(cv::Mat &image);

  // Given an array of contours, returns a polygon that is most similar to that of a gate
  std::vector<cv::Point> contour_to_2d_features(std::vector<std::vector<cv::Point>> &contour);
  // Given a set of points, find the center points between the closest point pairs
  std::vector<cv::Point> get_corner_center_points(const std::vector<cv::Point> &features);
  // Distance-based stereo matching
  std::vector<int> shortest_pair_stereo_matching(const std::vector<cv::Point> &features_l,
                                                 const std::vector<cv::Point> &features_r, int y_axis_diff_thresh = 0);
  // Finds a plane
  std::vector<double> best_fit_plane_standard(const std::vector<Eigen::Vector3d> &feature_pts_3d);

  // Some visualization
  void visualize_3d_reconstruction(const std::vector<Eigen::Vector3d> &feature_pts_3d, cv::Matx34d left_cam_mat,
                                   cv::Matx34d right_cam_mat, cv::Mat &current_left_right,
                                   cv::Mat &current_image_right);
  void visualize_3d_points_rviz(const std::vector<Eigen::Vector3d> &feature_pts_3d,
                                const std::vector<Eigen::Vector3d> &proj_pts);

  ros::NodeHandle nh;
  image_transport::CameraSubscriber left_image_sub_, right_image_sub_;
  image_transport::ImageTransport image_transport_;
  image_transport::Publisher debug_image_pub_left_;
  image_transport::Publisher debug_image_pub_right_;
  image_transport::Publisher debug_image_pub_canny_;
  image_geometry::PinholeCameraModel left_cam_model_, right_cam_model_;

  // To prevent invalid img pointers from being passed to toCvCopy (segfault)
  boost::mutex left_mtx_, right_mtx_;

  // Should there be processing to find the gate
  bool active_;

  // How far off in time can left and right cameras be off
  double sync_thresh_;

  // Publish marker visualization as well as the normal approximation
  ros::Publisher marker_pub_;
  ros::Publisher center_gate_pub_;
  ros::Publisher normal_gate_pub_;

  // Toggle to run the vision
  ros::ServiceServer active_service_;

  // Some filtering params used by the 'process_image' function
  int canny_low_;
  float canny_ratio_;
  int blur_size_;
  int dilate_amount_;
};