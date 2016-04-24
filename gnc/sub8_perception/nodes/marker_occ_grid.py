#!/usr/bin/env python
from __future__ import division

import rospy
import tf
from std_msgs.msg import Header
from geometry_msgs.msg import Pose, Point, Quaternion, Pose2D
from nav_msgs.msg import OccupancyGrid, MapMetaData
from sub8_msgs.srv import VisionRequest2D, VisionRequest2DResponse
from image_geometry import PinholeCameraModel
from sub8_ros_tools import threading_helpers

import cv2
import numpy as np
import threading


def unit_vector(vect):
    return vect / np.linalg.norm(vect)


def make_2D_rotation(angle):
        c, s = np.cos(angle), np.sin(angle)
        return np.array([[c, -s],
                         [s, c]], dtype=np.float32)


class OccGridUtils(object):
    '''
    Contains functions for dealing with occupancy grids as well as storing and publishing them.

    All distance measured in meters.
    '''
    def __init__(self, res, width, height, starting_pose, topic_name='/search_grid'):
        self.meta_data = MapMetaData()
        # Resolution is m/cell. Width is X, height is Y.
        self.meta_data.resolution = res  # rospy.get_param("map_resolution")
        self.meta_data.width = width  # rospy.get_param("map_width")
        self.meta_data.height = height  # rospy.get_param("map_height")

        # Starting Position
        self.mid_x = -starting_pose.x
        self.mid_y = -starting_pose.y

        self.meta_data.origin = Pose(position=Point(x=-starting_pose.x * res, y=-starting_pose.y * res, z=0),
                                     orientation=Quaternion(*tf.transformations.quaternion_from_euler(0, 0, starting_pose.theta)))

        # Create array of -1's of the correct size
        self.occ_grid = np.zeros((self.meta_data.height, self.meta_data.width)) - 1
        self.searched = np.zeros((self.meta_data.height, self.meta_data.width))
        self.trenches = np.zeros((self.meta_data.height, self.meta_data.width))

        self.occ_grid_pub = rospy.Publisher(topic_name, OccupancyGrid, queue_size=1)

    def add_circle(self, center, radius):
        '''
        Adds a circle to the grid.
        Also used to project the camera's view onto the grid but is rotationally intolerant.
        '''
        center_offset = np.array(center) / self.meta_data.resolution - np.array([self.mid_x, self.mid_y])

        # Create blank canvas the size of the circle
        radius = int(radius / self.meta_data.resolution)

        cv2.circle(self.searched, tuple(center_offset.astype(np.int8)), radius, 1, -1)

    def found_marker(self, pose_2d):
        '''
        Used to mark found trenches.
        It doesn't matter that this isn't perfect, we just need to know that something is there.
        '''
        TRENCH_LENGTH = 1.2 / self.meta_data.resolution  # cells (3 ft)
        TRENCH_WIDTH = .1524 / self.meta_data.resolution  # cells (6 inches)

        center = np.array([pose_2d.x, pose_2d.y])  # The negative all depends on how the center is returned
        rotation = -pose_2d.theta

        center_offset = center / self.meta_data.resolution - np.array([self.mid_x, self.mid_y])

        rot_top_point = np.dot(np.array([TRENCH_LENGTH, 0]) / 2, make_2D_rotation(rotation)).astype(np.int8)
        rot_bottom_point = np.dot(-np.array([TRENCH_LENGTH, 0]) / 2, make_2D_rotation(rotation)).astype(np.int8)

        pos_top_point = np.int0(rot_top_point + center_offset)
        pos_bottom_point = np.int0(rot_bottom_point + center_offset)

        cv2.line(self.trenches, tuple(pos_top_point), tuple(pos_bottom_point), 101, int(TRENCH_WIDTH))

    def publish_grid(self):
        '''
        Take the occupancy grid and send it out over ros with timestamps and whatnot.
        '''
        t = rospy.Time.now()
        header = Header(stamp=t, frame_id='/map')
        # Populate occ grid msg
        occ_msg = OccupancyGrid()
        occ_msg.header = header
        occ_msg.info = self.meta_data
        # Make sure values don't go out of range
        occ_grid = self.occ_grid + self.searched + self.trenches
        occ_msg.data = np.clip(occ_grid.flatten(), -1, 100)
        self.occ_grid_pub.publish(occ_msg)


class MarkerOccGrid(OccGridUtils):
    '''
    Handles updating occupancy grid when new data comes in.
    TODO: Upon call can return some path to go to in order to find them.
    '''
    def __init__(self, image_sub, grid_res, grid_width, grid_height, grid_starting_pose):
        super(self.__class__, self).__init__(res=grid_res, width=grid_width, height=grid_height, starting_pose=grid_starting_pose)

        self.tf_listener = tf.TransformListener()

        self.cam = PinholeCameraModel()
        self.camera_info = image_sub.wait_for_camera_info()
        if self.camera_info is None:
            # Maybe raise an alarm here.
            rospy.logerr("I don't know what to do without my camera info.")

        self.cam.fromCameraInfo(self.camera_info)

    def update_grid(self):
        '''
        Takes marker information to update occupacy grid.
        '''
        x_y_position, height = self.get_tf()

        self.add_circle(x_y_position, self.calculate_visual_radius(height))
        self.publish_grid()

    def add_marker(self, marker):
        '''
        Find the actual 3d pose of the marker and fill in the occupancy grid for that pose.
        This works by:
            1. Calculate unit vector between marker point and the image center in the image frame.
            2. Use height measurement to find real life distance (m) between center point and marker center.
            3. Use unit vec and magnitude to find dx and dy in meters.
            3. Pass info to OccGridUtils.
        '''
        if marker is None:
            return

        x_y_position, height = self.get_tf()
        dir_vector = unit_vector(np.array([self.cam.cx(), self.cam.cy()] - marker[0]))
        magnitude = self.calculate_visual_radius(height, second_point=marker[0])
        local_position = dir_vector * magnitude
        position = local_position + x_y_position

        # print dir_vector[::-1]

        # Pose on ground plane from center
        pose = Pose2D(x=position[0], y=position[1], theta=marker[1])
        # print pose
        self.found_marker(pose)

    def get_tf(self):
        self.tf_listener.waitForTransform("/map", "/downward", rospy.Time(), rospy.Duration(1.0))
        trans, rot = self.tf_listener.lookupTransform("/map", "/downward", rospy.Time())
        x_y_position = trans[:2]
        trans, rot = self.tf_listener.lookupTransform("/ground", "/downward", rospy.Time())
        height = trans[2]

        return x_y_position, height

    def calculate_visual_radius(self, height, second_point=None):
        '''
        Draws rays to find the radius of the FOV of the camera in meters.
        It also works to find the distance between two planar points some distance from the camera.
        '''

        mid_ray = unit_vector(self.cam.projectPixelTo3dRay((self.cam.cx(), self.cam.cy())))

        if second_point is None:
            second_point = np.array([0, min(self.camera_info.height, self.camera_info.width) / 2])
        edge_ray = unit_vector(self.cam.projectPixelTo3dRay(second_point))

        # Calculate angle between vectors and use that to find r
        theta = np.arccos(np.dot(mid_ray, edge_ray))
        return np.tan(theta) * height


if __name__ == "__main__":
    rospy.init_node('searcher')
    tr = MarkerOccGrid(res=.1, width=100, height=500, starting_pose=Pose2D(x=50, y=50, theta=0))
    rospy.spin()
